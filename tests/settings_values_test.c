#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "settings_values.h"

int main (void) {
	bool visualizer = false;
	assert (SbSettingsParseVisualizer ("spectrum", &visualizer) && visualizer);
	assert (SbSettingsParseVisualizer ("off", &visualizer) && !visualizer);
	visualizer = true;
	assert (!SbSettingsParseVisualizer ("bars", &visualizer) && visualizer);

	SbAlbumArtMode art = SB_ALBUM_ART_OFF;
	assert (SbSettingsParseAlbumArt ("auto", &art) && art == SB_ALBUM_ART_AUTO);
	assert (SbSettingsParseAlbumArt ("pixel", &art) && art == SB_ALBUM_ART_PIXEL);
	assert (SbSettingsParseAlbumArt ("off", &art) && art == SB_ALBUM_ART_OFF);
	assert (!SbSettingsParseAlbumArt ("kitty", &art) && art == SB_ALBUM_ART_OFF);

	SbLyricsDisplay lyrics = SB_LYRICS_DISPLAY_OFF;
	assert (SbSettingsParseLyricsDisplay ("line", &lyrics) &&
			lyrics == SB_LYRICS_DISPLAY_LINE);
	assert (SbSettingsParseLyricsDisplay ("three-line", &lyrics) &&
			lyrics == SB_LYRICS_DISPLAY_THREE_LINE);
	assert (SbSettingsParseLyricsDisplay ("off", &lyrics) &&
			lyrics == SB_LYRICS_DISPLAY_OFF);
	assert (!SbSettingsParseLyricsDisplay ("karaoke", &lyrics) &&
			lyrics == SB_LYRICS_DISPLAY_OFF);

	puts ("settings value tests passed");
	return 0;
}
