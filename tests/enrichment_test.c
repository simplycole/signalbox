#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enrichment.h"
#include "modal_state.h"
#include "mouse_state.h"
#include "platform.h"
#include "upcoming_layout.h"

static void testNormalization (void) {
	char text[128];
	SbTrackNormalize ("  The ARTIST -- Live!  ", text, sizeof (text));
	assert (strcmp (text, "the artist live") == 0);
}

static void testRetainedModalScroll (void) {
	SbUiModalScrollState state;
	SbUiModalScrollOpen (&state, 7);
	SbUiModalScrollClamp (&state, 40, 10);
	SbUiModalScrollLines (&state, 1);
	assert (state.offset == 1);
	/* Ordinary redraws and same-content regeneration preserve the offset. */
	SbUiModalScrollObserveIdentity (&state, 7);
	SbUiModalScrollClamp (&state, 40, 10);
	assert (state.offset == 1);
	/* A content change clamps only when the old offset is no longer valid. */
	state.offset = 20;
	SbUiModalScrollClamp (&state, 15, 10);
	assert (state.offset == 5);
	SbUiModalScrollPage (&state, -1, 10);
	assert (state.offset == 0);
	SbUiModalScrollPage (&state, 1, 10);
	assert (state.offset == 5);
	SbUiModalScrollLines (&state, 1); /* Arrow and wheel share this bounded path. */
	assert (state.offset == 5);
	state.offset = 3;
	SbUiModalScrollWheel (&state, -1);
	assert (state.offset == 0); /* One wheel-up event: three bounded lines. */
	SbUiModalScrollWheel (&state, 1);
	assert (state.offset == 3); /* One wheel-down event: three bounded lines. */
	SbUiModalScrollObserveIdentity (&state, 8);
	assert (state.offset == 0 && state.maximum == 0);
}

static void testRetainedModalScrollBounds (void) {
	SbUiModalScrollState state;
	SbUiModalScrollOpen (&state, 11);
	SbUiModalScrollClamp (&state, 23, 7);
	assert (state.maximum == 16); /* max (0, content - visible) */

	for (size_t event = 0; event < 20; event++)
		SbUiModalScrollWheel (&state, 1);
	assert (state.offset == state.maximum);

	state.offset = state.maximum - 1;
	SbUiModalScrollWheel (&state, 1);
	assert (state.offset == state.maximum);

	for (size_t event = 0; event < 20; event++)
		SbUiModalScrollWheel (&state, -1);
	assert (state.offset == 0);

	/* Keyboard and wheel are bounded by the same retained maximum. */
	state.offset = state.maximum;
	SbUiModalScrollLines (&state, 1);
	assert (state.offset == state.maximum);
	SbUiModalScrollWheel (&state, 1);
	assert (state.offset == state.maximum);
	state.offset = 0;
	SbUiModalScrollLines (&state, -1);
	assert (state.offset == 0);
	SbUiModalScrollWheel (&state, -1);
	assert (state.offset == 0);

	SbUiModalScrollClamp (&state, 4, 7);
	assert (state.maximum == 0 && state.offset == 0);
}

