#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "playlist_prefetch.h"

static PianoSong_t *song (const char *token) {
	PianoSong_t *value = calloc (1, sizeof (*value));
	assert (value != NULL);
	value->trackToken = strdup (token);
	assert (value->trackToken != NULL);
	return value;
}

int main (void) {
	assert (!SbPlaylistPrefetchNeeded (3, false, SIZE_MAX));
	assert (SbPlaylistPrefetchNeeded (2, false, SIZE_MAX));
	assert (!SbPlaylistPrefetchNeeded (2, true, SIZE_MAX));
	assert (!SbPlaylistPrefetchNeeded (2, false, 2));
	assert (SbPlaylistPrefetchNeeded (1, false, 2));

	PianoSong_t *queue = song ("a");
	queue->head.next = &song ("b")->head;
	PianoSong_t *batch = song ("b");
	batch->head.next = &song ("c")->head;
	assert (SbPlaylistAppendUnique (&queue, batch) == 1);
	assert (SbPlaylistCount (queue) == 3);
	assert (SbPlaylistHasToken (queue, "a"));
	assert (SbPlaylistHasToken (queue, "b"));
	assert (SbPlaylistHasToken (queue, "c"));
	PianoDestroyPlaylist (queue);
	puts ("playlist prefetch tests passed");
	return 0;
}
