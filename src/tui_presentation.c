#include "tui_presentation.h"
#include "enrichment.h"
#include "ui_dispatch.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

SbTuiTextRole SbTuiPresentationFieldRole (const char *label) {
	if (label == NULL) return SB_TUI_TEXT_PRIMARY;
	if (strcmp (label, "Artist") == 0 || strcmp (label, "Canonical Artist") == 0)
		return SB_TUI_TEXT_ARTIST;
	if (strcmp (label, "Track") == 0 || strcmp (label, "Canonical Track") == 0)
		return SB_TUI_TEXT_TRACK;
	if (strcmp (label, "Album") == 0 || strcmp (label, "Release") == 0)
		return SB_TUI_TEXT_ALBUM;
	if (strcmp (label, "Station") == 0) return SB_TUI_TEXT_STATION;
	if (strcmp (label, "Length") == 0 || strcmp (label, "Release Date") == 0 ||
			strcmp (label, "Rating") == 0) return SB_TUI_TEXT_TIME;
	if (strcmp (label, "Provider") == 0) return SB_TUI_TEXT_PROVIDER;
	if (strcmp (label, "Confidence") == 0) return SB_TUI_TEXT_CONFIDENCE;
	if (strcmp (label, "Metadata") == 0 || strcmp (label, "State") == 0)
		return SB_TUI_TEXT_STATE;
	return SB_TUI_TEXT_PRIMARY;
}

bool SbTuiPresentationIsSection (const char *line) {
	return line != NULL && (strcmp (line, "PANDORA") == 0 ||
			strcmp (line, "ENRICHMENT") == 0 || strcmp (line, "LYRICS") == 0 ||
			strcmp (line, "ALBUM ART") == 0);
}

bool SbTuiPresentationSplitField (const char *line, size_t *labelLength,
		const char **value, SbTuiTextRole *valueRole) {
	if (line == NULL) return false;
	const char *colon = strchr (line, ':');
	if (colon == NULL || colon[1] != ' ' || colon[2] == '\0') return false;
	if (labelLength != NULL) *labelLength = (size_t) (colon - line);
	if (value != NULL) *value = colon + 2;
	if (valueRole != NULL) {
		char label[64]; const size_t length = (size_t) (colon - line);
		if (length >= sizeof (label)) return false;
		memcpy (label, line, length); label[length] = '\0';
		*valueRole = SbTuiPresentationFieldRole (label);
	}
	return true;
}

bool SbTuiPresentationLyricsHeader (char *out, const size_t size,
		const char *artist, const char *title, const char *album) {
	if (out == NULL || size == 0 || artist == NULL || title == NULL) return false;
	const int written = snprintf (out, size, "%s — %s%s%s\n\n", artist, title,
			album != NULL && album[0] != '\0' ? "\n" : "",
			album != NULL ? album : "");
	return written >= 0 && (size_t) written < size;
}

const char *SbTuiPresentationLyricsState (const int status,
		const bool hasPlainLyrics, const size_t syncedLineCount) {
	if (status == SB_LOOKUP_INSTRUMENTAL) return "Instrumental";
	if (status == SB_LOOKUP_NO_MATCH) return "No match";
	if (status == SB_LOOKUP_ERROR || status == SB_LOOKUP_UNAVAILABLE)
		return "Temporarily unavailable";
	if (status == SB_LOOKUP_LOADING) return "Loading";
	if (status == SB_LOOKUP_AVAILABLE) {
		if (syncedLineCount > 0) return "Synced";
		if (hasPlainLyrics) return "Plain";
	}
	return "Temporarily unavailable";
}

bool SbTuiPresentationInlineLyrics (const int displayMode,
		const size_t syncedLineCount) {
	return displayMode != 0 && syncedLineCount > 0;
}

bool SbTuiPresentationStatus (char *out, const size_t size, const char *status,
		const int artState, const size_t availableWidth) {
	if (out == NULL || size == 0) return false;
	if (status == NULL || status[0] == '\0') status = "Ready";
	const size_t baseLength = strlen (status);
	const char *suffix = "";
	if (artState == SB_LOOKUP_LOADING) {
		if (baseLength + strlen ("    Art: Loading") <= availableWidth)
			suffix = "    Art: Loading";
		else if (baseLength + strlen ("    Art: Load") <= availableWidth)
			suffix = "    Art: Load";
		else if (baseLength + 4 + 4 <= availableWidth)
			suffix = "    Art…";
	} else {
		const char *complete = artState == SB_LOOKUP_AVAILABLE ? "    Art: Ready" :
				artState == SB_LOOKUP_NO_MATCH ? "    Art: None" :
				(artState == SB_LOOKUP_UNAVAILABLE || artState == SB_LOOKUP_ERROR) ?
				"    Art: Unavailable" : "";
		if (baseLength + strlen (complete) <= availableWidth) suffix = complete;
	}
	const int written = snprintf (out, size, "%s%s", status, suffix);
	const size_t displayLength = written >= 0 ? (size_t) written -
			(strstr (suffix, "…") != NULL ? 2 : 0) : 0;
	return written >= 0 && (size_t) written < size && displayLength <= availableWidth;
}

