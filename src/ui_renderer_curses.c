#define _XOPEN_SOURCE_EXTENDED 1
#include "config.h"

#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include <curses.h>

#include "ui_dispatch.h"
#include "ui_renderer.h"

typedef enum {
	SB_UI_NOTICE_INFO = 0,
	SB_UI_NOTICE_STATUS,
	SB_UI_NOTICE_WARNING,
	SB_UI_NOTICE_ERROR,
} SbUiNoticeSeverity;

typedef struct {
	SCREEN *screen;
	pthread_mutex_t statusLock;
	char status[256];
	SbUiNoticeSeverity statusSeverity;
	time_t statusExpires;
	bool helpVisible;
	bool colors;
	bool selectionInitialized;
	size_t selectedIndex;
	size_t scrollOffset;
} SbUiCursesData;

enum {
	SB_TUI_PAIR_ACCENT = 1,
	SB_TUI_PAIR_ERROR,
	SB_TUI_PAIR_WARNING,
};

static const SbUiRendererOps cursesOps;

static SbUiNoticeSeverity SbUiCursesNoticeSeverity (const BarUiMsg_t type) {
	if (type == MSG_ERR) return SB_UI_NOTICE_ERROR;
	if (type == MSG_QUESTION) return SB_UI_NOTICE_WARNING;
	if (type == MSG_PLAYING || type == MSG_TIME) return SB_UI_NOTICE_STATUS;
	return SB_UI_NOTICE_INFO;
}

static const char *SbUiCursesText (const char *text) {
	return text != NULL && *text != '\0' ? text : "--";
}

static void SbUiCursesPut (const int y, const int x, const int width,
		const char *text) {
	if (width <= 0) return;
	const char *source = SbUiCursesText (text);
	wchar_t wide[512];
	mbstate_t state;
	memset (&state, 0, sizeof (state));
	const size_t converted = mbsrtowcs (wide, &source,
			(sizeof (wide) / sizeof (*wide)) - 1, &state);
	if (converted == (size_t) -1) {
		mvaddnstr (y, x, text != NULL ? text : "--", width);
		return;
	}
	wide[converted] = L'\0';
	int cells = 0;
	size_t keep = 0;
	while (keep < converted) {
		const int charWidth = wcwidth (wide[keep]);
		if (charWidth < 0 || cells + charWidth > width) break;
		cells += charWidth;
		keep++;
	}
	if (keep < converted && width >= 2) {
		while (keep > 0 && cells > width - 1) {
			const int charWidth = wcwidth (wide[--keep]);
			if (charWidth > 0) cells -= charWidth;
		}
		wide[keep++] = L'\u2026';
	}
	wide[keep] = L'\0';
	mvaddnwstr (y, x, wide, (int) keep);
}

static void SbUiCursesTime (char *dest, const size_t size,
		const unsigned int seconds) {
	if (seconds >= 3600) {
		snprintf (dest, size, "%u:%02u:%02u", seconds / 3600,
				(seconds / 60) % 60, seconds % 60);
	} else {
		snprintf (dest, size, "%02u:%02u", seconds / 60, seconds % 60);
	}
}

static char SbUiCursesKey (const SbUiRenderer *renderer,
		const SbUiCommand command) {
	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (dispatchActions[i].command == command &&
				renderer->settings->keys[i] != BAR_KS_DISABLED) {
			return renderer->settings->keys[i];
		}
	}
	return '-';
}

static void SbUiCursesFooter (const SbUiRenderer *renderer, char *footer,
		const size_t size, const int width) {
	if (width < 76) {
		snprintf (footer, size, "j/k select  Enter tune  %c pause  %c next  %c help  %c quit",
				SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE),
				SbUiCursesKey (renderer, SB_UI_CMD_SKIP),
				SbUiCursesKey (renderer, SB_UI_CMD_HELP),
				SbUiCursesKey (renderer, SB_UI_CMD_QUIT));
		return;
	}
	snprintf (footer, size,
			"j/k stations  Enter tune  %c pause  %c next  %c/%c volume  %c help  %c quit",
			SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE),
			SbUiCursesKey (renderer, SB_UI_CMD_SKIP),
			SbUiCursesKey (renderer, SB_UI_CMD_VOLUME_DOWN),
			SbUiCursesKey (renderer, SB_UI_CMD_VOLUME_UP),
			SbUiCursesKey (renderer, SB_UI_CMD_HELP),
			SbUiCursesKey (renderer, SB_UI_CMD_QUIT));
}

