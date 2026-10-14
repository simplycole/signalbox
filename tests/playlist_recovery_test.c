/* Exercise production transition/poll/cleanup and UI actions with deterministic
 * playlist and player workers. No authentication, network, audio, or long wait. */
#include "../src/config.h"
/* The application objects may have been built with NDEBUG. */
#undef HAVE_DEBUGLOG
#define main SignalboxProgramMain
#define BarUiPianoCallQuiet TestPlaylistRequest
#define BarPlayerThread TestPlayerThread
#include "../src/main.c"
#undef main
#undef BarUiPianoCallQuiet
#undef BarPlayerThread
extern void *BarPlayerThread (void *);

/* Exercise the real background HTTP retry loop without opening a socket. */
static CURLcode TestCurlPerform (CURL *http);
#define curl_easy_perform TestCurlPerform
#include "../src/ui.c"
#undef curl_easy_perform

static unsigned httpAttempts, httpSucceedAt;
static CURLcode httpError;
static BarApp_t *httpCancel;

static CURLcode TestCurlPerform (CURL *http) {
	assert (http != NULL);
	httpAttempts++;
	if (httpCancel != NULL) {
		pthread_mutex_lock (&httpCancel->prefetch.lock);
		httpCancel->prefetch.cancel = true;
		pthread_mutex_unlock (&httpCancel->prefetch.lock);
	}
	return httpSucceedAt != 0 && httpAttempts == httpSucceedAt ? CURLE_OK : httpError;
}

static pthread_mutex_t testLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t testCond = PTHREAD_COND_INITIALIZER;
static bool holdRequest, requestEntered, failRequest;
static unsigned requests, starts, activeRequests, maxConcurrentRequests;
static BarPlayerResult playerResult;
static bool holdPlayer;
static PianoStation_t stationA = {.id = "station-a", .name = "A"};
static PianoStation_t stationB = {.id = "station-b", .name = "B"};

static PianoSong_t *testSong (const char *token, uint64_t fetched) {
	PianoSong_t *song = calloc (1, sizeof (*song));
	assert (song);
	song->trackToken = strdup (token);
	song->artist = strdup ("Artist"); song->title = strdup ("Track");
	song->album = strdup ("Album"); song->length = 180;
	song->audioUrl = strdup ("https://example.invalid/audio");
	song->audioUrlFetchedAtMs = fetched;
	return song;
}

bool TestPlaylistRequest (BarApp_t *app, CURL *http, PianoRequestType_t type,
		void *data, PianoReturn_t *pRet, CURLcode *wRet) {
	(void) http; assert (type == PIANO_REQUEST_GET_PLAYLIST);
	pthread_mutex_lock (&testLock);
	requests++; activeRequests++;
	if (activeRequests > maxConcurrentRequests) maxConcurrentRequests = activeRequests;
	requestEntered = true; pthread_cond_broadcast (&testCond);
	while (holdRequest) pthread_cond_wait (&testCond, &testLock);
	const bool fail = failRequest;
	pthread_mutex_unlock (&testLock);
	pthread_mutex_lock (&app->prefetch.lock);
	const bool cancel = app->prefetch.cancel;
	pthread_mutex_unlock (&app->prefetch.lock);
	*pRet = fail ? PIANO_RET_P_INTERNAL : PIANO_RET_OK;
	*wRet = cancel ? CURLE_ABORTED_BY_CALLBACK : CURLE_OK;
	PianoRequestDataGetPlaylist_t *req = data;
	if (!fail && !cancel) req->retPlaylist = testSong (req->station->id, 0);
	pthread_mutex_lock (&testLock);
	activeRequests--;
	pthread_mutex_unlock (&testLock);
	return !fail && !cancel;
}

void *TestPlayerThread (void *data) {
	player_t *p = data;
	pthread_mutex_lock (&testLock);
	starts++; pthread_cond_broadcast (&testCond);
	const BarPlayerResult result = playerResult;
	const bool hold = holdPlayer;
	pthread_mutex_unlock (&testLock);
	pthread_mutex_lock (&p->lock);
	if (hold) {
		p->mode = PLAYER_PLAYING;
		while (!p->doQuit) pthread_cond_wait (&p->cond, &p->lock);
	}
	p->mode = PLAYER_FINISHED;
	pthread_mutex_unlock (&p->lock);
	return (void *) (uintptr_t) result;
}

