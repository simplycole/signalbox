Playback recovery after pause or sleep
=====================================

Diagnosis
---------

In the reported build, player result 2 was ``PLAYER_RET_SOFTFAIL``. The only
assignment of that result was the false return from ``openStream`` in
``BarPlayerThread``. That covered:

* ``avformat_open_input`` failure (including expired/denied URLs, network
  errors, invalid URLs, and interrupted opens).
* ``avformat_find_stream_info`` failure.
* No usable audio stream from ``av_find_best_stream``.
* Decoder-context allocation, parameter conversion, missing decoder, or
  ``avcodec_open2`` failure.

Filter/device setup failure was ``PLAYER_RET_HARDFAIL`` (1). User skip/quit had
separate stop reasons. Read/decode errors during ``play`` were not reliably
reported as failures: some were treated as successful endings; invalid-data
and connection-reset errors could reopen the same URL without a retry bound.

The authenticated log shows a successful resumed current track, followed by
repeated stream-setup failures on queued tracks. It does not contain the
FFmpeg error/stage needed to prove an HTTP expiry response. Expiration is the
strongest explanation, not a proven HTTP status. The 55,842,560 ms log field
was enrichment-prefetch age, not a playback URL timestamp.

Previously cleanup counted soft failures, then the main loop removed the
current item and attempted the next queued URL. Even setting ``nextStation``
to NULL at the retry limit did not remove already-queued items. When the queue
finally emptied, ``SbUiModelSetSong(NULL)`` cleared song text without clearing
the prior Playing progress state.

The first recovery pass still had a long-resume scheduling hole. Resume set
``playlistRefreshReason=long_pause`` and discarded future entries, but did
not start a worker. That reason also suppressed ordinary prefetch. The current
player later returned ``PLAYER_RET_DECODEFAIL``, which was outside the generic
setup/read recovery policy; cleanup discarded an already-empty queue and set
``playbackBlocked``. The advance loop therefore never requested a playlist.
An invalidation count of zero describes removed entries, not recovery success.

The local authenticated ``signalbox-tui-debug.log`` establishes the difference
between the two reported tests:

* At pause age 5,583,946 ms (about 93 minutes), lines 2175–2192 show future
  invalidation, DECODEFAIL, and Audio unavailable. Line 2195 then records
  ``command=act_songpausetoggle`` with no current song. That explicit user
  retry cleared the block; line 2197 starts the successful stream-failure
  refill. It was not automatic DECODEFAIL recovery.
* At pause age 48,936,474 ms (about 13.6 hours), lines 3573–3591 show the same
  terminal path after five future entries were removed. The next command is
  quit (3595), so no explicit retry starts a refill.
* The earlier failed refresh at line 724 is also followed by an explicit
  pause/play command (725), then a new refresh (727). This was a new user
  attempt rather than two workers automatically racing for ownership.

Queue ownership and URL lifetime
--------------------------------

``station.getPlaylist`` uses ``PianoRequestDataGetPlaylist_t`` and the station
ID as ``stationToken``. ``PianoResponse`` builds linked ``PianoSong_t`` objects
with the selected quality's ``audioUrl``, ``stationId``, ``trackToken``,
artist/title/album, and duration. There is no retained playlist token or
explicit expiry field. The legacy URLs are opaque; this client has no verified
expiry parameter to parse. The original queue retained URLs until consumed or
station draining, without age checks.

The new ``audioUrlFetchedAtMs`` records wall time at the beginning of each
playlist request. Using request-start time also avoids making an old response
look fresh when the computer sleeps during the request. The queue does not
accept expired or superseded background responses.

Pandora's current GraphQL documentation says radio URLs last up to one hour:
https://developer.pandora.com/docs/reference/graphql-api/overview/things-every-developer-should-know/

This is not a verified TTL for Signalbox's older JSON endpoint. In the absence
of a usable explicit expiry, Signalbox conservatively refreshes at 45 minutes,
leaving 15 minutes of margin against that published upper lifetime. The limit
is far below the authenticated 15.5-hour failure, but well above normal short
pauses. Reactive recovery handles failures below this age too.

Timekeeping
-----------

Playback elapsed is decoded audio PTS, updated by ``BarAoPlayThread``, not a
wall-clock timer. Pause state was a boolean plus condition variable; it now
also records wall-clock pause-start time. Enrichment-prefetch ages, rate
limits, and renderer scheduling use ``SbPlatformMonotonicMs``. The input loop
uses short curses/readline waits. Persistent enrichment caches use Unix wall
time. Playback URL age and real pause duration now use a separate
``SbPlatformRealtimeMs``; backwards wall-clock changes invalidate queued URLs
rather than extending their life.

macOS ``CLOCK_MONOTONIC`` does advance during sleep. ``CLOCK_UPTIME_RAW`` and
``mach_absolute_time`` are the clocks that exclude sleep. This was confirmed
against the installed ``clock_gettime(3)`` manual and Apple's source:
https://github.com/apple-oss-distributions/Libc/blob/main/gen/clock_gettime.3

