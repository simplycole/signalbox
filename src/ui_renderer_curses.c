#define _XOPEN_SOURCE_EXTENDED 1
#include "config.h"
#include "credential.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#ifdef SIGNALBOX_PDCURSESMOD
#include <windows.h>
#include <pdcurses.h>
#if !defined(PDC_WIDE) || !defined(PDC_FORCE_UTF8)
#error "Signalbox requires PDCursesMod built with WIDE=Y and UTF8=Y"
#endif
#else
#include <curses.h>
#endif

#include "debug.h"
#include "station_browser.h"
#include "ui_dispatch.h"
#include "ui_renderer.h"
#include "platform.h"

typedef enum {
	SB_UI_NOTICE_INFO = 0,
	SB_UI_NOTICE_STATUS,
	SB_UI_NOTICE_WARNING,
	SB_UI_NOTICE_ERROR,
} SbUiNoticeSeverity;

typedef enum {
	SB_TUI_COLOR_BORDER = 0,
	SB_TUI_COLOR_TITLE,
	SB_TUI_COLOR_SECTION,
	SB_TUI_COLOR_PRIMARY,
	SB_TUI_COLOR_MUTED,
	SB_TUI_COLOR_STATION,
	SB_TUI_COLOR_STATION_ACTIVE,
	SB_TUI_COLOR_SELECTED,
	SB_TUI_COLOR_ARTIST,
	SB_TUI_COLOR_TRACK,
	SB_TUI_COLOR_ALBUM,
	SB_TUI_COLOR_PROGRESS_FILLED,
	SB_TUI_COLOR_STATUS,
	SB_TUI_COLOR_WARNING,
	SB_TUI_COLOR_ERROR,
	SB_TUI_COLOR_LOVED,
	SB_TUI_COLOR_KEY,
	SB_TUI_COLOR_COUNT,
} SbTuiColorRole;

typedef struct {
	short colors[SB_TUI_COLOR_COUNT];
} SbTuiPalette;

typedef enum {
	SB_TUI_FOCUS_STATIONS = 0,
	SB_TUI_FOCUS_RECENT,
} SbTuiFocus;

typedef struct {
	SCREEN *screen;
	pthread_mutex_t statusLock;
	char status[256];
	SbUiNoticeSeverity statusSeverity;
	time_t statusExpires;
	bool helpVisible;
	size_t helpOffset;
	bool colors;
	attr_t roleAttrs[SB_TUI_COLOR_COUNT];
	bool selectionInitialized;
	size_t selectedIndex;
	size_t scrollOffset;
	SbStationBrowser browser;
	bool jumpMode;
	bool unicodeSymbols;
	bool unicodeBlocks;
	SbTuiFocus focus;
	size_t recentSelected;
	size_t recentOffset;
	size_t observedHistoryCount;
	int screenRows;
	int screenCols;
} SbUiCursesData;

static bool SbUiCursesVisualizerKeyAvailable (const SbUiRenderer *renderer) {
	return BarUiCommandFromKey (renderer->settings, 'V') == SB_UI_CMD_NONE;
}

static attr_t SbUiCursesRole (const SbUiCursesData *data,
		const SbTuiColorRole role) {
	return data->colors ? data->roleAttrs[role] : 0;
}

static void SbUiCursesAttrOn (const SbUiCursesData *data,
		const SbTuiColorRole role, const attr_t extra) {
	attron (SbUiCursesRole (data, role) | extra);
}

static void SbUiCursesAttrOff (const SbUiCursesData *data,
		const SbTuiColorRole role, const attr_t extra) {
	attroff (SbUiCursesRole (data, role) | extra);
}

static void SbUiCursesWAttrOn (WINDOW *window, const SbUiCursesData *data,
		const SbTuiColorRole role, const attr_t extra) {
	wattron (window, SbUiCursesRole (data, role) | extra);
}

static void SbUiCursesWAttrOff (WINDOW *window, const SbUiCursesData *data,
		const SbTuiColorRole role, const attr_t extra) {
	wattroff (window, SbUiCursesRole (data, role) | extra);
}

static const SbUiRendererOps cursesOps;

static int SbUiCursesTextWidth (const char *text);

static size_t SbUiCursesUtf8ToWide (wchar_t *dest, const size_t destSize,
		const char *source) {
	if (destSize == 0) return (size_t) -1;
#ifdef SIGNALBOX_PDCURSESMOD
	if (source == NULL || strlen (source) > INT_MAX) return (size_t) -1;
	const int converted = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
			source, -1, dest, (int) destSize);
	if (converted == 0) return (size_t) -1;
	return (size_t) converted - 1;
#else
	mbstate_t state;
	memset (&state, 0, sizeof (state));
	const char *next = source;
	const size_t converted = mbsrtowcs (dest, &next, destSize - 1, &state);
	if (converted == (size_t) -1) return converted;
	dest[converted] = L'\0';
	return converted;
#endif
}

static size_t SbUiCursesWideToUtf8 (char *dest, const size_t destSize,
		const wchar_t *source) {
	if (destSize == 0) return (size_t) -1;
#ifdef SIGNALBOX_PDCURSESMOD
	const int converted = WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS,
			source, -1, dest, (int) destSize, NULL, NULL);
	if (converted == 0) return (size_t) -1;
	return (size_t) converted - 1;
#else
	mbstate_t state;
	memset (&state, 0, sizeof (state));
	const wchar_t *next = source;
	const size_t converted = wcsrtombs (dest, &next, destSize - 1, &state);
	if (converted == (size_t) -1) return converted;
	dest[converted] = '\0';
	return converted;
#endif
}

static void SbUiCursesBox (WINDOW *window) {
#ifdef SIGNALBOX_PDCURSESMOD
	/* Keep W2 structure ASCII.  Mixing WACS drawing and text APIs on the VT
	 * backend produced UTF-8 bytes as individual screen cells. */
	(void) wborder (window, '|', '|', '-', '-', '+', '+', '+', '+');
#else
	(void) box (window, 0, 0);
#endif
}

static void SbUiCursesHLine (WINDOW *window, const int y, const int x,
		const int length) {
#ifdef SIGNALBOX_PDCURSESMOD
	(void) mvwhline (window, y, x, '-', length);
#else
	(void) mvwhline (window, y, x, ACS_HLINE, length);
#endif
}

static void SbUiCursesVLine (WINDOW *window, const int y, const int x,
		const int length) {
#ifdef SIGNALBOX_PDCURSESMOD
	(void) mvwvline (window, y, x, '|', length);
#else
	(void) mvwvline (window, y, x, ACS_VLINE, length);
#endif
}

static int SbUiCursesCellWidth (const wchar_t value) {
#ifdef SIGNALBOX_PDCURSESMOD
	WORD type;
	if (value == L'\0') return 0;
	if (iswcntrl (value)) return -1;
	if (!GetStringTypeW (CT_CTYPE3, &value, 1, &type)) return 1;
	if (type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK)) return 0;
	if (type & (C3_FULLWIDTH | C3_IDEOGRAPH)) return 2;
	return 1;
#else
	return wcwidth (value);
#endif
}

static int SbUiCursesWideWidth (const wchar_t *text, const size_t length) {
	int width = 0;
	for (size_t i = 0; i < length; i++) {
		const int cells = SbUiCursesCellWidth (text[i]);
		if (cells < 0) return -1;
		width += cells;
	}
	return width;
}

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
	const size_t converted = SbUiCursesUtf8ToWide (wide,
			sizeof (wide) / sizeof (*wide), source);
	if (converted == (size_t) -1) {
		mvaddnstr (y, x, "--", width);
		return;
	}
	int cells = 0;
	size_t keep = 0;
	while (keep < converted) {
		const int charWidth = SbUiCursesCellWidth (wide[keep]);
		if (charWidth < 0 || cells + charWidth > width) break;
		cells += charWidth;
		keep++;
	}
	if (keep < converted && width >= 2) {
		while (keep > 0 && cells > width - 1) {
			const int charWidth = SbUiCursesCellWidth (wide[--keep]);
			if (charWidth > 0) cells -= charWidth;
		}
		wide[keep++] = L'\u2026';
	}
	wide[keep] = L'\0';
#ifdef SIGNALBOX_PDCURSESMOD
	char utf8[sizeof (wide) * 4];
	if (SbUiCursesWideToUtf8 (utf8, sizeof (utf8), wide) == (size_t) -1)
		mvaddnstr (y, x, "--", width);
	else
		mvaddstr (y, x, utf8);
#else
	mvaddnwstr (y, x, wide, (int) keep);
#endif
}

static void SbUiCursesWPut (WINDOW *window, const int y, const int x,
		const int width, const char *text) {
	if (width <= 0) return;
	const char *source = SbUiCursesText (text);
	wchar_t wide[512];
	const size_t converted = SbUiCursesUtf8ToWide (wide,
			sizeof (wide) / sizeof (*wide), source);
	if (converted == (size_t) -1) {
		mvwaddnstr (window, y, x, "--", width);
		return;
	}
	int cells = 0;
	size_t keep = 0;
	while (keep < converted) {
		const int charWidth = SbUiCursesCellWidth (wide[keep]);
		if (charWidth < 0 || cells + charWidth > width) break;
		cells += charWidth;
		keep++;
	}
	if (keep < converted && width >= 2) {
		while (keep > 0 && cells > width - 1) {
			const int charWidth = SbUiCursesCellWidth (wide[--keep]);
			if (charWidth > 0) cells -= charWidth;
		}
		wide[keep++] = L'\u2026';
	}
	wide[keep] = L'\0';
#ifdef SIGNALBOX_PDCURSESMOD
	char utf8[sizeof (wide) * 4];
	if (SbUiCursesWideToUtf8 (utf8, sizeof (utf8), wide) == (size_t) -1)
		mvwaddnstr (window, y, x, "--", width);
	else
		mvwaddstr (window, y, x, utf8);
#else
	mvwaddnwstr (window, y, x, wide, (int) keep);
#endif
}

typedef enum {
	SB_TUI_INPUT_LOGIN = 0,
	SB_TUI_INPUT_MAIN,
	SB_TUI_INPUT_MODAL,
	SB_TUI_INPUT_NUMERIC_JUMP,
	SB_TUI_INPUT_HELP,
} SbTuiInputContext;

typedef struct {
	int status;
	int rawKey;
	int key;
} SbTuiInput;

#ifdef SIGNALBOX_PDCURSESMOD
/* Developer-only key diagnostics are truncated for each TUI session.  Writing
 * directly to this file keeps PDCursesMod's VT stdout/stderr streams intact. */
#define SB_TUI_KEY_LOG_PATH "signalbox-keys.log"
static FILE *SbUiCursesKeyLog;

static const char *SbUiCursesKeyName (const int key) {
	switch (key) {
		case KEY_UP: return "KEY_UP";
		case KEY_DOWN: return "KEY_DOWN";
		case KEY_LEFT: return "KEY_LEFT";
		case KEY_RIGHT: return "KEY_RIGHT";
		case KEY_BTAB: return "KEY_BTAB";
		case KEY_PPAGE: return "KEY_PPAGE";
		case KEY_NPAGE: return "KEY_NPAGE";
		case KEY_HOME: return "KEY_HOME";
		case KEY_END: return "KEY_END";
		case KEY_ENTER: return "KEY_ENTER";
		case KEY_RESIZE: return "KEY_RESIZE";
		case KEY_BACKSPACE: return "KEY_BACKSPACE";
		case KEY_DC: return "KEY_DC";
		default: return "KEY_UNKNOWN";
	}
}

static const char *SbUiCursesInputContextName (const SbTuiInputContext context) {
	switch (context) {
		case SB_TUI_INPUT_LOGIN: return "login";
		case SB_TUI_INPUT_MAIN: return "main";
		case SB_TUI_INPUT_MODAL: return "modal";
		case SB_TUI_INPUT_NUMERIC_JUMP: return "numeric_jump";
		case SB_TUI_INPUT_HELP: return "help";
	}
	return "unknown";
}

static bool SbUiCursesDebugKeys (void) {
	const char * const value = getenv ("SIGNALBOX_DEBUG_KEYS");
	return value != NULL && value[0] != '\0' && value[0] != '0';
}

static void SbUiCursesOpenKeyLog (void) {
	if (SbUiCursesDebugKeys ()) {
		SbUiCursesKeyLog = fopen (SB_TUI_KEY_LOG_PATH, "w");
	}
}

static void SbUiCursesCloseKeyLog (void) {
	if (SbUiCursesKeyLog != NULL) {
		fclose (SbUiCursesKeyLog);
		SbUiCursesKeyLog = NULL;
	}
}

