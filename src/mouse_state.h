#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum {
	SB_UI_SCROLL_TOWARD_TOP = -1,
	SB_UI_SCROLL_TOWARD_BOTTOM = 1,
};

static inline int SbUiMouseWheelOwnedByModal (const int modalActive,
		const int direction) {
	return modalActive && direction != 0;
}

/* Decode only the bits supplied by the active curses implementation. */
static inline int SbUiMouseWheelDirection (const uint64_t state,
		const uint64_t wheelUpMask, const uint64_t wheelDownMask,
		const uint64_t legacyWheelDownMask, const uint64_t reportPositionMask,
		const int mouseApiVersion) {
	const int up = wheelUpMask != 0 && (state & wheelUpMask) != 0;
	const int down = (wheelDownMask != 0 && (state & wheelDownMask) != 0) ||
			(mouseApiVersion == 1 && legacyWheelDownMask != 0 &&
			reportPositionMask != 0 &&
			(state & legacyWheelDownMask) != 0 &&
			(state & reportPositionMask) != 0);
	/* Do not guess if a backend ever presents contradictory direction bits. */
	return up == down ? 0 : up ? -1 : 1;
}

static inline int SbUiMouseWheelFromNativeDelta (const int delta) {
	return delta > 0 ? SB_UI_SCROLL_TOWARD_TOP :
			delta < 0 ? SB_UI_SCROLL_TOWARD_BOTTOM : 0;
}

static inline void SbUiMouseBitName (char *out, const size_t size,
		const uint64_t state, const uint64_t mask, const char *name) {
	/* A flag can contain multiple bits; require the complete mask. */
	if (mask == 0 || (state & mask) != mask) return;
	const size_t used = strlen (out);
	if (used < size) snprintf (out + used, size - used, "%s%s",
			used > 0 ? "|" : "", name);
}
