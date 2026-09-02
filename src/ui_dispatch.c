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

#include "ui_dispatch.h"
#include "settings.h"
#include "ui.h"
#include "ui_act.h"

const BarUiDispatchAction_t dispatchActions[BAR_KS_COUNT] = {
	{'?', SB_UI_CMD_HELP, BAR_DC_UNDEFINED, NULL, "act_help"},
	{'+', SB_UI_CMD_LOVE, BAR_DC_SONG, "love song", "act_songlove"},
	{'-', SB_UI_CMD_BAN, BAR_DC_SONG, "ban song", "act_songban"},
	{'a', SB_UI_CMD_ADD_MUSIC, BAR_DC_STATION, "add music to station", "act_stationaddmusic"},
	{'c', SB_UI_CMD_CREATE_STATION, BAR_DC_GLOBAL, "create new station", "act_stationcreate"},
	{'d', SB_UI_CMD_DELETE_STATION, BAR_DC_STATION, "delete station", "act_stationdelete"},
	{'e', SB_UI_CMD_EXPLAIN, BAR_DC_SONG, "explain why this song is played", "act_songexplain"},
	{'g', SB_UI_CMD_GENRE_STATION, BAR_DC_GLOBAL, "add genre station", "act_stationaddbygenre"},
	{'h', SB_UI_CMD_HISTORY, BAR_DC_GLOBAL, "song history", "act_history"},
	{'i', SB_UI_CMD_INFO, BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_SONG, "print information about song/station", "act_songinfo"},
	{'j', SB_UI_CMD_ADD_SHARED, BAR_DC_GLOBAL, "add shared station", "act_addshared"},
	{'n', SB_UI_CMD_SKIP, BAR_DC_GLOBAL | BAR_DC_STATION, "next song", "act_songnext"},
	{'p', SB_UI_CMD_TOGGLE_PAUSE, BAR_DC_GLOBAL | BAR_DC_STATION, "pause/resume playback", "act_songpausetoggle"},
	{'q', SB_UI_CMD_QUIT, BAR_DC_GLOBAL, "quit", "act_quit"},
	{'r', SB_UI_CMD_RENAME_STATION, BAR_DC_STATION, "rename station", "act_stationrename"},
	{'s', SB_UI_CMD_SELECT_STATION, BAR_DC_GLOBAL, "change station", "act_stationchange"},
	{'t', SB_UI_CMD_TIRED, BAR_DC_SONG, "tired (ban song for 1 month)", "act_songtired"},
	{'u', SB_UI_CMD_UPCOMING, BAR_DC_GLOBAL | BAR_DC_STATION, "upcoming songs", "act_upcoming"},
	{'x', SB_UI_CMD_SELECT_QUICKMIX, BAR_DC_STATION, "select quickmix stations", "act_stationselectquickmix"},
	{'$', SB_UI_CMD_DEBUG, BAR_DC_SONG, NULL, "act_debug"},
	{'b', SB_UI_CMD_BOOKMARK, BAR_DC_SONG, "bookmark song/artist", "act_bookmark"},
	{'(', SB_UI_CMD_VOLUME_DOWN, BAR_DC_GLOBAL, "decrease volume", "act_voldown"},
	{')', SB_UI_CMD_VOLUME_UP, BAR_DC_GLOBAL, "increase volume", "act_volup"},
	{'=', SB_UI_CMD_MANAGE_STATION, BAR_DC_STATION, "manage station seeds/feedback/mode", "act_managestation"},
	{' ', SB_UI_CMD_TOGGLE_PAUSE, BAR_DC_GLOBAL | BAR_DC_STATION, NULL, "act_songpausetoggle2"},
	{'v', SB_UI_CMD_CREATE_STATION_FROM_SONG, BAR_DC_SONG, "create new station from song or artist", "act_stationcreatefromsong"},
	{'P', SB_UI_CMD_PLAY, BAR_DC_GLOBAL | BAR_DC_STATION, "resume playback", "act_songplay"},
	{'S', SB_UI_CMD_PAUSE, BAR_DC_GLOBAL | BAR_DC_STATION, "pause playback", "act_songpause"},
	{'^', SB_UI_CMD_VOLUME_RESET, BAR_DC_GLOBAL, "reset volume", "act_volreset"},
	{'!', SB_UI_CMD_SETTINGS, BAR_DC_GLOBAL, "change settings", "act_settings"},
};

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
};

SbUiCommand BarUiCommandFromKey (const BarSettings_t *settings, const char key) {
	assert (settings != NULL);
	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (settings->keys[i] != BAR_KS_DISABLED && settings->keys[i] == key) {
			return dispatchActions[i].command;
		}
	}
	return SB_UI_CMD_NONE;
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