static void SbUiCursesDebugKey (const SbTuiInput input,
		const SbTuiInputContext context, const bool passwordActive) {
	FILE * const output = SbUiCursesKeyLog;
	if (output == NULL) return;
	const char *status = input.status == OK ? "OK" :
			(input.status == KEY_CODE_YES ? "KEY_CODE_YES" : "ERR");
	fputs ("[signalbox:key] function=wget_wch ", output);
	if (passwordActive && input.status == OK) {
		fprintf (output, "status=%s ordinary_character_received context=%s\n",
				status, SbUiCursesInputContextName (context));
	} else if (input.status == ERR) {
		fprintf (output, "status=%s context=%s normalized=ERR\n", status,
				SbUiCursesInputContextName (context));
	} else if (input.status == KEY_CODE_YES) {
		fprintf (output,
				"status=%s raw=%d hex=0x%X special=%s context=%s normalized=%d\n",
				status, input.rawKey, (unsigned int) input.rawKey,
				SbUiCursesKeyName (input.rawKey),
				SbUiCursesInputContextName (context), input.key);
	} else if (input.rawKey >= 0x20 && input.rawKey <= 0x7e) {
		fprintf (output,
				"status=%s raw=%d hex=0x%X codepoint=U+%04X printable='%c' context=%s normalized=%d\n",
				status, input.rawKey, (unsigned int) input.rawKey,
				(unsigned int) input.rawKey, input.rawKey,
				SbUiCursesInputContextName (context), input.key);
	} else {
		fprintf (output,
				"status=%s raw=%d hex=0x%X codepoint=U+%04X context=%s normalized=%d\n",
				status, input.rawKey, (unsigned int) input.rawKey,
				(unsigned int) input.rawKey,
				SbUiCursesInputContextName (context), input.key);
	}
	fflush (output);
}
#endif

static SbTuiInput SbUiCursesReadKey (WINDOW *window,
		const SbTuiInputContext context, const bool passwordActive) {
#ifdef _WIN32
	/* PDCursesMod 4.5.4's VT port waits for the first byte in wget_wch(),
	 * but reads the rest of an ESC sequence with immediate _kbhit() checks.
	 * Wait on the public console handle without consuming that byte, then give
	 * Windows Terminal one short interval to enqueue the complete sequence. */
	const int delay = wgetdelay (window);
	const bool wasNodelay = is_nodelay (window);
	const int waitTimeout = wasNodelay ? 0 : (delay > 0 ? delay : -1);
	const bool keyReady = SbPlatformWaitForConsoleInput (waitTimeout);
	if (keyReady) SbPlatformSleepMs (20);
#endif
	/* Keep the status and output value separate.  wget_wch() returns a status;
	 * the logical key is written through its output argument. */
	wint_t key = 0;
#ifdef _WIN32
	/* The console wait above owns this read's timeout.  Make the consuming read
	 * immediate, then restore the caller's timed, blocking, or nodelay state. */
	if (keyReady) wtimeout (window, 0);
	const int status = keyReady ? wget_wch (window, &key) : ERR;
	if (keyReady) wtimeout (window, wasNodelay ? 0 : (delay > 0 ? delay : -1));
#else
	const int status = wget_wch (window, &key);
#endif
	const int rawKey = status == ERR ? ERR : (int) key;
	/* The shared renderer treats all three common Enter forms identically. */
	const int normalized = rawKey == '\r' || rawKey == KEY_ENTER ? '\n' : rawKey;
	const SbTuiInput input = {status, rawKey, normalized};
#ifdef SIGNALBOX_PDCURSESMOD
	SbUiCursesDebugKey (input, context, passwordActive);
#else
	(void) context;
	(void) passwordActive;
#endif
	return input;
}

static bool SbUiCursesResize (SbUiCursesData *data) {
#ifdef SIGNALBOX_PDCURSESMOD
	/* PDCurses requires the application to adopt a user resize before stdscr
	 * and curscr dimensions change.  It does not resize application windows. */
	if (is_termresized () && resize_term (0, 0) == ERR) {
		tuiDebugPrint ("resize_term failed\n");
		return false;
	}
#endif
	int rows, cols;
	getmaxyx (stdscr, rows, cols);
	if (rows == data->screenRows && cols == data->screenCols) return false;
	const bool shrinking = rows < data->screenRows || cols < data->screenCols;
	data->screenRows = rows;
	data->screenCols = cols;
	if (shrinking) {
		/* The virtual screen can retain cells from the old, larger geometry.
		 * Force the next update to discard both virtual and physical state. */
		erase ();
		clearok (stdscr, TRUE);
		clearok (curscr, TRUE);
		touchwin (stdscr);
	} else {
		erase ();
	}
	data->scrollOffset = data->selectedIndex;
	data->recentOffset = data->recentSelected;
	data->helpOffset = 0;
	tuiDebugPrint ("resize size=%dx%d shrinking=%s\n", cols, rows,
			shrinking ? "yes" : "no");
	return true;
}

static void SbUiCursesTime (char *dest, const size_t size,
		const unsigned int seconds) {
	if (seconds == 0) {
		snprintf (dest, size, "--:--");
		return;
	}
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

typedef enum { SB_HELP_BLANK = 0, SB_HELP_HEADING, SB_HELP_COMMAND } SbHelpRowKind;
typedef struct { SbHelpRowKind kind; char key[12]; const char *text; } SbHelpRow;

static void SbUiCursesHelpAddCommand (SbHelpRow *rows, size_t *count,
		const char *key, const char *text) {
	SbHelpRow * const row = &rows[(*count)++];
	row->kind = SB_HELP_COMMAND;
	snprintf (row->key, sizeof (row->key), "%s", key);
	row->text = text;
}

static void SbUiCursesHelpAddConfigured (const SbUiRenderer *renderer,
		SbHelpRow *rows, size_t *count, const SbUiCommand command,
		const char *text) {
	const char key = SbUiCursesKey (renderer, command);
	if (key == '-') return;
	char label[2] = {key, '\0'};
	SbUiCursesHelpAddCommand (rows, count, key == ' ' ? "Space" : label, text);
}

static size_t SbUiCursesHelpRows (const SbUiRenderer *renderer, SbHelpRow *rows) {
	const SbUiCursesData * const data = renderer->data;
	size_t count = 0;
#define HELP_BLANK() rows[count++] = (SbHelpRow) {SB_HELP_BLANK, "", NULL}
#define HELP_HEADING(value) rows[count++] = (SbHelpRow) {SB_HELP_HEADING, "", value}
#define HELP_SECTION(value) HELP_HEADING (value); HELP_BLANK ()
#define SB_TUI_HELP_COMMAND(key, value) SbUiCursesHelpAddCommand (rows, &count, key, value)
#define HELP_CONFIG(command, value) SbUiCursesHelpAddConfigured (renderer, rows, &count, command, value)
	HELP_SECTION ("NAVIGATION");
	SB_TUI_HELP_COMMAND ("Tab", "switch pane"); SB_TUI_HELP_COMMAND ("Shift+Tab", "switch pane backward");
	SB_TUI_HELP_COMMAND ("Up/Down", "navigate"); SB_TUI_HELP_COMMAND ("j/k", "navigate");
	SB_TUI_HELP_COMMAND ("PgUp/PgDn", "page"); SB_TUI_HELP_COMMAND ("Home/End", "first / last");
	SB_TUI_HELP_COMMAND ("Enter", "select / action"); SB_TUI_HELP_COMMAND ("Esc", "back / close");
	HELP_BLANK (); HELP_SECTION ("PLAYBACK");
	HELP_CONFIG (SB_UI_CMD_TOGGLE_PAUSE, "pause / resume");
	HELP_CONFIG (SB_UI_CMD_SKIP, "next track");
	HELP_CONFIG (SB_UI_CMD_LOVE, data->unicodeSymbols ? "♥ love" : "<3 love");
	HELP_CONFIG (SB_UI_CMD_BAN, "</3 ban");
	HELP_BLANK (); HELP_SECTION ("VOLUME");
	HELP_CONFIG (SB_UI_CMD_VOLUME_DOWN, "volume down");
	HELP_CONFIG (SB_UI_CMD_VOLUME_UP, "volume up");
	HELP_CONFIG (SB_UI_CMD_VOLUME_RESET, "reset to 0 dB");
	HELP_BLANK (); HELP_SECTION ("STATIONS");
	SB_TUI_HELP_COMMAND ("f", "favorite / unfavorite"); SB_TUI_HELP_COMMAND ("z", "cycle sort");
	SB_TUI_HELP_COMMAND ("/", "filter stations"); SB_TUI_HELP_COMMAND ("#", "jump to station number");
	HELP_CONFIG (SB_UI_CMD_GENRE_STATION, "genres");
	HELP_BLANK (); HELP_SECTION ("HISTORY");
	HELP_CONFIG (SB_UI_CMD_HISTORY, "full session history");
	SB_TUI_HELP_COMMAND ("Tab", "focus RECENT"); SB_TUI_HELP_COMMAND ("Enter", "history action");
	HELP_BLANK (); HELP_SECTION ("UPCOMING");
	HELP_CONFIG (SB_UI_CMD_UPCOMING, "browse upcoming");
	SB_TUI_HELP_COMMAND ("Enter", "selected-track actions");
	if (SbUiCursesVisualizerKeyAvailable (renderer)) {
		HELP_BLANK (); HELP_SECTION ("VISUALIZER");
		SB_TUI_HELP_COMMAND ("V", "toggle spectrum");
	}
#undef HELP_BLANK
#undef HELP_HEADING
#undef HELP_SECTION
#undef SB_TUI_HELP_COMMAND
#undef HELP_CONFIG
	return count;
}

static void SbUiCursesFooter (const SbUiRenderer *renderer, char *footer,
		const size_t size, const int width) {
	if (width < 80) {
		snprintf (footer, size, "j/k select  Enter tune  %c pause  %c next  %c help  %c quit",
				SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE),
				SbUiCursesKey (renderer, SB_UI_CMD_SKIP),
				SbUiCursesKey (renderer, SB_UI_CMD_HELP),
				SbUiCursesKey (renderer, SB_UI_CMD_QUIT));
		return;
	}
	snprintf (footer, size,
			"Tab pane  j/k navigate  Enter action  %c pause  %c next  %c help  %c quit",
			SbUiCursesKey (renderer, SB_UI_CMD_TOGGLE_PAUSE),
			SbUiCursesKey (renderer, SB_UI_CMD_SKIP),
			SbUiCursesKey (renderer, SB_UI_CMD_HELP),
			SbUiCursesKey (renderer, SB_UI_CMD_QUIT));
}

static void SbUiCursesFooterDraw (const SbUiCursesData *data, const int y,
		const int x, const int width, const char *footer) {
	int column = x;
	const char *part = footer;
	while (*part != '\0' && column < x + width) {
		const char *gap = strstr (part, "  ");
		const size_t length = gap != NULL ? (size_t) (gap - part) : strlen (part);
		const char *space = memchr (part, ' ', length);
		const size_t keyLength = space != NULL ? (size_t) (space - part) : length;
		SbUiCursesAttrOn (data, SB_TUI_COLOR_KEY, A_BOLD);
		mvaddnstr (y, column, part, (int) keyLength);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_KEY, A_BOLD);
		column += (int) keyLength;
		SbUiCursesAttrOn (data, SB_TUI_COLOR_PRIMARY, 0);
		mvaddnstr (y, column, part + keyLength, (int) (length - keyLength));
		SbUiCursesAttrOff (data, SB_TUI_COLOR_PRIMARY, 0);
		column += (int) (length - keyLength);
		if (gap == NULL) break;
		if (column + 2 <= x + width) mvaddnstr (y, column, "  ", 2);
		column += 2;
		part = gap + 2;
	}
}

static const char *SbUiCursesRating (const SbUiCursesData *data,
		const PianoSong_t *song) {
	if (song == NULL) return "Rating: Neutral";
	switch (song->rating) {
		case PIANO_RATE_LOVE: return data->unicodeSymbols ?
				"Rating: ♥ Loved" : "Rating: <3 Loved";
		case PIANO_RATE_BAN: return data->unicodeSymbols ?
				"Rating: </3 Banned" : "Rating: </3 Banned";
		case PIANO_RATE_TIRED: return "Rating: Tired";
		default: return "Rating: Neutral";
	}
}

static size_t SbUiCursesStationCount (const SbUiCursesData *data) {
	return data->browser.visibleCount;
}

