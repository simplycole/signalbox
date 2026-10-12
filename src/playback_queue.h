#pragma once

#include <stdint.h>
#include "playlist_prefetch.h"

/* Pandora documents radio URLs lasting up to one hour in its current API.
 * The legacy JSON API supplies opaque URLs without a documented expiry field.
 * Refresh at 45 minutes, leaving 15 minutes of margin; this is a conservative
 * client policy, not a claim about the legacy server's exact TTL. */
#define SB_PLAYBACK_URL_MAX_AGE_MS (45ULL * 60 * 1000)

static inline uint64_t SbPlaybackAgeMs (uint64_t fetched, uint64_t now) {
	return now >= fetched ? now - fetched : 0;
}

static inline bool SbPlaybackUrlStale (const PianoSong_t *song, uint64_t now) {
	return song != NULL && (song->audioUrlFetchedAtMs == 0 ||
			now < song->audioUrlFetchedAtMs || SbPlaybackAgeMs (
			song->audioUrlFetchedAtMs, now) >= SB_PLAYBACK_URL_MAX_AGE_MS);
}

static inline void SbPlaybackStamp (PianoSong_t *song, uint64_t fetched) {
	PianoListForeachP (song) song->audioUrlFetchedAtMs = fetched;
}

/* Only future playback objects are destroyed. The open player's URL and all
 * enrichment resolver/cache objects remain owned by their existing callers. */
static inline size_t SbPlaybackDiscard (PianoSong_t **queue, bool keepCurrent) {
	PianoSong_t *discard = keepCurrent && *queue != NULL ?
			PianoListNextP (*queue) : *queue;
	const size_t count = SbPlaylistCount (discard);
	if (keepCurrent && *queue != NULL) (*queue)->head.next = NULL;
	else *queue = NULL;
	PianoDestroyPlaylist (discard);
	return count;
}
