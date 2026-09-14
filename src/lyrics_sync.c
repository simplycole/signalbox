#include "lyrics_sync.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void SbSyncedLyricsInit (SbSyncedLyrics *lyrics) {
	memset (lyrics, 0, sizeof (*lyrics));
}

void SbSyncedLyricsDestroy (SbSyncedLyrics *lyrics) {
	for (size_t i = 0; i < lyrics->count; i++) free (lyrics->lines[i].text);
	free (lyrics->lines);
	SbSyncedLyricsInit (lyrics);
}

static bool timestamp (const char *p, const char **end, int64_t *milliseconds) {
	if (p[0] != '[' || !isdigit ((unsigned char) p[1])) return false;
	char *minutesEnd = NULL;
	const long minutes = strtol (p + 1, &minutesEnd, 10);
	if (minutesEnd == p + 1 || *minutesEnd != ':' ||
			!isdigit ((unsigned char) minutesEnd[1]) ||
			!isdigit ((unsigned char) minutesEnd[2])) return false;
	const int seconds = (minutesEnd[1] - '0') * 10 + minutesEnd[2] - '0';
	if (seconds >= 60 || minutes < 0) return false;
	const char *fraction = minutesEnd + 3;
	if (*fraction++ != '.') return false;
	int value = 0, digits = 0;
	while (digits < 3 && isdigit ((unsigned char) *fraction)) {
		value = value * 10 + *fraction++ - '0'; digits++;
	}
	if ((digits != 2 && digits != 3) || *fraction != ']') return false;
	if (digits == 2) value *= 10;
	*milliseconds = minutes * 60000 + seconds * 1000 + value;
	*end = fraction + 1;
	return true;
}

static bool addLine (SbSyncedLyrics *lyrics, const int64_t time,
		const char *text, const size_t length) {
	SbSyncedLyricLine *lines = realloc (lyrics->lines,
			(lyrics->count + 1) * sizeof (*lines));
	if (lines == NULL) return false;
	lyrics->lines = lines;
	char *copy = malloc (length + 1);
	if (copy == NULL) return false;
	memcpy (copy, text, length); copy[length] = '\0';
	lyrics->lines[lyrics->count++] = (SbSyncedLyricLine) {time, copy};
	return true;
}

static int compareLines (const void *left, const void *right) {
	const SbSyncedLyricLine *a = left, *b = right;
	return a->timestamp_ms < b->timestamp_ms ? -1 :
			a->timestamp_ms > b->timestamp_ms ? 1 : 0;
}

bool SbSyncedLyricsParse (SbSyncedLyrics *lyrics, const char *payload) {
	SbSyncedLyricsDestroy (lyrics);
	if (payload == NULL) return true;
	const char *line = payload;
	while (*line != '\0') {
		const char *lineEnd = strchr (line, '\n');
		if (lineEnd == NULL) lineEnd = line + strlen (line);
		if (lineEnd > line && lineEnd[-1] == '\r') lineEnd--;
		const char *p = line; int64_t times[32]; size_t timeCount = 0;
		while (p < lineEnd && timeCount < sizeof (times) / sizeof (*times)) {
			const char *after = NULL;
			if (!timestamp (p, &after, &times[timeCount])) break;
			timeCount++; p = after;
		}
		while (p < lineEnd && isspace ((unsigned char) *p)) p++;
		const char *textEnd = lineEnd;
		while (textEnd > p && isspace ((unsigned char) textEnd[-1])) textEnd--;
		/* Bracketed non-timestamp rows are common LRC metadata. Empty timed
		 * rows carry no displayable instrumental marker, so ignore them too. */
		if (timeCount > 0 && textEnd > p) {
			for (size_t i = 0; i < timeCount; i++)
				if (!addLine (lyrics, times[i], p, (size_t) (textEnd - p))) {
					SbSyncedLyricsDestroy (lyrics); return false;
				}
		}
		line = *lineEnd == '\0' ? lineEnd : lineEnd + 1;
	}
	qsort (lyrics->lines, lyrics->count, sizeof (*lyrics->lines), compareLines);
	return true;
}