static const char *SbUiCursesRating (const PianoSong_t *song) {
	if (song == NULL) return "Rating: Neutral";
	switch (song->rating) {
		case PIANO_RATE_LOVE: return "Rating: Loved";
		case PIANO_RATE_BAN: return "Rating: Banned";
		case PIANO_RATE_TIRED: return "Rating: Tired";
		default: return "Rating: Neutral";
	}
}

static size_t SbUiCursesStationCount (const SbUiModel *model) {
	size_t count = 0;
	const PianoStation_t *station = model->stations;
	PianoListForeachP (station) {
		count++;
	}
	return count;
}

static const PianoStation_t *SbUiCursesStationAt (const SbUiModel *model,
		const size_t index) {
	const PianoStation_t *station = model->stations;
	for (size_t i = 0; station != NULL && i < index; i++) {
		station = PianoListNextP (station);
	}
	return station;
}

static void SbUiCursesClampSelection (SbUiCursesData *data,
		const SbUiModel *model, const size_t visibleRows) {
	const size_t count = SbUiCursesStationCount (model);
	if (count == 0) {
		data->selectedIndex = data->scrollOffset = 0;
		data->selectionInitialized = false;
		return;
	}
	if (!data->selectionInitialized) {
		data->selectedIndex = 0;
		const PianoStation_t *station = model->stations;
		for (size_t i = 0; station != NULL; i++, station = PianoListNextP (station)) {
			if (station == model->station) {
				data->selectedIndex = i;
				break;
			}
		}
		data->selectionInitialized = true;
	}
	if (data->selectedIndex >= count) {
		data->selectedIndex = count - 1;
	}
	if (visibleRows == 0) {
		data->scrollOffset = data->selectedIndex;
	} else {
		if (data->selectedIndex < data->scrollOffset) {
			data->scrollOffset = data->selectedIndex;
		} else if (data->selectedIndex >= data->scrollOffset + visibleRows) {
			data->scrollOffset = data->selectedIndex - visibleRows + 1;
		}
		const size_t maxOffset = count > visibleRows ? count - visibleRows : 0;
		if (data->scrollOffset > maxOffset) {
			data->scrollOffset = maxOffset;
		}
	}
}

static void SbUiCursesStations (SbUiCursesData *data, const SbUiModel *model,
		const int y, const int x, const int height, const int width) {
	const size_t visible = height > 0 ? (size_t) height : 0;
	SbUiCursesClampSelection (data, model, visible);
	if (model->stations == NULL) {
		SbUiCursesPut (y, x, width, "No stations available");
		return;
	}
	for (size_t row = 0; row < visible; row++) {
		const size_t index = data->scrollOffset + row;
		const PianoStation_t * const station = SbUiCursesStationAt (model, index);
		if (station == NULL) {
			break;
		}
		mvaddch (y + (int) row, x, station == model->station ? '*' : ' ');
		if (index == data->selectedIndex) {
			attron (A_REVERSE);
		}
		SbUiCursesPut (y + (int) row, x + 2, width - 2, station->name);
		if (index == data->selectedIndex) {
			attroff (A_REVERSE);
		}
	}
}

static void SbUiCursesProgress (const SbUiModel *model, const int y,
		const int x, const int width) {
	char elapsed[16], duration[16];
	if (model->duration == 0) {
		strcpy (elapsed, "--:--");
		strcpy (duration, "--:--");
	} else {
		SbUiCursesTime (elapsed, sizeof (elapsed), model->elapsed);
		SbUiCursesTime (duration, sizeof (duration), model->duration);
	}
	char times[40];
	snprintf (times, sizeof (times), "%s / %s", elapsed, duration);
	const int timeWidth = (int) strlen (times);
	if (width < timeWidth + 5) {
		SbUiCursesPut (y, x, width, times);
		return;
	}
	const int barWidth = width - timeWidth - 3;
	unsigned int played = model->elapsed;
	if (model->duration > 0 && played > model->duration) {
		played = model->duration;
	}
	const unsigned int filled = model->duration > 0 ?
			(unsigned int) ((unsigned long long) played * (unsigned int) barWidth /
			model->duration) : 0;
	mvaddch (y, x, '[');
	for (int i = 0; i < barWidth; i++) {
		addch ((unsigned int) i < filled ? '=' : '-');
	}
	addch (']');
	SbUiCursesPut (y, x + barWidth + 3, timeWidth, times);
}

static const char *SbUiCursesPlayback (const SbUiModel *model) {
	if (model->playback == SB_UI_PLAYBACK_PLAYING) return "Playing";
	if (model->playback == SB_UI_PLAYBACK_PAUSED) return "Paused";
	return "Waiting";
}