static const PianoStation_t *SbUiCursesStationAt (const SbUiCursesData *data,
		const size_t index) {
	return SbStationBrowserAt (&data->browser, index);
}

static void SbUiCursesRebuildStations (SbUiCursesData *data,
		const SbUiModel *model, const bool preserveSelection) {
	const PianoStation_t *selected = preserveSelection ?
			SbUiCursesStationAt (data, data->selectedIndex) : NULL;
	if (!SbStationBrowserRebuild (&data->browser, model->stations,
			model->stationsGeneration)) return;
	if (!preserveSelection) data->selectionInitialized = false;
	data->selectedIndex = 0;
	if (selected != NULL) {
		for (size_t i = 0; i < data->browser.visibleCount; i++) {
			if (data->browser.visibleStations[i] == selected) {
				data->selectedIndex = i;
				break;
			}
		}
	}
	data->scrollOffset = 0;
}

static void SbUiCursesEnsureStations (SbUiCursesData *data,
		const SbUiModel *model) {
	if (data->browser.sourceGeneration != model->stationsGeneration) {
		/* A refresh may have freed a deleted canonical object. Re-anchor from the
		 * still-canonical active station instead of inspecting a stale selection. */
		SbUiCursesRebuildStations (data, model, false);
	}
}

static void SbUiCursesClampSelection (SbUiCursesData *data,
		const SbUiModel *model, const size_t visibleRows) {
	SbUiCursesEnsureStations (data, model);
	const size_t count = SbUiCursesStationCount (data);
	if (count == 0) {
		data->selectedIndex = data->scrollOffset = 0;
		data->selectionInitialized = false;
		return;
	}
	if (!data->selectionInitialized) {
		data->selectedIndex = 0;
		for (size_t i = 0; i < count; i++) {
			const PianoStation_t * const station = SbUiCursesStationAt (data, i);
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
	if (data->browser.visibleCount == 0) {
		SbUiCursesAttrOn (data, SB_TUI_COLOR_MUTED, A_DIM);
		char empty[192];
		if (data->browser.totalCount > 0 && data->browser.filter[0] != '\0') {
			snprintf (empty, sizeof (empty), "No stations match \"%s\"",
					data->browser.filter);
		} else {
			strcpy (empty, "No stations available");
		}
		SbUiCursesPut (y, x, width, empty);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_MUTED, A_DIM);
		return;
	}
	for (size_t row = 0; row < visible; row++) {
		const size_t index = data->scrollOffset + row;
		const PianoStation_t * const station = SbUiCursesStationAt (data, index);
		if (station == NULL) {
			break;
		}
		const bool active = station == model->station;
		SbUiCursesAttrOn (data, active ? SB_TUI_COLOR_STATION_ACTIVE :
				SB_TUI_COLOR_STATION, active ? A_BOLD : 0);
		mvaddch (y + (int) row, x, active ? '*' : ' ');
		mvaddch (y + (int) row, x + 1,
				SbStationBrowserIsFavorite (&data->browser, station) ? '*' : ' ');
		if (index == data->selectedIndex && data->focus == SB_TUI_FOCUS_STATIONS) {
			SbUiCursesAttrOn (data, SB_TUI_COLOR_SELECTED, A_REVERSE);
		}
		int nameX = x + 3;
		int nameWidth = width - 3;
		if (data->jumpMode && width >= 9) {
			char number[16];
			snprintf (number, sizeof (number), "%zu", index + 1);
			SbUiCursesPut (y + (int) row, nameX, 5, number);
			nameX += 6;
			nameWidth -= 6;
		}
		SbUiCursesPut (y + (int) row, nameX, nameWidth, station->name);
		if (index == data->selectedIndex && data->focus == SB_TUI_FOCUS_STATIONS) {
			SbUiCursesAttrOff (data, SB_TUI_COLOR_SELECTED, A_REVERSE);
		}
		SbUiCursesAttrOff (data, active ? SB_TUI_COLOR_STATION_ACTIVE :
				SB_TUI_COLOR_STATION, active ? A_BOLD : 0);
	}
}

static void SbUiCursesStationHeader (SbUiCursesData *data,
		const int y, const int x, const int width) {
	char header[256];
	if (data->browser.filter[0] != '\0') {
		snprintf (header, sizeof (header), "STATIONS %zu/%zu %s /%s",
				data->browser.visibleCount, data->browser.totalCount,
				SbStationBrowserSortName (data->browser.sort), data->browser.filter);
	} else {
		snprintf (header, sizeof (header), "STATIONS %zu %s",
				data->browser.totalCount,
				SbStationBrowserSortName (data->browser.sort));
	}
	SbUiCursesPut (y, x, width, header);
}

static void SbUiCursesProgress (const SbUiCursesData *data,
		const SbUiModel *model, const int y,
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
		SbUiCursesAttrOn (data, SB_TUI_COLOR_STATUS, 0);
		SbUiCursesPut (y, x, width, times);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_STATUS, 0);
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
		SbUiCursesAttrOn (data, (unsigned int) i < filled ?
				SB_TUI_COLOR_PROGRESS_FILLED : SB_TUI_COLOR_MUTED,
				(unsigned int) i < filled ? A_BOLD : A_DIM);
		addch ((unsigned int) i < filled ? '=' : '-');
		SbUiCursesAttrOff (data, (unsigned int) i < filled ?
				SB_TUI_COLOR_PROGRESS_FILLED : SB_TUI_COLOR_MUTED,
				(unsigned int) i < filled ? A_BOLD : A_DIM);
	}
	addch (']');
	SbUiCursesAttrOn (data, SB_TUI_COLOR_STATUS, 0);
	SbUiCursesPut (y, x + barWidth + 3, timeWidth, times);
	SbUiCursesAttrOff (data, SB_TUI_COLOR_STATUS, 0);
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
		case SB_UI_ACTIVITY_AUDIO_UNAVAILABLE: return "Audio unavailable";
		default: return SbUiCursesPlayback (model);
	}
}

static void SbUiCursesLabelValue (const SbUiCursesData *data, const int y,
		const int x, const int width, const char *label, const char *value,
		const SbTuiColorRole valueRole, const attr_t valueAttr) {
	const int labelWidth = (int) strlen (label);
	SbUiCursesAttrOn (data, SB_TUI_COLOR_MUTED, 0);
	mvaddnstr (y, x, label, width);
	SbUiCursesAttrOff (data, SB_TUI_COLOR_MUTED, 0);
	if (width > labelWidth) {
		SbUiCursesAttrOn (data, valueRole, valueAttr);
		SbUiCursesPut (y, x + labelWidth, width - labelWidth, value);
		SbUiCursesAttrOff (data, valueRole, valueAttr);
	}
}

static void SbUiCursesNowPlaying (const SbUiCursesData *data,
		const SbUiModel *model, const int y,
		const int x, const int height, const int width) {
	if (height <= 0) return;
	SbUiCursesLabelValue (data, y, x, width, "Artist: ",
			model->song != NULL ? model->song->artist : NULL,
			SB_TUI_COLOR_ARTIST, A_BOLD);
	if (height > 1) SbUiCursesLabelValue (data, y + 1, x, width, "Track: ",
			model->song != NULL ? model->song->title : NULL,
			SB_TUI_COLOR_TRACK, A_BOLD);
	if (height > 2) {
		SbUiCursesLabelValue (data, y + 2, x, width, "Album: ",
				model->song != NULL ? model->song->album : NULL,
				SB_TUI_COLOR_ALBUM, 0);
	}
	if (height > 3) {
		const PianoStation_t * const station = model->songStation != NULL ?
				model->songStation : model->station;
		SbUiCursesLabelValue (data, y + 3, x, width, "Station: ",
				station != NULL ? station->name : NULL,
				SB_TUI_COLOR_STATION_ACTIVE, A_BOLD);
	}
	if (height > 5) SbUiCursesProgress (data, model, y + 5, x, width);
	if (height > 6) {
		const SbTuiColorRole playbackRole = model->playback == SB_UI_PLAYBACK_PAUSED ?
				SB_TUI_COLOR_WARNING : SB_TUI_COLOR_STATUS;
		SbUiCursesAttrOn (data, playbackRole, A_BOLD);
		SbUiCursesPut (y + 6, x, width, SbUiCursesPlayback (model));
		SbUiCursesAttrOff (data, playbackRole, A_BOLD);
		char volume[40];
		snprintf (volume, sizeof (volume), "Volume %+d dB", model->volumeDb);
		SbUiCursesAttrOn (data, SB_TUI_COLOR_WARNING, 0);
		SbUiCursesPut (y + 6, x + 12, width - 12, volume);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_WARNING, 0);
		const char *rating = SbUiCursesRating (data, model->song);
		const int ratingX = x + 12 + (int) strlen (volume) + 3;
		SbTuiColorRole ratingRole = SB_TUI_COLOR_MUTED;
		if (model->song != NULL && model->song->rating == PIANO_RATE_LOVE)
			ratingRole = SB_TUI_COLOR_LOVED;
		else if (model->song != NULL && model->song->rating == PIANO_RATE_BAN)
			ratingRole = SB_TUI_COLOR_ERROR;
		if (ratingX < x + width) {
			SbUiCursesAttrOn (data, ratingRole, 0);
			SbUiCursesPut (y + 6, ratingX, x + width - ratingX, rating);
			SbUiCursesAttrOff (data, ratingRole, 0);
		}
	}
}

static void SbUiCursesSpectrum (const SbUiCursesData *data,
		const SbUiModel *model, const int y, const int x, const int barRows,
		const int width) {
	if (barRows <= 0 || width < 38) return;
	static const char *const wideLabels[SB_SPECTRUM_BANDS] =
			{"50", "80", "125", "200", "315", "500", "800", "1.25k",
			"2k", "3.15k", "5k", "10k"};
	static const char *const compactLabels[SB_SPECTRUM_COMPACT_BANDS] =
			{"60", "120", "250", "500", "1k", "2k", "4k", "8k"};
	const bool wide = width >= 69;
	const size_t bandCount = wide ? SB_SPECTRUM_BANDS : SB_SPECTRUM_COMPACT_BANDS;
	const int barWidth = 3;
	int gapWidth = 2;
	if (wide) {
		const int gapCount = (int) bandCount - 1;
		const int targetWidth = (width * 85 + 50) / 100;
		gapWidth = (targetWidth - (int) bandCount * barWidth +
				gapCount / 2) / gapCount;
		if (gapWidth < 3) gapWidth = 3;
		if (gapWidth > 8) gapWidth = 8;
	}
	const int bandPitch = barWidth + gapWidth;
	const int gridWidth = (int) bandCount * barWidth +
			((int) bandCount - 1) * gapWidth;
	const int gridLeft = x + (width - gridWidth) / 2;
	const char *const *labels = wide ? wideLabels : compactLabels;
	int labelX[SB_SPECTRUM_BANDS];
	int labelWidth[SB_SPECTRUM_BANDS];
	for (size_t band = 0; band < bandCount; band++) {
		const int barX = gridLeft + (int) band * bandPitch;
		labelWidth[band] = SbUiCursesTextWidth (labels[band]);
		/* Work in half cells and resolve an exact half-cell tie to the right. */
		labelX[band] = (2 * barX + barWidth - labelWidth[band] + 1) / 2;
	}
	float levels[SB_SPECTRUM_COMPACT_BANDS], peaks[SB_SPECTRUM_COMPACT_BANDS];
	if (!wide) {
		SbSpectrumAggregateCompact (model->spectrum.bands, levels);
		SbSpectrumAggregateCompact (model->spectrum.peaks, peaks);
	}
	for (size_t band = 0; band < bandCount; band++) {
		float level = model->spectrum.valid ?
				(wide ? model->spectrum.bands[band] : levels[band]) : 0.0f;
		float peak = model->spectrum.valid ?
				(wide ? model->spectrum.peaks[band] : peaks[band]) : 0.0f;
		if (level < 0.0f) level = 0.0f;
		if (level > 1.0f) level = 1.0f;
		if (peak < 0.0f) peak = 0.0f;
		if (peak > 1.0f) peak = 1.0f;
		const int filled = (int) ceilf (level * barRows);
		const int baseline = y + barRows - 1;
		const int bodyTop = baseline - filled + 1;
		/* Draw every active cell from the same baseline.  The old absolute-row
		 * colour test made the hot row look like an independently positioned
		 * marker instead of part of the body. */
		for (int bodyRow = 0; bodyRow < filled; bodyRow++) {
			SbTuiColorRole role = SB_TUI_COLOR_PROGRESS_FILLED;
			if (bodyRow >= (barRows + 1) / 2) role = SB_TUI_COLOR_ARTIST;
			if (barRows >= 4 && filled == barRows && bodyRow == barRows - 1)
				role = SB_TUI_COLOR_TRACK;
			SbUiCursesAttrOn (data, role, 0);
			SbUiCursesPut (baseline - bodyRow,
					gridLeft + (int) band * bandPitch, barWidth,
					data->unicodeBlocks ? "███" : "###");
			SbUiCursesAttrOff (data, role, 0);
		}
		int peakY = baseline - (int) ceilf (peak * barRows) + 1;
		if (peakY < y) peakY = y;
		if (peak > 0.01f && peakY >= y && peakY < bodyTop) {
			SbUiCursesAttrOn (data, SB_TUI_COLOR_WARNING, A_BOLD);
			SbUiCursesPut (peakY, gridLeft + (int) band * bandPitch, barWidth, "---");
			SbUiCursesAttrOff (data, SB_TUI_COLOR_WARNING, A_BOLD);
		}
		SbUiCursesAttrOn (data, SB_TUI_COLOR_MUTED, 0);
		SbUiCursesPut (y + barRows, labelX[band], labelWidth[band], labels[band]);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_MUTED, 0);
	}
}

