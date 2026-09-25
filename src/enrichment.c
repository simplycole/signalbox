#include "enrichment.h"
#include "version.h"
#include "platform.h"
#include "debug.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>

typedef struct { char *data; size_t length; } SbHttpBuffer;

#define enrichmentDebugPrint tuiDebugPrint

static pthread_mutex_t musicBrainzRateLock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t musicBrainzLastRequestMs;

unsigned int SbMusicBrainzRateDelayMs (const uint64_t lastRequestMs,
		const uint64_t nowMs) {
	if (lastRequestMs == 0) return 0;
	if (nowMs < lastRequestMs) return 1000;
	const uint64_t elapsed = nowMs - lastRequestMs;
	return elapsed >= 1000 ? 0 : (unsigned int) (1000 - elapsed);
}

static void musicBrainzRateWait (void) {
	pthread_mutex_lock (&musicBrainzRateLock);
	const uint64_t now = SbPlatformMonotonicMs ();
	const unsigned int delay = SbMusicBrainzRateDelayMs (
			musicBrainzLastRequestMs, now);
	if (delay > 0) SbPlatformSleepMs (delay);
	musicBrainzLastRequestMs = SbPlatformMonotonicMs ();
	pthread_mutex_unlock (&musicBrainzRateLock);
}

static void copyText (char *out, size_t size, const char *value) {
	if (size == 0) return;
	snprintf (out, size, "%s", value != NULL ? value : "");
}

void SbTrackIdentitySet (SbTrackIdentity *id, const char *artist,
		const char *title, const char *album, const char *station,
		unsigned int duration) {
	memset (id, 0, sizeof (*id));
	copyText (id->artist, sizeof (id->artist), artist);
	copyText (id->title, sizeof (id->title), title);
	copyText (id->album, sizeof (id->album), album);
	copyText (id->station, sizeof (id->station), station);
	id->duration = duration;
}

void SbTrackIdentitySetPandoraId (SbTrackIdentity *id, const char *pandoraId) {
	if (id == NULL) return;
	copyText (id->pandoraId, sizeof (id->pandoraId), pandoraId);
}

void SbTrackNormalize (const char *input, char *out, size_t size) {
	bool space = false; size_t n = 0;
	if (size == 0) return;
	for (const unsigned char *p = (const unsigned char *)
			(input != NULL ? input : ""); *p; p++) {
		/* Apostrophes are semantically equivalent whether straight or curly and
		 * do not form a word boundary ("don't" == "dont"). */
		if (*p == '\'' || (p[0] == 0xe2 && p[1] == 0x80 &&
				(p[2] == 0x98 || p[2] == 0x99))) {
			if (*p != '\'') p += 2;
			continue;
		}
		if (*p == '&') {
			if (space && n > 0 && n + 1 < size) out[n++] = ' ';
			const char andWord[] = "and";
			for (size_t i = 0; i < sizeof (andWord) - 1 && n + 1 < size; i++)
				out[n++] = andWord[i];
			space = true;
		} else if (isalnum (*p) || *p >= 128) {
			if (space && n > 0 && n + 1 < size) out[n++] = ' ';
			space = false;
			if (n + 1 < size) out[n++] = (char) (*p < 128 ? tolower (*p) : *p);
		} else space = n > 0;
	}
	out[n] = '\0';
}

static void trackKey (const char *provider, const SbTrackIdentity *id,
		const bool includePandoraId, char *out, const size_t size) {
	char artist[SB_ENRICH_TEXT_MAX], title[SB_ENRICH_TEXT_MAX];
	char album[SB_ENRICH_TEXT_MAX], joined[1200];
	SbTrackNormalize (id->artist, artist, sizeof (artist));
	SbTrackNormalize (id->title, title, sizeof (title));
	SbTrackNormalize (id->album, album, sizeof (album));
	snprintf (joined, sizeof (joined), "%s|%s|%s|%s|%u|%s", provider, artist,
			title, album, id->duration,
			includePandoraId ? id->pandoraId : "");
	uint64_t hash = UINT64_C (1469598103934665603);
	for (const unsigned char *p = (unsigned char *) joined; *p; p++)
		hash = (hash ^ *p) * UINT64_C (1099511628211);
	snprintf (out, size, "%s:%016llx", provider, (unsigned long long) hash);
}

void SbTrackCacheKey (const char *provider, const SbTrackIdentity *id,
		char *out, const size_t size) {
	trackKey (provider, id, false, out, size);
}

void SbTrackWorkKey (const SbTrackIdentity *id, char *out, const size_t size) {
	trackKey ("signalbox-track-v1", id, true, out, size);
}

void SbMetadataResultInit (SbMetadataResult *result) {
	memset (result, 0, sizeof (*result));
	result->status = SB_LOOKUP_IDLE;
}

void SbLyricsResultInit (SbLyricsResult *result) {
	memset (result, 0, sizeof (*result)); result->status = SB_LOOKUP_IDLE;
}

void SbLyricsResultDestroy (SbLyricsResult *result) {
	if (result == NULL) return;
	free (result->plainLyrics); free (result->syncedLyrics);
	SbLyricsResultInit (result);
}

bool SbLyricsResultCopy (SbLyricsResult *out, const SbLyricsResult *in) {
	SbLyricsResult copy = *in; copy.plainLyrics = copy.syncedLyrics = NULL;
	if (in->plainLyrics != NULL && (copy.plainLyrics = strdup (in->plainLyrics)) == NULL)
		return false;
	if (in->syncedLyrics != NULL && (copy.syncedLyrics = strdup (in->syncedLyrics)) == NULL) {
		free (copy.plainLyrics); return false;
	}
	SbLyricsResultDestroy (out); *out = copy; return true;
}

static const char *jsonString (json_object *obj, const char *key) {
	json_object *value = NULL;
	return obj != NULL && json_object_object_get_ex (obj, key, &value) &&
			json_object_is_type (value, json_type_string) ?
			json_object_get_string (value) : "";
}

char *SbMetadataSerialize (const SbMetadataResult *r) {
	json_object *o = json_object_new_object ();
	json_object_object_add (o, "status", json_object_new_int (r->status));
	json_object_object_add (o, "provider", json_object_new_string (r->provider));
	json_object_object_add (o, "artist", json_object_new_string (r->artist));
	json_object_object_add (o, "title", json_object_new_string (r->title));
	json_object_object_add (o, "release", json_object_new_string (r->release));
	json_object_object_add (o, "release_date", json_object_new_string (r->releaseDate));
	json_object_object_add (o, "first_release_date", json_object_new_string (r->firstReleaseDate));
	json_object_object_add (o, "release_type", json_object_new_string (r->releaseType));
	json_object_object_add (o, "release_country", json_object_new_string (r->releaseCountry));
	json_object_object_add (o, "label", json_object_new_string (r->label));
	json_object_object_add (o, "catalog_number", json_object_new_string (r->catalogNumber));
	json_object_object_add (o, "isrc", json_object_new_string (r->isrc));
	json_object_object_add (o, "genres", json_object_new_string (r->genres));
	json_object_object_add (o, "category_source",
			json_object_new_int (r->categorySource));
	json_object_object_add (o, "artist_id", json_object_new_string (r->artistId));
	json_object_object_add (o, "recording_id", json_object_new_string (r->recordingId));
	json_object_object_add (o, "release_id", json_object_new_string (r->releaseId));
	json_object_object_add (o, "release_group_id", json_object_new_string (r->releaseGroupId));
	json_object_object_add (o, "confidence", json_object_new_double (r->confidence));
	char *serialized = strdup (json_object_to_json_string_ext (o,
			JSON_C_TO_STRING_PLAIN));
	json_object_put (o); return serialized;
}

bool SbMetadataDeserialize (const char *serialized, SbMetadataResult *r) {
	json_object *o = json_tokener_parse (serialized), *v = NULL;
	if (o == NULL || !json_object_is_type (o, json_type_object)) {
		if (o != NULL) json_object_put (o); return false;
	}
	SbMetadataResultInit (r);
	copyText (r->provider, sizeof (r->provider), jsonString (o, "provider"));
	if (r->provider[0] == '\0') copyText (r->provider, sizeof (r->provider), "MusicBrainz");
	if (!json_object_object_get_ex (o, "status", &v) ||
			!json_object_is_type (v, json_type_int)) { json_object_put (o); return false; }
	r->status = (SbLookupStatus) json_object_get_int (v);
	copyText (r->artist, sizeof (r->artist), jsonString (o, "artist"));
	copyText (r->title, sizeof (r->title), jsonString (o, "title"));
	copyText (r->release, sizeof (r->release), jsonString (o, "release"));
	copyText (r->releaseDate, sizeof (r->releaseDate), jsonString (o, "release_date"));
	copyText (r->firstReleaseDate, sizeof (r->firstReleaseDate),
			jsonString (o, "first_release_date"));
	copyText (r->releaseType, sizeof (r->releaseType), jsonString (o, "release_type"));
	copyText (r->releaseCountry, sizeof (r->releaseCountry),
			jsonString (o, "release_country"));
	copyText (r->label, sizeof (r->label), jsonString (o, "label"));
	copyText (r->catalogNumber, sizeof (r->catalogNumber),
			jsonString (o, "catalog_number"));
	copyText (r->isrc, sizeof (r->isrc), jsonString (o, "isrc"));
	copyText (r->genres, sizeof (r->genres), jsonString (o, "genres"));
	if (json_object_object_get_ex (o, "category_source", &v) &&
			json_object_is_type (v, json_type_int))
		r->categorySource = (SbMetadataCategorySource) json_object_get_int (v);
	if (r->categorySource < SB_METADATA_CATEGORIES_NONE ||
			r->categorySource > SB_METADATA_CATEGORIES_TAGS) {
		r->categorySource = SB_METADATA_CATEGORIES_NONE;
		r->genres[0] = '\0';
	}
	copyText (r->artistId, sizeof (r->artistId), jsonString (o, "artist_id"));
	copyText (r->recordingId, sizeof (r->recordingId), jsonString (o, "recording_id"));
	copyText (r->releaseId, sizeof (r->releaseId), jsonString (o, "release_id"));
	copyText (r->releaseGroupId, sizeof (r->releaseGroupId),
			jsonString (o, "release_group_id"));
	if (json_object_object_get_ex (o, "confidence", &v))
		r->confidence = json_object_get_double (v);
	SbMetadataValidateDates (r);
	json_object_put (o); return SbCacheStatePersistent (r->status);
}
static char *lyricsSerialize(const SbLyricsResult*r){json_object*o=json_object_new_object();json_object_object_add(o,"status",json_object_new_int(r->status));json_object_object_add(o,"artist",json_object_new_string(r->artist));json_object_object_add(o,"title",json_object_new_string(r->title));json_object_object_add(o,"album",json_object_new_string(r->album));json_object_object_add(o,"record_id",json_object_new_string(r->recordId));json_object_object_add(o,"duration",json_object_new_double(r->duration));json_object_object_add(o,"instrumental",json_object_new_boolean(r->instrumental));json_object_object_add(o,"plain",json_object_new_string(r->plainLyrics?r->plainLyrics:""));json_object_object_add(o,"synced",json_object_new_string(r->syncedLyrics?r->syncedLyrics:""));char*s=strdup(json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN));json_object_put(o);return s;}
static bool lyricsDeserialize(const char*s,SbLyricsResult*r){json_object*o=json_tokener_parse(s),*v=NULL;if(!o)return false;SbLyricsResultInit(r);copyText(r->provider,sizeof(r->provider),"LRCLIB");json_object_object_get_ex(o,"status",&v);r->status=(SbLookupStatus)json_object_get_int(v);copyText(r->artist,sizeof(r->artist),jsonString(o,"artist"));copyText(r->title,sizeof(r->title),jsonString(o,"title"));copyText(r->album,sizeof(r->album),jsonString(o,"album"));copyText(r->recordId,sizeof(r->recordId),jsonString(o,"record_id"));if(json_object_object_get_ex(o,"duration",&v))r->duration=json_object_get_double(v);if(json_object_object_get_ex(o,"instrumental",&v))r->instrumental=json_object_get_boolean(v);const char*p=jsonString(o,"plain"),*y=jsonString(o,"synced");if(*p)r->plainLyrics=strdup(p);if(*y)r->syncedLyrics=strdup(y);json_object_put(o);return SbCacheStatePersistent(r->status);}
static char *artSerialize(const SbAlbumArtResult*r){json_object*o=json_object_new_object();json_object_object_add(o,"release_id",json_object_new_string(r->releaseId));json_object_object_add(o,"url",json_object_new_string(r->sourceUrl));json_object_object_add(o,"mime",json_object_new_string(r->mimeType));json_object_object_add(o,"path",json_object_new_string(r->cachedPath));json_object_object_add(o,"width",json_object_new_int(r->width));json_object_object_add(o,"height",json_object_new_int(r->height));char*s=strdup(json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN));json_object_put(o);return s;}
static bool artDeserialize(const SbCacheEntry*e,const char*release,SbAlbumArtResult*r){json_object*o=json_tokener_parse(e->payload),*v=NULL;if(!o)return false;SbAlbumArtResultInit(r);r->status=e->state;const char*stored=jsonString(o,"release_id");copyText(r->releaseId,sizeof(r->releaseId),*stored?stored:release);copyText(r->sourceUrl,sizeof(r->sourceUrl),jsonString(o,"url"));copyText(r->mimeType,sizeof(r->mimeType),jsonString(o,"mime"));copyText(r->cachedPath,sizeof(r->cachedPath),jsonString(o,"path"));if(json_object_object_get_ex(o,"width",&v))r->width=json_object_get_int(v);if(json_object_object_get_ex(o,"height",&v))r->height=json_object_get_int(v);json_object_put(o);if(r->status==SB_LOOKUP_AVAILABLE){FILE*f=fopen(r->cachedPath,"rb");if(!f)return false;fclose(f);}return true;}

bool SbLrclibParse (const char *json, SbLyricsResult *result) {
	SbLyricsResultInit (result); copyText (result->provider,
			sizeof (result->provider), "LRCLIB");
	json_object *root = json_tokener_parse (json);
	if (root == NULL || !json_object_is_type (root, json_type_object)) goto invalid;
	json_object *id = NULL, *instrumental = NULL;
	if (!json_object_object_get_ex (root, "id", &id) ||
			(!json_object_is_type (id, json_type_int) &&
			!json_object_is_type (id, json_type_string))) goto invalid;
	snprintf (result->recordId, sizeof (result->recordId), "%s",
			json_object_get_string (id));
	copyText (result->artist, sizeof (result->artist), jsonString (root, "artistName"));
	copyText (result->title, sizeof (result->title), jsonString (root, "trackName"));
	copyText (result->album, sizeof (result->album), jsonString (root, "albumName"));
	json_object *duration = NULL;
	if (json_object_object_get_ex (root, "duration", &duration))
		result->duration = json_object_get_double (duration);
	if (json_object_object_get_ex (root, "instrumental", &instrumental))
		result->instrumental = json_object_get_boolean (instrumental);
	const char *plain = jsonString (root, "plainLyrics");
	const char *synced = jsonString (root, "syncedLyrics");
	if (*plain != '\0') result->plainLyrics = strdup (plain);
	if (*synced != '\0') result->syncedLyrics = strdup (synced);
	if ((*plain && result->plainLyrics == NULL) || (*synced && result->syncedLyrics == NULL)) {
		json_object_put (root); SbLyricsResultDestroy (result);
		result->status = SB_LOOKUP_ERROR; copyText (result->provider,
				sizeof (result->provider), "LRCLIB"); return false;
	}
	result->status = result->instrumental ? SB_LOOKUP_INSTRUMENTAL :
			(result->plainLyrics != NULL || result->syncedLyrics != NULL ?
			SB_LOOKUP_AVAILABLE : SB_LOOKUP_NO_MATCH);
	json_object_put (root); return result->status == SB_LOOKUP_AVAILABLE ||
			result->status == SB_LOOKUP_INSTRUMENTAL;
invalid:
	if (root != NULL) json_object_put (root);
	result->status = SB_LOOKUP_ERROR;
	copyText (result->error, sizeof (result->error), "Invalid provider response");
	return false;
}

