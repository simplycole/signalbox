#include "art_renderer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public-domain 1x1 opaque red PNG, generated specifically for this test. */
static const unsigned char png[] = {
	137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,
	31,21,196,137,0,0,0,13,73,68,65,84,8,215,99,248,207,192,240,31,0,5,0,1,255,
	137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130
};

static SbArtImage TestImage (const unsigned int width,
		const unsigned int height) {
	SbArtImage image = {width, height, calloc ((size_t) width * height * 4, 1)};
	assert (image.rgba != NULL);
	for (size_t i = 0; i < (size_t) width * height; i++)
		image.rgba[i * 4 + 3] = 255;
	return image;
}

static void SetGray (SbArtImage *image, const unsigned int x,
		const unsigned int y, const uint8_t value) {
	uint8_t *pixel = image->rgba + ((size_t) y * image->width + x) * 4;
	pixel[0] = pixel[1] = pixel[2] = value;
}

static void TestEnhancementStyles (void) {
	/* High-contrast monochrome and minimal geometric covers keep clean, bounded
	 * regions without turning the intentionally chunky grid into dither. */
	SbArtImage geometric = TestImage (8, 8);
	for (unsigned int y = 0; y < geometric.height; y++)
		for (unsigned int x = 0; x < geometric.width; x++)
			SetGray (&geometric, x, y, x < 4 ? 18 : 232);
	assert (SbArtEnhance (&geometric));
	assert (geometric.rgba[(2 * 8 + 1) * 4] <= 24);
	assert (geometric.rgba[(2 * 8 + 6) * 4] >= 228);
	assert (geometric.rgba[(2 * 8 + 1) * 4] ==
			geometric.rgba[(5 * 8 + 1) * 4]);

	/* Dark, low-contrast artwork gains separation, with only a subtle overall
	 * lift. */
	SbArtImage dark = TestImage (8, 8);
	for (unsigned int y = 0; y < dark.height; y++)
		for (unsigned int x = 0; x < dark.width; x++)
			SetGray (&dark, x, y, x < 4 ? 24 : 48);
	const int before = 48 - 24;
	assert (SbArtEnhance (&dark));
	const int after = dark.rgba[(3 * 8 + 6) * 4] -
			dark.rgba[(3 * 8 + 1) * 4];
	assert (after > before);
	assert (dark.rgba[(3 * 8 + 1) * 4] < 40);

	/* Colorful graphics and portrait-like tones preserve channel ordering;
	 * enhancement does not add a saturation or quantization pass. */
	SbArtImage color = TestImage (6, 6);
	for (size_t i = 0; i < 36; i++) {
		color.rgba[i * 4] = i < 18 ? 180 : 72;
		color.rgba[i * 4 + 1] = i < 18 ? 55 : 132;
		color.rgba[i * 4 + 2] = i < 18 ? 38 : 96;
	}
	assert (SbArtEnhance (&color));
	assert (color.rgba[0] > color.rgba[1] && color.rgba[1] > color.rgba[2]);
	assert (color.rgba[35 * 4 + 1] > color.rgba[35 * 4 + 2]);

	/* Large logo/text blocks become more distinct; tiny text readability is not
	 * asserted.  Repeated preparation is deterministic for a fixed pixel grid. */
	SbArtImage blocks = TestImage (10, 6);
	for (unsigned int y = 0; y < blocks.height; y++)
		for (unsigned int x = 0; x < blocks.width; x++)
			SetGray (&blocks, x, y, (x / 2 + y / 2) % 2 ? 92 : 152);
	SbArtImage duplicate = TestImage (10, 6);
	memcpy (duplicate.rgba, blocks.rgba, 10 * 6 * 4);
	assert (SbArtEnhance (&blocks));
	assert (SbArtEnhance (&duplicate));
	assert (memcmp (blocks.rgba, duplicate.rgba, 10 * 6 * 4) == 0);
	for (size_t i = 0; i < 60; i++) assert (blocks.rgba[i * 4 + 3] == 255);

	SbArtImageDestroy (&geometric);
	SbArtImageDestroy (&dark);
	SbArtImageDestroy (&color);
	SbArtImageDestroy (&blocks);
	SbArtImageDestroy (&duplicate);
}

