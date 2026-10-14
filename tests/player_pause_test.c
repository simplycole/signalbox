/* Real AAC demux/decoder/filter/output-thread path; deterministic HTTP chunks
 * and an observational libao sink. No external network, credentials, device or long wait. */
#include "../src/config.h"
#undef HAVE_DEBUGLOG
#include "../src/player.h"
#include <curl/curl.h>
#include <libavutil/opt.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

static CURLcode testTransfer (SbPlayerInput *, CURL *);
#define curl_easy_perform(http) testTransfer(input, http)
#include "../src/player_input.c"
#undef curl_easy_perform

static void testAoInit (void) {}
static void testAoShutdown (void) {}
static int testAoDefault (void) { return 0; }
static ao_device *testAoOpen (int driver, ao_sample_format *format, ao_option *options);
static int testAoPlay (ao_device *, char *, uint_32);
static int testAoClose (ao_device *device) { (void) device; return 1; }
#define ao_initialize testAoInit
#define ao_shutdown testAoShutdown
#define ao_default_driver_id testAoDefault
#define ao_open_live testAoOpen
#define ao_play testAoPlay
#define ao_close testAoClose
#include "../src/player.c"
#undef ao_initialize
#undef ao_shutdown
#undef ao_default_driver_id
#undef ao_open_live
#undef ao_play
#undef ao_close
#define main SignalboxProgramMain
#include "../src/main.c"
#undef main

static pthread_mutex_t fixtureLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixtureCond = PTHREAD_COND_INITIALIZER;
static bool holdTransfer, transferEntered, readReturned, pauseAtOutput;
static CURLcode transferResult;
static uint8_t *fixture;
static size_t fixtureSize;
static uint64_t outputHash, outputBytes;
static unsigned outputCalls;
static player_t *outputPlayer;
static bool realHttp;
static const char *playerUrl = "https://example.invalid/audio";

static CURLcode testTransfer (SbPlayerInput *input, CURL *http) {
	if (realHttp) {
		/* Keep the fixture on loopback even with proxy environment variables. */
		assert (curl_easy_setopt (http, CURLOPT_PROXY, "") == CURLE_OK);
		return curl_easy_perform (http);
	}
	char header[80];
	const int headerSize = snprintf (header, sizeof (header), "Content-Length: %zu\r\n", fixtureSize);
	assert (headerData (header, 1, headerSize, input) == (size_t) headerSize);
	pthread_mutex_lock (&fixtureLock);
	transferEntered = true;
	pthread_cond_broadcast (&fixtureCond);
	while (holdTransfer) {
		pthread_mutex_unlock (&fixtureLock);
		if (progress (input, 0, 0, 0, 0)) return CURLE_ABORTED_BY_CALLBACK;
		SbPlatformSleepMs (1);
		pthread_mutex_lock (&fixtureLock);
	}
	pthread_mutex_unlock (&fixtureLock);
	/* Unlike PCM backpressure, pause cannot stop this compressed producer. */
	for (size_t at = 0; at < fixtureSize; ) {
		const size_t bytes = fixtureSize - at < 1024 ? fixtureSize - at : 1024;
		if (progress (input, 0, 0, 0, 0)) return CURLE_ABORTED_BY_CALLBACK;
		if (writeData ((char *) fixture + at, 1, bytes, input) != bytes)
			return CURLE_WRITE_ERROR;
		at += bytes;
	}
	return transferResult;
}

static ao_device *testAoOpen (int driver, ao_sample_format *format, ao_option *options) {
	(void) driver; (void) options;
	assert (format->bits == 16);
	return (ao_device *) (uintptr_t) 1;
}

static int testAoPlay (ao_device *device, char *bytes, uint_32 count) {
	assert (device == (ao_device *) (uintptr_t) 1);
	pthread_mutex_lock (&fixtureLock);
	for (uint_32 i = 0; i < count; i++) outputHash = (outputHash ^ (uint8_t) bytes[i]) * UINT64_C (1099511628211);
	outputBytes += count;
	outputCalls++;
	if (pauseAtOutput && outputCalls == 1) {
		pthread_mutex_lock (&outputPlayer->lock);
		outputPlayer->doPause = true;
		pthread_mutex_unlock (&outputPlayer->lock);
	}
	pthread_cond_broadcast (&fixtureCond);
	pthread_mutex_unlock (&fixtureLock);
	return 1;
}