bool SbLyricsFallbackTitle (const char *title, char *out, size_t size) {
	copyText (out, size, title); size_t n = strlen (out);
	const char *markers[] = {" (remaster", " (remastered", " (deluxe edition",
			" (radio edit", " (live", " (mono", " (stereo", " (anniversary edition",
			" (explicit version", " (explicit)", " - remaster", " - remastered",
			" - deluxe edition", " - radio edit", " - live", " - mono", " - stereo",
			" - anniversary edition", " - explicit version"};
	for (size_t i = 0; i < sizeof (markers) / sizeof (*markers); i++) {
		for (size_t p = 0; p < n; p++) {
			if (tolower ((unsigned char) out[p]) != markers[i][0]) continue;
			size_t j = 0; while (markers[i][j] && p + j < n &&
					tolower ((unsigned char) out[p+j]) == markers[i][j]) j++;
			if (markers[i][j] == '\0' && (p + j == n || out[p+j] == ')' ||
					isspace ((unsigned char) out[p+j]) ||
					isdigit ((unsigned char) out[p+j]))) { out[p] = '\0';
				while (p > 0 && isspace ((unsigned char) out[p-1])) out[--p] = '\0';
				return out[0] != '\0' && strcmp (out, title) != 0; }
		}
	}
	return false;
}

bool SbLyricsFallbackArtist (const char *artist, char *out, size_t size) {
	copyText (out, size, artist); size_t n = strlen (out);
	const char *markers[] = {" feat. ", " feat ", " ft. ", " ft ", " featuring "};
	for (size_t i = 0; i < sizeof (markers) / sizeof (*markers); i++) {
		for (size_t p = 1; p < n; p++) {
			size_t j = 0;
			while (markers[i][j] && p + j < n &&
					tolower ((unsigned char) out[p+j]) == markers[i][j]) j++;
			if (markers[i][j] == '\0') {
				out[p] = '\0';
				while (p > 0 && isspace ((unsigned char) out[p-1])) out[--p] = '\0';
				return out[0] != '\0';
			}
		}
	}
	return false;
}

static double lyricsTextSimilarity (const char *wanted, const char *candidate,
		const bool title) {
	char a[SB_ENRICH_TEXT_MAX], b[SB_ENRICH_TEXT_MAX];
	char cleanA[SB_ENRICH_TEXT_MAX], cleanB[SB_ENRICH_TEXT_MAX];
	const char *left = wanted, *right = candidate;
	if (title) {
		if (SbLyricsFallbackTitle (wanted, cleanA, sizeof (cleanA))) left = cleanA;
		if (SbLyricsFallbackTitle (candidate, cleanB, sizeof (cleanB))) right = cleanB;
	}
	SbTrackNormalize (left, a, sizeof (a));
	SbTrackNormalize (right, b, sizeof (b));
	if (a[0] == '\0' || b[0] == '\0') return 0.0;
	if (strcmp (a, b) == 0) return 1.0;
	return strstr (a, b) != NULL || strstr (b, a) != NULL ? 0.70 : 0.0;
}

static double lyricsArtistSimilarity (const char *wanted, const char *candidate) {
	double score = lyricsTextSimilarity (wanted, candidate, false);
	if (score == 1.0) return score;
	char wantedBase[SB_ENRICH_TEXT_MAX], candidateBase[SB_ENRICH_TEXT_MAX];
	const bool wantedFeatured = SbLyricsFallbackArtist (wanted, wantedBase,
			sizeof (wantedBase));
	const bool candidateFeatured = SbLyricsFallbackArtist (candidate,
			candidateBase, sizeof (candidateBase));
	if ((wantedFeatured || candidateFeatured) && lyricsTextSimilarity (
			wantedFeatured ? wantedBase : wanted,
			candidateFeatured ? candidateBase : candidate, false) == 1.0)
		return 0.82;
	return score;
}

static double lrclibCandidateScore (const SbTrackIdentity *identity,
		json_object *candidate) {
	const double title = lyricsTextSimilarity (identity->title,
			jsonString (candidate, "trackName"), true);
	const double artist = lyricsArtistSimilarity (identity->artist,
			jsonString (candidate, "artistName"));
	/* Lyrics are unsafe to guess: after punctuation and approved edition
	 * cleanup, the title itself must agree exactly. */
	if (title < 1.0 || artist < 0.70) return 0.0;
	double score = title * 0.55 + artist * 0.35;
	const char *album = jsonString (candidate, "albumName");
	if (identity->album[0] != '\0' && album[0] != '\0') {
		if (lyricsTextSimilarity (identity->album, album, false) == 1.0)
			score += 0.10;
		else score -= 0.12;
	}
	json_object *durationObject = NULL;
	if (identity->duration > 0 && json_object_object_get_ex (candidate,
			"duration", &durationObject)) {
		const double candidateDuration = json_object_get_double (durationObject);
		const double difference = candidateDuration > identity->duration ?
				candidateDuration - identity->duration :
				identity->duration - candidateDuration;
		if (difference <= 2.0) score += 0.10;
		else if (difference <= 5.0) score += 0.08;
		else if (difference <= 10.0) score += 0.04;
		else score -= 0.15;
	}
	return score;
}

static double lrclibCandidateDuration (json_object *candidate) {
	json_object *duration = NULL;
	return candidate != NULL && json_object_object_get_ex (candidate,
			"duration", &duration) ? json_object_get_double (duration) : 0.0;
}

static const char *lrclibCandidateId (json_object *candidate) {
	json_object *id = NULL;
	return candidate != NULL && json_object_object_get_ex (candidate, "id", &id) &&
			(json_object_is_type (id, json_type_int) ||
			json_object_is_type (id, json_type_string)) ?
			json_object_get_string (id) : "";
}

static const char *lrclibCandidateRejection (const SbTrackIdentity *identity,
		json_object *candidate, const double score) {
	if (candidate == NULL || !json_object_is_type (candidate, json_type_object) ||
			jsonString (candidate, "trackName")[0] == '\0' ||
			jsonString (candidate, "artistName")[0] == '\0')
		return "malformed response";
	if (lyricsTextSimilarity (identity->title,
			jsonString (candidate, "trackName"), true) < 1.0)
		return "title mismatch";
	if (lyricsArtistSimilarity (identity->artist,
			jsonString (candidate, "artistName")) < 0.70)
		return "artist mismatch";
	if (identity->duration > 0) {
		const double duration = lrclibCandidateDuration (candidate);
		const double difference = duration > identity->duration ?
				duration - identity->duration : identity->duration - duration;
		if (duration > 0.0 && difference > 10.0 && score < 0.84)
			return "duration mismatch";
	}
	return "below threshold";
}

static void lrclibLogCandidate (const char *endpoint, const char *step,
		const char *decision, const char *reason, json_object *candidate,
		const double score) {
	enrichmentDebugPrint ("lyrics provider=lrclib endpoint=%s step=%s decision=%s reason=\"%s\" id=%s returned_artist=\"%s\" returned_title=\"%s\" returned_album=\"%s\" duration=%.1f score=%.3f\n",
			endpoint, step, decision, reason,
			lrclibCandidateId (candidate), jsonString (candidate, "artistName"),
			jsonString (candidate, "trackName"), jsonString (candidate, "albumName"),
			lrclibCandidateDuration (candidate), score);
}

static bool lrclibSearchParse (const char *json, const SbTrackIdentity *identity,
		SbLyricsResult *result, double *acceptedScore, const char *step) {
	SbLyricsResultInit (result);
	if (acceptedScore != NULL) *acceptedScore = 0.0;
	copyText (result->provider, sizeof (result->provider), "LRCLIB");
	json_object *root = json_tokener_parse (json);
	if (root == NULL || !json_object_is_type (root, json_type_array)) {
		if (root != NULL) json_object_put (root);
		result->status = SB_LOOKUP_ERROR;
		copyText (result->error, sizeof (result->error), "Invalid provider response");
		enrichmentDebugPrint ("lyrics provider=lrclib endpoint=search step=%s decision=reject reason=\"malformed response\"\n", step);
		return false;
	}
	json_object *best = NULL; double bestScore = 0.0, secondScore = 0.0;
	for (size_t i = 0; i < json_object_array_length (root); i++) {
		json_object *candidate = json_object_array_get_idx (root, i);
		const double score = lrclibCandidateScore (identity, candidate);
		if (score > bestScore) {
			secondScore = bestScore; bestScore = score; best = candidate;
		} else if (score > secondScore) secondScore = score;
	}
	const bool strong = best != NULL && bestScore >= 0.84;
	const bool unambiguous = strong && (secondScore < 0.84 ||
			bestScore - secondScore >= 0.06);
	if (!unambiguous) {
		result->status = SB_LOOKUP_NO_MATCH;
		for (size_t i = 0; i < json_object_array_length (root); i++) {
			json_object *candidate = json_object_array_get_idx (root, i);
			const double score = lrclibCandidateScore (identity, candidate);
			lrclibLogCandidate ("search", step, "reject",
					strong && score >= 0.84 ? "ambiguous result" :
					lrclibCandidateRejection (identity, candidate, score),
					candidate, score);
		}
		json_object_put (root); return false;
	}
	for (size_t i = 0; i < json_object_array_length (root); i++) {
		json_object *candidate = json_object_array_get_idx (root, i);
		const double score = lrclibCandidateScore (identity, candidate);
		lrclibLogCandidate ("search", step, candidate == best ? "accept" : "reject",
				candidate == best ? "highest unambiguous score" :
				(score >= 0.84 ? "lower-ranked result" :
				lrclibCandidateRejection (identity, candidate, score)), candidate, score);
	}
	const char *serialized = json_object_to_json_string_ext (best,
			JSON_C_TO_STRING_PLAIN);
	const bool ok = SbLrclibParse (serialized, result);
	if (acceptedScore != NULL) *acceptedScore = bestScore;
	if (!ok) enrichmentDebugPrint ("lyrics provider=lrclib endpoint=search step=%s decision=reject reason=\"malformed response\" score=%.3f\n",
			step, bestScore);
	json_object_put (root); return ok;
}

bool SbLrclibSearchParse (const char *json, const SbTrackIdentity *identity,
		SbLyricsResult *result, double *acceptedScore) {
	return lrclibSearchParse (json, identity, result, acceptedScore, "search");
}

bool SbLyricsDisplayText (const SbLyricsResult *result, char **out) {
	*out = NULL;
	if (result->plainLyrics != NULL) return (*out = strdup (result->plainLyrics)) != NULL;
	if (result->syncedLyrics == NULL) return false;
	size_t length = strlen (result->syncedLyrics);
	char *display = malloc (length + 1); if (display == NULL) return false;
	size_t used = 0; const char *p = result->syncedLyrics;
	while (*p) {
		const char *end = strchr (p, '\n'); if (end == NULL) end = p + strlen (p);
		const char *line = p;
		while (line < end && *line == '[') {
			const char *close = memchr (line, ']', (size_t) (end - line));
			if (close == NULL) break; line = close + 1;
		}
		while (line < end && isspace ((unsigned char) *line)) line++;
		if (line < end) { size_t add = (size_t) (end - line);
			memcpy (display + used, line, add); used += add; display[used++] = '\n'; }
		p = *end == '\n' ? end + 1 : end;
	}
	if (used > 0) used--; display[used] = '\0'; *out = display; return used > 0;
}

static double similarity (const char *a, const char *b) {
	char na[SB_ENRICH_TEXT_MAX], nb[SB_ENRICH_TEXT_MAX];
	SbTrackNormalize (a, na, sizeof (na)); SbTrackNormalize (b, nb, sizeof (nb));
	if (na[0] == '\0' || nb[0] == '\0') return 0.0;
	if (strcmp (na, nb) == 0) return 1.0;
	return strstr (na, nb) != NULL || strstr (nb, na) != NULL ? 0.72 : 0.0;
}

static bool albumEditionDescriptor (const char *text) {
	char value[SB_ENRICH_TEXT_MAX]; SbTrackNormalize (text, value, sizeof (value));
	const char *fixed[] = {"deluxe", "deluxe edition", "remaster", "remastered",
			"expanded", "expanded edition", "anniversary", "anniversary edition",
			"special edition", "bonus edition", "explicit", "explicit version"};
	for (size_t i = 0; i < sizeof (fixed) / sizeof (*fixed); i++)
		if (strcmp (value, fixed[i]) == 0) return true;
	const char *p = value;
	while (isdigit ((unsigned char) *p)) p++;
	if (p == value || (strncmp (p, "st ", 3) != 0 &&
			strncmp (p, "nd ", 3) != 0 && strncmp (p, "rd ", 3) != 0 &&
			strncmp (p, "th ", 3) != 0)) return false;
	p += 3;
	return strcmp (p, "anniversary") == 0 ||
			strcmp (p, "anniversary edition") == 0 ||
			strcmp (p, "anniversary deluxe") == 0 ||
			strcmp (p, "anniversary deluxe edition") == 0;
}

static void trimAlbumTitle (char *value) {
	size_t length = strlen (value);
	while (length > 0 && isspace ((unsigned char) value[length - 1]))
		value[--length] = '\0';
}

bool SbMusicBrainzAlbumFamilyTitle (const char *input, char *out,
		const size_t size) {
	if (out == NULL || size == 0) return false;
	copyText (out, size, input != NULL ? input : ""); trimAlbumTitle (out);
	const size_t originalLength = strlen (out);
	if (originalLength == 0) return false;
	/* Parenthesized/bracketed edition qualifiers are the common case and are
	 * safe to remove without disturbing punctuation or genuine subtitles. */
	const char close = out[originalLength - 1];
	if (close == ')' || close == ']') {
		const char open = close == ')' ? '(' : '[';
		char *start = strrchr (out, open);
		if (start != NULL && albumEditionDescriptor (start + 1)) {
			char descriptor[SB_ENRICH_TEXT_MAX];
			copyText (descriptor, sizeof (descriptor), start + 1);
			descriptor[strlen (descriptor) - 1] = '\0';
			if (albumEditionDescriptor (descriptor)) {
				*start = '\0'; trimAlbumTitle (out); return out[0] != '\0';
			}
		}
	}
	/* Also accept an explicitly separated or bare trailing edition phrase. */
	const char *separators[] = {" - ", " – ", " — ", ", "};
	for (size_t i = 0; i < sizeof (separators) / sizeof (*separators); i++) {
		char *at = strstr (out, separators[i]); char *last = NULL;
		while (at != NULL) { last = at; at = strstr (at + 1, separators[i]); }
		if (last != NULL && albumEditionDescriptor (last + strlen (separators[i]))) {
			*last = '\0'; trimAlbumTitle (out); return out[0] != '\0';
		}
	}
	for (char *at = strchr (out, ' '); at != NULL; at = strchr (at + 1, ' '))
		if (albumEditionDescriptor (at + 1)) {
			*at = '\0'; trimAlbumTitle (out); return out[0] != '\0';
		}
	return false;
}