Recovery and state transitions
------------------------------

* Short pause resumes the open player without queue invalidation.
* Long real pause drains only future Pandora playback entries and immediately
  schedules one asynchronous refill, even when there are zero future entries.
  The current player's URL stays owned; a healthy open song continues while
  the refill runs. If an older worker is still cancelling, recovery retains
  ownership and schedules the new refill when its completion is polled.
* The long-pause context moves through needed, pending, ready, and starting
  fresh. It survives the original player's natural end, SOFTFAIL, STREAMFAIL,
  or DECODEFAIL, and survives a zero-entry invalidation. An early player end
  shows stopped/Waiting until the owned refill arrives. Completion requires
  a fresh track to start, or a bounded failure to publish Audio unavailable.
  Continued playback of the original track cannot clear this context.
* Every future song is checked before opening, even without an explicit pause
  (for example, sleep while playing). Old queues are discarded as a batch.
* A recoverable setup/read failure clears the remaining playback queue and
  requests one fresh playlist. It does not walk each unusable entry.
* The fresh attempt has no second automatic refresh. Failure leaves playback
  stopped with ``Audio unavailable``. Next or play explicitly starts a new
  attempt; switching station resets the cycle and rejects old worker results.
* Decoder failures are recoverable only for the original suspended player in
  an active long-pause context. A fresh track's decode failure still stops the
  bounded attempt; ordinary decode failures and audio-device/filter failures
  remain terminal. Next during an owned refill advances within that same
  attempt. Station change and quit cancel its ownership and reject old results.
* Playlist requests use the existing background worker, short authentication
  locks, cancellation, and bounded network transactions. Transient transport
  failures retry inside the same worker up to the existing ``maxRetry`` attempt
  limit; they do not create independent refill cycles. Each attempt is capped
  at 15 seconds (or a shorter configured timeout). Authentication expiry allows
  at most one two-step login and one playlist retry. Curl callbacks and the
  retry boundary check cancellation; the main loop never waits for an
  unfinished playlist worker. Shutdown cancels and then joins it.
* A failed proactive refill blocks future advancement and publishes Audio
  unavailable while allowing a healthy original player to finish. If that
  player is already gone, failure clears the song and Playing progress.
* Player network callbacks honor next/quit. The decoder buffer wait also checks
  stop state, so quitting cannot leave it waiting for a stopped output thread.
* Cleanup clears the song, playback status, lyrics/art presentation, and old
  enrichment publication generation. Waiting and failure states are stopped;
  Playing is rendered only for a running player. Thread-creation failures and
  invalid URLs cannot leave a phantom Playing state.

``PLAYER_RET_SOFTFAIL`` (2) now identifies unusable stream open/probe/audio
selection. Decoder setup errors report ``PLAYER_RET_DECODEFAIL`` (4), allocation
and output setup failures ``PLAYER_RET_HARDFAIL`` (1), and active stream/read
failure ``PLAYER_RET_STREAMFAIL`` (3). Debug logs include result number and
symbol, setup stage/error code, item age/stale decision, pause age, invalidation
count, refresh reason/result, and recovery attempt/result. ``long_pause`` logs
explicitly report begin, future invalidation, scheduling, refill result,
original-player result, fresh-queue readiness, and completion. Signed URLs are not
logged; FFmpeg log lines containing URLs are redacted.

Enrichment and audio preservation
--------------------------------

Playback invalidation never clears MusicBrainz, LRCLIB, album-art,
track-selection, persistent, or completed in-memory prefetch caches. Pending
work owns copied identities. Track work identity no longer falls back to a
rotating playback ``trackToken``: when a stable music ID is absent, normalized
artist/title/album/duration identify reusable work across playlist refreshes.
Station switches keep their existing cancellation policy for pending work;
completed enrichment remains reusable.

No CoreAudio open/reuse/format/close policy changed. The macOS live device is
still process-lived, compatible output is reused, and unsafe final
``ao_close``/``ao_shutdown`` remain skipped. Recovery changes playback objects
above the device lifecycle.

Deterministic tests
-------------------

``make playlist-recovery-test`` is included in ``make test``. It exercises the
production transition, player cleanup, playlist worker publication, and user
action paths with fake playlist/player workers. It covers short pause,
simulated overnight pause retaining the current player, successful reactive
recovery, multiple unusable entries, bounded fresh failure, failed refresh,
next/play retry, next on an aged queue, station change during recovery,
shutdown, clock-boundary/backwards-time decisions, and actual promotion of
cached metadata/lyrics/art across changed playback tokens. It also calls the
real player/FFmpeg setup path with a nonexistent protocol, deterministically
confirming result 2 without network/audio. Existing player lifecycle tests
verify unchanged macOS output policy.

