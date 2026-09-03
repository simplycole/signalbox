#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define strcasecmp _stricmp
#define unlink _unlink
#define mkdir(path, mode) _mkdir(path)
#else
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "settings.h"
#include "station_browser.h"

typedef struct {
	const PianoStation_t *station;
	bool favorite;
	size_t originalIndex;
} SbStationSortItem;

static SbStationSort compareSort;

static bool SbStationIdEqual (const char *a, const char *b) {
	return a != NULL && b != NULL && strcmp (a, b) == 0;
}

static bool SbStationBrowserHasId (const SbStationBrowser *browser,
		const char *id) {
	for (size_t i = 0; i < browser->favoriteCount; i++) {
		if (SbStationIdEqual (browser->favoriteIds[i], id)) return true;
	}
	return false;
}

bool SbStationBrowserIsFavorite (const SbStationBrowser *browser,
		const PianoStation_t *station) {
	return station != NULL && SbStationBrowserHasId (browser, station->id);
}

static int SbStationSortCompare (const void *left, const void *right) {
	const SbStationSortItem * const a = left;
	const SbStationSortItem * const b = right;
	if (compareSort == SB_STATION_SORT_FAVORITES_FIRST &&
			a->favorite != b->favorite) return a->favorite ? -1 : 1;
	if (compareSort != SB_STATION_SORT_ORIGINAL) {
		const char * const an = a->station->name != NULL ? a->station->name : "";
		const char * const bn = b->station->name != NULL ? b->station->name : "";
		const int folded = strcasecmp (an, bn);
		if (folded != 0) return folded;
		const int exact = strcmp (an, bn);
		if (exact != 0) return exact;
		const char * const ai = a->station->id != NULL ? a->station->id : "";
		const char * const bi = b->station->id != NULL ? b->station->id : "";
		const int id = strcmp (ai, bi);
		if (id != 0) return id;
	}
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
	SbStationSortItem * const items = calloc (browser->totalCount, sizeof (*items));
	if (items == NULL) return false;
	station = stations;
	size_t count = 0;
	for (size_t original = 0; station != NULL;
			original++, station = PianoListNextP (station)) {
		if (!SbStationContainsFolded (station->name, browser->filter)) continue;
		items[count++] = (SbStationSortItem) {station,
				SbStationBrowserIsFavorite (browser, station), original};
	}
	compareSort = browser->sort;
	if (count > 1) qsort (items, count, sizeof (*items), SbStationSortCompare);
	if (count > 0) {
		browser->visibleStations = malloc (count * sizeof (*browser->visibleStations));
		if (browser->visibleStations == NULL) {
			free (items);
			return false;
		}
		for (size_t i = 0; i < count; i++) browser->visibleStations[i] = items[i].station;
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

static bool SbStationBrowserSave (const SbStationBrowser *browser) {
	if (browser->favoritesPath == NULL) return false;
	char * const dir = BarSettingsSignalboxPath ("");
	if (dir == NULL) return false;
	const size_t dirLength = strlen (dir);
	if (dirLength > 0 && dir[dirLength - 1] == '/') dir[dirLength - 1] = '\0';
	if (mkdir (dir, 0700) != 0 && errno != EEXIST) {
		free (dir);
		return false;
	}
	free (dir);
	const size_t tempLength = strlen (browser->favoritesPath) + 5;
	char * const temporary = malloc (tempLength);
	if (temporary == NULL) return false;
	snprintf (temporary, tempLength, "%s.tmp", browser->favoritesPath);
	FILE * const file = fopen (temporary, "w");
	if (file == NULL) {
		free (temporary);
		return false;
	}
	bool ok = true;
	for (size_t i = 0; i < browser->favoriteCount; i++) {
		if (fprintf (file, "%s\n", browser->favoriteIds[i]) < 0) ok = false;
	}
	if (fclose (file) != 0) ok = false;
	if (ok && rename (temporary, browser->favoritesPath) != 0) ok = false;
	if (!ok) unlink (temporary);
	free (temporary);
	return ok;
}

static bool SbStationBrowserAddId (SbStationBrowser *browser, const char *id) {
	if (id == NULL || id[0] == '\0' || SbStationBrowserHasId (browser, id)) return true;
	char ** const resized = realloc (browser->favoriteIds,
			(browser->favoriteCount + 1) * sizeof (*resized));
	if (resized == NULL) return false;
	browser->favoriteIds = resized;
	browser->favoriteIds[browser->favoriteCount] = strdup (id);
	if (browser->favoriteIds[browser->favoriteCount] == NULL) return false;
	browser->favoriteCount++;
	return true;
}

bool SbStationBrowserInit (SbStationBrowser *browser) {
	assert (browser != NULL);
	memset (browser, 0, sizeof (*browser));
	browser->sort = SB_STATION_SORT_A_Z;
	browser->favoritesPath = BarSettingsSignalboxPath ("favorites");
	if (browser->favoritesPath == NULL) return false;
	FILE * const file = fopen (browser->favoritesPath, "r");
	if (file == NULL) return true;
	char id[256];
	while (fgets (id, sizeof (id), file) != NULL) {
		id[strcspn (id, "\r\n")] = '\0';
		if (id[0] != '\0' && !SbStationBrowserAddId (browser, id)) {
			fclose (file);
			return false;
		}
	}
	fclose (file);
	return true;
}

void SbStationBrowserDestroy (SbStationBrowser *browser) {
	if (browser == NULL) return;
	free (browser->visibleStations);
	for (size_t i = 0; i < browser->favoriteCount; i++) free (browser->favoriteIds[i]);
	free (browser->favoriteIds);
	free (browser->favoritesPath);
	memset (browser, 0, sizeof (*browser));
}

bool SbStationBrowserToggleFavorite (SbStationBrowser *browser,
		const PianoStation_t *station) {
	assert (browser != NULL && station != NULL);
	for (size_t i = 0; i < browser->favoriteCount; i++) {
		if (SbStationIdEqual (browser->favoriteIds[i], station->id)) {
			char * const removed = browser->favoriteIds[i];
			memmove (&browser->favoriteIds[i], &browser->favoriteIds[i + 1],
					(browser->favoriteCount - i - 1) * sizeof (*browser->favoriteIds));
			browser->favoriteCount--;
			if (!SbStationBrowserSave (browser)) {
				memmove (&browser->favoriteIds[i + 1], &browser->favoriteIds[i],
						(browser->favoriteCount - i) * sizeof (*browser->favoriteIds));
				browser->favoriteIds[i] = removed;
				browser->favoriteCount++;
				return false;
			}
			free (removed);
			return true;
		}
	}
	if (!SbStationBrowserAddId (browser, station->id)) return false;
	if (!SbStationBrowserSave (browser)) {
		free (browser->favoriteIds[--browser->favoriteCount]);
		return false;
	}
	return true;
}

const PianoStation_t *SbStationBrowserAt (const SbStationBrowser *browser,
		const size_t index) {
	return index < browser->visibleCount ? browser->visibleStations[index] : NULL;
}

const char *SbStationBrowserSortName (const SbStationSort sort) {
	switch (sort) {
		case SB_STATION_SORT_ORIGINAL: return "ORIGINAL";
		case SB_STATION_SORT_FAVORITES_FIRST: return "FAV+A-Z";
		default: return "A-Z";
	}
}
