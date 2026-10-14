Short-pause playback continuity
==============================

Evidence and scope
------------------

The October 12 authenticated log records a 169,292 ms pause, far below the
45-minute future-URL age limit. Lines 8392–8406 show pause/resume, advancing
playback through approximately 49 seconds, an AAC input-exhaustion error,
and PLAYER_RET_DECODEFAIL. Lines 8407–8418 show six future entries discarded
and Audio unavailable. Line 8419 records a pause/play command with no current
song; that command clears playbackBlocked and starts the refill at 8421.
There was no fresh playlist waiting to be started before that command.
The blocked failure branch prevented the automatic transition.

This is not evidence of URL expiry or a long-pause state-machine regression.
The old stream log lacks HTTP completion/error, packet size, and decoder API
stage. It cannot prove which upstream event produced the damaged packet.
The confirmed architectural defect is that pause backpressure reaches HTTP
input. Remote idle termination/truncation is a plausible mechanism; malformed
upstream AAC is also possible. These cannot be distinguished retrospectively.
No application compressed-byte ring existed that could overwrite AAC bytes.

Pause and buffer audit before this change
----------------------------------------

BarAoPlayThread pulls a filtered frame and calls ao_play. It then updates
songPlayed and, while doPause is true, waits on player.cond under player.lock.
It retains that one frame until resume. Pause takes effect at a frame boundary;
an already-submitted libao/device buffer may finish. It does not close or
reopen the output device, flush FFmpeg, or change decoder position.

BarPlayerThread continues av_read_frame, avcodec_send_packet/receive_frame,
and filter writes until decoded PTS is more than bufferSecs ahead of output
(default 5 seconds). It then waits on aoplayCond under aoplayLock. The paused
output consumer stops advancing lastTimestamp or requesting frames, so the
decoder stops reading the network too. The native FFmpeg HTTP connection
remains open, with no independent downloader or explicit HTTP pause request.
Finite OS receive buffers eventually apply TCP backpressure to the server.

The old ownership chain was:

* HTTP/TCP/TLS: FFmpeg's native HTTP protocol, not Signalbox's curl playlist
  worker. HTTP protocol buffering is MAX_URL_SIZE + HTTP_HEADERS_SIZE in the
  installed FFmpeg source; socket/TLS capacities are implementation-dependent.
  There is no Signalbox producer or background read when demux is blocked.
* AVIO: FFmpeg-owned input buffer, default 32 KiB; demux consumes it on the
  player thread. FFmpeg can resize it for probing/packet reads. Signalbox
  supplies only an interrupt callback for next/quit/signals, not pause.
* Demux/probe queues and AVPacket: allocated/owned by FFmpeg; one application
  packet is referenced until send/receive processing completes, then unrefed.
  Probe buffering is governed by FFmpeg's options. No application byte limit
  or pause-aware AVIO callback existed.
* Decoder AVFrame: one reusable frame plus FFmpeg's codec-owned internal
  references. The player drains receive_frame to EAGAIN before the next send.
  Decoder output buffering has no independent download thread.
* Filter PCM queue: FFmpeg owns referenced frames, both threads serialize
  access with aoplayLock. The application applies a PTS waterline of bufferSecs
  plus roughly one decoded frame, not a fixed byte capacity. At 44.1 kHz stereo
  S16, five output seconds are roughly 882,000 bytes; intermediate float frames
  and resampler queues have different sizes. It waits instead of overwriting.
* Output frame/libao/CoreAudio: one application sink frame, then backend-owned
  output buffers. Their capacity is configured by libao/the device. Audio
  output waits on player.cond while paused. Next/quit clears pause and wakes
  both output and decoder condition variables.

Consequently pause first stops the PCM consumer while decoding runs briefly,
then stops the decoder/network consumer and can leave HTTP idle. Pause itself
does not manufacture EOF, overflow a ring, or intentionally truncate packets.
Whether remote input survives depends on how much compressed data was already
downloaded, socket buffering, file size/bitrate, and remote connection policy.
That explains an intermittent vulnerability rather than a fixed pause timeout.