static void testCursesWheelStateMapping (void) {
	const uint64_t up = UINT64_C (1) << 20;
	const uint64_t down = UINT64_C (1) << 21;
	const uint64_t legacyDown = UINT64_C (1) << 6;
	const uint64_t reportPosition = UINT64_C (1) << 31;
	assert (SbUiMouseWheelDirection (up, up, down, 0, reportPosition, 2) == -1);
	assert (SbUiMouseWheelDirection (down, up, down, 0, reportPosition, 2) == 1);
	/* A compatibility form must carry its complete distinguishing context. */
	assert (SbUiMouseWheelDirection (legacyDown, up, 0, legacyDown,
			reportPosition, 1) == 0); /* A physical middle click is not a wheel. */
	assert (SbUiMouseWheelDirection (legacyDown | reportPosition,
			up, 0, legacyDown, reportPosition, 1) == 1);
	assert (SbUiMouseWheelDirection (legacyDown | reportPosition,
			up, 0, legacyDown, reportPosition, 2) == 0);
	assert (SbUiMouseWheelDirection (up | down, up, down, 0,
			reportPosition, 2) == 0);
	assert (SbUiMouseWheelDirection (0, up, down, legacyDown,
			reportPosition, 1) == 0);
	/* Actual local ncurses ABI v1 values observed on macOS. */
	assert (SbUiMouseWheelDirection (UINT64_C (0x80000),
			UINT64_C (0x80000), 0, UINT64_C (0x80),
			UINT64_C (0x8000000), 1) ==
			SB_UI_SCROLL_TOWARD_TOP);
	/* BUTTON1_CLICKED is ambiguous and must remain a click, even in a modal. */
	assert (SbUiMouseWheelDirection (UINT64_C (0x4),
			UINT64_C (0x80000), 0, UINT64_C (0x80),
			UINT64_C (0x8000000), 1) == 0);
	assert (SbUiMouseWheelOwnedByModal (1, SB_UI_SCROLL_TOWARD_TOP));
	assert (SbUiMouseWheelOwnedByModal (1, SB_UI_SCROLL_TOWARD_BOTTOM));
	assert (!SbUiMouseWheelOwnedByModal (1, 0));
	assert (!SbUiMouseWheelOwnedByModal (0, SB_UI_SCROLL_TOWARD_TOP));
	assert (SbUiMouseWheelFromNativeDelta (120) == SB_UI_SCROLL_TOWARD_TOP);
	assert (SbUiMouseWheelFromNativeDelta (-120) == SB_UI_SCROLL_TOWARD_BOTTOM);
}

static void testMouseBitNames (void) {
	char names[80] = "";
	SbUiMouseBitName (names, sizeof (names), UINT64_C (0x80000),
			UINT64_C (0x2), "BUTTON1_PRESSED");
	SbUiMouseBitName (names, sizeof (names), UINT64_C (0x80000),
			UINT64_C (0x80000), "BUTTON4_PRESSED");
	assert (strcmp (names, "BUTTON4_PRESSED") == 0);
	names[0] = '\0';
	SbUiMouseBitName (names, sizeof (names), UINT64_C (0x4),
			UINT64_C (0x4), "BUTTON1_CLICKED");
	assert (strcmp (names, "BUTTON1_CLICKED") == 0);
}

static void testUpcomingHeight (void) {
	const int single[6] = {1, 1, 1, 1, 1, 1};
	const int wrapped[6] = {2, 1, 2, 1, 1, 1};
	assert (SbUiUpcomingHeight (single, 6, 20, 6) == 6);
	assert (SbUiUpcomingHeight (wrapped, 6, 20, 6) == 8);
	assert (SbUiUpcomingHeight (single, 6, 11, 6) == 3);
	assert (SbUiUpcomingHeight (wrapped, 6, 11, 6) == 3);
	assert (SbUiUpcomingHeight (single, 6, 8, 6) == 0);
}

static void testMusicBrainzHttpStates (void) {
	assert (SbMusicBrainzHttpStatus (503, 0) == SB_LOOKUP_UNAVAILABLE);
	assert (SbMusicBrainzHttpStatus (500, 0) == SB_LOOKUP_ERROR);
	assert (SbMusicBrainzHttpStatus (200, 0) == SB_LOOKUP_LOADING);
	assert (SbMusicBrainzHttpStatus (503, 7) == SB_LOOKUP_ERROR);
	assert (SbMusicBrainzShouldRetry (503, 0, 1));
	assert (!SbMusicBrainzShouldRetry (503, 0, 2));
	assert (!SbMusicBrainzShouldRetry (500, 0, 1));
	assert (!SbMusicBrainzShouldRetry (503, 7, 1));
	/* A successful bounded second response proceeds to normal parsing. */
	assert (SbMusicBrainzHttpStatus (200, 0) == SB_LOOKUP_LOADING);
}