static void albumFamilyNormalize (const char *input, char *out, size_t size) {
	char family[SB_ENRICH_TEXT_MAX];
	if (!SbMusicBrainzAlbumFamilyTitle (input, family, sizeof (family)))
		copyText (family, sizeof (family), input);
	SbTrackNormalize (family, out, size);
}

static const char *releaseArtist (json_object *release) {
	json_object *credits = NULL, *credit = NULL;
	if (json_object_object_get_ex (release, "artist-credit", &credits) &&
			json_object_is_type (credits, json_type_array) &&
			json_object_array_length (credits) > 0)
		credit = json_object_array_get_idx (credits, 0);
	const char *name = jsonString (credit, "name");
	if (*name == '\0') { json_object *artist = NULL;
		json_object_object_get_ex (credit, "artist", &artist); name = jsonString (artist, "name"); }
	return name;
}

static bool releaseCompilation (json_object *release, const char *title,
		const char *artist, const char **typeOut) {
	json_object *group = NULL, *secondary = NULL;
	json_object_object_get_ex (release, "release-group", &group);
	const char *primary = jsonString (group, "primary-type");
	if (typeOut != NULL) *typeOut = primary;
	bool compilation = strcasecmp (artist, "Various Artists") == 0;
	if (json_object_object_get_ex (group, "secondary-types", &secondary) &&
			json_object_is_type (secondary, json_type_array))
		for (size_t i = 0; i < json_object_array_length (secondary); i++) {
			const char *value = json_object_get_string (json_object_array_get_idx (secondary, i));
			if (value != NULL && (strcasecmp (value, "Compilation") == 0 ||
					strcasecmp (value, "Soundtrack") == 0)) compilation = true;
		}
	char normalized[SB_ENRICH_TEXT_MAX]; SbTrackNormalize (title, normalized, sizeof (normalized));
	if ((strncmp (normalized, "100 ", 4) == 0 || strstr (normalized, " greatest hits") != NULL ||
			strstr (normalized, " anthology") != NULL) && *normalized) compilation = true;
	return compilation;
}

static double artReleaseScore (json_object *release, const SbTrackIdentity *identity,
		double *albumAgreement, double *artistAgreement, bool *compilation,
		const char **type) {
	char wanted[SB_ENRICH_TEXT_MAX], candidate[SB_ENRICH_TEXT_MAX];
	albumFamilyNormalize (identity->album, wanted, sizeof (wanted));
	albumFamilyNormalize (jsonString (release, "title"), candidate, sizeof (candidate));
	*albumAgreement = *wanted && *candidate ? (strcmp (wanted, candidate) == 0 ? 1.0 :
			(strstr (wanted, candidate) || strstr (candidate, wanted) ? .68 : 0.0)) : 0.0;
	const char *artist = releaseArtist (release);
	*artistAgreement = *artist ? similarity (identity->artist, artist) : .5;
	*compilation = releaseCompilation (release, jsonString (release, "title"), artist, type);
	const char *status = jsonString (release, "status");
	double score = *albumAgreement * 70.0 + *artistAgreement * 18.0;
	if (strcasecmp (status, "Official") == 0) score += 7.0;
	if (*type && (strcasecmp (*type, "Album") == 0 || strcasecmp (*type, "EP") == 0 ||
			strcasecmp (*type, "Single") == 0)) score += 5.0;
	if (*compilation) score -= *albumAgreement >= .99 ? 5.0 : 35.0;
	return score;
}

typedef struct { char name[SB_ENRICH_TEXT_MAX]; int count; } SbGenreCandidate;

static int genreCandidateCompare (const void *left, const void *right) {
	const SbGenreCandidate *a = left, *b = right;
	if (a->count != b->count) return b->count - a->count;
	return strcasecmp (a->name, b->name);
}

static bool usefulValue (const char *value) {
	if (value == NULL) return false;
	while (isspace ((unsigned char) *value)) value++;
	return *value != '\0' && strcasecmp (value, "none") != 0 &&
			strcasecmp (value, "n/a") != 0 && strcasecmp (value, "unknown") != 0 &&
			strcasecmp (value, "[none]") != 0;
}

static bool appendGenre (char *out, const size_t size, const char *name) {
	char normalized[SB_ENRICH_TEXT_MAX]; SbTrackNormalize (name, normalized,
			sizeof (normalized));
	if (normalized[0] == '\0') return false;
	char existing[SB_ENRICH_GENRES_MAX]; copyText (existing, sizeof (existing), out);
	for (char *item = strtok (existing, ","); item != NULL; item = strtok (NULL, ",")) {
		while (isspace ((unsigned char) *item)) item++;
		char known[SB_ENRICH_TEXT_MAX]; SbTrackNormalize (item, known, sizeof (known));
		if (strcmp (known, normalized) == 0) return false;
	}
	size_t used = strlen (out), add = strlen (name);
	if (used + (used ? 2 : 0) + add + 1 > size) return false;
	if (used) strcat (out, ", "); strcat (out, name); return true;
}

static bool collectGenres (json_object *owner, const char *key,
		const int minimumCount, char *out, const size_t size) {
	json_object *items = NULL;
	if (!json_object_object_get_ex (owner, key, &items) ||
			!json_object_is_type (items, json_type_array)) return false;
	SbGenreCandidate candidates[32]; size_t count = 0;
	for (size_t i = 0; i < json_object_array_length (items) && count < 32; i++) {
		json_object *item = json_object_array_get_idx (items, i), *countObject = NULL;
		const char *name = jsonString (item, "name"); int votes = 0;
		if (json_object_object_get_ex (item, "count", &countObject))
			votes = json_object_get_int (countObject);
		while (isspace ((unsigned char) *name)) name++;
		size_t length = strlen (name);
		while (length > 0 && isspace ((unsigned char) name[length - 1])) length--;
		if (length > 0 && length < SB_ENRICH_TEXT_MAX && votes >= minimumCount) {
			memcpy (candidates[count].name, name, length);
			candidates[count].name[length] = '\0';
			if (!usefulValue (candidates[count].name)) continue;
			candidates[count].count = votes; count++;
		}
	}
	qsort (candidates, count, sizeof (*candidates), genreCandidateCompare);
	out[0] = '\0'; size_t accepted = 0;
	for (size_t i = 0; i < count && accepted < 5; i++)
		if (appendGenre (out, size, candidates[i].name)) accepted++;
	return accepted > 0;
}

static void applyGenres (json_object *owner, SbMetadataResult *result) {
	char genres[SB_ENRICH_GENRES_MAX];
	if ((result->genres[0] == '\0' ||
			result->categorySource == SB_METADATA_CATEGORIES_TAGS) &&
			collectGenres (owner, "genres", 0, genres, sizeof (genres))) {
		copyText (result->genres, sizeof (result->genres), genres);
		result->categorySource = SB_METADATA_CATEGORIES_GENRES;
	} else if (result->genres[0] == '\0' &&
			collectGenres (owner, "tags", 1, genres, sizeof (genres))) {
		copyText (result->genres, sizeof (result->genres), genres);
		result->categorySource = SB_METADATA_CATEGORIES_TAGS;
	}
}

static void applyReleaseGroup (json_object *group, SbMetadataResult *result) {
	if (group == NULL || !json_object_is_type (group, json_type_object)) return;
	const char *value = jsonString (group, "id");
	if (*value) copyText (result->releaseGroupId, sizeof (result->releaseGroupId), value);
	value = jsonString (group, "first-release-date");
	if (*value) copyText (result->firstReleaseDate,
			sizeof (result->firstReleaseDate), value);
	value = jsonString (group, "primary-type");
	if (*value) copyText (result->releaseType, sizeof (result->releaseType), value);
	applyGenres (group, result); SbMetadataValidateDates (result);
}

static void applyLabelInfo (json_object *release, SbMetadataResult *result) {
	json_object *entries = NULL;
	if (!json_object_object_get_ex (release, "label-info", &entries) ||
			!json_object_is_type (entries, json_type_array)) return;
	for (size_t i = 0; i < json_object_array_length (entries); i++) {
		json_object *entry = json_object_array_get_idx (entries, i), *label = NULL;
		json_object_object_get_ex (entry, "label", &label);
		const char *name = jsonString (label, "name");
		if (!usefulValue (name)) continue;
		copyText (result->label, sizeof (result->label), name);
		const char *catalog = jsonString (entry, "catalog-number");
		if (usefulValue (catalog)) copyText (result->catalogNumber,
				sizeof (result->catalogNumber), catalog);
		else result->catalogNumber[0] = '\0';
		return;
	}
}

static void applyReleaseCountry (json_object *release, SbMetadataResult *result) {
	const char *country = jsonString (release, "country");
	if (*country) { copyText (result->releaseCountry,
			sizeof (result->releaseCountry), country); return; }
	json_object *events = NULL;
	if (!json_object_object_get_ex (release, "release-events", &events) ||
			!json_object_is_type (events, json_type_array)) return;
	for (size_t i = 0; i < json_object_array_length (events); i++) {
		json_object *event = json_object_array_get_idx (events, i), *area = NULL, *codes = NULL;
		json_object_object_get_ex (event, "area", &area);
		if (json_object_object_get_ex (area, "iso-3166-1-codes", &codes) &&
				json_object_is_type (codes, json_type_array) &&
				json_object_array_length (codes) > 0) {
			country = json_object_get_string (json_object_array_get_idx (codes, 0));
			if (country != NULL && *country) { copyText (result->releaseCountry,
					sizeof (result->releaseCountry), country); return; }
		}
	}
}

static void applyRelease (json_object *release, SbMetadataResult *result) {
	const char *value = jsonString (release, "title");
	if (*value) copyText (result->release, sizeof (result->release), value);
	value = jsonString (release, "date");
	if (*value) copyText (result->releaseDate, sizeof (result->releaseDate), value);
	value = jsonString (release, "id");
	if (*value) copyText (result->releaseId, sizeof (result->releaseId), value);
	json_object *group = NULL;
	if (json_object_object_get_ex (release, "release-group", &group))
		applyReleaseGroup (group, result);
	applyReleaseCountry (release, result); applyLabelInfo (release, result);
	SbMetadataValidateDates (result);
}

static void applyIsrcs (json_object *recording, SbMetadataResult *result) {
	json_object *isrcs = NULL;
	if (!json_object_object_get_ex (recording, "isrcs", &isrcs) ||
			!json_object_is_type (isrcs, json_type_array)) return;
	char best[16] = "";
	for (size_t i = 0; i < json_object_array_length (isrcs); i++) {
		const char *raw = json_object_get_string (json_object_array_get_idx (isrcs, i));
		char candidate[16]; size_t used = 0;
		for (const unsigned char *p = (const unsigned char *) (raw ? raw : "");
				*p && used + 1 < sizeof (candidate); p++)
			if (isalnum (*p)) candidate[used++] = (char) toupper (*p);
		candidate[used] = '\0';
		bool canonical = used == 12;
		for (size_t n = 0; canonical && n < 2; n++)
			canonical = isalpha ((unsigned char) candidate[n]);
		for (size_t n = 2; canonical && n < 5; n++)
			canonical = isalnum ((unsigned char) candidate[n]);
		for (size_t n = 5; canonical && n < 12; n++)
			canonical = isdigit ((unsigned char) candidate[n]);
		if (canonical && (best[0] == '\0' || strcmp (candidate, best) < 0))
			copyText (best, sizeof (best), candidate);
	}
	if (best[0]) copyText (result->isrc, sizeof (result->isrc), best);
}

static bool selectReleaseArray (json_object *releases, const SbTrackIdentity *identity,
		SbMetadataResult *result, const char *source) {
	json_object *best = NULL; double bestScore = -1000.0, bestAlbum = 0.0;
	if (releases == NULL || !json_object_is_type (releases, json_type_array)) return false;
	for (size_t i = 0; i < json_object_array_length (releases); i++) {
		json_object *release = json_object_array_get_idx (releases, i);
		double album = 0.0, artist = 0.0; bool compilation = false; const char *type = "";
		double score = artReleaseScore (release, identity, &album, &artist, &compilation, &type);
		const bool accepted = identity->album[0] == '\0' ? artist >= .5 : album >= .68 && artist >= .5;
		json_object *group = NULL; json_object_object_get_ex (release, "release-group", &group);
		enrichmentDebugPrint ("art candidate source=%s title=\"%s\" release_mbid=%s release_group_mbid=%s artist_agreement=%.2f album_agreement=%.2f type=%s status=%s compilation=%s score=%.1f decision=%s\n",
				source, jsonString (release, "title"), jsonString (release, "id"),
				jsonString (group, "id"), artist, album, type, jsonString (release, "status"),
				compilation ? "yes" : "no", score, accepted ? "accepted" : "rejected_identity");
		if (accepted && score > bestScore) { best = release; bestScore = score; bestAlbum = album; }
	}
	if (best == NULL) return false;
	const char *nextId = jsonString (best, "id");
	if (result->releaseId[0] && strcmp (result->releaseId, nextId) != 0) {
		result->release[0] = result->releaseDate[0] = result->releaseCountry[0] = '\0';
		result->label[0] = result->catalogNumber[0] = '\0';
		json_object *nextGroup = NULL;
		json_object_object_get_ex (best, "release-group", &nextGroup);
		if (strcmp (result->releaseGroupId, jsonString (nextGroup, "id")) != 0) {
			result->firstReleaseDate[0] = result->releaseType[0] = '\0';
			if (result->categorySource == SB_METADATA_CATEGORIES_TAGS) {
				result->genres[0] = '\0';
				result->categorySource = SB_METADATA_CATEGORIES_NONE;
			}
		}
	}
	applyRelease (best, result);
	enrichmentDebugPrint ("art selection source=%s release_mbid=%s release_group_mbid=%s album_agreement=%.2f score=%.1f\n",
			source, result->releaseId, result->releaseGroupId, bestAlbum, bestScore);
	return true;
}

bool SbMusicBrainzSelectRelease (const char *json, const SbTrackIdentity *identity,
		SbMetadataResult *result) {
	json_object *root = json_tokener_parse (json), *releases = NULL;
	if (root == NULL) return false;
	if (json_object_is_type (root, json_type_array)) releases = root;
	else json_object_object_get_ex (root, "releases", &releases);
	bool selected = selectReleaseArray (releases, identity, result, "album_lookup");
	json_object_put (root); return selected;
}

bool SbMusicBrainzSelectArtRelease (const char *json, const SbTrackIdentity *identity,
		SbMetadataResult *result) {
	return SbMusicBrainzSelectRelease (json, identity, result);
}

bool SbMusicBrainzApplyReleaseDetail (const char *json, SbMetadataResult *result) {
	json_object *root = json_tokener_parse (json);
	if (root == NULL || !json_object_is_type (root, json_type_object)) {
		if (root != NULL) json_object_put (root); return false;
	}
	const char *id = jsonString (root, "id");
	if (result->releaseId[0] && (!*id || strcmp (result->releaseId, id) != 0)) {
		json_object_put (root); return false;
	}
	/* Detail is authoritative only for the already selected edition. */
	applyRelease (root, result); applyGenres (root, result);
	json_object_put (root); return true;
}

bool SbMusicBrainzApplyReleaseGroupDetail (const char *json,
		SbMetadataResult *result) {
	json_object *root = json_tokener_parse (json);
	if (root == NULL || !json_object_is_type (root, json_type_object)) {
		if (root != NULL) json_object_put (root); return false;
	}
	const char *id = jsonString (root, "id");
	if (result->releaseGroupId[0] && (!*id ||
			strcmp (result->releaseGroupId, id) != 0)) {
		json_object_put (root); return false;
	}
	applyReleaseGroup (root, result); json_object_put (root); return true;
}

