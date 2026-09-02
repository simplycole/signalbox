#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

enum {
	SB_SPECTRUM_BANDS = 12,
	SB_SPECTRUM_COMPACT_BANDS = 8,
	SB_SPECTRUM_WINDOW = 1024,
};

typedef struct {
	float bands[SB_SPECTRUM_BANDS];
	float peaks[SB_SPECTRUM_BANDS];
	bool valid;
	uint64_t generation;
} SbSpectrumSnapshot;

/* Single-producer analyzer. PCM ingestion is called by the audio-output
 * thread; snapshots may be copied by the main/UI thread. */
typedef struct {
	pthread_mutex_t snapshotLock;
	float samples[SB_SPECTRUM_WINDOW];
	float bands[SB_SPECTRUM_BANDS];
	float peaks[SB_SPECTRUM_BANDS];
	unsigned int peakHold[SB_SPECTRUM_BANDS];
	size_t firstBin[SB_SPECTRUM_BANDS];
	size_t lastBin[SB_SPECTRUM_BANDS];
	size_t writeOffset;
	size_t sampleCount;
	unsigned int sampleRate;
	unsigned int channels;
	uint64_t lastAnalysisMs;
	uint64_t lastPcmMs;
	uint64_t generation;
	bool enabled;
	bool valid;
} SbSpectrum;

bool SbSpectrumInit (SbSpectrum *);
void SbSpectrumDestroy (SbSpectrum *);
void SbSpectrumSetEnabled (SbSpectrum *, bool);
void SbSpectrumReset (SbSpectrum *);
void SbSpectrumIngestS16 (SbSpectrum *, const int16_t *, size_t,
		unsigned int, unsigned int);
void SbSpectrumGetSnapshot (SbSpectrum *, SbSpectrumSnapshot *);
void SbSpectrumAggregateCompact (const float [SB_SPECTRUM_BANDS],
		float [SB_SPECTRUM_COMPACT_BANDS]);