static void testLrclibTransientPolicy (void) {
	/* 503/timeout retry once; a subsequent 200 is then handled normally. */
	assert (SbLrclibShouldRetry (503, CURLE_OK, 1));
	assert (!SbLrclibShouldRetry (200, CURLE_OK, 2));
	assert (!SbLrclibShouldRetry (503, CURLE_OK, 2));
	assert (SbLrclibShouldRetry (0, CURLE_OPERATION_TIMEDOUT, 1));
	assert (!SbLrclibShouldRetry (0, CURLE_OPERATION_TIMEDOUT, 2));
	/* 404 is a genuine no-match and proceeds through the lookup ladder. */
	assert (!SbLrclibTransientFailure (404, CURLE_OK));
	assert (!SbLrclibShouldRetry (404, CURLE_OK, 1));
	assert (SbLrclibTransientFailure (429, CURLE_OK));
	assert (SbLrclibTransientFailure (500, CURLE_OK));
	assert (SbLrclibTransientFailure (502, CURLE_OK));
	assert (SbLrclibTransientFailure (504, CURLE_OK));
	assert (SbLrclibRetryDelayMs (0) == 1000);
	assert (SbLrclibRetryDelayMs (3) == 3000);
	assert (SbLrclibRetryDelayMs (30) == 5000);
}

static void testCacheKey (void) {
	SbTrackIdentity a, b; char ka[80], kb[80];
	SbTrackIdentitySet (&a, "The Artist", "A Song!", NULL, NULL, 0);
	SbTrackIdentitySet (&b, " the  artist ", "a-song", NULL, NULL, 0);
	SbTrackCacheKey ("musicbrainz", &a, ka, sizeof (ka));
	SbTrackCacheKey ("musicbrainz", &b, kb, sizeof (kb));
	assert (strcmp (ka, kb) == 0);
}

static void testBestMatch (void) {
	const char json[] = "{\"recordings\":["
		"{\"id\":\"wrong\",\"title\":\"Elsewhere\",\"artist-credit\":[{\"name\":\"Nobody\",\"artist\":{\"id\":\"n\"}}]},"
		"{\"id\":\"rec-1\",\"title\":\"A Song\",\"artist-credit\":[{\"name\":\"The Artist\",\"artist\":{\"id\":\"art-1\"}}],"
		"\"releases\":[{\"id\":\"rel-1\",\"title\":\"The Album\",\"date\":\"2020-04-03\"}]}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "The Artist", "A Song", NULL, NULL, 0);
	assert (SbMusicBrainzParse (json, &id, &result));
	assert (result.status == SB_LOOKUP_AVAILABLE);
	assert (strcmp (result.artistId, "art-1") == 0);
	assert (strcmp (result.recordingId, "rec-1") == 0);
	assert (strcmp (result.releaseId, "rel-1") == 0);
	assert (result.confidence == 1.0);
}

static void testAlbumAwareArtRelease (void) {
	const char json[] = "{\"recordings\":[{\"id\":\"rec-death-cab\","
		"\"title\":\"Here to Forever\",\"artist-credit\":[{\"name\":\"Death Cab For Cutie\","
		"\"artist\":{\"id\":\"artist-1\"}}],\"releases\":["
		"{\"id\":\"compilation\",\"title\":\"100 From the 20's - Rock\","
		"\"status\":\"Official\",\"artist-credit\":[{\"name\":\"Various Artists\"}],"
		"\"release-group\":{\"id\":\"group-comp\",\"primary-type\":\"Album\","
		"\"secondary-types\":[\"Compilation\"]}},"
		"{\"id\":\"asphalt\",\"title\":\"Asphalt Meadows\",\"status\":\"Official\","
		"\"artist-credit\":[{\"name\":\"Death Cab for Cutie\"}],"
		"\"release-group\":{\"id\":\"group-asphalt\",\"primary-type\":\"Album\"}}]}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Death Cab For Cutie", "Here to Forever",
			"Asphalt Meadows", NULL, 0);
	assert (SbMusicBrainzParse (json, &id, &result));
	assert (strcmp (result.recordingId, "rec-death-cab") == 0);
	assert (strcmp (result.releaseId, "asphalt") == 0);
	assert (strcmp (result.releaseGroupId, "group-asphalt") == 0);

	const char deluxe[] = "{\"releases\":[{\"id\":\"deluxe\","
		"\"title\":\"From Under the Cork Tree (20th Anniversary Deluxe)\","
		"\"status\":\"Official\",\"artist-credit\":[{\"name\":\"Fall Out Boy\"}],"
		"\"release-group\":{\"id\":\"cork\",\"primary-type\":\"Album\"}}]}";
	SbTrackIdentitySet (&id, "Fall Out Boy", "Dance, Dance",
			"From Under the Cork Tree", NULL, 0);
	SbMetadataResultInit (&result);
	assert (SbMusicBrainzSelectArtRelease (deluxe, &id, &result));
	assert (strcmp (result.releaseId, "deluxe") == 0);
}