static bool dateDigits (const char *text, const size_t count) {
	for (size_t i = 0; i < count; i++)
		if (!isdigit ((unsigned char) text[i])) return false;
	return true;
}

bool SbMusicBrainzFormatDate (const char *date, char *out, size_t size) {
	if (out == NULL || size == 0) return false;
	out[0] = '\0';
	if (date == NULL) return false;

	const size_t length = strlen (date);
	if ((length != 4 && length != 7 && length != 10) ||
			!dateDigits (date, 4) || (length >= 7 &&
			(date[4] != '-' || !dateDigits (date + 5, 2))) || (length == 10 &&
			(date[7] != '-' || !dateDigits (date + 8, 2)))) return false;
	const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
			(date[2] - '0') * 10 + date[3] - '0';
	if (year == 0) return false;
	if (length == 4) {
		return snprintf (out, size, "%s", date) < (int) size;
	}
	const int month = (date[5] - '0') * 10 + date[6] - '0';
	if (month < 1 || month > 12) return false;
	if (length == 7) {
		return snprintf (out, size, "%c%c/%.*s", date[5], date[6], 4, date) <
				(int) size;
	}
	const int day = (date[8] - '0') * 10 + date[9] - '0';
	static const unsigned char daysPerMonth[] =
		{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int maximum = daysPerMonth[month - 1];
	if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)))
		maximum = 29;
	if (day < 1 || day > maximum) return false;
	return snprintf (out, size, "%c%c/%c%c/%.*s", date[5], date[6], date[8],
			date[9], 4, date) < (int) size;
}

static int musicBrainzDateCompare (const char *left, const char *right) {
	char display[16];
	if (!SbMusicBrainzFormatDate (left, display, sizeof (display)) ||
			!SbMusicBrainzFormatDate (right, display, sizeof (display))) return 0;
	const size_t shared = strlen (left) < strlen (right) ? strlen (left) :
			strlen (right);
	const int compared = strncmp (left, right, shared);
	return compared < 0 ? -1 : compared > 0 ? 1 : 0;
}

bool SbMetadataValidateDates (SbMetadataResult *result) {
	if (result == NULL || result->firstReleaseDate[0] == '\0') return true;
	if (result->releaseGroupId[0] == '\0') {
		enrichmentDebugPrint ("metadata date_reject reason=missing_release_group original=%s\n",
				result->firstReleaseDate);
		result->firstReleaseDate[0] = '\0'; return false;
	}
	if (result->releaseDate[0] == '\0') return true;
	if (musicBrainzDateCompare (result->firstReleaseDate,
			result->releaseDate) <= 0) return true;
	enrichmentDebugPrint ("metadata date_reject reason=original_after_edition original=%s edition=%s\n",
			result->firstReleaseDate, result->releaseDate);
	result->firstReleaseDate[0] = '\0'; return false;
}

static bool appendMetadataField (char *out, const size_t size,
		const char *label, const char *value) {
	if (value == NULL || value[0] == '\0') return true;
	const size_t used = strlen (out);
	if (used >= size) return false;
	const int written = snprintf (out + used, size - used, "%s%s: %s",
			used ? "\n" : "", label, value);
	return written >= 0 && (size_t) written < size - used;
}

bool SbMetadataFormatAvailableFields (const SbMetadataResult *result,
		char *out, const size_t size) {
	if (result == NULL || out == NULL || size == 0) return false;
	SbMetadataResult validated = *result; SbMetadataValidateDates (&validated);
	result = &validated;
	out[0] = '\0';
	if (!appendMetadataField (out, size, "Canonical Artist", result->artist) ||
			!appendMetadataField (out, size, "Canonical Track", result->title) ||
			!appendMetadataField (out, size, "Release", result->release)) return false;
	char first[16], edition[16];
	const bool hasFirst = SbMusicBrainzFormatDate (result->firstReleaseDate,
			first, sizeof (first));
	const bool hasEdition = SbMusicBrainzFormatDate (result->releaseDate,
			edition, sizeof (edition));
	if (hasFirst && hasEdition && strcmp (result->firstReleaseDate,
			result->releaseDate) == 0) {
		if (!appendMetadataField (out, size, "Release Date", edition)) return false;
	} else {
		if (hasFirst && !appendMetadataField (out, size, "Original Release", first))
			return false;
		if (hasEdition && !appendMetadataField (out, size, "Edition Release", edition))
			return false;
	}
	const char *country = strcmp (result->releaseCountry, "XW") == 0 ?
			"Worldwide" : strcmp (result->releaseCountry, "XE") == 0 ?
			"Europe" : result->releaseCountry;
	if (!appendMetadataField (out, size, "Type", result->releaseType) ||
			!appendMetadataField (out, size, "Country", country) ||
			!appendMetadataField (out, size, "Label", result->label) ||
			!appendMetadataField (out, size, "Catalog", result->catalogNumber) ||
			!appendMetadataField (out, size, "ISRC", result->isrc)) return false;
	const char *categoryLabel = result->categorySource == SB_METADATA_CATEGORIES_GENRES ?
			"Genres" : result->categorySource == SB_METADATA_CATEGORIES_TAGS ?
			"Tags" : NULL;
	if (categoryLabel != NULL && !appendMetadataField (out, size,
			categoryLabel, result->genres)) return false;
	char confidence[32]; snprintf (confidence, sizeof (confidence), "%.0f%%",
			result->confidence * 100.0);
	return appendMetadataField (out, size, "Confidence", confidence);
}

bool SbMusicBrainzParse (const char *json, const SbTrackIdentity *identity,
		SbMetadataResult *result) {
	SbMetadataResultInit (result); copyText (result->provider,
			sizeof (result->provider), "MusicBrainz");
	json_object *root = json_tokener_parse (json);
	json_object *recordings = NULL;
	if (root == NULL || !json_object_object_get_ex (root, "recordings", &recordings) ||
			!json_object_is_type (recordings, json_type_array)) {
		result->status = SB_LOOKUP_ERROR;
		copyText (result->error, sizeof (result->error), "Invalid provider response");
		if (root != NULL) json_object_put (root); return false;
	}
	json_object *best = NULL, *bestArtist = NULL; double bestScore = 0.0;
	for (size_t i = 0; i < json_object_array_length (recordings); i++) {
		json_object *recording = json_object_array_get_idx (recordings, i);
		json_object *credits = NULL, *credit = NULL, *artist = NULL;
		if (json_object_object_get_ex (recording, "artist-credit", &credits) &&
				json_object_array_length (credits) > 0) {
			credit = json_object_array_get_idx (credits, 0);
			json_object_object_get_ex (credit, "artist", &artist);
		}
		const char *artistName = jsonString (credit, "name");
		if (*artistName == '\0') artistName = jsonString (artist, "name");
		double score = similarity (identity->title, jsonString (recording, "title")) * .55 +
				similarity (identity->artist, artistName) * .45;
		if (score > bestScore) { bestScore = score; best = recording; bestArtist = artist; }
	}
	if (best == NULL || bestScore < .70) result->status = SB_LOOKUP_NO_MATCH;
	else {
		result->status = SB_LOOKUP_AVAILABLE; result->confidence = bestScore;
		copyText (result->title, sizeof (result->title), jsonString (best, "title"));
		json_object *credits = NULL, *credit = NULL;
		if (json_object_object_get_ex (best, "artist-credit", &credits) &&
				json_object_array_length (credits) > 0) credit = json_object_array_get_idx (credits, 0);
		copyText (result->artist, sizeof (result->artist), jsonString (credit, "name"));
		copyText (result->artistId, sizeof (result->artistId), jsonString (bestArtist, "id"));
		copyText (result->recordingId, sizeof (result->recordingId), jsonString (best, "id"));
		applyIsrcs (best, result); applyGenres (best, result);
		json_object *releases = NULL;
		if (json_object_object_get_ex (best, "releases", &releases))
			selectReleaseArray (releases, identity, result, "matched_recording");
	}
	json_object_put (root); return result->status == SB_LOOKUP_AVAILABLE;
}

static size_t httpWrite (char *ptr, size_t size, size_t count, void *userdata) {
	SbHttpBuffer *buffer = userdata; size_t add = size * count;
	char *next = realloc (buffer->data, buffer->length + add + 1);
	if (next == NULL) return 0;
	buffer->data = next; memcpy (next + buffer->length, ptr, add);
	buffer->length += add; next[buffer->length] = '\0'; return add;
}

SbLookupStatus SbMusicBrainzHttpStatus (const long status,
		const int curlCode) {
	if (SbMusicBrainzTransientFailure (status, curlCode))
		return SB_LOOKUP_UNAVAILABLE;
	return status == 200 ? SB_LOOKUP_LOADING : SB_LOOKUP_ERROR;
}

bool SbMusicBrainzTransientFailure (const long status, const int curlCode) {
	return curlCode != CURLE_OK || status == 429 || status == 500 ||
			status == 502 || status == 503 || status == 504;
}

bool SbMusicBrainzShouldRetry (const long status, const int curlCode,
		const unsigned int attempt) {
	return attempt < 2 && SbMusicBrainzTransientFailure (status, curlCode);
}

unsigned int SbMusicBrainzRetryDelayMs (const long retryAfter) {
	return (unsigned int) (retryAfter > 0 ? (retryAfter > 5 ? 5 : retryAfter) : 1) *
			1000;
}

typedef struct { long retryAfter; } SbMusicBrainzHeaders;

static size_t musicBrainzHeader (char *ptr, size_t size, size_t count,
		void *userdata) {
	const size_t length = size * count;
	SbMusicBrainzHeaders *headers = userdata;
	if (length > 12 && strncasecmp (ptr, "Retry-After:", 12) == 0) {
		char value[32]; size_t n = length - 12;
		if (n >= sizeof (value)) n = sizeof (value) - 1;
		memcpy (value, ptr + 12, n); value[n] = '\0';
		headers->retryAfter = strtol (value, NULL, 10);
	}
	return length;
}

static bool musicBrainzFetch (const char *url, const char *step,
		SbHttpBuffer *body, SbLookupStatus *failure) {
	CURL *curl = curl_easy_init (); long status = 0;
	CURLcode code = CURLE_FAILED_INIT; SbMusicBrainzHeaders headers = {0};
	if (curl == NULL) { *failure = SB_LOOKUP_ERROR; return false; }
	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_USERAGENT,
			PROGRAM_NAME "/" VERSION " (https://github.com/signalbox-player/signalbox)");
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, httpWrite);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, body);
	curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, musicBrainzHeader);
	curl_easy_setopt (curl, CURLOPT_HEADERDATA, &headers);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 12L);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
	for (unsigned int attempt = 1; attempt <= 2; attempt++) {
		headers.retryAfter = 0; status = 0;
		musicBrainzRateWait (); code = curl_easy_perform (curl);
		curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &status);
		enrichmentDebugPrint ("enrichment provider=musicbrainz step=%s attempt=%u http=%ld network_error=\"%s\" retry_after=%ld bytes=%zu\n",
				step, attempt, status,
				code == CURLE_OK ? "none" : curl_easy_strerror (code),
				headers.retryAfter, body->length);
		if (code == CURLE_OK && status == 200 && body->data != NULL) {
			curl_easy_cleanup (curl); return true;
		}
		if (!SbMusicBrainzShouldRetry (status, code, attempt)) break;
		const unsigned int delay = SbMusicBrainzRetryDelayMs (headers.retryAfter);
		enrichmentDebugPrint ("enrichment provider=musicbrainz step=%s transient_retry next_attempt=%u delay_ms=%u\n",
				step, attempt + 1, delay);
		free (body->data); *body = (SbHttpBuffer) {NULL, 0};
		SbPlatformSleepMs (delay);
	}
	curl_easy_cleanup (curl);
	*failure = SbMusicBrainzTransientFailure (status, code) ?
			SB_LOOKUP_UNAVAILABLE : SB_LOOKUP_ERROR;
	return false;
}

static bool releaseMetadataWeak (const SbMetadataResult *result) {
	return result->release[0] == '\0' || result->releaseDate[0] == '\0' ||
			result->firstReleaseDate[0] == '\0';
}

SbMetadataCompletionStep SbMusicBrainzNextCompletion (
		const SbMetadataResult *result, const char *releaseDetailId,
		const char *releaseGroupDetailId) {
	if (result == NULL) return SB_METADATA_COMPLETION_NONE;
	if (result->releaseId[0] != '\0' &&
			(releaseDetailId == NULL ||
			strcmp (result->releaseId, releaseDetailId) != 0) &&
			(result->releaseDate[0] == '\0' || result->releaseGroupId[0] == '\0' ||
			result->releaseCountry[0] == '\0' || result->label[0] == '\0' ||
			result->catalogNumber[0] == '\0'))
		return SB_METADATA_COMPLETION_RELEASE;
	if (result->releaseGroupId[0] != '\0' &&
			(releaseGroupDetailId == NULL ||
			strcmp (result->releaseGroupId, releaseGroupDetailId) != 0) &&
			(result->firstReleaseDate[0] == '\0' || result->releaseType[0] == '\0' ||
			result->genres[0] == '\0' ||
			result->categorySource == SB_METADATA_CATEGORIES_TAGS))
		return SB_METADATA_COMPLETION_RELEASE_GROUP;
	return SB_METADATA_COMPLETION_NONE;
}

static size_t metadataGenreCount (const char *genres) {
	if (genres == NULL || genres[0] == '\0') return 0;
	size_t count = 1;
	for (const char *p = genres; *p; p++) if (*p == ',') count++;
	return count;
}

typedef struct {
	SbMetadataResolver *resolver;
	uint64_t generation;
	SbEnrichmentPriority priority;
	char key[80];
} SbMusicBrainzProgress;

static bool enrichmentWorkPreempted (void *data) {
	SbMusicBrainzProgress *progress = data;
	if (progress == NULL || progress->resolver == NULL) return false;
	pthread_mutex_lock (&progress->resolver->lock);
	const bool preempted = progress->resolver->stopping ||
			(progress->priority == SB_ENRICH_PRIORITY_CURRENT ?
			(progress->resolver->currentKey[0] != '\0' &&
			strcmp (progress->key, progress->resolver->currentKey) != 0) :
			progress->resolver->jobs[SB_ENRICH_PRIORITY_CURRENT].pending);
	pthread_mutex_unlock (&progress->resolver->lock);
	return preempted;
}

void SbMetadataResolverPublishProgress (SbMetadataResolver *resolver,
		const uint64_t generation, const SbMetadataResult *result) {
	if (resolver == NULL || result == NULL) return;
	pthread_mutex_lock (&resolver->lock);
	/* Speculative jobs never publish.  A foreground job that was overtaken by a
	 * rapid skip also loses publication rights as soon as currentGeneration
	 * advances.  The zero case preserves the narrow direct unit-test hook. */
	if (!resolver->stopping && (resolver->currentGeneration == 0 ||
			resolver->currentGeneration == generation)) {
		resolver->completed = *result;
		resolver->metadataResultGeneration = generation;
		resolver->resultReady = true;
	}
	pthread_mutex_unlock (&resolver->lock);
}

static void publishMusicBrainzProgress (void *data,
		const SbMetadataResult *result, const bool basicOnly) {
	SbMusicBrainzProgress *progress = data;
	if (progress == NULL || progress->resolver == NULL) return;
	SbMetadataResult published = *result;
	if (basicOnly) {
		published.release[0] = published.releaseDate[0] = '\0';
		published.firstReleaseDate[0] = published.releaseType[0] = '\0';
		published.releaseCountry[0] = published.label[0] = '\0';
		published.catalogNumber[0] = published.isrc[0] = published.genres[0] = '\0';
		published.releaseId[0] = published.releaseGroupId[0] = '\0';
		published.categorySource = SB_METADATA_CATEGORIES_NONE;
	}
	SbMetadataResolverPublishProgress (progress->resolver, progress->generation,
			&published);
}

