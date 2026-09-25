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
	SbUiMouseWheelMasks masks = {
			.wheelUpPressed = UINT64_C (0x80000),
			.wheelUpClicked = UINT64_C (0x100000),
			.wheelDownPressed = UINT64_C (0x2000000),
			.wheelDownClicked = UINT64_C (0x4000000),
			.reportPosition = UINT64_C (0x8000000),
	};
	assert (SbUiMouseWheelDirection (masks.wheelUpPressed, masks) ==
			SB_UI_MOUSE_WHEEL_UP);
	assert (SbUiMouseWheelDirection (masks.wheelUpClicked, masks) ==
			SB_UI_MOUSE_WHEEL_UP);
	assert (SbUiMouseWheelDirection (masks.wheelDownPressed, masks) ==
			SB_UI_MOUSE_WHEEL_DOWN);
	assert (SbUiMouseWheelDirection (masks.wheelDownClicked, masks) ==
			SB_UI_MOUSE_WHEEL_DOWN);
	assert (SbUiMouseWheelDirection (UINT64_C (0x2), masks) ==
			SB_UI_MOUSE_WHEEL_NONE); /* BUTTON1_PRESSED */
	assert (SbUiMouseWheelDirection (UINT64_C (0x4), masks) ==
			SB_UI_MOUSE_WHEEL_NONE); /* BUTTON1_CLICKED */
	assert (SbUiMouseWheelDirection (UINT64_C (0x100), masks) ==
			SB_UI_MOUSE_WHEEL_NONE); /* BUTTON2_CLICKED */
	assert (SbUiMouseWheelDirection (UINT64_C (0x4000), masks) ==
			SB_UI_MOUSE_WHEEL_NONE); /* BUTTON3_CLICKED */
	assert (SbUiMouseWheelDirection (UINT64_C (0x20000000), masks) ==
			SB_UI_MOUSE_WHEEL_NONE); /* Unknown bit. */
	assert (SbUiMouseWheelDirection (masks.wheelUpPressed |
			masks.wheelDownPressed, masks) == SB_UI_MOUSE_WHEEL_NONE);

	/* Apple ncurses v1 maps X10 button 5 to REPORT_MOUSE_POSITION alone.
	 * It is accepted only when the exact terminal/build fallback is enabled. */
	assert (SbUiMouseWheelDirection (masks.reportPosition, masks) ==
			SB_UI_MOUSE_WHEEL_NONE);
	masks.appleTerminalNcursesV1 = 1;
	const int appleWheelDown = SbUiMouseWheelDirection (
			masks.reportPosition, masks);
	assert (appleWheelDown == SB_UI_MOUSE_WHEEL_DOWN);
	/* Physical decoding is identical with or without a modal; UI ownership and
	 * background navigation are decided only after decoding. */
	assert (SbUiMouseWheelOwnedByModal (1, appleWheelDown));
	assert (!SbUiMouseWheelOwnedByModal (0, appleWheelDown));
	enum { TEST_KEY_MOUSE = 409, TEST_KEY_UP = 1001, TEST_KEY_DOWN = 1002 };
	assert (SbUiMouseWheelNavigationKey (TEST_KEY_MOUSE, appleWheelDown,
			TEST_KEY_UP, TEST_KEY_DOWN) == TEST_KEY_DOWN);
	assert (SbUiMouseWheelDirection (masks.reportPosition | UINT64_C (0x2),
			masks) == SB_UI_MOUSE_WHEEL_NONE);
	assert (SbUiMouseWheelOwnedByModal (1, SB_UI_SCROLL_TOWARD_TOP));
	assert (SbUiMouseWheelOwnedByModal (1, SB_UI_SCROLL_TOWARD_BOTTOM));
	assert (!SbUiMouseWheelOwnedByModal (1, 0));
	assert (!SbUiMouseWheelOwnedByModal (0, SB_UI_SCROLL_TOWARD_TOP));
	assert (SbUiMouseWheelFromNativeDelta (120) == SB_UI_SCROLL_TOWARD_TOP);
	assert (SbUiMouseWheelFromNativeDelta (-120) == SB_UI_SCROLL_TOWARD_BOTTOM);
	assert (SbUiMouseWheelNavigationKey (TEST_KEY_MOUSE,
			SbUiMouseWheelFromNativeDelta (120), TEST_KEY_UP,
			TEST_KEY_DOWN) == TEST_KEY_UP);
	assert (SbUiMouseWheelNavigationKey (TEST_KEY_MOUSE,
			SbUiMouseWheelFromNativeDelta (-120), TEST_KEY_UP,
			TEST_KEY_DOWN) == TEST_KEY_DOWN);
	/* An ordinary click or unsupported/unknown event stays KEY_MOUSE and never
	 * enters the background Up/Down navigation path. */
	assert (SbUiMouseWheelNavigationKey (TEST_KEY_MOUSE,
			SB_UI_MOUSE_WHEEL_NONE, TEST_KEY_UP,
			TEST_KEY_DOWN) == TEST_KEY_MOUSE);
	assert (SbUiMouseListMove (2, 5, SB_UI_MOUSE_WHEEL_UP) == 1);
	assert (SbUiMouseListMove (2, 5, SB_UI_MOUSE_WHEEL_DOWN) == 3);
	assert (SbUiMouseListMove (0, 5, SB_UI_MOUSE_WHEEL_UP) == 0);
	assert (SbUiMouseListMove (4, 5, SB_UI_MOUSE_WHEEL_DOWN) == 4);
	assert (SbUiMouseListMove (9, 0, SB_UI_MOUSE_WHEEL_DOWN) == 0);
}

