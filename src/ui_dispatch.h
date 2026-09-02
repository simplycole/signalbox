/*
Copyright (c) 2010-2013
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

#pragma once

/* bit-mask */
typedef enum {
	BAR_DC_UNDEFINED = 0,
	BAR_DC_GLOBAL = 1, /* top-level action */
	BAR_DC_STATION = 2, /* station selected */
	BAR_DC_SONG = 4, /* song selected */
} BarUiDispatchContext_t;

#include "settings.h"
#include "ui_types.h"
#include "main.h"

typedef void (*BarKeyShortcutFunc_t) (BarApp_t *, PianoStation_t *,
		PianoSong_t *, BarUiDispatchContext_t);

typedef struct {
	char defaultKey;
	SbUiCommand command;
	BarUiDispatchContext_t context;
	const char * const helpText;
	const char * const configKey;
} BarUiDispatchAction_t;

/* see settings.h */
extern const BarUiDispatchAction_t dispatchActions[BAR_KS_COUNT];

#include <piano.h>
#include <stdbool.h>
#include <stdio.h>

SbUiCommand BarUiCommandFromKey (const BarSettings_t *, char);
bool BarUiDispatchCommand (BarApp_t *, SbUiCommand, PianoStation_t *, PianoSong_t *,
		bool, BarUiDispatchContext_t);