static int SbUiCursesTextWidth (const char *text) {
	if (text == NULL || *text == '\0') return 2;
	wchar_t wide[SB_UI_HISTORY_TEXT_MAX];
	const size_t converted = SbUiCursesUtf8ToWide (wide,
			sizeof (wide) / sizeof (*wide), text);
	if (converted == (size_t) -1) return (int) strlen (text);
	int width = 0;
	for (size_t i = 0; i < converted; i++) {
		const int cells = SbUiCursesCellWidth (wide[i]);
		if (cells > 0) width += cells;
	}
	return width;
}

#define SB_TUI_ROW_MARKER_WIDTH 6
#define SB_TUI_ROW_DURATION_WIDTH 8

static bool SbUiCursesMetadataWraps (const char *artist, const char *title,
		const char *album, const int width) {
	const int metadataWidth = width - SB_TUI_ROW_MARKER_WIDTH -
			SB_TUI_ROW_DURATION_WIDTH;
	const int naturalWidth = SbUiCursesTextWidth (artist) + 3 +
			SbUiCursesTextWidth (title) + 3 + SbUiCursesTextWidth (album);
	return metadataWidth >= 6 && naturalWidth > metadataWidth;
}

static bool SbUiCursesHistoryWraps (const SbUiHistoryEntry *entry,
		const int width) {
	return SbUiCursesMetadataWraps (entry->artist, entry->title, entry->album,
			width);
}

static const char *SbUiCursesRatingMarker (const SbUiCursesData *data,
		const PianoSongRating_t rating) {
	if (rating == PIANO_RATE_LOVE) return data->unicodeSymbols ? "♥  " : "<3 ";
	if (rating == PIANO_RATE_BAN) return "</3";
	return "   ";
}

static void SbUiCursesRowMain (const SbUiCursesData *data, WINDOW *window,
		const int y, const int x, const int width, const char *artist,
		const char *title) {
	if (width <= 0) return;
	const int separatorWidth = width >= 3 ? 3 : 0;
	const int contentWidth = width - separatorWidth;
	const int artistNatural = SbUiCursesTextWidth (artist);
	const int titleNatural = SbUiCursesTextWidth (title);
	int artistWidth = artistNatural;
	int titleWidth = titleNatural;
	if (artistWidth + titleWidth > contentWidth) {
		artistWidth = contentWidth > 1 ? contentWidth / 2 : contentWidth;
		titleWidth = contentWidth - artistWidth;
	}
	SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_ARTIST, A_BOLD);
	SbUiCursesWPut (window, y, x, artistWidth, artist);
	SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_ARTIST, A_BOLD);
	if (separatorWidth > 0) {
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_MUTED, 0);
		SbUiCursesWPut (window, y, x + artistWidth, separatorWidth, " — ");
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_MUTED, 0);
	}
	SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_TRACK, A_BOLD);
	SbUiCursesWPut (window, y, x + artistWidth + separatorWidth, titleWidth,
			title);
	SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_TRACK, A_BOLD);
}

static int SbUiCursesMetadataRow (const SbUiCursesData *data, WINDOW *window,
		const int y, const int x, const int width, const char *artist,
		const char *title, const char *album, const unsigned int seconds) {
	if (width <= SB_TUI_ROW_MARKER_WIDTH + SB_TUI_ROW_DURATION_WIDTH) return 0;
	char duration[16];
	SbUiCursesTime (duration, sizeof (duration), seconds);
	const bool wraps = SbUiCursesMetadataWraps (artist, title, album, width);
	const int metadataX = x + SB_TUI_ROW_MARKER_WIDTH;
	const int durationX = x + width - SB_TUI_ROW_DURATION_WIDTH;
	const int metadataWidth = durationX - metadataX;
	if (wraps) {
		SbUiCursesRowMain (data, window, y, metadataX,
				width - SB_TUI_ROW_MARKER_WIDTH, artist, title);
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_ALBUM, 0);
		SbUiCursesWPut (window, y + 1, metadataX, metadataWidth, album);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_ALBUM, 0);
	} else {
		const int artistTitleNatural = SbUiCursesTextWidth (artist) + 3 +
				SbUiCursesTextWidth (title);
		const int mainWidth = artistTitleNatural < metadataWidth ?
				artistTitleNatural : metadataWidth;
		SbUiCursesRowMain (data, window, y, metadataX, mainWidth, artist, title);
		if (mainWidth + 3 <= metadataWidth) {
			SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_MUTED, 0);
			SbUiCursesWPut (window, y, metadataX + mainWidth, 3, " · ");
			SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_MUTED, 0);
			SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_ALBUM, 0);
			SbUiCursesWPut (window, y, metadataX + mainWidth + 3,
					metadataWidth - mainWidth - 3, album);
			SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_ALBUM, 0);
		}
	}
	const int durationWidth = SbUiCursesTextWidth (duration);
	SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_WARNING, 0);
	SbUiCursesWPut (window, wraps ? y + 1 : y,
			durationX + SB_TUI_ROW_DURATION_WIDTH - durationWidth,
			durationWidth, duration);
	SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_WARNING, 0);
	return wraps ? 2 : 1;
}

static int SbUiCursesHistoryRow (const SbUiCursesData *data,
		WINDOW *window, const SbUiHistoryEntry *entry, const int y,
		const int x, const int width, const bool selected) {
	if (width <= 0) return 0;
	const bool mainSelection = selected && window == stdscr;
	/* Keep focus and rating fields reserved on every row, so selection never
	 * shifts the music metadata. */
	if (selected && !mainSelection) SbUiCursesWAttrOn (window, data,
			SB_TUI_COLOR_SELECTED, A_REVERSE);
	if (mainSelection) {
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_SELECTED, A_BOLD);
		SbUiCursesWPut (window, y, x, 1, data->unicodeSymbols ? "›" : ">");
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_SELECTED, A_BOLD);
	}
	const SbTuiColorRole markerRole = entry->rating == PIANO_RATE_LOVE ?
			SB_TUI_COLOR_LOVED : entry->rating == PIANO_RATE_BAN ?
			SB_TUI_COLOR_ERROR : SB_TUI_COLOR_MUTED;
	SbUiCursesWAttrOn (window, data, markerRole, A_BOLD);
	SbUiCursesWPut (window, y, x + 2, 3,
			SbUiCursesRatingMarker (data, entry->rating));
	SbUiCursesWAttrOff (window, data, markerRole, A_BOLD);
	const int rows = SbUiCursesMetadataRow (data, window, y, x, width,
			entry->artist, entry->title, entry->album, entry->duration);
	if (selected && !mainSelection) SbUiCursesWAttrOff (window, data,
			SB_TUI_COLOR_SELECTED, A_REVERSE);
	return rows;
}

static void SbUiCursesHistory (SbUiCursesData *data,
		const SbUiModel *model, const int y,
		const int x, const int height, const int width) {
	if (height <= 0) return;
	if (model->historyCount == 0) {
		SbUiCursesPut (y, x, width, "No previous tracks this session");
		return;
	}
	if (data->observedHistoryCount != model->historyCount) {
		if ((data->focus == SB_TUI_FOCUS_RECENT || data->recentSelected > 0) &&
				data->observedHistoryCount > 0 &&
				model->historyCount > data->observedHistoryCount) {
			const size_t added = model->historyCount - data->observedHistoryCount;
			data->recentSelected += added;
			data->recentOffset += added;
		}
		data->observedHistoryCount = model->historyCount;
	}
	if (data->recentSelected >= model->historyCount)
		data->recentSelected = model->historyCount - 1;
	if (data->focus != SB_TUI_FOCUS_RECENT) data->recentOffset = 0;
	if (data->recentOffset > data->recentSelected)
		data->recentOffset = data->recentSelected;
	int rowsToSelection = 0;
	for (size_t i = data->recentOffset; i <= data->recentSelected; i++)
		rowsToSelection += SbUiCursesHistoryWraps (&model->history[i], width) ? 2 : 1;
	while (rowsToSelection > height && data->recentOffset < data->recentSelected) {
		rowsToSelection -= SbUiCursesHistoryWraps (
				&model->history[data->recentOffset++], width) ? 2 : 1;
	}
	int used = 0;
	for (size_t i = data->recentOffset; i < model->historyCount; i++) {
		const int cost = SbUiCursesHistoryWraps (&model->history[i], width) ? 2 : 1;
		if (used + cost > height) break;
		const int rowY = y + used;
		used += SbUiCursesHistoryRow (data, stdscr, &model->history[i],
				rowY, x, width, data->focus == SB_TUI_FOCUS_RECENT &&
				i == data->recentSelected);
	}
}

static size_t SbUiCursesUpcomingCount (const SbUiModel *model) {
	size_t count = 0;
	const PianoSong_t *song = model->song != NULL ? PianoListNextP (model->song) : NULL;
	PianoListForeachP (song) count++;
	return count;
}

static void SbUiCursesUpcoming (const SbUiCursesData *data,
		const SbUiModel *model, const int y, const int x, const int height,
		const int width) {
	const PianoSong_t *song = model->song != NULL ? PianoListNextP (model->song) : NULL;
	int row = 0;
	PianoListForeachP (song) {
		const int cost = SbUiCursesMetadataWraps (song->artist, song->title,
				song->album, width) ? 2 : 1;
		if (row + cost > height) break;
		row += SbUiCursesMetadataRow (data, stdscr, y + row, x, width,
				song->artist, song->title, song->album, song->length);
	}
}