static void testReadOnlyModalWheelIntegration (void) {
	/* Exercise three independent modal instances with the same centralized
	 * direction values used by the read-only modal routes. */
	SbUiModalScrollState modal[3];
	for (size_t index = 0; index < sizeof (modal) / sizeof (*modal); index++) {
		SbUiModalScrollOpen (&modal[index], index + 1);
		SbUiModalScrollClamp (&modal[index], 20, 8);
		SbUiModalScrollWheel (&modal[index], SB_UI_MOUSE_WHEEL_DOWN);
		assert (modal[index].offset == 3);
		SbUiModalScrollWheel (&modal[index], SB_UI_MOUSE_WHEEL_UP);
		assert (modal[index].offset == 0);
		SbUiModalScrollWheel (&modal[index], SB_UI_MOUSE_WHEEL_UP);
		assert (modal[index].offset == 0);
		modal[index].offset = modal[index].maximum - 1;
		SbUiModalScrollWheel (&modal[index], SB_UI_MOUSE_WHEEL_DOWN);
		assert (modal[index].offset == modal[index].maximum);
	}
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
	assert (SbMusicBrainzRateDelayMs (0, 100) == 0);
	assert (SbMusicBrainzRateDelayMs (100, 100) == 1000);
	assert (SbMusicBrainzRateDelayMs (100, 1099) == 1);
	assert (SbMusicBrainzRateDelayMs (100, 1100) == 0);
	assert (SbMusicBrainzRequestBudget () == 5);
	assert (SbMusicBrainzHttpStatus (503, 0) == SB_LOOKUP_UNAVAILABLE);
	assert (SbMusicBrainzHttpStatus (500, 0) == SB_LOOKUP_UNAVAILABLE);
	assert (SbMusicBrainzHttpStatus (429, 0) == SB_LOOKUP_UNAVAILABLE);
	assert (SbMusicBrainzHttpStatus (200, 0) == SB_LOOKUP_LOADING);
	assert (SbMusicBrainzHttpStatus (503, 7) == SB_LOOKUP_UNAVAILABLE);
	assert (SbMusicBrainzShouldRetry (503, 0, 1));
	assert (!SbMusicBrainzShouldRetry (503, 0, 2));
	const long transient[] = {429, 500, 502, 503, 504};
	for (size_t i = 0; i < sizeof (transient) / sizeof (*transient); i++)
		assert (SbMusicBrainzShouldRetry (transient[i], CURLE_OK, 1));
	assert (SbMusicBrainzShouldRetry (0, CURLE_OPERATION_TIMEDOUT, 1));
	assert (SbMusicBrainzShouldRetry (503, CURLE_COULDNT_CONNECT, 1));
	assert (!SbMusicBrainzShouldRetry (404, CURLE_OK, 1));
	assert (SbMusicBrainzRetryDelayMs (0) == 1000);
	assert (SbMusicBrainzRetryDelayMs (3) == 3000);
	assert (SbMusicBrainzRetryDelayMs (30) == 5000);
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
	SbTrackIdentitySetPandoraId (&a, "pandora-recording-a");
	SbTrackIdentitySetPandoraId (&b, "pandora-recording-b");
	SbTrackCacheKey ("musicbrainz", &a, ka, sizeof (ka));
	SbTrackCacheKey ("musicbrainz", &b, kb, sizeof (kb));
	assert (strcmp (ka, kb) == 0); /* provider data remains reusable */
	SbTrackWorkKey (&a, ka, sizeof (ka));
	SbTrackWorkKey (&b, kb, sizeof (kb));
	assert (strcmp (ka, kb) != 0);
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

static void testRichMusicBrainzMetadata (void) {
	const char json[] = "{\"recordings\":[{\"id\":\"rec-feathers\","
		"\"title\":\"Feathers\",\"first-release-date\":\"2007\","
		"\"artist-credit\":[{\"name\":\"Coheed and Cambria\","
		"\"artist\":{\"id\":\"artist-coheed\"}}],"
		"\"isrcs\":[\"USSM10703925\",\"us-sm1-07-03924\"],"
		"\"genres\":[{\"name\":\"alternative rock\",\"count\":5},"
		"{\"name\":\"Progressive Rock\",\"count\":8},"
		"{\"name\":\"progressive-rock\",\"count\":2}],"
		"\"tags\":[{\"name\":\"seen live\",\"count\":40}],"
		"\"releases\":[{\"id\":\"rel-nwft\",\"title\":\"No World for Tomorrow\","
		"\"date\":\"2007-10-23\",\"country\":\"US\",\"status\":\"Official\","
		"\"artist-credit\":[{\"name\":\"Coheed and Cambria\"}],"
		"\"label-info\":[{\"catalog-number\":\"BAD\",\"label\":{\"name\":\"\"}},"
		"{\"catalog-number\":\"88697 10270 2\",\"label\":{\"name\":\"Columbia\"}},"
		"{\"catalog-number\":\"SECOND\",\"label\":{\"name\":\"Legacy\"}}],"
		"\"release-group\":{\"id\":\"rg-nwft\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2007-10-23\"}}]}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Coheed and Cambria", "Feathers",
			"No World for Tomorrow", NULL, 0);
	assert (SbMusicBrainzParse (json, &id, &result));
	assert (strcmp (result.release, "No World for Tomorrow") == 0);
	assert (strcmp (result.releaseDate, "2007-10-23") == 0);
	assert (strcmp (result.firstReleaseDate, "2007-10-23") == 0);
	assert (strcmp (result.releaseType, "Album") == 0);
	assert (strcmp (result.releaseCountry, "US") == 0);
	assert (strcmp (result.label, "Columbia") == 0);
	assert (strcmp (result.catalogNumber, "88697 10270 2") == 0);
	assert (strcmp (result.isrc, "USSM10703924") == 0);
	assert (strcmp (result.genres, "Progressive Rock, alternative rock") == 0);
	assert (result.categorySource == SB_METADATA_CATEGORIES_GENRES);

	char display[2048];
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Release Date: 10/23/2007") != NULL);
	assert (strstr (display, "Original Release:") == NULL);
	assert (strstr (display, "Edition Release:") == NULL);
	assert (strstr (display, "Label: Columbia\nCatalog: 88697 10270 2") != NULL);
	assert (strstr (display, "ISRC: USSM10703924") != NULL);
	assert (strstr (display, "Genres: Progressive Rock") != NULL);
	assert (strstr (display, "Tags:") == NULL);
	assert (strstr (display, "rec-feathers") == NULL);
	assert (strstr (display, "rg-nwft") == NULL);
	strcpy (result.releaseCountry, "XE");
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Country: Europe") != NULL);

	/* A fallback edition must not inherit release-specific values from the
	 * previously selected edition. */
	const char alternate[] = "{\"releases\":[{\"id\":\"rel-reissue\","
		"\"title\":\"No World for Tomorrow\",\"date\":\"2023\","
		"\"status\":\"Official\",\"artist-credit\":[{\"name\":\"Coheed and Cambria\"}],"
		"\"release-group\":{\"id\":\"rg-nwft\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2007-10-23\"}}]}";
	assert (SbMusicBrainzSelectRelease (alternate, &id, &result));
	assert (strcmp (result.releaseId, "rel-reissue") == 0);
	assert (result.releaseCountry[0] == '\0' && result.label[0] == '\0' &&
			result.catalogNumber[0] == '\0');
}

