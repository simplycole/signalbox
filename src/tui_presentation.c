#include "tui_presentation.h"
#include "enrichment.h"

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
	if (key == 'V' && configuredCommand == SB_UI_CMD_NONE)
		return SB_UI_CMD_TOGGLE_VISUALIZER;
	return configuredCommand;
}

const char *SbTuiPresentationStationSortHelp (void) {
	return "Station sort: A-Z / Original";
}

const char *SbTuiPresentationCreateStationHelp (void) {
	return "Create station from song / artist";
}

const char *SbTuiPresentationVisualizerHelp (void) {
	return "Toggle visualizer";
}
