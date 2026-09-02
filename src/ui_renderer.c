#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "ui_renderer.h"

static void SbUiModelChanged (SbUiModel *model) {
	++model->generation;
}

void SbUiModelInit (SbUiModel *model) {
	assert (model != NULL);
	memset (model, 0, sizeof (*model));
}

void SbUiModelSetStation (SbUiModel *model, const PianoStation_t *station) {
	assert (model != NULL);
	model->station = station;
	SbUiModelChanged (model);
}

void SbUiModelSetSong (SbUiModel *model, const PianoSong_t *song,
		const PianoStation_t *songStation) {
	assert (model != NULL);
	model->song = song;
	model->songStation = songStation;
	SbUiModelChanged (model);
}

void SbUiModelSetProgress (SbUiModel *model, const unsigned int elapsed,
		const unsigned int duration, const SbUiPlaybackState playback) {
	assert (model != NULL);
	model->elapsed = elapsed;
	model->duration = duration;
	model->playback = playback;
	SbUiModelChanged (model);
}

void SbUiClassicMessageV (const BarSettings_t *settings,
		const BarUiMsg_t type, const char *format, va_list fmtargs) {
	assert (settings != NULL);
	assert (type < MSG_COUNT);
	assert (format != NULL);

	switch (type) {
		case MSG_INFO:
		case MSG_PLAYING:
		case MSG_TIME:
		case MSG_ERR:
		case MSG_QUESTION:
		case MSG_LIST:
			fputs ("\033[2K", stdout);
			break;

		default:
			break;
	}

	if (settings->msgFormat[type].prefix != NULL) {
		fputs (settings->msgFormat[type].prefix, stdout);
	}
	vprintf (format, fmtargs);
	if (settings->msgFormat[type].postfix != NULL) {
		fputs (settings->msgFormat[type].postfix, stdout);
	}
	fflush (stdout);
}

static void SbUiClassicInit (SbUiRenderer *renderer) {
	(void) renderer;
}

static const char *SbUiClassicRatingIcon (const BarSettings_t *settings,
		const PianoSong_t *song) {
	switch (song->rating) {
		case PIANO_RATE_LOVE:
			return settings->loveIcon;
		case PIANO_RATE_BAN:
			return settings->banIcon;
		case PIANO_RATE_TIRED:
			return settings->tiredIcon;
		default:
			return "";
	}
}

static void SbUiClassicAppendNewline (char *s, const size_t maxlen) {
	const size_t len = strlen (s);
	if (len == maxlen-1) {
		s[maxlen-2] = '\n';
	} else {
		s[len] = '\n';
		s[len+1] = '\0';
	}
}

static void SbUiClassicRender (SbUiRenderer *renderer,
		const SbUiModel *model, const SbUiRenderEvent event) {
	const BarSettings_t *settings = renderer->settings;
	char outstr[512];

	switch (event) {
		case SB_UI_RENDER_STATION: {
			assert (model->station != NULL);
			const char *vals[] = {model->station->name, model->station->id};
			BarUiCustomFormat (outstr, sizeof (outstr), settings->npStationFormat,
					"ni", vals);
			SbUiClassicAppendNewline (outstr, sizeof (outstr));
			BarUiMsg (settings, MSG_PLAYING, "%s", outstr);
			break;
		}

		case SB_UI_RENDER_SONG: {
			assert (model->song != NULL);
			const char *vals[] = {model->song->title, model->song->artist,
					model->song->album, SbUiClassicRatingIcon (settings, model->song),
					model->songStation != NULL ? settings->atIcon : "",
					model->songStation != NULL ? model->songStation->name : "",
					model->song->detailUrl};
			BarUiCustomFormat (outstr, sizeof (outstr), settings->npSongFormat,
					"talr@su", vals);
			SbUiClassicAppendNewline (outstr, sizeof (outstr));
			BarUiMsg (settings, MSG_PLAYING, "%s", outstr);
			break;
		}

		case SB_UI_RENDER_PROGRESS: {
			unsigned int remaining;
			char sign[2] = {0, 0};
			if (model->elapsed <= model->duration) {
				remaining = model->duration - model->elapsed;
				sign[0] = '-';
			} else {
				remaining = model->elapsed - model->duration;
				sign[0] = '+';
			}
			char total[16], remainingText[16], elapsed[16];
			const char *vals[] = {total, remainingText, elapsed, sign};
			snprintf (total, sizeof (total), "%02u:%02u",
					model->duration/60, model->duration%60);
			snprintf (remainingText, sizeof (remainingText), "%02u:%02u",
					remaining/60, remaining%60);
			snprintf (elapsed, sizeof (elapsed), "%02u:%02u",
					model->elapsed/60, model->elapsed%60);
			BarUiCustomFormat (outstr, sizeof (outstr), settings->timeFormat,
					"tres", vals);
			BarUiMsg (settings, MSG_TIME, "%s\r", outstr);
			break;
		}
	}
}

static void SbUiClassicShutdown (SbUiRenderer *renderer) {
	(void) renderer;
}

static SbUiCommand SbUiClassicReadCommand (SbUiRenderer *renderer,
		const SbUiModel *model) {
	(void) renderer;
	(void) model;
	return SB_UI_CMD_NONE;
}

static void SbUiClassicMessage (SbUiRenderer *renderer, const BarUiMsg_t type,
		const char *format, va_list fmtargs) {
	SbUiClassicMessageV (renderer->settings, type, format, fmtargs);
}

static const SbUiRendererOps classicOps = {
	.init = SbUiClassicInit,
	.render = SbUiClassicRender,
	.readCommand = SbUiClassicReadCommand,
	.message = SbUiClassicMessage,
	.shutdown = SbUiClassicShutdown,
};

static SbUiRenderer *activeRenderer;

void SbUiRendererInitClassic (SbUiRenderer *renderer,
		const BarSettings_t *settings) {
	assert (renderer != NULL);
	assert (settings != NULL);
	renderer->ops = &classicOps;
	renderer->settings = settings;
	renderer->data = NULL;
	renderer->ops->init (renderer);
}

void SbUiRendererRender (SbUiRenderer *renderer, const SbUiModel *model,
		const SbUiRenderEvent event) {
	assert (renderer != NULL);
	assert (renderer->ops != NULL);
	assert (model != NULL);
	renderer->ops->render (renderer, model, event);
}

SbUiCommand SbUiRendererReadCommand (SbUiRenderer *renderer,
		const SbUiModel *model) {
	assert (renderer != NULL);
	assert (renderer->ops != NULL);
	return renderer->ops->readCommand (renderer, model);
}

void SbUiRendererSetActive (SbUiRenderer *renderer) {
	activeRenderer = renderer;
}

SbUiRenderer *SbUiRendererGetActive (void) {
	return activeRenderer;
}

void SbUiRendererMessageV (SbUiRenderer *renderer, const BarUiMsg_t type,
		const char *format, va_list fmtargs) {
	assert (renderer != NULL);
	renderer->ops->message (renderer, type, format, fmtargs);
}

void SbUiRendererShutdown (SbUiRenderer *renderer) {
	assert (renderer != NULL);
	assert (renderer->ops != NULL);
	renderer->ops->shutdown (renderer);
}
