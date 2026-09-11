#pragma once

#include <stddef.h>

/* availableRows begins at the UPCOMING heading and ends above STATUS.  Keep
 * four structural rows (section spacing/separator, RECENT heading, and bottom
 * spacing) plus four useful RECENT content rows; UPCOMING receives what
 * remains, capped by track count. */
static inline int SbUiUpcomingHeight (const int *rowCosts, const size_t count,
		const int availableRows, const size_t trackCap) {
	const int budget = availableRows > 8 ? availableRows - 8 : 0;
	int used = 0;
	for (size_t i = 0; i < count && i < trackCap; i++) {
		if (used + rowCosts[i] > budget) break;
		used += rowCosts[i];
	}
	return used;
}