static void SbUiCursesFrame (const SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	int rows, cols;
#ifdef SIGNALBOX_PDCURSESMOD
	if (is_termresized ()) SbUiCursesResize (data);
#endif
	getmaxyx (stdscr, rows, cols);
	erase ();
	attrset (SbUiCursesRole (data, SB_TUI_COLOR_PRIMARY));
	char footer[256];
	SbUiCursesFooter (renderer, footer, sizeof (footer), cols);

	if (rows < 15 || cols < 50) {
		data->focus = SB_TUI_FOCUS_STATIONS;
		const int left = cols > 6 ? 3 : 0;
		const int width = cols > left ? cols - left : 0;
		if (rows > 0) {
			SbUiCursesAttrOn (data, SB_TUI_COLOR_TITLE, A_BOLD);
			SbUiCursesPut (0, left, width, "Terminal too small");
			SbUiCursesAttrOff (data, SB_TUI_COLOR_TITLE, A_BOLD);
		}
		if (rows > 1)
			SbUiCursesPut (1, left, width, "Resize to at least 50x15");
		if (rows > 2) SbUiCursesPut (2, left, width, "q quit");
		refresh ();
		return;
	}
	if (cols < 80 || rows < 24) data->focus = SB_TUI_FOCUS_STATIONS;

	SbUiCursesAttrOn (data, SB_TUI_COLOR_BORDER, 0);
	SbUiCursesBox (stdscr);
	SbUiCursesAttrOff (data, SB_TUI_COLOR_BORDER, 0);
	SbUiCursesAttrOn (data, SB_TUI_COLOR_TITLE, A_BOLD);
	SbUiCursesPut (1, 2, cols / 2, "SIGNALBOX");
	SbUiCursesAttrOff (data, SB_TUI_COLOR_TITLE, A_BOLD);
	SbUiCursesAttrOn (data, SB_TUI_COLOR_MUTED, 0);
	SbUiCursesPut (1, cols - 15, 13, "PANDORA RADIO");
	SbUiCursesAttrOff (data, SB_TUI_COLOR_MUTED, 0);
	SbUiCursesAttrOn (data, SB_TUI_COLOR_BORDER, 0);
	SbUiCursesHLine (stdscr, 2, 1, cols - 2);

	const int footerY = rows - 2;
	const int statusY = rows - 4;
	SbUiCursesHLine (stdscr, statusY - 1, 1, cols - 2);
	SbUiCursesHLine (stdscr, footerY - 1, 1, cols - 2);
	SbUiCursesAttrOff (data, SB_TUI_COLOR_BORDER, 0);
	SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION, A_BOLD);
	SbUiCursesPut (statusY, 2, 8, "STATUS");
	SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION, A_BOLD);
	SbUiCursesAttrOn (data, model->activity == SB_UI_ACTIVITY_ERROR ?
			SB_TUI_COLOR_ERROR : (model->activity == SB_UI_ACTIVITY_READY ?
			SB_TUI_COLOR_STATUS : SB_TUI_COLOR_WARNING), A_BOLD);
	SbUiCursesPut (statusY, 11, 20, SbUiCursesActivity (model));
	SbUiCursesAttrOff (data, model->activity == SB_UI_ACTIVITY_ERROR ?
			SB_TUI_COLOR_ERROR : (model->activity == SB_UI_ACTIVITY_READY ?
			SB_TUI_COLOR_STATUS : SB_TUI_COLOR_WARNING), A_BOLD);
	pthread_mutex_lock (&data->statusLock);
	if (data->statusExpires != 0 && time (NULL) >= data->statusExpires) {
		data->status[0] = '\0';
		data->statusExpires = 0;
	}
	if (data->statusSeverity >= SB_UI_NOTICE_WARNING &&
			data->status[0] != '\0') {
		SbUiCursesAttrOn (data, data->statusSeverity == SB_UI_NOTICE_ERROR ?
				SB_TUI_COLOR_ERROR : SB_TUI_COLOR_WARNING, A_BOLD);
	}
	SbUiCursesPut (statusY, 32, cols - 34,
			data->status[0] != '\0' ? data->status : "Ready");
	if (data->statusSeverity >= SB_UI_NOTICE_WARNING &&
			data->status[0] != '\0') {
		SbUiCursesAttrOff (data, data->statusSeverity == SB_UI_NOTICE_ERROR ?
				SB_TUI_COLOR_ERROR : SB_TUI_COLOR_WARNING, A_BOLD);
	}
	pthread_mutex_unlock (&data->statusLock);
	SbUiCursesFooterDraw (data, footerY, 2, cols - 4, footer);

	if (cols >= 80 && rows >= 24) {
		const int split = cols / 3;
		SbUiCursesVLine (stdscr, 3, split, statusY - 4);
		const int nowPlayingHeight = 8;
		const size_t upcomingCount = SbUiCursesUpcomingCount (model);
		const int rightWidth = cols - (split + 2) - 2;
		const int spectrumRows = model->visualizerEnabled && rightWidth >= 38 ?
				(rows >= 45 ? 6 : rows >= 39 ? 4 : rows >= 33 ? 2 : 0) : 0;
		const int spectrumY = 6 + nowPlayingHeight + 1;
		/* Heading + top breathing row + bars + labels + bottom breathing row. */
		const int spectrumHeight = spectrumRows > 0 ? spectrumRows + 4 : 0;
		const int upcomingY = spectrumY + spectrumHeight;
		const int maxUpcoming = rows >= 44 ? 4 : rows >= 34 ? 3 : 2;
		const int upcomingRows = upcomingCount > 0 && rows >= 30 ?
				(int) (upcomingCount < (size_t) maxUpcoming ? upcomingCount :
				(size_t) maxUpcoming) : 0;
		const int historyY = upcomingY + (upcomingRows > 0 ? upcomingRows + 2 : 0);
		SbUiCursesHLine (stdscr, historyY - 1, split + 1, cols - split - 2);
		SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_STATIONS ? A_BOLD : 0);
		SbUiCursesStationHeader (data, 4, 2, split - 3);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_STATIONS ? A_BOLD : 0);
		SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION, A_BOLD);
		SbUiCursesPut (4, split + 2, cols - split - 4, "NOW PLAYING");
		if (spectrumRows > 0)
			SbUiCursesPut (spectrumY, split + 2, cols - split - 4, "SPECTRUM");
		if (upcomingRows > 0) {
			char upcomingTitle[64];
			snprintf (upcomingTitle, sizeof (upcomingTitle), "UPCOMING %zu",
					upcomingCount);
			SbUiCursesPut (upcomingY, split + 2, cols - split - 4,
					upcomingTitle);
		}
		char historyTitle[64];
		snprintf (historyTitle, sizeof (historyTitle), "RECENT %zu",
				model->historyCount);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION, A_BOLD);
		SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_RECENT ? A_BOLD : 0);
		SbUiCursesPut (historyY, split + 2, cols - split - 4, historyTitle);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_RECENT ? A_BOLD : 0);
		SbUiCursesStations (data, model, 6, 2, statusY - 7, split - 3);
		const int rightX = split + 2;
		SbUiCursesNowPlaying (data, model, 6, rightX, nowPlayingHeight, rightWidth);
		if (spectrumRows > 0) SbUiCursesSpectrum (data, model, spectrumY + 2,
				rightX, spectrumRows, rightWidth);
		if (upcomingRows > 0) SbUiCursesUpcoming (data, model, upcomingY + 1,
				rightX, upcomingRows, rightWidth);
		SbUiCursesHistory (data, model, historyY + 1, rightX,
				statusY - historyY - 2, rightWidth);
	} else if (cols >= 80 && rows >= 20) {
		const int split = cols / 3;
		SbUiCursesVLine (stdscr, 3, split, statusY - 4);
		SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_STATIONS ? A_BOLD : 0);
		SbUiCursesStationHeader (data, 4, 2, split - 3);
		SbUiCursesPut (4, split + 2, cols - split - 4, "NOW PLAYING");
		SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_STATIONS ? A_BOLD : 0);
		SbUiCursesStations (data, model, 6, 2, statusY - 7, split - 3);
		SbUiCursesNowPlaying (data, model, 6, split + 2, statusY - 7,
				cols - split - 4);
	} else {
		SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_STATIONS ? A_BOLD : 0);
		SbUiCursesStationHeader (data, 4, 2, cols - 4);
		SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION,
				data->focus == SB_TUI_FOCUS_STATIONS ? A_BOLD : 0);
		const int stationRows = (statusY - 9) / 2;
		SbUiCursesStations (data, model, 5, 2, stationRows, cols - 4);
		const int dividerY = 5 + stationRows;
		SbUiCursesHLine (stdscr, dividerY, 1, cols - 2);
		SbUiCursesAttrOn (data, SB_TUI_COLOR_SECTION, A_BOLD);
		SbUiCursesPut (dividerY + 1, 2, cols - 4, "NOW PLAYING");
		SbUiCursesAttrOff (data, SB_TUI_COLOR_SECTION, A_BOLD);
		SbUiCursesNowPlaying (data, model, dividerY + 2, 2,
				statusY - dividerY - 3, cols - 4);
	}

	if (data->helpVisible) {
		SbHelpRow helpRows[48];
		const size_t helpRowCount = SbUiCursesHelpRows (renderer, helpRows);
		const int wantedHeight = (int) helpRowCount + 5;
		const int height = wantedHeight < rows - 4 ? wantedHeight : rows - 4;
		const int width = cols < 70 ? cols - 6 : 64;
		WINDOW * const help = newwin (height, width, (rows - height) / 2,
				(cols - width) / 2);
		if (help != NULL) {
			/* Keep the penultimate row for navigation help at every size. */
			const size_t visibleRows = height > 5 ? (size_t) height - 5 : 0;
			const size_t maxOffset = helpRowCount > visibleRows ?
					helpRowCount - visibleRows : 0;
			if (data->helpOffset > maxOffset) data->helpOffset = maxOffset;
			wbkgdset (help, SbUiCursesRole (data, SB_TUI_COLOR_PRIMARY));
			SbUiCursesWAttrOn (help, data, SB_TUI_COLOR_BORDER, 0);
			SbUiCursesBox (help);
			SbUiCursesWAttrOff (help, data, SB_TUI_COLOR_BORDER, 0);
			SbUiCursesWAttrOn (help, data, SB_TUI_COLOR_TITLE, A_BOLD);
			mvwaddstr (help, 1, 2, "SIGNALBOX HELP");
			SbUiCursesWAttrOff (help, data, SB_TUI_COLOR_TITLE, A_BOLD);
			for (size_t shown = 0; shown < visibleRows &&
					data->helpOffset + shown < helpRowCount; shown++) {
				const SbHelpRow * const row = &helpRows[data->helpOffset + shown];
				const int y = 3 + (int) shown;
				if (row->kind == SB_HELP_HEADING) {
					SbUiCursesWAttrOn (help, data, SB_TUI_COLOR_SECTION, A_BOLD);
					SbUiCursesWPut (help, y, 2, width - 4, row->text);
					SbUiCursesWAttrOff (help, data, SB_TUI_COLOR_SECTION, A_BOLD);
				} else if (row->kind == SB_HELP_COMMAND) {
					SbUiCursesWAttrOn (help, data, SB_TUI_COLOR_KEY, A_BOLD);
					mvwaddnstr (help, y, 2, row->key, 11);
					SbUiCursesWAttrOff (help, data, SB_TUI_COLOR_KEY, A_BOLD);
					SbUiCursesWPut (help, y, 14, width - 16, row->text);
				}
			}
			SbUiCursesWAttrOn (help, data, SB_TUI_COLOR_MUTED, 0);
			mvwaddnstr (help, height - 2, 2,
					"Up/Down scroll  PgUp/PgDn page  Esc close", width - 6);
			SbUiCursesWAttrOff (help, data, SB_TUI_COLOR_MUTED, 0);
			if (data->helpOffset > 0) mvwaddch (help, 1, width - 6, '^');
			if (data->helpOffset < maxOffset) mvwaddch (help, 1, width - 4, 'v');
			wnoutrefresh (stdscr);
			wnoutrefresh (help);
			doupdate ();
			delwin (help);
			return;
		}
	}
	refresh ();
}

static WINDOW *SbUiCursesModal (const SbUiCursesData *data,
		const char *title, const char *prompt,
		const int wantedHeight) {
	int rows, cols;
	getmaxyx (stdscr, rows, cols);
	if (rows < 15 || cols < 50) return NULL;
	const int width = cols < 72 ? cols - 4 : 68;
	const int height = wantedHeight < rows - 2 ? wantedHeight : rows - 2;
	WINDOW *window = newwin (height, width, (rows - height) / 2,
			(cols - width) / 2);
	if (window != NULL) {
		tuiDebugPrint ("modal_open title=\"%s\" size=%dx%d\n", title,
				width, height);
		wbkgdset (window, SbUiCursesRole (data, SB_TUI_COLOR_PRIMARY));
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_BORDER, 0);
		SbUiCursesBox (window);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_BORDER, 0);
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_TITLE, A_BOLD);
		SbUiCursesWPut (window, 1, 2, width - 4, title);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_TITLE, A_BOLD);
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_PRIMARY, 0);
		SbUiCursesWPut (window, 3, 2, width - 4, prompt);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_PRIMARY, 0);
		keypad (window, TRUE);
		wnoutrefresh (stdscr);
	}
	return window;
}