static const char *SbUiCursesActivity (const SbUiModel *model) {
	switch (model->activity) {
		case SB_UI_ACTIVITY_REQUESTING: return "Requesting";
		case SB_UI_ACTIVITY_RECONNECTING: return "Reconnecting";
		case SB_UI_ACTIVITY_ERROR: return "Error";
		case SB_UI_ACTIVITY_WAITING_PLAYLIST: return "Waiting for playlist";
		default: return SbUiCursesPlayback (model);
	}
}

static void SbUiCursesNowPlaying (const SbUiModel *model, const int y,
		const int x, const int height, const int width) {
	if (height <= 0) return;
	attron (A_BOLD);
	SbUiCursesPut (y, x, width,
			model->song != NULL ? model->song->artist : NULL);
	if (height > 1) SbUiCursesPut (y + 1, x, width,
			model->song != NULL ? model->song->title : NULL);
	attroff (A_BOLD);
	if (height > 2) {
		char line[320];
		snprintf (line, sizeof (line), "Album: %s",
				model->song != NULL ? SbUiCursesText (model->song->album) : "--");
		SbUiCursesPut (y + 2, x, width, line);
	}
	if (height > 3) {
		const PianoStation_t * const station = model->songStation != NULL ?
				model->songStation : model->station;
		char line[320];
		snprintf (line, sizeof (line), "Station: %s",
				station != NULL ? SbUiCursesText (station->name) : "--");
		SbUiCursesPut (y + 3, x, width, line);
	}
	if (height > 5) SbUiCursesProgress (model, y + 5, x, width);
	if (height > 6) {
		char state[128];
		snprintf (state, sizeof (state), "%s   Volume %+d dB   %s",
				SbUiCursesPlayback (model), model->volumeDb,
				SbUiCursesRating (model->song));
		SbUiCursesPut (y + 6, x, width, state);
	}
}

static void SbUiCursesHistory (const SbUiModel *model, const int y,
		const int x, const int height, const int width) {
	if (height <= 0) return;
	if (model->historyCount == 0) {
		SbUiCursesPut (y, x, width, "No previous tracks this session");
		return;
	}
	const size_t rows = model->historyCount < (size_t) height ?
			model->historyCount : (size_t) height;
	for (size_t i = 0; i < rows; i++) {
		char line[SB_UI_HISTORY_TEXT_MAX * 2 + 8];
		snprintf (line, sizeof (line), "%s — %s", model->history[i].artist,
				model->history[i].title);
		SbUiCursesPut (y + (int) i, x, width, line);
	}
}