static void testMusicBrainzDateFormatting (void) {
	char display[16];
	assert (SbMusicBrainzFormatDate ("2020-04-03", display, sizeof (display)));
	assert (strcmp (display, "04/03/2020") == 0);
	assert (SbMusicBrainzFormatDate ("2020-04", display, sizeof (display)));
	assert (strcmp (display, "04/2020") == 0);
	assert (SbMusicBrainzFormatDate ("2020", display, sizeof (display)));
	assert (strcmp (display, "2020") == 0);
	assert (!SbMusicBrainzFormatDate (NULL, display, sizeof (display)));
	assert (display[0] == '\0');
	assert (!SbMusicBrainzFormatDate ("2020-02-30", display, sizeof (display)));
	assert (display[0] == '\0');
}

static void testErrorsAndNoMatch (void) {
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Artist", "Song", NULL, NULL, 0);
	assert (!SbMusicBrainzParse ("not-json", &id, &result));
	assert (result.status == SB_LOOKUP_ERROR);
	assert (!SbMusicBrainzParse ("{\"recordings\":[]}", &id, &result));
	assert (result.status == SB_LOOKUP_NO_MATCH);
}

static void testStaleResult (void) {
	SbMetadataResolver resolver; SbMetadataResult result;
	SbMetadataResolverInit (&resolver);
	pthread_mutex_lock (&resolver.lock);
	resolver.resultReady = true; resolver.metadataResultGeneration = 3;
	resolver.completed.status = SB_LOOKUP_AVAILABLE;
	pthread_mutex_unlock (&resolver.lock);
	assert (!SbMetadataResolverPoll (&resolver, 4, &result));
	assert (!SbMetadataResolverPoll (&resolver, 3, &result));
	SbMetadataResolverDestroy (&resolver);
}

static void testLrclibPlainAndSynced (void) {
	const char json[] = "{\"id\":42,\"trackName\":\"Night Signal\","
		"\"artistName\":\"Test Operator\",\"albumName\":\"Synthetic Airwaves\",\"duration\":212.75,"
		"\"instrumental\":false,\"plainLyrics\":\"Signal in the static\\nGreen phosphor in the night\","
		"\"syncedLyrics\":\"[00:01.00] Signal in the static\\n[00:03.50] Green phosphor in the night\"}";
	SbLyricsResult result;
	assert (SbLrclibParse (json, &result));
	assert (result.status == SB_LOOKUP_AVAILABLE);
	assert (strcmp (result.provider, "LRCLIB") == 0);
	assert (strcmp (result.recordId, "42") == 0);
	assert (result.duration == 212.75);
	assert (strstr (result.plainLyrics, "Signal in the static") != NULL);
	assert (strstr (result.syncedLyrics, "[00:03.50]") != NULL);
	char *display = NULL; assert (SbLyricsDisplayText (&result, &display));
	assert (strcmp (display, result.plainLyrics) == 0); free (display);
	SbLyricsResultDestroy (&result);
	assert (SbLrclibParse ("{\"id\":43,\"trackName\":\"Plain\","
			"\"artistName\":\"Artist\",\"albumName\":\"Album\","
			"\"instrumental\":false,\"plainLyrics\":\"Only plain lyrics\","
			"\"syncedLyrics\":null}", &result));
	assert (result.plainLyrics != NULL && result.syncedLyrics == NULL);
	display = NULL; assert (SbLyricsDisplayText (&result, &display));
	assert (strcmp (display, "Only plain lyrics") == 0);
	free (display); SbLyricsResultDestroy (&result);
}

