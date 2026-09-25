#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tui_presentation.h"
#include "enrichment.h"
#include "settings.h"
#include "ui_dispatch.h"

static const SbTuiHelpEntry *FindHelpCommand (const SbTuiHelpEntry *entries,
		const size_t count, const SbUiCommand command, const bool configured) {
	for (size_t i = 0; i < count; i++) {
		if (entries[i].command == command && entries[i].configured == configured)
			return &entries[i];
	}
	return NULL;
}

static const SbTuiHelpEntry *FindHelpText (const SbTuiHelpEntry *entries,
		const size_t count, const char *keys, const char *description) {
	for (size_t i = 0; i < count; i++) {
		if ((keys == NULL || strcmp (entries[i].keys, keys) == 0) &&
				(description == NULL || strstr (entries[i].description,
						description) != NULL)) return &entries[i];
	}
	return NULL;
}

static bool CommandHasReachableKey (const BarSettings_t *settings,
		const SbUiCommand command) {
	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (dispatchActions[i].command == command &&
				SbTuiPresentationConfiguredKeyReachable (settings,
						settings->keys[i], command)) return true;
	}
	return false;
}

enum { COMPOSITOR_ROWS = 50, COMPOSITOR_COLS = 120 };

static void CompositorReset (char cells[COMPOSITOR_ROWS][COMPOSITOR_COLS]) {
	memset (cells, '.', COMPOSITOR_ROWS * COMPOSITOR_COLS);
}

static void CompositorPaintArt (
		char cells[COMPOSITOR_ROWS][COMPOSITOR_COLS], const SbTuiRect art,
		const SbTuiRect overlay) {
	for (int y = 0; y < COMPOSITOR_ROWS; y++)
		for (int x = 0; x < COMPOSITOR_COLS; x++)
			if (SbTuiPresentationCellOwner (art, overlay, y, x) == SB_TUI_CELL_ART)
				cells[y][x] = 'A';
}

static void CompositorDrawModal (
		char cells[COMPOSITOR_ROWS][COMPOSITOR_COLS], const SbTuiRect modal,
		const char *title) {
	const char instruction[] = "Up/Down scroll; Enter/Esc closes this dialog safely";
	const char body[] = "Pandora Artist: AFI / Track: Bleed Black";
	for (int x = modal.x; x < modal.x + modal.width; x++) {
		cells[modal.y][x] = '#';
		cells[modal.y + modal.height - 1][x] = '#';
	}
	for (int y = modal.y; y < modal.y + modal.height; y++) {
		cells[y][modal.x] = '#';
		cells[y][modal.x + modal.width - 1] = '#';
	}
	memcpy (&cells[modal.y + 1][modal.x + 2], title, strlen (title));
	memcpy (&cells[modal.y + 3][modal.x + 2], instruction,
			strlen (instruction));
	memcpy (&cells[modal.y + 5][modal.x + 2], body, strlen (body));
}

static void CompositorAssertModal (
		const char cells[COMPOSITOR_ROWS][COMPOSITOR_COLS],
		const SbTuiRect modal, const char *title) {
	const char instruction[] = "Up/Down scroll; Enter/Esc closes this dialog safely";
	const char body[] = "Pandora Artist: AFI / Track: Bleed Black";
	assert (memcmp (&cells[modal.y + 1][modal.x + 2], title,
			strlen (title)) == 0);
	assert (memcmp (&cells[modal.y + 3][modal.x + 2], instruction,
			strlen (instruction)) == 0);
	assert (memcmp (&cells[modal.y + 5][modal.x + 2], body,
			strlen (body)) == 0);
	for (int x = modal.x; x < modal.x + modal.width; x++) {
		assert (cells[modal.y][x] == '#');
		assert (cells[modal.y + modal.height - 1][x] == '#');
	}
}