bool SbUiRendererPromptText (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *prompt, char *buffer, const size_t size) {
	if (!SbUiRendererIsCurses (renderer) || size < 2) return false;
	wchar_t input[256];
	size_t length = SbUiCursesUtf8ToWide (input,
			sizeof (input) / sizeof (*input), buffer);
	if (length == (size_t) -1) length = 0;
	size_t cursor = length;
	input[length] = L'\0';
	(void) curs_set (1);
	SbUiCursesFrame (renderer, model);
	WINDOW *window = NULL;
	for (;;) {
		SbUiCursesData * const data = renderer->data;
		if (window == NULL) window = SbUiCursesModal (data, title, prompt, 8);
		if (window == NULL) continue;
		int height, width;
		getmaxyx (window, height, width);
		(void) height;
		mvwhline (window, 5, 2, ' ', width - 4);
		size_t start = 0;
		while (start < cursor && SbUiCursesWideWidth (&input[start],
				cursor - start) > width - 5) {
			start++;
		}
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_ARTIST, A_BOLD);
		mvwaddnwstr (window, 5, 2, &input[start], width - 5);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_ARTIST, A_BOLD);
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_KEY, 0);
		mvwaddnstr (window, 6, 2, "Enter: submit   Esc: cancel", width - 4);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_KEY, 0);
		const int cursorCells = SbUiCursesWideWidth (&input[start], cursor - start);
		wmove (window, 5, 2 + (cursorCells >= 0 ? cursorCells : 0));
		wnoutrefresh (window);
		doupdate ();
		const SbTuiInput read = SbUiCursesReadKey (window,
				SB_TUI_INPUT_MODAL, false);
		const int result = read.status;
		const int key = read.key;
		if (result == ERR) continue;
		if (key == KEY_RESIZE) {
			delwin (window);
			window = NULL;
			if (SbUiCursesResize (data)) SbUiCursesFrame (renderer, model);
			continue;
		}
		if (key == 27) {
			delwin (window);
			(void) curs_set (0);
			SbUiCursesFrame (renderer, model);
			return false;
		}
		if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			const size_t converted = SbUiCursesWideToUtf8 (buffer, size, input);
			if (converted == (size_t) -1) buffer[0] = '\0';
			else buffer[converted] = '\0';
			delwin (window);
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

bool SbUiRendererPromptLogin (SbUiRenderer *renderer, const SbUiModel *model,
		char *username, const size_t usernameSize, char *password,
		const size_t passwordSize, bool *remember, const char *error) {
	if (!SbUiRendererIsCurses (renderer) || usernameSize < 2 ||
			passwordSize < 2 || remember == NULL) return false;
	size_t userLen = strlen (username), passLen = 0;
	if (userLen >= usernameSize) userLen = usernameSize - 1;
	password[0] = '\0';
	int field = username[0] == '\0' ? 0 : 1;
	(void) curs_set (1);
	SbUiCursesFrame (renderer, model);
	WINDOW *window = NULL;
	for (;;) {
		SbUiCursesData * const data = renderer->data;
		if (window == NULL) window = SbUiCursesModal (data, "SIGNALBOX LOGIN",
				error != NULL ? error : "Sign in to Pandora", 12);
		if (window == NULL) continue;
		const int width = getmaxx (window);
		/* Redrawing this small window updates only changed cells.  In particular,
		 * keep stdscr out of the per-keystroke refresh path on PDCurses VT. */
		werase (window);
		wbkgdset (window, SbUiCursesRole (data, SB_TUI_COLOR_PRIMARY));
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_BORDER, 0);
		SbUiCursesBox (window);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_BORDER, 0);
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_TITLE, A_BOLD);
		SbUiCursesWPut (window, 1, 2, width - 4, "SIGNALBOX LOGIN");
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_TITLE, A_BOLD);
		if (error != NULL) {
			SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_ERROR, A_BOLD);
			SbUiCursesWPut (window, 3, 2, width - 4, error);
			SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_ERROR, A_BOLD);
		} else SbUiCursesWPut (window, 3, 2, width - 4, "Sign in to Pandora");
		const char *labels[] = {"Pandora email:", "Password:",
				"Remember securely:"};
		for (int i = 0; i < 3; i++) {
			SbUiCursesWAttrOn (window, data, i == field ? SB_TUI_COLOR_TITLE :
					SB_TUI_COLOR_PRIMARY, i == field ? A_BOLD : 0);
			mvwaddnstr (window, 5 + i, 2, labels[i], 20);
			SbUiCursesWAttrOff (window, data, i == field ? SB_TUI_COLOR_TITLE :
					SB_TUI_COLOR_PRIMARY, i == field ? A_BOLD : 0);
		}
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_ARTIST,
				field == 0 ? A_BOLD : 0);
		SbUiCursesWPut (window, 5, 22, width - 25, username);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_ARTIST,
				field == 0 ? A_BOLD : 0);
		char masked[100];
		const size_t visiblePass = passLen < sizeof (masked) - 1 ? passLen :
				sizeof (masked) - 1;
		memset (masked, '*', visiblePass); masked[visiblePass] = '\0';
		mvwaddnstr (window, 6, 22, masked, width - 25);
		mvwprintw (window, 7, 22, "%s", *remember ? "[x]" : "[ ]");
		SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_KEY, 0);
		mvwaddnstr (window, 9, 2,
				"Tab: next   Space: toggle   Enter: sign in   Esc: cancel",
				width - 4);
		SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_KEY, 0);
		if (field == 0) wmove (window, 5, 22 + (int) userLen);
		else if (field == 1) wmove (window, 6, 22 + (int) visiblePass);
		else (void) curs_set (0);
		wnoutrefresh (window);
		doupdate ();
		const SbTuiInput read = SbUiCursesReadKey (window,
				SB_TUI_INPUT_LOGIN, field == 1);
		const int result = read.status;
		const int key = read.key;
		if (result == ERR) continue;
		if (key == KEY_RESIZE) {
			delwin (window);
			window = NULL;
			if (SbUiCursesResize (data)) SbUiCursesFrame (renderer, model);
			continue;
		}
		if (key == 27) {
			SbCredentialClear (password, passwordSize);
			delwin (window);
			(void) curs_set (0); SbUiCursesFrame (renderer, model); return false;
		}
		if (key == '\t' || key == KEY_DOWN) { field = (field + 1) % 3; continue; }
		if (key == KEY_BTAB || key == KEY_UP) { field = (field + 2) % 3; continue; }
		if (field == 2 && key == ' ') { *remember = !*remember; continue; }
		if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			if (field < 2) { field++; continue; }
			if (userLen > 0 && passLen > 0) {
				delwin (window);
				(void) curs_set (0); SbUiCursesFrame (renderer, model); return true;
			}
			continue;
		}
		char *buffer = field == 0 ? username : password;
		size_t *length = field == 0 ? &userLen : &passLen;
		const size_t capacity = field == 0 ? usernameSize : passwordSize;
		if (field < 2 && (key == KEY_BACKSPACE || key == 127 || key == 8) &&
				*length > 0) buffer[--*length] = '\0';
		else if (field < 2 && key == KEY_DC && *length > 0)
			buffer[--*length] = '\0';
		else if (field < 2 && result == OK && iswprint ((wint_t) key) &&
				*length + 1 < capacity) {
			buffer[(*length)++] = (char) key; buffer[*length] = '\0';
		}
	}
}

bool SbUiRendererConfirm (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *prompt) {
	if (!SbUiRendererIsCurses (renderer)) return false;
	bool yes = false;
	for (;;) {
		SbUiCursesFrame (renderer, model);
		SbUiCursesData * const data = renderer->data;
		WINDOW *window = SbUiCursesModal (data, title, prompt, 8);
		if (window == NULL) continue;
		SbUiCursesWAttrOn (window, data, yes ? SB_TUI_COLOR_WARNING :
				SB_TUI_COLOR_ERROR, A_BOLD);
		mvwprintw (window, 5, 2, "%s   %s", yes ? "[YES]" : " Yes ",
				yes ? " No " : "[NO]");
		SbUiCursesWAttrOff (window, data, yes ? SB_TUI_COLOR_WARNING :
				SB_TUI_COLOR_ERROR, A_BOLD);
		mvwaddnstr (window, 6, 2,
				"y/n or arrows; Enter confirms selection; Esc cancels", getmaxx (window) - 4);
		wrefresh (window);
		const int key = SbUiCursesReadKey (window,
				SB_TUI_INPUT_MODAL, false).key;
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
		SbUiCursesData * const data = renderer->data;
		WINDOW *window = SbUiCursesModal (data, title,
				"Up/Down or j/k; Enter selects; Esc cancels", height);
		if (window == NULL) continue;
		int wh, ww;
		getmaxyx (window, wh, ww);
		const size_t visible = wh > 6 ? (size_t) wh - 6 : 1;
		if (selected < offset) offset = selected;
		if (selected >= offset + visible) offset = selected - visible + 1;
		for (size_t row = 0; row < visible && offset + row < count; row++) {
			if (offset + row == selected) SbUiCursesWAttrOn (window, data,
					SB_TUI_COLOR_SELECTED, A_REVERSE);
			const char *item = items[offset + row] != NULL ?
					items[offset + row] : "(unavailable)";
			SbUiCursesWPut (window, 5 + (int) row, 2, ww - 4, item);
			if (offset + row == selected) SbUiCursesWAttrOff (window, data,
					SB_TUI_COLOR_SELECTED, A_REVERSE);
		}
		wrefresh (window);
		const int key = SbUiCursesReadKey (window,
				SB_TUI_INPUT_MODAL, false).key;
		delwin (window);
		if (key == 27) return -1;
		if ((key == KEY_UP || key == 'k') && selected > 0) selected--;
		else if ((key == KEY_DOWN || key == 'j') && selected + 1 < count) selected++;
		else if (key == KEY_HOME) selected = 0;
		else if (key == KEY_END) selected = count - 1;
		else if (key == KEY_PPAGE) selected = selected > visible ?
				selected - visible : 0;
		else if (key == KEY_NPAGE) selected = selected + visible < count ?
				selected + visible : count - 1;
		else if (key == '\n' || key == '\r' || key == KEY_ENTER) return (int) selected;
	}
}

int SbUiRendererSelectHistory (SbUiRenderer *renderer,
		const SbUiModel *model) {
	if (!SbUiRendererIsCurses (renderer) || model->historyCount == 0) return -1;
	/* The main event loop is paused while this modal is active, so its count is
	 * a stable snapshot even though resize events rebuild the window. */
	const size_t count = model->historyCount;
	size_t selected = 0, offset = 0;
	for (;;) {
		SbUiCursesFrame (renderer, model);
		int rows, cols;
		getmaxyx (stdscr, rows, cols);
		const int height = rows < 28 ? rows - 2 : 26;
		SbUiCursesData * const data = renderer->data;
		char title[80];
		snprintf (title, sizeof (title), "SESSION HISTORY  %zu tracks", count);
		WINDOW *window = SbUiCursesModal (data, title,
				"Arrows/j/k, PgUp/PgDn, Home/End; Enter selects; Esc closes",
				height);
		if (window == NULL) continue;
		int wh, ww;
		getmaxyx (window, wh, ww);
		const int availableRows = wh > 6 ? wh - 6 : 1;
		const int entryWidth = ww - 10;
		if (selected < offset) offset = selected;
		for (;;) {
			int selectedRows = 0;
			for (size_t i = offset; i <= selected; i++) {
				selectedRows += SbUiCursesHistoryWraps (&model->history[i],
						entryWidth) ? 2 : 1;
			}
			if (selectedRows <= availableRows || offset == selected) break;
			offset++;
		}
		int usedRows = 0;
		size_t visible = 0;
		for (size_t row = 0; offset + row < count; row++) {
			const size_t index = offset + row;
			const int cost = SbUiCursesHistoryWraps (&model->history[index],
					entryWidth) ? 2 : 1;
			if (usedRows + cost > availableRows) break;
			char played[8] = "--:--";
			struct tm local;
			if (model->history[index].playedAt != (time_t) 0 &&
					SbPlatformLocalTime (model->history[index].playedAt, &local)) {
				strftime (played, sizeof (played), "%H:%M", &local);
			}
			SbUiCursesWAttrOn (window, data, SB_TUI_COLOR_MUTED, 0);
			SbUiCursesWPut (window, 5 + usedRows, 2, 5, played);
			SbUiCursesWAttrOff (window, data, SB_TUI_COLOR_MUTED, 0);
			const int rowY = 5 + usedRows;
			usedRows += SbUiCursesHistoryRow (data, window, &model->history[index],
					rowY, 8, entryWidth, index == selected);
			visible++;
		}
		wrefresh (window);
		const int key = SbUiCursesReadKey (window,
				SB_TUI_INPUT_MODAL, false).key;
		delwin (window);
		if (key == 27) return -1;
		if ((key == KEY_UP || key == 'k') && selected > 0) selected--;
		else if ((key == KEY_DOWN || key == 'j') && selected + 1 < count) selected++;
		else if (key == KEY_HOME) selected = 0;
		else if (key == KEY_END) selected = count - 1;
		else if (key == KEY_PPAGE) selected = selected > visible ?
				selected - visible : 0;
		else if (key == KEY_NPAGE) selected = selected + visible < count ?
				selected + visible : count - 1;
		else if (key == '\n' || key == '\r' || key == KEY_ENTER)
			return (int) selected;
	}
}