static void testAlbumDateSemanticsRegressions (void) {
	const char dashboardSearch[] = "{\"recordings\":[{\"id\":\"dashboard-rec\","
		"\"title\":\"Screaming Infidelities\",\"first-release-date\":\"2011-10-10\","
		"\"artist-credit\":[{\"name\":\"Dashboard Confessional\","
		"\"artist\":{\"id\":\"dashboard-artist\"}}]}]}";
	const char dashboardAlbum[] = "{\"releases\":[{\"id\":\"dashboard-release\","
		"\"title\":\"The Places You Have Come to Fear the Most\","
		"\"date\":\"2001-03-20\",\"status\":\"Official\","
		"\"artist-credit\":[{\"name\":\"Dashboard Confessional\"}],"
		"\"release-group\":{\"id\":\"dashboard-group\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2001-03-20\"}}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Dashboard Confessional", "Screaming Infidelities",
			"The Places You Have Come to Fear the Most", NULL, 0);
	assert (SbMusicBrainzParse (dashboardSearch, &id, &result));
	assert (result.status == SB_LOOKUP_AVAILABLE);
	assert (result.releaseGroupId[0] == '\0' && result.firstReleaseDate[0] == '\0');
	assert (SbMusicBrainzShouldRetry (503, CURLE_OK, 1));
	assert (SbMusicBrainzSelectRelease (dashboardAlbum, &id, &result));
	assert (strcmp (result.firstReleaseDate, "2001-03-20") == 0);
	assert (strcmp (result.releaseGroupId, "dashboard-group") == 0);

	SbMetadataResult unavailable;
	assert (SbMusicBrainzParse (dashboardSearch, &id, &unavailable));
	assert (SbMusicBrainzShouldRetry (503, CURLE_OK, 1));
	assert (!SbMusicBrainzShouldRetry (503, CURLE_OK, 2));
	assert (unavailable.status == SB_LOOKUP_AVAILABLE && unavailable.release[0] == '\0' &&
			unavailable.firstReleaseDate[0] == '\0');

	const char brandSearch[] = "{\"recordings\":[{\"id\":\"brand-rec\","
		"\"title\":\"I Will Play My Game Beneath The Spin Light\","
		"\"first-release-date\":\"2017-10-15\",\"artist-credit\":["
		"{\"name\":\"Brand New\",\"artist\":{\"id\":\"brand-artist\"}}]}]}";
	const char deja[] = "{\"releases\":[{\"id\":\"825f384f-efcd-475f-b69c-8b618feb88f4\","
		"\"title\":\"Deja Entendu\",\"date\":\"2003-06-17\",\"status\":\"Official\","
		"\"artist-credit\":[{\"name\":\"Brand New\"}],\"release-group\":{"
		"\"id\":\"6917ec0c-3d53-3835-bd9d-d079d70c1ce0\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2003-06-17\"}}]}";
	SbTrackIdentitySet (&id, "Brand New", "I Will Play My Game Beneath The Spin Light",
			"Deja Entendu", NULL, 0);
	assert (SbMusicBrainzParse (brandSearch, &id, &result));
	assert (result.firstReleaseDate[0] == '\0');
	assert (SbMusicBrainzSelectRelease (deja, &id, &result));
	assert (strcmp (result.firstReleaseDate, "2003-06-17") == 0);
	assert (strcmp (result.releaseDate, "2003-06-17") == 0);
	assert (strcmp (result.firstReleaseDate, "2017-10-15") != 0);

	strcpy (result.firstReleaseDate, "2017-10-15");
	strcpy (result.releaseDate, "2003-06-17");
	assert (!SbMetadataValidateDates (&result));
	assert (result.firstReleaseDate[0] == '\0');
	assert (strcmp (result.releaseDate, "2003-06-17") == 0);
	char display[1024];
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Original Release:") == NULL);
	assert (strstr (display, "Edition Release: 06/17/2003") != NULL);
}