enum { SB_MUSICBRAINZ_METADATA_REQUEST_MAX = 5 };

unsigned int SbMusicBrainzRequestBudget (void) {
	return SB_MUSICBRAINZ_METADATA_REQUEST_MAX;
}

typedef struct {
	char releaseId[SB_ENRICH_ID_MAX];
	char releaseGroupId[SB_ENRICH_ID_MAX];
} SbMusicBrainzCompletionState;

static const char *musicBrainzCompletionReason (const SbMetadataResult *result,
		const SbMetadataCompletionStep step) {
	if (step == SB_METADATA_COMPLETION_RELEASE) {
		if (result->releaseDate[0] == '\0') return "missing_release_date";
		if (result->releaseGroupId[0] == '\0') return "missing_release_group";
		if (result->releaseCountry[0] == '\0') return "missing_country";
		if (result->label[0] == '\0') return "missing_label";
		return "missing_catalog";
	}
	if (result->firstReleaseDate[0] == '\0') return "missing_first_release_date";
	if (result->releaseType[0] == '\0') return "missing_release_type";
	if (result->genres[0] == '\0') return "missing_categories";
	return "tags_need_genre_upgrade";
}

static void musicBrainzCompleteSelection (SbMetadataResult *result,
		SbMusicBrainzCompletionState *state, int *logicalRequests,
		SbHttpBuffer *body, void *progressData) {
	char url[1400];
	while (*logicalRequests < SB_MUSICBRAINZ_METADATA_REQUEST_MAX) {
		if (enrichmentWorkPreempted (progressData)) return;
		const SbMetadataCompletionStep next = SbMusicBrainzNextCompletion (result,
				state->releaseId, state->releaseGroupId);
		if (next == SB_METADATA_COMPLETION_NONE) return;
		const bool release = next == SB_METADATA_COMPLETION_RELEASE;
		const char *id = release ? result->releaseId : result->releaseGroupId;
		char *attempted = release ? state->releaseId : state->releaseGroupId;
		copyText (attempted, SB_ENRICH_ID_MAX, id);
		free (body->data); *body = (SbHttpBuffer) {NULL, 0};
		(*logicalRequests)++;
		if (release)
			snprintf (url, sizeof (url),
					"https://musicbrainz.org/ws/2/release/%s?inc=artist-credits+labels+release-groups+recordings&fmt=json",
					id);
		else
			snprintf (url, sizeof (url),
					"https://musicbrainz.org/ws/2/release-group/%s?inc=artist-credits+genres+tags&fmt=json",
					id);
		enrichmentDebugPrint ("metadata detail %s required=yes reason=%s id=%s request=%d/%d\n",
				release ? "release" : "release_group",
				musicBrainzCompletionReason (result, next), id,
				*logicalRequests, SB_MUSICBRAINZ_METADATA_REQUEST_MAX);
		SbLookupStatus optionalFailure = SB_LOOKUP_NO_MATCH;
		const bool fetched = musicBrainzFetch (url,
				release ? "selected_release_detail" : "release_group_detail",
				body, &optionalFailure);
		const bool applied = fetched && (release ?
				SbMusicBrainzApplyReleaseDetail (body->data, result) :
				SbMusicBrainzApplyReleaseGroupDetail (body->data, result));
		if (applied) publishMusicBrainzProgress (progressData, result, false);
		else enrichmentDebugPrint ("metadata completion step=%s id=%s result=%s retained=available\n",
				release ? "release_detail" : "release_group_detail", id,
				fetched ? "parse_rejected" : optionalFailure == SB_LOOKUP_UNAVAILABLE ?
				"transient_failure" : "failed");
	}
}

typedef enum {
	SB_ALBUM_SEARCH_FAILED = 0,
	SB_ALBUM_SEARCH_NO_SELECTION,
	SB_ALBUM_SEARCH_SELECTED,
} SbMusicBrainzAlbumSearchResult;

static SbMusicBrainzAlbumSearchResult musicBrainzAlbumSearch (
		const SbTrackIdentity *identity,
		const char *album, const char *step, SbMetadataResult *result,
		int *logicalRequests, SbHttpBuffer *body, void *progressData) {
	if (*logicalRequests >= SB_MUSICBRAINZ_METADATA_REQUEST_MAX)
		return SB_ALBUM_SEARCH_FAILED;
	if (enrichmentWorkPreempted (progressData))
		return SB_ALBUM_SEARCH_FAILED;
	CURL *escapeCurl = curl_easy_init (); char query[700], url[1400];
	char *escaped = NULL;
	snprintf (query, sizeof (query), "release:\"%s\" AND artist:\"%s\"",
			album, identity->artist);
	if (escapeCurl != NULL) escaped = curl_easy_escape (escapeCurl, query, 0);
	if (escaped == NULL) {
		if (escapeCurl != NULL) curl_easy_cleanup (escapeCurl);
		return SB_ALBUM_SEARCH_FAILED;
	}
	snprintf (url, sizeof (url),
			"https://musicbrainz.org/ws/2/release/?query=%s&fmt=json&limit=12", escaped);
	curl_free (escaped); curl_easy_cleanup (escapeCurl);
	free (body->data); *body = (SbHttpBuffer) {NULL, 0};
	(*logicalRequests)++;
	const char *queryKind = strcmp (step, "album_family") == 0 ? "family" : "exact";
	enrichmentDebugPrint ("metadata album_lookup query=%s album=\"%s\" request=%d/%d\n",
			queryKind, album, *logicalRequests, SB_MUSICBRAINZ_METADATA_REQUEST_MAX);
	SbLookupStatus optionalFailure = SB_LOOKUP_NO_MATCH;
	if (!musicBrainzFetch (url, step, body, &optionalFailure))
		return SB_ALBUM_SEARCH_FAILED;
	const bool selected = SbMusicBrainzSelectRelease (body->data, identity, result);
	if (selected) publishMusicBrainzProgress (progressData, result, false);
	return selected ? SB_ALBUM_SEARCH_SELECTED : SB_ALBUM_SEARCH_NO_SELECTION;
}

static bool musicBrainzLookup (const SbTrackIdentity *id,
		SbMetadataResult *result, void *progressData) {
	CURL *escapeCurl = curl_easy_init (); SbHttpBuffer body = {NULL, 0};
	SbMetadataResultInit (result); copyText (result->provider, sizeof (result->provider), "MusicBrainz");
	if (escapeCurl == NULL) goto failed;
	char query[600]; snprintf (query, sizeof (query), "recording:\"%s\" AND artist:\"%s\"", id->title, id->artist);
	char *escaped = curl_easy_escape (escapeCurl, query, 0); char url[1400];
	if (escaped == NULL) goto failed;
	snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/recording/?query=%s&fmt=json&limit=8", escaped);
	curl_free (escaped); curl_easy_cleanup (escapeCurl); escapeCurl = NULL;
	int logicalRequests = 1;
	SbLookupStatus initialFailure = SB_LOOKUP_ERROR;
	if (!musicBrainzFetch (url, "recording_search", &body, &initialFailure)) {
		result->status = initialFailure;
		copyText (result->error, sizeof (result->error),
				initialFailure == SB_LOOKUP_UNAVAILABLE ?
				"MusicBrainz temporarily unavailable" : "MusicBrainz request failed");
		goto finished;
	}
	bool ok = SbMusicBrainzParse (body.data, id, result);
	enrichmentDebugPrint ("enrichment provider=musicbrainz parse=%s state=%d\n",
			ok || result->status == SB_LOOKUP_NO_MATCH ? "ok" : "error",
			(int) result->status);
	if (ok) {
		publishMusicBrainzProgress (progressData, result, true);
		if (result->releaseId[0] || result->releaseGroupId[0])
			publishMusicBrainzProgress (progressData, result, false);
	}
	SbMusicBrainzCompletionState completion = {{0}, {0}};
	if (ok) musicBrainzCompleteSelection (result, &completion,
			&logicalRequests, &body, progressData);
	if (ok && !enrichmentWorkPreempted (progressData) &&
			releaseMetadataWeak (result) && result->releaseGroupId[0] &&
			logicalRequests < SB_MUSICBRAINZ_METADATA_REQUEST_MAX) {
		free (body.data); body = (SbHttpBuffer) {NULL, 0};
		logicalRequests++;
		snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/release?release-group=%s&inc=artist-credits+release-groups+labels&fmt=json&limit=25",
				result->releaseGroupId);
		SbLookupStatus optionalFailure = SB_LOOKUP_NO_MATCH;
		if (musicBrainzFetch (url, "alternate_release", &body, &optionalFailure) &&
				SbMusicBrainzSelectRelease (body.data, id, result)) {
			publishMusicBrainzProgress (progressData, result, false);
			musicBrainzCompleteSelection (result, &completion,
					&logicalRequests, &body, progressData);
		}
	}
	if (ok && !enrichmentWorkPreempted (progressData) &&
			(result->releaseId[0] == '\0' || result->releaseGroupId[0] == '\0') &&
			id->album[0] && id->artist[0]) {
		const SbMusicBrainzAlbumSearchResult exact = musicBrainzAlbumSearch (id,
				id->album, "album_exact", result, &logicalRequests, &body,
				progressData);
		if (exact == SB_ALBUM_SEARCH_SELECTED)
			musicBrainzCompleteSelection (result, &completion,
					&logicalRequests, &body, progressData);
		if (exact != SB_ALBUM_SEARCH_FAILED &&
				(result->releaseId[0] == '\0' || result->releaseGroupId[0] == '\0') &&
				logicalRequests < SB_MUSICBRAINZ_METADATA_REQUEST_MAX) {
			char family[SB_ENRICH_TEXT_MAX];
			if (SbMusicBrainzAlbumFamilyTitle (id->album, family, sizeof (family)) &&
					musicBrainzAlbumSearch (id, family, "album_family", result,
							&logicalRequests, &body, progressData) ==
							SB_ALBUM_SEARCH_SELECTED)
				musicBrainzCompleteSelection (result, &completion,
						&logicalRequests, &body, progressData);
		}
	}
	if (ok) enrichmentDebugPrint ("metadata release selected title=\"%s\" release_mbid=%s release_group_mbid=%s first_release_date=%s original_date_source=%s release_date=%s edition_date_source=%s type=\"%s\" country=%s label=\"%s\" catalog=\"%s\" categories_source=%s categories=%zu requests=%d request_cap=%d attempt_cap=%d\n",
			result->release, result->releaseId, result->releaseGroupId,
			result->firstReleaseDate,
			result->firstReleaseDate[0] && result->releaseGroupId[0] ? "release_group" : "none",
			result->releaseDate,
			result->releaseDate[0] && result->releaseId[0] ? "selected_release" : "none",
			result->releaseType,
			result->releaseCountry, result->label, result->catalogNumber,
			result->categorySource == SB_METADATA_CATEGORIES_GENRES ? "genres" :
			result->categorySource == SB_METADATA_CATEGORIES_TAGS ? "tags" : "none",
			metadataGenreCount (result->genres), logicalRequests,
			SB_MUSICBRAINZ_METADATA_REQUEST_MAX,
			SB_MUSICBRAINZ_METADATA_REQUEST_MAX * 2);
	if (ok) enrichmentDebugPrint ("metadata categories source=%s count=%zu\n",
			result->categorySource == SB_METADATA_CATEGORIES_GENRES ? "genres" :
			result->categorySource == SB_METADATA_CATEGORIES_TAGS ? "tags" : "none",
			metadataGenreCount (result->genres));
finished:
	enrichmentDebugPrint ("enrichment provider=musicbrainz final_state=%d requests=%d request_cap=%d attempt_cap=%d\n",
			(int) result->status, logicalRequests,
			SB_MUSICBRAINZ_METADATA_REQUEST_MAX,
			SB_MUSICBRAINZ_METADATA_REQUEST_MAX * 2);
	free (body.data); return result->status == SB_LOOKUP_AVAILABLE;
failed:
	if (escapeCurl != NULL) curl_easy_cleanup (escapeCurl);
	free (body.data); result->status = SB_LOOKUP_ERROR;
	copyText (result->provider, sizeof (result->provider), "MusicBrainz");
	copyText (result->error, sizeof (result->error), "Unable to create request");
	enrichmentDebugPrint ("enrichment provider=musicbrainz final_state=%d detail=\"%s\"\n",
			(int) result->status, result->error);
	return false;
}

typedef struct { long retryAfter; } SbLrclibHeaders;
typedef struct { unsigned int attempts; long httpStatus; CURLcode curlCode;
	const char *successfulStep; double score; } SbLrclibLookupTrace;

static size_t lrclibHeader (char *ptr, size_t size, size_t count, void *userdata) {
	const size_t length = size * count; SbLrclibHeaders *headers = userdata;
	if (length > 12 && strncasecmp (ptr, "Retry-After:", 12) == 0) {
		char value[32]; size_t n = length - 12;
		if (n >= sizeof (value)) n = sizeof (value) - 1;
		memcpy (value, ptr + 12, n); value[n] = '\0';
		headers->retryAfter = strtol (value, NULL, 10);
	}
	return length;
}

bool SbLrclibTransientFailure (const long status, const int curlCode) {
	return curlCode != CURLE_OK || status == 429 || status == 500 ||
			status == 502 || status == 503 || status == 504;
}

bool SbLrclibShouldRetry (const long status, const int curlCode,
		const unsigned int attempt) {
	return attempt < 2 && SbLrclibTransientFailure (status, curlCode);
}

unsigned int SbLrclibRetryDelayMs (const long retryAfter) {
	return (unsigned int) (retryAfter > 0 ? (retryAfter > 5 ? 5 : retryAfter) : 1) * 1000;
}

