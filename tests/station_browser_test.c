#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "station_browser.h"

static void Link (PianoStation_t *stations, const size_t count) {
	for (size_t i = 0; i < count; i++)
		stations[i].head.next = i + 1 < count ? &stations[i + 1].head : NULL;
}

int main (void) {
	SbStationBrowser browser;
	assert (SbStationBrowserInit (&browser));
	assert (SbStationBrowserRebuild (&browser, NULL, 1));
	assert (browser.totalCount == 0 && browser.visibleCount == 0);
	assert (strcmp (SbStationBrowserEmptyText (&browser),
			"No stations available") == 0);
	assert (SbStationBrowserMove (&browser, 8, 1) == 0);
	PianoStation_t only = {.name = "Only", .id = "only"};
	assert (SbStationBrowserRebuild (&browser, &only, 2));
	assert (browser.visibleCount == 1);
	assert (SbStationBrowserMove (&browser, 0, -1) == 0);
	assert (SbStationBrowserMove (&browser, 0, 1) == 0);

	char longName[300];
	memset (longName, 'x', sizeof (longName) - 1); longName[299] = '\0';
	PianoStation_t stations[] = {
		{.name = "Rock One", .id = "id-1"},
		{.name = "Duplicate", .id = "id-2"},
		{.name = "duplicate", .id = "id-3"},
		{.name = "Jazz", .id = "id-4"},
		{.name = longName, .id = "id-5"},
	};
	Link (stations, 5);
	assert (SbStationBrowserRebuild (&browser, stations, 2));
	assert (browser.totalCount == 5 && browser.visibleCount == 5);
	for (size_t i = 0; i < 5; i++) assert (SbStationBrowserAt (&browser, i) == &stations[i]);
	assert (SbStationBrowserIsCurrent (&stations[3], &stations[3]));
	assert (!SbStationBrowserIsCurrent (&stations[2], &stations[3]));
	assert (SbStationBrowserMove (&browser, 0, -1) == 0);
	assert (SbStationBrowserMove (&browser, 0, 2) == 2);
	assert (SbStationBrowserMove (&browser, 2, 99) == 4);
	assert (SbStationBrowserMove (&browser, 4, -99) == 0);
	assert (SbStationBrowserScroll (&browser, 0, 0, 2) == 0);
	assert (SbStationBrowserScroll (&browser, 3, 0, 2) == 2);
	assert (SbStationBrowserScroll (&browser, 4, 2, 2) == 3);

	const PianoStation_t *selected = &stations[2];
	assert (SbStationBrowserSetFilter (&browser, "DUP"));
	assert (SbStationBrowserRebuild (&browser, stations, 2));
	assert (browser.visibleCount == 2);
	assert (SbStationBrowserAt (&browser, 0) == &stations[1]);
	assert (SbStationBrowserAt (&browser, 1) == &stations[2]);
	assert (SbStationBrowserFind (&browser, selected) == 1);
	assert (strcmp (SbStationBrowserAt (&browser, 1)->id, "id-3") == 0);
	assert (SbStationBrowserSetFilter (&browser, "DU"));
	assert (SbStationBrowserRebuild (&browser, stations, 2));
	assert (SbStationBrowserFind (&browser, selected) == 1);

	assert (SbStationBrowserSetFilter (&browser, "zz-no-match"));
	assert (SbStationBrowserRebuild (&browser, stations, 3));
	assert (browser.totalCount == 5 && browser.visibleCount == 0);
	assert (strcmp (SbStationBrowserEmptyText (&browser),
			"No matching stations") == 0);
	assert (SbStationBrowserFind (&browser, selected) == SIZE_MAX);
	assert (SbStationBrowserSetFilter (&browser, ""));
	assert (SbStationBrowserRebuild (&browser, stations, 4));
	assert (SbStationBrowserAt (&browser, 4)->name == longName);

	SbStationBrowserDestroy (&browser);
	puts ("station browser tests passed");
	return 0;
}
