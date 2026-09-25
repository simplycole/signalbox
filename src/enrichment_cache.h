#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum { SB_CACHE_SCHEMA_VERSION = 5, SB_CACHE_MAX_ENTRIES = 4096 };
typedef enum { SB_CACHE_METADATA, SB_CACHE_LYRICS, SB_CACHE_ART } SbCacheKind;
typedef struct { char *provider, *key, *payload; int state; time_t fetched, used; } SbCacheEntry;
typedef struct { SbCacheEntry *entries; size_t count, capacity; char *path; } SbPersistentCache;

void SbPersistentCacheInit (SbPersistentCache *, char *ownedPath);
void SbPersistentCacheDestroy (SbPersistentCache *);
bool SbPersistentCacheLoad (SbPersistentCache *, time_t now);
const SbCacheEntry *SbPersistentCacheGet (SbPersistentCache *, const char *,
 const char *, SbCacheKind, time_t now);
bool SbPersistentCachePut (SbPersistentCache *, const char *, const char *, int,
 const char *, SbCacheKind, time_t now);
bool SbPersistentCacheWrite (SbPersistentCache *);
time_t SbCacheTtl (SbCacheKind, int state);
bool SbCacheStatePersistent (int state);
