#pragma once

#include <stdbool.h>

typedef enum {
	SB_AUDIO_PLATFORM_MACOS = 0,
	SB_AUDIO_PLATFORM_LINUX,
	SB_AUDIO_PLATFORM_WINDOWS,
} SbAudioPlatform;

typedef enum {
	SB_AUDIO_OUTPUT_OPEN = 0,
	SB_AUDIO_OUTPUT_REUSE,
	SB_AUDIO_OUTPUT_REOPEN,
} SbAudioOutputAction;

typedef enum {
	SB_PLAYER_STOP_NONE = 0,
	SB_PLAYER_STOP_NEXT,
	SB_PLAYER_STOP_END_OF_TRACK,
	SB_PLAYER_STOP_STATION_CHANGE,
	SB_PLAYER_STOP_QUIT,
} SbPlayerStopReason;

typedef struct {
	int bits, channels, rate, byteFormat;
	bool live;
} SbAudioOutputFormat;

bool SbAudioOutputFormatEqual (SbAudioOutputFormat, SbAudioOutputFormat);
SbAudioOutputFormat SbAudioOutputTarget (SbAudioPlatform, bool deviceOpen,
		SbAudioOutputFormat current, SbAudioOutputFormat decoded);
SbAudioOutputAction SbAudioOutputPlan (bool deviceOpen,
		SbAudioOutputFormat current, SbAudioOutputFormat requested);
bool SbAudioOutputCloseOnTrackEnd (SbAudioPlatform, bool live);
bool SbAudioOutputCloseAtShutdown (SbAudioPlatform, bool live);
bool SbAudioOutputCloseForStop (SbAudioPlatform, bool live,
		SbPlayerStopReason);
const char *SbPlayerStopReasonName (SbPlayerStopReason);
