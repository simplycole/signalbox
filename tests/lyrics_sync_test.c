#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lyrics_sync.h"

int main (void) {
	SbSyncedLyrics lyrics; SbSyncedLyricsInit (&lyrics);
	assert (SbSyncedLyricsParse (&lyrics,
			"[ar:metadata]\n[00:20.125] later\n[bad]no\n"
			"[00:01.25] first\n[00:10.000][00:15.50] same\n[00:08.00]\n"));
	assert (lyrics.count == 4);
	assert (lyrics.lines[0].timestamp_ms == 1250 &&
			strcmp (lyrics.lines[0].text, "first") == 0);
	assert (lyrics.lines[1].timestamp_ms == 10000);
	assert (lyrics.lines[2].timestamp_ms == 15500);
	assert (lyrics.lines[3].timestamp_ms == 20125);
	SbLyricContext context = SbSyncedLyricsLookup (&lyrics, 1000);
	assert (context.current == NULL && context.previous == NULL &&
			context.next == &lyrics.lines[0]);
	context = SbSyncedLyricsLookup (&lyrics, 1250);
	assert (context.current == &lyrics.lines[0] && context.previous == NULL &&
			context.next == &lyrics.lines[1]);
	context = SbSyncedLyricsLookup (&lyrics, 12000);
	assert (context.previous == &lyrics.lines[0] &&
			context.current == &lyrics.lines[1] && context.next == &lyrics.lines[2]);
	context = SbSyncedLyricsLookup (&lyrics, 999999);
	assert (context.current == &lyrics.lines[3] && context.next == NULL);
	lyrics.offset_ms = 250;
	assert (SbSyncedLyricsLookup (&lyrics, 1000).current == &lyrics.lines[0]);
	lyrics.offset_ms = -250;
	assert (SbSyncedLyricsLookup (&lyrics, 1500).current == &lyrics.lines[0]);
	lyrics.offset_ms = 0;
	SbLyricCursor cursor; SbLyricCursorReset (&cursor); bool reseek = false;
	context = SbSyncedLyricsLookupCursor (&lyrics, 1250, &cursor, &reseek);
	assert (!reseek && context.current_index == 0); /* normal playback */
	context = SbSyncedLyricsLookupCursor (&lyrics, 1250, &cursor, &reseek);
	assert (!reseek && context.current_index == 0); /* pause */
	context = SbSyncedLyricsLookupCursor (&lyrics, 1500, &cursor, &reseek);
	assert (!reseek && context.current_index == 0); /* resume */
	context = SbSyncedLyricsLookupCursor (&lyrics, 16000, &cursor, &reseek);
	assert (reseek && context.current_index == 2); /* large jump, skipped lines */
	context = SbSyncedLyricsLookupCursor (&lyrics, 10000, &cursor, &reseek);
	assert (reseek && context.current_index == 1); /* backward jump */
	cursor.current_index = 999; /* retained index invalid */
	context = SbSyncedLyricsLookupCursor (&lyrics, 10000, &cursor, &reseek);
	assert (reseek && context.current_index == 1); /* binary search restored it */
	lyrics.offset_ms = 7000;
	context = SbSyncedLyricsLookupCursor (&lyrics, 9000, &cursor, &reseek);
	assert (context.current_index == 2); /* offset survives cursor recovery */

	/* Async publication uses the current playback position immediately. An
	 * empty/plain result does not poison the retained cursor or require a track,
	 * station, resize, or lyric-boundary event to recover. */
	SbSyncedLyricsDestroy (&lyrics);
	SbLyricCursorReset (&cursor);
	context = SbSyncedLyricsLookupCursor (&lyrics, 12000, &cursor, &reseek);
	assert (context.current == NULL && !cursor.initialized);
	assert (SbSyncedLyricsParse (&lyrics,
			"[00:01.00] first\n[00:10.00] current\n[00:20.00] next"));
	context = SbSyncedLyricsLookupCursor (&lyrics, 12000, &cursor, &reseek);
	assert (context.current != NULL && strcmp (context.current->text, "current") == 0);
	assert (context.next != NULL && strcmp (context.next->text, "next") == 0);

	/* Track-to-track clear and republish transitions never retain old text. */
	SbSyncedLyricsDestroy (&lyrics); /* Synced -> Plain/No match/etc. */
	assert (lyrics.count == 0 && lyrics.lines == NULL);
	SbLyricCursorReset (&cursor);
	assert (SbSyncedLyricsParse (&lyrics,
			"[00:02.00] replacement one\n[00:08.00] replacement two"));
	context = SbSyncedLyricsLookupCursor (&lyrics, 9000, &cursor, &reseek);
	assert (context.current != NULL &&
			strcmp (context.current->text, "replacement two") == 0);
	SbSyncedLyricsDestroy (&lyrics);
	puts ("lyrics sync tests passed");
	return 0;
}
