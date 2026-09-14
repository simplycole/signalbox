#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	int64_t timestamp_ms;
	char *text;
} SbSyncedLyricLine;

typedef struct {
	SbSyncedLyricLine *lines;
	size_t count;
	int64_t offset_ms;
} SbSyncedLyrics;

typedef struct {
	const SbSyncedLyricLine *previous, *current, *next;
	/* SIZE_MAX means playback is still before the first timestamp. */
	size_t current_index;
} SbLyricContext;

typedef struct {
	size_t current_index;
	int64_t last_effective_ms;
	bool initialized;
} SbLyricCursor;

void SbSyncedLyricsInit (SbSyncedLyrics *);
void SbSyncedLyricsDestroy (SbSyncedLyrics *);
bool SbSyncedLyricsParse (SbSyncedLyrics *, const char *);
SbLyricContext SbSyncedLyricsLookup (const SbSyncedLyrics *, int64_t);
void SbLyricCursorReset (SbLyricCursor *);
SbLyricContext SbSyncedLyricsLookupCursor (const SbSyncedLyrics *, int64_t,
		SbLyricCursor *, bool *);