void SbUiRendererTextModal (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *text) {
	if (!SbUiRendererIsCurses (renderer)) return;
	size_t offset = 0;
	for (;;) {
		SbUiCursesFrame (renderer, model);
		int rows, cols;
		getmaxyx (stdscr, rows, cols);
		(void) cols;
		SbUiCursesData * const data = renderer->data;
		WINDOW *window = SbUiCursesModal (data, title,
				"Up/Down or j/k scroll; Esc or Enter closes", rows < 24 ? rows - 2 : 22);
		if (window == NULL) continue;
		int wh, ww;
		getmaxyx (window, wh, ww);
		const int lineWidth = ww - 4;
		char copy[2048];
		snprintf (copy, sizeof (copy), "%s", text != NULL ? text : "No information available.");
		const char *lines[128];
		size_t lineCount = 0;
		char *cursor = copy;
		while (*cursor != '\0' && lineCount < sizeof (lines) / sizeof (*lines)) {
			while (*cursor == ' ' || *cursor == '\n') cursor++;
			if (*cursor == '\0') break;
			lines[lineCount++] = cursor;
			char *end = cursor;
			char *lastSpace = NULL;
			int cells = 0;
			while (*end != '\0' && *end != '\n' && cells < lineWidth) {
				if (*end == ' ') lastSpace = end;
				end++; cells++;
			}
			if (*end == '\0') break;
			char *cut = (*end == '\n' || cells < lineWidth) ? end :
					(lastSpace != NULL ? lastSpace : end);
			*cut = '\0';
			cursor = cut + 1;
		}
		const size_t visible = wh > 6 ? (size_t) wh - 6 : 1;
		if (offset >= lineCount) offset = lineCount > 0 ? lineCount - 1 : 0;
		for (size_t i = 0; i < visible && offset + i < lineCount; i++)
			SbUiCursesWPut (window, 5 + (int) i, 2, lineWidth,
					lines[offset + i]);
		wrefresh (window);
		const int key = SbUiCursesReadKey (window,
				SB_TUI_INPUT_MODAL, false).key;
		delwin (window);
		if (key == 27 || key == '\n' || key == '\r' || key == KEY_ENTER) {
			SbUiCursesFrame (renderer, model);
			return;
		}
		if ((key == KEY_UP || key == 'k') && offset > 0) offset--;
		else if ((key == KEY_DOWN || key == 'j') && offset + visible < lineCount)
			offset++;
	}
}

void SbUiRendererSongDetails (SbUiRenderer *renderer, const SbUiModel *model,
		const PianoSong_t *song, const PianoStation_t *station,
		const time_t playedAt) {
	if (!SbUiRendererIsCurses (renderer) || song == NULL) return;
	char duration[16], played[64] = "";
	SbUiCursesTime (duration, sizeof (duration), song->length);
	if (playedAt != (time_t) 0) {
		struct tm local;
		if (SbPlatformLocalTime (playedAt, &local))
			strftime (played, sizeof (played), "%Y-%m-%d %H:%M:%S", &local);
	}
	SbUiCursesData * const data = renderer->data;
	const char *rating = SbUiCursesRating (data, song);
	char details[1600];
	snprintf (details, sizeof (details),
			"Artist: %s\nTrack: %s\nAlbum: %s\nStation: %s\nLength: %s\n%s%s%s%s%s",
			song->artist != NULL ? song->artist : "--",
			song->title != NULL ? song->title : "--",
			song->album != NULL ? song->album : "--",
			station != NULL && station->name != NULL ? station->name : "--",
			duration, rating, played[0] != '\0' ? "\nPlayed: " : "",
			played, song->detailUrl != NULL ? "\nDetails: " : "",
			song->detailUrl != NULL ? song->detailUrl : "");
	SbUiRendererTextModal (renderer, model, "SONG DETAILS", details);
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
		SbUiCursesData * const data = renderer->data;
		WINDOW *window = SbUiCursesModal (data, title,
				"Up/Down or j/k; Space toggles; Enter saves; Esc cancels", height);
		if (window == NULL) continue;
		int wh, ww;
		getmaxyx (window, wh, ww);
		const size_t visible = wh > 6 ? (size_t) wh - 6 : 1;
		if (selected < offset) offset = selected;
		if (selected >= offset + visible) offset = selected - visible + 1;
		for (size_t row = 0; row < visible && offset + row < count; row++) {
			const size_t index = offset + row;
			if (index == selected) SbUiCursesWAttrOn (window, data,
					SB_TUI_COLOR_SELECTED, A_REVERSE);
			mvwprintw (window, 5 + (int) row, 2, "[%c] ",
					checked[index] ? 'x' : ' ');
			SbUiCursesWPut (window, 5 + (int) row, 6, ww - 8,
					items[index] != NULL ? items[index] : "(unavailable)");
			if (index == selected) SbUiCursesWAttrOff (window, data,
					SB_TUI_COLOR_SELECTED, A_REVERSE);
		}
		wrefresh (window);
		const int key = SbUiCursesReadKey (window,
				SB_TUI_INPUT_MODAL, false).key;
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

static void SbUiCursesLocalNotice (SbUiCursesData *data, const char *notice) {
	pthread_mutex_lock (&data->statusLock);
	snprintf (data->status, sizeof (data->status), "%s", notice);
	data->statusSeverity = SB_UI_NOTICE_INFO;
	data->statusExpires = time (NULL) + 4;
	pthread_mutex_unlock (&data->statusLock);
}

static void SbUiCursesFilter (SbUiRenderer *renderer, const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	char filter[sizeof (data->browser.filter)];
	strcpy (filter, data->browser.filter);
	while (true) {
		char prompt[192];
		snprintf (prompt, sizeof (prompt), "Filter: %s%s", filter,
				strlen (filter) + 1 < sizeof (filter) ? "_" : "");
		SbUiCursesLocalNotice (data, prompt);
		SbUiCursesFrame (renderer, model);
		const int key = SbUiCursesReadKey (stdscr,
				SB_TUI_INPUT_MODAL, false).key;
		if (key == ERR) continue;
		if (key == KEY_RESIZE) {
			SbUiCursesResize (data);
			continue;
		}
		if (key == '\n' || key == '\r' || key == KEY_ENTER) break;
		if (key == 27) {
			filter[0] = '\0';
			SbStationBrowserSetFilter (&data->browser, filter);
			SbUiCursesRebuildStations (data, model, true);
			break;
		}
		const size_t length = strlen (filter);
		if ((key == KEY_BACKSPACE || key == 127 || key == 8) && length > 0) {
			filter[length - 1] = '\0';
		} else if (key >= 0 && key <= UCHAR_MAX && isprint ((unsigned char) key) &&
				length + 1 < sizeof (filter)) {
			filter[length] = (char) key;
			filter[length + 1] = '\0';
		} else {
			continue;
		}
		SbStationBrowserSetFilter (&data->browser, filter);
		SbUiCursesRebuildStations (data, model, true);
	}
	SbUiCursesLocalNotice (data, data->browser.filter[0] != '\0' ?
			"Station filter active" : "Station filter cleared");
	SbUiCursesFrame (renderer, model);
}

static bool SbUiCursesNormalizeNumericInput (const int key,
		const int *sequence, const size_t sequenceLength, char *digit) {
	if (key >= '0' && key <= '9') {
		*digit = (char) key;
		return true;
	}
	/* xterm-compatible application keypad: ESC O p through ESC O y. */
	if (key == 27 && sequenceLength == 2 && sequence[0] == 'O' &&
			sequence[1] >= 'p' && sequence[1] <= 'y') {
		*digit = (char) ('0' + sequence[1] - 'p');
		return true;
	}
	return false;
}

static PianoStation_t *SbUiCursesJump (SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	char digits[16] = "";
	PianoStation_t *activate = NULL;
	data->jumpMode = true;
	while (true) {
		char prompt[80];
		snprintf (prompt, sizeof (prompt), "Jump to station #: %s_", digits);
		SbUiCursesLocalNotice (data, prompt);
		SbUiCursesFrame (renderer, model);
		const int key = SbUiCursesReadKey (stdscr,
				SB_TUI_INPUT_NUMERIC_JUMP, false).key;
		if (key == ERR) continue;
		if (key == KEY_RESIZE) {
			SbUiCursesResize (data);
			continue;
		}
		if (key == 27) {
			/* Some macOS terminals emit application-keypad digits as ESC O p..y,
			 * even when the terminfo entry does not teach ncurses those strings.
			 * Consume the whole sequence here so none of it can become a normal
			 * application command.  A bare Esc remains cancel. */
			int sequence[8];
			size_t sequenceLength = 0;
			wtimeout (stdscr, 35);
			while (sequenceLength < sizeof (sequence) / sizeof (sequence[0])) {
				const int next = SbUiCursesReadKey (stdscr,
						SB_TUI_INPUT_NUMERIC_JUMP, false).key;
				if (next == ERR) break;
				sequence[sequenceLength++] = next;
				char digit;
				if (SbUiCursesNormalizeNumericInput (key, sequence,
						sequenceLength, &digit)) break;
			}
			wtimeout (stdscr, 1000);
			if (sequenceLength == 0) {
				tuiDebugPrint ("jump_input code=27 class=cancel length=%zu value=%s\n",
						strlen (digits), digits);
				break;
			}
			char digit;
			if (SbUiCursesNormalizeNumericInput (key, sequence, sequenceLength,
					&digit)) {
				const size_t length = strlen (digits);
				if (length + 1 < sizeof (digits)) {
					digits[length] = digit;
					digits[length + 1] = '\0';
				}
				tuiDebugPrint ("jump_input code=27 sequence=O%c class=digit digit=%c length=%zu value=%s\n",
						sequence[1], digit, strlen (digits), digits);
			} else {
				tuiDebugPrint ("jump_input code=27 class=ignored-special sequence_length=%zu length=%zu value=%s\n",
						sequenceLength, strlen (digits), digits);
			}
			continue;
		}
		if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			size_t target = 0;
			bool valid = digits[0] != '\0';
			for (const char *p = digits; valid && *p != '\0'; p++) {
				const unsigned int digit = (unsigned int) (*p - '0');
				if (target > (SIZE_MAX - digit) / 10) valid = false;
				else target = target * 10 + digit;
			}
			if (valid && target > 0 && target <= data->browser.visibleCount) {
				data->selectedIndex = target - 1;
				activate = (PianoStation_t *) SbUiCursesStationAt (data,
						data->selectedIndex);
				SbUiCursesLocalNotice (data, "Tuning selected station");
			} else {
				SbUiCursesLocalNotice (data, "Station number is out of range");
			}
			tuiDebugPrint ("jump_input code=%d class=confirm valid=%s length=%zu value=%s\n",
					key, valid ? "yes" : "no", strlen (digits), digits);
			break;
		}
		const size_t length = strlen (digits);
		if ((key == KEY_BACKSPACE || key == KEY_DC || key == 127 || key == 8) &&
				length > 0) {
			digits[length - 1] = '\0';
			tuiDebugPrint ("jump_input code=%d class=backspace length=%zu value=%s\n",
					key, strlen (digits), digits);
		} else {
			char digit;
			if (SbUiCursesNormalizeNumericInput (key, NULL, 0, &digit) &&
					length + 1 < sizeof (digits)) {
			digits[length] = digit;
			digits[length + 1] = '\0';
			tuiDebugPrint ("jump_input code=%d class=digit length=%zu value=%s\n",
					key, strlen (digits), digits);
			} else {
			tuiDebugPrint ("jump_input code=%d class=ignored-special length=%zu value=%s\n",
					key, length, digits);
			}
		}
	}
	data->jumpMode = false;
	SbUiCursesFrame (renderer, model);
	return activate;
}

