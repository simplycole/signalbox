#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { SB_ART_URL_MAX=1024, SB_ART_PATH_MAX=1024, SB_ART_MAX_BYTES=5*1024*1024, SB_ART_ID_MAX=64 };
typedef struct { int status; char provider[32],sourceUrl[SB_ART_URL_MAX],mimeType[32],cachedPath[SB_ART_PATH_MAX],releaseId[SB_ART_ID_MAX],reason[32]; unsigned int width,height; long httpStatus; size_t byteCount; int64_t fetched; } SbAlbumArtResult;
typedef struct { const char *encodedPath; unsigned int sourceWidth,sourceHeight,targetColumns,targetRows,colorCount; } SbArtRenderRequest;
void SbAlbumArtResultInit(SbAlbumArtResult*);
int SbCoverArtHttpStatus(long,int);
bool SbCoverArtParse(const char*,const char*,SbAlbumArtResult*);
bool SbAlbumArtFilename(const char*,const char*,char*,size_t);
bool SbAlbumArtMimeSupported(const char*);
bool SbCoverArtLookup(const char *releaseId, const char *artDirectory,
 SbAlbumArtResult *);
