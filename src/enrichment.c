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

void SbTrackCacheKey (const char *provider, const SbTrackIdentity *id,
		char *out, size_t size) {
	char artist[SB_ENRICH_TEXT_MAX], title[SB_ENRICH_TEXT_MAX];
	char album[SB_ENRICH_TEXT_MAX], joined[900];
	SbTrackNormalize (id->artist, artist, sizeof (artist));
	SbTrackNormalize (id->title, title, sizeof (title));
	SbTrackNormalize (id->album, album, sizeof (album));
	snprintf (joined, sizeof (joined), "%s|%s|%s|%s|%u", provider, artist,
			title, album, id->duration);
	uint64_t hash = UINT64_C (1469598103934665603);
	for (const unsigned char *p = (unsigned char *) joined; *p; p++)
		hash = (hash ^ *p) * UINT64_C (1099511628211);
	snprintf (out, size, "%s:%016llx", provider, (unsigned long long) hash);
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

static char *metadataSerialize(const SbMetadataResult*r){json_object*o=json_object_new_object();json_object_object_add(o,"status",json_object_new_int(r->status));json_object_object_add(o,"artist",json_object_new_string(r->artist));json_object_object_add(o,"title",json_object_new_string(r->title));json_object_object_add(o,"release",json_object_new_string(r->release));json_object_object_add(o,"release_date",json_object_new_string(r->releaseDate));json_object_object_add(o,"artist_id",json_object_new_string(r->artistId));json_object_object_add(o,"recording_id",json_object_new_string(r->recordingId));json_object_object_add(o,"release_id",json_object_new_string(r->releaseId));json_object_object_add(o,"release_group_id",json_object_new_string(r->releaseGroupId));json_object_object_add(o,"confidence",json_object_new_double(r->confidence));char*s=strdup(json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN));json_object_put(o);return s;}
static bool metadataDeserialize(const char*s,SbMetadataResult*r){json_object*o=json_tokener_parse(s),*v=NULL;if(!o)return false;SbMetadataResultInit(r);copyText(r->provider,sizeof(r->provider),"MusicBrainz");json_object_object_get_ex(o,"status",&v);r->status=(SbLookupStatus)json_object_get_int(v);copyText(r->artist,sizeof(r->artist),jsonString(o,"artist"));copyText(r->title,sizeof(r->title),jsonString(o,"title"));copyText(r->release,sizeof(r->release),jsonString(o,"release"));copyText(r->releaseDate,sizeof(r->releaseDate),jsonString(o,"release_date"));copyText(r->artistId,sizeof(r->artistId),jsonString(o,"artist_id"));copyText(r->recordingId,sizeof(r->recordingId),jsonString(o,"recording_id"));copyText(r->releaseId,sizeof(r->releaseId),jsonString(o,"release_id"));copyText(r->releaseGroupId,sizeof(r->releaseGroupId),jsonString(o,"release_group_id"));if(json_object_object_get_ex(o,"confidence",&v))r->confidence=json_object_get_double(v);json_object_put(o);return SbCacheStatePersistent(r->status);}
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

static void albumFamilyNormalize (const char *input, char *out, size_t size) {
	SbTrackNormalize (input, out, size);
	/* Edition language is intentionally stripped only as a trailing qualifier.
	 * This keeps unrelated titles from becoming a family match. */
	const char *markers[] = {" anniversary", " deluxe", " remaster", " remastered",
			" expanded", " special edition", " deluxe edition", " explicit"};
	for (size_t i = 0; i < sizeof (markers) / sizeof (*markers); i++) {
		char *at = strstr (out, markers[i]);
		if (at != NULL) {
			*at = '\0'; while (at > out && at[-1] == ' ') *--at = '\0';
			if (strcmp (markers[i], " anniversary") == 0) {
				char *word = strrchr (out, ' '); const char *p = word ? word + 1 : out;
				bool ordinal = isdigit ((unsigned char) *p);
				for (; ordinal && *p; p++) ordinal = isdigit ((unsigned char) *p) ||
						strchr ("stndrh", tolower ((unsigned char) *p)) != NULL;
				if (ordinal) { if (word) *word = '\0'; else out[0] = '\0'; }
			}
		}
	}
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
	copyText (result->release, sizeof (result->release), jsonString (best, "title"));
	copyText (result->releaseDate, sizeof (result->releaseDate), jsonString (best, "date"));
	copyText (result->releaseId, sizeof (result->releaseId), jsonString (best, "id"));
	json_object *group = NULL; if (json_object_object_get_ex (best, "release-group", &group))
		copyText (result->releaseGroupId, sizeof (result->releaseGroupId), jsonString (group, "id"));
	enrichmentDebugPrint ("art selection source=%s release_mbid=%s release_group_mbid=%s album_agreement=%.2f score=%.1f\n",
			source, result->releaseId, result->releaseGroupId, bestAlbum, bestScore);
	return true;
}