static void SbUiCursesFrame (const SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	int rows, cols;
	getmaxyx (stdscr, rows, cols);
	erase ();
	char footer[256];
	SbUiCursesFooter (renderer, footer, sizeof (footer), cols);

	if (rows < 15 || cols < 50) {
		box (stdscr, 0, 0);
		attron (A_BOLD);
		SbUiCursesPut (2, 3, cols - 6, "SIGNALBOX");
		attroff (A_BOLD);
		SbUiCursesPut (5, 3, cols - 6,
				"Terminal too small for Signalbox TUI");
		mvprintw (7, 3, "Need 50x15; current size is %dx%d", cols, rows);
		SbUiCursesPut (rows - 2, 3, cols - 6, footer);
		refresh ();
		return;
	}

	box (stdscr, 0, 0);
	attron (A_BOLD | (data->colors ? COLOR_PAIR (SB_TUI_PAIR_ACCENT) : 0));
	SbUiCursesPut (1, 2, cols / 2, "SIGNALBOX");
	attroff (A_BOLD | (data->colors ? COLOR_PAIR (SB_TUI_PAIR_ACCENT) : 0));
	SbUiCursesPut (1, cols - 15, 13, "PANDORA RADIO");
	mvhline (2, 1, ACS_HLINE, cols - 2);

	const int footerY = rows - 2;
	const int statusY = rows - 4;
	const char quitKey = renderer->settings->keys[BAR_KS_QUIT];
	const char helpKey = renderer->settings->keys[BAR_KS_HELP];
	mvhline (statusY - 1, 1, ACS_HLINE, cols - 2);
	mvhline (footerY - 1, 1, ACS_HLINE, cols - 2);
	attron (A_BOLD);
	SbUiCursesPut (statusY, 2, 8, "STATUS");
	attroff (A_BOLD);
	SbUiCursesPut (statusY, 11, 20, SbUiCursesActivity (model));
	pthread_mutex_lock (&data->statusLock);
	if (data->statusExpires != 0 && time (NULL) >= data->statusExpires) {
		data->status[0] = '\0';
		data->statusExpires = 0;
	}
	if (data->statusSeverity >= SB_UI_NOTICE_WARNING &&
			data->status[0] != '\0') {
		const int pair = data->statusSeverity == SB_UI_NOTICE_ERROR ?
				SB_TUI_PAIR_ERROR : SB_TUI_PAIR_WARNING;
		attron (A_BOLD | (data->colors ? COLOR_PAIR (pair) : 0));
	}
	SbUiCursesPut (statusY, 32, cols - 34,
			data->status[0] != '\0' ? data->status : "Ready");
	if (data->statusSeverity >= SB_UI_NOTICE_WARNING &&
			data->status[0] != '\0') {
		const int pair = data->statusSeverity == SB_UI_NOTICE_ERROR ?
				SB_TUI_PAIR_ERROR : SB_TUI_PAIR_WARNING;
		attroff (A_BOLD | (data->colors ? COLOR_PAIR (pair) : 0));
	}
	pthread_mutex_unlock (&data->statusLock);
	SbUiCursesPut (footerY, 2, cols - 4, footer);

	if (cols >= 100 && rows >= 24) {
		const int split = cols / 3;
		mvvline (3, split, ACS_VLINE, statusY - 4);
		const int historyY = statusY - 5;
		mvhline (historyY - 1, split + 1, ACS_HLINE, cols - split - 2);
		attron (A_BOLD);
		SbUiCursesPut (4, 2, split - 3, "STATIONS");
		SbUiCursesPut (4, split + 2, cols - split - 4, "NOW PLAYING");
		SbUiCursesPut (historyY, split + 2, cols - split - 4, "RECENT");
		attroff (A_BOLD);
		SbUiCursesStations (data, model, 6, 2, statusY - 7, split - 3);
		const int rightX = split + 2;
		const int rightWidth = cols - rightX - 2;
		SbUiCursesNowPlaying (model, 6, rightX, historyY - 8, rightWidth);
		SbUiCursesHistory (model, historyY + 1, rightX,
				statusY - historyY - 2, rightWidth);
	} else if (cols >= 80 && rows >= 20) {
		const int split = cols / 3;
		mvvline (3, split, ACS_VLINE, statusY - 4);
		attron (A_BOLD);
		SbUiCursesPut (4, 2, split - 3, "STATIONS");
		SbUiCursesPut (4, split + 2, cols - split - 4, "NOW PLAYING");
		attroff (A_BOLD);
		SbUiCursesStations (data, model, 6, 2, statusY - 7, split - 3);
		SbUiCursesNowPlaying (model, 6, split + 2, statusY - 7,
				cols - split - 4);
	} else {
		attron (A_BOLD);
		SbUiCursesPut (4, 2, cols - 4, "STATION");
		attroff (A_BOLD);
		const int stationRows = (statusY - 9) / 2;
		SbUiCursesStations (data, model, 5, 2, stationRows, cols - 4);
		const int dividerY = 5 + stationRows;
		mvhline (dividerY, 1, ACS_HLINE, cols - 2);
		attron (A_BOLD);
		SbUiCursesPut (dividerY + 1, 2, cols - 4, "NOW PLAYING");
		attroff (A_BOLD);
		SbUiCursesNowPlaying (model, dividerY + 2, 2,
				statusY - dividerY - 3, cols - 4);
	}

	if (data->helpVisible) {
		const int height = 15;
		const int width = cols < 64 ? cols - 6 : 58;
		WINDOW * const help = newwin (height, width, (rows - height) / 2,
				(cols - width) / 2);
		if (help != NULL) {
			box (help, 0, 0);
			wattron (help, A_BOLD);
			mvwaddstr (help, 1, 2, "SIGNALBOX HELP");
			wattroff (help, A_BOLD);
			mvwaddstr (help, 2, 2, "NAVIGATION");
			mvwaddstr (help, 3, 2, "Up/Down or j/k select; Enter tunes");
			mvwaddstr (help, 4, 2, "PLAYBACK");
			mvwprintw (help, 5, 2, "%c pause/resume   %c next   %c love   %c ban",
					SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE),
					SbUiCursesKey (renderer, SB_UI_CMD_SKIP),
					SbUiCursesKey (renderer, SB_UI_CMD_LOVE),
					SbUiCursesKey (renderer, SB_UI_CMD_BAN));
			mvwaddstr (help, 6, 2, "STATIONS");
			mvwprintw (help, 7, 2, "%c create  %c genre  %c shared  %c add music",
					SbUiCursesKey (renderer, SB_UI_CMD_CREATE_STATION),
					SbUiCursesKey (renderer, SB_UI_CMD_GENRE_STATION),
					SbUiCursesKey (renderer, SB_UI_CMD_ADD_SHARED),
					SbUiCursesKey (renderer, SB_UI_CMD_ADD_MUSIC));
			mvwprintw (help, 8, 2, "%c rename  %c delete  %c QuickMix  %c manage",
					SbUiCursesKey (renderer, SB_UI_CMD_RENAME_STATION),
					SbUiCursesKey (renderer, SB_UI_CMD_DELETE_STATION),
					SbUiCursesKey (renderer, SB_UI_CMD_SELECT_QUICKMIX),
					SbUiCursesKey (renderer, SB_UI_CMD_MANAGE_STATION));
			mvwaddstr (help, 9, 2, "LIBRARY");
			mvwprintw (help, 10, 2, "%c bookmark  %c from song  %c history  %c upcoming",
					SbUiCursesKey (renderer, SB_UI_CMD_BOOKMARK),
					SbUiCursesKey (renderer, SB_UI_CMD_CREATE_STATION_FROM_SONG),
					SbUiCursesKey (renderer, SB_UI_CMD_HISTORY),
					SbUiCursesKey (renderer, SB_UI_CMD_UPCOMING));
			mvwprintw (help, 12, 2, "%c quit   %c close help",
					quitKey != BAR_KS_DISABLED ? quitKey : '-',
					helpKey != BAR_KS_DISABLED ? helpKey : '-');
			wnoutrefresh (stdscr);
			wnoutrefresh (help);
			doupdate ();
			delwin (help);
			return;
		}
	}
	refresh ();
}

