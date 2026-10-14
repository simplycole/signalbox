#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

/* A seekable, bounded compressed-track spool. HTTP download is independent of
 * decoding/output backpressure. The anonymous file is removed on close. */
#define SB_PLAYER_INPUT_LIMIT (128U * 1024U * 1024U)
#define SB_PLAYER_AVIO_BUFFER_SIZE (32U * 1024U)
typedef struct SbPlayerInput SbPlayerInput;
typedef struct {
	uint64_t received, position;
	bool complete;
	int error;
} SbPlayerInputSnapshot;

SbPlayerInput *SbPlayerInputOpen (const char *, const BarSettings_t *,
		int (*interrupt) (void *), void *);
void SbPlayerInputCancel (SbPlayerInput *);
void SbPlayerInputClose (SbPlayerInput *);
void SbPlayerInputGetSnapshot (SbPlayerInput *, SbPlayerInputSnapshot *);
int SbPlayerInputRead (void *, uint8_t *, int);
int64_t SbPlayerInputSeek (void *, int64_t, int);