static void testLrclibSyncedOnlyAndInstrumental (void) {
	SbLyricsResult result; char *display = NULL;
	assert (SbLrclibParse ("{\"id\":\"sync-1\",\"trackName\":\"Pulse\","
		"\"artistName\":\"Operator\",\"instrumental\":false,"
		"\"syncedLyrics\":\"[00:01.20] Signal in the static\\n[00:02.40] Green phosphor in the night\"}", &result));
	assert (result.plainLyrics == NULL && result.syncedLyrics != NULL);
	assert (SbLyricsDisplayText (&result, &display));
	assert (strchr (display, '[') == NULL); free (display);
	SbLyricsResultDestroy (&result);
	assert (SbLrclibParse ("{\"id\":7,\"trackName\":\"Quiet Circuit\","
		"\"artistName\":\"Operator\",\"instrumental\":true}", &result));
	assert (result.status == SB_LOOKUP_INSTRUMENTAL && result.instrumental);
	SbLyricsResultDestroy (&result);
}

static void testLrclibErrorsAndFallback (void) {
	SbLyricsResult result; char title[128];
	assert (!SbLrclibParse ("not-json", &result));
	assert (result.status == SB_LOOKUP_ERROR); SbLyricsResultDestroy (&result);
	assert (!SbLrclibParse ("{\"trackName\":\"Missing ID\"}", &result));
	assert (result.status == SB_LOOKUP_ERROR); SbLyricsResultDestroy (&result);
	assert (SbLyricsFallbackTitle ("Night Signal (Remastered 2024)", title, sizeof (title)));
	assert (strcmp (title, "Night Signal") == 0);
	assert (SbLyricsFallbackTitle ("Night Signal - Live", title, sizeof (title)));
	assert (SbLyricsFallbackTitle ("Night Signal (Live at Moon Hall)", title, sizeof (title)));
	assert (strcmp (title, "Night Signal") == 0);
	assert (SbLyricsFallbackTitle ("Night Signal (Deluxe Edition)", title, sizeof (title)));
	assert (!SbLyricsFallbackTitle ("Night Signal", title, sizeof (title)));
	char artist[128];
	assert (SbLyricsFallbackArtist ("Operator feat. Guest", artist, sizeof (artist)));
	assert (strcmp (artist, "Operator") == 0);
	assert (SbLyricsFallbackArtist ("Operator ft. Guest", artist, sizeof (artist)));
}