static WINDOW *SbUiCursesModal (const char *title, const char *prompt,
		const int wantedHeight) {
	int rows, cols;
	getmaxyx (stdscr, rows, cols);
	if (rows < 15 || cols < 50) return NULL;
	const int width = cols < 72 ? cols - 4 : 68;
	const int height = wantedHeight < rows - 2 ? wantedHeight : rows - 2;
	WINDOW *window = newwin (height, width, (rows - height) / 2,
			(cols - width) / 2);
	if (window != NULL) {
		box (window, 0, 0);
		wattron (window, A_BOLD);
		mvwaddnstr (window, 1, 2, title, width - 4);
		wattroff (window, A_BOLD);
		mvwaddnstr (window, 3, 2, prompt, width - 4);
		keypad (window, TRUE);
		wnoutrefresh (stdscr);
	}
	return window;
}

bool SbUiRendererPromptText (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *prompt, char *buffer, const size_t size) {
	if (!SbUiRendererIsCurses (renderer) || size < 2) return false;
	wchar_t input[256];
	mbstate_t state;
	memset (&state, 0, sizeof (state));
	const char *source = buffer;
	size_t length = mbsrtowcs (input, &source,
			sizeof (input) / sizeof (*input) - 1, &state);
	if (length == (size_t) -1) length = 0;
	size_t cursor = length;
	input[length] = L'\0';
	(void) curs_set (1);
	for (;;) {
		SbUiCursesFrame (renderer, model);
		WINDOW *window = SbUiCursesModal (title, prompt, 8);
		if (window == NULL) continue;
		int height, width;
		getmaxyx (window, height, width);
		(void) height;
		mvwhline (window, 5, 2, ' ', width - 4);
		size_t start = 0;
		while (start < cursor && wcswidth (&input[start], cursor - start) > width - 5) {
			start++;
		}
		mvwaddnwstr (window, 5, 2, &input[start], width - 5);
		mvwaddnstr (window, 6, 2, "Enter: submit   Esc: cancel", width - 4);
		const int cursorCells = wcswidth (&input[start], cursor - start);
		wmove (window, 5, 2 + (cursorCells >= 0 ? cursorCells : 0));
		wrefresh (window);
		wint_t key;
		const int result = wget_wch (window, &key);
		delwin (window);
		if (result == ERR) continue;
		if (key == 27) {
			(void) curs_set (0);
			SbUiCursesFrame (renderer, model);
			return false;
		}
		if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			memset (&state, 0, sizeof (state));
			const wchar_t *wide = input;
			const size_t converted = wcsrtombs (buffer, &wide, size - 1, &state);
			if (converted == (size_t) -1) buffer[0] = '\0';
			else buffer[converted] = '\0';
			(void) curs_set (0);
			SbUiCursesFrame (renderer, model);
			return buffer[0] != '\0';
		}
		if (key == KEY_LEFT && cursor > 0) cursor--;
		else if (key == KEY_RIGHT && cursor < length) cursor++;
		else if (key == KEY_HOME) cursor = 0;
		else if (key == KEY_END) cursor = length;
		else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && cursor > 0) {
			memmove (&input[cursor - 1], &input[cursor],
					(length - cursor + 1) * sizeof (*input));
			cursor--; length--;
		} else if (key == KEY_DC && cursor < length) {
			memmove (&input[cursor], &input[cursor + 1],
					(length - cursor) * sizeof (*input));
			length--;
		} else if (result == OK && iswprint ((wint_t) key) &&
				length + 1 < sizeof (input) / sizeof (*input)) {
			memmove (&input[cursor + 1], &input[cursor],
					(length - cursor + 1) * sizeof (*input));
			input[cursor++] = (wchar_t) key;
			length++;
		}
	}
}

