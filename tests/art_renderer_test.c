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

int main (void) {
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
	SbArtLayout large=SbArtChooseLayout(98,11,true), medium=SbArtChooseLayout(71,8,true);
	assert(large.visible && large.columns==20); assert(medium.visible && medium.columns==16);
	assert(SbArtChooseLayout(59,8,true).columns==12);
	assert(!SbArtChooseLayout(40,8,true).visible); assert(!SbArtChooseLayout(80,16,false).visible);
	assert(SbArtDetectColorMode("xterm-256color",NULL,"Apple_Terminal",256)==SB_ART_COLOR_TRUECOLOR);
	assert(SbArtDetectColorMode("xterm-256color",NULL,NULL,256)==SB_ART_COLOR_256);
	assert(SbArtDetectColorMode("dumb","truecolor",NULL,256)==SB_ART_COLOR_NONE);
	SbPreparedArt cache={0}; assert(SbPreparedArtGet(&cache,path,6,3,SB_ART_COLOR_256));
	const unsigned int builds=cache.builds; assert(SbPreparedArtGet(&cache,path,6,3,SB_ART_COLOR_256));
	assert(cache.builds==builds && cache.hits==1);
	/* Model a 500x448 source requested at 20x10 cells. Aspect preservation
	 * produces 20x9 cells, but cache validity remains keyed by the request. */
	SbPreparedArtDestroy(&cache); snprintf(cache.path,sizeof(cache.path),"%s",path);
	cache.requestedColumns=20; cache.requestedRows=10; cache.columns=20; cache.rows=9;
	cache.mode=SB_ART_COLOR_TRUECOLOR; cache.cells=calloc(20*9,sizeof(*cache.cells));
	assert(cache.cells); assert(SbPreparedArtGet(&cache,path,20,10,SB_ART_COLOR_TRUECOLOR));
	assert(cache.builds==0 && cache.hits==1 && cache.columns==20 && cache.rows==9);
	assert(SbPreparedArtGet(&cache,path,21,10,SB_ART_COLOR_TRUECOLOR));
	assert(cache.builds==1); /* requested resize invalidates */
	const unsigned int resizedBuilds=cache.builds;
	assert(SbPreparedArtGet(&cache,path,21,10,SB_ART_COLOR_256));
	assert(cache.builds==resizedBuilds+1); /* color mode invalidates */
	const unsigned int colorBuilds=cache.builds;
	assert(!SbPreparedArtGet(&cache,"/tmp/signalbox-art-other.png",21,10,SB_ART_COLOR_256));
	assert(cache.builds==colorBuilds+1); /* path invalidates, even when decode fails */
	SbPreparedArtDestroy(&cache);
	SbArtImageDestroy(&three); SbArtImageDestroy(&resized); SbArtImageDestroy(&decoded);
	remove(path); puts("art renderer tests passed"); return 0;
}
