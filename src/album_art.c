#include "album_art.h"
#include "enrichment.h"
#include "platform.h"
#include "version.h"

#include <ctype.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
	unsigned char *data;
	size_t length;
	bool overflow;
} ArtBuffer;

const char *SbAlbumArtProviderName (const SbAlbumArtProvider provider) {
	return provider == SB_ART_PROVIDER_COVER_ART_ARCHIVE ?
			"Cover Art Archive" : "None";
}

const char *SbAlbumArtStepName (const SbAlbumArtResolutionStep step) {
	switch (step) {
		case SB_ART_STEP_EXACT_RELEASE: return "exact_release";
		case SB_ART_STEP_RELEASE_GROUP: return "release_group";
		case SB_ART_STEP_ALTERNATE_RELEASE: return "alternate_release";
		case SB_ART_STEP_ALBUM_FAMILY: return "album_family";
		default: return "done";
	}
}

SbAlbumArtResolutionStep SbAlbumArtNextStep (
		const SbAlbumArtResolutionStep step, const int status,
		const bool hasReleaseGroup, const bool hasAlternateRelease,
		const bool hasAlbumFamily) {
	if (status == SB_LOOKUP_AVAILABLE || status == SB_LOOKUP_UNAVAILABLE ||
			status == SB_LOOKUP_ERROR) return SB_ART_STEP_DONE;
	if (step < SB_ART_STEP_RELEASE_GROUP && hasReleaseGroup)
		return SB_ART_STEP_RELEASE_GROUP;
	if (step < SB_ART_STEP_ALTERNATE_RELEASE && hasAlternateRelease)
		return SB_ART_STEP_ALTERNATE_RELEASE;
	if (step < SB_ART_STEP_ALBUM_FAMILY && hasAlbumFamily)
		return SB_ART_STEP_ALBUM_FAMILY;
	return SB_ART_STEP_DONE;
}

void SbAlbumArtResultInit (SbAlbumArtResult *result) {
	memset (result, 0, sizeof (*result));
	result->status = SB_LOOKUP_IDLE;
	result->providerKind = SB_ART_PROVIDER_NONE;
	snprintf (result->provider, sizeof (result->provider), "%s",
			SbAlbumArtProviderName (SB_ART_PROVIDER_NONE));
	snprintf (result->reason, sizeof (result->reason), "idle");
}

int SbCoverArtHttpStatus (const long status, const int curlCode) {
	if (curlCode != CURLE_OK) return SB_LOOKUP_ERROR;
	if (status == 200) return SB_LOOKUP_AVAILABLE;
	if (status == 404) return SB_LOOKUP_NO_MATCH;
	if (status == 429 || status >= 500) return SB_LOOKUP_UNAVAILABLE;
	return SB_LOOKUP_ERROR;
}

bool SbAlbumArtMimeSupported (const char *mime) {
	return mime != NULL && (!strcasecmp (mime, "image/jpeg") ||
			!strcasecmp (mime, "image/png") ||
			!strcasecmp (mime, "image/webp"));
}

static bool safeIdentity (const char *identity) {
	if (identity == NULL || *identity == '\0') return false;
	for (const unsigned char *p = (const unsigned char *) identity; *p; p++)
		if (!isalnum (*p) && *p != '-') return false;
	return true;
}

static const char *mimeExtension (const char *mime) {
	return mime == NULL ? "jpg" : strstr (mime, "png") != NULL ? "png" :
			strstr (mime, "webp") != NULL ? "webp" : "jpg";
}

bool SbAlbumArtFilename (const char *identity, const char *mime,
		char *out, const size_t size) {
	if (!safeIdentity (identity) || out == NULL || size == 0) return false;
	const int written = snprintf (out, size, "%s.%s", identity,
			mimeExtension (mime));
	return written > 0 && (size_t) written < size;
}

bool SbAlbumArtCacheFilename (const SbAlbumArtProvider provider,
		const SbAlbumArtIdentityKind kind, const char *identity,
		const char *mime, char *out, const size_t size) {
	if (provider != SB_ART_PROVIDER_COVER_ART_ARCHIVE ||
			(kind != SB_ART_IDENTITY_RELEASE &&
			kind != SB_ART_IDENTITY_RELEASE_GROUP) ||
			!safeIdentity (identity) || out == NULL || size == 0) return false;
	const int written = snprintf (out, size, "caa-%s-%s.%s",
			kind == SB_ART_IDENTITY_RELEASE ? "release" : "group",
			identity, mimeExtension (mime));
	return written > 0 && (size_t) written < size;
}

static const char *jsonString (json_object *object, const char *key) {
	json_object *value = NULL;
	return json_object_object_get_ex (object, key, &value) &&
			json_object_is_type (value, json_type_string) ?
			json_object_get_string (value) : "";
}

static void releaseFromUrl (const char *url, char *out, const size_t size) {
	if (url == NULL || out == NULL || size == 0) return;
	const char *value = strrchr (url, '/');
	value = value != NULL ? value + 1 : url;
	if (safeIdentity (value)) snprintf (out, size, "%s", value);
}

