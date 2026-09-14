#include "enrichment.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
int main(void){SbAlbumArtResult r;const char*j="{\"images\":[{\"front\":false,\"image\":\"https://x/back.jpg\"},{\"front\":true,\"image\":\"https://x/full.jpg\",\"thumbnails\":{\"500\":\"https://x/500.jpg\"}}]}";assert(SbCoverArtParse(j,"abc-def",&r));assert(!strcmp(r.sourceUrl,"https://x/500.jpg"));assert(!strcmp(r.releaseId,"abc-def"));assert(!SbCoverArtParse("{\"images\":[{\"front\":false,\"image\":\"https://x/back.jpg\"}]}","x",&r)&&r.status==SB_LOOKUP_NO_MATCH);assert(SbCoverArtHttpStatus(404,CURLE_OK)==SB_LOOKUP_NO_MATCH);assert(SbCoverArtHttpStatus(503,CURLE_OK)==SB_LOOKUP_UNAVAILABLE);assert(SbCoverArtHttpStatus(429,CURLE_OK)==SB_LOOKUP_UNAVAILABLE);assert(SbCoverArtHttpStatus(200,CURLE_COULDNT_CONNECT)==SB_LOOKUP_ERROR);assert(!SbCoverArtParse("bad","x",&r)&&r.status==SB_LOOKUP_ERROR);char name[64];assert(SbAlbumArtFilename("abc-def","image/png",name,sizeof(name))&&!strcmp(name,"abc-def.png"));assert(!SbAlbumArtFilename("../bad","image/jpeg",name,sizeof(name)));assert(SbAlbumArtMimeSupported("image/webp"));puts("album art tests passed");}
