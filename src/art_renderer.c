#include "art_renderer.h"

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void SbArtImageDestroy (SbArtImage *image) {
	if (image == NULL) return;
	free (image->rgba); memset (image, 0, sizeof (*image));
}

static enum AVCodecID SbArtCodec (const uint8_t *p, const size_t n) {
	if (n >= 8 && !memcmp (p, "\x89PNG\r\n\x1a\n", 8)) return AV_CODEC_ID_PNG;
	if (n >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff) return AV_CODEC_ID_MJPEG;
	if (n >= 12 && !memcmp (p, "RIFF", 4) && !memcmp (p + 8, "WEBP", 4)) return AV_CODEC_ID_WEBP;
	return AV_CODEC_ID_NONE;
}

bool SbArtDecodeFile (const char *path, SbArtImage *out) {
	if (out == NULL) return false; memset (out, 0, sizeof (*out));
	FILE *f = path != NULL ? fopen (path, "rb") : NULL;
	if (f == NULL || fseek (f, 0, SEEK_END) != 0) { if (f) fclose (f); return false; }
	const long length = ftell (f);
	if (length <= 0 || length > 5 * 1024 * 1024 || fseek (f, 0, SEEK_SET) != 0) { fclose (f); return false; }
	uint8_t *encoded = malloc ((size_t) length + AV_INPUT_BUFFER_PADDING_SIZE);
	if (encoded == NULL) { fclose (f); return false; }
	const bool readOk = fread (encoded, 1, (size_t) length, f) == (size_t) length;
	fclose (f); memset (encoded + length, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	const enum AVCodecID id = readOk ? SbArtCodec (encoded, (size_t) length) : AV_CODEC_ID_NONE;
	const AVCodec *codec = avcodec_find_decoder (id);
	AVCodecContext *context = codec != NULL ? avcodec_alloc_context3 (codec) : NULL;
	AVFrame *frame = av_frame_alloc (); AVPacket *packet = av_packet_alloc ();
	bool ok = context != NULL && frame != NULL && packet != NULL &&
			avcodec_open2 (context, codec, NULL) >= 0;
	if (ok) { packet->data = encoded; packet->size = (int) length;
		ok = avcodec_send_packet (context, packet) >= 0 && avcodec_receive_frame (context, frame) >= 0; }
	if (ok) ok = frame->width > 0 && frame->height > 0 &&
			frame->width <= SB_ART_DECODE_MAX_DIMENSION && frame->height <= SB_ART_DECODE_MAX_DIMENSION &&
			(uint64_t) frame->width * (uint64_t) frame->height <= SB_ART_DECODE_MAX_PIXELS;
	if (ok) {
		const size_t bytes = (size_t) frame->width * (size_t) frame->height * 4;
		out->rgba = malloc (bytes); ok = out->rgba != NULL;
		if (ok) {
			struct SwsContext *sws = sws_getContext (frame->width, frame->height,
					(enum AVPixelFormat) frame->format, frame->width, frame->height,
					AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
			uint8_t *dst[] = {out->rgba}; int stride[] = {frame->width * 4};
			ok = sws != NULL && sws_scale (sws, (const uint8_t *const *) frame->data,
					frame->linesize, 0, frame->height, dst, stride) == frame->height;
			sws_freeContext (sws);
			if (ok) { out->width = (unsigned int) frame->width; out->height = (unsigned int) frame->height; }
		}
	}
	av_packet_free (&packet); av_frame_free (&frame); avcodec_free_context (&context); free (encoded);
	if (!ok) SbArtImageDestroy (out); return ok;
}

bool SbArtResizeFit (const SbArtImage *source, unsigned int width,
		unsigned int height, SbArtImage *out) {
	if (out == NULL) return false; memset (out, 0, sizeof (*out));
	if (source == NULL || source->rgba == NULL || width == 0 || height == 0 ||
			width > SB_ART_DECODE_MAX_DIMENSION || height > SB_ART_DECODE_MAX_DIMENSION ||
			(uint64_t) width * height > SB_ART_DECODE_MAX_PIXELS) return false;
	unsigned int fitWidth = width, fitHeight = (unsigned int) ((uint64_t) source->height * width / source->width);
	if (fitHeight > height) { fitHeight = height; fitWidth = (unsigned int) ((uint64_t) source->width * height / source->height); }
	if (fitWidth == 0) fitWidth = 1; if (fitHeight == 0) fitHeight = 1;
	out->rgba = malloc ((size_t) fitWidth * fitHeight * 4); if (!out->rgba) return false;
	struct SwsContext *sws = sws_getContext ((int) source->width, (int) source->height,
			AV_PIX_FMT_RGBA, (int) fitWidth, (int) fitHeight, AV_PIX_FMT_RGBA,
			SWS_BILINEAR, NULL, NULL, NULL);
	const uint8_t *src[] = {source->rgba}; int srcStride[] = {(int) source->width * 4};
	uint8_t *dst[] = {out->rgba}; int dstStride[] = {(int) fitWidth * 4};
	const bool ok = sws != NULL && sws_scale (sws, src, srcStride, 0,
			(int) source->height, dst, dstStride) == (int) fitHeight;
	sws_freeContext (sws); if (!ok) { SbArtImageDestroy (out); return false; }
	out->width = fitWidth; out->height = fitHeight; return true;
}

unsigned char SbArtXterm256 (uint8_t r, uint8_t g, uint8_t b) {
	int gray = ((int) r + g + b) / 3, grayIndex = (gray - 8 + 5) / 10;
	if (grayIndex < 0) grayIndex = 0; if (grayIndex > 23) grayIndex = 23;
	const int grayValue = 8 + grayIndex * 10;
	const int ri = (r * 5 + 127) / 255, gi = (g * 5 + 127) / 255, bi = (b * 5 + 127) / 255;
	const int rv = ri ? 55 + ri * 40 : 0, gv = gi ? 55 + gi * 40 : 0, bv = bi ? 55 + bi * 40 : 0;
	const int cubeDistance = (r-rv)*(r-rv) + (g-gv)*(g-gv) + (b-bv)*(b-bv);
	const int grayDistance = (r-grayValue)*(r-grayValue) + (g-grayValue)*(g-grayValue) + (b-grayValue)*(b-grayValue);
	return (unsigned char) (grayDistance < cubeDistance ? 232 + grayIndex : 16 + 36*ri + 6*gi + bi);
}

bool SbArtCellsBuild (const SbArtImage *image, SbArtColorMode mode,
		SbArtCell **cells, unsigned int *columns, unsigned int *rows) {
	if (!image || !image->rgba || !cells || !columns || !rows || mode == SB_ART_COLOR_NONE) return false;
	*columns = image->width; *rows = (image->height + 1) / 2;
	*cells = calloc ((size_t) *columns * *rows, sizeof (**cells)); if (!*cells) return false;
	for (unsigned int y = 0; y < *rows; y++) for (unsigned int x = 0; x < *columns; x++) {
		SbArtCell *cell = &(*cells)[(size_t)y * *columns + x];
		const uint8_t *upper = &image->rgba[((size_t) (y*2) * image->width + x) * 4];
		const uint8_t *lower = y*2+1 < image->height ? upper + image->width*4 : upper;
		memcpy (cell->upper, upper, 3); memcpy (cell->lower, lower, 3);
		cell->upper256 = SbArtXterm256 (upper[0],upper[1],upper[2]);
		cell->lower256 = SbArtXterm256 (lower[0],lower[1],lower[2]);
	}
	return true;
}

void SbPreparedArtDestroy (SbPreparedArt *art) { if (art) { free (art->cells); memset (art, 0, sizeof (*art)); } }
bool SbPreparedArtGet (SbPreparedArt *art, const char *path, unsigned int columns,
		unsigned int rows, SbArtColorMode mode) {
	if (!art || !path || !*path || !columns || !rows || mode == SB_ART_COLOR_NONE) return false;
	if ((art->cells || art->failed) && !strcmp (art->path,path) &&
			art->requestedColumns==columns && art->requestedRows==rows &&
			art->mode==mode) { art->hits++; return !art->failed; }
	const unsigned int builds = art->builds; SbPreparedArtDestroy (art); art->builds = builds;
	art->builds++; snprintf(art->path,sizeof(art->path),"%s",path);
	art->requestedColumns=columns; art->requestedRows=rows; art->mode=mode;
	SbArtImage source={0}, resized={0}; bool ok=SbArtDecodeFile(path,&source); art->sourceWidth=source.width; art->sourceHeight=source.height;
	if(ok) ok=SbArtResizeFit(&source,columns,rows*2,&resized);
	unsigned int cellColumns=0,cellRows=0; if(ok) ok=SbArtCellsBuild(&resized,mode,&art->cells,&cellColumns,&cellRows);
	SbArtImageDestroy(&source); SbArtImageDestroy(&resized); if(!ok){art->failed=true;return false;}
	art->columns=cellColumns; art->rows=cellRows; return true;
}

SbArtLayout SbArtChooseLayout (unsigned int width, unsigned int height, bool enabled) {
	SbArtLayout out={0}; if(!enabled || width < 48 || height < 8) return out;
	out.columns = width >= 80 && height >= 10 ? 20 :
			width >= 68 && height >= 8 ? 16 : width >= 56 ? 12 : 10;
	if (out.columns + 24 > width) return (SbArtLayout){0};
	out.rows = (out.columns + 1) / 2; if(out.rows > height) out.rows=height;
	out.visible = out.rows >= 5; return out;
}

SbArtColorMode SbArtDetectColorMode (const char *term, const char *colorTerm,
		const char *termProgram, const int colors) {
	if (term == NULL || *term == '\0' || !strcmp (term, "dumb"))
		return SB_ART_COLOR_NONE;
	if ((colorTerm != NULL && (strstr (colorTerm, "truecolor") != NULL ||
			strstr (colorTerm, "24bit") != NULL)) ||
			(termProgram != NULL && !strcmp (termProgram, "Apple_Terminal")))
		return SB_ART_COLOR_TRUECOLOR;
	return colors >= 256 ? SB_ART_COLOR_256 : SB_ART_COLOR_NONE;
}

size_t SbArtCellAnsi (const SbArtCell *cell, SbArtColorMode mode, char *out, size_t size) {
	if(!cell || !out || !size) return 0; int n=0;
	if(mode==SB_ART_COLOR_TRUECOLOR) n=snprintf(out,size,"\033[38;2;%u;%u;%u;48;2;%u;%u;%um▀",
		cell->upper[0],cell->upper[1],cell->upper[2],cell->lower[0],cell->lower[1],cell->lower[2]);
	else if(mode==SB_ART_COLOR_256) n=snprintf(out,size,"\033[38;5;%u;48;5;%um▀",cell->upper256,cell->lower256);
	return n>0 && (size_t)n<size ? (size_t)n : 0;
}
