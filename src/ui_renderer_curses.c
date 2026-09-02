#include "config.h"

#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

#include "ui_dispatch.h"
#include "ui_renderer.h"

typedef struct {
	SCREEN *screen;
	pthread_mutex_t statusLock;
	char status[256];
	bool helpVisible;
	bool colors;
	bool selectionInitialized;
	size_t selectedIndex;
	size_t scrollOffset;
} SbUiCursesData;

static const SbUiRendererOps cursesOps;

static const char *SbUiCursesText (const char *text) {
	return text != NULL && *text != '\0' ? text : "--";
}

static void SbUiCursesPut (const int y, const int x, const int width,
		const char *text) {
	if (width > 0) {
		mvaddnstr (y, x, SbUiCursesText (text), width);
	}
}

static void SbUiCursesTime (char *dest, const size_t size,
		const unsigned int seconds) {
	snprintf (dest, size, "%02u:%02u", seconds / 60, seconds % 60);
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
		const size_t size) {
	snprintf (footer, size,
			"j/k stations  Enter tune  %c pause  %c next  %c love  %c ban  %c help  %c quit",
			SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE),
			SbUiCursesKey (renderer, SB_UI_CMD_SKIP),
			SbUiCursesKey (renderer, SB_UI_CMD_LOVE),
			SbUiCursesKey (renderer, SB_UI_CMD_BAN),
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
	if (width < 18) {
		return;
	}
	const int barWidth = width - 15;
	const unsigned int filled = model->duration > 0 ?
			(unsigned int) ((unsigned long long) model->elapsed * barWidth /
			model->duration) : 0;
	char elapsed[16], duration[16];
	SbUiCursesTime (elapsed, sizeof (elapsed), model->elapsed);
	SbUiCursesTime (duration, sizeof (duration), model->duration);
	mvaddch (y, x, '[');
	for (int i = 0; i < barWidth; i++) {
		addch ((unsigned int) i < filled ? '=' : '-');
	}
	addch (']');
	mvprintw (y, x + barWidth + 3, "%s / %s", elapsed, duration);
}

static void SbUiCursesFrame (const SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	int rows, cols;
	getmaxyx (stdscr, rows, cols);
	erase ();
	char footer[256];
	SbUiCursesFooter (renderer, footer, sizeof (footer));

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
	attron (A_BOLD | (data->colors ? COLOR_PAIR (1) : 0));
	SbUiCursesPut (1, 2, cols / 2, "SIGNALBOX");
	attroff (A_BOLD | (data->colors ? COLOR_PAIR (1) : 0));
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
	const char *playback = "Waiting";
	if (model->playback == SB_UI_PLAYBACK_PLAYING) {
		playback = "Playing";
	} else if (model->playback == SB_UI_PLAYBACK_PAUSED) {
		playback = "Paused";
	}
	SbUiCursesPut (statusY, 11, 9, playback);
	pthread_mutex_lock (&data->statusLock);
	SbUiCursesPut (statusY, 21, cols - 23, data->status);
	pthread_mutex_unlock (&data->statusLock);
	SbUiCursesPut (footerY, 2, cols - 4, footer);

	if (cols >= 80 && rows >= 20) {
		const int split = cols / 3;
		mvvline (3, split, ACS_VLINE, statusY - 4);
		attron (A_BOLD);
		SbUiCursesPut (4, 2, split - 3, "STATIONS");
		SbUiCursesPut (4, split + 2, cols - split - 4, "NOW PLAYING");
		attroff (A_BOLD);
		SbUiCursesStations (data, model, 6, 2, statusY - 7, split - 3);
		const int rightX = split + 2;
		const int rightWidth = cols - rightX - 2;
		SbUiCursesPut (6, rightX, rightWidth,
				model->song != NULL ? model->song->artist : NULL);
		attron (A_BOLD);
		SbUiCursesPut (8, rightX, rightWidth,
				model->song != NULL ? model->song->title : NULL);
		attroff (A_BOLD);
		SbUiCursesPut (10, rightX, rightWidth,
				model->song != NULL ? model->song->album : NULL);
		SbUiCursesPut (11, rightX, rightWidth, SbUiCursesRating (model->song));
		SbUiCursesProgress (model, 12, rightX, rightWidth);
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
		SbUiCursesPut (dividerY + 2, 2, cols - 4,
				model->song != NULL ? model->song->artist : NULL);
		SbUiCursesPut (dividerY + 3, 2, cols - 4,
				model->song != NULL ? model->song->title : NULL);
		if (dividerY + 5 < statusY - 1) {
			SbUiCursesPut (dividerY + 4, 2, cols - 4,
					SbUiCursesRating (model->song));
			SbUiCursesProgress (model, dividerY + 5, 2, cols - 4);
		} else {
			SbUiCursesProgress (model, dividerY + 4, 2, cols - 4);
		}
	}

	if (data->helpVisible) {
		const int height = 13;
		const int width = cols < 64 ? cols - 6 : 58;
		WINDOW * const help = newwin (height, width, (rows - height) / 2,
				(cols - width) / 2);
		if (help != NULL) {
			box (help, 0, 0);
			wattron (help, A_BOLD);
			mvwaddstr (help, 1, 2, "SIGNALBOX HELP");
			wattroff (help, A_BOLD);
			mvwaddstr (help, 3, 2, "Up/Down or j/k   select station");
			mvwaddstr (help, 4, 2, "Enter            tune selected station");
			mvwprintw (help, 5, 2, "%c                pause/resume",
					SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE));
			mvwprintw (help, 6, 2, "%c                next song",
					SbUiCursesKey (renderer, SB_UI_CMD_SKIP));
			mvwprintw (help, 7, 2, "%c                love song",
					SbUiCursesKey (renderer, SB_UI_CMD_LOVE));
			mvwprintw (help, 8, 2, "%c                ban song",
					SbUiCursesKey (renderer, SB_UI_CMD_BAN));
			mvwprintw (help, 9, 2, "%c                quit",
					quitKey != BAR_KS_DISABLED ? quitKey : '-');
			mvwprintw (help, 10, 2, "%c                close help",
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
				command == SB_UI_CMD_BAN) {
			return (SbUiCommandEvent) {command, NULL};
		}
	}
	return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
}

static void SbUiCursesMessage (SbUiRenderer *renderer, const BarUiMsg_t type,
		const char *format, va_list fmtargs) {
	(void) type;
	SbUiCursesData * const data = renderer->data;
	pthread_mutex_lock (&data->statusLock);
	vsnprintf (data->status, sizeof (data->status), format, fmtargs);
	data->status[strcspn (data->status, "\r\n")] = '\0';
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
		const BarSettings_t *settings) {
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
	cbreak ();
	noecho ();
	keypad (stdscr, TRUE);
	wtimeout (stdscr, 1000);
	(void) curs_set (0);
	if (has_colors ()) {
		start_color ();
#ifdef NCURSES_VERSION
		use_default_colors ();
#endif
		init_pair (1, COLOR_GREEN, -1);
		data->colors = true;
	}
	renderer->ops = &cursesOps;
	renderer->settings = settings;
	renderer->data = data;
	return true;
}
