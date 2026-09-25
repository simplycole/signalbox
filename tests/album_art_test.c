#include "album_art.h"
#include "enrichment.h"

#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static void TestCoverArtArchiveRelease (void) {
	SbAlbumArtResult result;
	const char *json = "{\"images\":["
			"{\"front\":false,\"image\":\"https://x/back.jpg\"},"
			"{\"front\":true,\"image\":\"https://x/full.jpg\","
			"\"thumbnails\":{\"500\":\"https://x/500.jpg\"}}]}";
	assert (SbCoverArtParse (json, "abc-def", &result));
	assert (result.providerKind == SB_ART_PROVIDER_COVER_ART_ARCHIVE);
	assert (result.identityKind == SB_ART_IDENTITY_RELEASE);
	assert (!strcmp (result.provider, "Cover Art Archive"));
	assert (!strcmp (result.sourceUrl, "https://x/500.jpg"));
	assert (!strcmp (result.releaseId, "abc-def"));
	assert (result.confidence == 1.0);
	assert (!SbCoverArtParse ("{\"images\":[{\"front\":false,"
			"\"image\":\"https://x/back.jpg\"}]}", "x", &result));
	assert (result.status == SB_LOOKUP_NO_MATCH);
}

static void TestCoverArtArchiveReleaseGroup (void) {
	SbAlbumArtResult result;
	const char *json = "{\"release\":\"https://musicbrainz.org/release/"
			"release-from-group\",\"images\":[{\"front\":true,"
			"\"image\":\"https://x/group-full.png\",\"thumbnails\":{"
			"\"large\":\"https://x/group-500.jpg\"}}]}";
	assert (SbCoverArtParseGroup (json, "group-id", &result));
	assert (result.identityKind == SB_ART_IDENTITY_RELEASE_GROUP);
	assert (!strcmp (result.releaseGroupId, "group-id"));
	assert (!strcmp (result.releaseId, "release-from-group"));
	assert (!strcmp (result.sourceUrl, "https://x/group-500.jpg"));
	assert (result.confidence > .9 && result.confidence < 1.0);
}

static void TestProviderLadder (void) {
	/* Authoritative misses advance in priority order. Success and transient
	 * failures terminate the ladder rather than selecting lower-confidence art. */
	assert (SbAlbumArtNextStep (SB_ART_STEP_EXACT_RELEASE,
			SB_LOOKUP_NO_MATCH, true, true, true) ==
			SB_ART_STEP_RELEASE_GROUP);
	assert (SbAlbumArtNextStep (SB_ART_STEP_RELEASE_GROUP,
			SB_LOOKUP_NO_MATCH, true, true, true) ==
			SB_ART_STEP_ALTERNATE_RELEASE);
	assert (SbAlbumArtNextStep (SB_ART_STEP_ALTERNATE_RELEASE,
			SB_LOOKUP_NO_MATCH, true, true, true) ==
			SB_ART_STEP_ALBUM_FAMILY);
	assert (SbAlbumArtNextStep (SB_ART_STEP_RELEASE_GROUP,
			SB_LOOKUP_AVAILABLE, true, true, true) == SB_ART_STEP_DONE);
	assert (SbAlbumArtNextStep (SB_ART_STEP_EXACT_RELEASE,
			SB_LOOKUP_UNAVAILABLE, true, true, true) == SB_ART_STEP_DONE);
	assert (SbAlbumArtNextStep (SB_ART_STEP_EXACT_RELEASE,
			SB_LOOKUP_NO_MATCH, false, false, false) == SB_ART_STEP_DONE);
	assert (!strcmp (SbAlbumArtStepName (SB_ART_STEP_RELEASE_GROUP),
			"release_group"));
}

static void TestCacheIdentity (void) {
	char release[128], group[128];
	assert (SbAlbumArtFilename ("abc-def", "image/png", release,
			sizeof (release)));
	assert (!strcmp (release, "abc-def.png"));
	assert (!SbAlbumArtFilename ("../bad", "image/jpeg", release,
			sizeof (release)));
	assert (SbAlbumArtCacheFilename (SB_ART_PROVIDER_COVER_ART_ARCHIVE,
			SB_ART_IDENTITY_RELEASE, "abc-def", "image/jpeg",
			release, sizeof (release)));
	assert (SbAlbumArtCacheFilename (SB_ART_PROVIDER_COVER_ART_ARCHIVE,
			SB_ART_IDENTITY_RELEASE_GROUP, "abc-def", "image/jpeg",
			group, sizeof (group)));
	assert (strcmp (release, group) != 0);
	assert (strstr (release, "caa-release-") == release);
	assert (strstr (group, "caa-group-") == group);
}

int main (void) {
	TestCoverArtArchiveRelease ();
	TestCoverArtArchiveReleaseGroup ();
	TestProviderLadder ();
	TestCacheIdentity ();
	assert (SbCoverArtHttpStatus (404, CURLE_OK) == SB_LOOKUP_NO_MATCH);
	assert (SbCoverArtHttpStatus (503, CURLE_OK) == SB_LOOKUP_UNAVAILABLE);
	assert (SbCoverArtHttpStatus (429, CURLE_OK) == SB_LOOKUP_UNAVAILABLE);
	assert (SbCoverArtHttpStatus (200, CURLE_COULDNT_CONNECT) ==
			SB_LOOKUP_ERROR);
	SbAlbumArtResult result;
	assert (!SbCoverArtParse ("bad", "x", &result) &&
			result.status == SB_LOOKUP_ERROR);
	assert (SbAlbumArtMimeSupported ("image/webp"));
	puts ("album art tests passed");
	return 0;
}
