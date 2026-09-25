#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum {
	SB_UI_MOUSE_WHEEL_NONE = 0,
	SB_UI_MOUSE_WHEEL_UP = -1,
	SB_UI_MOUSE_WHEEL_DOWN = 1,
	SB_UI_SCROLL_TOWARD_TOP = -1,
	SB_UI_SCROLL_TOWARD_BOTTOM = 1,
};

typedef struct {
	uint64_t wheelUpPressed;
	uint64_t wheelUpClicked;
	uint64_t wheelDownPressed;
	uint64_t wheelDownClicked;
	uint64_t reportPosition;
	int appleTerminalNcursesV1;
} SbUiMouseWheelMasks;

static inline int SbUiMouseWheelOwnedByModal (const int modalActive,
		const int direction) {
	return modalActive && direction != 0;
}

/* Decode only unambiguous wheel states supplied by the active curses
 * implementation.  Apple's ncurses v1 cannot represent button 5 and maps an
 * X10 wheel-down report to REPORT_MOUSE_POSITION alone. */
static inline int SbUiMouseWheelDirection (const uint64_t state,
		const SbUiMouseWheelMasks masks) {
	const int up = (masks.wheelUpPressed != 0 &&
			(state & masks.wheelUpPressed) == masks.wheelUpPressed) ||
			(masks.wheelUpClicked != 0 &&
			(state & masks.wheelUpClicked) == masks.wheelUpClicked);
	const int down = (masks.wheelDownPressed != 0 &&
			(state & masks.wheelDownPressed) == masks.wheelDownPressed) ||
			(masks.wheelDownClicked != 0 &&
			(state & masks.wheelDownClicked) == masks.wheelDownClicked) ||
			(masks.appleTerminalNcursesV1 && masks.reportPosition != 0 &&
			state == masks.reportPosition);
	/* Do not guess if a backend ever presents contradictory direction bits. */
	return up == down ? SB_UI_MOUSE_WHEEL_NONE :
			up ? SB_UI_MOUSE_WHEEL_UP : SB_UI_MOUSE_WHEEL_DOWN;
}

static inline int SbUiMouseWheelFromNativeDelta (const int delta) {
	return delta > 0 ? SB_UI_SCROLL_TOWARD_TOP :
			delta < 0 ? SB_UI_SCROLL_TOWARD_BOTTOM : 0;
}

static inline int SbUiMouseWheelNavigationKey (const int key,
		const int direction, const int upKey, const int downKey) {
	return direction == SB_UI_MOUSE_WHEEL_UP ? upKey :
			direction == SB_UI_MOUSE_WHEEL_DOWN ? downKey : key;
}

static inline size_t SbUiMouseListMove (const size_t selected,
		const size_t count, const int direction) {
	if (count == 0) return 0;
	const size_t current = selected < count ? selected : count - 1;
	if (direction < 0) return current > 0 ? current - 1 : 0;
	if (direction > 0) return current + 1 < count ? current + 1 : count - 1;
	return current;
}

static inline void SbUiMouseBitName (char *out, const size_t size,
		const uint64_t state, const uint64_t mask, const char *name) {
	/* A flag can contain multiple bits; require the complete mask. */
	if (mask == 0 || (state & mask) != mask) return;
	const size_t used = strlen (out);
	if (used < size) snprintf (out + used, size - used, "%s%s",
			used > 0 ? "|" : "", name);
}