static void testLrclibSearchScoring (void) {
	SbTrackIdentity id; SbLyricsResult result; double score = 0.0;
	const char exact[] = "[{\"id\":801,\"trackName\":\"Moth Circuit\","
		"\"artistName\":\"The Uncatalogued Machines\",\"albumName\":\"Basement Signals\","
		"\"duration\":187,\"instrumental\":false,"
		"\"plainLyrics\":\"Invented wires hum softly\"}]";
	SbTrackIdentitySet (&id, "The Uncatalogued Machines", "Moth Circuit",
			"Basement Signals", NULL, 188);
	assert (SbLrclibSearchParse (exact, &id, &result, &score));
	assert (score >= .99 && strcmp (result.recordId, "801") == 0);
	SbLyricsResultDestroy (&result);

	const char variants[] = "[{\"id\":802,\"trackName\":\"Cant Wait & Wonder\","
		"\"artistName\":\"Small Operator\",\"albumName\":\"Quiet Relay\","
		"\"duration\":201,\"instrumental\":false,"
		"\"plainLyrics\":\"Invented lanterns answer\"}]";
	SbTrackIdentitySet (&id, "Small Operator", "Can’t Wait and Wonder (Remastered 2025)",
			"Quiet Relay", NULL, 201);
	assert (SbLrclibSearchParse (variants, &id, &result, &score));
	SbLyricsResultDestroy (&result);

	const char featured[] = "[{\"id\":803,\"trackName\":\"Paper Antenna\","
		"\"artistName\":\"Night Cartographer\",\"albumName\":\"Low Orbit\","
		"\"duration\":244,\"instrumental\":false,"
		"\"plainLyrics\":\"Invented constellations fold\"}]";
	SbTrackIdentitySet (&id, "Night Cartographer featuring Guest Relay",
			"Paper Antenna (Live)", "Low Orbit", NULL, 243);
	assert (SbLrclibSearchParse (featured, &id, &result, &score));
	SbLyricsResultDestroy (&result);

	const char albumMismatch[] = "[{\"id\":804,\"trackName\":\"Paper Antenna\","
		"\"artistName\":\"Night Cartographer\",\"albumName\":\"Wrong Planet\","
		"\"instrumental\":false,\"plainLyrics\":\"Wrong invented words\"}]";
	SbTrackIdentitySet (&id, "Night Cartographer", "Paper Antenna",
			"Low Orbit", NULL, 0);
	assert (!SbLrclibSearchParse (albumMismatch, &id, &result, &score));
	assert (result.status == SB_LOOKUP_NO_MATCH);

	/* Close duration can disambiguate an alternate-release album name. */
	const char durationClose[] = "[{\"id\":805,\"trackName\":\"Paper Antenna\","
		"\"artistName\":\"Night Cartographer\",\"albumName\":\"Tour Archive\","
		"\"duration\":242,\"instrumental\":false,"
		"\"plainLyrics\":\"Invented close-duration words\"}]";
	SbTrackIdentitySet (&id, "Night Cartographer", "Paper Antenna (Live)",
			"Low Orbit", NULL, 243);
	assert (SbLrclibSearchParse (durationClose, &id, &result, &score));
	SbLyricsResultDestroy (&result);

	const char ambiguous[] = "["
		"{\"id\":806,\"trackName\":\"Glass Frequency\",\"artistName\":\"Odd Receiver\","
		"\"duration\":180,\"instrumental\":false,\"plainLyrics\":\"Invented one\"},"
		"{\"id\":807,\"trackName\":\"Glass Frequency\",\"artistName\":\"Odd Receiver\","
		"\"duration\":181,\"instrumental\":false,\"plainLyrics\":\"Invented two\"}]";
	SbTrackIdentitySet (&id, "Odd Receiver", "Glass Frequency", NULL, NULL, 180);
	assert (!SbLrclibSearchParse (ambiguous, &id, &result, &score));
	assert (result.status == SB_LOOKUP_NO_MATCH);

	const char weak[] = "[{\"id\":808,\"trackName\":\"Different Signal\","
		"\"artistName\":\"Someone Else\",\"duration\":180,\"instrumental\":false,"
		"\"plainLyrics\":\"Invented weak words\"}]";
	assert (!SbLrclibSearchParse (weak, &id, &result, &score));
	assert (result.status == SB_LOOKUP_NO_MATCH);

	/* A title that merely contains the requested title is not safe enough. */
	const char longerTitle[] = "[{\"id\":809,\"trackName\":\"Glass Frequency Reprise\","
		"\"artistName\":\"Odd Receiver\",\"duration\":180,\"instrumental\":false,"
		"\"plainLyrics\":\"Invented but wrong reprise words\"}]";
	assert (!SbLrclibSearchParse (longerTitle, &id, &result, &score));
	assert (result.status == SB_LOOKUP_NO_MATCH);
}

typedef struct { int lyricsCalls, metadataCalls; SbLookupStatus lyricsStatus,
	metadataStatus; unsigned int lyricsDelayMs; } MockProviders;

static bool mockMetadata (const SbTrackIdentity *id, SbMetadataResult *result, void *data) {
	MockProviders *mock = data; mock->metadataCalls++; SbMetadataResultInit (result);
	result->status = mock->metadataStatus; (void) id;
	return result->status == SB_LOOKUP_AVAILABLE;
}

static bool mockLyrics (const SbTrackIdentity *id, SbLyricsResult *result, void *data) {
	MockProviders *mock = data; mock->lyricsCalls++; SbLyricsResultInit (result);
	if (mock->lyricsDelayMs > 0) SbPlatformSleepMs (mock->lyricsDelayMs);
	strcpy (result->provider, "MockLyrics"); result->status = mock->lyricsStatus;
	if (result->status == SB_LOOKUP_AVAILABLE) {
		strcpy (result->artist, id->artist); strcpy (result->title, id->title);
		const char fixture[] = "Signal in the static\nGreen phosphor in the night";
		result->plainLyrics = malloc (sizeof (fixture));
		if (result->plainLyrics != NULL) memcpy (result->plainLyrics, fixture, sizeof (fixture));
	}
	return result->status == SB_LOOKUP_AVAILABLE;
}