int main (void) {
	TestEnhancementStyles ();
	const char *path = "/tmp/signalbox-art-renderer-test.png";
	FILE *file = fopen (path, "wb"); assert (file != NULL);
	assert (fwrite (png, 1, sizeof (png), file) == sizeof (png)); fclose (file);
	SbArtImage decoded={0}; assert (SbArtDecodeFile (path, &decoded));
	assert (decoded.width == 1 && decoded.height == 1 && decoded.rgba[0] > 240);
	const char *badPath="/tmp/signalbox-art-renderer-corrupt.png";
	file=fopen(badPath,"wb"); assert(file); assert(fwrite(png,1,20,file)==20); fclose(file);
	SbArtImage corrupt={0}; assert(!SbArtDecodeFile(badPath,&corrupt)); remove(badPath);
	assert(!SbArtDecodeFile("/tmp/signalbox-art-renderer.unsupported",&corrupt));
	SbArtImage resized={0}; assert (SbArtResizeFit (&decoded, 6, 4, &resized));
	assert (resized.width == 4 && resized.height == 4);
	SbArtImage impossible={0}; assert (!SbArtResizeFit (&decoded,
		SB_ART_DECODE_MAX_DIMENSION + 1, 2, &impossible));
	SbArtImage three={2,3,NULL}; three.rgba=calloc(24,1); assert(three.rgba);
	three.rgba[0]=255; three.rgba[4*2]=10; three.rgba[4*4]=77;
	SbArtCell *cells=NULL; unsigned int columns=0,rows=0;
	assert(SbArtCellsBuild(&three,SB_ART_COLOR_TRUECOLOR,&cells,&columns,&rows));
	assert(columns==2 && rows==2 && cells[0].upper[0]==255 && cells[0].lower[0]==10);
	assert(cells[2].upper[0]==77 && cells[2].lower[0]==77); free(cells);
	assert(SbArtXterm256(0,0,0)==16); assert(SbArtXterm256(255,255,255)==231);
	assert(SbArtXterm256(128,128,128)>=232 && SbArtXterm256(128,128,128)<=255);
	SbArtCell cell={{1,2,3},{4,5,6},16,231}; char ansi[96];
	assert(SbArtCellAnsi(&cell,SB_ART_COLOR_TRUECOLOR,ansi,sizeof(ansi))>0);
	assert(strstr(ansi,"38;2;1;2;3;48;2;4;5;6m")!=NULL && strstr(ansi,"▀")!=NULL);
	const SbArtLayout wide=SbArtChooseLayout(88,12,true);
	const SbArtLayout medium=SbArtChooseLayout(80,10,true);
	const SbArtLayout compact=SbArtChooseLayout(71,8,true);
	assert(wide.visible && wide.columns==24 && wide.rows==12);
	assert(medium.visible && medium.columns==20 && medium.rows==10);
	assert(compact.visible && compact.columns==16 && compact.rows==8);
	assert(SbArtChooseLayout(87,12,true).columns==20);
	assert(SbArtChooseLayout(59,8,true).columns==12);
	assert(SbArtChooseLayout(48,8,true).columns==10);
	assert(!SbArtChooseLayout(40,8,true).visible); assert(!SbArtChooseLayout(80,16,false).visible);
	assert(SbArtDetectColorMode("xterm-256color",NULL,"Apple_Terminal",256)==SB_ART_COLOR_TRUECOLOR);
	assert(SbArtDetectColorMode("xterm-256color",NULL,NULL,256)==SB_ART_COLOR_256);
	assert(SbArtDetectColorMode("dumb","truecolor",NULL,256)==SB_ART_COLOR_NONE);
	SbPreparedArt cache={0}; assert(SbPreparedArtGet(&cache,path,6,3,SB_ART_COLOR_256));
	assert (cache.qualityVersion == SB_ART_QUALITY_VERSION);
	const unsigned int builds=cache.builds; assert(SbPreparedArtGet(&cache,path,6,3,SB_ART_COLOR_256));
	assert(cache.builds==builds && cache.hits==1);
	cache.qualityVersion = SB_ART_QUALITY_VERSION - 1;
	assert (SbPreparedArtGet (&cache, path, 6, 3, SB_ART_COLOR_256));
	assert (cache.builds == builds + 1); /* quality revision invalidates */
	/* Model a 500x448 source requested at 20x10 cells. Aspect preservation
	 * produces 20x9 cells, but cache validity remains keyed by the request. */
	SbPreparedArtDestroy(&cache); snprintf(cache.path,sizeof(cache.path),"%s",path);
	cache.requestedColumns=20; cache.requestedRows=10; cache.columns=20; cache.rows=9;
	cache.mode=SB_ART_COLOR_TRUECOLOR; cache.qualityVersion=SB_ART_QUALITY_VERSION;
	cache.cells=calloc(20*9,sizeof(*cache.cells));
	assert(cache.cells); assert(SbPreparedArtGet(&cache,path,20,10,SB_ART_COLOR_TRUECOLOR));
	assert(cache.builds==0 && cache.hits==1 && cache.columns==20 && cache.rows==9);
	assert(SbPreparedArtGet(&cache,path,24,12,SB_ART_COLOR_TRUECOLOR));
	assert(cache.builds==1 && cache.columns==24 && cache.rows==12);
	assert(SbPreparedArtGet(&cache,path,20,10,SB_ART_COLOR_TRUECOLOR));
	assert(cache.builds==2 && cache.columns==20 && cache.rows==10);
	const unsigned int resizedBuilds=cache.builds;
	assert(SbPreparedArtGet(&cache,path,20,10,SB_ART_COLOR_256));
	assert(cache.builds==resizedBuilds+1); /* color mode invalidates */
	const unsigned int colorBuilds=cache.builds;
	assert(!SbPreparedArtGet(&cache,"/tmp/signalbox-art-other.png",20,10,SB_ART_COLOR_256));
	assert(cache.builds==colorBuilds+1); /* path invalidates, even when decode fails */
	SbPreparedArtDestroy(&cache);
	SbArtImageDestroy(&three); SbArtImageDestroy(&resized); SbArtImageDestroy(&decoded);
	remove(path); puts("art renderer tests passed"); return 0;
}
