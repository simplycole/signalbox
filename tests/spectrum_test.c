#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "spectrum.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void waitCadence (void) {
	const struct timespec delay = {.tv_sec = 0, .tv_nsec = 85000000};
	nanosleep (&delay, NULL);
}

static void waitMs (const long milliseconds) {
	const struct timespec delay = {.tv_sec = milliseconds / 1000,
			.tv_nsec = (milliseconds % 1000) * 1000000};
	nanosleep (&delay, NULL);
}

static size_t dominant (const SbSpectrumSnapshot *snapshot) {
	size_t best = 0;
	for (size_t i = 1; i < SB_SPECTRUM_BANDS; i++)
		if (snapshot->bands[i] > snapshot->bands[best]) best = i;
	return best;
}

static void tone (int16_t *pcm, const size_t frames, const unsigned int channels,
		const unsigned int sampleRate, const float firstHz, const float secondHz,
		const bool rightOnly) {
	for (size_t frame = 0; frame < frames; frame++) {
		float value = firstHz > 0 ? sinf ((float) (2.0 * M_PI * firstHz * frame /
				sampleRate)) : 0.0f;
		if (secondHz > 0) value = 0.5f * value + 0.5f * sinf ((float)
				(2.0 * M_PI * secondHz * frame / sampleRate));
		for (unsigned int channel = 0; channel < channels; channel++)
			pcm[frame * channels + channel] = rightOnly && channel == 0 ? 0 :
					(int16_t) (value * 24000.0f);
	}
}

static int checkTone (SbSpectrum *s, const char *name, const float hz,
		const unsigned int firstExpected, const unsigned int lastExpected,
		const unsigned int sampleRate,
		const bool rightOnly) {
	int16_t pcm[SB_SPECTRUM_WINDOW * 2];
	SbSpectrumReset (s);
	tone (pcm, SB_SPECTRUM_WINDOW, 2, sampleRate, hz, 0, rightOnly);
	waitCadence ();
	SbSpectrumIngestS16 (s, pcm, SB_SPECTRUM_WINDOW, 2, sampleRate);
	SbSpectrumSnapshot snapshot;
	SbSpectrumGetSnapshot (s, &snapshot);
	const size_t actual = dominant (&snapshot);
	printf ("%-18s dominant=%zu level=%.3f\n", name, actual,
			snapshot.bands[actual]);
	return actual >= firstExpected && actual <= lastExpected ? 0 : 1;
}