bool SbUiRendererConfirm (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *prompt) {
	if (!SbUiRendererIsCurses (renderer)) return false;
	bool yes = false;
	for (;;) {
		SbUiCursesFrame (renderer, model);
		WINDOW *window = SbUiCursesModal (title, prompt, 8);
		if (window == NULL) continue;
		mvwprintw (window, 5, 2, "%s   %s", yes ? "[YES]" : " Yes ",
				yes ? " No " : "[NO]");
		mvwaddnstr (window, 6, 2,
				"y/n or arrows; Enter confirms selection; Esc cancels", getmaxx (window) - 4);
		wrefresh (window);
		const int key = wgetch (window);
		delwin (window);
		if (key == 27 || key == 'n' || key == 'N') return false;
		if (key == 'y' || key == 'Y') return true;
		if (key == KEY_LEFT || key == KEY_RIGHT || key == '\t') yes = !yes;
		if (key == '\n' || key == '\r' || key == KEY_ENTER) return yes;
	}
}

int SbUiRendererSelectList (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *const *items, const size_t count) {
	if (!SbUiRendererIsCurses (renderer) || count == 0) return -1;
	size_t selected = 0, offset = 0;
	for (;;) {
		SbUiCursesFrame (renderer, model);
		int rows, cols;
		getmaxyx (stdscr, rows, cols);
		const int height = rows < 22 ? rows - 2 : 20;
		WINDOW *window = SbUiCursesModal (title,
				"Up/Down or j/k; Enter selects; Esc cancels", height);
		if (window == NULL) continue;
		int wh, ww;
		getmaxyx (window, wh, ww);
		const size_t visible = wh > 6 ? (size_t) wh - 6 : 1;
		if (selected < offset) offset = selected;
		if (selected >= offset + visible) offset = selected - visible + 1;
		for (size_t row = 0; row < visible && offset + row < count; row++) {
			if (offset + row == selected) wattron (window, A_REVERSE);
			const char *item = items[offset + row] != NULL ?
					items[offset + row] : "(unavailable)";
			mvwaddnstr (window, 5 + (int) row, 2, item, ww - 4);
			if (offset + row == selected) wattroff (window, A_REVERSE);
		}
		wrefresh (window);
		const int key = wgetch (window);
		delwin (window);
		if (key == 27) return -1;
		if ((key == KEY_UP || key == 'k') && selected > 0) selected--;
		else if ((key == KEY_DOWN || key == 'j') && selected + 1 < count) selected++;
		else if (key == KEY_HOME) selected = 0;
		else if (key == KEY_END) selected = count - 1;
		else if (key == '\n' || key == '\r' || key == KEY_ENTER) return (int) selected;
	}
}

/* The checked array is caller-owned scratch state.  Canonical station flags
 * are deliberately not exposed to or changed by the renderer. */