/* Generate an AAC/ADTS fixture through the installed codec, not a mocked
 * decoder or a dependency on an external ffmpeg command. */
static void makeFixture (const char *muxer) {
	AVFormatContext *format = NULL;
	assert (avformat_alloc_output_context2 (&format, NULL, muxer, NULL) == 0);
	AVStream *stream = avformat_new_stream (format, NULL);
	const AVCodec *encoder = avcodec_find_encoder (AV_CODEC_ID_AAC);
	assert (stream != NULL && encoder != NULL);
	AVCodecContext *codec = avcodec_alloc_context3 (encoder);
	codec->sample_rate = 44100; codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	codec->bit_rate = 96000; codec->time_base = (AVRational) {1, 44100};
	if (format->oformat->flags & AVFMT_GLOBALHEADER) codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	av_channel_layout_default (&codec->ch_layout, 2);
	assert (avcodec_open2 (codec, encoder, NULL) == 0);
	stream->time_base = codec->time_base;
	assert (avcodec_parameters_from_context (stream->codecpar, codec) == 0);
	assert (avio_open_dyn_buf (&format->pb) == 0);
	assert (avformat_write_header (format, NULL) == 0);
	AVFrame *frame = av_frame_alloc ();
	frame->format = codec->sample_fmt; frame->sample_rate = codec->sample_rate;
	frame->nb_samples = codec->frame_size;
	assert (av_channel_layout_copy (&frame->ch_layout, &codec->ch_layout) == 0);
	assert (av_frame_get_buffer (frame, 0) == 0);
	AVPacket *packet = av_packet_alloc ();
	for (int n = 0; n <= 1600; n++) {
		if (n < 1600) {
			assert (av_frame_make_writable (frame) == 0);
			for (int ch = 0; ch < 2; ch++)
				for (int i = 0; i < frame->nb_samples; i++)
					((float *) frame->data[ch])[i] = 0.1f * sin (0.031 * (n * frame->nb_samples + i));
			frame->pts = (int64_t) n * frame->nb_samples;
		}
		assert (avcodec_send_frame (codec, n < 1600 ? frame : NULL) == 0);
		int ret;
		while ((ret = avcodec_receive_packet (codec, packet)) == 0) {
			av_packet_rescale_ts (packet, codec->time_base, stream->time_base);
			packet->stream_index = stream->index;
			assert (av_interleaved_write_frame (format, packet) == 0);
		}
		assert (ret == AVERROR (EAGAIN) || ret == AVERROR_EOF);
	}
	assert (av_write_trailer (format) == 0);
	const int size = avio_close_dyn_buf (format->pb, &fixture);
	assert (size > 0); fixtureSize = (size_t) size;
	format->pb = NULL;
	av_packet_free (&packet); av_frame_free (&frame);
	avcodec_free_context (&codec); avformat_free_context (format);
}

static int neverInterrupt (void *opaque) { (void) opaque; return 0; }
static void waitTransfer (void) {
	const uint64_t deadline = SbPlatformMonotonicMs () + 3000;
	for (;;) {
		pthread_mutex_lock (&fixtureLock);
		const bool entered = transferEntered;
		pthread_mutex_unlock (&fixtureLock);
		if (entered) return;
		assert (SbPlatformMonotonicMs () < deadline);
		SbPlatformSleepMs (1);
	}
}

static void waitInput (SbPlayerInput *input) {
	const uint64_t deadline = SbPlatformMonotonicMs () + 3000;
	SbPlayerInputSnapshot snapshot;
	do {
		SbPlayerInputGetSnapshot (input, &snapshot);
		if (snapshot.complete) return;
		assert (SbPlatformMonotonicMs () < deadline); SbPlatformSleepMs (1);
	} while (true);
}