static void testDeathCabFallbackAndJackKaysHappyPath (void) {
	const char plansSearch[] = "{\"recordings\":[{\"id\":\"plans-rec\","
		"\"title\":\"I Will Follow You into the Dark\",\"artist-credit\":["
		"{\"name\":\"Death Cab For Cutie\",\"artist\":{\"id\":\"dcfc\"}}]}]}";
	const char plansAlbum[] = "{\"releases\":[{\"id\":\"plans-release\","
		"\"title\":\"Plans\",\"date\":\"2005-08-30\",\"status\":\"Official\","
		"\"artist-credit\":[{\"name\":\"Death Cab for Cutie\"}],\"release-group\":{"
		"\"id\":\"plans-group\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2005-08-30\"}}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Death Cab For Cutie", "I Will Follow You into the Dark",
			"Plans", NULL, 0);
	assert (SbMusicBrainzParse (plansSearch, &id, &result));
	assert (result.status == SB_LOOKUP_AVAILABLE && result.release[0] == '\0');
	assert (SbMusicBrainzShouldRetry (503, CURLE_OK, 1));
	assert (SbMusicBrainzSelectRelease (plansAlbum, &id, &result));
	assert (strcmp (result.release, "Plans") == 0);
	assert (strcmp (result.firstReleaseDate, "2005-08-30") == 0);

	const char jackSearch[] = "{\"recordings\":[{\"id\":\"jack-rec\","
		"\"title\":\"Drinking Song\",\"artist-credit\":[{\"name\":\"Jack Kays\","
		"\"artist\":{\"id\":\"jack\"}}],\"releases\":[{\"id\":\"deadbeat-release\","
		"\"title\":\"DEADBEAT! - Disc 1\",\"date\":\"2024-08-23\","
		"\"country\":\"XW\",\"status\":\"Official\",\"artist-credit\":["
		"{\"name\":\"Jack Kays\"}],\"label-info\":[{\"catalog-number\":\"19687235072\","
		"\"label\":{\"name\":\"Columbia\"}}],\"release-group\":{"
		"\"id\":\"deadbeat-group\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2024-08-23\"}}]}]}";
	SbTrackIdentitySet (&id, "Jack Kays", "Drinking Song", "DEADBEAT! - Disc 1", NULL, 0);
	assert (SbMusicBrainzParse (jackSearch, &id, &result));
	assert (strcmp (result.firstReleaseDate, "2024-08-23") == 0);
	assert (strcmp (result.releaseType, "Album") == 0);
	assert (strcmp (result.label, "Columbia") == 0);
	char display[1024];
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Country: Worldwide") != NULL);
}

static void testProgressiveMetadataPublication (void) {
	SbMetadataResolver resolver; SbMetadataResult basic, rich, observed;
	SbMetadataResolverInit (&resolver); SbMetadataResultInit (&basic);
	basic.status = SB_LOOKUP_AVAILABLE; strcpy (basic.provider, "MusicBrainz");
	strcpy (basic.artist, "Artist"); strcpy (basic.title, "Track"); basic.confidence = 1.0;
	SbMetadataResolverPublishProgress (&resolver, 9, &basic);
	assert (SbMetadataResolverPoll (&resolver, 9, &observed));
	assert (strcmp (observed.title, "Track") == 0 && observed.release[0] == '\0');
	rich = basic; strcpy (rich.release, "Album"); strcpy (rich.releaseId, "release");
	strcpy (rich.releaseGroupId, "group"); strcpy (rich.firstReleaseDate, "2020");
	SbMetadataResolverPublishProgress (&resolver, 9, &rich);
	assert (SbMetadataResolverPoll (&resolver, 9, &observed));
	assert (strcmp (observed.release, "Album") == 0);
	SbMetadataResolverPublishProgress (&resolver, 8, &basic);
	assert (!SbMetadataResolverPoll (&resolver, 9, &observed));
	SbMetadataResolverDestroy (&resolver);
}

static void testDeluxeDatesAndDetailMerge (void) {
	const char search[] = "{\"recordings\":[{\"id\":\"rec-foo\",\"title\":\"Song\","
		"\"artist-credit\":[{\"name\":\"Artist\",\"artist\":{\"id\":\"a\"}}],"
		"\"releases\":[{\"id\":\"deluxe\",\"title\":\"Foo (20th Anniversary Deluxe)\","
		"\"date\":\"2025-05\",\"status\":\"Official\","
		"\"artist-credit\":[{\"name\":\"Artist\"}],\"release-group\":{\"id\":\"foo-group\","
		"\"primary-type\":\"Album\",\"first-release-date\":\"2005\"}}]}]}";
	const char detail[] = "{\"id\":\"deluxe\",\"title\":\"Foo (20th Anniversary Deluxe)\","
		"\"date\":\"2025-05\",\"country\":\"GB\",\"label-info\":["
		"{\"catalog-number\":\"CAT-20\",\"label\":{\"name\":\"Example Records\"}}],"
		"\"release-group\":{\"id\":\"foo-group\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2005\"}}";
	const char group[] = "{\"id\":\"foo-group\",\"primary-type\":\"Album\","
		"\"first-release-date\":\"2005\",\"genres\":["
		"{\"name\":\"Art Rock\",\"count\":4}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Artist", "Song", "Foo (20th Anniversary Deluxe)", NULL, 0);
	assert (SbMusicBrainzParse (search, &id, &result));
	/* The detail helper uses the same 503 -> retry -> successful parse policy. */
	assert (SbMusicBrainzShouldRetry (503, CURLE_OK, 1));
	assert (SbMusicBrainzApplyReleaseDetail (detail, &result));
	assert (SbMusicBrainzApplyReleaseGroupDetail (group, &result));
	assert (strcmp (result.firstReleaseDate, "2005") == 0);
	assert (strcmp (result.releaseDate, "2025-05") == 0);
	assert (strcmp (result.label, "Example Records") == 0);
	assert (strcmp (result.catalogNumber, "CAT-20") == 0);
	assert (strcmp (result.genres, "Art Rock") == 0);
	char display[1024];
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Original Release: 2005") != NULL);
	assert (strstr (display, "Edition Release: 05/2025") != NULL);
}

