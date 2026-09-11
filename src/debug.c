/*
Copyright (c) 2008-2018
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

#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

static FILE *tuiDebugFile;

bool tuiDebugInit (const bool useTui) {
	if (!useTui || !tuiDebugEnable ()) return false;
	tuiDebugFile = fopen ("signalbox-tui-debug.log", "w");
	if (tuiDebugFile == NULL) return false;
	/* Line buffering preserves the last complete event without per-event fsync. */
	setvbuf (tuiDebugFile, NULL, _IOLBF, 0);
	atexit (tuiDebugClose);
	tuiDebugPrint ("debug_log path=signalbox-tui-debug.log\n");
	return true;
}

void tuiDebugClose (void) {
	if (tuiDebugFile != NULL) {
		tuiDebugPrint ("debug_log closing\n");
		fclose (tuiDebugFile);
		tuiDebugFile = NULL;
	}
}

void tuiDebugPrint (const char * const format, ...) {
	if (tuiDebugFile == NULL) return;
	va_list args;
	va_start (args, format);
	fputs ("[signalbox:tui] ", tuiDebugFile);
	vfprintf (tuiDebugFile, format, args);
	va_end (args);
}

#ifdef HAVE_DEBUGLOG
unsigned int debug = 0;
#endif
