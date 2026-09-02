#include "config.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "spectrum.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
	SB_SPECTRUM_INTERVAL_MS = 80,
	SB_SPECTRUM_STALE_MS = 100,
	SB_SPECTRUM_PEAK_HOLD_FRAMES = 3,
};

static const float SbSpectrumEdges[SB_SPECTRUM_BANDS + 1] =
		{35, 65, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000,
		7000, 12000};

static void SbSpectrumMapBins (SbSpectrum *s) {
	const float nyquist = s->sampleRate * 0.5f;
	for (size_t band = 0; band < SB_SPECTRUM_BANDS; band++) {
		const float upper = SbSpectrumEdges[band + 1] < nyquist ?
				SbSpectrumEdges[band + 1] : nyquist;
		s->firstBin[band] = (size_t) ceilf (SbSpectrumEdges[band] *
				SB_SPECTRUM_WINDOW / (float) s->sampleRate);
		s->lastBin[band] = (size_t) floorf (upper * SB_SPECTRUM_WINDOW /
				(float) s->sampleRate);
		if (s->firstBin[band] < 1) s->firstBin[band] = 1;
		if (s->lastBin[band] >= SB_SPECTRUM_WINDOW / 2)
			s->lastBin[band] = SB_SPECTRUM_WINDOW / 2 - 1;
	}
}

static uint64_t SbSpectrumNowMs (void) {
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return (uint64_t) now.tv_sec * 1000u + (uint64_t) now.tv_nsec / 1000000u;
}

static void SbSpectrumFft (float *real, float *imag) {
	for (size_t i = 1, j = 0; i < SB_SPECTRUM_WINDOW; i++) {
		size_t bit = SB_SPECTRUM_WINDOW >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) {
			const float tr = real[i], ti = imag[i];
			real[i] = real[j]; imag[i] = imag[j];
			real[j] = tr; imag[j] = ti;
		}
	}
	for (size_t length = 2; length <= SB_SPECTRUM_WINDOW; length <<= 1) {
		const float angle = (float) (-2.0 * M_PI / (double) length);
		const float wrStep = cosf (angle), wiStep = sinf (angle);
		for (size_t base = 0; base < SB_SPECTRUM_WINDOW; base += length) {
			float wr = 1.0f, wi = 0.0f;
			for (size_t j = 0; j < length / 2; j++) {
				const size_t even = base + j, odd = even + length / 2;
				const float tr = wr * real[odd] - wi * imag[odd];
				const float ti = wr * imag[odd] + wi * real[odd];
				real[odd] = real[even] - tr; imag[odd] = imag[even] - ti;
				real[even] += tr; imag[even] += ti;
				const float nextWr = wr * wrStep - wi * wiStep;
				wi = wr * wiStep + wi * wrStep;
				wr = nextWr;
			}
		}
	}
}

static void SbSpectrumAnalyze (SbSpectrum *s, const uint64_t now) {
	float real[SB_SPECTRUM_WINDOW], imag[SB_SPECTRUM_WINDOW];
	for (size_t i = 0; i < SB_SPECTRUM_WINDOW; i++) {
		const size_t source = (s->writeOffset + i) % SB_SPECTRUM_WINDOW;
		const float hann = 0.5f - 0.5f * cosf ((float) (2.0 * M_PI * i /
				(SB_SPECTRUM_WINDOW - 1)));
		real[i] = s->samples[source] * hann;
		imag[i] = 0.0f;
	}
	SbSpectrumFft (real, imag);

	float next[SB_SPECTRUM_BANDS] = {0};
	for (size_t band = 0; band < SB_SPECTRUM_BANDS; band++) {
		const size_t first = s->firstBin[band], last = s->lastBin[band];
		float energy = 0.0f;
		size_t bins = 0;
		for (size_t bin = first; bin <= last && first <= last; bin++) {
			energy += real[bin] * real[bin] + imag[bin] * imag[bin];
			bins++;
		}
		/* RMS magnitude, Hann coherent-gain correction, then restrained visual
		 * compression.  The slight gamma keeps low-level energy from masking
		 * musical changes while retaining useful motion in quiet material. */
		const float magnitude = bins > 0 ?
				2.0f * sqrtf (energy / bins) / (SB_SPECTRUM_WINDOW * 0.5f) : 0.0f;
		const float db = 20.0f * log10f (magnitude + 1.0e-9f);
		next[band] = (db + 68.0f) / 56.0f;
		if (next[band] < 0.0f) next[band] = 0.0f;
		if (next[band] > 1.0f) next[band] = 1.0f;
		next[band] = powf (next[band], 1.12f);
	}

	pthread_mutex_lock (&s->snapshotLock);
	if (!s->enabled) {
		pthread_mutex_unlock (&s->snapshotLock);
		return;
	}
	for (size_t band = 0; band < SB_SPECTRUM_BANDS; band++) {
		const float coefficient = next[band] > s->bands[band] ? 0.86f : 0.32f;
		s->bands[band] += (next[band] - s->bands[band]) * coefficient;
		if (s->bands[band] >= s->peaks[band]) {
			s->peaks[band] = s->bands[band];
			s->peakHold[band] = SB_SPECTRUM_PEAK_HOLD_FRAMES;
		} else if (s->peakHold[band] > 0) {
			s->peakHold[band]--;
		} else {
			s->peaks[band] -= 0.045f;
			if (s->peaks[band] < s->bands[band]) s->peaks[band] = s->bands[band];
		}
	}
	s->valid = true;
	s->lastPcmMs = now;
	s->generation++;
	pthread_mutex_unlock (&s->snapshotLock);
	s->lastAnalysisMs = now;
}