bool SbMusicBrainzSelectArtRelease (const char *json, const SbTrackIdentity *identity,
		SbMetadataResult *result) {
	json_object *root = json_tokener_parse (json), *releases = NULL;
	if (root == NULL) return false;
	if (json_object_is_type (root, json_type_array)) releases = root;
	else json_object_object_get_ex (root, "releases", &releases);
	bool selected = selectReleaseArray (releases, identity, result, "album_lookup");
	json_object_put (root); return selected;
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
	if (curlCode != CURLE_OK) return SB_LOOKUP_ERROR;
	return status == 503 ? SB_LOOKUP_UNAVAILABLE :
			status == 200 ? SB_LOOKUP_LOADING : SB_LOOKUP_ERROR;
}

bool SbMusicBrainzShouldRetry (const long status, const int curlCode,
		const unsigned int attempt) {
	return curlCode == CURLE_OK && status == 503 && attempt < 2;
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

static bool musicBrainzLookup (const SbTrackIdentity *id,
		SbMetadataResult *result, void *unused) {
	(void) unused; CURL *curl = curl_easy_init (); SbHttpBuffer body = {NULL, 0};
	CURLcode code = CURLE_FAILED_INIT; long status = 0;
	SbMusicBrainzHeaders headers = {0}; int attempts = 0;
	SbMetadataResultInit (result); copyText (result->provider, sizeof (result->provider), "MusicBrainz");
	if (curl == NULL) goto failed;
	char query[600]; snprintf (query, sizeof (query), "recording:\"%s\" AND artist:\"%s\"", id->title, id->artist);
	char *escaped = curl_easy_escape (curl, query, 0); char url[1400];
	if (escaped == NULL) goto failed;
	snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/recording/?query=%s&inc=artist-credits+releases+release-groups&fmt=json&limit=8", escaped);
	curl_free (escaped); curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_USERAGENT,
			PROGRAM_NAME "/" VERSION " (https://github.com/signalbox-player/signalbox)");
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, httpWrite); curl_easy_setopt (curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, musicBrainzHeader);
	curl_easy_setopt (curl, CURLOPT_HEADERDATA, &headers);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 12L); curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
	for (int attempt = 1; attempt <= 2; attempt++) {
		attempts = attempt; headers.retryAfter = 0; status = 0;
		code = curl_easy_perform (curl);
		curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &status);
		enrichmentDebugPrint ("enrichment provider=musicbrainz artist=\"%s\" title=\"%s\" endpoint=/ws/2/recording attempt=%d http=%ld network_error=\"%s\" retry_after=%ld bytes=%zu\n",
				id->artist, id->title, attempt, status,
				code == CURLE_OK ? "none" : curl_easy_strerror (code),
				headers.retryAfter, body.length);
		if (!SbMusicBrainzShouldRetry (status, code,
				(unsigned int) attempt)) break;
		/* One bounded retry, observing both the shared one-second provider
		 * spacing and a reasonable numeric Retry-After response. */
		free (body.data); body = (SbHttpBuffer) {NULL, 0};
		long delay = headers.retryAfter > 0 ? headers.retryAfter : 1;
		if (delay > 5) delay = 5;
		SbPlatformSleepMs ((unsigned int) delay * 1000);
	}
	curl_easy_cleanup (curl);
	if (code != CURLE_OK || status != 200 || body.data == NULL) goto failed_no_curl;
	bool ok = SbMusicBrainzParse (body.data, id, result);
	enrichmentDebugPrint ("enrichment provider=musicbrainz parse=%s state=%d\n",
			ok || result->status == SB_LOOKUP_NO_MATCH ? "ok" : "error",
			(int) result->status);
	enrichmentDebugPrint ("enrichment provider=musicbrainz final_state=%d attempts=%d\n",
			(int) result->status, attempts);
	free (body.data); return ok;