int main (void) {
	SbSpectrum spectrum;
	if (!SbSpectrumInit (&spectrum)) return 2;
	int failed = 0;
	failed += checkTone (&spectrum, "50 Hz", 50, 0, 0, 44100, false);
	failed += checkTone (&spectrum, "125 Hz", 125, 1, 2, 44100, false);
	failed += checkTone (&spectrum, "250 Hz", 250, 3, 4, 44100, false);
	failed += checkTone (&spectrum, "500 Hz", 500, 5, 5, 44100, false);
	failed += checkTone (&spectrum, "1 kHz", 1000, 6, 7, 44100, false);
	failed += checkTone (&spectrum, "1.25 kHz", 1250, 7, 7, 44100, false);
	failed += checkTone (&spectrum, "2 kHz", 2000, 8, 8, 44100, false);
	failed += checkTone (&spectrum, "3.5 kHz", 3500, 9, 9, 44100, false);
	failed += checkTone (&spectrum, "5 kHz", 5000, 10, 10, 44100, false);
	failed += checkTone (&spectrum, "9 kHz", 9000, 11, 11, 44100, false);
	failed += checkTone (&spectrum, "right-only 1k", 1000, 6, 7, 44100, true);
	failed += checkTone (&spectrum, "48k 4 kHz", 4000, 9, 10, 48000, false);

	int16_t mixed[SB_SPECTRUM_WINDOW * 2];
	SbSpectrumReset (&spectrum);
	tone (mixed, SB_SPECTRUM_WINDOW, 2, 44100, 120, 4000, false);
	waitCadence ();
	SbSpectrumIngestS16 (&spectrum, mixed, SB_SPECTRUM_WINDOW, 2, 44100);
	SbSpectrumSnapshot snapshot;
	SbSpectrumGetSnapshot (&spectrum, &snapshot);
	printf ("%-18s low=%.3f high=%.3f\n", "mixed 120/4k",
			snapshot.bands[2], snapshot.bands[9]);
	if (snapshot.bands[2] < 0.2f || snapshot.bands[9] < 0.2f) failed++;
	const size_t highBand = snapshot.bands[9] > snapshot.bands[10] ? 9 : 10;
	const float strongBody = snapshot.bands[highBand];
	const float strongPeak = snapshot.peaks[highBand];
	if (strongBody < 0.60f) failed++;

	memset (mixed, 0, sizeof (mixed));
	waitCadence ();
	SbSpectrumIngestS16 (&spectrum, mixed, SB_SPECTRUM_WINDOW, 2, 44100);
	SbSpectrumGetSnapshot (&spectrum, &snapshot);
	printf ("%-18s body=%.3f peak=%.3f\n", "first decay",
			snapshot.bands[highBand], snapshot.peaks[highBand]);
	if (snapshot.bands[highBand] >= strongBody * 0.8f ||
			snapshot.peaks[highBand] < snapshot.bands[highBand] ||
			snapshot.peaks[highBand] < strongPeak * 0.9f) failed++;
	for (int i = 0; i < 11; i++) {
		waitCadence ();
		SbSpectrumIngestS16 (&spectrum, mixed, SB_SPECTRUM_WINDOW, 2, 44100);
	}
	SbSpectrumGetSnapshot (&spectrum, &snapshot);
	printf ("%-18s max=%.3f\n", "silence decay",
			snapshot.bands[dominant (&snapshot)]);
	if (snapshot.bands[dominant (&snapshot)] > 0.1f) failed++;

	/* Stale PCM (pause/buffering/transition) fades independently of the normal
	 * musical release path, with the held peak allowed to trail briefly. */
	SbSpectrumReset (&spectrum);
	tone (mixed, SB_SPECTRUM_WINDOW, 2, 44100, 4000, 0, false);
	waitCadence ();
	SbSpectrumIngestS16 (&spectrum, mixed, SB_SPECTRUM_WINDOW, 2, 44100);
	SbSpectrumGetSnapshot (&spectrum, &snapshot);
	const size_t staleBand = dominant (&snapshot);
	const float staleBody = snapshot.bands[staleBand];
	const float stalePeak = snapshot.peaks[staleBand];
	waitMs (300);
	SbSpectrumGetSnapshot (&spectrum, &snapshot);
	printf ("%-18s body=%.3f peak=%.3f\n", "stale 300ms",
			snapshot.bands[staleBand], snapshot.peaks[staleBand]);
	if (snapshot.bands[staleBand] >= staleBody * 0.45f ||
			snapshot.peaks[staleBand] < snapshot.bands[staleBand] ||
			snapshot.peaks[staleBand] >= stalePeak * 0.65f) failed++;
	waitMs (500);
	SbSpectrumGetSnapshot (&spectrum, &snapshot);
	printf ("%-18s body=%.3f peak=%.3f valid=%d\n", "stale 800ms",
			snapshot.bands[staleBand], snapshot.peaks[staleBand], snapshot.valid);
	if (snapshot.valid || snapshot.bands[staleBand] != 0.0f ||
			snapshot.peaks[staleBand] != 0.0f) failed++;

	float canonical[SB_SPECTRUM_BANDS] = {0};
	float compact[SB_SPECTRUM_COMPACT_BANDS];
	canonical[1] = 0.8f; canonical[7] = 0.7f; canonical[11] = 0.9f;
	SbSpectrumAggregateCompact (canonical, compact);
	printf ("%-18s low=%.3f mid=%.3f high=%.3f\n", "compact mapping",
			compact[0], compact[4], compact[7]);
	if (compact[0] != 0.8f || compact[4] != 0.7f || compact[7] != 0.9f)
		failed++;
	SbSpectrumDestroy (&spectrum);
	return failed == 0 ? 0 : 1;
}
