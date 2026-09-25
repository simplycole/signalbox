#include "enrichment_cache.h"
#include "enrichment.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main (void) {
	char path[256];
	snprintf (path, sizeof (path), "/tmp/signalbox-cache-%ld.json",
			(long) getpid ());
	SbPersistentCache cache;
	SbPersistentCacheInit (&cache, strdup (path));
	const time_t now = 1700000000;
	assert (!SbCacheStatePersistent (SB_LOOKUP_ERROR));
	assert (SbCacheTtl (SB_CACHE_ART, SB_LOOKUP_NO_MATCH) <
			SbCacheTtl (SB_CACHE_ART, SB_LOOKUP_AVAILABLE));
	assert (SbPersistentCachePut (&cache, "musicbrainz", "k",
			SB_LOOKUP_AVAILABLE, "{\"title\":\"x\"}", SB_CACHE_METADATA, now));
	assert (SbPersistentCacheGet (&cache, "musicbrainz", "k",
			SB_CACHE_METADATA, now - 1));
	assert (!SbPersistentCachePut (&cache, "musicbrainz", "e",
			SB_LOOKUP_ERROR, "{}", SB_CACHE_METADATA, now));
	assert (SbPersistentCacheWrite (&cache));
	SbPersistentCacheDestroy (&cache);

	SbPersistentCacheInit (&cache, strdup (path));
	assert (SbPersistentCacheLoad (&cache, now + 1));
	assert (SbPersistentCacheGet (&cache, "musicbrainz", "k",
			SB_CACHE_METADATA, now + 1));
	assert (!SbPersistentCacheGet (&cache, "musicbrainz", "k",
			SB_CACHE_METADATA, now + 100 * 86400));
	SbPersistentCacheDestroy (&cache);

	FILE *file = fopen (path, "wb");
	fputs ("{\"schema_version\":999,\"entries\":[]}", file);
	fclose (file);
	SbPersistentCacheInit (&cache, strdup (path));
	assert (!SbPersistentCacheLoad (&cache, now));
	for (size_t i = 0; i < SB_CACHE_MAX_ENTRIES + 1; i++) {
		char key[32];
		snprintf (key, sizeof (key), "key-%zu", i);
		assert (SbPersistentCachePut (&cache, "lrclib", key,
				SB_LOOKUP_NO_MATCH, "{}", SB_CACHE_LYRICS, now + (time_t) i));
	}
	assert (cache.count == SB_CACHE_MAX_ENTRIES);
	assert (!SbPersistentCacheGet (&cache, "lrclib", "key-0",
			SB_CACHE_LYRICS, now + SB_CACHE_MAX_ENTRIES));
	SbPersistentCacheDestroy (&cache);

	file = fopen (path, "wb");
	fputs ("{\"schema_version\":5,\"entries\":[{\"provider\":\"lrclib\","
			"\"key\":\"bad\",\"payload\":\"{}\",\"state\":3}]}", file);
	fclose (file);
	SbPersistentCacheInit (&cache, strdup (path));
	assert (SbPersistentCacheLoad (&cache, now));
	assert (cache.count == 0);
	SbPersistentCacheDestroy (&cache);

	/* The previous rich-metadata schema expires cleanly so cached category text
	 * cannot retain the old ambiguous Genres label. */
	file = fopen (path, "wb");
	fputs ("{\"schema_version\":4,\"entries\":[]}", file);
	fclose (file);
	SbPersistentCacheInit (&cache, strdup (path));
	assert (!SbPersistentCacheLoad (&cache, now));
	assert (cache.count == 0);
	SbPersistentCacheDestroy (&cache);

	file = fopen (path, "wb");
	fputs ("broken", file);
	fclose (file);
	SbPersistentCacheInit (&cache, strdup (path));
	assert (!SbPersistentCacheLoad (&cache, now));
	SbPersistentCacheDestroy (&cache);
	remove (path);
	puts ("enrichment cache tests passed");
	return 0;
}
