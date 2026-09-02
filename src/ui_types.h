/*
Copyright (c) 2011
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

#ifndef SRC_UI_TYPES_H_2HR75RII
#define SRC_UI_TYPES_H_2HR75RII

typedef enum {
	MSG_NONE = 0,
	MSG_INFO = 1,
	MSG_PLAYING = 2,
	MSG_TIME = 3,
	MSG_ERR = 4,
	MSG_QUESTION = 5,
	MSG_LIST = 6,
	MSG_COUNT = 7, /* invalid type */
} BarUiMsg_t;

typedef enum {
	SB_UI_CMD_NONE = 0,
	SB_UI_CMD_HELP,
	SB_UI_CMD_LOVE,
	SB_UI_CMD_BAN,
	SB_UI_CMD_ADD_MUSIC,
	SB_UI_CMD_CREATE_STATION,
	SB_UI_CMD_DELETE_STATION,
	SB_UI_CMD_EXPLAIN,
	SB_UI_CMD_GENRE_STATION,
	SB_UI_CMD_HISTORY,
	SB_UI_CMD_INFO,
	SB_UI_CMD_ADD_SHARED,
	SB_UI_CMD_SKIP,
	SB_UI_CMD_TOGGLE_PAUSE,
	SB_UI_CMD_QUIT,
	SB_UI_CMD_RENAME_STATION,
	SB_UI_CMD_SELECT_STATION,
	SB_UI_CMD_TIRED,
	SB_UI_CMD_UPCOMING,
	SB_UI_CMD_SELECT_QUICKMIX,
	SB_UI_CMD_DEBUG,
	SB_UI_CMD_BOOKMARK,
	SB_UI_CMD_VOLUME_DOWN,
	SB_UI_CMD_VOLUME_UP,
	SB_UI_CMD_MANAGE_STATION,
	SB_UI_CMD_CREATE_STATION_FROM_SONG,
	SB_UI_CMD_PLAY,
	SB_UI_CMD_PAUSE,
	SB_UI_CMD_VOLUME_RESET,
	SB_UI_CMD_SETTINGS,
	/* TUI-only contextual command: activate an already selected station. */
	SB_UI_CMD_ACTIVATE_STATION,
	SB_UI_CMD_COUNT,
} SbUiCommand;

typedef struct {
	SbUiCommand command;
	struct PianoStation *station;
} SbUiCommandEvent;

#endif /* SRC_UI_TYPES_H_2HR75RII */