static void initApp (BarApp_t *app) {
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	app->settings.npSongFormat = strdup ("%t by %a");
	app->settings.npStationFormat = strdup ("%n");
	app->settings.timeFormat = strdup ("%t");
	app->settings.audioQuality = PIANO_AQ_HIGH;
	app->settings.history = 5;
	BarPlayerInit (&app->player, &app->settings);
	pthread_mutex_init (&app->prefetch.lock, NULL);
	pthread_mutex_init (&app->pianoLock, NULL);
	SbUiModelInit (&app->uiModel);
	SbUiRendererInitClassic (&app->uiRenderer, &app->settings);
	SbMetadataResolverInit (&app->metadataResolver);
	SbLyricsResultInit (&app->lyrics);
	app->curStation = app->nextStation = &stationA;
	app->prefetch.lastAttemptRemaining = SIZE_MAX;
	interrupted = &app->doQuit;
	requests = starts = activeRequests = maxConcurrentRequests = 0; failRequest = holdRequest = requestEntered = false;
	playerResult = PLAYER_RET_OK; holdPlayer = true;
}

static void destroyApp (BarApp_t *app, pthread_t *playerThread) {
	if (BarPlayerGetMode (&app->player) != PLAYER_DEAD) {
		BarPlayerRequestStop (&app->player, SB_PLAYER_STOP_QUIT);
		BarMainPlayerCleanup (app, playerThread);
	}
	assert (!app->prefetch.inFlight);
	assert (activeRequests == 0 && maxConcurrentRequests <= 1);
	SbUiModelSetSong (&app->uiModel, NULL, NULL);
	PianoDestroyPlaylist (app->playlist);
	PianoDestroyPlaylist (app->songHistory);
	SbUiModelDestroy (&app->uiModel);
	SbLyricsResultDestroy (&app->lyrics);
	SbMetadataResolverDestroy (&app->metadataResolver);
	BarPlayerDestroy (&app->player);
	BarSettingsDestroy (&app->settings);
	pthread_mutex_destroy (&app->prefetch.lock);
	pthread_mutex_destroy (&app->pianoLock);
}

static void testTransportRetry (void) {
	static BarApp_t app; pthread_t thread;
	initApp (&app);
	app.settings.rpcHost = strdup ("example.invalid");
	app.settings.rpcTlsPort = strdup ("443");
	app.settings.maxRetry = 3;
	CURL *http = curl_easy_init ();
	assert (http != NULL);
	PianoRequest_t req = {.urlPath = "/test", .postData = "{}", .secure = true};
	httpCancel = NULL;
	httpError = CURLE_COULDNT_RESOLVE_HOST;
	httpAttempts = 0; httpSucceedAt = 3;
	assert (BarPianoHttpRequest (http, &app.settings, &req, &app) == CURLE_OK);
	assert (httpAttempts == 3 && interrupted == &app.doQuit);
	httpAttempts = 0; httpSucceedAt = 0;
	assert (BarPianoHttpRequest (http, &app.settings, &req, &app) == httpError);
	assert (httpAttempts == 3);
	httpAttempts = 0; httpError = CURLE_UNSUPPORTED_PROTOCOL;
	assert (BarPianoHttpRequest (http, &app.settings, &req, &app) == httpError);
	assert (httpAttempts == 1);
	httpAttempts = 0; httpError = CURLE_COULDNT_RESOLVE_HOST; httpCancel = &app;
	assert (BarPianoHttpRequest (http, &app.settings, &req, &app) == httpError);
	assert (httpAttempts == 1 && playlistProgressCb (&app, 0, 0, 0, 0));
	httpCancel = NULL;
	curl_easy_cleanup (http);
	destroyApp (&app, &thread);
}

/* Wait only for a mocked worker's completion, with a short hard deadline. */
static void pollRequest (BarApp_t *app) {
	const uint64_t deadline = SbPlatformMonotonicMs () + 2000;
	while (app->prefetch.inFlight) {
		assert (SbPlatformMonotonicMs () < deadline);
		BarMainPollPrefetch (app);
		SbPlatformSleepMs (1);
	}
}

