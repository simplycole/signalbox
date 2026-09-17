#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"
#include "ui_types.h"

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

enum { SB_TUI_HELP_KEY_LABEL_SIZE = 32, SB_TUI_HELP_ENTRY_CAPACITY = 96 };

typedef struct {
	SbTuiHelpSection section;
	SbUiCommand command;
	char keys[SB_TUI_HELP_KEY_LABEL_SIZE];
	const char *description;
	bool configured;
} SbTuiHelpEntry;

/* ANSI album art bypasses curses' physical-screen bookkeeping.  Track whether
 * those cells need to be explicitly invalidated before an overlay is drawn. */
typedef struct {
	bool painted;
} SbTuiArtCompositor;

SbTuiTextRole SbTuiPresentationFieldRole (const char *label);
bool SbTuiPresentationIsSection (const char *line);
bool SbTuiPresentationSplitField (const char *line, size_t *labelLength,
		const char **value, SbTuiTextRole *valueRole);
bool SbTuiPresentationLyricsHeader (char *out, size_t size,
		const char *artist, const char *title, const char *album);
const char *SbTuiPresentationLyricsState (int status, bool hasPlainLyrics,
		size_t syncedLineCount);
bool SbTuiPresentationInlineLyrics (int displayMode, size_t syncedLineCount);
bool SbTuiPresentationStatus (char *out, size_t size, const char *status,
		int artState, size_t availableWidth);
int SbTuiArtStatusUpdate (SbTuiArtStatus *, uint64_t generation, int state,
		uint64_t nowMs);
SbUiCommand SbTuiPresentationResolveKey (int key, SbUiCommand configuredCommand);
bool SbTuiPresentationConfiguredKeyReachable (const BarSettings_t *, char key,
		SbUiCommand);
size_t SbTuiPresentationHelpEntries (const BarSettings_t *, SbTuiHelpEntry *,
		size_t capacity);
const char *SbTuiPresentationHelpSectionName (SbTuiHelpSection);
bool SbTuiPresentationArtMayPaint (bool helpVisible, bool textModalVisible,
		bool popupVisible);
bool SbTuiPresentationArtOcclude (SbTuiArtCompositor *, bool overlayVisible);
void SbTuiPresentationArtDidPaint (SbTuiArtCompositor *);
