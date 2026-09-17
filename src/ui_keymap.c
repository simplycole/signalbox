#include <assert.h>

#include "ui_dispatch.h"

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

const BarUiTuiCommandHelp tuiCommandHelp[] = {
	{SB_UI_CMD_HELP, SB_TUI_HELP_GLOBAL, "Show / close Help"},
	{SB_UI_CMD_QUIT, SB_TUI_HELP_GLOBAL, "Quit Signalbox"},
	{SB_UI_CMD_TOGGLE_PAUSE, SB_TUI_HELP_PLAYBACK, "Pause / resume"},
	{SB_UI_CMD_SKIP, SB_TUI_HELP_PLAYBACK, "Next track"},
	{SB_UI_CMD_PLAY, SB_TUI_HELP_PLAYBACK, "Resume playback"},
	{SB_UI_CMD_PAUSE, SB_TUI_HELP_PLAYBACK, "Pause playback"},
	{SB_UI_CMD_LOVE, SB_TUI_HELP_RATING, "Love current track"},
	{SB_UI_CMD_BAN, SB_TUI_HELP_RATING, "Ban current track"},
	{SB_UI_CMD_VOLUME_DOWN, SB_TUI_HELP_VOLUME, "Volume down"},
	{SB_UI_CMD_VOLUME_UP, SB_TUI_HELP_VOLUME, "Volume up"},
	{SB_UI_CMD_VOLUME_RESET, SB_TUI_HELP_VOLUME, "Reset volume to 0 dB"},
	{SB_UI_CMD_SELECT_STATION, SB_TUI_HELP_STATIONS, "Focus Stations pane"},
	{SB_UI_CMD_ADD_MUSIC, SB_TUI_HELP_STATIONS, "Add music to selected station"},
	{SB_UI_CMD_CREATE_STATION, SB_TUI_HELP_STATIONS, "Create station by search"},
	{SB_UI_CMD_DELETE_STATION, SB_TUI_HELP_STATIONS, "Delete selected station"},
	{SB_UI_CMD_GENRE_STATION, SB_TUI_HELP_STATIONS, "Create genre station"},
	{SB_UI_CMD_ADD_SHARED, SB_TUI_HELP_STATIONS, "Add shared station by ID"},
	{SB_UI_CMD_RENAME_STATION, SB_TUI_HELP_STATIONS, "Rename selected station"},
	{SB_UI_CMD_SELECT_QUICKMIX, SB_TUI_HELP_STATIONS, "Edit selected QuickMix"},
	{SB_UI_CMD_MANAGE_STATION, SB_TUI_HELP_STATIONS, "Manage selected station"},
	{SB_UI_CMD_CREATE_STATION_FROM_SONG, SB_TUI_HELP_STATIONS,
			"Create station from song / artist"},
	{SB_UI_CMD_HISTORY, SB_TUI_HELP_HISTORY, "Browse full session history"},
	{SB_UI_CMD_UPCOMING, SB_TUI_HELP_UPCOMING, "Browse upcoming tracks"},
	{SB_UI_CMD_INFO, SB_TUI_HELP_TRACK, "Song / station information"},
	{SB_UI_CMD_EXPLAIN, SB_TUI_HELP_TRACK, "Why current track is playing"},
	{SB_UI_CMD_BOOKMARK, SB_TUI_HELP_TRACK, "Bookmark current song / artist"},
};

const size_t tuiCommandHelpCount = sizeof (tuiCommandHelp) /
		sizeof (*tuiCommandHelp);

SbUiCommand BarUiCommandFromKey (const BarSettings_t *settings, const char key) {
	assert (settings != NULL);
	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (settings->keys[i] != BAR_KS_DISABLED && settings->keys[i] == key) {
			return dispatchActions[i].command;
		}
	}
	return SB_UI_CMD_NONE;
}

const BarUiTuiCommandHelp *BarUiTuiCommandHelpFor (
		const SbUiCommand command) {
	for (size_t i = 0; i < tuiCommandHelpCount; i++) {
		if (tuiCommandHelp[i].command == command) return &tuiCommandHelp[i];
	}
	return NULL;
}

bool BarUiCommandTuiEnabled (const SbUiCommand command) {
	return BarUiTuiCommandHelpFor (command) != NULL;
}