SbLyricContext SbSyncedLyricsLookup (const SbSyncedLyrics *lyrics,
		const int64_t playback_ms) {
	SbLyricContext result = {NULL, NULL, NULL, SIZE_MAX};
	if (lyrics == NULL || lyrics->count == 0) return result;
	const int64_t effective = playback_ms + lyrics->offset_ms;
	size_t low = 0, high = lyrics->count;
	while (low < high) {
		const size_t middle = low + (high - low) / 2;
		if (lyrics->lines[middle].timestamp_ms <= effective) low = middle + 1;
		else high = middle;
	}
	if (low == 0) { result.next = &lyrics->lines[0]; return result; }
	result.current_index = low - 1;
	result.current = &lyrics->lines[result.current_index];
	if (result.current_index > 0) result.previous = &lyrics->lines[result.current_index - 1];
	if (low < lyrics->count) result.next = &lyrics->lines[low];
	return result;
}

void SbLyricCursorReset (SbLyricCursor *cursor) {
	cursor->current_index = SIZE_MAX;
	cursor->last_effective_ms = 0;
	cursor->initialized = false;
}

SbLyricContext SbSyncedLyricsLookupCursor (const SbSyncedLyrics *lyrics,
		const int64_t playback_ms, SbLyricCursor *cursor, bool *reseek) {
	if (reseek != NULL) *reseek = false;
	if (lyrics == NULL || cursor == NULL || lyrics->count == 0)
		return SbSyncedLyricsLookup (lyrics, playback_ms);
	const int64_t effective = playback_ms + lyrics->offset_ms;
	const bool discontinuity = cursor->initialized &&
			(effective < cursor->last_effective_ms ||
			effective - cursor->last_effective_ms > 5000);
	if (!cursor->initialized || discontinuity ||
			(cursor->current_index != SIZE_MAX && cursor->current_index >= lyrics->count)) {
		SbLyricContext found = SbSyncedLyricsLookup (lyrics, playback_ms);
		if (reseek != NULL) *reseek = cursor->initialized;
		cursor->current_index = found.current_index;
		cursor->last_effective_ms = effective; cursor->initialized = true;
		return found;
	}
	/* Sequential playback advances across any number of timestamps without
	 * accumulating time. A preroll cursor becomes current at the first line. */
	if (cursor->current_index == SIZE_MAX) {
		if (effective >= lyrics->lines[0].timestamp_ms) cursor->current_index = 0;
	} else while (cursor->current_index + 1 < lyrics->count &&
			lyrics->lines[cursor->current_index + 1].timestamp_ms <= effective)
		cursor->current_index++;
	/* The cursor is only an optimization. If its interval is ever invalid,
	 * recover from the authoritative position with binary search. */
	const bool invalid = cursor->current_index == SIZE_MAX ?
			effective >= lyrics->lines[0].timestamp_ms :
			effective < lyrics->lines[cursor->current_index].timestamp_ms ||
			(cursor->current_index + 1 < lyrics->count &&
			effective >= lyrics->lines[cursor->current_index + 1].timestamp_ms);
	if (invalid) {
		SbLyricContext found = SbSyncedLyricsLookup (lyrics, playback_ms);
		if (reseek != NULL) *reseek = true;
		cursor->current_index = found.current_index;
		cursor->last_effective_ms = effective;
		return found;
	}
	cursor->last_effective_ms = effective;
	SbLyricContext result = {NULL, NULL, NULL, cursor->current_index};
	if (cursor->current_index == SIZE_MAX) result.next = &lyrics->lines[0];
	else {
		result.current = &lyrics->lines[cursor->current_index];
		if (cursor->current_index > 0) result.previous =
				&lyrics->lines[cursor->current_index - 1];
		if (cursor->current_index + 1 < lyrics->count) result.next =
				&lyrics->lines[cursor->current_index + 1];
	}
	return result;
}
