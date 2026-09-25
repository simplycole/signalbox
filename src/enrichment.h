#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

enum { SB_ENRICH_TEXT_MAX = 256, SB_ENRICH_GENRES_MAX = 512,
	SB_ENRICH_ID_MAX = 64,
	SB_ENRICH_ERROR_MAX = 160, SB_ENRICH_CACHE_MAX = 32,
	SB_ENRICH_PREFETCH_CACHE_MAX = 8 };

typedef struct {
	char artist[SB_ENRICH_TEXT_MAX];
	char title[SB_ENRICH_TEXT_MAX];
	char album[SB_ENRICH_TEXT_MAX];
	char station[SB_ENRICH_TEXT_MAX];
	char pandoraId[SB_ENRICH_TEXT_MAX];
	unsigned int duration;
} SbTrackIdentity;

typedef enum {
	SB_LOOKUP_IDLE = 0, SB_LOOKUP_LOADING, SB_LOOKUP_AVAILABLE,
	SB_LOOKUP_INSTRUMENTAL, SB_LOOKUP_NO_MATCH, SB_LOOKUP_UNAVAILABLE,
	SB_LOOKUP_ERROR
} SbLookupStatus;

typedef enum {
	SB_METADATA_CATEGORIES_NONE = 0,
	SB_METADATA_CATEGORIES_GENRES,
	SB_METADATA_CATEGORIES_TAGS,
} SbMetadataCategorySource;

typedef enum {
	SB_METADATA_COMPLETION_NONE = 0,
	SB_METADATA_COMPLETION_RELEASE,
	SB_METADATA_COMPLETION_RELEASE_GROUP,
} SbMetadataCompletionStep;

#include "enrichment_cache.h"
#include "album_art.h"

SbLookupStatus SbMusicBrainzHttpStatus (long, int);
bool SbMusicBrainzTransientFailure (long, int);
bool SbMusicBrainzShouldRetry (long, int, unsigned int);
unsigned int SbMusicBrainzRetryDelayMs (long);
unsigned int SbMusicBrainzRateDelayMs (uint64_t, uint64_t);
unsigned int SbMusicBrainzRequestBudget (void);
bool SbLrclibTransientFailure (long, int);
bool SbLrclibShouldRetry (long, int, unsigned int);
unsigned int SbLrclibRetryDelayMs (long);

typedef struct {
	SbLookupStatus status;
	char artist[SB_ENRICH_TEXT_MAX], title[SB_ENRICH_TEXT_MAX];
	char release[SB_ENRICH_TEXT_MAX], releaseDate[32], firstReleaseDate[32];
	char releaseType[96], releaseCountry[16];
	char label[SB_ENRICH_TEXT_MAX], catalogNumber[128], isrc[32];
	char genres[SB_ENRICH_GENRES_MAX];
	char artistId[SB_ENRICH_ID_MAX], recordingId[SB_ENRICH_ID_MAX];
	char releaseId[SB_ENRICH_ID_MAX], releaseGroupId[SB_ENRICH_ID_MAX], provider[32];
	double confidence;
	SbMetadataCategorySource categorySource;
	char error[SB_ENRICH_ERROR_MAX];
} SbMetadataResult;

