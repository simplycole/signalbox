#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

enum { SB_ENRICH_TEXT_MAX = 256, SB_ENRICH_ID_MAX = 64,
	SB_ENRICH_ERROR_MAX = 160, SB_ENRICH_CACHE_MAX = 32 };

typedef struct {
	char artist[SB_ENRICH_TEXT_MAX];
	char title[SB_ENRICH_TEXT_MAX];
	char album[SB_ENRICH_TEXT_MAX];
	char station[SB_ENRICH_TEXT_MAX];
	unsigned int duration;
} SbTrackIdentity;

typedef enum {
	SB_LOOKUP_IDLE = 0, SB_LOOKUP_LOADING, SB_LOOKUP_AVAILABLE,
	SB_LOOKUP_INSTRUMENTAL, SB_LOOKUP_NO_MATCH, SB_LOOKUP_UNAVAILABLE,
	SB_LOOKUP_ERROR
} SbLookupStatus;

#include "enrichment_cache.h"
#include "album_art.h"

SbLookupStatus SbMusicBrainzHttpStatus (long, int);
bool SbMusicBrainzShouldRetry (long, int, unsigned int);

typedef struct {
	SbLookupStatus status;
	char artist[SB_ENRICH_TEXT_MAX], title[SB_ENRICH_TEXT_MAX];
	char release[SB_ENRICH_TEXT_MAX], releaseDate[32];
	char artistId[SB_ENRICH_ID_MAX], recordingId[SB_ENRICH_ID_MAX];
	char releaseId[SB_ENRICH_ID_MAX], releaseGroupId[SB_ENRICH_ID_MAX], provider[32];
	double confidence;
	char error[SB_ENRICH_ERROR_MAX];
} SbMetadataResult;

typedef struct {
	SbLookupStatus status;
	char provider[32], artist[SB_ENRICH_TEXT_MAX], title[SB_ENRICH_TEXT_MAX];
	char album[SB_ENRICH_TEXT_MAX], recordId[SB_ENRICH_ID_MAX];
	bool instrumental;
	char *plainLyrics, *syncedLyrics;
	char error[SB_ENRICH_ERROR_MAX];
} SbLyricsResult;

typedef bool (*SbMetadataProviderLookup) (const SbTrackIdentity *,
		SbMetadataResult *, void *);
typedef struct { const char *name; SbMetadataProviderLookup lookup; void *data; }
	SbMetadataProvider;
typedef bool (*SbLyricsProviderLookup) (const SbTrackIdentity *,
		SbLyricsResult *, void *);
typedef struct { const char *name; SbLyricsProviderLookup lookup; void *data; }
	SbLyricsProvider;

typedef struct {
	char key[80];
	SbMetadataResult result;
	bool used;
} SbEnrichmentCacheEntry;

typedef struct {
	char key[80];
	SbLyricsResult result;
	bool used;
} SbLyricsCacheEntry;

typedef struct {
	pthread_t thread;
	pthread_t artThread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, artStarted, stopping, pending, artPending, resultReady, lyricsResultReady;
	bool artResultReady;
	uint64_t pendingGeneration, metadataResultGeneration, lyricsResultGeneration;
	uint64_t artResultGeneration;
	SbTrackIdentity pendingIdentity;
	char pendingArtReleaseId[SB_ENRICH_ID_MAX];
	char pendingArtReleaseGroupId[SB_ENRICH_ID_MAX];
	char pendingArtRecordingId[SB_ENRICH_ID_MAX];
	SbTrackIdentity pendingArtIdentity;
	uint64_t pendingArtGeneration;
	SbMetadataResult completed;
	SbLyricsResult completedLyrics;
	SbAlbumArtResult completedArt;
	SbMetadataProvider provider;
	SbLyricsProvider lyricsProvider;
	SbEnrichmentCacheEntry cache[SB_ENRICH_CACHE_MAX];
	SbLyricsCacheEntry lyricsCache[SB_ENRICH_CACHE_MAX];
	size_t cacheNext, lyricsCacheNext;
	SbPersistentCache persistent;
	SbPersistentCache artPersistent;
	char *artDirectory;
} SbMetadataResolver;

void SbTrackIdentitySet (SbTrackIdentity *, const char *, const char *,
		const char *, const char *, unsigned int);
void SbTrackNormalize (const char *, char *, size_t);
void SbTrackCacheKey (const char *, const SbTrackIdentity *, char *, size_t);
void SbMetadataResultInit (SbMetadataResult *);
void SbLyricsResultInit (SbLyricsResult *);
void SbLyricsResultDestroy (SbLyricsResult *);
bool SbLyricsResultCopy (SbLyricsResult *, const SbLyricsResult *);
bool SbLrclibParse (const char *, SbLyricsResult *);
bool SbLrclibSearchParse (const char *, const SbTrackIdentity *,
		SbLyricsResult *, double *);
bool SbLyricsDisplayText (const SbLyricsResult *, char **);
bool SbLyricsFallbackTitle (const char *, char *, size_t);
bool SbLyricsFallbackArtist (const char *, char *, size_t);
bool SbMusicBrainzParse (const char *, const SbTrackIdentity *,
		SbMetadataResult *);
bool SbMusicBrainzSelectArtRelease (const char *, const SbTrackIdentity *,
		SbMetadataResult *);
bool SbMusicBrainzFormatDate (const char *, char *, size_t);
void SbMetadataResolverInit (SbMetadataResolver *);
bool SbMetadataResolverStart (SbMetadataResolver *);
void SbMetadataResolverRequest (SbMetadataResolver *, const SbTrackIdentity *,
		uint64_t);
bool SbMetadataResolverPoll (SbMetadataResolver *, uint64_t,
		SbMetadataResult *);
bool SbLyricsResolverPoll (SbMetadataResolver *, uint64_t, SbLyricsResult *);
bool SbAlbumArtResolverPoll (SbMetadataResolver *, uint64_t, SbAlbumArtResult *);
void SbMetadataResolverDestroy (SbMetadataResolver *);