static void testWeakMetadataAndTransientDetail (void) {
	const char json[] = "{\"recordings\":[{\"id\":\"rec\",\"title\":\"Song\","
		"\"artist-credit\":[{\"name\":\"Artist\",\"artist\":{\"id\":\"a\"}}]}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "Artist", "Song", "Missing Album", NULL, 0);
	assert (SbMusicBrainzParse (json, &id, &result));
	assert (result.status == SB_LOOKUP_AVAILABLE && result.release[0] == '\0');
	SbMetadataResult before = result;
	assert (!SbMusicBrainzApplyReleaseDetail ("not-json", &result));
	assert (memcmp (&before, &result, sizeof (result)) == 0);
	char *serialized = SbMetadataSerialize (&result); SbMetadataResult restored;
	assert (serialized != NULL && SbMetadataDeserialize (serialized, &restored));
	free (serialized);
	assert (restored.status == SB_LOOKUP_AVAILABLE);
	assert (strcmp (restored.artist, "Artist") == 0 && restored.release[0] == '\0');
	char display[1024];
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Canonical Artist: Artist") != NULL);
	assert (strstr (display, "Release:") == NULL);
	assert (strstr (display, "Genres:") == NULL && strstr (display, "Tags:") == NULL);
}

static void testTagFallbackAndMetadataCacheRoundTrip (void) {
	const char json[] = "{\"recordings\":[{\"id\":\"rec\",\"title\":\"Song\","
		"\"artist-credit\":[{\"name\":\"Artist\",\"artist\":{\"id\":\"a\"}}],"
		"\"isrcs\":[\"invalid\",\"ZZAAA2400001\"],\"tags\":["
		"{\"name\":\"space rock\",\"count\":9},{\"name\":\"SPACE-ROCK\",\"count\":8},"
		"{\"name\":\"psychedelic rock\",\"count\":7},{\"name\":\" art rock \",\"count\":6},"
		"{\"name\":\"progressive rock\",\"count\":5},{\"name\":\"dream pop\",\"count\":4},"
		"{\"name\":\"shoegaze\",\"count\":3},{\"name\":\"noise\",\"count\":0}]}]}";
	SbTrackIdentity id; SbMetadataResult result, restored;
	SbTrackIdentitySet (&id, "Artist", "Song", NULL, NULL, 0);
	assert (SbMusicBrainzParse (json, &id, &result));
	assert (strcmp (result.genres,
			"space rock, psychedelic rock, art rock, progressive rock, dream pop") == 0);
	assert (result.categorySource == SB_METADATA_CATEGORIES_TAGS);
	assert (strcmp (result.isrc, "ZZAAA2400001") == 0);
	strcpy (result.release, "Cached Release"); strcpy (result.releaseDate, "2024-09-03");
	strcpy (result.firstReleaseDate, "1999"); strcpy (result.releaseType, "Album");
	strcpy (result.releaseCountry, "JP"); strcpy (result.label, "Cache Label");
	strcpy (result.catalogNumber, "CACHE-1"); strcpy (result.releaseId, "release-id");
	strcpy (result.releaseGroupId, "group-id");
	char *serialized = SbMetadataSerialize (&result); assert (serialized != NULL);
	assert (SbMetadataDeserialize (serialized, &restored)); free (serialized);
	assert (strcmp (restored.release, result.release) == 0);
	assert (strcmp (restored.firstReleaseDate, result.firstReleaseDate) == 0);
	assert (strcmp (restored.releaseType, result.releaseType) == 0);
	assert (strcmp (restored.releaseCountry, result.releaseCountry) == 0);
	assert (strcmp (restored.label, result.label) == 0);
	assert (strcmp (restored.catalogNumber, result.catalogNumber) == 0);
	assert (strcmp (restored.isrc, result.isrc) == 0);
	assert (strcmp (restored.genres, result.genres) == 0);
	assert (restored.categorySource == SB_METADATA_CATEGORIES_TAGS);
	char display[1024];
	assert (SbMetadataFormatAvailableFields (&restored, display, sizeof (display)));
	assert (strstr (display, "Tags: space rock") != NULL);
	assert (strstr (display, "Genres:") == NULL);
	/* Deserialization is defensive even within the current schema. */
	strcpy (result.firstReleaseDate, "2025-01-01");
	strcpy (result.releaseDate, "2024-09-03");
	serialized = SbMetadataSerialize (&result); assert (serialized != NULL);
	assert (SbMetadataDeserialize (serialized, &restored)); free (serialized);
	assert (restored.firstReleaseDate[0] == '\0');
	assert (strcmp (restored.releaseDate, "2024-09-03") == 0);
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

static void testAlbumFamilyFallbackAndCompletion (void) {
	char family[SB_ENRICH_TEXT_MAX];
	assert (SbMusicBrainzAlbumFamilyTitle ("Sing the Sorrow (Deluxe)",
			family, sizeof (family)));
	assert (strcmp (family, "Sing the Sorrow") == 0);
	assert (SbMusicBrainzAlbumFamilyTitle (
			"From Under the Cork Tree (20th Anniversary Deluxe)",
			family, sizeof (family)));
	assert (strcmp (family, "From Under the Cork Tree") == 0);
	assert (!SbMusicBrainzAlbumFamilyTitle ("Songs: Ohia",
			family, sizeof (family)));
	assert (strcmp (family, "Songs: Ohia") == 0);
	assert (!SbMusicBrainzAlbumFamilyTitle ("The Wall (Disc 2)",
			family, sizeof (family)));
	assert (strcmp (family, "The Wall (Disc 2)") == 0);
	assert (!SbMusicBrainzAlbumFamilyTitle ("Soundtrack, Vol. 2",
			family, sizeof (family)));

	/* Exact lookup has no usable release; the one conservative family query
	 * selects an edition, then the same post-selection completion state machine
	 * requires release detail followed by release-group detail. */
	const char exact[] = "{\"releases\":[]}";
	const char familySearch[] = "{\"releases\":[{\"id\":\"unrelated-compilation\","
			"\"title\":\"Goth Rock Collection\",\"status\":\"Official\","
			"\"artist-credit\":[{\"name\":\"Various Artists\"}],"
			"\"release-group\":{\"id\":\"comp-group\",\"primary-type\":\"Album\","
			"\"secondary-types\":[\"Compilation\"]}},{\"id\":\"afi-deluxe\","
			"\"title\":\"Sing the Sorrow (Deluxe)\",\"date\":\"2023\","
			"\"status\":\"Official\",\"artist-credit\":[{\"name\":\"AFI\"}],"
			"\"release-group\":{\"id\":\"afi-group\"}}]}";
	const char releaseDetail[] = "{\"id\":\"afi-deluxe\","
			"\"title\":\"Sing the Sorrow (Deluxe)\",\"date\":\"2023-03-10\","
			"\"country\":\"US\",\"label-info\":[{\"catalog-number\":\"AFI-20\","
			"\"label\":{\"name\":\"DreamWorks\"}}],"
			"\"release-group\":{\"id\":\"afi-group\"}}";
	const char groupDetail[] = "{\"id\":\"afi-group\","
			"\"primary-type\":\"Album\",\"first-release-date\":\"2003-03-11\","
			"\"tags\":[{\"name\":\"alternative rock\",\"count\":4}]}";
	SbTrackIdentity id; SbMetadataResult result;
	SbTrackIdentitySet (&id, "AFI", "Girl's Not Grey",
			"Sing the Sorrow (Deluxe)", NULL, 0);
	SbMetadataResultInit (&result);
	assert (!SbMusicBrainzSelectRelease (exact, &id, &result));
	assert (SbMusicBrainzSelectRelease (familySearch, &id, &result));
	assert (strcmp (result.releaseId, "afi-deluxe") == 0);
	assert (SbMusicBrainzNextCompletion (&result, "", "") ==
			SB_METADATA_COMPLETION_RELEASE);
	assert (SbMusicBrainzApplyReleaseDetail (releaseDetail, &result));
	assert (SbMusicBrainzNextCompletion (&result, result.releaseId, "") ==
			SB_METADATA_COMPLETION_RELEASE_GROUP);
	assert (SbMusicBrainzApplyReleaseGroupDetail (groupDetail, &result));
	assert (SbMusicBrainzNextCompletion (&result, result.releaseId,
			result.releaseGroupId) == SB_METADATA_COMPLETION_NONE);
	assert (strcmp (result.firstReleaseDate, "2003-03-11") == 0);
	assert (result.categorySource == SB_METADATA_CATEGORIES_TAGS);
	char display[1024];
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Original Release: 03/11/2003") != NULL);
	assert (strstr (display, "Edition Release: 03/10/2023") != NULL);

	/* A release chosen directly by the album lookup is not terminal: missing
	 * group facts explicitly schedule the downstream group request. */
	const char pageAvenue[] = "{\"releases\":[{\"id\":\"page-release\","
			"\"title\":\"Page Avenue\",\"date\":\"2004-05-10\",\"country\":\"XE\","
			"\"status\":\"Official\",\"artist-credit\":[{\"name\":\"Story of the Year\"}],"
			"\"label-info\":[{\"catalog-number\":\"9 48438-2\","
			"\"label\":{\"name\":\"Maverick\"}}],"
			"\"release-group\":{\"id\":\"page-group\"}}]}";
	SbTrackIdentitySet (&id, "Story Of The Year", "Anthem of Our Dying Day",
			"Page Avenue", NULL, 0);
	SbMetadataResultInit (&result);
	assert (SbMusicBrainzSelectRelease (pageAvenue, &id, &result));
	assert (SbMusicBrainzNextCompletion (&result, "", "") ==
			SB_METADATA_COMPLETION_RELEASE_GROUP);
	const char pageGroup[] = "{\"id\":\"page-group\","
			"\"primary-type\":\"Album\",\"first-release-date\":\"2003-09-16\","
			"\"genres\":[{\"name\":\"Post-Hardcore\",\"count\":3}]}";
	assert (SbMusicBrainzApplyReleaseGroupDetail (pageGroup, &result));
	assert (strcmp (result.releaseId, "page-release") == 0);
	assert (strcmp (result.releaseDate, "2004-05-10") == 0);
	assert (strcmp (result.firstReleaseDate, "2003-09-16") == 0);
	assert (result.categorySource == SB_METADATA_CATEGORIES_GENRES);
	assert (SbMetadataFormatAvailableFields (&result, display, sizeof (display)));
	assert (strstr (display, "Original Release: 09/16/2003") != NULL);
	assert (strstr (display, "Edition Release: 05/10/2004") != NULL);
	assert (strstr (display, "Country: Europe") != NULL);
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
	metadataStatus; unsigned int lyricsDelayMs, metadataDelayMs;
	char firstLyricsTitle[SB_ENRICH_TEXT_MAX]; } MockProviders;

static bool mockMetadata (const SbTrackIdentity *id, SbMetadataResult *result, void *data) {
	MockProviders *mock = data; mock->metadataCalls++;
	if (mock->metadataDelayMs > 0) SbPlatformSleepMs (mock->metadataDelayMs);
	SbMetadataResultInit (result);
	result->status = mock->metadataStatus; (void) id;
	return result->status == SB_LOOKUP_AVAILABLE;
}

static bool mockLyrics (const SbTrackIdentity *id, SbLyricsResult *result, void *data) {
	MockProviders *mock = data; mock->lyricsCalls++; SbLyricsResultInit (result);
	if (mock->firstLyricsTitle[0] == '\0')
		snprintf (mock->firstLyricsTitle, sizeof (mock->firstLyricsTitle), "%s",
				id->title);
	if (mock->lyricsDelayMs > 0) SbPlatformSleepMs (mock->lyricsDelayMs);
	strcpy (result->provider, "MockLyrics"); result->status = mock->lyricsStatus;
	if (result->status == SB_LOOKUP_AVAILABLE) {
		strcpy (result->artist, id->artist); strcpy (result->title, id->title);
		const char fixture[] = "Signal in the static\nGreen phosphor in the night";
		const char synced[] = "[00:01.00]Signal in the static\n[00:04.00]Green phosphor in the night";
		result->plainLyrics = malloc (sizeof (fixture));
		if (result->plainLyrics != NULL) memcpy (result->plainLyrics, fixture, sizeof (fixture));
		result->syncedLyrics = malloc (sizeof (synced));
		if (result->syncedLyrics != NULL)
			memcpy (result->syncedLyrics, synced, sizeof (synced));
	}
	return result->status == SB_LOOKUP_AVAILABLE;
}

/* The worker may intentionally wait 1000 ms between uncached provider calls.
 * Keep a scheduling margin, but return immediately when publication arrives. */
enum { ASYNC_RESULT_TIMEOUT_MS = 2000, ASYNC_POLL_MS = 10 };

static bool waitLyricsFor (SbMetadataResolver *resolver, uint64_t generation,
		SbLyricsResult *result, const uint64_t timeoutMs) {
	const uint64_t start = SbPlatformMonotonicMs ();
	const uint64_t deadline = start + timeoutMs;
	for (;;) {
		if (SbLyricsResolverPoll (resolver, generation, result)) return true;
		const uint64_t now = SbPlatformMonotonicMs ();
		if (now >= deadline) return false;
		const uint64_t remaining = deadline - now;
		SbPlatformSleepMs ((unsigned int) (remaining < ASYNC_POLL_MS ?
				remaining : ASYNC_POLL_MS));
	}
}

static bool waitLyrics (SbMetadataResolver *resolver, uint64_t generation,
		SbLyricsResult *result) {
	return waitLyricsFor (resolver, generation, result,
			ASYNC_RESULT_TIMEOUT_MS);
}

static bool waitMetadata (SbMetadataResolver *resolver, uint64_t generation,
		SbMetadataResult *result) {
	const uint64_t deadline = SbPlatformMonotonicMs () +
			ASYNC_RESULT_TIMEOUT_MS;
	for (;;) {
		if (SbMetadataResolverPoll (resolver, generation, result)) return true;
		const uint64_t now = SbPlatformMonotonicMs ();
		if (now >= deadline) return false;
		const uint64_t remaining = deadline - now;
		SbPlatformSleepMs ((unsigned int) (remaining < ASYNC_POLL_MS ?
				remaining : ASYNC_POLL_MS));
	}
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

static SbEnrichmentPrefetchSchedule waitPrefetchComplete (
		SbMetadataResolver *resolver, const SbTrackIdentity *identity,
		const SbEnrichmentPriority priority) {
	const uint64_t deadline = SbPlatformMonotonicMs () + 5000;
	for (;;) {
		const SbEnrichmentPrefetchSchedule state =
				SbMetadataResolverPrefetch (resolver, identity, priority);
		if (state == SB_ENRICH_PREFETCH_CACHE_HIT ||
				state == SB_ENRICH_PREFETCH_RECENT_ATTEMPT) return state;
		assert (SbPlatformMonotonicMs () < deadline);
		SbPlatformSleepMs (ASYNC_POLL_MS);
	}
}

static void testBoundedPrefetchPriorityAndPromotion (void) {
	SbMetadataResolver resolver;
	MockProviders mock = {.lyricsStatus = SB_LOOKUP_AVAILABLE,
			.metadataStatus = SB_LOOKUP_AVAILABLE, .metadataDelayMs = 100};
	SbTrackIdentity next, next2, current;
	SbTrackIdentitySet (&next, "Queued Artist", "Next Signal", NULL, NULL, 181);
	SbTrackIdentitySet (&next2, "Queued Artist", "Second Signal", NULL, NULL, 182);
	SbTrackIdentitySet (&current, "Visible Artist", "Foreground Signal", NULL,
			NULL, 183);
	SbMetadataResolverInit (&resolver);
	resolver.provider = (SbMetadataProvider) {"mock-metadata", mockMetadata, &mock};
	resolver.lyricsProvider = (SbLyricsProvider) {"mock-lyrics", mockLyrics, &mock};
	assert (SbMetadataResolverStart (&resolver));

	assert (SbMetadataResolverPrefetch (&resolver, &next,
			SB_ENRICH_PRIORITY_NEXT) == SB_ENRICH_PREFETCH_SCHEDULED);
	/* A copied stable identity deduplicates both queued and active work. */
	assert (SbMetadataResolverPrefetch (&resolver, &next,
			SB_ENRICH_PRIORITY_NEXT) == SB_ENRICH_PREFETCH_IN_FLIGHT);
	/* +2 remains opportunistic while next-track work is outstanding. */
	assert (SbMetadataResolverPrefetch (&resolver, &next2,
			SB_ENRICH_PRIORITY_NEXT2) == SB_ENRICH_PREFETCH_HIGHER_PRIORITY);

	/* Foreground work enters the same serialized provider worker at a higher
	 * priority. It runs before the deferred speculative LRCLIB stage. */
	SbMetadataResolverRequest (&resolver, &current, 70);
	SbMetadataResult metadata;
	SbLyricsResult lyrics;
	assert (waitMetadata (&resolver, 70, &metadata));
	assert (waitLyricsFor (&resolver, 70, &lyrics, 5000));
	assert (strcmp (mock.firstLyricsTitle, current.title) == 0);
	SbLyricsResultDestroy (&lyrics);

	assert (waitPrefetchComplete (&resolver, &next,
			SB_ENRICH_PRIORITY_NEXT) == SB_ENRICH_PREFETCH_CACHE_HIT);
	const int metadataCalls = mock.metadataCalls;
	const int lyricsCalls = mock.lyricsCalls;
	/* Promotion rebinds speculative data to the real generation and publishes
	 * metadata, parsed lyrics, and cached/no-match art without provider work. */
	SbMetadataResolverRequest (&resolver, &next, 71);
	assert (SbMetadataResolverPoll (&resolver, 71, &metadata));
	assert (SbLyricsResolverPoll (&resolver, 71, &lyrics));
	assert (lyrics.status == SB_LOOKUP_AVAILABLE && lyrics.syncedLyrics != NULL);
	SbLyricsResultDestroy (&lyrics);
	SbAlbumArtResult art;
	assert (SbAlbumArtResolverPoll (&resolver, 71, &art));
	assert (art.status == SB_LOOKUP_NO_MATCH);
	SbPlatformSleepMs (30);
	assert (mock.metadataCalls == metadataCalls && mock.lyricsCalls == lyricsCalls);

	/* Once next is complete and foreground is idle, exactly one +2 identity is
	 * admitted; the scheduler still never walks beyond this caller-supplied slot. */
	assert (SbMetadataResolverPrefetch (&resolver, &next2,
			SB_ENRICH_PRIORITY_NEXT2) == SB_ENRICH_PREFETCH_SCHEDULED);
	assert (!SbMetadataResolverPoll (&resolver, 999, &metadata));
	SbMetadataResolverDestroy (&resolver);
}

static void testPrefetchCancellationWithoutQueuePointers (void) {
	SbMetadataResolver resolver;
	SbTrackIdentity next, next2;
	SbTrackIdentitySet (&next, "Old Station", "Queued One", NULL, NULL, 200);
	SbTrackIdentitySet (&next2, "Old Station", "Queued Two", NULL, NULL, 201);
	SbMetadataResolverInit (&resolver);
	/* No worker is started: cancellation deterministically proves that station
	 * changes remove copied speculative jobs without touching reusable caches. */
	assert (SbMetadataResolverPrefetch (&resolver, &next,
			SB_ENRICH_PRIORITY_NEXT) == SB_ENRICH_PREFETCH_SCHEDULED);
	assert (SbMetadataResolverPrefetch (&resolver, &next2,
			SB_ENRICH_PRIORITY_NEXT2) == SB_ENRICH_PREFETCH_HIGHER_PRIORITY);
	SbMetadataResolverCancelPrefetch (&resolver);
	assert (!resolver.jobs[SB_ENRICH_PRIORITY_NEXT].pending);
	assert (!resolver.jobs[SB_ENRICH_PRIORITY_NEXT2].pending);
	assert (!resolver.artJobs[SB_ENRICH_PRIORITY_NEXT].pending);
	assert (!resolver.artJobs[SB_ENRICH_PRIORITY_NEXT2].pending);
	SbMetadataResolverDestroy (&resolver);
}

static void testRapidCurrentReplacementPublishesNewestOnly (void) {
	SbMetadataResolver resolver;
	MockProviders mock = {.lyricsStatus = SB_LOOKUP_AVAILABLE,
			.metadataStatus = SB_LOOKUP_AVAILABLE, .metadataDelayMs = 75};
	SbTrackIdentity tracks[4];
	SbMetadataResolverInit (&resolver);
	resolver.provider = (SbMetadataProvider) {"mock-metadata", mockMetadata, &mock};
	resolver.lyricsProvider = (SbLyricsProvider) {"mock-lyrics", mockLyrics, &mock};
	assert (SbMetadataResolverStart (&resolver));
	for (size_t i = 0; i < 4; i++) {
		char title[32]; snprintf (title, sizeof (title), "Rapid %zu", i + 1);
		SbTrackIdentitySet (&tracks[i], "Skip Artist", title, NULL, NULL,
				200 + (unsigned int) i);
		SbMetadataResolverRequest (&resolver, &tracks[i], 80 + i);
	}
	SbMetadataResult metadata;
	SbLyricsResult lyrics;
	assert (waitMetadata (&resolver, 83, &metadata));
	assert (waitLyricsFor (&resolver, 83, &lyrics, 5000));
	assert (strcmp (lyrics.title, "Rapid 4") == 0);
	SbLyricsResultDestroy (&lyrics);
	assert (!SbMetadataResolverPoll (&resolver, 80, &metadata));
	assert (mock.metadataCalls <= 2 && mock.lyricsCalls == 1);
	SbMetadataResolverDestroy (&resolver);
}

int main (void) {
	testNormalization (); testRetainedModalScroll (); testRetainedModalScrollBounds ();
	testCursesWheelStateMapping (); testReadOnlyModalWheelIntegration ();
	testMouseBitNames (); testUpcomingHeight ();
	testMusicBrainzHttpStates (); testLrclibTransientPolicy ();
	testCacheKey (); testBestMatch (); testRichMusicBrainzMetadata ();
	testAlbumDateSemanticsRegressions (); testDeathCabFallbackAndJackKaysHappyPath ();
	testProgressiveMetadataPublication ();
	testDeluxeDatesAndDetailMerge (); testWeakMetadataAndTransientDetail ();
	testTagFallbackAndMetadataCacheRoundTrip (); testAlbumAwareArtRelease ();
	testAlbumFamilyFallbackAndCompletion ();
	testMusicBrainzDateFormatting ();
	testErrorsAndNoMatch (); testStaleResult (); testLrclibPlainAndSynced ();
	testLrclibSyncedOnlyAndInstrumental (); testLrclibErrorsAndFallback ();
	testLrclibSearchScoring ();
	testLyricsResolverCacheAndFailures ();
	testProviderStatesAreIndependent ();
	testTransientMetadataIsNotCached ();
	testBoundedPrefetchPriorityAndPromotion ();
	testPrefetchCancellationWithoutQueuePointers ();
	testRapidCurrentReplacementPublishesNewestOnly ();
	puts ("enrichment tests passed"); return 0;
}