static void TestCompositorModalOwnership (const SbTuiRect modal,
		const char *title) {
	char cells[COMPOSITOR_ROWS][COMPOSITOR_COLS];
	const SbTuiRect partialArt = {modal.y > 2 ? modal.y - 2 : 0,
			modal.x + modal.width / 2, modal.height + 4,
			modal.width / 2 + 8};
	CompositorReset (cells);
	CompositorPaintArt (cells, partialArt, modal);
	CompositorDrawModal (cells, modal, title);
	CompositorAssertModal (cells, modal, title);
	assert (cells[partialArt.y][partialArt.x] == 'A');
	assert (cells[modal.y][partialArt.x] == '#');

	/* Async arrival paints after an already-visible modal, but passive clipping
	 * leaves every modal-owned body/instruction/border cell untouched. */
	CompositorReset (cells);
	CompositorDrawModal (cells, modal, title);
	CompositorPaintArt (cells, partialArt, modal);
	CompositorAssertModal (cells, modal, title);

	/* Full overlap still gives every cell to the modal. */
	CompositorReset (cells);
	CompositorPaintArt (cells, modal, modal);
	CompositorDrawModal (cells, modal, title);
	CompositorAssertModal (cells, modal, title);

	/* Closing removes the clip and restores the complete art rectangle. */
	CompositorReset (cells);
	CompositorPaintArt (cells, modal, (SbTuiRect) {0});
	for (int y = modal.y; y < modal.y + modal.height; y++)
		for (int x = modal.x; x < modal.x + modal.width; x++)
			assert (cells[y][x] == 'A');
}

