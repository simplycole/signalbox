#include "terminal.h"

#include <windows.h>

typedef struct {
	HANDLE input;
	HANDLE output;
	DWORD inputMode;
	DWORD outputMode;
	UINT inputCodePage;
	UINT outputCodePage;
	bool inputModeSaved;
	bool outputModeSaved;
	bool codePagesSaved;
	bool active;
} SbWindowsTerminalState;

static SbWindowsTerminalState terminalState;

bool BarTermIsInteractive (void) {
	DWORD inputMode, outputMode;
	return GetConsoleMode (GetStdHandle (STD_INPUT_HANDLE), &inputMode) != 0 &&
			GetConsoleMode (GetStdHandle (STD_OUTPUT_HANDLE), &outputMode) != 0;
}

void BarTermInit (void) {
	if (terminalState.active || !BarTermIsInteractive ()) return;
	terminalState.input = GetStdHandle (STD_INPUT_HANDLE);
	terminalState.output = GetStdHandle (STD_OUTPUT_HANDLE);
	terminalState.inputModeSaved = GetConsoleMode (terminalState.input,
			&terminalState.inputMode) != 0;
	terminalState.outputModeSaved = GetConsoleMode (terminalState.output,
			&terminalState.outputMode) != 0;
	terminalState.inputCodePage = GetConsoleCP ();
	terminalState.outputCodePage = GetConsoleOutputCP ();
	terminalState.codePagesSaved = terminalState.inputCodePage != 0 &&
			terminalState.outputCodePage != 0;
	if (terminalState.outputModeSaved) {
		const DWORD mode = terminalState.outputMode | ENABLE_PROCESSED_OUTPUT |
				ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		(void) SetConsoleMode (terminalState.output, mode);
	}
	if (terminalState.inputModeSaved) {
		DWORD mode = terminalState.inputMode;
#ifdef SIGNALBOX_PDCURSES_VT
		mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
#else
		/* WinCon reads native KEY_EVENT_RECORD input.  In particular, do not
		 * make Windows Terminal translate those records into VT sequences. */
		mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
#endif
		(void) SetConsoleMode (terminalState.input, mode);
	}
	(void) SetConsoleCP (CP_UTF8);
	(void) SetConsoleOutputCP (CP_UTF8);
	terminalState.active = true;
}

void BarTermRestore (void) {
	if (!terminalState.active) return;
	if (terminalState.inputModeSaved)
		(void) SetConsoleMode (terminalState.input, terminalState.inputMode);
	if (terminalState.outputModeSaved)
		(void) SetConsoleMode (terminalState.output, terminalState.outputMode);
	if (terminalState.codePagesSaved) {
		(void) SetConsoleCP (terminalState.inputCodePage);
		(void) SetConsoleOutputCP (terminalState.outputCodePage);
	}
	terminalState.active = false;
}