failed:
	if (curl != NULL) curl_easy_cleanup (curl);
failed_no_curl:
	free (body.data); result->status = SbMusicBrainzHttpStatus (status, code);
	copyText (result->provider, sizeof (result->provider), "MusicBrainz");
	if (code != CURLE_OK) snprintf (result->error, sizeof (result->error),
			"curl %d: %s", (int) code, curl_easy_strerror (code));
	else snprintf (result->error, sizeof (result->error),
			"HTTP %ld", status);
	enrichmentDebugPrint ("enrichment provider=musicbrainz final_state=%d attempts=%d detail=\"%s\"\n",
			(int) result->status, attempts, result->error);
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
		void *unused) {
	(void) unused; SbLrclibLookupTrace trace = {0};
	enrichmentDebugPrint ("lyrics lookup provider=lrclib original_artist=\"%s\" original_title=\"%s\" original_album=\"%s\" original_duration=%u\n",
			id->artist, id->title, id->album, id->duration);
	bool found = lrclibRequest (id, id->artist, id->title, true, "exact",
			result, &trace);
	char title[SB_ENRICH_TEXT_MAX]; const bool cleanedTitle =
			SbLyricsFallbackTitle (id->title, title, sizeof (title));
	char artist[SB_ENRICH_TEXT_MAX]; const bool baseArtist =
			SbLyricsFallbackArtist (id->artist, artist, sizeof (artist));
	if (!found && result->status == SB_LOOKUP_NO_MATCH) {
		if (cleanedTitle) {
			SbPlatformSleepMs (300);
			found = lrclibRequest (id, id->artist, title, true,
					"clean-title", result, &trace);
		}
	}
	if (!found && result->status == SB_LOOKUP_NO_MATCH) {
		SbPlatformSleepMs (300);
		found = lrclibRequest (id, baseArtist ? artist : id->artist,
				cleanedTitle ? title : id->title, false,
				baseArtist ? "base-artist-unconstrained" : "unconstrained",
				result, &trace);
	}
	if (!found && result->status == SB_LOOKUP_NO_MATCH) {
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
	CURL *curl = curl_easy_init (); long status = 0; CURLcode code = CURLE_FAILED_INIT;
	if (curl == NULL) { *failure = SB_LOOKUP_ERROR; return false; }
	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_USERAGENT,
			PROGRAM_NAME "/" VERSION " (https://github.com/signalbox-player/signalbox)");
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, httpWrite);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, body);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 12L); curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
	code = curl_easy_perform (curl); curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup (curl);
	enrichmentDebugPrint ("art fallback provider=musicbrainz http=%ld network_error=\"%s\" bytes=%zu\n",
			status, code == CURLE_OK ? "none" : curl_easy_strerror (code), body->length);
	if (code == CURLE_OK && status == 200 && body->data != NULL) return true;
	*failure = status == 503 || status == 429 ? SB_LOOKUP_UNAVAILABLE : SB_LOOKUP_ERROR;
	return false;
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