static void *readInput (void *opaque) {
	uint8_t bytes[64];
	const int result = SbPlayerInputRead (opaque, bytes, sizeof (bytes));
	pthread_mutex_lock (&fixtureLock); readReturned = true;
	pthread_mutex_unlock (&fixtureLock);
	return (void *) (intptr_t) result;
}

static void testInputWaitAndBounds (void) {
	BarSettings_t settings = {0};
	holdTransfer = true; transferEntered = readReturned = false;
	transferResult = CURLE_OK;
	SbPlayerInput *input = SbPlayerInputOpen ("https://example.invalid/audio", &settings, neverInterrupt, NULL);
	assert (input != NULL); waitTransfer ();
	pthread_t reader;
	assert (pthread_create (&reader, NULL, readInput, input) == 0);
	/* Spurious wakeups/temporary emptiness cannot fabricate EOF. */
	for (unsigned i = 0; i < 20; i++) {
		pthread_mutex_lock (&input->lock); pthread_cond_broadcast (&input->cond);
		pthread_mutex_unlock (&input->lock); SbPlatformSleepMs (1);
	}
	pthread_mutex_lock (&fixtureLock); assert (!readReturned); holdTransfer = false;
	pthread_mutex_unlock (&fixtureLock);
	void *result;
	pthread_join (reader, &result); assert ((intptr_t) result > 0);
	waitInput (input);
	assert (SbPlayerInputSeek (input, 0, AVSEEK_SIZE) == (int64_t) fixtureSize);
	assert (SbPlayerInputSeek (input, 0, SEEK_SET) == 0);
	uint8_t bytes[4096]; size_t offset = 0; int ret;
	while ((ret = SbPlayerInputRead (input, bytes, sizeof (bytes))) > 0) {
		assert (!memcmp (bytes, fixture + offset, ret)); offset += ret;
	}
	assert (ret == AVERROR_EOF && offset == fixtureSize);
	assert (SbPlayerInputSeek (input, -16, SEEK_END) == (int64_t) fixtureSize - 16);
	assert (SbPlayerInputRead (input, bytes, 16) == 16);
	assert (!memcmp (bytes, fixture + fixtureSize - 16, 16));
	assert (SbPlayerInputSeek (input, -1, SEEK_SET) < 0);
	assert (SbPlayerInputSeek (input, INT64_MAX, SEEK_CUR) < 0);
	/* Bounds reject the next write instead of blocking or overwriting a ring. */
	pthread_mutex_lock (&input->lock); input->received = SB_PLAYER_INPUT_LIMIT;
	pthread_mutex_unlock (&input->lock);
	assert (writeData ("x", 1, 1, input) == 0);
	assert (SbPlayerInputRead (input, bytes, 1) == AVERROR (EFBIG));
	SbPlayerInputClose (input);

	holdTransfer = true; transferEntered = readReturned = false;
	input = SbPlayerInputOpen ("https://example.invalid/audio", &settings, neverInterrupt, NULL);
	waitTransfer (); assert (pthread_create (&reader, NULL, readInput, input) == 0);
	SbPlayerInputCancel (input);
	pthread_join (reader, &result); assert ((intptr_t) result == AVERROR_EXIT);
	SbPlayerInputClose (input);

	holdTransfer = false; transferResult = CURLE_PARTIAL_FILE;
	input = SbPlayerInputOpen ("https://example.invalid/audio", &settings, neverInterrupt, NULL);
	waitInput (input);
	assert (SbPlayerInputSeek (input, 0, AVSEEK_SIZE) == AVERROR (EIO));
	assert (SbPlayerInputRead (input, bytes, 1) == AVERROR (EIO));
	SbPlayerInputClose (input);
	transferResult = CURLE_OPERATION_TIMEDOUT;
	input = SbPlayerInputOpen ("https://example.invalid/audio", &settings, neverInterrupt, NULL);
	waitInput (input);
	assert (SbPlayerInputSeek (input, 0, AVSEEK_SIZE) == AVERROR (ETIMEDOUT));
	SbPlayerInputClose (input);
	transferResult = CURLE_OK;
}

