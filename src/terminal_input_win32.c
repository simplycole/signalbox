#include "terminal_input.h"

#include <stdio.h>
#include <windows.h>
#include <pdcurses.h>

typedef struct {
	SbTerminalInputEvent event;
	WORD remaining;
	WCHAR highSurrogate;
} SbTerminalInputState;

static SbTerminalInputState inputState;

static SbTerminalInputEvent SbTerminalNoInput (void) {
	return (SbTerminalInputEvent) {.status = ERR, .key = ERR};
}

unsigned int SbTerminalDrainResizeEvents (void) {
	HANDLE const input = GetStdHandle (STD_INPUT_HANDLE);
	if (input == INVALID_HANDLE_VALUE || input == NULL) return 0;
	unsigned int count = 0;
	for (;;) {
		INPUT_RECORD record;
		DWORD peeked = 0, consumed = 0;
		if (PeekConsoleInputW (input, &record, 1, &peeked) == 0 ||
				peeked != 1 || record.EventType != WINDOW_BUFFER_SIZE_EVENT)
			break;
		if (ReadConsoleInputW (input, &record, 1, &consumed) == 0 ||
				consumed != 1) break;
		count++;
	}
	return count;
}

static int SbTerminalSpecialKey (const KEY_EVENT_RECORD * const key) {
	switch (key->wVirtualKeyCode) {
		case VK_UP: return KEY_UP;
		case VK_DOWN: return KEY_DOWN;
		case VK_LEFT: return KEY_LEFT;
		case VK_RIGHT: return KEY_RIGHT;
		case VK_PRIOR: return KEY_PPAGE;
		case VK_NEXT: return KEY_NPAGE;
		case VK_HOME: return KEY_HOME;
		case VK_END: return KEY_END;
		case VK_TAB:
			return (key->dwControlKeyState & SHIFT_PRESSED) != 0 ? KEY_BTAB : '\t';
		case VK_RETURN: return '\n';
		case VK_ESCAPE: return 27;
		case VK_BACK: return KEY_BACKSPACE;
		case VK_DELETE: return KEY_DC;
		default: return ERR;
	}
}

bool SbTerminalInputInit (void) {
	HANDLE const input = GetStdHandle (STD_INPUT_HANDLE);
	DWORD mode;
	if (input == INVALID_HANDLE_VALUE || input == NULL ||
			GetConsoleMode (input, &mode) == 0) return false;
	/* Signalbox consumes INPUT_RECORDs. PDCursesMod owns drawing only. Keep
	 * processed input as inherited, request resize records, and prevent ConPTY
	 * from replacing native key records with a VT byte stream. */
	mode |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
	mode &= ~(ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_LINE_INPUT |
			ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE);
	return SetConsoleMode (input, mode) != 0;
}

static SbTerminalInputEvent SbTerminalKeyEvent (const KEY_EVENT_RECORD *key) {
	SbTerminalInputEvent event = {
		.status = OK,
		.source = SB_TERMINAL_INPUT_WIN32_EVENT,
		.virtualKey = key->wVirtualKeyCode,
		.unicode = key->uChar.UnicodeChar,
		.controlState = key->dwControlKeyState,
		.repeatCount = key->wRepeatCount,
	};
	const int special = SbTerminalSpecialKey (key);
	if (special != ERR) {
		event.key = special;
		event.status = (special == '\t' || special == '\n' || special == 27) ?
				OK : KEY_CODE_YES;
	} else if (key->uChar.UnicodeChar != 0) {
		event.key = key->uChar.UnicodeChar;
	} else {
		event.status = ERR;
		event.key = ERR;
	}
	return event;
}

