#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SB_ART_DECODE_MAX_DIMENSION = 8192, SB_ART_DECODE_MAX_PIXELS = 16000000 };

typedef enum { SB_ART_COLOR_NONE = 0, SB_ART_COLOR_256, SB_ART_COLOR_TRUECOLOR } SbArtColorMode;
typedef struct { unsigned int width, height; uint8_t *rgba; } SbArtImage;
typedef struct { uint8_t upper[3], lower[3]; unsigned char upper256, lower256; } SbArtCell;
typedef struct {
	char path[1024]; unsigned int columns, rows;
	unsigned int requestedColumns, requestedRows; SbArtColorMode mode;
	SbArtCell *cells; unsigned int builds, hits, sourceWidth, sourceHeight;
	bool failed;
} SbPreparedArt;
typedef struct { bool visible; unsigned int columns, rows; } SbArtLayout;

void SbArtImageDestroy (SbArtImage *);
bool SbArtDecodeFile (const char *, SbArtImage *);
bool SbArtResizeFit (const SbArtImage *, unsigned int, unsigned int, SbArtImage *);
unsigned char SbArtXterm256 (uint8_t, uint8_t, uint8_t);
bool SbArtCellsBuild (const SbArtImage *, SbArtColorMode, SbArtCell **,
		unsigned int *, unsigned int *);
void SbPreparedArtDestroy (SbPreparedArt *);
bool SbPreparedArtGet (SbPreparedArt *, const char *, unsigned int,
		unsigned int, SbArtColorMode);
SbArtLayout SbArtChooseLayout (unsigned int, unsigned int, bool);
SbArtColorMode SbArtDetectColorMode (const char *, const char *, const char *,
		int);
size_t SbArtCellAnsi (const SbArtCell *, SbArtColorMode, char *, size_t);
