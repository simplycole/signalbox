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

int main (void) {
	/* Direct ANSI art must yield to every modal class. */
	assert (SbTuiPresentationArtMayPaint (false, false, false));
	assert (!SbTuiPresentationArtMayPaint (true, false, false)); /* Help */
	assert (!SbTuiPresentationArtMayPaint (false, true, false)); /* Track Info */
	assert (!SbTuiPresentationArtMayPaint (false, true, false)); /* Lyrics */
	assert (!SbTuiPresentationArtMayPaint (false, false, true)); /* Dialogs */
	SbTuiArtCompositor compositor = {0};
	assert (!SbTuiPresentationArtOcclude (&compositor, true));
	SbTuiPresentationArtDidPaint (&compositor);
	assert (compositor.painted);
	assert (SbTuiPresentationArtOcclude (&compositor, true));
	assert (!compositor.painted);
	/* Art becoming ready and timer redraws cannot repaint an open Help modal. */
	for (size_t update = 0; update < 4; update++) {
		assert (!SbTuiPresentationArtMayPaint (true, false, false));
		assert (!SbTuiPresentationArtOcclude (&compositor, true));
	}
	/* Closing permits immediate restoration; resize re-occludes at new geometry. */
	assert (SbTuiPresentationArtMayPaint (false, false, false));
	SbTuiPresentationArtDidPaint (&compositor);
	assert (SbTuiPresentationArtOcclude (&compositor, true));
	assert (!SbTuiPresentationArtMayPaint (false, true, false));
	assert (!SbTuiPresentationArtMayPaint (false, false, true));

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
	assert (strcmp (FindHelpCommand (help, helpCount, SB_UI_CMD_HELP,
			true)->keys, "?") == 0);
	assert (strcmp (FindHelpCommand (help, helpCount, SB_UI_CMD_QUIT,
			true)->keys, "q") == 0);
	assert (strcmp (FindHelpCommand (help, helpCount, SB_UI_CMD_LOVE,
			true)->keys, "+") == 0);
	assert (strcmp (FindHelpCommand (help, helpCount, SB_UI_CMD_BAN,
			true)->keys, "-") == 0);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_VOLUME_DOWN, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_VOLUME_UP, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_VOLUME_RESET, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_HISTORY, true) != NULL);
	assert (FindHelpCommand (help, helpCount, SB_UI_CMD_UPCOMING, true) != NULL);
	assert (FindHelpCommand (help, helpCount,
			SB_UI_CMD_CREATE_STATION_FROM_SONG, true) != NULL);
	assert (strcmp (FindHelpCommand (help, helpCount,
			SB_UI_CMD_CREATE_STATION_FROM_SONG, true)->keys, "v") == 0);
	assert (strstr (FindHelpCommand (help, helpCount, SB_UI_CMD_TOGGLE_PAUSE,
			true)->keys, "p") != NULL);
	assert (strstr (FindHelpCommand (help, helpCount, SB_UI_CMD_TOGGLE_PAUSE,
			true)->keys, "Space") != NULL);
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
	assert (strcmp (FindHelpCommand (help, helpCount, SB_UI_CMD_ADD_SHARED,
			true)->keys, "J") == 0);
	assert (strcmp (FindHelpCommand (help, helpCount, SB_UI_CMD_INFO,
			true)->keys, "o") == 0);
	/* A configured uppercase V wins; Help must not advertise the local toggle. */
	settings.keys[BAR_KS_CREATESTATION] = 'V';
	helpCount = SbTuiPresentationHelpEntries (&settings, help,
			sizeof (help) / sizeof (*help));
	assert (strstr (FindHelpCommand (help, helpCount, SB_UI_CMD_CREATE_STATION,
			true)->keys, "V") != NULL);
	assert (FindHelpCommand (help, helpCount,
			SB_UI_CMD_TOGGLE_VISUALIZER, false) == NULL);

	assert (SbTuiPresentationFieldRole ("Artist") == SB_TUI_TEXT_ARTIST);
	assert (SbTuiPresentationFieldRole ("Canonical Track") == SB_TUI_TEXT_TRACK);
	assert (SbTuiPresentationFieldRole ("Release") == SB_TUI_TEXT_ALBUM);
	assert (SbTuiPresentationFieldRole ("State") == SB_TUI_TEXT_STATE);
	assert (SbTuiPresentationFieldRole ("Provider") == SB_TUI_TEXT_PROVIDER);
	size_t labelLength = 0; const char *value = NULL; SbTuiTextRole role;
	assert (SbTuiPresentationSplitField ("Artist: Deftones", &labelLength,
			&value, &role));
	assert (labelLength == strlen ("Artist") && strcmp (value, "Deftones") == 0);
	assert (role == SB_TUI_TEXT_ARTIST);
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
