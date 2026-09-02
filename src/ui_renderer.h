#pragma once

#include <stdarg.h>
#include <stdint.h>

#include <piano.h>

#include "settings.h"
#include "ui_types.h"

typedef enum {
	SB_UI_PLAYBACK_STOPPED = 0,
	SB_UI_PLAYBACK_PLAYING,
	SB_UI_PLAYBACK_PAUSED,
} SbUiPlaybackState;

/* Renderer-facing view state. Canonical Piano objects remain owned by BarApp_t;
 * these pointers are borrowed and valid only while their canonical objects are. */
typedef struct {
	const PianoStation_t *station;
	const PianoStation_t *songStation;
	const PianoSong_t *song;
	unsigned int duration;
	unsigned int elapsed;
	SbUiPlaybackState playback;
	uint64_t generation;
} SbUiModel;

typedef enum {
	SB_UI_RENDER_STATION,
	SB_UI_RENDER_SONG,
	SB_UI_RENDER_PROGRESS,
} SbUiRenderEvent;

typedef struct SbUiRenderer SbUiRenderer;

typedef struct {
	void (*init) (SbUiRenderer *);
	void (*render) (SbUiRenderer *, const SbUiModel *, SbUiRenderEvent);
	void (*shutdown) (SbUiRenderer *);
} SbUiRendererOps;

struct SbUiRenderer {
	const SbUiRendererOps *ops;
	const BarSettings_t *settings;
	void *data;
};

void SbUiModelInit (SbUiModel *);
void SbUiModelSetStation (SbUiModel *, const PianoStation_t *);
void SbUiModelSetSong (SbUiModel *, const PianoSong_t *,
		const PianoStation_t *);
void SbUiModelSetProgress (SbUiModel *, unsigned int, unsigned int,
		SbUiPlaybackState);

void SbUiRendererInitClassic (SbUiRenderer *, const BarSettings_t *);
void SbUiRendererRender (SbUiRenderer *, const SbUiModel *, SbUiRenderEvent);
void SbUiRendererShutdown (SbUiRenderer *);

void SbUiClassicMessageV (const BarSettings_t *, BarUiMsg_t, const char *,
		va_list);