static void waitMode (BarApp_t *app, BarPlayerMode mode) {
	const uint64_t deadline = SbPlatformMonotonicMs () + 2000;
	while (BarPlayerGetMode (&app->player) != mode) {
		assert (SbPlatformMonotonicMs () < deadline);
		SbPlatformSleepMs (1);
	}
}

static void callAction (BarApp_t *app, SbUiCommand cmd) {
	assert (BarUiDispatchCommand (app, cmd, app->curStation, app->playlist,
			false, BAR_DC_GLOBAL));
}

static void waitRequest (void) {
	const uint64_t deadline = SbPlatformMonotonicMs () + 2000;
	for (;;) {
		pthread_mutex_lock (&testLock);
		const bool entered = requestEntered;
		pthread_mutex_unlock (&testLock);
		if (entered) return;
		assert (SbPlatformMonotonicMs () < deadline);
		SbPlatformSleepMs (1);
	}
}

static void releaseRequest (void) {
	pthread_mutex_lock (&testLock);
	holdRequest = false;
	pthread_cond_broadcast (&testCond);
	pthread_mutex_unlock (&testLock);
}

static void longResume (BarApp_t *app) {
	callAction (app, SB_UI_CMD_PAUSE);
	pthread_mutex_lock (&app->player.lock);
	app->player.pausedAtMs -= 48936474;
	pthread_mutex_unlock (&app->player.lock);
	callAction (app, SB_UI_CMD_PLAY);
}

static void finishCurrent (BarApp_t *app, pthread_t *thread, BarPlayerResult result) {
	BarPlayerRequestStop (&app->player, result == PLAYER_RET_OK ?
			SB_PLAYER_STOP_END_OF_TRACK : SB_PLAYER_STOP_NONE);
	waitMode (app, PLAYER_FINISHED);
	BarMainPlayerCleanup (app, thread);
}

/* Test both orderings for every suspended-player result, including an empty
 * future queue. The fresh player succeeds independently of the old result. */
static void longPauseTransition (BarPlayerResult result, bool refreshFirst,
		bool emptyFuture) {
	static BarApp_t app; pthread_t thread;
	initApp (&app);
	app.playlist = testSong ("suspended", SbPlatformRealtimeMs ());
	if (!emptyFuture) app.playlist->head.next =
			&testSong ("stale-future", SbPlatformRealtimeMs () - 48936474)->head;
	playerResult = result;
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	BarMainPrintTime (&app);
	const PianoSong_t *current = app.playlist;
	holdRequest = true;
	longResume (&app);
	assert (app.prefetch.inFlight && app.prefetch.required);
	assert (!strcmp (app.prefetch.reason, "long_pause"));
	assert (app.longPause.state == SB_LONG_PAUSE_REFRESH_PENDING);
	assert (app.longPause.currentPlayer && app.recoveryPending && app.recoveryAttempted);
	assert (app.playlist == current && SbPlaylistCount (app.playlist) == 1);
	assert (app.uiModel.song == current && app.uiModel.playback == SB_UI_PLAYBACK_PLAYING);
	assert (!app.player.doQuit && BarPlayerGetMode (&app.player) == PLAYER_PLAYING);
	waitRequest (); assert (requests == 1);
	const uint64_t generation = app.playlistGeneration;
	for (unsigned int i = 0; i < 10; i++) {
		BarMainBeginLongPauseRecovery (&app, 48936474);
		BarMainMaybePrefetch (&app);
		BarMainAdvancePlayback (&app, &thread);
		BarMainObservePlayback (&app);
	}
	assert (app.playlistGeneration == generation && requests == 1);
	assert (app.longPause.state == SB_LONG_PAUSE_REFRESH_PENDING && app.recoveryPending);
	if (refreshFirst) {
		releaseRequest (); pollRequest (&app);
		assert (app.longPause.state == SB_LONG_PAUSE_REFRESH_READY);
		assert (app.playlist == current && SbPlaylistCount (app.playlist) == 2);
		for (unsigned int i = 0; i < 10; i++) {
			BarMainMaybePrefetch (&app); BarMainObservePlayback (&app);
		}
		assert (requests == 1 && app.recoveryPending);
	}
	finishCurrent (&app, &thread, result);
	assert (!app.playbackBlocked && app.uiModel.song == NULL);
	assert (app.uiModel.playback == SB_UI_PLAYBACK_STOPPED);
	assert (!app.longPause.currentPlayer && app.recoveryPending && app.recoveryAttempted);
	if (!refreshFirst) {
		assert (app.playlist == NULL && app.prefetch.inFlight);
		for (unsigned int i = 0; i < 10; i++) {
			BarMainMaybePrefetch (&app); BarMainAdvancePlayback (&app, &thread);
		}
		assert (requests == 1 && app.uiModel.activity == SB_UI_ACTIVITY_WAITING_PLAYLIST);
		/* Next while awaiting the same refill must not reset its retry budget. */
		callAction (&app, SB_UI_CMD_SKIP);
		assert (app.recoveryAttempted && app.recoveryPending);
		releaseRequest (); pollRequest (&app);
	}
	assert (app.longPause.state == SB_LONG_PAUSE_REFRESH_READY);
	playerResult = PLAYER_RET_OK;
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	assert (app.longPause.state == SB_LONG_PAUSE_STARTING_FRESH && app.recoveryPending);
	BarMainObservePlayback (&app); BarMainPrintTime (&app);
	assert (app.longPause.state == SB_LONG_PAUSE_NONE && !app.recoveryPending);
	assert (app.uiModel.playback == SB_UI_PLAYBACK_PLAYING);
	assert (requests == 1 && starts == 2 && maxConcurrentRequests == 1);
	destroyApp (&app, &thread);
}