SbTerminalInputEvent SbTerminalReadInput (int timeoutMs) {
	if (inputState.remaining > 0) {
		inputState.remaining--;
		return inputState.event;
	}
	HANDLE const input = GetStdHandle (STD_INPUT_HANDLE);
	if (input == INVALID_HANDLE_VALUE || input == NULL) return SbTerminalNoInput ();
	const ULONGLONG started = GetTickCount64 ();
	const ULONGLONG deadline = timeoutMs >= 0 ?
			started + (ULONGLONG) timeoutMs : 0;
	for (;;) {
		DWORD wait = INFINITE;
		ULONGLONG remaining = (ULONGLONG) -1;
		if (timeoutMs >= 0) {
			const ULONGLONG now = GetTickCount64 ();
			if (timeoutMs != 0 && now >= deadline) return SbTerminalNoInput ();
			remaining = timeoutMs == 0 ? 0 : deadline - now;
			wait = remaining > MAXDWORD ? MAXDWORD : (DWORD) remaining;
		}
		const DWORD waitResult = WaitForSingleObject (input, wait);
		if (waitResult != WAIT_OBJECT_0)
			return SbTerminalNoInput ();
		INPUT_RECORD record;
		DWORD count = 0;
		if (ReadConsoleInputW (input, &record, 1, &count) == 0 || count != 1)
			return SbTerminalNoInput ();
		if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
			/* A drag can enqueue many intermediate geometries.  Leave keyboard and
			 * other event types in order, but collapse an adjacent resize run into
			 * one logical KEY_RESIZE so the renderer adopts the newest geometry. */
			(void) SbTerminalDrainResizeEvents ();
			return (SbTerminalInputEvent) {
				.status = KEY_CODE_YES, .key = KEY_RESIZE,
				.source = SB_TERMINAL_INPUT_WIN32_EVENT,
			};
		}
		if (record.EventType != KEY_EVENT) continue;
		if (!record.Event.KeyEvent.bKeyDown)
			continue;
		SbTerminalInputEvent event = SbTerminalKeyEvent (&record.Event.KeyEvent);
		if (event.status == ERR) continue;
		WCHAR const value = record.Event.KeyEvent.uChar.UnicodeChar;
		if (value >= 0xd800 && value <= 0xdbff && event.status == OK) {
			inputState.highSurrogate = value;
			continue;
		}
		if (value >= 0xdc00 && value <= 0xdfff && inputState.highSurrogate != 0 &&
				event.status == OK) {
			event.unicode = 0x10000u +
					(((uint32_t) inputState.highSurrogate - 0xd800u) << 10) +
					((uint32_t) value - 0xdc00u);
			event.key = (int) event.unicode;
			inputState.highSurrogate = 0;
		} else {
			inputState.highSurrogate = 0;
		}
		inputState.event = event;
		inputState.remaining = record.Event.KeyEvent.wRepeatCount > 1 ?
				record.Event.KeyEvent.wRepeatCount - 1 : 0;
		return event;
	}
}

void SbTerminalInputDiagnostic (FILE * const output) {
	if (output == NULL) return;
	HANDLE const input = GetStdHandle (STD_INPUT_HANDLE);
	DWORD mode = 0, pending = 0;
	INPUT_RECORD record;
	const DWORD type = input == INVALID_HANDLE_VALUE || input == NULL ?
			FILE_TYPE_UNKNOWN : GetFileType (input);
	const BOOL hasMode = input != INVALID_HANDLE_VALUE && input != NULL &&
			GetConsoleMode (input, &mode);
	const BOOL canPeek = input != INVALID_HANDLE_VALUE && input != NULL &&
			PeekConsoleInputW (input, &record, 1, &pending);
	fprintf (output,
			"[signalbox:input] file_type=%lu get_console_mode=%s mode=0x%08lX "
			"vt_input=%s window_input=%s processed_input=%s "
			"peek_console_input=%s pending=%lu handle_kind=%s\n",
			(unsigned long) type, hasMode ? "yes" : "no", (unsigned long) mode,
			(hasMode && (mode & ENABLE_VIRTUAL_TERMINAL_INPUT)) ? "on" : "off",
			(hasMode && (mode & ENABLE_WINDOW_INPUT)) ? "on" : "off",
			(hasMode && (mode & ENABLE_PROCESSED_INPUT)) ? "on" : "off",
			canPeek ? "yes" : "no", (unsigned long) pending,
			hasMode && type == FILE_TYPE_CHAR ? "console_buffer" :
			(type == FILE_TYPE_PIPE ? "pipe" : "other"));
	fflush (output);
}