FFmpeg error path
-----------------

The installed version is FFmpeg 9.0.1. Its AAC decoder's
ff_aac_decode_frame_int parses raw AAC elements until TYPE_END. If fewer than
three bits remain before the next element marker, it emits the unprefixed
input-exhaustion message and returns AVERROR_INVALIDDATA (-1094995529).
Related branches emit prefixed messages for exhausted band-type/extension
payloads. This concerns encoded packet bits, not an empty PCM queue.

The generic avcodec_send_packet implementation can eagerly decode into its
internal frame and return this error. Signalbox previously set decoderError
for negative send, receive, or flush returns, without recording which stage
or code occurred. BarPlayerThread maps that boolean to PLAYER_RET_DECODEFAIL.
Demux errors instead map to STREAMFAIL; successful demux EOF flushes the
decoder and drains the filter. The historic log cannot identify the exact API
call, but the new real-AAC test reproduces the same message and INVALIDDATA
by removing the last byte of a valid ADTS stream.

Primary sources:

* https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.1/libavcodec/aac/aacdec.c
* https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.1/libavcodec/decode.c
* https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.1/libavformat/aviobuf.c
* https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.1/libavformat/http.c
* https://curl.se/libcurl/c/CURLOPT_LOW_SPEED_TIME.html

Implemented input policy
------------------------

HTTP -> curl worker -> anonymous compressed-track file -> custom FFmpeg AVIO
-> demux/decoder -> existing bounded PCM filter queue -> existing libao output.

Only the current track is downloaded. The worker keeps downloading during
pause, without decoding or growing a PCM queue. The anonymous tmpfile is
limited to 128 MiB and removed when the current player finishes. Curl has
bounded chunks, stdio has its fixed internal buffer, and custom AVIO starts
with 32 KiB (FFmpeg can resize that buffer). File producer and consumer have
separate logical offsets and one mutex for file access. There is no wraparound
or overwrite. Reaching the file limit returns EFBIG, not producer backpressure
or unbounded memory growth. Disk failure returns EIO.

An empty but unfinished input waits on its condition variable. Download writes,
HTTP completion/error, and cancellation wake it. Only successful completion
with all published bytes consumed becomes AVERROR_EOF. Short Content-Length
responses, curl errors, and read-idle timeout become errors, not EOF. Seek uses
the real Content-Length or final length; an unknown unfinished length returns
ENOSYS rather than advertising a temporarily truncated file. Reads after a
seek ahead wait for those bytes or terminal completion. No signed URL or token
is included in the new diagnostics.

The HTTP worker performs one GET, without restarting/appending a partially
downloaded response. Connect and low-speed timeouts use the configured timeout,
with a 30-second fallback for timeout=0; there is no total-transfer deadline
that would terminate a steadily downloading song. Next/quit cancels the spool,
wakes AVIO, and aborts curl through its progress callback. The main/UI thread
does not join an unfinished downloader. Curl response/status/error diagnostics
distinguish partial-file, timeout, cancellation, and successful HTTP EOF.

This chooses continued compressed download over paused HTTP consumption.
Pausing network input would preserve the observed idle/backpressure risk;
reopening the URL on resume would depend on range support, seek accuracy, and
URL lifetime. Unbounded in-memory or decoded-PCM buffering would consume far
more memory. The spool keeps the same open decoder and output position and
does not depend on reopening an aged URL. Startup can wait for the tail/full
download if container probing requires it; files above the cap are rejected.
Files without a usable Content-Length have less information for early seeking.

Scoped automatic fallback
-------------------------

A resumed PLAYING current player is tagged until reset. If that player later
returns DECODEFAIL with INVALIDDATA and no explicit stop, it gets one automatic
fallback through the existing recovery budget. Already-fetched healthy future
entries stay queued and start automatically; if there is no future entry, the
existing asynchronous required refill starts automatically. Existing URL-age
checks still reject an actually stale future entry. Continued failure stops
with Audio unavailable instead of creating a decode-failure skip loop.
Fresh player reset clears the resumed tag and decoder error. Unrelated decode
failures, allocation errors, and intentional next/quit are not globally retried.