static bool waitLyrics (SbMetadataResolver *resolver, uint64_t generation,
		SbLyricsResult *result) {
	for (int i = 0; i < 100; i++) {
		if (SbLyricsResolverPoll (resolver, generation, result)) return true;
		SbPlatformSleepMs (10);
	}
	return false;
}

static bool waitMetadata (SbMetadataResolver *resolver, uint64_t generation,
		SbMetadataResult *result) {
	for (int i = 0; i < 100; i++) {
		if (SbMetadataResolverPoll (resolver, generation, result)) return true;
		SbPlatformSleepMs (10);
	}
	return false;
}

static void testProviderStatesAreIndependent (void) {
	SbMetadataResolver resolver; SbTrackIdentity id;
	SbMetadataResult metadata; SbLyricsResult lyrics;
	MockProviders mock = {0, 0, SB_LOOKUP_AVAILABLE, SB_LOOKUP_ERROR, 150};
	SbMetadataResolverInit (&resolver);
	resolver.provider = (SbMetadataProvider) {"mock-metadata", mockMetadata, &mock};
	resolver.lyricsProvider = (SbLyricsProvider) {"mock-lyrics", mockLyrics, &mock};
	assert (SbMetadataResolverStart (&resolver));
	SbTrackIdentitySet (&id, "Operator", "Independent Signal", NULL, NULL, 0);
	SbMetadataResolverRequest (&resolver, &id, 10);
	/* Metadata completion is published before a slow lyrics provider finishes. */
	assert (waitMetadata (&resolver, 10, &metadata));
	assert (metadata.status == SB_LOOKUP_ERROR);
	assert (waitLyrics (&resolver, 10, &lyrics));
	assert (lyrics.status == SB_LOOKUP_AVAILABLE);
	SbLyricsResultDestroy (&lyrics);

	mock.metadataStatus = SB_LOOKUP_AVAILABLE;
	mock.lyricsStatus = SB_LOOKUP_ERROR;
	mock.lyricsDelayMs = 0;
	SbTrackIdentitySet (&id, "Operator", "Reverse Signal", NULL, NULL, 0);
	SbMetadataResolverRequest (&resolver, &id, 11);
	assert (waitMetadata (&resolver, 11, &metadata));
	assert (metadata.status == SB_LOOKUP_AVAILABLE);
	assert (waitLyrics (&resolver, 11, &lyrics));
	assert (lyrics.status == SB_LOOKUP_ERROR);
	SbLyricsResultDestroy (&lyrics); SbMetadataResolverDestroy (&resolver);
}

static void testTransientMetadataIsNotCached (void) {
	SbMetadataResolver resolver; SbTrackIdentity id;
	SbMetadataResult metadata; SbLyricsResult lyrics;
	MockProviders mock = {0, 0, SB_LOOKUP_NO_MATCH,
			SB_LOOKUP_UNAVAILABLE, 0};
	SbMetadataResolverInit (&resolver);
	resolver.provider = (SbMetadataProvider) {"mock-metadata", mockMetadata, &mock};
	resolver.lyricsProvider = (SbLyricsProvider) {"mock-lyrics", mockLyrics, &mock};
	assert (SbMetadataResolverStart (&resolver));
	SbTrackIdentitySet (&id, "Operator", "Busy Signal", NULL, NULL, 0);
	SbMetadataResolverRequest (&resolver, &id, 20);
	assert (waitMetadata (&resolver, 20, &metadata));
	assert (metadata.status == SB_LOOKUP_UNAVAILABLE && mock.metadataCalls == 1);
	assert (waitLyrics (&resolver, 20, &lyrics)); SbLyricsResultDestroy (&lyrics);
	mock.metadataStatus = SB_LOOKUP_AVAILABLE;
	SbMetadataResolverRequest (&resolver, &id, 21);
	assert (waitMetadata (&resolver, 21, &metadata));
	assert (metadata.status == SB_LOOKUP_AVAILABLE && mock.metadataCalls == 2);
	assert (waitLyrics (&resolver, 21, &lyrics)); SbLyricsResultDestroy (&lyrics);
	SbMetadataResolverDestroy (&resolver);
}

