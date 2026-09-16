#include "settings_values.h"

#include <string.h>

bool SbSettingsParseVisualizer (const char *value, bool *enabled) {
	if (value == NULL || enabled == NULL) return false;
	if (strcmp (value, "spectrum") == 0) *enabled = true;
	else if (strcmp (value, "off") == 0) *enabled = false;
	else return false;
	return true;
}

bool SbSettingsParseAlbumArt (const char *value, SbAlbumArtMode *mode) {
	if (value == NULL || mode == NULL) return false;
	if (strcmp (value, "auto") == 0) *mode = SB_ALBUM_ART_AUTO;
	else if (strcmp (value, "pixel") == 0) *mode = SB_ALBUM_ART_PIXEL;
	else if (strcmp (value, "off") == 0) *mode = SB_ALBUM_ART_OFF;
	else return false;
	return true;
}

bool SbSettingsParseLyricsDisplay (const char *value,
		SbLyricsDisplay *display) {
	if (value == NULL || display == NULL) return false;
	if (strcmp (value, "off") == 0) *display = SB_LYRICS_DISPLAY_OFF;
	else if (strcmp (value, "line") == 0) *display = SB_LYRICS_DISPLAY_LINE;
	else if (strcmp (value, "three-line") == 0)
		*display = SB_LYRICS_DISPLAY_THREE_LINE;
	else return false;
	return true;
}