bool SbSpectrumInit (SbSpectrum *s) {
	assert (s != NULL);
	memset (s, 0, sizeof (*s));
	s->enabled = true;
	return pthread_mutex_init (&s->snapshotLock, NULL) == 0;
}

void SbSpectrumDestroy (SbSpectrum *s) {
	assert (s != NULL);
	pthread_mutex_destroy (&s->snapshotLock);
}

void SbSpectrumSetEnabled (SbSpectrum *s, const bool enabled) {
	assert (s != NULL);
	pthread_mutex_lock (&s->snapshotLock);
	s->enabled = enabled;
	if (!enabled) {
		memset (s->bands, 0, sizeof (s->bands));
		memset (s->peaks, 0, sizeof (s->peaks));
		s->valid = false;
		s->generation++;
	}
	pthread_mutex_unlock (&s->snapshotLock);
}

void SbSpectrumReset (SbSpectrum *s) {
	assert (s != NULL);
	pthread_mutex_lock (&s->snapshotLock);
	memset (s->bands, 0, sizeof (s->bands));
	memset (s->peaks, 0, sizeof (s->peaks));
	memset (s->peakHold, 0, sizeof (s->peakHold));
	s->valid = false;
	s->generation++;
	pthread_mutex_unlock (&s->snapshotLock);
	memset (s->samples, 0, sizeof (s->samples));
	s->writeOffset = s->sampleCount = 0;
	s->sampleRate = s->channels = 0;
	s->lastAnalysisMs = s->lastPcmMs = 0;
}

void SbSpectrumIngestS16 (SbSpectrum *s, const int16_t *pcm,
		const size_t frames, const unsigned int channels,
		const unsigned int sampleRate) {
	assert (s != NULL);
	pthread_mutex_lock (&s->snapshotLock);
	const bool enabled = s->enabled;
	pthread_mutex_unlock (&s->snapshotLock);
	if (!enabled || pcm == NULL || frames == 0 || channels == 0 ||
			sampleRate == 0) return;
	if (s->sampleRate != sampleRate || s->channels != channels) {
		SbSpectrumReset (s);
		s->sampleRate = sampleRate;
		s->channels = channels;
		SbSpectrumMapBins (s);
	}
	for (size_t frame = 0; frame < frames; frame++) {
		float mono = 0.0f;
		for (unsigned int channel = 0; channel < channels; channel++)
			mono += pcm[frame * channels + channel] / 32768.0f;
		s->samples[s->writeOffset] = mono / channels;
		s->writeOffset = (s->writeOffset + 1) % SB_SPECTRUM_WINDOW;
		if (s->sampleCount < SB_SPECTRUM_WINDOW) s->sampleCount++;
	}
	const uint64_t now = SbSpectrumNowMs ();
	if (s->sampleCount == SB_SPECTRUM_WINDOW &&
			(now - s->lastAnalysisMs >= SB_SPECTRUM_INTERVAL_MS ||
			s->lastAnalysisMs == 0)) SbSpectrumAnalyze (s, now);
}

void SbSpectrumAggregateCompact (const float source[SB_SPECTRUM_BANDS],
		float compact[SB_SPECTRUM_COMPACT_BANDS]) {
	assert (source != NULL && compact != NULL);
	static const unsigned char first[SB_SPECTRUM_COMPACT_BANDS] =
			{0, 2, 3, 5, 6, 8, 9, 11};
	static const unsigned char last[SB_SPECTRUM_COMPACT_BANDS] =
			{1, 2, 4, 5, 7, 8, 10, 11};
	for (size_t out = 0; out < SB_SPECTRUM_COMPACT_BANDS; out++) {
		compact[out] = source[first[out]];
		for (size_t in = first[out] + 1; in <= last[out]; in++)
			if (source[in] > compact[out]) compact[out] = source[in];
	}
}

void SbSpectrumGetSnapshot (SbSpectrum *s, SbSpectrumSnapshot *snapshot) {
	assert (s != NULL && snapshot != NULL);
	memset (snapshot, 0, sizeof (*snapshot));
	const uint64_t now = SbSpectrumNowMs ();
	pthread_mutex_lock (&s->snapshotLock);
	memcpy (snapshot->bands, s->bands, sizeof (snapshot->bands));
	memcpy (snapshot->peaks, s->peaks, sizeof (snapshot->peaks));
	snapshot->valid = s->valid && s->enabled;
	snapshot->generation = s->generation;
	const uint64_t stale = now > s->lastPcmMs ? now - s->lastPcmMs : 0;
	pthread_mutex_unlock (&s->snapshotLock);
	if (snapshot->valid && stale > SB_SPECTRUM_STALE_MS) {
		const float elapsed = (float) (stale - SB_SPECTRUM_STALE_MS);
		/* Silence caused by pause, buffering, or a track boundary must fade much
		 * faster than normal musical release.  Peaks retain a little more inertia. */
		const float bodyDecay = expf (-elapsed / 150.0f);
		const float peakDecay = expf (-elapsed / 190.0f);
		bool visible = false;
		for (size_t band = 0; band < SB_SPECTRUM_BANDS; band++) {
			snapshot->bands[band] *= bodyDecay;
			snapshot->peaks[band] *= peakDecay;
			if (snapshot->bands[band] < 0.04f) snapshot->bands[band] = 0.0f;
			if (snapshot->peaks[band] < 0.025f) snapshot->peaks[band] = 0.0f;
			if (snapshot->bands[band] > 0.0f || snapshot->peaks[band] > 0.0f)
				visible = true;
		}
		snapshot->valid = visible;
	}
}