#ifndef _WIN32
static int serverSocket;
static bool holdHttp, httpHalfSent, abortHttp;
static size_t httpBodySize;
static pthread_t serverThread;
static char serverUrl[100];

static void sendAll (int socket, const void *data, size_t size) {
	const uint8_t *bytes = data;
	while (size > 0) {
		const ssize_t sent = send (socket, bytes, size, 0);
		assert (sent > 0); bytes += sent; size -= sent;
	}
}

static void *serveHttp (void *opaque) {
	(void) opaque;
	const int client = accept (serverSocket, NULL, NULL);
	assert (client >= 0);
	char request[2048]; size_t used = 0;
	do {
		const ssize_t got = recv (client, request + used, sizeof (request) - used - 1, 0);
		assert (got > 0); used += got; request[used] = '\0';
	} while (strstr (request, "\r\n\r\n") == NULL);
	char header[160];
	const int length = snprintf (header, sizeof (header),
			"HTTP/1.1 200 OK\r\nContent-Type: audio/aac\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", fixtureSize);
	sendAll (client, header, length);
	const size_t half = httpBodySize / 2;
	sendAll (client, fixture, half);
	pthread_mutex_lock (&fixtureLock); httpHalfSent = true;
	while (holdHttp) pthread_cond_wait (&fixtureCond, &fixtureLock);
	const bool abort = abortHttp;
	pthread_mutex_unlock (&fixtureLock);
	if (!abort) sendAll (client, fixture + half, httpBodySize - half);
	close (client); return NULL;
}

static void startHttp (bool hold, size_t bytes) {
	serverSocket = socket (AF_INET, SOCK_STREAM, 0); assert (serverSocket >= 0);
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl (INADDR_LOOPBACK)};
	assert (bind (serverSocket, (struct sockaddr *) &address, sizeof (address)) == 0);
	socklen_t size = sizeof (address);
	assert (getsockname (serverSocket, (struct sockaddr *) &address, &size) == 0);
	assert (listen (serverSocket, 1) == 0);
	snprintf (serverUrl, sizeof (serverUrl), "http://127.0.0.1:%u/audio", ntohs (address.sin_port));
	holdHttp = hold; httpHalfSent = abortHttp = false; httpBodySize = bytes;
	realHttp = true; playerUrl = serverUrl;
	assert (pthread_create (&serverThread, NULL, serveHttp, NULL) == 0);
}

static void stopHttp (void) {
	pthread_join (serverThread, NULL); close (serverSocket);
	realHttp = false; playerUrl = "https://example.invalid/audio";
}
#endif