static bool coverArtParse (const char *json, const char *identity,
		const SbAlbumArtIdentityKind kind, SbAlbumArtResult *result) {
	SbAlbumArtResultInit (result);
	result->providerKind = SB_ART_PROVIDER_COVER_ART_ARCHIVE;
	result->identityKind = kind;
	result->confidence = kind == SB_ART_IDENTITY_RELEASE ? 1.0 : 0.96;
	snprintf (result->provider, sizeof (result->provider), "%s",
			SbAlbumArtProviderName (result->providerKind));
	if (kind == SB_ART_IDENTITY_RELEASE)
		snprintf (result->releaseId, sizeof (result->releaseId), "%s",
				identity != NULL ? identity : "");
	else
		snprintf (result->releaseGroupId, sizeof (result->releaseGroupId), "%s",
				identity != NULL ? identity : "");
	json_object *root = json_tokener_parse (json), *images = NULL;
	if (root == NULL || !json_object_object_get_ex (root, "images", &images) ||
			!json_object_is_type (images, json_type_array)) {
		if (root != NULL) json_object_put (root);
		result->status = SB_LOOKUP_ERROR;
		return false;
	}
	json_object *best = NULL;
	for (size_t i = 0; i < json_object_array_length (images); i++) {
		json_object *candidate = json_object_array_get_idx (images, i);
		json_object *front = NULL;
		if (json_object_object_get_ex (candidate, "front", &front) &&
				json_object_get_boolean (front)) {
			best = candidate;
			break;
		}
	}
	if (best == NULL) {
		result->status = SB_LOOKUP_NO_MATCH;
		json_object_put (root);
		return false;
	}
	json_object *thumbnails = NULL;
	if (json_object_object_get_ex (best, "thumbnails", &thumbnails)) {
		const char *url = jsonString (thumbnails, "500");
		if (*url == '\0') url = jsonString (thumbnails, "large");
		snprintf (result->sourceUrl, sizeof (result->sourceUrl), "%s", url);
	}
	if (result->sourceUrl[0] == '\0')
		snprintf (result->sourceUrl, sizeof (result->sourceUrl), "%s",
				jsonString (best, "image"));
	if (kind == SB_ART_IDENTITY_RELEASE_GROUP)
		releaseFromUrl (jsonString (root, "release"), result->releaseId,
				sizeof (result->releaseId));
	if (result->sourceUrl[0] == '\0') {
		result->status = SB_LOOKUP_ERROR;
		json_object_put (root);
		return false;
	}
	result->status = SB_LOOKUP_AVAILABLE;
	json_object_put (root);
	return true;
}

bool SbCoverArtParse (const char *json, const char *release,
		SbAlbumArtResult *result) {
	return coverArtParse (json, release, SB_ART_IDENTITY_RELEASE, result);
}

bool SbCoverArtParseGroup (const char *json, const char *releaseGroup,
		SbAlbumArtResult *result) {
	return coverArtParse (json, releaseGroup,
			SB_ART_IDENTITY_RELEASE_GROUP, result);
}

static size_t artWrite (char *data, const size_t size, const size_t count,
		void *userdata) {
	ArtBuffer *buffer = userdata;
	const size_t add = size * count;
	if (add > SB_ART_MAX_BYTES - buffer->length) {
		buffer->overflow = true;
		return 0;
	}
	void *next = realloc (buffer->data, buffer->length + add + 1);
	if (next == NULL) return 0;
	buffer->data = next;
	memcpy (buffer->data + buffer->length, data, add);
	buffer->length += add;
	buffer->data[buffer->length] = 0;
	return add;
}

static bool fetch (const char *url, ArtBuffer *buffer, long *status,
		char **type) {
	CURL *curl = curl_easy_init ();
	if (curl == NULL) return false;
	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" VERSION
			" (https://github.com/signalbox-player/signalbox)");
	curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt (curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, artWrite);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);
	const CURLcode code = curl_easy_perform (curl);
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, status);
	if (type != NULL) {
		char *contentType = NULL;
		curl_easy_getinfo (curl, CURLINFO_CONTENT_TYPE, &contentType);
		*type = contentType != NULL ? strdup (contentType) : NULL;
	}
	curl_easy_cleanup (curl);
	return code == CURLE_OK;
}

static void setFailureReason (SbAlbumArtResult *result) {
	snprintf (result->reason, sizeof (result->reason), "%s",
			result->status == SB_LOOKUP_NO_MATCH ? "no_match" :
			result->status == SB_LOOKUP_UNAVAILABLE ?
			"temporarily_unavailable" : "download_error");
}

