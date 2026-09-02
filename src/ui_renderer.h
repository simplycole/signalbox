#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#include <piano.h>

#include "settings.h"
#include "ui_types.h"

typedef enum {
	SB_UI_PLAYBACK_STOPPED = 0,
	SB_UI_PLAYBACK_PLAYING,
	SB_UI_PLAYBACK_PAUSED,
} SbUiPlaybackState;

typedef enum {
	SB_UI_ACTIVITY_READY = 0,
	SB_UI_ACTIVITY_REQUESTING,
	SB_UI_ACTIVITY_RECONNECTING,
	SB_UI_ACTIVITY_ERROR,
	SB_UI_ACTIVITY_WAITING_PLAYLIST,
} SbUiActivityState;

enum {
	SB_UI_HISTORY_TEXT_MAX = 128,
};

typedef struct {
	char artist[SB_UI_HISTORY_TEXT_MAX];
	char title[SB_UI_HISTORY_TEXT_MAX];
	char album[SB_UI_HISTORY_TEXT_MAX];
	char station[SB_UI_HISTORY_TEXT_MAX];
	PianoSongRating_t rating;
	time_t playedAt;
} SbUiHistoryEntry;

/* Renderer-facing view state. Canonical Piano objects remain owned by BarApp_t;
 * these pointers are borrowed and valid only while their canonical objects are. */
typedef struct {
	/* Borrowed canonical list. BarApp_t owns every station and serializes list
	 * mutation with renderer access on the main thread. */
	const PianoStation_t *stations;
	const PianoStation_t *station;
	const PianoStation_t *songStation;
	const PianoSong_t *song;
	unsigned int duration;
	unsigned int elapsed;
	SbUiPlaybackState playback;
	SbUiActivityState activity;
	/* Software-volume offset in decibels. ReplayGain is applied separately by
	 * the player and is deliberately not presented as user volume. */
	int volumeDb;
	/* Owned display snapshots, newest first. These never borrow song pointers. */
	SbUiHistoryEntry *history;
	size_t historyCount;
	size_t historyCapacity;
	uint64_t generation;
	/* Changes only when the canonical station collection is refreshed. */
	uint64_t stationsGeneration;
} SbUiModel;

typedef enum {
	SB_UI_RENDER_STATION,
	SB_UI_RENDER_SONG,
	SB_UI_RENDER_PROGRESS,
	SB_UI_RENDER_STATE,
} SbUiRenderEvent;

typedef struct SbUiRenderer SbUiRenderer;

typedef enum {
	SB_TUI_THEME_PHOSPHOR = 0,
	SB_TUI_THEME_AMBER,
	SB_TUI_THEME_MONO,
	SB_TUI_THEME_NEUTRAL,
} SbTuiTheme;

typedef struct {
	void (*init) (SbUiRenderer *);
	void (*render) (SbUiRenderer *, const SbUiModel *, SbUiRenderEvent);
	SbUiCommandEvent (*readCommand) (SbUiRenderer *, const SbUiModel *);
	void (*message) (SbUiRenderer *, BarUiMsg_t, const char *, va_list);
	void (*shutdown) (SbUiRenderer *);
} SbUiRendererOps;

struct SbUiRenderer {
	const SbUiRendererOps *ops;
	const BarSettings_t *settings;
	void *data;
};

void SbUiModelInit (SbUiModel *);
void SbUiModelDestroy (SbUiModel *);
void SbUiModelSetStations (SbUiModel *, const PianoStation_t *);
void SbUiModelSetStation (SbUiModel *, const PianoStation_t *);
void SbUiModelSetSong (SbUiModel *, const PianoSong_t *,
		const PianoStation_t *);
void SbUiModelSetProgress (SbUiModel *, unsigned int, unsigned int,
		SbUiPlaybackState);
void SbUiModelSetVolume (SbUiModel *, int);
void SbUiModelSetActivity (SbUiModel *, SbUiActivityState);

void SbUiRendererInitClassic (SbUiRenderer *, const BarSettings_t *);
bool SbUiRendererInitCurses (SbUiRenderer *, const BarSettings_t *, SbTuiTheme);
bool SbUiRendererIsCurses (const SbUiRenderer *);
bool SbUiRendererPromptText (SbUiRenderer *, const SbUiModel *,
		const char *, const char *, char *, size_t);
bool SbUiRendererConfirm (SbUiRenderer *, const SbUiModel *,
		const char *, const char *);
int SbUiRendererSelectList (SbUiRenderer *, const SbUiModel *,
		const char *, const char *const *, size_t);
int SbUiRendererSelectHistory (SbUiRenderer *, const SbUiModel *);
bool SbUiRendererToggleList (SbUiRenderer *, const SbUiModel *,
		const char *, const char *const *, bool *, size_t);
void SbUiRendererRender (SbUiRenderer *, const SbUiModel *, SbUiRenderEvent);
SbUiCommandEvent SbUiRendererReadCommand (SbUiRenderer *, const SbUiModel *);
void SbUiRendererShutdown (SbUiRenderer *);
void SbUiRendererSetActive (SbUiRenderer *);
SbUiRenderer *SbUiRendererGetActive (void);
void SbUiRendererMessageV (SbUiRenderer *, BarUiMsg_t, const char *, va_list);

void SbUiClassicMessageV (const BarSettings_t *, BarUiMsg_t, const char *,
		va_list);
