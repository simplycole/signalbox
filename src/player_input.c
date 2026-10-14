#include "player_input.h"
#include "config.h"
#include "debug.h"
#ifdef _WIN32
#include "platform.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <curl/curl.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>

struct SbPlayerInput {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	pthread_t thread;
	FILE *file;
	const char *url;
	const BarSettings_t *settings;
	int (*interrupt) (void *);
	void *opaque;
	uint64_t received, position;
	int64_t expected;
	bool complete, cancelled;
	int error;
};

static size_t headerData (char *data, size_t size, size_t count, void *opaque) {
	SbPlayerInput *input = opaque;
	if (size != 0 && count > SIZE_MAX / size) return 0;
	const size_t bytes = size * count;
	pthread_mutex_lock (&input->lock);
	if (bytes >= 5 && memcmp (data, "HTTP/", 5) == 0) input->expected = -1;
	const char key[] = "content-length:";
	bool lengthHeader = bytes > sizeof (key) - 1;
	for (size_t i = 0; lengthHeader && i < sizeof (key) - 1; i++)
		if (tolower ((unsigned char) data[i]) != key[i]) lengthHeader = false;
	if (lengthHeader) {
		size_t i = sizeof (key) - 1;
		while (i < bytes && (data[i] == ' ' || data[i] == '\t')) i++;
		uint64_t length = 0; bool valid = i < bytes && isdigit ((unsigned char) data[i]);
		for (; i < bytes && isdigit ((unsigned char) data[i]); i++) {
			const unsigned digit = data[i] - '0';
			if (length > SB_PLAYER_INPUT_LIMIT / 10 || length * 10 + digit > SB_PLAYER_INPUT_LIMIT) {
				input->error = AVERROR (EFBIG); valid = false; break;
			}
			length = length * 10 + digit;
		}
		if (valid) input->expected = (int64_t) length;
	}
	const bool failed = input->error != 0;
	pthread_mutex_unlock (&input->lock);
	return failed ? 0 : bytes;
}

static size_t writeData (char *data, size_t size, size_t count, void *opaque) {
	SbPlayerInput *input = opaque;
	if (size != 0 && count > SIZE_MAX / size) return 0;
	const size_t bytes = size * count;
	pthread_mutex_lock (&input->lock);
	if (input->cancelled || bytes > SB_PLAYER_INPUT_LIMIT - input->received) {
		if (!input->cancelled) input->error = AVERROR (EFBIG);
		pthread_mutex_unlock (&input->lock);
		return 0;
	}
	/* One mutex serializes the file cursor; producer and consumer have separate
	 * logical offsets. Publish only successfully written bytes, never overwrite. */
	if (fseek (input->file, (long) input->received, SEEK_SET) != 0 ||
			fwrite (data, 1, bytes, input->file) != bytes) {
		input->error = AVERROR (EIO);
		pthread_mutex_unlock (&input->lock);
		return 0;
	}
	input->received += bytes;
	pthread_cond_broadcast (&input->cond);
	pthread_mutex_unlock (&input->lock);
	return bytes;
}

static int progress (void *opaque, curl_off_t total, curl_off_t now,
		curl_off_t uploadTotal, curl_off_t uploadNow) {
	(void) total; (void) now; (void) uploadTotal; (void) uploadNow;
	SbPlayerInput *input = opaque;
	if (input->interrupt (input->opaque)) SbPlayerInputCancel (input);
	pthread_mutex_lock (&input->lock);
	const bool cancel = input->cancelled;
	pthread_mutex_unlock (&input->lock);
	return cancel;
}