Healthy short pause alone never invalidates a playlist, launches recovery,
reopens the current stream, or replaces its player. Long-pause wall-clock age,
45-minute future invalidation, proactive owned refill, fresh-start completion,
station-generation rejection, and bounded failure are unchanged. The long-pause
context still handles its original player's failure before this fallback.
CoreAudio open/reuse/format/close policy is unchanged; its device remains
process-lived. Enrichment caches and identity handling are unchanged.

Diagnostics and authenticated manual test
-----------------------------------------

The added logs report pause/resume playback_ms (from the existing whole-second
songPlayed counter), compressed buffered_bytes, total received stream_bytes,
network_state, HTTP result/status, AVIO/demux EOF/error, decoder send/receive/
flush stage and exact error, packet size/corrupt flag, and natural/explicit stop.
The two stream_bytes snapshots compare bytes before pause and after resume.
Diagnostics are emitted at transitions/errors, not for every packet.

The debug log is truncated at startup. Archive it before the next launch::

    cp signalbox-tui-debug.log /private/tmp/signalbox-before-short-pause.log
    SIGNALBOX_DEBUG_TUI=1 ./signalbox --tui

1. Start a song and pause after audio begins, away from its end.
2. Wait approximately three minutes; resume using the normal pause/play key.
3. Confirm the SAME song continues from its paused position and reaches its
   natural end; confirm the next song starts without next/pause/play input.
4. Repeat with several songs, including pauses of 10 seconds, one minute, and
   ten minutes. No LLDB or injected production timing is required.
5. While paused in a separate trial, confirm next and quit remain responsive.
6. Repeat the existing long-pause/sleep manual test to verify future refill.

After exit, inspect::

    rg 'command=act_songpausetoggle|player (pause|resume|input|decoder error|stop)|player thread exiting|playback recovery|long_pause|playlist refresh' signalbox-tui-debug.log

For a healthy short pause, look for matching pause/resume playback positions,
network_state=downloading or complete, successful HTTP/AVIO/demux EOF,
player stop reason=end_of_track, and PLAYER_RET_OK. There should be no
long_pause invalidation and no decoder error. A genuine resumed-stream failure
should enter context=resumed_decode_failure automatically, without a command
with song_context=no. HTTP completion can occur before the pause itself.

Tests and validation
--------------------

player-pause-test generates AAC/ADTS and MP4/AAC through the installed encoder, uses the real
demux/decoder/filter/output-thread path with an observational libao sink, and
compares paused and uninterrupted PCM byte counts/hashes. It covers temporary
input emptiness/spurious wakeups, seek and byte preservation, spool limit,
partial/timeout errors, cancellation, paused next/quit, real malformed AAC,
and cleanup of AVIO/file/network ownership. POSIX builds also use a real local
HTTP server: compressed download completes while its consumer/player is paused,
real curl detects truncated Content-Length, and paused AAC retains identical
PCM. No Pandora authentication or real audio device is used.

playlist-recovery-test adds resumed INVALIDDATA fallback with/without a healthy
future entry, no user nudge, and bounded fresh failure. Existing short-pause and
all 16 long-pause ordering/result/empty-queue cases remain in the suite.

Validation completed successfully: make clean && make; make test (13 targets);
make player-lifecycle-test; make playlist-recovery-test;
make playlist-prefetch-test; make player-pause-test; git diff --check; git status.
ASan/UBSan cover the full application build and both pause/recovery targets.
The final application binary is a normal optimized build. The final pause
sanitizer run also covers next/quit during an unfinished real HTTP transfer.
The local HTTP test requires loopback socket permission in restricted sandboxes.
Authenticated short-pause repetitions and macOS sleep/device validation remain
manual; the historical damage mechanism is not claimed as proven by unit tests.

Files changed: src/player_input.c/h, src/player.c/h, src/ui_act.c, src/main.c,
tests/player_pause_test.c, tests/playlist_recovery_test.c, Makefile, and this
document. No commit or push is performed by this task.
