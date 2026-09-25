#include <assert.h>
#include <stdio.h>

#include "player_lifecycle.h"

int main (void) {
	const SbAudioOutputFormat stereo44 = {16, 2, 44100, 1, true};
	const SbAudioOutputFormat stereo48 = {16, 2, 48000, 1, true};
	const SbAudioOutputFormat pipe44 = {16, 2, 44100, 1, false};
	assert (SbAudioOutputPlan (false, stereo44, stereo44) ==
			SB_AUDIO_OUTPUT_OPEN);
	assert (SbAudioOutputPlan (true, stereo44, stereo44) ==
			SB_AUDIO_OUTPUT_REUSE);
	assert (SbAudioOutputPlan (true, stereo44, stereo48) ==
			SB_AUDIO_OUTPUT_REOPEN);

	/* macOS live playback normalizes every later decoder to the already-open
	 * device, so even a changed source rate remains a reuse operation. */
	const SbAudioOutputFormat macTarget = SbAudioOutputTarget (
			SB_AUDIO_PLATFORM_MACOS, true, stereo44, stereo48);
	assert (SbAudioOutputFormatEqual (macTarget, stereo44));
	assert (SbAudioOutputPlan (true, stereo44, macTarget) ==
			SB_AUDIO_OUTPUT_REUSE);
	assert (SbAudioOutputFormatEqual (SbAudioOutputTarget (
			SB_AUDIO_PLATFORM_LINUX, true, stereo44, stereo48), stereo48));
	assert (SbAudioOutputFormatEqual (SbAudioOutputTarget (
			SB_AUDIO_PLATFORM_WINDOWS, true, stereo44, stereo48), stereo48));

	/* Natural end, next, and station change all use the same track-end policy. */
	assert (!SbAudioOutputCloseOnTrackEnd (SB_AUDIO_PLATFORM_MACOS, true));
	assert (!SbAudioOutputCloseForStop (SB_AUDIO_PLATFORM_MACOS, true,
			SB_PLAYER_STOP_NEXT));
	assert (!SbAudioOutputCloseForStop (SB_AUDIO_PLATFORM_MACOS, true,
			SB_PLAYER_STOP_END_OF_TRACK));
	assert (!SbAudioOutputCloseForStop (SB_AUDIO_PLATFORM_MACOS, true,
			SB_PLAYER_STOP_STATION_CHANGE));
	assert (!SbAudioOutputCloseForStop (SB_AUDIO_PLATFORM_MACOS, true,
			SB_PLAYER_STOP_QUIT));
	assert (SbAudioOutputCloseOnTrackEnd (SB_AUDIO_PLATFORM_MACOS, false));
	assert (SbAudioOutputCloseOnTrackEnd (SB_AUDIO_PLATFORM_LINUX, true));
	assert (SbAudioOutputCloseOnTrackEnd (SB_AUDIO_PLATFORM_WINDOWS, true));
	assert (!SbAudioOutputCloseAtShutdown (SB_AUDIO_PLATFORM_MACOS, true));
	assert (SbAudioOutputCloseAtShutdown (SB_AUDIO_PLATFORM_MACOS, false));
	assert (SbAudioOutputCloseAtShutdown (SB_AUDIO_PLATFORM_LINUX, true));
	assert (SbAudioOutputCloseAtShutdown (SB_AUDIO_PLATFORM_WINDOWS, true));
	assert (SbAudioOutputCloseForStop (SB_AUDIO_PLATFORM_LINUX, true,
			SB_PLAYER_STOP_NEXT));
	assert (SbAudioOutputCloseForStop (SB_AUDIO_PLATFORM_WINDOWS, true,
			SB_PLAYER_STOP_QUIT));
	assert (!SbAudioOutputFormatEqual (stereo44, pipe44));

	assert (SbPlayerStopReasonName (SB_PLAYER_STOP_NEXT)[0] == 'n');
	assert (SbPlayerStopReasonName (SB_PLAYER_STOP_END_OF_TRACK)[0] == 'e');
	assert (SbPlayerStopReasonName (SB_PLAYER_STOP_STATION_CHANGE)[0] == 's');
	assert (SbPlayerStopReasonName (SB_PLAYER_STOP_QUIT)[0] == 'q');
	puts ("player lifecycle tests passed");
	return 0;
}
