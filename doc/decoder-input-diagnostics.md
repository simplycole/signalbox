# Decoder input architecture and diagnostics

## Current architecture

Pandora's API response supplies an `audioUrl`. Signalbox selects that URL and
stores it in `player_t`; it does not download, decrypt, parse, or buffer the
media bytes itself. The Blowfish code in `libpiano` applies only to Pandora API
request/response fields, not to audio media.

The playback input path is:

1. `main.c` assigns the selected song's `audioUrl` to `player.url`.
2. `openStream()` gives that HTTP(S) URL to libavformat.
3. FFmpeg's URL protocol layer performs HTTP and its internal buffering.
4. libavformat probes the container, selects the audio stream, and creates
   `AVPacket` objects. Signalbox does not synthesize packet boundaries and does
   not use an audio parser directly.
5. `play()` obtains packets with `av_read_frame()`, sends audio packets with
   `avcodec_send_packet()`, and drains frames with
`avcodec_receive_frame()` until `EAGAIN` after every accepted packet.
6. Decoded frames enter the existing FFmpeg filter graph and then libao.

The codec and container are determined at runtime from the URL contents. With
diagnostics enabled they are reported as `container=... codec=...`; no codec or
extension should be inferred solely from Pandora's URL.

Each playback attempt allocates a fresh demuxer and decoder and frees both in
`finish()`, including retries. Packets are unreferenced only after the decoder
has been drained to `EAGAIN`; `avcodec_receive_frame()` replaces/unreferences
the supplied frame according to FFmpeg's API contract. EOF is sent once with a
NULL packet and frames are received until decoder EOF. Send/receive return
values are checked; an unexpected error terminates decoding and closes the
filter source so the output thread cannot wait indefinitely.

## `SIGNALBOX_PCM_DIAGNOSTICS=1`

On Windows, the first five track attempts use a transparent custom AVIO wrapper
around FFmpeg's normal URL-protocol AVIO context. Reads remain synchronous;
there is no new producer thread, ring buffer, or reusable media buffer. Each
successful read is copied to `signalbox-compressed-track-NNN.bin` at its source
offset before the same caller-owned bytes are returned to libavformat. Thus a
seek does not turn the capture into a concatenation of duplicated ranges.

Only anomalous packets are logged (with a 20-line cap), including missing or
non-contiguous timestamps, tiny packets, and corrupt/discard flags. Demux,
send-packet, receive-frame, and AVIO errors are also logged. Aggregate packet
and byte-continuity summaries are emitted at track end.

The three byte counters currently reconcile by construction because Signalbox
has no separate network producer or stream buffer:

- `bytes_received_from_network`: bytes returned by FFmpeg's underlying URL
  protocol AVIO layer to the diagnostic wrapper.
- `bytes_written_into_stream_buffer`: the same bytes made available to the
  wrapper AVIO context.
- `bytes_read_by_ffmpeg`: the same bytes returned to libavformat's demuxer-side
  AVIO buffer.

This boundary cannot count socket-level read-ahead hidden inside FFmpeg's HTTP
implementation. `accounting_scope=avio_protocol_boundary` makes that explicit.
Short reads, seeks, read errors, captured byte writes, capture extent, and known
unread source bytes are reported separately.

## Offline A/B decode

The `.bin` suffix is intentional because the container is not known before
probing. FFmpeg probes file contents independently of the suffix. First inspect
the capture:

```sh
ffprobe signalbox-compressed-track-002.bin
```

Then decode it without forcing a guessed format:

```sh
ffmpeg -i signalbox-compressed-track-002.bin signalbox-offline-track-002.wav
```

For a complete, seekable source this produces a standalone reconstruction.
For an interrupted/non-seekable stream, the capture contains only byte ranges
actually requested by libavformat; `bytes_remaining_at_end`, `seeks`, and
`capture_extent` indicate whether incompleteness may explain an offline failure.
Do not rename the file to AAC, M4A, MP4, or MP3 until `ffprobe` identifies it.

Compare the offline WAV with `signalbox-decoder-track-002.wav`:

- artifacts in both place corruption in the source bytes or upstream delivery;
- a clean offline decode with artifacts in the live decoder capture implicates
  decoder feeding/lifecycle behavior;
- non-reconciling counters, AVIO errors, or a capture write error implicate the
  input/capture boundary.
