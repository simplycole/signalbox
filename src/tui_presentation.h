#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SB_TUI_ART_COMPLETION_MS = 2000 };

typedef struct {
	uint64_t generation, completionDeadlineMs;
	int observedState, displayState;
} SbTuiArtStatus;

typedef enum {
	SB_TUI_TEXT_PRIMARY = 0,
	SB_TUI_TEXT_LABEL,
	SB_TUI_TEXT_SECTION,
	SB_TUI_TEXT_ARTIST,
	SB_TUI_TEXT_TRACK,
	SB_TUI_TEXT_ALBUM,
	SB_TUI_TEXT_STATION,
	SB_TUI_TEXT_TIME,
	SB_TUI_TEXT_STATE,
	SB_TUI_TEXT_PROVIDER,
	SB_TUI_TEXT_CONFIDENCE,
	SB_TUI_TEXT_LYRICS_BODY,
} SbTuiTextRole;

SbTuiTextRole SbTuiPresentationFieldRole (const char *label);
bool SbTuiPresentationIsSection (const char *line);
bool SbTuiPresentationSplitField (const char *line, size_t *labelLength,
		const char **value, SbTuiTextRole *valueRole);
bool SbTuiPresentationLyricsHeader (char *out, size_t size,
		const char *artist, const char *title, const char *album);
bool SbTuiPresentationStatus (char *out, size_t size, const char *status,
		int artState, size_t availableWidth);
int SbTuiArtStatusUpdate (SbTuiArtStatus *, uint64_t generation, int state,
		uint64_t nowMs);