static void runPlayer (bool paused, SbPlayerStopReason stop, BarPlayerResult expected) {
	player_t player = {0}; BarSettings_t settings = {0};
	settings.bufferSecs = 1;
	BarPlayerInit (&player, &settings);
	player.url = (char *) playerUrl;
	outputPlayer = &player;
	pauseAtOutput = paused; outputCalls = 0; outputBytes = 0;
	outputHash = UINT64_C (1469598103934665603);
	holdTransfer = false; transferEntered = false; transferResult = CURLE_OK;
	pthread_t thread;
	assert (pthread_create (&thread, NULL, BarPlayerThread, &player) == 0);
	if (paused) {
		const uint64_t deadline = SbPlatformMonotonicMs () + 3000;
		for (;;) {
			pthread_mutex_lock (&player.lock); const bool isPaused = player.doPause;
			SbPlayerInputSnapshot input;
			SbPlayerInputGetSnapshot (player.input, &input);
			pthread_mutex_unlock (&player.lock);
#ifndef _WIN32
			if (isPaused && realHttp && !input.complete) {
				/* HTTP is still active while PCM is paused. Release the remainder,
				 * then require all compressed bytes to arrive before resuming. */
				pthread_mutex_lock (&fixtureLock);
				assert (httpHalfSent); holdHttp = false;
				pthread_cond_broadcast (&fixtureCond); pthread_mutex_unlock (&fixtureLock);
			}
#endif
			if (isPaused && input.complete) {
				assert (input.error == 0 && input.received == fixtureSize);
				break;
			}
			assert (SbPlatformMonotonicMs () < deadline); SbPlatformSleepMs (1);
		}
		SbPlatformSleepMs (25);
		pthread_mutex_lock (&fixtureLock); assert (outputCalls == 1);
		pthread_mutex_unlock (&fixtureLock);
		if (stop != SB_PLAYER_STOP_NONE) BarPlayerRequestStop (&player, stop);
		else {
			pthread_mutex_lock (&player.lock); player.doPause = false;
			pthread_cond_broadcast (&player.cond); pthread_mutex_unlock (&player.lock);
		}
	}
	const uint64_t deadline = SbPlatformMonotonicMs () + 3000;
	while (BarPlayerGetMode (&player) != PLAYER_FINISHED) {
		assert (SbPlatformMonotonicMs () < deadline); SbPlatformSleepMs (1);
	}
	void *result; pthread_join (thread, &result);
	assert ((uintptr_t) result == (uintptr_t) expected);
	assert (player.decoderError == (expected == PLAYER_RET_DECODEFAIL ? AVERROR_INVALIDDATA : 0));
	assert (player.stopReason == (stop == SB_PLAYER_STOP_NONE && expected == PLAYER_RET_OK ? SB_PLAYER_STOP_END_OF_TRACK : stop));
	assert (player.input == NULL && player.avio == NULL && player.fctx == NULL);
	BarPlayerDestroy (&player);
}

