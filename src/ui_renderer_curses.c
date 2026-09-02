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

static void SbUiCursesFooter (const SbUiRenderer *renderer, char *footer,
		const size_t size) {
	const char quitKey = renderer->settings->keys[BAR_KS_QUIT];
	const char helpKey = renderer->settings->keys[BAR_KS_HELP];
	if (quitKey != BAR_KS_DISABLED && helpKey != BAR_KS_DISABLED) {
		snprintf (footer, size, "%c quit   %c help", quitKey, helpKey);
	} else if (quitKey != BAR_KS_DISABLED) {
		snprintf (footer, size, "%c quit", quitKey);
	} else if (helpKey != BAR_KS_DISABLED) {
		snprintf (footer, size, "%c help", helpKey);
	} else {
		snprintf (footer, size, "Ctrl-C quit");
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
	char footer[64];
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
		SbUiCursesPut (6, 2, split - 3,
				model->station != NULL ? model->station->name : NULL);
		SbUiCursesPut (8, 2, split - 3, "station browser coming next");
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
		SbUiCursesProgress (model, 12, rightX, rightWidth);
	} else {
		attron (A_BOLD);
		SbUiCursesPut (4, 2, cols - 4, "STATION");
		attroff (A_BOLD);
		SbUiCursesPut (5, 2, cols - 4,
				model->station != NULL ? model->station->name : NULL);
		mvhline (7, 1, ACS_HLINE, cols - 2);
		attron (A_BOLD);
		SbUiCursesPut (8, 2, cols - 4, "NOW PLAYING");
		attroff (A_BOLD);
		SbUiCursesPut (9, 2, cols - 4,
				model->song != NULL ? model->song->artist : NULL);
		SbUiCursesPut (10, 2, cols - 4,
				model->song != NULL ? model->song->title : NULL);
		SbUiCursesProgress (model, 12, 2, cols - 4);
	}

	if (data->helpVisible) {
		const int height = 7;
		const int width = cols < 64 ? cols - 6 : 58;
		WINDOW * const help = newwin (height, width, (rows - height) / 2,
				(cols - width) / 2);
		if (help != NULL) {
			box (help, 0, 0);
			wattron (help, A_BOLD);
			mvwaddstr (help, 1, 2, "SIGNALBOX HELP");
			wattroff (help, A_BOLD);
			mvwprintw (help, 3, 2, "%c   quit",
					quitKey != BAR_KS_DISABLED ? quitKey : '-');
			mvwprintw (help, 4, 2, "%c   close help",
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

static SbUiCommand SbUiCursesReadCommand (SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	const int key = getch ();
	if (key == ERR) {
		SbUiCursesFrame (renderer, model);
		return SB_UI_CMD_NONE;
	}
	if (key == KEY_RESIZE) {
		clearok (stdscr, TRUE);
		SbUiCursesFrame (renderer, model);
		return SB_UI_CMD_NONE;
	}
	if (key >= 0 && key <= UCHAR_MAX) {
		const SbUiCommand command = BarUiCommandFromKey (renderer->settings,
				(char) key);
		if (command == SB_UI_CMD_HELP) {
			data->helpVisible = !data->helpVisible;
			SbUiCursesFrame (renderer, model);
			return SB_UI_CMD_NONE;
		}
		if (data->helpVisible) {
			data->helpVisible = false;
			SbUiCursesFrame (renderer, model);
			return SB_UI_CMD_NONE;
		}
		/* Phase C0 exposes only commands that cannot enter inherited blocking
		 * prompts or write classic-mode output into the curses screen. */
		return command == SB_UI_CMD_QUIT ? command : SB_UI_CMD_NONE;
	}
	return SB_UI_CMD_NONE;
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
