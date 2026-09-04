/* Project-owned logical keyboard input boundary. */
#pragma once

#ifdef _WIN32
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
	SB_TERMINAL_INPUT_NONE = 0,
	SB_TERMINAL_INPUT_WIN32_EVENT,
} SbTerminalInputSource;

typedef struct {
	int status;
	int key;
	SbTerminalInputSource source;
	uint16_t virtualKey;
	uint32_t unicode;
	uint32_t controlState;
	uint16_t repeatCount;
} SbTerminalInputEvent;

/* Reasserts the native input mode after the drawing backend initializes. */
bool SbTerminalInputInit (void);

/* timeoutMs is -1 for blocking, zero for polling, or a bounded wait. */
SbTerminalInputEvent SbTerminalReadInput (int timeoutMs);

/* Removes only consecutive resize records at the head of the native queue.
 * Keyboard and unrelated records remain untouched and in order. */
unsigned int SbTerminalDrainResizeEvents (void);

/* Registers the diagnostic stream and writes handle/mode capabilities without
 * consuming an input record. Pass NULL to unregister the stream. */
void SbTerminalInputDiagnostic (FILE *output);
#endif
