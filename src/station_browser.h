#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <piano.h>

typedef enum {
	SB_STATION_SORT_ORIGINAL = 0,
	SB_STATION_SORT_A_Z,
	SB_STATION_SORT_FAVORITES_FIRST,
	SB_STATION_SORT_COUNT,
} SbStationSort;

typedef struct {
	/* Borrowed references only; libpiano retains ownership. */
	const PianoStation_t **visibleStations;
	size_t visibleCount;
	size_t totalCount;
	char **favoriteIds;
	size_t favoriteCount;
	char *favoritesPath;
	char filter[128];
	SbStationSort sort;
	uint64_t sourceGeneration;
} SbStationBrowser;

bool SbStationBrowserInit (SbStationBrowser *);
void SbStationBrowserDestroy (SbStationBrowser *);
bool SbStationBrowserRebuild (SbStationBrowser *, const PianoStation_t *, uint64_t);
bool SbStationBrowserSetFilter (SbStationBrowser *, const char *);
bool SbStationBrowserToggleFavorite (SbStationBrowser *, const PianoStation_t *);
bool SbStationBrowserIsFavorite (const SbStationBrowser *, const PianoStation_t *);
const PianoStation_t *SbStationBrowserAt (const SbStationBrowser *, size_t);
const char *SbStationBrowserSortName (SbStationSort);

