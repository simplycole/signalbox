#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "station_browser.h"

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
	browser->visibleStations = malloc (browser->totalCount *
			sizeof (*browser->visibleStations));
	if (browser->visibleStations == NULL) return false;
	station = stations;
	size_t count = 0;
	for (; station != NULL; station = PianoListNextP (station)) {
		if (!SbStationContainsFolded (station->name, browser->filter)) continue;
		browser->visibleStations[count++] = station;
	}
	if (count == 0) {
		free (browser->visibleStations);
		browser->visibleStations = NULL;
	} else {
		const PianoStation_t **resized = realloc (browser->visibleStations,
				count * sizeof (*browser->visibleStations));
		if (resized != NULL) browser->visibleStations = resized;
	}
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

bool SbStationBrowserInit (SbStationBrowser *browser) {
	assert (browser != NULL);
	memset (browser, 0, sizeof (*browser));
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