static SbUiCommandEvent SbUiCursesReadCommand (SbUiRenderer *renderer,
		const SbUiModel *model) {
	SbUiCursesData * const data = renderer->data;
	const int key = SbUiCursesReadKey (stdscr, data->helpVisible ?
			SB_TUI_INPUT_HELP : SB_TUI_INPUT_MAIN, false).key;
	if (key == ERR) {
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (key == KEY_RESIZE) {
		tuiDebugPrint ("resize\n");
		SbUiCursesResize (data);
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (data->helpVisible && (key == KEY_UP || key == 'k' ||
			key == KEY_DOWN || key == 'j' || key == KEY_HOME ||
			key == KEY_END || key == KEY_PPAGE || key == KEY_NPAGE)) {
		SbHelpRow helpRows[48];
		const size_t count = SbUiCursesHelpRows (renderer, helpRows);
		int rows, cols;
		getmaxyx (stdscr, rows, cols);
		(void) cols;
		const size_t page = rows > 8 ? (size_t) rows - 8 : 1;
		if ((key == KEY_UP || key == 'k') && data->helpOffset > 0)
			data->helpOffset--;
		else if ((key == KEY_DOWN || key == 'j') && data->helpOffset + 1 < count)
			data->helpOffset++;
		else if (key == KEY_HOME) data->helpOffset = 0;
		else if (key == KEY_END) data->helpOffset = count > 0 ? count - 1 : 0;
		else if (key == KEY_PPAGE) data->helpOffset = data->helpOffset > page ?
				data->helpOffset - page : 0;
		else if (key == KEY_NPAGE) data->helpOffset =
				data->helpOffset + page < count ? data->helpOffset + page :
				(count > 0 ? count - 1 : 0);
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (data->helpVisible && key == 27) {
		data->helpVisible = false;
		data->helpOffset = 0;
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && (key == KEY_UP || key == 'k' ||
			key == KEY_DOWN || key == 'j' || key == KEY_HOME ||
			key == KEY_END || key == KEY_PPAGE || key == KEY_NPAGE)) {
		if (data->focus == SB_TUI_FOCUS_RECENT) {
			const size_t count = model->historyCount;
			int rows, cols;
			getmaxyx (stdscr, rows, cols);
			(void) cols;
			const size_t page = rows > 22 ? (size_t) rows - 21 : 1;
			if (count > 0) {
				if ((key == KEY_UP || key == 'k') && data->recentSelected > 0)
					data->recentSelected--;
				else if ((key == KEY_DOWN || key == 'j') &&
						data->recentSelected + 1 < count) data->recentSelected++;
				else if (key == KEY_HOME) data->recentSelected = 0;
				else if (key == KEY_END) data->recentSelected = count - 1;
				else if (key == KEY_PPAGE) data->recentSelected =
						data->recentSelected > page ?
						data->recentSelected - page : 0;
				else if (key == KEY_NPAGE) data->recentSelected =
						data->recentSelected + page < count ?
						data->recentSelected + page : count - 1;
			}
			SbUiCursesFrame (renderer, model);
			return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
		}
		SbUiCursesClampSelection (data, model, 1);
		const size_t count = SbUiCursesStationCount (data);
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
		tuiDebugPrint ("station_selection index=%zu count=%zu\n",
				data->selectedIndex, count);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && (key == '\n' || key == '\r' || key == KEY_ENTER)) {
		if (data->focus == SB_TUI_FOCUS_RECENT &&
				data->recentSelected < model->historyCount) {
			return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL, true,
					data->recentSelected};
		}
		const PianoStation_t * const station = SbUiCursesStationAt (data,
				data->selectedIndex);
		return (SbUiCommandEvent) {station != NULL ?
				SB_UI_CMD_ACTIVATE_STATION : SB_UI_CMD_NONE,
				(PianoStation_t *) station};
	}
	if (!data->helpVisible && (key == '\t' || key == KEY_BTAB)) {
		int rows, cols;
		getmaxyx (stdscr, rows, cols);
		if (data->focus == SB_TUI_FOCUS_STATIONS && cols >= 80 && rows >= 24)
			data->focus = SB_TUI_FOCUS_RECENT;
		else data->focus = SB_TUI_FOCUS_STATIONS;
		if (data->focus == SB_TUI_FOCUS_RECENT && model->historyCount == 0)
			data->recentSelected = data->recentOffset = 0;
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && key == '/') {
		SbUiCursesFilter (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && key == '#') {
		PianoStation_t *station = SbUiCursesJump (renderer, model);
		return (SbUiCommandEvent) {station != NULL ?
				SB_UI_CMD_ACTIVATE_STATION : SB_UI_CMD_NONE, station};
	}
	if (!data->helpVisible && key == 'z') {
		data->browser.sort = (SbStationSort) ((data->browser.sort + 1) %
				SB_STATION_SORT_COUNT);
		SbUiCursesRebuildStations (data, model, true);
		SbUiCursesLocalNotice (data, SbStationBrowserSortName (data->browser.sort));
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && key == 'f') {
		const PianoStation_t * const station = SbUiCursesStationAt (data,
				data->selectedIndex);
		if (station != NULL) {
			const bool wasFavorite = SbStationBrowserIsFavorite (&data->browser, station);
			if (SbStationBrowserToggleFavorite (&data->browser, station)) {
				SbUiCursesRebuildStations (data, model, true);
				SbUiCursesLocalNotice (data, wasFavorite ?
						"Removed from favorites" : "Added to favorites");
			} else {
				SbUiCursesLocalNotice (data, "Could not save favorites");
			}
		}
		SbUiCursesFrame (renderer, model);
		return (SbUiCommandEvent) {SB_UI_CMD_NONE, NULL};
	}
	if (!data->helpVisible && key == 'V' &&
			SbUiCursesVisualizerKeyAvailable (renderer)) {
		SbUiCursesLocalNotice (data, model->visualizerEnabled ?
				"Spectrum analyzer off" : "Spectrum analyzer on");
		wtimeout (stdscr, model->visualizerEnabled ? 1000 : 80);
		return (SbUiCommandEvent) {SB_UI_CMD_TOGGLE_VISUALIZER, NULL};
	}
	if (key >= 0 && key <= UCHAR_MAX) {
		const SbUiCommand command = BarUiCommandFromKey (renderer->settings,
				(char) key);
		if (command == SB_UI_CMD_HELP) {
			data->helpVisible = !data->helpVisible;
			data->helpOffset = 0;
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
				command == SB_UI_CMD_BAN || command == SB_UI_CMD_INFO ||
				command == SB_UI_CMD_EXPLAIN || command == SB_UI_CMD_VOLUME_DOWN ||
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
				station = SbUiCursesStationAt (data, data->selectedIndex);
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
		tuiDebugPrint ("renderer_shutdown\n");
#ifdef SIGNALBOX_PDCURSESMOD
		SbUiCursesCloseKeyLog ();
#endif
		endwin ();
		delscreen (data->screen);
		pthread_mutex_destroy (&data->statusLock);
		SbStationBrowserDestroy (&data->browser);
		free (data);
		renderer->data = NULL;
	}
}

static SbTuiPalette SbUiCursesPalette (const SbTuiTheme theme,
		const bool rich) {
	SbTuiPalette palette;
	if (theme == SB_TUI_THEME_AMBER) {
		palette = (SbTuiPalette) {{COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW,
				COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW,
				COLOR_CYAN, COLOR_CYAN, COLOR_MAGENTA, COLOR_MAGENTA,
				COLOR_YELLOW, COLOR_CYAN, COLOR_YELLOW, COLOR_RED,
				COLOR_MAGENTA, COLOR_CYAN}};
		if (rich) {
			palette.colors[SB_TUI_COLOR_BORDER] = 172;
			palette.colors[SB_TUI_COLOR_TITLE] = 220;
			palette.colors[SB_TUI_COLOR_SECTION] = 214;
			palette.colors[SB_TUI_COLOR_PRIMARY] = 178;
			palette.colors[SB_TUI_COLOR_MUTED] = 136;
			palette.colors[SB_TUI_COLOR_STATION] = 178;
			palette.colors[SB_TUI_COLOR_STATION_ACTIVE] = 220;
			palette.colors[SB_TUI_COLOR_WARNING] = 214;
		}
	} else if (theme == SB_TUI_THEME_NEUTRAL) {
		palette = (SbTuiPalette) {{COLOR_WHITE, COLOR_CYAN, COLOR_MAGENTA,
				COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_GREEN, COLOR_CYAN,
				COLOR_CYAN, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_GREEN,
				COLOR_CYAN, COLOR_YELLOW, COLOR_RED, COLOR_MAGENTA, COLOR_CYAN}};
		if (rich) {
			palette.colors[SB_TUI_COLOR_BORDER] = 245;
			palette.colors[SB_TUI_COLOR_PRIMARY] = 252;
			palette.colors[SB_TUI_COLOR_MUTED] = 244;
			palette.colors[SB_TUI_COLOR_ALBUM] = 141;
			palette.colors[SB_TUI_COLOR_TRACK] = 205;
		}
	} else {
		palette = (SbTuiPalette) {{COLOR_GREEN, COLOR_GREEN, COLOR_GREEN,
				COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_GREEN, COLOR_CYAN,
				COLOR_CYAN, COLOR_MAGENTA, COLOR_MAGENTA, COLOR_GREEN,
				COLOR_CYAN, COLOR_YELLOW, COLOR_RED, COLOR_MAGENTA, COLOR_CYAN}};
		if (rich) {
			palette.colors[SB_TUI_COLOR_BORDER] = 34;
			palette.colors[SB_TUI_COLOR_TITLE] = 46;
			palette.colors[SB_TUI_COLOR_SECTION] = 40;
			palette.colors[SB_TUI_COLOR_PRIMARY] = 40;
			palette.colors[SB_TUI_COLOR_MUTED] = 65;
			palette.colors[SB_TUI_COLOR_STATION] = 40;
			palette.colors[SB_TUI_COLOR_STATION_ACTIVE] = 46;
			palette.colors[SB_TUI_COLOR_SELECTED] = 51;
			palette.colors[SB_TUI_COLOR_ARTIST] = 51;
			palette.colors[SB_TUI_COLOR_TRACK] = 205;
			palette.colors[SB_TUI_COLOR_ALBUM] = 141;
			palette.colors[SB_TUI_COLOR_PROGRESS_FILLED] = 48;
			palette.colors[SB_TUI_COLOR_STATUS] = 51;
			palette.colors[SB_TUI_COLOR_WARNING] = 214;
			palette.colors[SB_TUI_COLOR_LOVED] = 205;
			palette.colors[SB_TUI_COLOR_KEY] = 51;
		}
	}
	return palette;
}

static bool SbUiCursesInitPalette (SbUiCursesData *data,
		const SbTuiTheme theme) {
	if (start_color () == ERR) return false;
	short background = COLOR_BLACK;
#ifdef NCURSES_VERSION
	if (use_default_colors () == OK) background = -1;
#endif
	const SbTuiPalette palette = SbUiCursesPalette (theme, COLORS >= 256);
	short nextPair = 1;
	for (int role = 0; role < SB_TUI_COLOR_COUNT; role++) {
		for (int previous = 0; previous < role; previous++) {
			if (palette.colors[previous] == palette.colors[role]) {
				data->roleAttrs[role] = data->roleAttrs[previous];
				break;
			}
		}
		if (data->roleAttrs[role] != 0) continue;
		if (nextPair >= COLOR_PAIRS ||
				init_pair (nextPair, palette.colors[role], background) == ERR) {
			data->roleAttrs[role] = 0;
			continue;
		}
		data->roleAttrs[role] = COLOR_PAIR (nextPair++);
	}
	return nextPair > 1;
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
	if (!SbStationBrowserInit (&data->browser)) {
		SbStationBrowserDestroy (&data->browser);
		free (data);
		return false;
	}
	data->screen = newterm (NULL, stdout, stdin);
	if (data->screen == NULL) {
		SbStationBrowserDestroy (&data->browser);
		free (data);
		return false;
	}
	set_term (data->screen);
	if (pthread_mutex_init (&data->statusLock, NULL) != 0) {
		endwin ();
		delscreen (data->screen);
		SbStationBrowserDestroy (&data->browser);
		free (data);
		return false;
	}
	strcpy (data->status, "Ready");
	data->statusSeverity = SB_UI_NOTICE_INFO;
	data->statusExpires = time (NULL) + 4;
	data->unicodeSymbols = SbUiCursesCellWidth (L'♥') == 1 &&
			SbUiCursesCellWidth (L'›') == 1;
	data->unicodeBlocks = SbUiCursesCellWidth (L'█') == 1;
	cbreak ();
	noecho ();
	keypad (stdscr, TRUE);
	wtimeout (stdscr, settings->visualizerSpectrum ? 80 : 1000);
	getmaxyx (stdscr, data->screenRows, data->screenCols);
	(void) curs_set (0);
	if (has_colors () && theme != SB_TUI_THEME_MONO && getenv ("NO_COLOR") == NULL) {
		data->colors = SbUiCursesInitPalette (data, theme);
	}
	renderer->ops = &cursesOps;
	renderer->settings = settings;
	renderer->data = data;
#ifdef SIGNALBOX_PDCURSESMOD
	SbUiCursesOpenKeyLog ();
#endif
	tuiDebugPrint ("renderer_init theme=%d colors=%s\n", (int) theme,
			data->colors ? "yes" : "no");
	return true;
}