static void *download (void *opaque) {
	SbPlayerInput *input = opaque;
	CURL *http = curl_easy_init ();
	CURLcode result = CURLE_FAILED_INIT;
	long status = 0;
#ifdef _WIN32
	char *bundledCa = NULL;
#endif
	if (http != NULL) {
		/* A read-idle timeout, not a total transfer deadline. Pausing the player
		 * never pauses curl, so it cannot trigger this timeout through backpressure. */
		const long timeout = input->settings->timeout > 0 ? input->settings->timeout : 30;
#define option(key, value) do { result = curl_easy_setopt (http, key, value); \
		if (result != CURLE_OK) goto done; } while (0)
		option (CURLOPT_URL, input->url);
		option (CURLOPT_USERAGENT, PACKAGE "-" VERSION);
		option (CURLOPT_PROTOCOLS_STR, "http,https");
		option (CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
		option (CURLOPT_FOLLOWLOCATION, 1L);
		option (CURLOPT_MAXREDIRS, 5L);
		option (CURLOPT_FAILONERROR, 1L);
		option (CURLOPT_NOSIGNAL, 1L);
		option (CURLOPT_CONNECTTIMEOUT, timeout);
		option (CURLOPT_LOW_SPEED_LIMIT, 1L);
		option (CURLOPT_LOW_SPEED_TIME, timeout);
		option (CURLOPT_MAXFILESIZE_LARGE, (curl_off_t) SB_PLAYER_INPUT_LIMIT);
		option (CURLOPT_WRITEFUNCTION, writeData);
		option (CURLOPT_WRITEDATA, input);
		option (CURLOPT_HEADERFUNCTION, headerData);
		option (CURLOPT_HEADERDATA, input);
		option (CURLOPT_XFERINFOFUNCTION, progress);
		option (CURLOPT_XFERINFODATA, input);
		option (CURLOPT_NOPROGRESS, 0L);
		if (input->settings->proxy != NULL && input->settings->proxy[0] != '\0')
			option (CURLOPT_PROXY, input->settings->proxy);
		if (input->settings->bindTo != NULL) option (CURLOPT_INTERFACE, input->settings->bindTo);
		if (input->settings->caBundle != NULL) option (CURLOPT_CAINFO, input->settings->caBundle);
#ifdef _WIN32
		else if ((bundledCa = SbPlatformFindExecutableSibling ("cert.pem")) != NULL)
			option (CURLOPT_CAINFO, bundledCa);
#endif
		result = curl_easy_perform (http);
done:
		curl_easy_getinfo (http, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup (http);
#ifdef _WIN32
		free (bundledCa);
#endif
#undef option
	}
	pthread_mutex_lock (&input->lock);
	if (input->cancelled) input->error = AVERROR_EXIT;
	else if (input->error == 0 && result != CURLE_OK)
		input->error = result == CURLE_OPERATION_TIMEDOUT ? AVERROR (ETIMEDOUT) : AVERROR (EIO);
	input->complete = true;
	tuiDebugPrint ("player input %s source=http curl_error=%d http_status=%ld bytes=%llu ffmpeg_error=%d\n",
			input->error == 0 ? "eof" : "error", (int) result, status,
			(unsigned long long) input->received, input->error);
	pthread_cond_broadcast (&input->cond);
	pthread_mutex_unlock (&input->lock);
	return NULL;
}

SbPlayerInput *SbPlayerInputOpen (const char *url, const BarSettings_t *settings,
		int (*interrupt) (void *), void *opaque) {
	SbPlayerInput *input = calloc (1, sizeof (*input));
	if (input == NULL) return NULL;
	input->file = tmpfile ();
	if (input->file == NULL) { free (input); return NULL; }
	if (pthread_mutex_init (&input->lock, NULL) != 0) {
		fclose (input->file); free (input); return NULL;
	}
	if (pthread_cond_init (&input->cond, NULL) != 0) {
		pthread_mutex_destroy (&input->lock); fclose (input->file); free (input); return NULL;
	}
	input->url = url; input->settings = settings; input->expected = -1;
	input->interrupt = interrupt; input->opaque = opaque;
	if (pthread_create (&input->thread, NULL, download, input) != 0) {
		pthread_cond_destroy (&input->cond); pthread_mutex_destroy (&input->lock);
		fclose (input->file); free (input); return NULL;
	}
	return input;
}

void SbPlayerInputCancel (SbPlayerInput *input) {
	if (input == NULL) return;
	pthread_mutex_lock (&input->lock);
	input->cancelled = true;
	pthread_cond_broadcast (&input->cond);
	pthread_mutex_unlock (&input->lock);
}

void SbPlayerInputClose (SbPlayerInput *input) {
	if (input == NULL) return;
	SbPlayerInputCancel (input);
	pthread_join (input->thread, NULL);
	fclose (input->file);
	pthread_cond_destroy (&input->cond);
	pthread_mutex_destroy (&input->lock);
	free (input);
}

void SbPlayerInputGetSnapshot (SbPlayerInput *input, SbPlayerInputSnapshot *snapshot) {
	*snapshot = (SbPlayerInputSnapshot) {0};
	if (input == NULL) return;
	pthread_mutex_lock (&input->lock);
	snapshot->received = input->received; snapshot->position = input->position;
	snapshot->complete = input->complete; snapshot->error = input->error;
	pthread_mutex_unlock (&input->lock);
}

int SbPlayerInputRead (void *opaque, uint8_t *data, int size) {
	SbPlayerInput *input = opaque;
	if (size <= 0) return AVERROR (EINVAL);
	pthread_mutex_lock (&input->lock);
	/* Temporary emptiness is a condition wait, including during pause. Only
	 * successful HTTP completion becomes EOF; transport errors remain errors. */
	while (input->position >= input->received && !input->complete && !input->cancelled)
		pthread_cond_wait (&input->cond, &input->lock);
	int ret;
	if (input->cancelled) ret = AVERROR_EXIT;
	else if (input->error != 0) ret = input->error;
	else if (input->position >= input->received) {
		ret = AVERROR_EOF;
		tuiDebugPrint ("player input eof source=avio bytes=%llu\n",
				(unsigned long long) input->received);
	} else {
		const uint64_t available = input->received - input->position;
		const int bytes = available < (uint64_t) size ? (int) available : size;
		if (fseek (input->file, (long) input->position, SEEK_SET) != 0 ||
				fread (data, 1, bytes, input->file) != (size_t) bytes) ret = AVERROR (EIO);
		else { input->position += bytes; ret = bytes; }
	}
	pthread_mutex_unlock (&input->lock);
	return ret;
}

int64_t SbPlayerInputSeek (void *opaque, int64_t offset, int whence) {
	SbPlayerInput *input = opaque;
	pthread_mutex_lock (&input->lock);
	whence &= ~AVSEEK_FORCE;
	/* Advertise a real Content-Length or final size, never the temporarily
	 * buffered length. A later read can wait for a seek target ahead of download. */
	const int64_t length = input->complete ? (int64_t) input->received : input->expected;
	int64_t ret = AVERROR (EINVAL);
	if (input->cancelled) ret = AVERROR_EXIT;
	else if (input->error != 0) ret = input->error;
	else if ((whence == AVSEEK_SIZE || whence == SEEK_END) && length < 0) ret = AVERROR (ENOSYS);
	else if (whence == AVSEEK_SIZE) ret = length;
	else {
		int64_t base = 0;
		if (whence == SEEK_CUR) base = (int64_t) input->position;
		else if (whence == SEEK_END) base = length;
		if ((whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END) &&
				offset >= -base && offset <= (int64_t) SB_PLAYER_INPUT_LIMIT - base) {
			input->position = (uint64_t) (base + offset);
			ret = (int64_t) input->position;
		}
	}
	pthread_mutex_unlock (&input->lock);
	return ret;
}