int main (void) {
	/* Primary modals share width, centering, clamping, and chrome policy while
	 * retaining content-appropriate preferred heights. */
	const SbTuiRect helpRect = SbTuiPresentationModalRect (
			SB_TUI_MODAL_HELP, 68, 192, 80);
	const SbTuiRect trackInfoRect = SbTuiPresentationModalRect (
			SB_TUI_MODAL_TRACK_INFO, 68, 192, 22);
	const SbTuiRect lyricsRect = SbTuiPresentationModalRect (
			SB_TUI_MODAL_LYRICS, 68, 192, 80);
	assert (helpRect.width == 72 && helpRect.height >= 32 && helpRect.height <= 34);
	assert (lyricsRect.width == 72 && lyricsRect.height >= 38 &&
			lyricsRect.height <= 40);
	assert (trackInfoRect.width == 72 && trackInfoRect.height >= 26 &&
			trackInfoRect.height <= 32);
	assert (helpRect.x == trackInfoRect.x && trackInfoRect.x == lyricsRect.x);
	assert (helpRect.width == trackInfoRect.width &&
			trackInfoRect.width == lyricsRect.width);
	assert (helpRect.x == (192 - helpRect.width) / 2);
	assert (helpRect.y == (68 - helpRect.height) / 2);
	assert (trackInfoRect.y == (68 - trackInfoRect.height) / 2);
	assert (lyricsRect.y == (68 - lyricsRect.height) / 2);
	/* Track Info ends two outer rows after its last rendered content row and
	 * clamps long content rather than forcing Lyrics-height dead space. */
	const SbTuiRect shortInfo = SbTuiPresentationModalRect (
			SB_TUI_MODAL_TRACK_INFO, 68, 192, 8);
	const SbTuiRect longInfo = SbTuiPresentationModalRect (
			SB_TUI_MODAL_TRACK_INFO, 68, 192, 80);
	assert (shortInfo.height == 15 && longInfo.height == 32);
	const SbTuiRect medium = SbTuiPresentationModalRect (
			SB_TUI_MODAL_LYRICS, 40, 100, 50);
	assert (medium.width == 72 && medium.height >= 20 && medium.height <= 24);
	assert (medium.x == (100 - medium.width) / 2);
	assert (medium.y == (40 - medium.height) / 2);
	const SbTuiRect compact = SbTuiPresentationModalRect (
			SB_TUI_MODAL_HELP, 24, 70, 80);
	assert (compact.width == 66 && compact.height >= 12 && compact.height < 20);
	const SbTuiRect narrow = SbTuiPresentationModalRect (
			SB_TUI_MODAL_LYRICS, 15, 50, 80);
	assert (narrow.width == 46 && narrow.height == 13);
	assert (narrow.x >= 0 && narrow.y >= 0);
	assert (narrow.x + narrow.width <= 50 && narrow.y + narrow.height <= 15);
	assert (narrow.height - 6 > 0); /* text title/footer leave a viewport */

	/* ANSI art is clipped cell-by-cell only where the topmost overlay overlaps. */
	const SbTuiRect artRect = {6, 66, 10, 20};
	const SbTuiRect noOverlap = {20, 80, 8, 12};
	const SbTuiRect partial = {8, 76, 8, 20};
	const SbTuiRect full = {5, 60, 20, 40};
	assert (!SbTuiPresentationRectsIntersect (artRect, noOverlap));
	assert (SbTuiPresentationRectsIntersect (artRect, partial));
	assert (SbTuiPresentationRectsIntersect (artRect, full));
	assert (!SbTuiPresentationRectContains (noOverlap, 6, 66));
	assert (!SbTuiPresentationRectContains (partial, 7, 76));
	assert (SbTuiPresentationRectContains (partial, 8, 76));
	assert (SbTuiPresentationRectContains (full, 6, 66));
	const SbTuiRect closed = {0};
	assert (!SbTuiPresentationRectContains (closed, 8, 76));
	const SbTuiRect resizedModal = SbTuiPresentationModalRect (
			SB_TUI_MODAL_LYRICS, 54, 150, 80);
	assert (resizedModal.x == (150 - resizedModal.width) / 2);
	assert (resizedModal.y == (54 - resizedModal.height) / 2);
	/* Every popup class uses its actual centered bounds; no approximate shared
	 * modal geometry is reconstructed by the compositor. */
	const SbTuiRect dialog = SbTuiPresentationPopupRect (68, 192, 8);
	assert (dialog.width == 68 && dialog.height == 8);
	assert (dialog.x == 62 && dialog.y == 30);
	assert (!SbTuiPresentationArtCellVisible (helpRect, helpRect.y, helpRect.x));
	assert (!SbTuiPresentationArtCellVisible (trackInfoRect, trackInfoRect.y,
			trackInfoRect.x));
	assert (!SbTuiPresentationArtCellVisible (lyricsRect, lyricsRect.y,
			lyricsRect.x));
	assert (!SbTuiPresentationArtCellVisible (dialog, dialog.y, dialog.x));
	assert (SbTuiPresentationCellOwner (artRect, partial, 8, 76) ==
			SB_TUI_CELL_OVERLAY);
	assert (SbTuiPresentationCellOwner (artRect, partial, 7, 76) ==
			SB_TUI_CELL_ART);
	assert (SbTuiPresentationCellOwner (artRect, partial, 30, 30) ==
			SB_TUI_CELL_BASE);
	/* Inspect final cell contents for every retained overlay family and one
	 * blocking dialog, including partial/full overlap, async arrival, and close. */
	const SbTuiRect testHelp = SbTuiPresentationModalRect (
			SB_TUI_MODAL_HELP, 40, 90, 24);
	const SbTuiRect testTrack = SbTuiPresentationModalRect (
			SB_TUI_MODAL_TRACK_INFO, 40, 90, 12);
	const SbTuiRect testLyrics = SbTuiPresentationModalRect (
			SB_TUI_MODAL_LYRICS, 40, 90, 24);
	const SbTuiRect testDialog = SbTuiPresentationPopupRect (40, 90, 10);
	TestCompositorModalOwnership (testHelp, "SIGNALBOX HELP");
	TestCompositorModalOwnership (testTrack, "TRACK INFO");
	TestCompositorModalOwnership (testLyrics, "LYRICS");
	TestCompositorModalOwnership (testDialog, "CONFIRM");
	/* Resized retained modals recompute ownership with no stale old clip. */
	TestCompositorModalOwnership (SbTuiPresentationModalRect (
			SB_TUI_MODAL_HELP, 30, 76, 40), "SIGNALBOX HELP RESIZED");
	TestCompositorModalOwnership (SbTuiPresentationModalRect (
			SB_TUI_MODAL_TRACK_INFO, 30, 76, 10), "TRACK INFO RESIZED");

	/* Half-open outer rectangles own every border cell, but no adjacent cell. */
	const SbTuiRect frame = {10, 20, 6, 8};
	assert (!SbTuiPresentationArtCellVisible (frame, 10, 20)); /* top/left */
	assert (!SbTuiPresentationArtCellVisible (frame, 10, 27)); /* top/right */
	assert (!SbTuiPresentationArtCellVisible (frame, 15, 20)); /* bottom/left */
	assert (!SbTuiPresentationArtCellVisible (frame, 15, 27)); /* bottom/right */
	assert (SbTuiPresentationArtCellVisible (frame, 9, 20)); /* above */
	assert (SbTuiPresentationArtCellVisible (frame, 16, 20)); /* below */
	assert (SbTuiPresentationArtCellVisible (frame, 10, 19)); /* left */
	assert (SbTuiPresentationArtCellVisible (frame, 10, 28)); /* right */

	BarSettings_t settings = {0};
	for (size_t i = 0; i < BAR_KS_COUNT; i++)
		settings.keys[i] = dispatchActions[i].defaultKey;
	assert (BarUiCommandFromKey (&settings, 'v') ==
			SB_UI_CMD_CREATE_STATION_FROM_SONG);
	assert (BarUiCommandFromKey (&settings, 'V') == SB_UI_CMD_NONE);
	assert (SbTuiPresentationResolveKey ('v', BarUiCommandFromKey (&settings, 'v')) ==
			SB_UI_CMD_CREATE_STATION_FROM_SONG);
	assert (SbTuiPresentationResolveKey ('V', BarUiCommandFromKey (&settings, 'V')) ==
			SB_UI_CMD_TOGGLE_VISUALIZER);
	assert (SbTuiPresentationResolveKey ('z', SB_UI_CMD_NONE) ==
			SB_UI_CMD_CYCLE_STATION_SORT);
	assert (SbTuiPresentationResolveKey ('V', SB_UI_CMD_HELP) == SB_UI_CMD_HELP);

	SbTuiHelpEntry help[SB_TUI_HELP_ENTRY_CAPACITY];
	size_t helpCount = SbTuiPresentationHelpEntries (&settings, help,
			sizeof (help) / sizeof (*help));
	assert (helpCount > 0);
	/* Canonical configurable inventory and rendered Help must stay in lockstep. */
	for (size_t i = 0; i < tuiCommandHelpCount; i++) {
		const SbUiCommand command = tuiCommandHelp[i].command;
		assert ((FindHelpCommand (help, helpCount, command, true) != NULL) ==
				CommandHasReachableKey (&settings, command));
	}
	for (size_t i = 0; i < helpCount; i++) {
		if (help[i].configured) {
			assert (BarUiCommandTuiEnabled (help[i].command));
			assert (BarUiTuiCommandHelpFor (help[i].command) != NULL);
		}
	}
	const SbTuiHelpEntry *entry = FindHelpCommand (help, helpCount,
			SB_UI_CMD_HELP, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "?") == 0);
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_QUIT, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "q") == 0);
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_LOVE, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "+") == 0);
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_BAN, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "-") == 0);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_VOLUME_DOWN, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_VOLUME_UP, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_VOLUME_RESET, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_HISTORY, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_UPCOMING, true) != NULL);
	entry = FindHelpCommand (help, helpCount,
			SB_UI_CMD_CREATE_STATION_FROM_SONG, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "v") == 0);
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_TOGGLE_PAUSE, true);
	assert (entry != NULL);
	assert (strstr (entry->keys, "p") != NULL);
	assert (strstr (entry->keys, "Space") != NULL);
	assert (FindHelpText (help, helpCount, "z", "A-Z / Original") != NULL);
	assert (FindHelpText (help, helpCount, "V", "visualizer") != NULL);
	assert (FindHelpText (help, helpCount, "i / I", "Track Info") != NULL);
	assert (FindHelpText (help, helpCount, "l / L", "Lyrics") != NULL);
	assert (FindHelpText (help, helpCount, "Tab", "Switch pane") != NULL);
	assert (FindHelpText (help, helpCount, "Up/Down / j/k", "Navigate") != NULL);
	assert (FindHelpText (help, helpCount, "Printable text", "station filter") != NULL);
	assert (FindHelpText (help, helpCount, "Backspace", "filter") != NULL);
	assert (FindHelpText (help, helpCount, "/", "Filter Stations") != NULL);
	assert (FindHelpText (help, helpCount, "#",
			"Jump to visible station number") != NULL);
	assert (FindHelpText (help, helpCount, "0-9 / keypad", "jump mode") != NULL);
	assert (FindHelpText (help, helpCount, "Enter",
			"selected history track") != NULL);
	assert (FindHelpText (help, helpCount, "Enter", "Select track / action") != NULL);
	/* A 64-column Help modal leaves 41 columns for each description. */
	for (size_t i = 0; i < helpCount; i++)
		assert (strlen (help[i].description) <= 41);
	/* Tired/debug/settings are not dispatched by the retained TUI. */
	assert (!BarUiCommandTuiEnabled (SB_UI_CMD_TIRED));
	assert (!BarUiCommandTuiEnabled (SB_UI_CMD_DEBUG));
	assert (!BarUiCommandTuiEnabled (SB_UI_CMD_SETTINGS));
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_TIRED, true) == NULL);
	/* Default j and i bindings are shadowed by navigation and Track Info. */
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_ADD_SHARED, true) == NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_INFO, true) == NULL);
	settings.keys[BAR_KS_ADDSHARED] = 'J';
	settings.keys[BAR_KS_INFO] = 'o';
	helpCount = SbTuiPresentationHelpEntries (&settings, help,
			sizeof (help) / sizeof (*help));
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_ADD_SHARED, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "J") == 0);
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_INFO, true);
	assert (entry != NULL);
	assert (strcmp (entry->keys, "o") == 0);
	/* A configured uppercase V wins; Help must not advertise the local toggle. */
	settings.keys[BAR_KS_CREATESTATION] = 'V';
	helpCount = SbTuiPresentationHelpEntries (&settings, help,
			sizeof (help) / sizeof (*help));
	entry = FindHelpCommand (help, helpCount, SB_UI_CMD_CREATE_STATION, true);
	assert (entry != NULL);
	assert (strstr (entry->keys, "V") != NULL);
	assert (FindHelpCommand (help, helpCount,
			SB_UI_CMD_TOGGLE_VISUALIZER, false) == NULL);

	assert (SbTuiPresentationFieldRole ("Artist") == SB_TUI_TEXT_ARTIST);
	assert (SbTuiPresentationFieldRole ("Canonical Track") == SB_TUI_TEXT_TRACK);
	assert (SbTuiPresentationFieldRole ("Release") == SB_TUI_TEXT_ALBUM);
	assert (SbTuiPresentationFieldRole ("Original Release") == SB_TUI_TEXT_TIME);
	assert (SbTuiPresentationFieldRole ("Edition Release") == SB_TUI_TEXT_TIME);
	assert (SbTuiPresentationFieldRole ("Genres") == SB_TUI_TEXT_PRIMARY);
	assert (SbTuiPresentationFieldRole ("State") == SB_TUI_TEXT_STATE);
	assert (SbTuiPresentationFieldRole ("Provider") == SB_TUI_TEXT_PROVIDER);
	size_t labelLength = 0; const char *value = NULL; SbTuiTextRole role;
	assert (SbTuiPresentationSplitField ("Artist: Deftones", &labelLength,
			&value, &role));
	assert (labelLength == strlen ("Artist") && strcmp (value, "Deftones") == 0);
	assert (role == SB_TUI_TEXT_ARTIST);
	assert (SbTuiPresentationSplitField ("Release: A Very Long Deluxe Release Title That Wraps", &labelLength,
			&value, &role));
	assert (labelLength == strlen ("Release"));
	assert (role == SB_TUI_TEXT_ALBUM);
	assert (!SbTuiPresentationSplitField ("ALBUM ART", NULL, NULL, NULL));
	assert (!SbTuiPresentationSplitField ("Provider: ", NULL, NULL, NULL));
	assert (SbTuiPresentationIsSection ("LYRICS"));
	assert (!SbTuiPresentationIsSection ("a wrapped field value"));
	char text[256];
	assert (SbTuiPresentationLyricsHeader (text, sizeof (text), "Deftones",
			"Entombed", "Koi No Yokan"));
	assert (strcmp (text, "Deftones — Entombed\nKoi No Yokan\n\n") == 0);
	assert (SbTuiPresentationLyricsHeader (text, sizeof (text), "Artist", "Title", ""));
	assert (strcmp (text, "Artist — Title\n\n") == 0);
	assert (strcmp (SbTuiPresentationLyricsState (SB_LOOKUP_AVAILABLE,
			true, 0), "Plain") == 0);
	assert (strcmp (SbTuiPresentationLyricsState (SB_LOOKUP_AVAILABLE,
			true, 22), "Synced") == 0);
	assert (strcmp (SbTuiPresentationLyricsState (SB_LOOKUP_INSTRUMENTAL,
			false, 0), "Instrumental") == 0);
	assert (strcmp (SbTuiPresentationLyricsState (SB_LOOKUP_NO_MATCH,
			false, 0), "No match") == 0);
	assert (strcmp (SbTuiPresentationLyricsState (SB_LOOKUP_ERROR,
			false, 0), "Temporarily unavailable") == 0);
	assert (strcmp (SbTuiPresentationLyricsState (SB_LOOKUP_IDLE,
			false, 0), "Temporarily unavailable") == 0);
	assert (!SbTuiPresentationInlineLyrics (2, 0)); /* plain stays out of inline */
	assert (SbTuiPresentationInlineLyrics (2, 22)); /* synced renders inline */
	assert (!SbTuiPresentationInlineLyrics (0, 22)); /* display=off */
	SbTuiInlineLyricsPresentation inlineLyrics =
			SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, true,
					SB_LYRICS_DISPLAY_THREE_LINE, 0, 1, 1, 11, 40);
	assert (!inlineLyrics.eligible && !inlineLyrics.visible &&
			inlineLyrics.reason == SB_TUI_INLINE_PLAIN); /* Plain -> Synced */
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, true,
			SB_LYRICS_DISPLAY_THREE_LINE, 20, 1, 1, 11, 40);
	assert (inlineLyrics.eligible && inlineLyrics.visible && inlineLyrics.threeLine &&
			inlineLyrics.separated);
	/* Synced -> Plain clears immediately; all non-synced terminal states do too. */
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, true,
			SB_LYRICS_DISPLAY_THREE_LINE, 0, 2, 2, 11, 40);
	assert (!inlineLyrics.visible && inlineLyrics.reason == SB_TUI_INLINE_PLAIN);
	const int terminalStates[] = {SB_LOOKUP_NO_MATCH, SB_LOOKUP_UNAVAILABLE,
			SB_LOOKUP_INSTRUMENTAL};
	for (size_t i = 0; i < sizeof (terminalStates) / sizeof (*terminalStates); i++) {
		inlineLyrics = SbTuiPresentationInlineLyricsState (terminalStates[i], false,
				SB_LYRICS_DISPLAY_THREE_LINE, 0, 3, 3, 11, 40);
		assert (!inlineLyrics.eligible && !inlineLyrics.visible);
		inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, false,
				SB_LYRICS_DISPLAY_THREE_LINE, 12, 3, 3, 11, 40);
		assert (inlineLyrics.eligible && inlineLyrics.visible);
	}
	/* Both ordinary and station-driven next songs publish against the current
	 * song generation. Stale results and layout suppression remain explicit. */
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, false,
			SB_LYRICS_DISPLAY_LINE, 12, 4, 5, 8, 40);
	assert (!inlineLyrics.eligible &&
			inlineLyrics.reason == SB_TUI_INLINE_STALE_GENERATION);
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, false,
			SB_LYRICS_DISPLAY_LINE, 12, 5, 5, 6, 40);
	assert (inlineLyrics.eligible && !inlineLyrics.visible &&
			inlineLyrics.reason == SB_TUI_INLINE_LAYOUT);
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, false,
			SB_LYRICS_DISPLAY_LINE, 12, 5, 5, 8, 40);
	assert (inlineLyrics.visible && !inlineLyrics.separated);
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, false,
			SB_LYRICS_DISPLAY_LINE, 12, 5, 5, 9, 40);
	assert (inlineLyrics.visible && inlineLyrics.separated);
	/* async result/layout invalidation */
	inlineLyrics = SbTuiPresentationInlineLyricsState (SB_LOOKUP_AVAILABLE, false,
			SB_LYRICS_DISPLAY_OFF, 12, 5, 5, 11, 40);
	assert (!inlineLyrics.eligible &&
			inlineLyrics.reason == SB_TUI_INLINE_DISPLAY_OFF);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 24));
	assert (strcmp (text, "Ready    Art: Loading") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 18));
	assert (strcmp (text, "Ready    Art: Load") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 13));
	assert (strcmp (text, "Ready    Art…") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 9));
	assert (strcmp (text, "Ready") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_AVAILABLE, 24));
	assert (strcmp (text, "Ready    Art: Ready") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_NO_MATCH, 17));
	assert (strcmp (text, "Ready") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Playing", SB_LOOKUP_ERROR, 32));
	assert (strcmp (text, "Playing    Art: Unavailable") == 0);
	SbTuiArtStatus art = {0};
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_LOADING, 100) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_LOADING, 5000) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_AVAILABLE, 5000) == SB_LOOKUP_AVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_AVAILABLE, 6999) == SB_LOOKUP_AVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_AVAILABLE, 7000) == SB_LOOKUP_IDLE);
	assert (SbTuiArtStatusUpdate (&art, 2, SB_LOOKUP_NO_MATCH, 8000) == SB_LOOKUP_NO_MATCH);
	assert (SbTuiArtStatusUpdate (&art, 2, SB_LOOKUP_NO_MATCH, 10000) == SB_LOOKUP_IDLE);
	assert (SbTuiArtStatusUpdate (&art, 3, SB_LOOKUP_UNAVAILABLE, 11000) == SB_LOOKUP_UNAVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 3, SB_LOOKUP_UNAVAILABLE, 13000) == SB_LOOKUP_IDLE);
	/* A new generation replaces the old completion deadline. */
	assert (SbTuiArtStatusUpdate (&art, 4, SB_LOOKUP_AVAILABLE, 14000) == SB_LOOKUP_AVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_LOADING, 14500) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_LOADING, 17000) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_ERROR, 17000) == SB_LOOKUP_ERROR);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_ERROR, 19000) == SB_LOOKUP_IDLE);
	puts ("tui presentation tests passed");
	return 0;
}