static void testLyricsResolverCacheAndFailures (void) {
	SbMetadataResolver resolver; SbTrackIdentity id; SbLyricsResult result;
	MockProviders mock = {0, 0, SB_LOOKUP_AVAILABLE, SB_LOOKUP_NO_MATCH, 0};
	SbMetadataResolverInit (&resolver);
	resolver.provider = (SbMetadataProvider) {"mock-metadata", mockMetadata, &mock};
	resolver.lyricsProvider = (SbLyricsProvider) {"mock-lyrics", mockLyrics, &mock};
	assert (SbMetadataResolverStart (&resolver));
	SbTrackIdentitySet (&id, "Operator", "Night Signal", "Synthetic Airwaves", NULL, 180);
	SbMetadataResolverRequest (&resolver, &id, 1);
	assert (waitLyrics (&resolver, 1, &result));
	assert (result.status == SB_LOOKUP_AVAILABLE && mock.lyricsCalls == 1);
	SbLyricsResultDestroy (&result);
	SbPlatformSleepMs (30);
	SbMetadataResolverRequest (&resolver, &id, 2);
	assert (waitLyrics (&resolver, 2, &result));
	assert (mock.lyricsCalls == 1); SbLyricsResultDestroy (&result);
	SbTrackIdentitySet (&id, "Operator", "Unknown Signal", NULL, NULL, 0);
	mock.lyricsStatus = SB_LOOKUP_NO_MATCH;
	SbMetadataResolverRequest (&resolver, &id, 3);
	assert (waitLyrics (&resolver, 3, &result));
	assert (result.status == SB_LOOKUP_NO_MATCH && mock.lyricsCalls == 2);
	SbLyricsResultDestroy (&result);
	SbMetadataResolverRequest (&resolver, &id, 4);
	assert (waitLyrics (&resolver, 4, &result));
	assert (mock.lyricsCalls == 2); SbLyricsResultDestroy (&result);
	SbTrackIdentitySet (&id, "Operator", "Broken Link", NULL, NULL, 0);
	mock.lyricsStatus = SB_LOOKUP_ERROR;
	SbMetadataResolverRequest (&resolver, &id, 5);
	assert (!waitLyrics (&resolver, 6, &result)); /* stale generation rejected */
	SbMetadataResolverRequest (&resolver, &id, 6);
	assert (waitLyrics (&resolver, 6, &result));
	assert (result.status == SB_LOOKUP_ERROR && mock.lyricsCalls >= 3);
	SbLyricsResultDestroy (&result);
	/* Transient unavailability is never retained as a negative match. */
	SbTrackIdentitySet (&id, "Operator", "Temporary Link", NULL, NULL, 0);
	mock.lyricsStatus = SB_LOOKUP_UNAVAILABLE;
	const int beforeTransient = mock.lyricsCalls;
	SbMetadataResolverRequest (&resolver, &id, 7);
	assert (waitLyrics (&resolver, 7, &result)); SbLyricsResultDestroy (&result);
	SbMetadataResolverRequest (&resolver, &id, 8);
	assert (waitLyrics (&resolver, 8, &result));
	assert (result.status == SB_LOOKUP_UNAVAILABLE &&
			mock.lyricsCalls == beforeTransient + 2);
	SbLyricsResultDestroy (&result); SbMetadataResolverDestroy (&resolver);
}

int main (void) {
	testNormalization (); testRetainedModalScroll (); testRetainedModalScrollBounds ();
	testCursesWheelStateMapping (); testMouseBitNames (); testUpcomingHeight ();
	testMusicBrainzHttpStates (); testLrclibTransientPolicy ();
	testCacheKey (); testBestMatch (); testAlbumAwareArtRelease ();
	testMusicBrainzDateFormatting ();
	testErrorsAndNoMatch (); testStaleResult (); testLrclibPlainAndSynced ();
	testLrclibSyncedOnlyAndInstrumental (); testLrclibErrorsAndFallback ();
	testLrclibSearchScoring ();
	testLyricsResolverCacheAndFailures ();
	testProviderStatesAreIndependent ();
	testTransientMetadataIsNotCached ();
	puts ("enrichment tests passed"); return 0;
}