typedef struct {
	SbLookupStatus status;
	char provider[32], artist[SB_ENRICH_TEXT_MAX], title[SB_ENRICH_TEXT_MAX];
	char album[SB_ENRICH_TEXT_MAX], recordId[SB_ENRICH_ID_MAX];
	double duration;
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

typedef enum {
	SB_ENRICH_PRIORITY_CURRENT = 0,
	SB_ENRICH_PRIORITY_NEXT,
	SB_ENRICH_PRIORITY_NEXT2,
	SB_ENRICH_PRIORITY_COUNT,
} SbEnrichmentPriority;

typedef enum {
	SB_ENRICH_PREFETCH_SCHEDULED = 0,
	SB_ENRICH_PREFETCH_CACHE_HIT,
	SB_ENRICH_PREFETCH_IN_FLIGHT,
	SB_ENRICH_PREFETCH_HIGHER_PRIORITY,
	SB_ENRICH_PREFETCH_RECENT_ATTEMPT,
	SB_ENRICH_PREFETCH_STOPPING,
} SbEnrichmentPrefetchSchedule;

typedef struct {
	bool pending;
	SbTrackIdentity identity;
	char key[80];
	uint64_t generation, queuedAtMs;
} SbEnrichmentJob;

typedef struct {
	bool pending;
	SbEnrichmentPriority priority;
	SbTrackIdentity identity;
	char key[80];
	char releaseId[SB_ENRICH_ID_MAX];
	char releaseGroupId[SB_ENRICH_ID_MAX];
	char recordingId[SB_ENRICH_ID_MAX];
	uint64_t generation, queuedAtMs;
} SbEnrichmentArtJob;

typedef struct {
	bool used, metadataDone, lyricsDone, artDone;
	bool metadataReady, lyricsReady, artReady;
	char key[80];
	SbTrackIdentity identity;
	uint64_t startedAtMs, finishedAtMs;
	SbMetadataResult metadata;
	SbLyricsResult lyrics;
	SbAlbumArtResult art;
} SbEnrichmentPrefetchEntry;

typedef struct {
	pthread_t thread;
	pthread_t artThread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, artStarted, stopping, resultReady, lyricsResultReady;
	bool artResultReady;
	uint64_t metadataResultGeneration, lyricsResultGeneration;
	uint64_t artResultGeneration;
	SbEnrichmentJob jobs[SB_ENRICH_PRIORITY_COUNT];
	SbEnrichmentArtJob artJobs[SB_ENRICH_PRIORITY_COUNT];
	bool jobActive, artJobActive;
	SbEnrichmentPriority activePriority, activeArtPriority;
	char activeKey[80], activeArtKey[80];
	char currentKey[80];
	uint64_t currentGeneration;
	SbMetadataResult completed;
	SbLyricsResult completedLyrics;
	SbAlbumArtResult completedArt;
	SbMetadataProvider provider;
	SbLyricsProvider lyricsProvider;
	SbEnrichmentCacheEntry cache[SB_ENRICH_CACHE_MAX];
	SbLyricsCacheEntry lyricsCache[SB_ENRICH_CACHE_MAX];
	size_t cacheNext, lyricsCacheNext;
	SbEnrichmentPrefetchEntry prefetch[SB_ENRICH_PREFETCH_CACHE_MAX];
	size_t prefetchNext;
	char loggedPrefetchKey[SB_ENRICH_PRIORITY_COUNT][80];
	int loggedPrefetchDecision[SB_ENRICH_PRIORITY_COUNT];
	SbPersistentCache persistent;
	SbPersistentCache artPersistent;
	char *artDirectory;
} SbMetadataResolver;

void SbTrackIdentitySet (SbTrackIdentity *, const char *, const char *,
		const char *, const char *, unsigned int);
void SbTrackIdentitySetPandoraId (SbTrackIdentity *, const char *);
void SbTrackNormalize (const char *, char *, size_t);
void SbTrackCacheKey (const char *, const SbTrackIdentity *, char *, size_t);
void SbTrackWorkKey (const SbTrackIdentity *, char *, size_t);
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
bool SbMusicBrainzApplyReleaseDetail (const char *, SbMetadataResult *);
bool SbMusicBrainzApplyReleaseGroupDetail (const char *, SbMetadataResult *);
bool SbMusicBrainzSelectRelease (const char *, const SbTrackIdentity *,
		SbMetadataResult *);
bool SbMusicBrainzAlbumFamilyTitle (const char *, char *, size_t);
SbMetadataCompletionStep SbMusicBrainzNextCompletion (
		const SbMetadataResult *, const char *, const char *);
bool SbMusicBrainzSelectArtRelease (const char *, const SbTrackIdentity *,
		SbMetadataResult *);
bool SbMusicBrainzFormatDate (const char *, char *, size_t);
bool SbMetadataValidateDates (SbMetadataResult *);
bool SbMetadataFormatAvailableFields (const SbMetadataResult *, char *, size_t);
char *SbMetadataSerialize (const SbMetadataResult *);
bool SbMetadataDeserialize (const char *, SbMetadataResult *);
void SbMetadataResolverInit (SbMetadataResolver *);
bool SbMetadataResolverStart (SbMetadataResolver *);
void SbMetadataResolverRequest (SbMetadataResolver *, const SbTrackIdentity *,
		uint64_t);
SbEnrichmentPrefetchSchedule SbMetadataResolverPrefetch (
		SbMetadataResolver *, const SbTrackIdentity *, SbEnrichmentPriority);
void SbMetadataResolverCancelPrefetch (SbMetadataResolver *);
bool SbMetadataResolverPoll (SbMetadataResolver *, uint64_t,
		SbMetadataResult *);
void SbMetadataResolverPublishProgress (SbMetadataResolver *, uint64_t,
		const SbMetadataResult *);
bool SbLyricsResolverPoll (SbMetadataResolver *, uint64_t, SbLyricsResult *);
bool SbAlbumArtResolverPoll (SbMetadataResolver *, uint64_t, SbAlbumArtResult *);
void SbMetadataResolverDestroy (SbMetadataResolver *);