static bool lrclibRequestOnce (const SbTrackIdentity *id, const char *artistName,
		const char *title, const bool constrained, const char *variant,
		const unsigned int attempt, SbLyricsResult *result, long *retryAfter,
		SbLrclibLookupTrace *trace) {
	CURL *curl = curl_easy_init (); SbHttpBuffer body = {NULL, 0};
	SbLrclibHeaders headers = {0};
	if (curl == NULL) goto failed;
	char *artist = curl_easy_escape (curl, artistName, 0);
	char *track = curl_easy_escape (curl, title, 0);
	char *album = constrained && id->album[0] ?
			curl_easy_escape (curl, id->album, 0) : NULL;
	if (artist == NULL || track == NULL ||
			(constrained && id->album[0] && album == NULL)) {
		curl_free (artist); curl_free (track); curl_free (album); goto failed;
	}
	char url[1800];
	int used = snprintf (url, sizeof (url),
			"https://lrclib.net/api/get?track_name=%s&artist_name=%s%s%s",
			track, artist, album != NULL ? "&album_name=" : "", album != NULL ? album : "");
	if (constrained && id->duration > 0 && id->duration <= 3600 &&
			used > 0 && (size_t) used < sizeof (url))
		snprintf (url + used, sizeof (url) - (size_t) used, "&duration=%u", id->duration);
	curl_free (artist); curl_free (track); curl_free (album);
	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_USERAGENT,
			PROGRAM_NAME "/" VERSION " (https://github.com/signalbox-player/signalbox)");
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, httpWrite);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, lrclibHeader);
	curl_easy_setopt (curl, CURLOPT_HEADERDATA, &headers);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 12L);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
	CURLcode code = curl_easy_perform (curl); long status = 0;
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &status); curl_easy_cleanup (curl);
	trace->attempts++; trace->httpStatus = status; trace->curlCode = code;
	enrichmentDebugPrint ("lyrics provider=lrclib attempt=%u step=%s endpoint=/api/get lookup_artist=\"%s\" lookup_title=\"%s\" album_included=%s duration_included=%s http=%ld network_error=\"%s\"\n",
			attempt, variant, artistName, title, constrained && id->album[0] ? "yes" : "no",
			constrained && id->duration > 0 && id->duration <= 3600 ? "yes" : "no",
			status, code == CURLE_OK ? "none" : curl_easy_strerror (code));
	if (retryAfter != NULL) *retryAfter = headers.retryAfter;
	if (code == CURLE_OK && status == 200 && body.data != NULL) {
		json_object *candidate = json_tokener_parse (body.data);
		const double score = candidate != NULL &&
				json_object_is_type (candidate, json_type_object) ?
				lrclibCandidateScore (id, candidate) : 0.0;
		bool ok = score >= 0.84 && SbLrclibParse (body.data, result);
		lrclibLogCandidate ("get", variant, ok ? "accept" : "reject",
				ok ? "exact endpoint match above threshold" :
				lrclibCandidateRejection (id, candidate, score), candidate, score);
		if (ok) { trace->successfulStep = variant; trace->score = score; }
		if (candidate != NULL) json_object_put (candidate);
		free (body.data);
		if (!ok) {
			SbLyricsResultDestroy (result);
			copyText (result->provider, sizeof (result->provider), "LRCLIB");
			result->status = SB_LOOKUP_NO_MATCH;
		}
		return ok;
	}
	free (body.data); SbLyricsResultInit (result);
	copyText (result->provider, sizeof (result->provider), "LRCLIB");
	if (code == CURLE_OK && status == 404) { result->status = SB_LOOKUP_NO_MATCH; return false; }
	result->status = SbLrclibTransientFailure (status, code) ?
			SB_LOOKUP_UNAVAILABLE : SB_LOOKUP_ERROR;
	copyText (result->error, sizeof (result->error), "LRCLIB request failed"); return false;
failed:
	if (curl != NULL) curl_easy_cleanup (curl);
	SbLyricsResultInit (result); result->status = SB_LOOKUP_ERROR;
	copyText (result->provider, sizeof (result->provider), "LRCLIB");
	copyText (result->error, sizeof (result->error), "LRCLIB request failed"); return false;
}

static bool lrclibRequest (const SbTrackIdentity *id, const char *artistName,
		const char *title, const bool constrained, const char *variant,
		SbLyricsResult *result, SbLrclibLookupTrace *trace) {
	long retryAfter = 0;
	for (unsigned int attempt = 1; attempt <= 2; attempt++) {
		const bool found = lrclibRequestOnce (id, artistName, title, constrained,
				variant, attempt, result, &retryAfter, trace);
		if (found || !SbLrclibShouldRetry (trace->httpStatus,
				trace->curlCode, attempt)) return found;
		const unsigned int delay = SbLrclibRetryDelayMs (retryAfter);
		enrichmentDebugPrint ("lyrics provider=lrclib retry delay_ms=%u\n", delay);
		SbPlatformSleepMs (delay);
		SbLyricsResultDestroy (result);
	}
	return false;
}

static bool lrclibSearchRequestOnce (const SbTrackIdentity *id, const char *artistName,
		const char *title, const unsigned int stepNumber, SbLyricsResult *result,
		long *retryAfter, SbLrclibLookupTrace *trace) {
	CURL *curl = curl_easy_init (); SbHttpBuffer body = {NULL, 0};
	SbLrclibHeaders headers = {0}; CURLcode code = CURLE_FAILED_INIT; long status = 0;
	if (curl == NULL) goto failed;
	char *artist = curl_easy_escape (curl, artistName, 0);
	char *track = curl_easy_escape (curl, title, 0);
	if (artist == NULL || track == NULL) {
		curl_free (artist); curl_free (track); goto failed;
	}
	char url[1400]; snprintf (url, sizeof (url),
			"https://lrclib.net/api/search?track_name=%s&artist_name=%s",
			track, artist);
	curl_free (artist); curl_free (track);
	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_USERAGENT,
			PROGRAM_NAME "/" VERSION " (https://github.com/signalbox-player/signalbox)");
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, httpWrite);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, lrclibHeader);
	curl_easy_setopt (curl, CURLOPT_HEADERDATA, &headers);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 12L);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
	code = curl_easy_perform (curl);
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup (curl); curl = NULL;
	if (retryAfter != NULL) *retryAfter = headers.retryAfter;
	trace->attempts++; trace->httpStatus = status; trace->curlCode = code;
	enrichmentDebugPrint ("lyrics provider=lrclib attempt=%u step=search endpoint=/api/search lookup_artist=\"%s\" lookup_title=\"%s\" album_included=no duration_included=no http=%ld network_error=\"%s\"\n",
			stepNumber, artistName, title, status,
			code == CURLE_OK ? "none" : curl_easy_strerror (code));
	if (code == CURLE_OK && status == 200 && body.data != NULL) {
		double score = 0.0;
		const bool ok = lrclibSearchParse (body.data, id, result, &score, "search");
		if (ok) { trace->successfulStep = "search"; trace->score = score; }
		free (body.data); return ok;
	}
failed:
	if (curl != NULL) curl_easy_cleanup (curl);
	free (body.data); SbLyricsResultInit (result);
	copyText (result->provider, sizeof (result->provider), "LRCLIB");
	result->status = SbLrclibTransientFailure (status, code) ?
			SB_LOOKUP_UNAVAILABLE : SB_LOOKUP_ERROR;
	copyText (result->error, sizeof (result->error), "LRCLIB search failed");
	return false;
}

static bool lrclibSearchRequest (const SbTrackIdentity *id,
		const char *artistName, const char *title, SbLyricsResult *result,
		SbLrclibLookupTrace *trace) {
	long retryAfter = 0;
	for (unsigned int attempt = 1; attempt <= 2; attempt++) {
		const bool found = lrclibSearchRequestOnce (id, artistName, title,
				attempt, result, &retryAfter, trace);
		if (found || !SbLrclibShouldRetry (trace->httpStatus,
				trace->curlCode, attempt)) return found;
		const unsigned int delay = SbLrclibRetryDelayMs (retryAfter);
		enrichmentDebugPrint ("lyrics provider=lrclib retry delay_ms=%u\n", delay);
		SbPlatformSleepMs (delay);
		SbLyricsResultDestroy (result);
	}
	return false;
}

static bool lrclibLookup (const SbTrackIdentity *id, SbLyricsResult *result,
		void *progressData) {
	SbLrclibLookupTrace trace = {0};
	enrichmentDebugPrint ("lyrics lookup provider=lrclib original_artist=\"%s\" original_title=\"%s\" original_album=\"%s\" original_duration=%u\n",
			id->artist, id->title, id->album, id->duration);
	bool found = lrclibRequest (id, id->artist, id->title, true, "exact",
			result, &trace);
	char title[SB_ENRICH_TEXT_MAX]; const bool cleanedTitle =
			SbLyricsFallbackTitle (id->title, title, sizeof (title));
	char artist[SB_ENRICH_TEXT_MAX]; const bool baseArtist =
			SbLyricsFallbackArtist (id->artist, artist, sizeof (artist));
	if (!found && result->status == SB_LOOKUP_NO_MATCH &&
			!enrichmentWorkPreempted (progressData)) {
		if (cleanedTitle) {
			SbPlatformSleepMs (300);
			found = lrclibRequest (id, id->artist, title, true,
					"clean-title", result, &trace);
		}
	}
	if (!found && result->status == SB_LOOKUP_NO_MATCH &&
			!enrichmentWorkPreempted (progressData)) {
		SbPlatformSleepMs (300);
		found = lrclibRequest (id, baseArtist ? artist : id->artist,
				cleanedTitle ? title : id->title, false,
				baseArtist ? "base-artist-unconstrained" : "unconstrained",
				result, &trace);
	}
	if (!found && result->status == SB_LOOKUP_NO_MATCH &&
			!enrichmentWorkPreempted (progressData)) {
		SbPlatformSleepMs (300);
		found = lrclibSearchRequest (id, baseArtist ? artist : id->artist,
				cleanedTitle ? title : id->title, result, &trace);
	}
	if (found) enrichmentDebugPrint ("lyrics lookup result=available step=%s score=%.3f\n",
			trace.successfulStep, trace.score);
	else if (result->status == SB_LOOKUP_NO_MATCH)
		enrichmentDebugPrint ("lyrics lookup result=no_match attempts=%u\n", trace.attempts);
	else enrichmentDebugPrint ("lyrics lookup result=temporarily_unavailable provider=lrclib http=%ld network_error=\"%s\"\n",
			trace.httpStatus, trace.curlCode == CURLE_OK ? "none" :
			curl_easy_strerror (trace.curlCode));
	return found;
}

static bool musicBrainzArtFetch (const char *url, SbHttpBuffer *body,
		SbLookupStatus *failure) {
	return musicBrainzFetch (url, "art_release_lookup", body, failure);
}

static bool cachedCoverLookup (SbMetadataResolver *r, const char *release,
		SbAlbumArtResult *art) {
	const SbCacheEntry *cached = SbPersistentCacheGet (&r->artPersistent,
			"coverartarchive-v2", release, SB_CACHE_ART, time (NULL));
	if (cached && artDeserialize (cached, release, art)) {
		enrichmentDebugPrint ("art cache=%s release_mbid=%s path=%s\n",
				art->status == SB_LOOKUP_AVAILABLE ? "hit" : "negative", release, art->cachedPath);
		return art->status == SB_LOOKUP_AVAILABLE;
	}
	SbCoverArtLookup (release, r->artDirectory, art);
	if (art->status == SB_LOOKUP_AVAILABLE || art->status == SB_LOOKUP_NO_MATCH) {
		char *payload = artSerialize (art); if (payload != NULL) {
			SbPersistentCachePut (&r->artPersistent, "coverartarchive-v2", release,
					art->status, payload, SB_CACHE_ART, time (NULL)); free (payload);
		}
	}
	return art->status == SB_LOOKUP_AVAILABLE;
}

static bool foregroundArtPending (SbMetadataResolver *r) {
	pthread_mutex_lock (&r->lock);
	const bool pending = r->stopping ||
			(r->artJobs[SB_ENRICH_PRIORITY_CURRENT].pending &&
			strcmp (r->artJobs[SB_ENRICH_PRIORITY_CURRENT].key,
			r->activeArtKey) != 0);
	pthread_mutex_unlock (&r->lock);
	return pending;
}

static bool rankedReleaseArt (SbMetadataResolver *r, const char *json,
		const SbTrackIdentity *identity, const char *exclude, const char *step,
		SbAlbumArtResult *art) {
	json_object *root = json_tokener_parse (json), *releases = NULL;
	if (root == NULL) return false;
	json_object_object_get_ex (root, "releases", &releases);
	if (!json_object_is_type (releases, json_type_array)) { json_object_put (root); return false; }
	const size_t count = json_object_array_length (releases) > 32 ? 32 :
			json_object_array_length (releases); bool tried[32] = {false};
	for (size_t attempt = 0; attempt < count && attempt < 6; attempt++) {
		if (foregroundArtPending (r)) break;
		double top = -1000.0; size_t topIndex = count;
		for (size_t i = 0; i < count; i++) if (!tried[i]) {
			json_object *release = json_object_array_get_idx (releases, i);
			if (exclude != NULL && strcmp (jsonString (release, "id"), exclude) == 0) { tried[i] = true; continue; }
			double album, artist; bool compilation; const char *type;
			double score = artReleaseScore (release, identity, &album, &artist, &compilation, &type);
			bool accepted = identity->album[0] ? album >= .68 && artist >= .5 : artist >= .5;
			if (accepted && score > top) { top = score; topIndex = i; }
		}
		if (topIndex == count) break; tried[topIndex] = true;
		json_object *release = json_object_array_get_idx (releases, topIndex);
		const char *releaseId = jsonString (release, "id");
		enrichmentDebugPrint ("art fallback step=%s release_mbid=%s score=%.1f\n", step, releaseId, top);
		if (*releaseId && cachedCoverLookup (r, releaseId, art)) { json_object_put (root); return true; }
		if (art->status == SB_LOOKUP_UNAVAILABLE || art->status == SB_LOOKUP_ERROR) {
			json_object_put (root); return false;
		}
	}
	json_object_put (root); return false;
}

static void trackWorkKey (const SbTrackIdentity *identity, char *key,
		const size_t size) {
	SbTrackWorkKey (identity, key, size);
}

static const char *priorityName (const SbEnrichmentPriority priority) {
	return priority == SB_ENRICH_PRIORITY_CURRENT ? "current" :
			priority == SB_ENRICH_PRIORITY_NEXT ? "next" : "next2";
}

static const char *lookupStateName (const SbLookupStatus status) {
	switch (status) {
		case SB_LOOKUP_AVAILABLE: return "available";
		case SB_LOOKUP_INSTRUMENTAL: return "instrumental";
		case SB_LOOKUP_NO_MATCH: return "no_match";
		case SB_LOOKUP_UNAVAILABLE: return "temporarily_unavailable";
		case SB_LOOKUP_ERROR: return "error";
		case SB_LOOKUP_LOADING: return "loading";
		default: return "idle";
	}
}

static const char *lyricsPrefetchState (const SbLyricsResult *lyrics) {
	if (lyrics->status == SB_LOOKUP_AVAILABLE)
		return lyrics->syncedLyrics != NULL ? "synced" : "plain";
	return lookupStateName (lyrics->status);
}

static bool metadataReusable (const SbLookupStatus status) {
	return status == SB_LOOKUP_AVAILABLE || status == SB_LOOKUP_NO_MATCH;
}

static bool lyricsReusable (const SbLookupStatus status) {
	return status == SB_LOOKUP_AVAILABLE || status == SB_LOOKUP_INSTRUMENTAL ||
			status == SB_LOOKUP_NO_MATCH;
}

static bool artReusable (const SbLookupStatus status) {
	return status == SB_LOOKUP_AVAILABLE || status == SB_LOOKUP_NO_MATCH;
}

static SbEnrichmentPrefetchEntry *prefetchEntryLocked (
		SbMetadataResolver *r, const char *key, const SbTrackIdentity *identity,
		const bool create) {
	for (size_t i = 0; i < SB_ENRICH_PREFETCH_CACHE_MAX; i++)
		if (r->prefetch[i].used && strcmp (r->prefetch[i].key, key) == 0)
			return &r->prefetch[i];
	if (!create) return NULL;
	SbEnrichmentPrefetchEntry *entry =
			&r->prefetch[r->prefetchNext++ % SB_ENRICH_PREFETCH_CACHE_MAX];
	if (entry->used) SbLyricsResultDestroy (&entry->lyrics);
	memset (entry, 0, sizeof (*entry));
	entry->used = true;
	copyText (entry->key, sizeof (entry->key), key);
	entry->identity = *identity;
	entry->startedAtMs = SbPlatformMonotonicMs ();
	SbLyricsResultInit (&entry->lyrics);
	SbAlbumArtResultInit (&entry->art);
	return entry;
}

static void prefetchFinishedLocked (SbEnrichmentPrefetchEntry *entry) {
	if (entry != NULL && entry->metadataDone && entry->lyricsDone &&
			entry->artDone && entry->finishedAtMs == 0)
		entry->finishedAtMs = SbPlatformMonotonicMs ();
}