bool SbUiRendererToggleList (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *const *items, bool *checked,
		const size_t count) {
	if (!SbUiRendererIsCurses (renderer) || count == 0 || checked == NULL) {
		return false;
	}
	size_t selected = 0, offset = 0;
	for (;;) {
		SbUiCursesFrame (renderer, model);
		int rows, cols;
		getmaxyx (stdscr, rows, cols);
		const int height = rows < 22 ? rows - 2 : 20;
		WINDOW *window = SbUiCursesModal (title,
				"Up/Down or j/k; Space toggles; Enter saves; Esc cancels", height);
		if (window == NULL) continue;
		int wh, ww;
		getmaxyx (window, wh, ww);
		const size_t visible = wh > 6 ? (size_t) wh - 6 : 1;
		if (selected < offset) offset = selected;
		if (selected >= offset + visible) offset = selected - visible + 1;
		for (size_t row = 0; row < visible && offset + row < count; row++) {
			const size_t index = offset + row;
			if (index == selected) wattron (window, A_REVERSE);
			mvwprintw (window, 5 + (int) row, 2, "[%c] ",
					checked[index] ? 'x' : ' ');
			mvwaddnstr (window, 5 + (int) row, 6,
					items[index] != NULL ? items[index] : "(unavailable)", ww - 8);
			if (index == selected) wattroff (window, A_REVERSE);
		}
		wrefresh (window);
		const int key = wgetch (window);
		delwin (window);
		if (key == 27) return false;
		if ((key == KEY_UP || key == 'k') && selected > 0) selected--;
		else if ((key == KEY_DOWN || key == 'j') && selected + 1 < count) selected++;
		else if (key == KEY_HOME) selected = 0;
		else if (key == KEY_END) selected = count - 1;
		else if (key == ' ') checked[selected] = !checked[selected];
		else if (key == '\n' || key == '\r' || key == KEY_ENTER) return true;
	}
}

static void SbUiCursesInit (SbUiRenderer *renderer) {
	(void) renderer;
}

static void SbUiCursesRender (SbUiRenderer *renderer,
		const SbUiModel *model, const SbUiRenderEvent event) {
	(void) event;
	SbUiCursesFrame (renderer, model);
}

