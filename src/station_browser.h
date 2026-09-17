#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <piano.h>

typedef enum {
	SB_STATION_SORT_A_Z = 0,
	SB_STATION_SORT_ORIGINAL,
	SB_STATION_SORT_COUNT,
} SbStationSort;

typedef struct {
	/* Borrowed references only; libpiano retains ownership. */
	const PianoStation_t **visibleStations;
	size_t visibleCount;
	size_t totalCount;
	char filter[128];
	SbStationSort sort;
	uint64_t sourceGeneration;
} SbStationBrowser;

bool SbStationBrowserInit (SbStationBrowser *);
void SbStationBrowserDestroy (SbStationBrowser *);
bool SbStationBrowserRebuild (SbStationBrowser *, const PianoStation_t *, uint64_t);
bool SbStationBrowserSetFilter (SbStationBrowser *, const char *);
SbStationSort SbStationBrowserCycleSort (SbStationBrowser *);
const char *SbStationBrowserSortName (SbStationSort);
const PianoStation_t *SbStationBrowserAt (const SbStationBrowser *, size_t);
size_t SbStationBrowserFind (const SbStationBrowser *, const PianoStation_t *);
size_t SbStationBrowserMove (const SbStationBrowser *, size_t, long);
size_t SbStationBrowserScroll (const SbStationBrowser *, size_t, size_t, size_t);
bool SbStationBrowserIsCurrent (const PianoStation_t *, const PianoStation_t *);
const char *SbStationBrowserEmptyText (const SbStationBrowser *);
