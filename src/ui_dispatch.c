/*
Copyright (c) 2010-2011
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <assert.h>

#include "debug.h"
#include "ui_dispatch.h"
#include "settings.h"
#include "ui.h"
#include "ui_act.h"

static const BarKeyShortcutFunc_t commandHandlers[SB_UI_CMD_COUNT] = {
	[SB_UI_CMD_HELP] = BarUiActHelp,
	[SB_UI_CMD_LOVE] = BarUiActLoveSong,
	[SB_UI_CMD_BAN] = BarUiActBanSong,
	[SB_UI_CMD_ADD_MUSIC] = BarUiActAddMusic,
	[SB_UI_CMD_CREATE_STATION] = BarUiActCreateStation,
	[SB_UI_CMD_DELETE_STATION] = BarUiActDeleteStation,
	[SB_UI_CMD_EXPLAIN] = BarUiActExplain,
	[SB_UI_CMD_GENRE_STATION] = BarUiActStationFromGenre,
	[SB_UI_CMD_HISTORY] = BarUiActHistory,
	[SB_UI_CMD_INFO] = BarUiActSongInfo,
	[SB_UI_CMD_ADD_SHARED] = BarUiActAddSharedStation,
	[SB_UI_CMD_SKIP] = BarUiActSkipSong,
	[SB_UI_CMD_TOGGLE_PAUSE] = BarUiActTogglePause,
	[SB_UI_CMD_QUIT] = BarUiActQuit,
	[SB_UI_CMD_RENAME_STATION] = BarUiActRenameStation,
	[SB_UI_CMD_SELECT_STATION] = BarUiActSelectStation,
	[SB_UI_CMD_TIRED] = BarUiActTempBanSong,
	[SB_UI_CMD_UPCOMING] = BarUiActPrintUpcoming,
	[SB_UI_CMD_SELECT_QUICKMIX] = BarUiActSelectQuickMix,
	[SB_UI_CMD_DEBUG] = BarUiActDebug,
	[SB_UI_CMD_BOOKMARK] = BarUiActBookmark,
	[SB_UI_CMD_VOLUME_DOWN] = BarUiActVolDown,
	[SB_UI_CMD_VOLUME_UP] = BarUiActVolUp,
	[SB_UI_CMD_MANAGE_STATION] = BarUiActManageStation,
	[SB_UI_CMD_CREATE_STATION_FROM_SONG] = BarUiActCreateStationFromSong,
	[SB_UI_CMD_PLAY] = BarUiActPlay,
	[SB_UI_CMD_PAUSE] = BarUiActPause,
	[SB_UI_CMD_VOLUME_RESET] = BarUiActVolReset,
	[SB_UI_CMD_SETTINGS] = BarUiActSettings,
	[SB_UI_CMD_ACTIVATE_STATION] = BarUiActActivateStation,
	[SB_UI_CMD_TRACK_INFO] = NULL,
	[SB_UI_CMD_LYRICS] = NULL,
};

static const char *BarUiCommandDiagnosticName (const SbUiCommand command) {
	if (command == SB_UI_CMD_ACTIVATE_STATION) return "activate_station";
	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (dispatchActions[i].command == command) {
			return dispatchActions[i].configKey;
		}
	}
	return "unknown";
}

/*	dispatch a named UI command
 *	@return true if the action was performed
 */
bool BarUiDispatchCommand (BarApp_t *app, const SbUiCommand command, PianoStation_t *selStation,
		PianoSong_t *selSong, const bool verbose,
		BarUiDispatchContext_t context) {
	assert (app != NULL);
	if (command <= SB_UI_CMD_NONE || command >= SB_UI_CMD_COUNT) {
		return false;
	}
	tuiDebugPrint ("command=%s station_context=%s song_context=%s model_generation=%llu\n",
			BarUiCommandDiagnosticName (command),
			selStation != NULL ? "yes" : "no",
			selSong != NULL ? "yes" : "no",
			(unsigned long long) app->uiModel.generation);
	if (command == SB_UI_CMD_ACTIVATE_STATION) {
		if (selStation == NULL) {
			if (verbose) {
				BarUiMsg (&app->settings, MSG_ERR, "No station selected.\n");
			}
			return false;
		}
		commandHandlers[command] (app, selStation, selSong,
				context | BAR_DC_STATION);
		return true;
	}

	if (selStation != NULL) {
		context |= BAR_DC_STATION;
	}
	if (selSong != NULL) {
		context |= BAR_DC_SONG;
	}

	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (dispatchActions[i].command == command) {
			if ((dispatchActions[i].context & context) == dispatchActions[i].context) {
				assert (commandHandlers[command] != NULL);
				commandHandlers[command] (app, selStation, selSong, context);
				return true;
			} else if (verbose) {
				if (dispatchActions[i].context & BAR_DC_SONG) {
					BarUiMsg (&app->settings, MSG_ERR, "No song playing.\n");
				} else if (dispatchActions[i].context & BAR_DC_STATION) {
					BarUiMsg (&app->settings, MSG_ERR, "No station selected.\n");
				} else {
					assert (0);
				}
				return false;
			}
		}
	}
	return false;
}