int SbTuiArtStatusUpdate (SbTuiArtStatus *tracker, const uint64_t generation,
		const int state, const uint64_t nowMs) {
	if (tracker == NULL) return state;
	if (tracker->generation != generation || tracker->observedState != state) {
		tracker->generation = generation;
		tracker->observedState = tracker->displayState = state;
		tracker->completionDeadlineMs = state == SB_LOOKUP_AVAILABLE ||
				state == SB_LOOKUP_NO_MATCH || state == SB_LOOKUP_UNAVAILABLE ||
				state == SB_LOOKUP_ERROR ? nowMs + SB_TUI_ART_COMPLETION_MS : 0;
	}
	if (tracker->completionDeadlineMs != 0 && nowMs >= tracker->completionDeadlineMs) {
		tracker->displayState = SB_LOOKUP_IDLE;
		tracker->completionDeadlineMs = 0;
	}
	return tracker->displayState;
}

SbUiCommand SbTuiPresentationResolveKey (const int key,
		const SbUiCommand configuredCommand) {
	if (key == 'z') return SB_UI_CMD_CYCLE_STATION_SORT;
	if (key == 'i' || key == 'I') return SB_UI_CMD_TRACK_INFO;
	if (key == 'l' || key == 'L') return SB_UI_CMD_LYRICS;
	if (key == 'V' && configuredCommand == SB_UI_CMD_NONE)
		return SB_UI_CMD_TOGGLE_VISUALIZER;
	return configuredCommand;
}

typedef struct {
	SbTuiHelpSection section;
	SbUiCommand command;
	const char *keys;
	const char *description;
} SbTuiFixedHelp;

static const SbTuiFixedHelp fixedHelp[] = {
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Tab", "Switch pane"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Shift+Tab", "Switch pane backward"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Up/Down / j/k", "Navigate focused list / scroll"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "PgUp/PgDn", "Page lists / text views"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Home/End", "First / last"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Mouse wheel", "Navigate / scroll"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Enter", "Activate / open focused item"},
	{SB_TUI_HELP_NAVIGATION, SB_UI_CMD_NONE, "Esc", "Back / close / cancel"},
	{SB_TUI_HELP_STATIONS, SB_UI_CMD_CYCLE_STATION_SORT, "z", "Station sort: A-Z / Original"},
	{SB_TUI_HELP_STATIONS, SB_UI_CMD_NONE, "/", "Filter Stations"},
	{SB_TUI_HELP_STATIONS, SB_UI_CMD_NONE, "#", "Jump to visible station number"},
	{SB_TUI_HELP_STATIONS, SB_UI_CMD_NONE, "0-9 / keypad", "Type station number (jump mode)"},
	{SB_TUI_HELP_STATIONS, SB_UI_CMD_NONE, "Backspace/Delete", "Edit station number (jump mode)"},
	{SB_TUI_HELP_STATIONS, SB_UI_CMD_NONE, "Enter", "Tune station number (jump mode)"},
	{SB_TUI_HELP_FILTER_EDITING, SB_UI_CMD_NONE, "Printable text", "Edit station filter"},
	{SB_TUI_HELP_FILTER_EDITING, SB_UI_CMD_NONE, "Backspace", "Delete previous filter character"},
	{SB_TUI_HELP_FILTER_EDITING, SB_UI_CMD_NONE, "Enter", "Keep filter and return"},
	{SB_TUI_HELP_FILTER_EDITING, SB_UI_CMD_NONE, "Esc", "Clear filter and return"},
	{SB_TUI_HELP_HISTORY, SB_UI_CMD_NONE, "Tab", "Focus Recent pane"},
	{SB_TUI_HELP_HISTORY, SB_UI_CMD_NONE, "Enter", "Actions for selected history track"},
	{SB_TUI_HELP_UPCOMING, SB_UI_CMD_NONE, "Enter", "Select track / action"},
	{SB_TUI_HELP_TRACK, SB_UI_CMD_TRACK_INFO, "i / I", "Open / close Track Info"},
	{SB_TUI_HELP_TRACK, SB_UI_CMD_LYRICS, "l / L", "Open / close Lyrics"},
	{SB_TUI_HELP_DISPLAY, SB_UI_CMD_TOGGLE_VISUALIZER, "V", "Toggle visualizer"},
	{SB_TUI_HELP_TEXT_INPUT, SB_UI_CMD_NONE, "Printable text", "Type in text prompts"},
	{SB_TUI_HELP_TEXT_INPUT, SB_UI_CMD_NONE, "Left/Right", "Move text cursor"},
	{SB_TUI_HELP_TEXT_INPUT, SB_UI_CMD_NONE, "Home/End", "Start / end of text"},
	{SB_TUI_HELP_TEXT_INPUT, SB_UI_CMD_NONE, "Backspace/Delete", "Delete text"},
	{SB_TUI_HELP_TEXT_INPUT, SB_UI_CMD_NONE, "Enter", "Submit text"},
	{SB_TUI_HELP_TEXT_INPUT, SB_UI_CMD_NONE, "Esc", "Cancel text prompt"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Up/Down / j/k", "Navigate choices / scroll text"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "PgUp/PgDn", "Page long lists / text"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Home/End", "First / last"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Mouse wheel", "Scroll text views"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Enter", "Select / confirm / close text"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Esc", "Cancel / close"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Space", "Toggle QuickMix item"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "y / n", "Answer confirmation"},
	{SB_TUI_HELP_MODALS, SB_UI_CMD_NONE, "Left/Right / Tab", "Change confirmation choice"},
};

