#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tui_presentation.h"
#include "enrichment.h"

int main (void) {
	assert (SbTuiPresentationFieldRole ("Artist") == SB_TUI_TEXT_ARTIST);
	assert (SbTuiPresentationFieldRole ("Canonical Track") == SB_TUI_TEXT_TRACK);
	assert (SbTuiPresentationFieldRole ("Release") == SB_TUI_TEXT_ALBUM);
	assert (SbTuiPresentationFieldRole ("State") == SB_TUI_TEXT_STATE);
	assert (SbTuiPresentationFieldRole ("Provider") == SB_TUI_TEXT_PROVIDER);
	size_t labelLength = 0; const char *value = NULL; SbTuiTextRole role;
	assert (SbTuiPresentationSplitField ("Artist: Deftones", &labelLength,
			&value, &role));
	assert (labelLength == strlen ("Artist") && strcmp (value, "Deftones") == 0);
	assert (role == SB_TUI_TEXT_ARTIST);
	assert (!SbTuiPresentationSplitField ("ALBUM ART", NULL, NULL, NULL));
	assert (!SbTuiPresentationSplitField ("Provider: ", NULL, NULL, NULL));
	assert (SbTuiPresentationIsSection ("LYRICS"));
	assert (!SbTuiPresentationIsSection ("a wrapped field value"));
	char text[256];
	assert (SbTuiPresentationLyricsHeader (text, sizeof (text), "Deftones",
			"Entombed", "Koi No Yokan"));
	assert (strcmp (text, "Deftones — Entombed\nKoi No Yokan\n\n") == 0);
	assert (SbTuiPresentationLyricsHeader (text, sizeof (text), "Artist", "Title", ""));
	assert (strcmp (text, "Artist — Title\n\n") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 24));
	assert (strcmp (text, "Ready    Art: Loading") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 18));
	assert (strcmp (text, "Ready    Art: Load") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 13));
	assert (strcmp (text, "Ready    Art…") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_LOADING, 9));
	assert (strcmp (text, "Ready") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_AVAILABLE, 24));
	assert (strcmp (text, "Ready    Art: Ready") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Ready", SB_LOOKUP_NO_MATCH, 17));
	assert (strcmp (text, "Ready") == 0);
	assert (SbTuiPresentationStatus (text, sizeof (text), "Playing", SB_LOOKUP_ERROR, 32));
	assert (strcmp (text, "Playing    Art: Unavailable") == 0);
	SbTuiArtStatus art = {0};
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_LOADING, 100) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_LOADING, 5000) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_AVAILABLE, 5000) == SB_LOOKUP_AVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_AVAILABLE, 6999) == SB_LOOKUP_AVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 1, SB_LOOKUP_AVAILABLE, 7000) == SB_LOOKUP_IDLE);
	assert (SbTuiArtStatusUpdate (&art, 2, SB_LOOKUP_NO_MATCH, 8000) == SB_LOOKUP_NO_MATCH);
	assert (SbTuiArtStatusUpdate (&art, 2, SB_LOOKUP_NO_MATCH, 10000) == SB_LOOKUP_IDLE);
	assert (SbTuiArtStatusUpdate (&art, 3, SB_LOOKUP_UNAVAILABLE, 11000) == SB_LOOKUP_UNAVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 3, SB_LOOKUP_UNAVAILABLE, 13000) == SB_LOOKUP_IDLE);
	/* A new generation replaces the old completion deadline. */
	assert (SbTuiArtStatusUpdate (&art, 4, SB_LOOKUP_AVAILABLE, 14000) == SB_LOOKUP_AVAILABLE);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_LOADING, 14500) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_LOADING, 17000) == SB_LOOKUP_LOADING);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_ERROR, 17000) == SB_LOOKUP_ERROR);
	assert (SbTuiArtStatusUpdate (&art, 5, SB_LOOKUP_ERROR, 19000) == SB_LOOKUP_IDLE);
	puts ("tui presentation tests passed");
	return 0;
}
