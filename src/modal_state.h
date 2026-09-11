#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
	size_t offset;
	size_t maximum;
	uint64_t identityGeneration;
} SbUiModalScrollState;

static inline void SbUiModalScrollOpen (SbUiModalScrollState *state,
		const uint64_t identityGeneration) {
	state->offset = 0;
	state->maximum = 0;
	state->identityGeneration = identityGeneration;
}

static inline void SbUiModalScrollObserveIdentity (SbUiModalScrollState *state,
		const uint64_t identityGeneration) {
	if (state->identityGeneration != identityGeneration) {
		SbUiModalScrollOpen (state, identityGeneration);
	}
}

static inline void SbUiModalScrollClamp (SbUiModalScrollState *state,
		const size_t contentLines, const size_t visibleLines) {
	state->maximum = contentLines > visibleLines ?
			contentLines - visibleLines : 0;
	if (state->offset > state->maximum) state->offset = state->maximum;
}

static inline void SbUiModalScrollLines (SbUiModalScrollState *state,
		const int direction) {
	if (direction < 0) {
		if (state->offset > 0) state->offset--;
	} else if (direction > 0 && state->offset < state->maximum) {
		state->offset++;
	}
}

static inline void SbUiModalScrollWheel (SbUiModalScrollState *state,
		const int direction) {
	/* A wheel notch moves three text lines through the same bounded primitive
	 * used by Up/Down and j/k. */
	for (int line = 0; line < 3; line++)
		SbUiModalScrollLines (state, direction);
}

static inline void SbUiModalScrollPage (SbUiModalScrollState *state,
		const int direction, const size_t page) {
	if (direction < 0) {
		state->offset = state->offset > page ? state->offset - page : 0;
	} else if (direction > 0) {
		const size_t remaining = state->maximum - state->offset;
		state->offset += page < remaining ? page : remaining;
	}
}