bool SbTuiPresentationConfiguredKeyReachable (
		const BarSettings_t *settings, const char key,
		const SbUiCommand command) {
	if (key == BAR_KS_DISABLED || key == 'j' || key == 'k' || key == '\t' ||
			key == '\n' || key == '\r' || key == 27) return false;
	if (BarUiCommandFromKey (settings, key) != command) return false;
	return SbTuiPresentationResolveKey ((unsigned char) key, command) == command;
}

static void SbTuiPresentationAppendKey (char *keys, const size_t size,
		const char key) {
	char label[8];
	if (key == ' ') strcpy (label, "Space");
	else if (isprint ((unsigned char) key)) snprintf (label, sizeof (label), "%c", key);
	else snprintf (label, sizeof (label), "0x%02X", (unsigned char) key);
	const size_t used = strlen (keys);
	if (used >= size) return;
	snprintf (keys + used, size - used, "%s%s", used > 0 ? " / " : "", label);
}

size_t SbTuiPresentationHelpEntries (const BarSettings_t *settings,
		SbTuiHelpEntry *entries, const size_t capacity) {
	if (settings == NULL || entries == NULL || capacity == 0) return 0;
	size_t count = 0;
	for (int section = 0; section < SB_TUI_HELP_SECTION_COUNT; section++) {
		for (size_t i = 0; i < tuiCommandHelpCount; i++) {
			const BarUiTuiCommandHelp * const help = &tuiCommandHelp[i];
			if ((int) help->section != section || count >= capacity) continue;
			SbTuiHelpEntry entry = {.section = help->section,
					.command = help->command, .description = help->description,
					.configured = true};
			for (size_t keyIndex = 0; keyIndex < BAR_KS_COUNT; keyIndex++) {
				if (dispatchActions[keyIndex].command == help->command &&
						SbTuiPresentationConfiguredKeyReachable (settings,
								settings->keys[keyIndex], help->command))
					SbTuiPresentationAppendKey (entry.keys, sizeof (entry.keys),
							settings->keys[keyIndex]);
			}
			if (entry.keys[0] != '\0') entries[count++] = entry;
		}
		for (size_t i = 0; i < sizeof (fixedHelp) / sizeof (*fixedHelp); i++) {
			if ((int) fixedHelp[i].section != section || count >= capacity) continue;
			if (fixedHelp[i].command == SB_UI_CMD_TOGGLE_VISUALIZER &&
					BarUiCommandFromKey (settings, 'V') != SB_UI_CMD_NONE) continue;
			entries[count++] = (SbTuiHelpEntry) {.section = fixedHelp[i].section,
					.command = fixedHelp[i].command,
					.description = fixedHelp[i].description, .configured = false};
			snprintf (entries[count - 1].keys, sizeof (entries[count - 1].keys),
					"%s", fixedHelp[i].keys);
		}
	}
	return count;
}

const char *SbTuiPresentationHelpSectionName (const SbTuiHelpSection section) {
	static const char * const names[SB_TUI_HELP_SECTION_COUNT] = {
		"GLOBAL", "NAVIGATION", "PLAYBACK", "RATING", "VOLUME", "STATIONS",
		"FILTER EDITING", "HISTORY", "UPCOMING", "TRACK", "DISPLAY",
		"TEXT INPUT", "MODALS",
	};
	return section >= 0 && section < SB_TUI_HELP_SECTION_COUNT ? names[section] : "";
}

bool SbTuiPresentationArtMayPaint (const bool helpVisible,
		const bool textModalVisible, const bool popupVisible) {
	return !helpVisible && !textModalVisible && !popupVisible;
}

bool SbTuiPresentationArtOcclude (SbTuiArtCompositor *compositor,
		const bool overlayVisible) {
	if (compositor == NULL || !overlayVisible || !compositor->painted) return false;
	compositor->painted = false;
	return true;
}

void SbTuiPresentationArtDidPaint (SbTuiArtCompositor *compositor) {
	if (compositor != NULL) compositor->painted = true;
}
