#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { SB_ART_URL_MAX=1024, SB_ART_PATH_MAX=1024, SB_ART_MAX_BYTES=5*1024*1024, SB_ART_ID_MAX=64 };
typedef enum {
	SB_ART_PROVIDER_NONE = 0,
	SB_ART_PROVIDER_COVER_ART_ARCHIVE,
} SbAlbumArtProvider;
typedef enum {
	SB_ART_IDENTITY_NONE = 0,
	SB_ART_IDENTITY_RELEASE,
	SB_ART_IDENTITY_RELEASE_GROUP,
} SbAlbumArtIdentityKind;
typedef enum {
	SB_ART_STEP_EXACT_RELEASE = 0,
	SB_ART_STEP_RELEASE_GROUP,
	SB_ART_STEP_ALTERNATE_RELEASE,
	SB_ART_STEP_ALBUM_FAMILY,
	SB_ART_STEP_DONE,
} SbAlbumArtResolutionStep;
typedef struct {
	int status;
	SbAlbumArtProvider providerKind;
	SbAlbumArtIdentityKind identityKind;
	char provider[32], sourceUrl[SB_ART_URL_MAX], mimeType[32];
	char cachedPath[SB_ART_PATH_MAX], releaseId[SB_ART_ID_MAX];
	char releaseGroupId[SB_ART_ID_MAX], reason[32];
	double confidence;
	unsigned int width, height;
	long httpStatus;
	size_t byteCount;
	int64_t fetched;
	uint64_t resolutionElapsedMs, downloadElapsedMs;
	bool cacheHit;
} SbAlbumArtResult;
typedef struct { const char *encodedPath; unsigned int sourceWidth,sourceHeight,targetColumns,targetRows,colorCount; } SbArtRenderRequest;
void SbAlbumArtResultInit(SbAlbumArtResult*);
const char *SbAlbumArtProviderName(SbAlbumArtProvider);
const char *SbAlbumArtStepName(SbAlbumArtResolutionStep);
SbAlbumArtResolutionStep SbAlbumArtNextStep(SbAlbumArtResolutionStep,
		int status, bool hasReleaseGroup, bool hasAlternateRelease,
		bool hasAlbumFamily);
int SbCoverArtHttpStatus(long,int);
bool SbCoverArtParse(const char*,const char*,SbAlbumArtResult*);
bool SbCoverArtParseGroup(const char*,const char*,SbAlbumArtResult*);
bool SbAlbumArtFilename(const char*,const char*,char*,size_t);
bool SbAlbumArtCacheFilename(SbAlbumArtProvider, SbAlbumArtIdentityKind,
		const char*,const char*,char*,size_t);
bool SbAlbumArtMimeSupported(const char*);
bool SbCoverArtLookup(const char *releaseId, const char *artDirectory,
 SbAlbumArtResult *);
bool SbCoverArtGroupLookup(const char *releaseGroupId,
		const char *artDirectory, SbAlbumArtResult *);