static bool jobsPendingLocked (const SbMetadataResolver *r) {
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++)
		if (r->jobs[i].pending) return true;
	return false;
}

static bool artJobsPendingLocked (const SbMetadataResolver *r) {
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++)
		if (r->artJobs[i].pending) return true;
	return false;
}

static SbEnrichmentJob takeJobLocked (SbMetadataResolver *r) {
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++) {
		if (!r->jobs[i].pending) continue;
		SbEnrichmentJob job = r->jobs[i];
		r->jobs[i].pending = false;
		r->jobActive = true;
		r->activePriority = (SbEnrichmentPriority) i;
		copyText (r->activeKey, sizeof (r->activeKey), job.key);
		return job;
	}
	return (SbEnrichmentJob) {0};
}

static SbEnrichmentArtJob takeArtJobLocked (SbMetadataResolver *r) {
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++) {
		if (!r->artJobs[i].pending) continue;
		SbEnrichmentArtJob job = r->artJobs[i];
		r->artJobs[i].pending = false;
		r->artJobActive = true;
		r->activeArtPriority = (SbEnrichmentPriority) i;
		copyText (r->activeArtKey, sizeof (r->activeArtKey), job.key);
		return job;
	}
	return (SbEnrichmentArtJob) {0};
}

static void deferArtJobLocked (SbMetadataResolver *r,
		const SbEnrichmentArtJob *job) {
	if (!r->artJobs[job->priority].pending) {
		r->artJobs[job->priority] = *job;
		r->artJobs[job->priority].pending = true;
	}
	r->artJobActive = false;
	r->activeArtKey[0] = '\0';
	pthread_cond_broadcast (&r->cond);
}

static bool metadataCacheGetLocked (SbMetadataResolver *r, const char *key,
		SbMetadataResult *result) {
	for (size_t i = 0; i < SB_ENRICH_CACHE_MAX; i++)
		if (r->cache[i].used && strcmp (key, r->cache[i].key) == 0) {
			*result = r->cache[i].result;
			return true;
		}
	const SbCacheEntry *entry = SbPersistentCacheGet (&r->persistent,
			r->provider.name, key, SB_CACHE_METADATA, time (NULL));
	return entry != NULL && SbMetadataDeserialize (entry->payload, result);
}

static bool lyricsCacheGetLocked (SbMetadataResolver *r, const char *key,
		SbLyricsResult *result) {
	for (size_t i = 0; i < SB_ENRICH_CACHE_MAX; i++)
		if (r->lyricsCache[i].used &&
				strcmp (key, r->lyricsCache[i].key) == 0) {
			return SbLyricsResultCopy (result, &r->lyricsCache[i].result);
		}
	const SbCacheEntry *entry = SbPersistentCacheGet (&r->persistent,
			r->lyricsProvider.name, key, SB_CACHE_LYRICS, time (NULL));
	return entry != NULL && lyricsDeserialize (entry->payload, result);
}

static void scheduleArtLocked (SbMetadataResolver *r,
		const SbEnrichmentJob *job, const SbMetadataResult *metadata) {
	SbEnrichmentPriority priority = job->key[0] != '\0' &&
			strcmp (job->key, r->currentKey) == 0 ?
			SB_ENRICH_PRIORITY_CURRENT : r->activePriority;
	if (r->artJobActive && strcmp (r->activeArtKey, job->key) == 0) return;
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++) {
		if (!r->artJobs[i].pending ||
				strcmp (r->artJobs[i].key, job->key) != 0) continue;
		if (priority < (SbEnrichmentPriority) i) {
			r->artJobs[priority] = r->artJobs[i];
			r->artJobs[priority].priority = priority;
			r->artJobs[i].pending = false;
		}
		return;
	}
	SbEnrichmentArtJob *art = &r->artJobs[priority];
	memset (art, 0, sizeof (*art));
	art->pending = true;
	art->priority = priority;
	art->identity = job->identity;
	copyText (art->key, sizeof (art->key), job->key);
	copyText (art->releaseId, sizeof (art->releaseId),
			metadata->status == SB_LOOKUP_AVAILABLE ? metadata->releaseId : "");
	copyText (art->releaseGroupId, sizeof (art->releaseGroupId),
			metadata->status == SB_LOOKUP_AVAILABLE ? metadata->releaseGroupId : "");
	copyText (art->recordingId, sizeof (art->recordingId),
			metadata->status == SB_LOOKUP_AVAILABLE ? metadata->recordingId : "");
	art->generation = job->generation;
	art->queuedAtMs = job->queuedAtMs;
	pthread_cond_broadcast (&r->cond);
}

static void deferJobLocked (SbMetadataResolver *r,
		const SbEnrichmentJob *job, const SbEnrichmentPriority priority) {
	if (!r->jobs[priority].pending) {
		r->jobs[priority] = *job;
		r->jobs[priority].pending = true;
	}
	r->jobActive = false;
	r->activeKey[0] = '\0';
	pthread_cond_broadcast (&r->cond);
}

static void *artResolverThread (void *arg) {
	SbMetadataResolver *r = arg;
	for (;;) {
		pthread_mutex_lock (&r->lock);
		while (!artJobsPendingLocked (r) && !r->stopping)
			pthread_cond_wait (&r->cond, &r->lock);
		if (r->stopping) { pthread_mutex_unlock (&r->lock); break; }
		const SbEnrichmentArtJob job = takeArtJobLocked (r);
		pthread_mutex_unlock (&r->lock);
		const char *release = job.releaseId;
		const char *group = job.releaseGroupId;
		const char *recording = job.recordingId;
		const SbTrackIdentity identity = job.identity;
		const uint64_t generation = job.generation;
		SbAlbumArtResult art; SbAlbumArtResultInit(&art);
		char trackKey[80]; SbTrackCacheKey ("coverart-selection-v2", &identity, trackKey, sizeof (trackKey));
		const SbCacheEntry *trackCached = SbPersistentCacheGet (&r->artPersistent,
				"coverart-selection-v2", trackKey, SB_CACHE_ART, time (NULL));
		enrichmentDebugPrint("art state=loading generation=%llu artist=\"%s\" title=\"%s\" album=\"%s\" recording_mbid=%s release_mbid=%s release_group_mbid=%s\n",(unsigned long long)generation,identity.artist,identity.title,identity.album,recording,release,group);
		bool found = trackCached && artDeserialize (trackCached, release, &art) &&
				art.status == SB_LOOKUP_AVAILABLE;
		if (found) enrichmentDebugPrint ("art fallback cache=track_selection release_mbid=%s\n", art.releaseId);
		if (!found && release[0] && r->artDirectory) {
			enrichmentDebugPrint ("art fallback step=best_release release_mbid=%s\n", release);
			found = cachedCoverLookup (r, release, &art);
		}
		SbLookupStatus mbFailure = SB_LOOKUP_NO_MATCH;
		if (!found && !foregroundArtPending (r) &&
				art.status != SB_LOOKUP_UNAVAILABLE &&
				art.status != SB_LOOKUP_ERROR && group[0]) {
			char url[512]; SbHttpBuffer body = {0};
			snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/release?release-group=%s&inc=artist-credits+release-groups&fmt=json&limit=25", group);
			enrichmentDebugPrint ("art fallback step=release_group_lookup release_group_mbid=%s\n", group);
			if (musicBrainzArtFetch (url, &body, &mbFailure)) found = rankedReleaseArt (r,
					body.data, &identity, release, "alternate_edition", &art);
			free (body.data);
		}
		if (!found && !foregroundArtPending (r) &&
				art.status != SB_LOOKUP_UNAVAILABLE && art.status != SB_LOOKUP_ERROR &&
				identity.album[0] && identity.artist[0]) {
			CURL *curl = curl_easy_init (); SbHttpBuffer body = {0};
			if (curl != NULL) { char query[700], url[1600]; snprintf (query, sizeof (query),
					"release:\"%s\" AND artist:\"%s\"", identity.album, identity.artist);
				char *escaped = curl_easy_escape (curl, query, 0); curl_easy_cleanup (curl);
				if (escaped != NULL) { snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/release/?query=%s&fmt=json&limit=12", escaped); curl_free (escaped);
					enrichmentDebugPrint ("art fallback step=bounded_album_lookup artist=\"%s\" album=\"%s\"\n", identity.artist, identity.album);
					if (musicBrainzArtFetch (url, &body, &mbFailure)) found = rankedReleaseArt (r,
							body.data, &identity, NULL, "album_release", &art); }
			}
			free (body.data);
		}
		if (!found && job.priority != SB_ENRICH_PRIORITY_CURRENT &&
				foregroundArtPending (r)) {
			pthread_mutex_lock (&r->lock);
			deferArtJobLocked (r, &job);
			pthread_mutex_unlock (&r->lock);
			continue;
		}
		if (found) { snprintf (art.reason, sizeof (art.reason), "available"); char *payload=artSerialize(&art);
			if(payload){SbPersistentCachePut(&r->artPersistent,"coverart-selection-v2",trackKey,art.status,payload,SB_CACHE_ART,time(NULL));free(payload);} }
		else if (art.status != SB_LOOKUP_UNAVAILABLE && art.status != SB_LOOKUP_ERROR) {
			art.status = mbFailure == SB_LOOKUP_UNAVAILABLE ? SB_LOOKUP_UNAVAILABLE :
					mbFailure == SB_LOOKUP_ERROR ? SB_LOOKUP_ERROR : SB_LOOKUP_NO_MATCH;
			snprintf (art.reason, sizeof (art.reason), art.status == SB_LOOKUP_NO_MATCH ?
					"no_match" : "temporarily_unavailable");
		}
		SbPersistentCacheWrite(&r->artPersistent);
		enrichmentDebugPrint("art provider=coverartarchive generation=%llu http=%ld result=%s mime=%s bytes=%zu url=%s path=%s\n",(unsigned long long)generation,art.httpStatus,art.reason,art.mimeType,art.byteCount,art.sourceUrl,art.cachedPath);
		pthread_mutex_lock (&r->lock);
		SbEnrichmentPrefetchEntry *entry = prefetchEntryLocked (r, job.key,
				&job.identity, false);
		if (entry != NULL) {
			entry->artDone = true;
			entry->artReady = artReusable ((SbLookupStatus) art.status);
			entry->art = art;
			const uint64_t elapsed = SbPlatformMonotonicMs () - entry->startedAtMs;
			enrichmentDebugPrint ("art prefetch complete state=%s elapsed_ms=%llu\n",
					lookupStateName ((SbLookupStatus) art.status),
					(unsigned long long) elapsed);
			prefetchFinishedLocked (entry);
		}
		if (strcmp (job.key, r->currentKey) == 0) {
			r->completedArt = art;
			r->artResultGeneration = r->currentGeneration;
			r->artResultReady = true;
		}
		r->artJobActive = false;
		r->activeArtKey[0] = '\0';
		pthread_cond_broadcast (&r->cond);
		pthread_mutex_unlock (&r->lock);
	}
	return NULL;
}

static void *resolverThread (void *arg) {
	SbMetadataResolver *r = arg; time_t lastLyricsRequest = 0;
	for (;;) {
		pthread_mutex_lock (&r->lock);
		while (!jobsPendingLocked (r) && !r->stopping)
			pthread_cond_wait (&r->cond, &r->lock);
		if (r->stopping) { pthread_mutex_unlock (&r->lock); break; }
		const SbEnrichmentJob job = takeJobLocked (r);
		const SbEnrichmentPriority priority = r->activePriority;
		const SbTrackIdentity id = job.identity;
		const uint64_t generation = job.generation;
		char key[80]; SbTrackCacheKey (r->provider.name, &id, key, sizeof (key));
		SbMetadataResult result; SbMetadataResultInit (&result);
		const bool cached = metadataCacheGetLocked (r, key, &result);
		char lyricsKey[80]; SbTrackCacheKey (r->lyricsProvider.name, &id,
				lyricsKey, sizeof (lyricsKey));
		SbLyricsResult lyrics; SbLyricsResultInit (&lyrics);
		const bool lyricsCached = lyricsCacheGetLocked (r, lyricsKey, &lyrics);
		pthread_mutex_unlock (&r->lock);
		SbMusicBrainzProgress progress = {.resolver = r,
				.generation = generation, .priority = priority};
		copyText (progress.key, sizeof (progress.key), job.key);
		/* Metadata goes first: LRCLIB retries and Retry-After pauses must never
		 * starve MusicBrainz on the shared, playback-independent worker. */
		if (!cached) {
			void *providerData = r->provider.lookup == musicBrainzLookup ?
					(void *) &progress :
					r->provider.data;
			r->provider.lookup (&id, &result, providerData);
		}
		pthread_mutex_lock (&r->lock);
		if (!cached && (result.status == SB_LOOKUP_AVAILABLE ||
				result.status == SB_LOOKUP_NO_MATCH)) {
			SbEnrichmentCacheEntry *e = &r->cache[r->cacheNext++ % SB_ENRICH_CACHE_MAX];
			e->used = true; copyText (e->key, sizeof (e->key), key); e->result = result;
			char *payload=SbMetadataSerialize(&result); if(payload){SbPersistentCachePut(&r->persistent,r->provider.name,key,result.status,payload,SB_CACHE_METADATA,time(NULL));free(payload);SbPersistentCacheWrite(&r->persistent);}
		}
		if (priority == SB_ENRICH_PRIORITY_CURRENT &&
				strcmp (job.key, r->currentKey) != 0) {
			r->jobActive = false; r->activeKey[0] = '\0';
			pthread_cond_broadcast (&r->cond);
			pthread_mutex_unlock (&r->lock);
			SbLyricsResultDestroy (&lyrics);
			continue;
		}
		SbEnrichmentPrefetchEntry *entry = prefetchEntryLocked (r, job.key,
				&job.identity, false);
		if (entry != NULL) {
			entry->metadataDone = true;
			entry->metadataReady = metadataReusable (result.status);
			entry->metadata = result;
			const uint64_t elapsed = SbPlatformMonotonicMs () - entry->startedAtMs;
			enrichmentDebugPrint ("enrichment prefetch complete provider=musicbrainz state=%s metadata ready elapsed_ms=%llu\n",
					lookupStateName (result.status), (unsigned long long) elapsed);
		}
		if (strcmp (job.key, r->currentKey) == 0) {
			r->completed = result;
			r->metadataResultGeneration = r->currentGeneration;
			r->resultReady = true;
		}
		enrichmentDebugPrint("art identity generation=%llu artist=\"%s\" title=\"%s\" album=\"%s\" release_mbid=%s release_group_mbid=%s\n",(unsigned long long)generation,id.artist,id.title,id.album,result.releaseId,result.releaseGroupId);
		scheduleArtLocked (r, &job, &result);
		/* A newly queued foreground track gets the worker before the speculative
		 * LRCLIB ladder.  The deferred job will hit its metadata cache on resume. */
		if (priority != SB_ENRICH_PRIORITY_CURRENT &&
				r->jobs[SB_ENRICH_PRIORITY_CURRENT].pending) {
			deferJobLocked (r, &job, priority);
			pthread_mutex_unlock (&r->lock);
			SbLyricsResultDestroy (&lyrics);
			continue;
		}
		pthread_mutex_unlock (&r->lock);
		if (!lyricsCached) {
			time_t now = time (NULL);
			if (lastLyricsRequest != 0 && now <= lastLyricsRequest) SbPlatformSleepMs (1000);
			void *providerData = r->lyricsProvider.lookup == lrclibLookup ?
					(void *) &progress : r->lyricsProvider.data;
			r->lyricsProvider.lookup (&id, &lyrics, providerData);
			lastLyricsRequest = time (NULL);
		}
		if (enrichmentWorkPreempted (&progress)) {
			pthread_mutex_lock (&r->lock);
			if (priority != SB_ENRICH_PRIORITY_CURRENT)
				deferJobLocked (r, &job, priority);
			else {
				r->jobActive = false; r->activeKey[0] = '\0';
				pthread_cond_broadcast (&r->cond);
			}
			pthread_mutex_unlock (&r->lock);
			SbLyricsResultDestroy (&lyrics);
			continue;
		}
		pthread_mutex_lock (&r->lock);
		if (!lyricsCached && (lyrics.status == SB_LOOKUP_AVAILABLE ||
				lyrics.status == SB_LOOKUP_INSTRUMENTAL || lyrics.status == SB_LOOKUP_NO_MATCH)) {
			SbLyricsCacheEntry *e = &r->lyricsCache[r->lyricsCacheNext++ % SB_ENRICH_CACHE_MAX];
			if (e->used) SbLyricsResultDestroy (&e->result);
			e->used = true; copyText (e->key, sizeof (e->key), lyricsKey);
			SbLyricsResultInit (&e->result); SbLyricsResultCopy (&e->result, &lyrics);
			char *payload=lyricsSerialize(&lyrics); if(payload){SbPersistentCachePut(&r->persistent,r->lyricsProvider.name,lyricsKey,lyrics.status,payload,SB_CACHE_LYRICS,time(NULL));free(payload);SbPersistentCacheWrite(&r->persistent);}
		}
		entry = prefetchEntryLocked (r, job.key, &job.identity, false);
		if (entry != NULL) {
			entry->lyricsDone = true;
			entry->lyricsReady = lyricsReusable (lyrics.status);
			SbLyricsResultCopy (&entry->lyrics, &lyrics);
			const uint64_t elapsed = SbPlatformMonotonicMs () - entry->startedAtMs;
			enrichmentDebugPrint ("enrichment prefetch complete provider=lrclib state=%s lyrics ready elapsed_ms=%llu\n",
					lyricsPrefetchState (&lyrics), (unsigned long long) elapsed);
			prefetchFinishedLocked (entry);
		}
		if (strcmp (job.key, r->currentKey) == 0) {
			SbLyricsResultCopy (&r->completedLyrics, &lyrics);
			r->lyricsResultGeneration = r->currentGeneration;
			r->lyricsResultReady = true;
		}
		r->jobActive = false; r->activeKey[0] = '\0';
		pthread_cond_broadcast (&r->cond);
		pthread_mutex_unlock (&r->lock);
		SbLyricsResultDestroy (&lyrics);
	}
	return NULL;
}