static bool writeImage (const char *directory, const char *identity,
		SbAlbumArtResult *result, const ArtBuffer *image) {
	char name[128];
	if (!SbAlbumArtCacheFilename (result->providerKind, result->identityKind,
			identity, result->mimeType, name, sizeof (name))) return false;
	char *path = SbPlatformJoinPath (directory, name);
	char *temporary = path != NULL ? malloc (strlen (path) + 5) : NULL;
	if (path == NULL || temporary == NULL) {
		free (path); free (temporary);
		return false;
	}
	sprintf (temporary, "%s.tmp", path);
	FILE *file = fopen (temporary, "wb");
	bool ok = file != NULL;
	if (file != NULL) {
		ok = fwrite (image->data, 1, image->length, file) == image->length &&
				fflush (file) == 0;
		if (fclose (file) != 0) ok = false;
	}
	if (ok) ok = SbPlatformAtomicReplace (temporary, path);
	remove (temporary);
	if (ok) snprintf (result->cachedPath, sizeof (result->cachedPath),
			"%s", path);
	free (path); free (temporary);
	return ok;
}

static bool coverArtLookup (const char *identity,
		const SbAlbumArtIdentityKind kind, const char *directory,
		SbAlbumArtResult *result) {
	const uint64_t started = SbPlatformMonotonicMs ();
	SbAlbumArtResultInit (result);
	result->providerKind = SB_ART_PROVIDER_COVER_ART_ARCHIVE;
	result->identityKind = kind;
	snprintf (result->provider, sizeof (result->provider), "%s",
			SbAlbumArtProviderName (result->providerKind));
	if (!safeIdentity (identity) || directory == NULL) {
		snprintf (result->reason, sizeof (result->reason), "no_release_identity");
		return false;
	}
	char url[256];
	snprintf (url, sizeof (url), "https://coverartarchive.org/%s/%s",
			kind == SB_ART_IDENTITY_RELEASE ? "release" : "release-group",
			identity);
	ArtBuffer metadata = {0}; long status = 0;
	if (!fetch (url, &metadata, &status, NULL)) {
		result->httpStatus = status;
		result->status = metadata.overflow ? SB_LOOKUP_ERROR :
				SbCoverArtHttpStatus (status, CURLE_RECV_ERROR);
		setFailureReason (result);
		free (metadata.data);
		result->resolutionElapsedMs = SbPlatformMonotonicMs () - started;
		return false;
	}
	result->httpStatus = status;
	result->status = SbCoverArtHttpStatus (status, CURLE_OK);
	if (result->status != SB_LOOKUP_AVAILABLE) {
		setFailureReason (result);
		free (metadata.data);
		result->resolutionElapsedMs = SbPlatformMonotonicMs () - started;
		return false;
	}
	const bool parsed = kind == SB_ART_IDENTITY_RELEASE ?
			SbCoverArtParse ((char *) metadata.data, identity, result) :
			SbCoverArtParseGroup ((char *) metadata.data, identity, result);
	free (metadata.data);
	if (!parsed) {
		setFailureReason (result);
		result->resolutionElapsedMs = SbPlatformMonotonicMs () - started;
		return false;
	}
	const uint64_t downloadStarted = SbPlatformMonotonicMs ();
	ArtBuffer image = {0}; char *type = NULL;
	const bool fetched = fetch (result->sourceUrl, &image, &status, &type);
	result->downloadElapsedMs = SbPlatformMonotonicMs () - downloadStarted;
	if (!fetched || status != 200 || image.overflow ||
			!SbAlbumArtMimeSupported (type)) {
		result->httpStatus = status;
		result->byteCount = image.length;
		result->status = fetched ? SbCoverArtHttpStatus (status, CURLE_OK) :
				SB_LOOKUP_ERROR;
		if (result->status == SB_LOOKUP_AVAILABLE) result->status = SB_LOOKUP_ERROR;
		if (fetched && status == 200 && !image.overflow &&
				!SbAlbumArtMimeSupported (type))
			snprintf (result->reason, sizeof (result->reason),
					"unsupported_format");
		else setFailureReason (result);
		free (type); free (image.data);
		result->resolutionElapsedMs = SbPlatformMonotonicMs () - started;
		return false;
	}
	result->httpStatus = status;
	result->byteCount = image.length;
	snprintf (result->mimeType, sizeof (result->mimeType), "%s", type);
	free (type);
	const bool ok = writeImage (directory, identity, result, &image);
	free (image.data);
	if (ok) snprintf (result->reason, sizeof (result->reason), "available");
	else {
		result->status = SB_LOOKUP_ERROR;
		snprintf (result->reason, sizeof (result->reason), "download_error");
	}
	result->resolutionElapsedMs = SbPlatformMonotonicMs () - started;
	return ok;
}

bool SbCoverArtLookup (const char *release, const char *directory,
		SbAlbumArtResult *result) {
	return coverArtLookup (release, SB_ART_IDENTITY_RELEASE,
			directory, result);
}

bool SbCoverArtGroupLookup (const char *releaseGroup, const char *directory,
		SbAlbumArtResult *result) {
	return coverArtLookup (releaseGroup, SB_ART_IDENTITY_RELEASE_GROUP,
			directory, result);
}
