#pragma once

#include <stdbool.h>

typedef enum {
	SB_ALBUM_ART_AUTO = 0,
	SB_ALBUM_ART_PIXEL,
	SB_ALBUM_ART_OFF,
} SbAlbumArtMode;

typedef enum {
	SB_LYRICS_DISPLAY_OFF = 0,
	SB_LYRICS_DISPLAY_LINE,
	SB_LYRICS_DISPLAY_THREE_LINE,
} SbLyricsDisplay;

bool SbSettingsParseVisualizer (const char *, bool *);
bool SbSettingsParseAlbumArt (const char *, SbAlbumArtMode *);
bool SbSettingsParseLyricsDisplay (const char *, SbLyricsDisplay *);
