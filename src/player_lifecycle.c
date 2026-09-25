#include "player_lifecycle.h"

bool SbAudioOutputFormatEqual (const SbAudioOutputFormat left,
		const SbAudioOutputFormat right) {
	return left.bits == right.bits && left.channels == right.channels &&
			left.rate == right.rate && left.byteFormat == right.byteFormat &&
			left.live == right.live;
}

SbAudioOutputFormat SbAudioOutputTarget (const SbAudioPlatform platform,
		const bool deviceOpen, const SbAudioOutputFormat current,
		const SbAudioOutputFormat decoded) {
	/* CoreAudio's libao device is process-lived. Once opened, each later FFmpeg
	 * graph is normalized to that compatible PCM format instead of cycling the
	 * AudioUnit during ordinary track transitions. */
	if (platform == SB_AUDIO_PLATFORM_MACOS && decoded.live && deviceOpen)
		return current;
	return decoded;
}

SbAudioOutputAction SbAudioOutputPlan (const bool deviceOpen,
		const SbAudioOutputFormat current,
		const SbAudioOutputFormat requested) {
	if (!deviceOpen) return SB_AUDIO_OUTPUT_OPEN;
	return SbAudioOutputFormatEqual (current, requested) ?
			SB_AUDIO_OUTPUT_REUSE : SB_AUDIO_OUTPUT_REOPEN;
}

bool SbAudioOutputCloseOnTrackEnd (const SbAudioPlatform platform,
		const bool live) {
	return platform != SB_AUDIO_PLATFORM_MACOS || !live;
}

bool SbAudioOutputCloseAtShutdown (const SbAudioPlatform platform,
		const bool live) {
	/* libao's macOS plugin can deadlock indefinitely in AudioUnit teardown even
	 * after its writer has stopped. Process exit safely reclaims this one device. */
	return platform != SB_AUDIO_PLATFORM_MACOS || !live;
}

bool SbAudioOutputCloseForStop (const SbAudioPlatform platform,
		const bool live, const SbPlayerStopReason reason) {
	return reason == SB_PLAYER_STOP_QUIT ?
			SbAudioOutputCloseAtShutdown (platform, live) :
			SbAudioOutputCloseOnTrackEnd (platform, live);
}

const char *SbPlayerStopReasonName (const SbPlayerStopReason reason) {
	switch (reason) {
		case SB_PLAYER_STOP_NEXT: return "next";
		case SB_PLAYER_STOP_END_OF_TRACK: return "end_of_track";
		case SB_PLAYER_STOP_STATION_CHANGE: return "station_change";
		case SB_PLAYER_STOP_QUIT: return "quit";
		case SB_PLAYER_STOP_NONE: return "none";
	}
	return "unknown";
}