static void testLongPauseFailures (void) {
	static BarApp_t app; pthread_t thread;
	/* Decode failure on the fresh retry is not another suspend-context retry. */
	initApp (&app);
	app.playlist = testSong ("old", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	longResume (&app); pollRequest (&app);
	finishCurrent (&app, &thread, PLAYER_RET_OK);
	playerResult = PLAYER_RET_DECODEFAIL; holdPlayer = false;
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_FINISHED);
	BarMainPlayerCleanup (&app, &thread);
	assert (app.playbackBlocked && app.longPause.state == SB_LONG_PAUSE_NONE);
	assert (app.uiModel.activity == SB_UI_ACTIVITY_AUDIO_UNAVAILABLE);
	for (unsigned int i = 0; i < 10; i++) {
		BarMainMaybePrefetch (&app); BarMainAdvancePlayback (&app, &thread);
	}
	assert (requests == 1 && starts == 2);
	destroyApp (&app, &thread);

	/* Failure of a required refill is final even after the suspended decoder
	 * has already exited; no independent stream_failure request may follow. */
	initApp (&app); playerResult = PLAYER_RET_DECODEFAIL;
	app.playlist = testSong ("old", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	holdRequest = failRequest = true;
	longResume (&app); waitRequest ();
	finishCurrent (&app, &thread, PLAYER_RET_DECODEFAIL);
	releaseRequest (); pollRequest (&app);
	assert (app.playbackBlocked && app.longPause.state == SB_LONG_PAUSE_NONE);
	assert (app.uiModel.song == NULL && app.uiModel.playback == SB_UI_PLAYBACK_STOPPED);
	assert (app.uiModel.activity == SB_UI_ACTIVITY_AUDIO_UNAVAILABLE);
	for (unsigned int i = 0; i < 10; i++) {
		BarMainMaybePrefetch (&app); BarMainAdvancePlayback (&app, &thread);
	}
	assert (requests == 1);
	callAction (&app, SB_UI_CMD_QUIT); assert (app.doQuit);
	destroyApp (&app, &thread);

	/* A failed future refill does not interrupt the healthy open track. */
	initApp (&app);
	app.playlist = testSong ("healthy", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	BarMainPrintTime (&app);
	failRequest = true;
	longResume (&app); pollRequest (&app);
	assert (app.playbackBlocked && !app.player.doQuit);
	assert (BarPlayerGetMode (&app.player) == PLAYER_PLAYING);
	assert (app.uiModel.song == app.playlist && app.uiModel.playback == SB_UI_PLAYBACK_PLAYING);
	finishCurrent (&app, &thread, PLAYER_RET_OK);
	BarMainMaybePrefetch (&app); BarMainAdvancePlayback (&app, &thread);
	assert (requests == 1 && app.uiModel.playback == SB_UI_PLAYBACK_STOPPED);
	assert (app.uiModel.activity == SB_UI_ACTIVITY_AUDIO_UNAVAILABLE);
	destroyApp (&app, &thread);

	/* Ordinary decode failure retains the normal terminal failure behavior. */
	initApp (&app); playerResult = PLAYER_RET_DECODEFAIL; holdPlayer = false;
	app.playlist = testSong ("decode-error", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_FINISHED);
	BarMainPlayerCleanup (&app, &thread); BarMainAdvancePlayback (&app, &thread);
	assert (app.playbackBlocked && requests == 0);
	destroyApp (&app, &thread);
}

static void testLongPauseCancellation (void) {
	static BarApp_t app; pthread_t thread;
	/* Old in-flight prefetch is cancelled before the one owned refill starts. */
	initApp (&app);
	app.playlist = testSong ("old", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	holdRequest = true;
	BarMainStartPlaylistRequest (&app, false, "prefetch"); waitRequest ();
	longResume (&app);
	assert (app.longPause.state == SB_LONG_PAUSE_REFRESH_NEEDED);
	for (unsigned int i = 0; i < 10; i++) BarMainMaybePrefetch (&app);
	assert (requests == 1 && !app.player.doQuit);
	releaseRequest (); pollRequest (&app);
	assert (app.longPause.state == SB_LONG_PAUSE_REFRESH_NEEDED);
	BarMainMaybePrefetch (&app); pollRequest (&app);
	assert (requests == 2 && app.longPause.state == SB_LONG_PAUSE_REFRESH_READY);
	finishCurrent (&app, &thread, PLAYER_RET_OK);
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	BarMainObservePlayback (&app);
	assert (maxConcurrentRequests == 1);
	destroyApp (&app, &thread);

	/* Station change cancels the recovery owner; its late publication loses. */
	initApp (&app);
	app.playlist = testSong ("old", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	holdRequest = true;
	longResume (&app); waitRequest ();
	assert (BarUiDispatchCommand (&app, SB_UI_CMD_ACTIVATE_STATION, &stationB,
			app.playlist, false, BAR_DC_GLOBAL));
	assert (app.longPause.state == SB_LONG_PAUSE_NONE && app.nextStation == &stationB);
	releaseRequest (); pollRequest (&app);
	assert (SbPlaylistCount (app.playlist) == 1 && !app.playbackBlocked);
	waitMode (&app, PLAYER_FINISHED); BarMainPlayerCleanup (&app, &thread);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (requests == 2 && !strcmp (app.playlist->trackToken, stationB.id));
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	destroyApp (&app, &thread);
}

static void testShortPauseDecodeRecovery (bool queued) {
	static BarApp_t app; pthread_t thread;
	initApp (&app);
	app.playlist = testSong ("resumed", SbPlatformRealtimeMs ());
	if (queued) app.playlist->head.next = &testSong ("healthy-next", SbPlatformRealtimeMs ())->head;
	playerResult = PLAYER_RET_DECODEFAIL;
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	callAction (&app, SB_UI_CMD_PAUSE); callAction (&app, SB_UI_CMD_PLAY);
	assert (app.player.resumedAfterPause && !app.prefetch.inFlight && requests == 0);
	app.player.decoderError = AVERROR_INVALIDDATA;
	finishCurrent (&app, &thread, PLAYER_RET_DECODEFAIL);
	assert (!app.playbackBlocked && app.recoveryAttempted && app.recoveryPending);
	playerResult = PLAYER_RET_OK;
	BarMainAdvancePlayback (&app, &thread);
	if (!queued) { pollRequest (&app); BarMainAdvancePlayback (&app, &thread); }
	waitMode (&app, PLAYER_PLAYING);
	assert (!app.player.resumedAfterPause);
	assert (requests == (queued ? 0U : 1U) && starts == 2);
	assert (!strcmp (app.playlist->trackToken, queued ? "healthy-next" : stationA.id));
	BarMainObservePlayback (&app); BarMainPrintTime (&app);
	assert (!app.recoveryPending && app.uiModel.playback == SB_UI_PLAYBACK_PLAYING);
	destroyApp (&app, &thread);

	/* The fresh fallback fails before PLAYING: no second automatic attempt. */
	initApp (&app); playerResult = PLAYER_RET_DECODEFAIL;
	app.playlist = testSong ("resumed", SbPlatformRealtimeMs ());
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	callAction (&app, SB_UI_CMD_PAUSE); callAction (&app, SB_UI_CMD_PLAY);
	app.player.decoderError = AVERROR_INVALIDDATA;
	finishCurrent (&app, &thread, PLAYER_RET_DECODEFAIL);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	holdPlayer = false; playerResult = PLAYER_RET_SOFTFAIL;
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_FINISHED);
	BarMainPlayerCleanup (&app, &thread);
	for (unsigned int i = 0; i < 10; i++) {
		BarMainMaybePrefetch (&app); BarMainAdvancePlayback (&app, &thread);
	}
	assert (requests == 1 && app.playbackBlocked && !app.recoveryPending);
	assert (app.uiModel.song == NULL && app.uiModel.activity == SB_UI_ACTIVITY_AUDIO_UNAVAILABLE);
	destroyApp (&app, &thread);
}

int main (void) {
	const uint64_t now = UINT64_C (100000000);
	PianoSong_t *entry = testSong ("age", now);
	assert (!SbPlaybackUrlStale (entry, now + 1000));
	assert (!SbPlaybackUrlStale (entry, now + SB_PLAYBACK_URL_MAX_AGE_MS - 1));
	assert (SbPlaybackUrlStale (entry, now + SB_PLAYBACK_URL_MAX_AGE_MS));
	assert (SbPlaybackUrlStale (entry, now + 55842560));
	assert (SbPlaybackUrlStale (entry, now - 1)); /* wall clock adjustment */
	entry->audioUrlFetchedAtMs = 0;
	assert (SbPlaybackUrlStale (entry, now));
	PianoDestroyPlaylist (entry);
	assert (!BarPlayerResultRecoverable (PLAYER_RET_HARDFAIL, SB_PLAYER_STOP_NONE));
	assert (!BarPlayerResultRecoverable (PLAYER_RET_DECODEFAIL, SB_PLAYER_STOP_NONE));
	assert (!BarPlayerResultRecoverable (PLAYER_RET_SOFTFAIL, SB_PLAYER_STOP_NEXT));
	assert (!strcmp (BarPlayerResultName (2), "PLAYER_RET_SOFTFAIL"));

	static BarApp_t app; pthread_t thread;
	/* Real FFmpeg/player setup failure, with no socket or audio device. */
	initApp (&app);
	app.player.url = "signalbox-test-missing-protocol://audio";
	assert ((uintptr_t) BarPlayerThread (&app.player) == PLAYER_RET_SOFTFAIL);
	assert (BarPlayerGetMode (&app.player) == PLAYER_FINISHED);
	assert (app.player.fctx == NULL && app.player.aoDev == NULL);
	BarPlayerReset (&app.player); destroyApp (&app, &thread);

	initApp (&app);
	app.playlist = testSong ("open", SbPlatformRealtimeMs ());
	app.playlist->head.next = &testSong ("queued", SbPlatformRealtimeMs ())->head;
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	callAction (&app, SB_UI_CMD_PAUSE);
	callAction (&app, SB_UI_CMD_PLAY);
	assert (SbPlaylistCount (app.playlist) == 2 && requests == 0);
	/* Simulate overnight wall time without advancing playback elapsed. */
	callAction (&app, SB_UI_CMD_PAUSE);
	app.player.pausedAtMs -= 55842560;
	callAction (&app, SB_UI_CMD_PLAY);
	assert (SbPlaylistCount (app.playlist) == 1 && app.prefetch.inFlight);
	pollRequest (&app);
	assert (requests == 1 && app.longPause.state == SB_LONG_PAUSE_REFRESH_READY);
	assert (BarPlayerGetMode (&app.player) == PLAYER_PLAYING);
	assert (!app.player.doQuit); /* current open stream survives */
	BarPlayerRequestStop (&app.player, SB_PLAYER_STOP_END_OF_TRACK);
	waitMode (&app, PLAYER_FINISHED); BarMainPlayerCleanup (&app, &thread);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (requests == 1 && SbPlaylistCount (app.playlist) == 1);
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	destroyApp (&app, &thread);

	/* An unusable resumed stream is replaced once; fresh playback succeeds. */
	initApp (&app);
	app.playlist = testSong ("resumed-unusable", SbPlatformRealtimeMs ());
	app.playlist->head.next = &testSong ("unused", SbPlatformRealtimeMs ())->head;
	playerResult = PLAYER_RET_STREAMFAIL; holdPlayer = false;
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_FINISHED);
	BarMainPlayerCleanup (&app, &thread);
	assert (app.playlist == NULL && app.recoveryPending);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	playerResult = PLAYER_RET_OK; holdPlayer = true;
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	assert (requests == 1 && starts == 2);
	destroyApp (&app, &thread);

	/* A below-threshold URL can still be unusable: refresh the whole queue,
	 * never try its next stale entries. A fresh failure exhausts the cycle. */
	initApp (&app);
	app.playlist = testSong ("bad", SbPlatformRealtimeMs ());
	app.playlist->head.next = &testSong ("bad2", SbPlatformRealtimeMs ())->head;
	PianoSong_t *second = PianoListNextP (app.playlist);
	second->head.next = &testSong ("bad3", SbPlatformRealtimeMs ())->head;
	playerResult = PLAYER_RET_SOFTFAIL; holdPlayer = false;
	BarMainStartPlayback (&app, &thread); waitMode (&app, PLAYER_FINISHED);
	BarMainPlayerCleanup (&app, &thread);
	assert (app.playlist == NULL && starts == 1 && app.recoveryAttempted);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (requests == 1);
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_FINISHED);
	BarMainPlayerCleanup (&app, &thread);
	assert (app.playbackBlocked && app.uiModel.song == NULL);
	assert (app.uiModel.playback == SB_UI_PLAYBACK_STOPPED);
	assert (app.uiModel.activity == SB_UI_ACTIVITY_AUDIO_UNAVAILABLE);
	for (int i = 0; i < 10; i++) {
		BarMainMaybePrefetch (&app); BarMainAdvancePlayback (&app, &thread);
	}
	assert (requests == 1 && starts == 2);
	callAction (&app, SB_UI_CMD_PLAY);
	assert (!app.playbackBlocked);
	playerResult = PLAYER_RET_OK; holdPlayer = true;
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	destroyApp (&app, &thread);

	/* Next on aged entries discards them before any URL is opened. */
	initApp (&app);
	app.playlist = testSong ("old", SbPlatformRealtimeMs () - 55842560);
	app.playlist->head.next = &testSong ("old2", SbPlatformRealtimeMs () - 55842560)->head;
	callAction (&app, SB_UI_CMD_SKIP);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (requests == 1 && starts == 0);
	assert (!strcmp (app.playlist->trackToken, stationA.id));
	BarMainAdvancePlayback (&app, &thread); waitMode (&app, PLAYER_PLAYING);
	destroyApp (&app, &thread);

	/* Failed refresh stays stopped; next explicitly allows another attempt. */
	initApp (&app); failRequest = true;
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (app.playbackBlocked && requests == 1);
	BarMainAdvancePlayback (&app, &thread); assert (requests == 1);
	callAction (&app, SB_UI_CMD_SKIP); failRequest = false;
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (requests == 2);
	destroyApp (&app, &thread);

	/* Switch station while refresh is pending: late old-station response loses. */
	initApp (&app); holdRequest = true;
	BarMainAdvancePlayback (&app, &thread);
	pthread_mutex_lock (&testLock);
	while (!requestEntered) pthread_cond_wait (&testCond, &testLock);
	pthread_mutex_unlock (&testLock);
	assert (BarUiDispatchCommand (&app, SB_UI_CMD_ACTIVATE_STATION, &stationB,
			NULL, false, BAR_DC_GLOBAL));
	pthread_mutex_lock (&testLock);
	holdRequest = false; pthread_cond_broadcast (&testCond);
	pthread_mutex_unlock (&testLock);
	pollRequest (&app); assert (app.playlist == NULL);
	BarMainAdvancePlayback (&app, &thread); pollRequest (&app);
	assert (!strcmp (app.playlist->trackToken, stationB.id));
	destroyApp (&app, &thread);

	/* Shutdown cancels an outstanding worker and launches no successor. */
	initApp (&app); holdRequest = true;
	BarMainAdvancePlayback (&app, &thread);
	callAction (&app, SB_UI_CMD_QUIT);
	pthread_mutex_lock (&app.prefetch.lock); app.prefetch.cancel = true;
	pthread_mutex_unlock (&app.prefetch.lock);
	pthread_mutex_lock (&testLock);
	holdRequest = false; pthread_cond_broadcast (&testCond);
	pthread_mutex_unlock (&testLock);
	pollRequest (&app); BarMainAdvancePlayback (&app, &thread);
	assert (app.doQuit && app.playlist == NULL && starts == 0);
	destroyApp (&app, &thread);

	/* Rotating playback tokens must not split stable enrichment work keys. */
	initApp (&app);
	PianoSong_t *old = testSong ("token-old", 1), *fresh = testSong ("token-new", 2);
	SbTrackIdentity oldId, newId;
	BarMainTrackIdentity (&app, old, &oldId);
	BarMainTrackIdentity (&app, fresh, &newId);
	char oldKey[128], newKey[128];
	SbTrackWorkKey (&oldId, oldKey, sizeof (oldKey));
	SbTrackWorkKey (&newId, newKey, sizeof (newKey));
	assert (!strcmp (oldKey, newKey));
	SbEnrichmentPrefetchEntry *cached = &app.metadataResolver.prefetch[0];
	cached->used = true;
	snprintf (cached->key, sizeof (cached->key), "%s", oldKey);
	cached->identity = oldId;
	cached->metadataDone = cached->metadataReady = true;
	cached->lyricsDone = cached->lyricsReady = true;
	cached->artDone = cached->artReady = true;
	cached->metadata.status = SB_LOOKUP_AVAILABLE;
	cached->lyrics.status = SB_LOOKUP_AVAILABLE;
	cached->lyrics.plainLyrics = strdup ("Preserved lyrics");
	cached->art.status = SB_LOOKUP_AVAILABLE;
	snprintf (cached->art.cachedPath, sizeof (cached->art.cachedPath), "preserved-art.jpg");
	app.playlist = old;
	app.metadata.status = SB_LOOKUP_AVAILABLE;
	app.albumArt.status = SB_LOOKUP_AVAILABLE;
	BarMainInvalidatePlaybackQueue (&app, false, "long_pause");
	assert (app.metadata.status == SB_LOOKUP_AVAILABLE && app.albumArt.status == SB_LOOKUP_AVAILABLE);
	SbMetadataResolverRequest (&app.metadataResolver, &newId, 123);
	SbMetadataResult metadata; SbLyricsResult lyrics; SbAlbumArtResult art;
	assert (SbMetadataResolverPoll (&app.metadataResolver, 123, &metadata));
	assert (SbLyricsResolverPoll (&app.metadataResolver, 123, &lyrics));
	assert (SbAlbumArtResolverPoll (&app.metadataResolver, 123, &art));
	assert (metadata.status == SB_LOOKUP_AVAILABLE);
	assert (!strcmp (lyrics.plainLyrics, "Preserved lyrics"));
	assert (!strcmp (art.cachedPath, "preserved-art.jpg"));
	SbLyricsResultDestroy (&lyrics);
	PianoDestroyPlaylist (fresh); destroyApp (&app, &thread);
	assert (!SbAudioOutputCloseAtShutdown (SB_AUDIO_PLATFORM_MACOS, true));
	assert (!SbAudioOutputCloseOnTrackEnd (SB_AUDIO_PLATFORM_MACOS, true));
	const BarPlayerResult results[] = {PLAYER_RET_OK, PLAYER_RET_DECODEFAIL,
			PLAYER_RET_SOFTFAIL, PLAYER_RET_STREAMFAIL};
	for (size_t i = 0; i < sizeof (results) / sizeof (*results); i++)
		for (unsigned int order = 0; order < 2; order++)
			for (unsigned int empty = 0; empty < 2; empty++)
				longPauseTransition (results[i], order != 0, empty != 0);
	testLongPauseFailures ();
	testLongPauseCancellation ();
	testTransportRetry ();
	testShortPauseDecodeRecovery (false);
	testShortPauseDecodeRecovery (true);
	puts ("playlist recovery tests passed");
	return 0;
}