The follow-up tests exercise OK, DECODEFAIL, SOFTFAIL, and STREAMFAIL for the
original player, each with refill-before-end and end-before-refill orderings,
both with future entries and with an already-empty future queue (16 cases).
They assert immediate refill, preserved original song/Playing, stopped/Waiting
after early termination, fresh-track completion, and one concurrent request.
Repeated scheduling polls cannot clear ownership or start another cycle.
Additional cases cover failed refill before/after player end, terminal fresh
decode failure, ordinary decode failure, explicit next while waiting, an old
prefetch cancelling before the owned refill starts, and station switching.
The production HTTP loop is tested with mocked curl results for transient
success, attempt-limit exhaustion, permanent error, and cancellation between
attempts. No network connection is made by these tests.

Manual authenticated validation
-------------------------------

Run from a directory whose debug log you can safely overwrite, or archive the
previous log first; ``SIGNALBOX_DEBUG_TUI=1`` truncates the log at startup.

1. Normal pause: start ``SIGNALBOX_DEBUG_TUI=1 ./signalbox --tui``, pause for
   10 seconds, resume, let the song finish, then press ``n``. Confirm normal
   playback and no long-pause invalidation.
2. Fast simulation: ``make playlist-recovery-test`` simulates overnight age
   without real waiting. For an authenticated simulation, build with
   ``make clean && make CFLAGS='-O0 -g -DNDEBUG'`` and start under LLDB::

       SIGNALBOX_DEBUG_TUI=1 lldb -- ./signalbox --tui
       breakpoint set --name setPause --condition 'paused == 0 && app->player.doPause'
       run

   Pause/resume in the application. At the conditional resume breakpoint::

       expression -- app->player.pausedAtMs -= 55842560ULL
       breakpoint disable 1
       continue

   This changes only the paused-age input in the stopped process. Confirm the
   current song resumes, future entries are invalidated, and a fresh playlist
   is fetched immediately in parallel. Repeat with ``n`` after resuming.
   LLDB changes are a manual test only; production uses no simulation flag.
   Restore the normal build with ``make clean && make`` afterward.
3. Overnight: pause an authenticated playing track, sleep macOS for several
   hours, wake and resume. Confirm the open track can finish, the following
   track plays without restart, and the log shows long-pause invalidation and
   successful refresh. Repeat using ``n`` and switching station after wake.
4. Failure/responsiveness: disconnect networking before a needed refresh.
   Confirm the TUI accepts input while Waiting, ``q`` exits, and a failed
   refresh stops at Audio unavailable. Reconnect and press ``n`` or play to
   recover. Confirm it does not retry continuously while stopped.

Automated checks do not replace the authenticated overnight/CoreAudio test.
No authenticated session, actual system sleep, or live audio device was used
for automated validation.

Files changed and validation
----------------------------

* ``src/playback_queue.h``: injectable age decisions, batch timestamping, and
  playback-only discard helpers.
* ``src/libpiano/piano.h``: per-entry URL fetch timestamp.
* ``src/main.c`` / ``src/main.h``: asynchronous required playlist retrieval,
  invalidation, bounded recovery, transition ownership, and UI failure state.
* ``src/ui_act.c``: real pause timing, resume/next retry, station cancellation.
* ``src/player.c`` / ``src/player.h``: setup/read/decode classification,
  symbolic diagnostics, safe network/buffer cancellation, and pause timestamp.
* ``src/player_lifecycle.c`` / ``src/player_lifecycle.h``: named result enum and
  recoverability policy; existing audio-output policy functions are unchanged.
* ``src/platform.c`` / ``src/platform.h``: separate real-world age clock.
* ``src/ui.c``: cancellable, bounded background requests and bounded auth retry,
  with no authentication mutex held across background network I/O.
* ``src/settings.c``: update obsolete retry-limit comment.
* ``tests/playlist_recovery_test.c`` / ``Makefile``: new deterministic recovery
  target included in the full suite and clean target.
* ``docs/playback-recovery.rst``: audit, policy, limitations, and manual plan.

Validation completed successfully:

* ``make clean && make`` and ``make test`` (all 12 targets).
* Separate ``make playlist-prefetch-test``, ``make player-lifecycle-test``,
  ``make enrichment-test``, ``make tui-presentation-test``, and
  ``make playlist-recovery-test``.
* Full application dependencies plus recovery test built/run with
  AddressSanitizer and UndefinedBehaviorSanitizer.
* The initial pass also ran warning-focused builds of
  main/player/UI/actions/platform/lifecycle/settings using
  ``-Wall -Wextra -Wpedantic``. That build had the same 76 warning
  categories/counts as the unchanged HEAD baseline: existing callback unused
  parameters, old-style prototypes, an assert-only variable, and FFmpeg's C11
  union in C99 headers. No new warnings were introduced.
* ``git diff --check`` and ``git status``. Changes remain uncommitted; no push.

The final application binary is a normal optimized build, not a sanitizer or
age-simulation build.
