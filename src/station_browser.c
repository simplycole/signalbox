#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "station_browser.h"

typedef struct {
	const PianoStation_t *station;
	size_t originalIndex;
} SbStationSortItem;

static int SbStationFoldedCompare (const char *left, const char *right) {
	if (left == NULL) left = "";
	if (right == NULL) right = "";
	while (*left != '\0' && *right != '\0') {
		const int a = tolower ((unsigned char) *left);
		const int b = tolower ((unsigned char) *right);
		if (a != b) return a - b;
		left++;
		right++;
	}
	return tolower ((unsigned char) *left) -
			tolower ((unsigned char) *right);
}

static int SbStationSortCompare (const void *left, const void *right) {
	const SbStationSortItem * const a = left;
	const SbStationSortItem * const b = right;
	const int folded = SbStationFoldedCompare (a->station->name,
			b->station->name);
	if (folded != 0) return folded;
	const char * const an = a->station->name != NULL ? a->station->name : "";
	const char * const bn = b->station->name != NULL ? b->station->name : "";
	const int exact = strcmp (an, bn);
	if (exact != 0) return exact;
	const char * const ai = a->station->id != NULL ? a->station->id : "";
	const char * const bi = b->station->id != NULL ? b->station->id : "";
	const int id = strcmp (ai, bi);
	if (id != 0) return id;
	return a->originalIndex < b->originalIndex ? -1 :
			(a->originalIndex > b->originalIndex ? 1 : 0);
}

static bool SbStationContainsFolded (const char *text, const char *needle) {
	if (needle[0] == '\0') return true;
	if (text == NULL) return false;
	const size_t length = strlen (needle);
	for (; *text != '\0'; text++) {
		size_t i = 0;
		while (i < length && text[i] != '\0' &&
				tolower ((unsigned char) text[i]) ==
				tolower ((unsigned char) needle[i])) i++;
		if (i == length) return true;
	}
	return false;
}

bool SbStationBrowserRebuild (SbStationBrowser *browser,
		const PianoStation_t *stations, const uint64_t generation) {
	assert (browser != NULL);
	free (browser->visibleStations);
	browser->visibleStations = NULL;
	browser->visibleCount = browser->totalCount = 0;
	const PianoStation_t *station = stations;
	PianoListForeachP (station) browser->totalCount++;
	if (browser->totalCount == 0) {
		browser->sourceGeneration = generation;
		return true;
	}
	SbStationSortItem * const items = calloc (browser->totalCount,
			sizeof (*items));
	if (items == NULL) return false;
	station = stations;
	size_t count = 0;
	for (size_t original = 0; station != NULL;
			original++, station = PianoListNextP (station)) {
		if (!SbStationContainsFolded (station->name, browser->filter)) continue;
		items[count++] = (SbStationSortItem) {station, original};
	}
	if (browser->sort == SB_STATION_SORT_A_Z && count > 1)
		qsort (items, count, sizeof (*items), SbStationSortCompare);
	if (count > 0) {
		browser->visibleStations = malloc (count *
				sizeof (*browser->visibleStations));
		if (browser->visibleStations == NULL) {
			free (items);
			return false;
		}
		for (size_t i = 0; i < count; i++)
			browser->visibleStations[i] = items[i].station;
	}
	free (items);
	browser->visibleCount = count;
	browser->sourceGeneration = generation;
	return true;
}

bool SbStationBrowserSetFilter (SbStationBrowser *browser, const char *filter) {
	assert (browser != NULL && filter != NULL);
	if (strlen (filter) >= sizeof (browser->filter)) return false;
	strcpy (browser->filter, filter);
	return true;
}

SbStationSort SbStationBrowserCycleSort (SbStationBrowser *browser) {
	assert (browser != NULL);
	browser->sort = (SbStationSort) ((browser->sort + 1) %
			SB_STATION_SORT_COUNT);
	return browser->sort;
}

const char *SbStationBrowserSortName (const SbStationSort sort) {
	return sort == SB_STATION_SORT_ORIGINAL ? "ORIGINAL" : "A-Z";
}

bool SbStationBrowserInit (SbStationBrowser *browser) {
	assert (browser != NULL);
	memset (browser, 0, sizeof (*browser));
	browser->sort = SB_STATION_SORT_A_Z;
	return true;
}

void SbStationBrowserDestroy (SbStationBrowser *browser) {
	if (browser == NULL) return;
	free (browser->visibleStations);
	memset (browser, 0, sizeof (*browser));
}

const PianoStation_t *SbStationBrowserAt (const SbStationBrowser *browser,
		const size_t index) {
	return index < browser->visibleCount ? browser->visibleStations[index] : NULL;
}

size_t SbStationBrowserFind (const SbStationBrowser *browser,
		const PianoStation_t *station) {
	for (size_t i = 0; i < browser->visibleCount; i++) {
		if (browser->visibleStations[i] == station) return i;
	}
	return SIZE_MAX;
}

size_t SbStationBrowserMove (const SbStationBrowser *browser,
		const size_t selected, const long distance) {
	if (browser->visibleCount == 0) return 0;
	const size_t current = selected < browser->visibleCount ? selected :
			browser->visibleCount - 1;
	if (distance < 0) {
		const size_t amount = (size_t) (-(distance + 1)) + 1;
		return amount > current ? 0 : current - amount;
	}
	const size_t amount = (size_t) distance;
	return amount >= browser->visibleCount - current ?
			browser->visibleCount - 1 : current + amount;
}

size_t SbStationBrowserScroll (const SbStationBrowser *browser,
		const size_t selected, const size_t offset, const size_t rows) {
	if (browser->visibleCount == 0 || rows == 0) return selected;
	size_t result = offset;
	if (selected < result) result = selected;
	else if (selected >= result + rows) result = selected - rows + 1;
	const size_t maximum = browser->visibleCount > rows ?
			browser->visibleCount - rows : 0;
	return result > maximum ? maximum : result;
}

bool SbStationBrowserIsCurrent (const PianoStation_t *station,
		const PianoStation_t *current) {
	return station != NULL && station == current;
}

const char *SbStationBrowserEmptyText (const SbStationBrowser *browser) {
	return browser->totalCount > 0 && browser->filter[0] != '\0' ?
			"No matching stations" : "No stations available";
}