void SbMetadataResolverInit (SbMetadataResolver *r) {
	memset (r, 0, sizeof (*r)); pthread_mutex_init (&r->lock, NULL); pthread_cond_init (&r->cond, NULL);
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++)
		r->loggedPrefetchDecision[i] = -1;
	r->provider = (SbMetadataProvider) {"musicbrainz", musicBrainzLookup, NULL};
	r->lyricsProvider = (SbLyricsProvider) {"lrclib", lrclibLookup, NULL};
	SbLyricsResultInit (&r->completedLyrics);
	SbAlbumArtResultInit(&r->completedArt); SbPersistentCacheInit(&r->persistent,SbPlatformCachePath("enrichment-v2.json")); SbPersistentCacheLoad(&r->persistent,time(NULL)); r->artDirectory=SbPlatformCachePath("art"); if(r->artDirectory)SbPlatformEnsureDirectory(r->artDirectory);
	SbPersistentCacheInit(&r->artPersistent,SbPlatformCachePath("album-art-v2.json")); SbPersistentCacheLoad(&r->artPersistent,time(NULL));
}
bool SbMetadataResolverStart (SbMetadataResolver *r) {
	r->artStarted=pthread_create(&r->artThread,NULL,artResolverThread,r)==0;
	r->started = pthread_create (&r->thread, NULL, resolverThread, r) == 0;
	return r->started;
}
void SbMetadataResolverRequest (SbMetadataResolver *r, const SbTrackIdentity *id, uint64_t generation) {
	char key[80]; trackWorkKey (id, key, sizeof (key));
	pthread_mutex_lock (&r->lock);
	r->currentGeneration = generation;
	copyText (r->currentKey, sizeof (r->currentKey), key);
	r->resultReady = r->lyricsResultReady = r->artResultReady = false;
	SbEnrichmentPrefetchEntry *entry = prefetchEntryLocked (r, key, id, false);
	const bool metadataReady = entry != NULL && entry->metadataReady;
	const bool lyricsReady = entry != NULL && entry->lyricsReady;
	const bool artReady = entry != NULL && entry->artReady;
	if (metadataReady) {
		r->completed = entry->metadata;
		r->metadataResultGeneration = generation;
		r->resultReady = true;
	}
	if (lyricsReady) {
		SbLyricsResultCopy (&r->completedLyrics, &entry->lyrics);
		r->lyricsResultGeneration = generation;
		r->lyricsResultReady = true;
	}
	if (artReady) {
		r->completedArt = entry->art;
		r->artResultGeneration = generation;
		r->artResultReady = true;
	}
	if (entry != NULL) {
		const uint64_t age = SbPlatformMonotonicMs () - entry->startedAtMs;
		enrichmentDebugPrint ("enrichment prefetch promote artist=\"%s\" title=\"%s\" prefetch age_ms=%llu lyrics_ready_at_start=%s metadata_ready_at_start=%s art_ready_at_start=%s\n",
				id->artist, id->title, (unsigned long long) age,
				lyricsReady ? "yes" : "no", metadataReady ? "yes" : "no",
				artReady ? "yes" : "no");
	} else {
		enrichmentDebugPrint ("prefetch age_ms=0 lyrics_ready_at_start=no metadata_ready_at_start=no art_ready_at_start=no\n");
	}
	bool resolverWork = r->jobActive && strcmp (r->activeKey, key) == 0;
	bool artWork = r->artJobActive && strcmp (r->activeArtKey, key) == 0;
	if (resolverWork) r->activePriority = SB_ENRICH_PRIORITY_CURRENT;
	if (artWork) r->activeArtPriority = SB_ENRICH_PRIORITY_CURRENT;
	for (size_t i = SB_ENRICH_PRIORITY_NEXT;
			i < SB_ENRICH_PRIORITY_COUNT; i++) {
		if (r->jobs[i].pending && strcmp (r->jobs[i].key, key) == 0) {
			r->jobs[SB_ENRICH_PRIORITY_CURRENT] = r->jobs[i];
			r->jobs[SB_ENRICH_PRIORITY_CURRENT].generation = generation;
			r->jobs[i].pending = false;
			resolverWork = true;
		}
		if (r->artJobs[i].pending && strcmp (r->artJobs[i].key, key) == 0) {
			r->artJobs[SB_ENRICH_PRIORITY_CURRENT] = r->artJobs[i];
			r->artJobs[SB_ENRICH_PRIORITY_CURRENT].priority =
					SB_ENRICH_PRIORITY_CURRENT;
			r->artJobs[SB_ENRICH_PRIORITY_CURRENT].generation = generation;
			r->artJobs[i].pending = false;
			artWork = true;
		}
	}
	const bool needsResolver = !metadataReady || !lyricsReady;
	const bool needsArtDerivation = !artReady && !artWork;
	if ((needsResolver && !resolverWork) ||
			(!needsResolver && needsArtDerivation)) {
		SbEnrichmentJob *job = &r->jobs[SB_ENRICH_PRIORITY_CURRENT];
		memset (job, 0, sizeof (*job));
		job->pending = true;
		job->identity = *id;
		copyText (job->key, sizeof (job->key), key);
		job->generation = generation;
		job->queuedAtMs = SbPlatformMonotonicMs ();
	}
	pthread_cond_broadcast (&r->cond);
	pthread_mutex_unlock (&r->lock);
}

static bool sameWorkPendingLocked (const SbMetadataResolver *r,
		const char *key) {
	if (r->jobActive && strcmp (r->activeKey, key) == 0) return true;
	if (r->artJobActive && strcmp (r->activeArtKey, key) == 0) return true;
	for (size_t i = 0; i < SB_ENRICH_PRIORITY_COUNT; i++)
		if ((r->jobs[i].pending && strcmp (r->jobs[i].key, key) == 0) ||
				(r->artJobs[i].pending && strcmp (r->artJobs[i].key, key) == 0))
			return true;
	return false;
}

static bool higherPriorityWorkLocked (const SbMetadataResolver *r,
		const SbEnrichmentPriority priority) {
	if (r->jobActive && r->activePriority < priority) return true;
	if (r->artJobActive && r->activeArtPriority < priority) return true;
	for (size_t i = 0; i < (size_t) priority; i++)
		if (r->jobs[i].pending || r->artJobs[i].pending) return true;
	return false;
}

SbEnrichmentPrefetchSchedule SbMetadataResolverPrefetch (
		SbMetadataResolver *r, const SbTrackIdentity *id,
		const SbEnrichmentPriority priority) {
	if (priority != SB_ENRICH_PRIORITY_NEXT &&
			priority != SB_ENRICH_PRIORITY_NEXT2)
		return SB_ENRICH_PREFETCH_HIGHER_PRIORITY;
	char key[80]; trackWorkKey (id, key, sizeof (key));
	pthread_mutex_lock (&r->lock);
	SbEnrichmentPrefetchSchedule result = SB_ENRICH_PREFETCH_SCHEDULED;
	SbEnrichmentPrefetchEntry *entry = prefetchEntryLocked (r, key, id, false);
	const uint64_t now = SbPlatformMonotonicMs ();
	if (entry != NULL && entry->metadataDone && entry->lyricsDone &&
			entry->artDone && !(entry->metadataReady && entry->lyricsReady &&
			entry->artReady) && entry->finishedAtMs != 0 &&
			now >= entry->finishedAtMs && now - entry->finishedAtMs >= 60000) {
		/* Transient results suppress speculative retry storms, not future work.
		 * Reusable components remain warm and will be cache hits on this retry. */
		entry->metadataDone = entry->metadataReady;
		entry->lyricsDone = entry->lyricsReady;
		entry->artDone = entry->artReady;
		entry->startedAtMs = now;
		entry->finishedAtMs = 0;
	}
	if (r->stopping) result = SB_ENRICH_PREFETCH_STOPPING;
	else if (entry != NULL && entry->metadataReady && entry->lyricsReady &&
			entry->artReady) result = SB_ENRICH_PREFETCH_CACHE_HIT;
	else if (entry != NULL && entry->metadataDone && entry->lyricsDone &&
			entry->artDone) result = SB_ENRICH_PREFETCH_RECENT_ATTEMPT;
	else if (sameWorkPendingLocked (r, key)) result = SB_ENRICH_PREFETCH_IN_FLIGHT;
	else if (higherPriorityWorkLocked (r, priority))
		result = SB_ENRICH_PREFETCH_HIGHER_PRIORITY;
	else {
		entry = prefetchEntryLocked (r, key, id, true);
		SbEnrichmentJob *job = &r->jobs[priority];
		memset (job, 0, sizeof (*job));
		job->pending = true;
		job->identity = *id;
		copyText (job->key, sizeof (job->key), key);
		job->queuedAtMs = SbPlatformMonotonicMs ();
		pthread_cond_broadcast (&r->cond);
	}
	const bool logChanged = strcmp (r->loggedPrefetchKey[priority], key) != 0 ||
			r->loggedPrefetchDecision[priority] != (int) result;
	if (logChanged && result == SB_ENRICH_PREFETCH_SCHEDULED) {
		enrichmentDebugPrint ("enrichment prefetch schedule position=%s artist=\"%s\" title=\"%s\" prefetch start t=%llu\n",
				priorityName (priority), id->artist, id->title,
				(unsigned long long) entry->startedAtMs);
	} else if (logChanged) {
		const char *reason = result == SB_ENRICH_PREFETCH_CACHE_HIT ? "cache_hit" :
				result == SB_ENRICH_PREFETCH_IN_FLIGHT ? "in_flight" :
				result == SB_ENRICH_PREFETCH_RECENT_ATTEMPT ? "recent_attempt" :
				result == SB_ENRICH_PREFETCH_STOPPING ? "stopping" :
				"higher_priority";
		enrichmentDebugPrint ("enrichment prefetch skip position=%s reason=%s artist=\"%s\" title=\"%s\"\n",
				priorityName (priority), reason, id->artist, id->title);
	}
	copyText (r->loggedPrefetchKey[priority],
			sizeof (r->loggedPrefetchKey[priority]), key);
	r->loggedPrefetchDecision[priority] = (int) result;
	pthread_mutex_unlock (&r->lock);
	return result;
}

void SbMetadataResolverCancelPrefetch (SbMetadataResolver *r) {
	pthread_mutex_lock (&r->lock);
	for (size_t i = SB_ENRICH_PRIORITY_NEXT; i < SB_ENRICH_PRIORITY_COUNT; i++) {
		r->jobs[i].pending = false;
		r->artJobs[i].pending = false;
	}
	enrichmentDebugPrint ("enrichment prefetch cancel reason=station_change\n");
	pthread_mutex_unlock (&r->lock);
}
bool SbMetadataResolverPoll (SbMetadataResolver *r, uint64_t generation, SbMetadataResult *out) {
	bool ready = false; pthread_mutex_lock (&r->lock);
	if (r->resultReady) { ready = r->metadataResultGeneration == generation; if (ready) *out = r->completed; r->resultReady = false; }
	pthread_mutex_unlock (&r->lock); return ready;
}
bool SbLyricsResolverPoll (SbMetadataResolver *r, uint64_t generation, SbLyricsResult *out) {
	bool ready = false; pthread_mutex_lock (&r->lock);
	if (r->lyricsResultReady) {
		ready = r->lyricsResultGeneration == generation;
		if (ready) { SbLyricsResultInit (out); SbLyricsResultCopy (out, &r->completedLyrics); }
		r->lyricsResultReady = false;
	}
	pthread_mutex_unlock (&r->lock); return ready;
}
bool SbAlbumArtResolverPoll(SbMetadataResolver*r,uint64_t generation,SbAlbumArtResult*out){bool ready=false;pthread_mutex_lock(&r->lock);if(r->artResultReady){ready=r->artResultGeneration==generation;if(ready)*out=r->completedArt;r->artResultReady=false;}pthread_mutex_unlock(&r->lock);return ready;}
void SbMetadataResolverDestroy (SbMetadataResolver *r) {
	if (r->started || r->artStarted) { pthread_mutex_lock (&r->lock); r->stopping = true; pthread_cond_broadcast (&r->cond); pthread_mutex_unlock (&r->lock); if(r->started)pthread_join (r->thread, NULL); if(r->artStarted)pthread_join(r->artThread,NULL); }
	SbLyricsResultDestroy (&r->completedLyrics);
	for (size_t i = 0; i < SB_ENRICH_CACHE_MAX; i++)
		if (r->lyricsCache[i].used) SbLyricsResultDestroy (&r->lyricsCache[i].result);
	for (size_t i = 0; i < SB_ENRICH_PREFETCH_CACHE_MAX; i++)
		if (r->prefetch[i].used) SbLyricsResultDestroy (&r->prefetch[i].lyrics);
	SbPersistentCacheDestroy(&r->persistent); SbPersistentCacheDestroy(&r->artPersistent); free(r->artDirectory);
	pthread_cond_destroy (&r->cond); pthread_mutex_destroy (&r->lock);
}
