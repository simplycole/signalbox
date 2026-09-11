#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <piano.h>

#define SB_PREFETCH_THRESHOLD 2

static inline size_t SbPlaylistCount (const PianoSong_t *song) {
	size_t count = 0;
	PianoListForeachP (song) count++;
	return count;
}

static inline bool SbPlaylistPrefetchNeeded (const size_t remaining,
		const bool inFlight, const size_t lastAttemptRemaining) {
	return !inFlight && remaining <= SB_PREFETCH_THRESHOLD &&
			remaining != lastAttemptRemaining;
}

static inline bool SbPlaylistHasToken (const PianoSong_t *song,
		const char *token) {
	if (token == NULL || *token == '\0') return false;
	PianoListForeachP (song) if (song->trackToken != NULL &&
			strcmp (song->trackToken, token) == 0) return true;
	return false;
}

/* Consumes batch, appending unique songs and destroying overlap entries. */
static inline size_t SbPlaylistAppendUnique (PianoSong_t **queue,
		PianoSong_t *batch) {
	size_t added = 0;
	PianoSong_t *tail = *queue;
	while (tail != NULL && PianoListNextP (tail) != NULL)
		tail = PianoListNextP (tail);
	while (batch != NULL) {
		PianoSong_t *song = batch;
		batch = PianoListNextP (batch);
		song->head.next = NULL;
		if (SbPlaylistHasToken (*queue, song->trackToken)) {
			PianoDestroyPlaylist (song);
			continue;
		}
		if (tail == NULL) *queue = song;
		else tail->head.next = &song->head;
		tail = song;
		added++;
	}
	return added;
}