static SbUiCommandEvent SbUiCursesReadCommand (SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	const int key = getch ();
	if (key == ERR) {
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (key == KEY_RESIZE) {
		clearok (stdscr, TRUE);
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && (key == KEY_UP || key == 'k' ||
			key == KEY_DOWN || key == 'j' || key == KEY_HOME ||
			key == KEY_END || key == KEY_PPAGE || key == KEY_NPAGE)) {
		const size_t count = SbUiCursesStationCount (model);
		SbUiCursesClampSelection (data, model, 1);
		if (count > 0) {
			if (key == KEY_UP || key == 'k') {
				if (data->selectedIndex > 0) data->selectedIndex--;
			} else if (key == KEY_DOWN || key == 'j') {
				if (data->selectedIndex + 1 < count) data->selectedIndex++;
			} else if (key == KEY_HOME) {
				data->selectedIndex = 0;
			} else if (key == KEY_END) {
				data->selectedIndex = count - 1;
			} else if (key == KEY_PPAGE) {
				data->selectedIndex = data->selectedIndex > 5 ?
						data->selectedIndex - 5 : 0;
			} else if (key == KEY_NPAGE) {
				data->selectedIndex = data->selectedIndex + 5 < count ?
						data->selectedIndex + 5 : count - 1;
			}
		}
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && (key == '\n' || key == '\r' || key == KEY_ENTER)) {
		const PianoStation_t * const station = SbUiCursesStationAt (model,
				data->selectedIndex);
		return (SbUiCommandEvent) {station != NULL ?
				SB_UI_CMD_ACTIVATE_STATION : SB_UI_CMD_NONE,
				(PianoStation_t *) station};
	}
	if (key >= 0 && key <= UCHAR_MAX) {
		const SbUiCommand command = BarUiCommandFromKey (renderer->settings,
				(char) key);
		if (command == SB_UI_CMD_HELP) {
			data->helpVisible = !data->helpVisible;
			SbUiCursesFrame (renderer, model);
			return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
		}
		if (data->helpVisible) {
			data->helpVisible = false;
			SbUiCursesFrame (renderer, model);
			return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
		}
		/* Only actions audited as prompt-free are allowed to leave the renderer.
		 * The classic station-select key is intentionally consumed with guidance. */
		if (command == SB_UI_CMD_SELECT_STATION) {
			pthread_mutex_lock (&data->statusLock);
			strcpy (data->status, "Use arrows or j/k, then Enter to tune");
			pthread_mutex_unlock (&data->statusLock);
			SbUiCursesFrame (renderer, model);
			return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
		}
		if (command == SB_UI_CMD_QUIT || command == SB_UI_CMD_TOGGLE_PAUSE ||
				command == SB_UI_CMD_PLAY || command == SB_UI_CMD_PAUSE ||
				command == SB_UI_CMD_SKIP || command == SB_UI_CMD_LOVE ||
				command == SB_UI_CMD_BAN || command == SB_UI_CMD_VOLUME_DOWN ||
				command == SB_UI_CMD_VOLUME_UP ||
				command == SB_UI_CMD_VOLUME_RESET ||
				command == SB_UI_CMD_ADD_MUSIC ||
				command == SB_UI_CMD_CREATE_STATION ||
				command == SB_UI_CMD_GENRE_STATION ||
				command == SB_UI_CMD_HISTORY ||
				command == SB_UI_CMD_ADD_SHARED ||
				command == SB_UI_CMD_UPCOMING ||
				command == SB_UI_CMD_SELECT_QUICKMIX ||
				command == SB_UI_CMD_BOOKMARK ||
				command == SB_UI_CMD_MANAGE_STATION ||
				command == SB_UI_CMD_CREATE_STATION_FROM_SONG ||
				command == SB_UI_CMD_RENAME_STATION ||
				command == SB_UI_CMD_DELETE_STATION) {
			const PianoStation_t *station = NULL;
			if (command == SB_UI_CMD_ADD_MUSIC ||
					command == SB_UI_CMD_RENAME_STATION ||
					command == SB_UI_CMD_DELETE_STATION ||
					command == SB_UI_CMD_SELECT_QUICKMIX ||
					command == SB_UI_CMD_MANAGE_STATION) {
				station = SbUiCursesStationAt (model, data->selectedIndex);
			}
			return (SbUiCommandEvent) {command, (PianoStation_t *) station};
		}
	}
	return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
}

static void SbUiCursesMessage (SbUiRenderer *renderer, const BarUiMsg_t type,
		const char *format, va_list fmtargs) {
	SbUiCursesData * const data = renderer->data;
	pthread_mutex_lock (&data->statusLock);
	vsnprintf (data->status, sizeof (data->status), format, fmtargs);
	data->status[strcspn (data->status, "\r\n")] = '\0';
	data->statusSeverity = SbUiCursesNoticeSeverity (type);
	if (type == MSG_NONE && (strncmp (data->status, "Error:", 6) == 0 ||
			strncmp (data->status, "Network error:", 14) == 0)) {
		data->statusSeverity = SB_UI_NOTICE_ERROR;
	}
	/* Errors remain visible longer; all other messages are concise transient
	 * notices. The main thread expires them during its existing 1 Hz redraw. */
	data->statusExpires = time (NULL) +
			(data->statusSeverity == SB_UI_NOTICE_ERROR ? 8 : 4);
	pthread_mutex_unlock (&data->statusLock);
}

static void SbUiCursesShutdown (SbUiRenderer *renderer) {
	SbUiCursesData * const data = renderer->data;
	if (data != NULL) {
		endwin ();
		delscreen (data->screen);
		pthread_mutex_destroy (&data->statusLock);
		free (data);
		renderer->data = NULL;
	}
}

static const SbUiRendererOps cursesOps = {
	.init = SbUiCursesInit,
	.render = SbUiCursesRender,
	.readCommand = SbUiCursesReadCommand,
	.message = SbUiCursesMessage,
	.shutdown = SbUiCursesShutdown,
};

bool SbUiRendererInitCurses (SbUiRenderer *renderer,
		const BarSettings_t *settings, const SbTuiTheme theme) {
	assert (renderer != NULL);
	assert (settings != NULL);
	setlocale (LC_ALL, "");
	SbUiCursesData * const data = calloc (1, sizeof (*data));
	if (data == NULL) {
		return false;
	}
	data->screen = newterm (NULL, stdout, stdin);
	if (data->screen == NULL) {
		free (data);
		return false;
	}
	set_term (data->screen);
	if (pthread_mutex_init (&data->statusLock, NULL) != 0) {
		endwin ();
		delscreen (data->screen);
		free (data);
		return false;
	}
	strcpy (data->status, "Ready");
	data->statusSeverity = SB_UI_NOTICE_INFO;
	data->statusExpires = time (NULL) + 4;
	cbreak ();
	noecho ();
	keypad (stdscr, TRUE);
	wtimeout (stdscr, 1000);
	(void) curs_set (0);
	if (has_colors () && theme != SB_TUI_THEME_MONO && getenv ("NO_COLOR") == NULL) {
		start_color ();
#ifdef NCURSES_VERSION
		use_default_colors ();
#endif
		const short accent = theme == SB_TUI_THEME_AMBER ? COLOR_YELLOW :
				(theme == SB_TUI_THEME_NEUTRAL ? COLOR_CYAN : COLOR_GREEN);
		init_pair (SB_TUI_PAIR_ACCENT, accent, -1);
		init_pair (SB_TUI_PAIR_ERROR, COLOR_RED, -1);
		init_pair (SB_TUI_PAIR_WARNING, COLOR_YELLOW, -1);
		data->colors = true;
	}
	renderer->ops = &cursesOps;
	renderer->settings = settings;
	renderer->data = data;
	return true;
}