int main (void) {
	assert (curl_global_init (CURL_GLOBAL_DEFAULT) == CURLE_OK);
	av_log_set_level (AV_LOG_ERROR);
	makeFixture ("adts");
	testInputWaitAndBounds ();
	runPlayer (false, SB_PLAYER_STOP_NONE, PLAYER_RET_OK);
	const uint64_t bytes = outputBytes, hash = outputHash;
	assert (bytes > 500000);
	runPlayer (true, SB_PLAYER_STOP_NONE, PLAYER_RET_OK);
	assert (outputBytes == bytes && outputHash == hash); /* no missing/repeated PCM */
	runPlayer (true, SB_PLAYER_STOP_NEXT, PLAYER_RET_OK);
	runPlayer (true, SB_PLAYER_STOP_QUIT, PLAYER_RET_OK);
	/* Deliberately incomplete AAC, even with a nominally successful HTTP body,
	 * reaches the real decoder's invalid-data path rather than demux/flush EOF. */
	fixtureSize--;
	runPlayer (false, SB_PLAYER_STOP_NONE, PLAYER_RET_DECODEFAIL);
	fixtureSize++;
#ifndef _WIN32
	/* Hold the decoded consumer paused while real HTTP delivers the remainder.
	 * No FFmpeg read is needed for the compressed producer to finish. */
	startHttp (true, fixtureSize);
	BarSettings_t settings = {0}; player_t pausedPlayer = {0};
	BarPlayerInit (&pausedPlayer, &settings); pausedPlayer.doPause = true;
	SbPlayerInput *input = SbPlayerInputOpen (serverUrl, &settings, intCb, &pausedPlayer);
	assert (input != NULL);
	const uint64_t deadline = SbPlatformMonotonicMs () + 3000;
	SbPlayerInputSnapshot snapshot;
	do {
		SbPlayerInputGetSnapshot (input, &snapshot);
		assert (SbPlatformMonotonicMs () < deadline); SbPlatformSleepMs (1);
	} while (snapshot.received < fixtureSize / 2);
	assert (!snapshot.complete && snapshot.position == 0);
	pthread_mutex_lock (&fixtureLock); holdHttp = false;
	pthread_cond_broadcast (&fixtureCond); pthread_mutex_unlock (&fixtureLock);
	waitInput (input); SbPlayerInputGetSnapshot (input, &snapshot);
	assert (snapshot.complete && snapshot.error == 0 && snapshot.received == fixtureSize);
	assert (snapshot.position == 0 && pausedPlayer.doPause);
	SbPlayerInputClose (input); BarPlayerDestroy (&pausedPlayer); stopHttp ();
	/* Next/quit also wake a blocked AVIO read and abort an unfinished curl GET. */
	const SbPlayerStopReason stops[] = {SB_PLAYER_STOP_NEXT, SB_PLAYER_STOP_QUIT};
	for (size_t i = 0; i < sizeof (stops) / sizeof (*stops); i++) {
		startHttp (true, fixtureSize);
		memset (&pausedPlayer, 0, sizeof (pausedPlayer));
		BarPlayerInit (&pausedPlayer, &settings); pausedPlayer.doPause = true;
		input = SbPlayerInputOpen (serverUrl, &settings, intCb, &pausedPlayer);
		assert (input != NULL);
		pthread_mutex_lock (&pausedPlayer.lock); pausedPlayer.input = input;
		pthread_mutex_unlock (&pausedPlayer.lock);
		const uint64_t end = SbPlatformMonotonicMs () + 3000;
		do {
			SbPlayerInputGetSnapshot (input, &snapshot);
			assert (SbPlatformMonotonicMs () < end); SbPlatformSleepMs (1);
		} while (snapshot.received < fixtureSize / 2);
		assert (SbPlayerInputSeek (input, fixtureSize, SEEK_SET) == (int64_t) fixtureSize);
		pthread_t reader; assert (pthread_create (&reader, NULL, readInput, input) == 0);
		BarPlayerRequestStop (&pausedPlayer, stops[i]);
		void *result; pthread_join (reader, &result);
		assert ((intptr_t) result == AVERROR_EXIT && !pausedPlayer.doPause);
		waitInput (input);
		pthread_mutex_lock (&fixtureLock); holdHttp = false; abortHttp = true;
		pthread_cond_broadcast (&fixtureCond); pthread_mutex_unlock (&fixtureLock);
		pthread_mutex_lock (&pausedPlayer.lock); pausedPlayer.input = NULL;
		pthread_mutex_unlock (&pausedPlayer.lock);
		SbPlayerInputClose (input); BarPlayerDestroy (&pausedPlayer); stopHttp ();
	}
	/* AAC probing may seek the tail before playback, so let that legitimate
	 * input read complete. The compressed spool remains intact across pause. */
	startHttp (false, fixtureSize);
	runPlayer (true, SB_PLAYER_STOP_NONE, PLAYER_RET_OK);
	assert (outputBytes == bytes && outputHash == hash);
	stopHttp ();
	/* Real libcurl detects an advertised body ending early; AVIO exposes EIO. */
	startHttp (false, fixtureSize - 1);
	input = SbPlayerInputOpen (serverUrl, &settings, neverInterrupt, NULL);
	assert (input != NULL); waitInput (input);
	uint8_t byte;
	assert (SbPlayerInputRead (input, &byte, 1) == AVERROR (EIO));
	SbPlayerInputClose (input); stopHttp ();
#endif
	av_free (fixture);
	/* MP4/AAC requires size queries and backward/forward seeks through the spool. */
	makeFixture ("mp4");
	runPlayer (false, SB_PLAYER_STOP_NONE, PLAYER_RET_OK);
	const uint64_t mp4Bytes = outputBytes, mp4Hash = outputHash;
	runPlayer (true, SB_PLAYER_STOP_NONE, PLAYER_RET_OK);
	assert (outputBytes == mp4Bytes && outputHash == mp4Hash);
#ifndef _WIN32
	startHttp (false, fixtureSize);
	runPlayer (true, SB_PLAYER_STOP_NONE, PLAYER_RET_OK);
	assert (outputBytes == mp4Bytes && outputHash == mp4Hash); stopHttp ();
#endif
	av_free (fixture); curl_global_cleanup ();
	puts ("player pause tests passed");
	return 0;
}