static void *artResolverThread(void *arg) {
	SbMetadataResolver *r=arg;
	for (;;) {
		pthread_mutex_lock(&r->lock);
		while(!r->artPending&&!r->stopping) pthread_cond_wait(&r->cond,&r->lock);
		if(r->stopping){pthread_mutex_unlock(&r->lock);break;}
		char release[SB_ENRICH_ID_MAX], group[SB_ENRICH_ID_MAX], recording[SB_ENRICH_ID_MAX];
		copyText(release,sizeof(release),r->pendingArtReleaseId);
		copyText(group,sizeof(group),r->pendingArtReleaseGroupId);
		copyText(recording,sizeof(recording),r->pendingArtRecordingId);
		SbTrackIdentity identity = r->pendingArtIdentity;
		uint64_t generation=r->pendingArtGeneration; r->artPending=false;
		pthread_mutex_unlock(&r->lock);
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
		if (!found && art.status != SB_LOOKUP_UNAVAILABLE && art.status != SB_LOOKUP_ERROR && group[0]) {
			SbPlatformSleepMs (1000); char url[512]; SbHttpBuffer body = {0};
			snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/release?release-group=%s&inc=artist-credits+release-groups&fmt=json&limit=25", group);
			enrichmentDebugPrint ("art fallback step=release_group_lookup release_group_mbid=%s\n", group);
			if (musicBrainzArtFetch (url, &body, &mbFailure)) found = rankedReleaseArt (r,
					body.data, &identity, release, "alternate_edition", &art);
			free (body.data);
		}
		if (!found && art.status != SB_LOOKUP_UNAVAILABLE && art.status != SB_LOOKUP_ERROR &&
				identity.album[0] && identity.artist[0]) {
			SbPlatformSleepMs (1000); CURL *curl = curl_easy_init (); SbHttpBuffer body = {0};
			if (curl != NULL) { char query[700], url[1600]; snprintf (query, sizeof (query),
					"release:\"%s\" AND artist:\"%s\"", identity.album, identity.artist);
				char *escaped = curl_easy_escape (curl, query, 0); curl_easy_cleanup (curl);
				if (escaped != NULL) { snprintf (url, sizeof (url), "https://musicbrainz.org/ws/2/release/?query=%s&inc=artist-credits+release-groups&fmt=json&limit=12", escaped); curl_free (escaped);
					enrichmentDebugPrint ("art fallback step=bounded_album_lookup artist=\"%s\" album=\"%s\"\n", identity.artist, identity.album);
					if (musicBrainzArtFetch (url, &body, &mbFailure)) found = rankedReleaseArt (r,
							body.data, &identity, NULL, "album_release", &art); }
			}
			free (body.data);
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
		pthread_mutex_lock(&r->lock); r->completedArt=art;
		r->artResultGeneration=generation; r->artResultReady=true;
		pthread_mutex_unlock(&r->lock);
	}
	return NULL;
}

static void *resolverThread (void *arg) {
	SbMetadataResolver *r = arg; time_t lastMetadataRequest = 0, lastLyricsRequest = 0;
	for (;;) {
		pthread_mutex_lock (&r->lock);
		while (!r->pending && !r->stopping) pthread_cond_wait (&r->cond, &r->lock);
		if (r->stopping) { pthread_mutex_unlock (&r->lock); break; }
		SbTrackIdentity id = r->pendingIdentity; uint64_t generation = r->pendingGeneration;
		r->pending = false; char key[80]; SbTrackCacheKey (r->provider.name, &id, key, sizeof (key));
		SbMetadataResult result; bool cached = false;
		for (size_t i = 0; i < SB_ENRICH_CACHE_MAX; i++) if (r->cache[i].used && strcmp (key, r->cache[i].key) == 0) {
			result = r->cache[i].result; cached = true; break;
		}
		if (!cached) { const SbCacheEntry *e=SbPersistentCacheGet(&r->persistent,r->provider.name,key,SB_CACHE_METADATA,time(NULL)); if(e) cached=metadataDeserialize(e->payload,&result); }
		char lyricsKey[80]; SbTrackCacheKey (r->lyricsProvider.name, &id,
				lyricsKey, sizeof (lyricsKey));
		SbLyricsResult lyrics; SbLyricsResultInit (&lyrics); bool lyricsCached = false;
		for (size_t i = 0; i < SB_ENRICH_CACHE_MAX; i++)
			if (r->lyricsCache[i].used && strcmp (lyricsKey, r->lyricsCache[i].key) == 0) {
				SbLyricsResultCopy (&lyrics, &r->lyricsCache[i].result);
				lyricsCached = true; break;
			}
		if (!lyricsCached) { const SbCacheEntry *e=SbPersistentCacheGet(&r->persistent,r->lyricsProvider.name,lyricsKey,SB_CACHE_LYRICS,time(NULL)); if(e) lyricsCached=lyricsDeserialize(e->payload,&lyrics); }
		pthread_mutex_unlock (&r->lock);
		/* Metadata goes first: LRCLIB retries and Retry-After pauses must never
		 * starve MusicBrainz on the shared, playback-independent worker. */
		if (!cached) {
			time_t now = time (NULL);
			if (lastMetadataRequest != 0 && now <= lastMetadataRequest) SbPlatformSleepMs (1000);
			r->provider.lookup (&id, &result, r->provider.data); lastMetadataRequest = time (NULL);
		}
		pthread_mutex_lock (&r->lock);
		if (!cached && (result.status == SB_LOOKUP_AVAILABLE ||
				result.status == SB_LOOKUP_NO_MATCH)) {
			SbEnrichmentCacheEntry *e = &r->cache[r->cacheNext++ % SB_ENRICH_CACHE_MAX];
			e->used = true; copyText (e->key, sizeof (e->key), key); e->result = result;
			char *payload=metadataSerialize(&result); if(payload){SbPersistentCachePut(&r->persistent,r->provider.name,key,result.status,payload,SB_CACHE_METADATA,time(NULL));free(payload);SbPersistentCacheWrite(&r->persistent);}
		}
		r->completed = result; r->metadataResultGeneration = generation;
		r->resultReady = true;
		pthread_mutex_unlock (&r->lock);
		pthread_mutex_lock(&r->lock);
		copyText(r->pendingArtReleaseId,sizeof(r->pendingArtReleaseId),
				result.status==SB_LOOKUP_AVAILABLE?result.releaseId:"");
		copyText(r->pendingArtReleaseGroupId,sizeof(r->pendingArtReleaseGroupId),
				result.status==SB_LOOKUP_AVAILABLE?result.releaseGroupId:"");
		copyText(r->pendingArtRecordingId,sizeof(r->pendingArtRecordingId),
				result.status==SB_LOOKUP_AVAILABLE?result.recordingId:"");
		r->pendingArtIdentity = id;
		enrichmentDebugPrint("art identity generation=%llu artist=\"%s\" title=\"%s\" album=\"%s\" release_mbid=%s release_group_mbid=%s\n",(unsigned long long)generation,id.artist,id.title,id.album,result.releaseId,result.releaseGroupId);
		r->pendingArtGeneration=generation; r->artPending=true;
		pthread_cond_broadcast(&r->cond); pthread_mutex_unlock(&r->lock);
		if (!lyricsCached) {
			time_t now = time (NULL);
			if (lastLyricsRequest != 0 && now <= lastLyricsRequest) SbPlatformSleepMs (1000);
			r->lyricsProvider.lookup (&id, &lyrics, r->lyricsProvider.data);
			lastLyricsRequest = time (NULL);
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
		SbLyricsResultCopy (&r->completedLyrics, &lyrics);
		r->lyricsResultGeneration = generation; r->lyricsResultReady = true;
		pthread_mutex_unlock (&r->lock); SbLyricsResultDestroy (&lyrics);
	}
	return NULL;
}

void SbMetadataResolverInit (SbMetadataResolver *r) {
	memset (r, 0, sizeof (*r)); pthread_mutex_init (&r->lock, NULL); pthread_cond_init (&r->cond, NULL);
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
	pthread_mutex_lock (&r->lock); r->pendingIdentity = *id; r->pendingGeneration = generation;
	r->pending = true; pthread_cond_broadcast (&r->cond); pthread_mutex_unlock (&r->lock);
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
	SbPersistentCacheDestroy(&r->persistent); SbPersistentCacheDestroy(&r->artPersistent); free(r->artDirectory);
	pthread_cond_destroy (&r->cond); pthread_mutex_destroy (&r->lock);
}
