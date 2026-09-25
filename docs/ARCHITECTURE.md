# Architecture

This document first describes the implemented pianobar-derived application and
then records longer-term boundary goals. The final Target state section is
directional; the preceding sections describe code present in this repository.

## Metadata enrichment

Cached encoded album art flows through the renderer-neutral FFmpeg decoder,
bounded RGBA image, aspect-preserving Lanczos reduction to the effective terminal
pixel grid, conservative tonal enhancement, terminal color conversion, and an
in-memory prepared-art cache. Tonal preparation caps percentile-based contrast
expansion at 16%, applies only a tiny lift to dark covers, and follows with a
capped one-eighth-strength four-neighbour unsharp mask. Wide layouts request a
24-column by 12-terminal-row grid directly from the encoded source; smaller
layouts retain 20×10, 16×8, 12×6, and 10×5 fallbacks. It does not boost
saturation, dither, or upscale an already prepared grid. The curses renderer remains authoritative
for layout and input; after its atomic screen update, the prepared cells are
painted into the reserved Now Playing rectangle with standard ANSI color and
Unicode half blocks. Cache identity includes path, target geometry, and color
mode, so normal progress redraws do not decode or resize artwork. Because the
ANSI cells bypass curses' physical-screen cache, the compositor uses the actual
topmost `WINDOW` origin and dimensions as a passive clip rectangle: it never
emits art or clearing spaces into cells owned by an overlay. Uncovered art
remains visible immediately outside that half-open frame, and closing or
resizing an overlay immediately recomposes the full cover at the current
geometry. Retained overlays are repainted after direct ANSI emission; Help also
explicitly invalidates its physical curses rows, so its complete frame is the
final physical writer without forcing a full-terminal repaint. Before a later
curses commit, the full-width band containing the previous ANSI art, plus a
one-row guard, is marked physically corrupted. This prevents curses update
optimizations from moving cells that exist in the terminal but not in curses'
physical-screen model; ANSI art remains the final writer on non-modal frames.

`src/enrichment.c` implements a provider-neutral enrichment boundary. A Pandora
song is copied into a `SbTrackIdentity`; original display strings are retained
while separately normalized artist/title values form a provider-tagged cache
key. The main loop submits that identity to one bounded worker, never the audio
thread. Current work has priority over a copied next-track identity and one
opportunistic track-+2 identity; generation checking rejects completed results
belonging to an older track.

Upcoming enrichment uses the same metadata/LRCLIB worker and art worker rather
than adding provider concurrency. Provider caches are checked before network
access and duplicate pending/active keys are coalesced. A prefetched bundle
remains cache-only until its stable artist/title/album/duration key becomes
current. A hashed Pandora track token, when available, also prevents two queued
recordings with otherwise identical display fields from sharing speculative
state; provider caches remain reusable by normalized recording fields. Metadata,
parsed lyrics, and source-art state are then rebound to the
actual current generation. Station changes drop queued speculative jobs but keep
reusable cache entries. All MusicBrainz paths, including art release fallback,
continue through the process-wide one-request-per-second gate and existing
per-track request budget.

The first metadata adapter queries the public MusicBrainz recording search API
with artist and title, a descriptive User-Agent, a short timeout, and no more
than one request per second. Conservative normalized artist/title scoring
reports available, no-match, or non-fatal error state. Its provider-neutral
result retains canonical names; selected release and release-group identity;
edition and first-release dates at their source precision; release-group type;
country; one coherent label/catalog pair; one normalized ISRC; up to five
deduplicated categories with explicit official-genre or folksonomy-tag
provenance; provider; and confidence. Missing optional values remain
empty rather than acquiring placeholders. Album-level `firstReleaseDate` comes
only from the selected release group's `first-release-date`; a recording search
date is never promoted to album Original Release. If a supplied group date is
later than the selected edition date at their shared precision, it is rejected
rather than displayed or silently swapped.

Release choice uses the same album-family, artist, status, type, and compilation
scoring used by album-art resolution. Every selected candidate—whether embedded
in the recording, chosen from a release-group browse, or found by album
search—passes through the same release and release-group completion stage.
When recording data has no suitable release identity, Signalbox performs an
exact artist+album search and, only if that produces no useful candidate, one
conservatively edition-normalized family search. Clear trailing qualifiers such
as `(Deluxe)` or `(20th Anniversary Deluxe)` may be removed; genuine subtitles
and unrelated punctuation are preserved. The metadata path is capped at five
logical MusicBrainz requests per uncached track (the initial search plus four
bounded lookups). Each logical lookup retries the same request once after 429,
500, 502, 503, 504, timeout, or transport failure; 404 is not retried. Metadata
therefore has a hard maximum of ten HTTP attempts. Cover-art recovery
can independently add at most two MusicBrainz release-list lookups when the
selected edition has no cover, so the complete enrichment path remains bounded
at seven logical lookups and fourteen HTTP attempts when every lookup retries.
All calls run on playback-independent workers and retain one-second spacing.
A fully populated embedded release needs only the recording search. The usual
matched-recording path takes two or three logical metadata requests: recording
search plus whichever selected-release and release-group details are actually
absent.

A 32-entry provider-aware in-memory cache avoids duplicate requests during a
run, while the schema-versioned persistent cache reuses eligible results across
launches through atomic writes in the platform data directory. A successful
recording match remains `Available` when an optional release or release-group
detail request fails; partial rich metadata can still be cached, while transient
top-level provider failures are not persisted.

The worker publishes generation-tagged metadata progressively: canonical
recording identity first, a validated release family next, and optional detail
as it arrives. The retained Track Info view can therefore update without waiting
for the complete ladder. A failed optional request never replaces an already
published successful core result, and normal generation checks reject stale
intermediate publications.

Lyrics have their own provider-neutral result and provider interfaces rather
than sharing the metadata provider shape. LRCLIB is the current community
lyrics adapter. It uses the original Pandora artist/title and, when available,
album and reliable duration. A bounded lookup ladder tries the constrained
identity, a conservatively cleaned edition title, artist/title without album or
duration constraints, and finally one structured LRCLIB search. Search results
must have strong normalized title/artist agreement and, when available, album
and duration support; weak or ambiguous candidates are rejected. Metadata runs
first on the same coalescing enrichment worker, preventing LRCLIB timeouts or
rate-limit pauses from starving MusicBrainz; neither provider can block playback
or create an unbounded queue. Metadata and lyrics use separate provider-aware
32-entry caches; successful, instrumental, and normal no-match lyric results are
cached, while transient failures are not.

The generic lyrics result retains LRCLIB's matched identity, record ID,
instrumental flag, plain lyrics, and synchronized lyrics. `lyrics_sync.c`
parses synchronized LRC lines once when a result is published; the renderer
uses a cursor for sequential playback and binary-search recovery after a timing
discontinuity. Inline eligibility carries the current song generation and is
re-evaluated on every published lyric/art/layout state; plain-only lyrics remain
available only in the full view. Inline one- or three-line display is
configurable, and the full Lyrics view highlights the current synchronized
line. Lowercase `i` opens the
one unified Track Info view;
uppercase `I` is a harmless alias. `L` opens Lyrics, with lowercase `l` also
accepted. The aliases toggle retained modal state in the main TUI loop; async
results refresh an open view without a nested input/render loop. Both lookups
and failures leave Pandora playback unchanged.

The enrichment worker also owns a schema-versioned JSON cache which survives
restarts. Metadata and lyrics use provider-separated keys in the shared file;
corrupt or incompatible files are ignored, transient failures are excluded,
and flushed temporary files are atomically replaced. Cache schema 5 persists
the complete rich metadata result, including category provenance; older cache
files are ignored and replaced without manual deletion. Deserialization also
rechecks the date invariant.
Successful MusicBrainz results retain recording, release, and release-group
MBIDs internally. Art resolution uses a provider-neutral result containing the
provider kind and display name, source URL, source identity kind, release and
release-group identity, MIME type, confidence, cached path, state, byte count,
and timing. The current bounded ladder is:

1. the selected release's front Cover Art Archive image;
2. the selected release group's canonical front image via the direct
   [`/release-group/{mbid}` Cover Art Archive endpoint](https://musicbrainz.org/doc/Cover_Art_Archive/API);
3. the best conservatively matched alternate release in that validated group;
4. the existing bounded artist/album family recovery.

Only entries marked `front` are accepted. Compilation, artist, and album-family
guards continue to reject unrelated artwork. Encoded JPEG, PNG, or WebP data
(at most 5 MiB) is stored separately under `art/`. Source filenames include
provider plus release-vs-group identity, while persistent entries use distinct
provider/version namespaces, preventing provider or identity collisions. A
successful or authoritative no-match result is reusable; rate limits, timeouts,
transport failures, and provider-unavailable states are never persisted as
no-art. The same ladder and caches serve current, next, and +2 work, and
generation checks prevent stale UI publication. Track Info shows only the clean
provider name, never source URLs.

Last.fm was evaluated for the secondary-provider slot. Its documented
[`album.getInfo`](https://www.last.fm/api/show/album.getInfo) and
[`track.getInfo`](https://www.last.fm/api/show/track.getInfo) methods require an
API key (though not an authenticated user
session), expose image-size URLs, and return explicit service-offline,
temporary-error, and rate-limit states. Signalbox does not use those images:
the current [Last.fm API terms](https://www.last.fm/api/tos) explicitly exclude images/artwork from permitted
API use. No API-key setting is therefore exposed. fanart.tv was also evaluated;
its [album endpoint](https://api.fanart.tv/) cleanly accepts a MusicBrainz
release-group MBID but requires
a project key and serves user-contributed artwork under a separate policy. It
was not added because the extra legal/configuration surface is disproportionate
after direct CAA release-group recovery. Offline operation and installations
without third-party keys continue normally with CAA and cached art only.

Prepared terminal cells are memory-only and keyed by encoded source path,
requested geometry, color mode, and quality-algorithm version, so no persistent
prepared-art migration is required for the quality algorithm change. Decode, preparation, cache-hit,
provider-resolution, and image-download timings are available in debug output.

LRCLIB is an open/community lyrics service requiring no API key. Signalbox
identifies itself with its version and project URL, spaces requests, honors a
numeric `Retry-After` after rate limiting when practical, and treats all
provider errors as non-fatal. The abstraction permits a licensed provider such
as Musixmatch in the future; Signalbox makes no claim that lyric text is public
domain or licensed by Signalbox.

## Current state

The current application is a single C program with a statically linked-in
`libpiano` by default. The main loop owns shared application state and directly
coordinates protocol requests, playback, settings, and terminal interaction.

### Pandora protocol: `src/libpiano/`

`libpiano` contains the Pandora-facing data model and request machinery.
`piano.c` manages model objects and request construction, `request.c` performs
request setup, `response.c` parses service responses, `crypt.c` supplies the
protocol's cryptographic operations, and `list.c` contains list helpers.

The rest of the program calls this layer through `piano.h`. Signalbox retains
the `libpiano` name and protocol behavior at the current baseline.

### Application coordination: `src/main.c`

`main.c` is the orchestration layer. It initializes settings, terminal state,
HTTP and protocol handles; obtains credentials; authenticates; fetches stations
and playlists; starts the player thread; dispatches input; and handles shutdown
and retry behavior. `BarApp_t` in `main.h` is the central runtime state shared
across those operations.

### Playback engine: `src/player.c`, `src/player.h`

The player runs in a pthread. FFmpeg/libav opens and decodes the stream and
constructs the audio-filter graph; libao sends decoded samples to the selected
audio output. The player state exposes pause, quit, elapsed time, duration,
volume, and lifecycle information to the rest of the application.

Each track still owns fresh stream, decoder, filter, and worker-thread state.
The main loop joins a player only after it reports `PLAYER_FINISHED`, then may
start the next track. Audio-device policy is separate and testable. Linux,
Windows/WMM, and explicit audio pipes preserve their per-track libao close/open
lifecycle. macOS live output is process-lived because libao's CoreAudio plugin
can block indefinitely inside `ao_close()` even after its writer thread exits.
Normal end, skip, and station change therefore reuse the compatible device.
At final macOS shutdown Signalbox deliberately skips both `ao_close()` and
`ao_shutdown()` while that live device exists and lets immediate process exit
reclaim it; this avoids moving the same AudioUnit deadlock into the quit path.
The decoder/filter and audio-output threads are always stopped and joined first,
and no concurrent track accesses the retained handle.

`src/spectrum.c` is a platform-neutral observational branch at the final PCM
boundary. FFmpeg decoding runs on the per-track player thread. Its filter graph
applies the existing volume and `aformat` stages and produces packed,
native-endian signed 16-bit PCM. Linux, Windows, and audio-pipe output retain
the configured/stream sample rate and source channel count. On macOS, live
libao output opens once as stereo at the first track's configured/stream rate;
later FFmpeg graphs resample and mix to that stored format. The per-track
audio-output thread pulls each `AVFrame`; directly
before the unchanged synchronous `ao_play()` call it gives the analyzer a
read-only view of that frame. Decoder frames, output format, pointer, byte count,
gain, and timing are not changed.

The analyzer keeps a fixed 1024-sample mono ring, performs at most one internal
radix-2 FFT per 80 ms with a Hann window, and publishes twelve normalized bands
plus peak caps under a small dedicated mutex. Bin ranges are recomputed only
when the input sample rate changes and clip naturally at Nyquist. The UI/main
thread only copies a
bounded `SbSpectrumSnapshot` into `SbUiModel`; curses never sees FFmpeg state,
and the audio thread never calls curses. No per-buffer, per-transform, or
per-render allocation occurs. The renderer uses all twelve canonical bands when
the right pane is at least 69 cells wide, max-aggregates them to eight display
bands from 38–68 cells, and hides the display below 38 cells. Track and format
changes reset its fixed state;
stale snapshots decay during pause, buffering, and transitions. The module has
no macOS DSP dependency and is shared by the macOS, Linux, and Windows builds.

### Current terminal interaction: `src/ui*`, `src/terminal*`

The default interactive interface is a retained full-screen TUI. The inherited
line-oriented interface remains available through `--classic` and for
non-interactive/headless execution:

- `ui.c` formats messages and lists, mediates protocol calls, and launches
  event commands.
- `ui_act.c` implements user actions such as station selection, love, ban,
  history, pause, and volume changes.
- `ui_keymap.c` maps configured keys to named `SbUiCommand` values and keeps
  the retained-TUI command/help metadata beside that map. `ui_dispatch.c`
  dispatches those commands to the inherited actions.
- `ui_readline.c` provides terminal input and filtered selection.
- `terminal.c` establishes and restores terminal attributes.

These files both present information and participate in application control, so
the UI boundary is not fully isolated from orchestration or service calls.
`ui_renderer.h` provides the implemented seam: `BarApp_t`
owns an `SbUiModel` and `SbUiRenderer`, and the current station, current song,
and progress flow through that model to the classic renderer. `BarUiMsg()` now
delegates its byte-for-byte-compatible formatting to the classic renderer
implementation. The inherited message API remains a compatibility facade while
call sites are migrated incrementally.

`SbUiModel` is view state, not a second application model. It borrows the
canonical station list and current `PianoStation_t` and `PianoSong_t` objects
owned by the application, and stores the small playback projection needed
to render progress: duration, elapsed time, playing/paused state, and signed-dB
software volume. A dynamically grown, newest-first session-history array copies
bounded artist, title, album, and station strings plus rating and transition
time when the current song transitions away, avoiding dangling libpiano
pointers. It has no small track cap and is freed at shutdown. A monotonically
increasing generation records updates for the retained renderer. It does not
own or copy Pandora lists or objects. The synchronous
main loop serializes their lifetime; renderer-local selection and scrolling do
not mutate the model.

The model includes request activity (`requesting`, `waiting for playlist`, `error`,
and recovered/ready) to that projection. It describes synchronous request
activity rather than pretending Pandora maintains a continuous socket
connection; existing curl retry policy is unchanged.

The renderer has an `init`/`render`/input/notice/`shutdown` lifecycle. Its
classic synchronous line backend preserves
configured prefixes, postfixes, formats, ANSI erase-line behavior, flushing,
and carriage-return progress. The ncursesw backend
owns its `SCREEN`, maps ordinary keys through the shared command table, stores
captures notices under a mutex for the status line, expires normal notices
after four seconds and errors after eight, redraws on `KEY_RESIZE`, and calls
`endwin()` during normal shutdown. It owns station selection and scrolling,
keeps the active station independent, and sends Enter activation plus audited
playback actions through named commands. Its small synchronous text,
confirmation, and list prompt primitives own only local input/presentation;
actions still own search, creation, rename, deletion, and canonical mutation.
ncurses types remain private to `ui_renderer_curses.c`.

The scrollable Help view is assembled by `tui_presentation.c`. Configurable
rows take their active runtime keys, descriptions, sections, and retained-TUI
allowlist from the canonical metadata in `ui_keymap.c`; fixed navigation,
editing, pane, and modal controls are recorded as presentation metadata beside
the local-key resolver. Bindings shadowed by fixed TUI controls are omitted.
This keeps the renderer's advertised commands and its dispatch allowlist in
lockstep without moving context-specific curses input into the shared classic
keymap.

The retained station-pane model uses a lightweight view array of borrowed
`PianoStation_t` pointers. `station_browser.c` rebuilds it only after a station
refresh, filter edit, or view-sort change; it never relinks or deep-copies the
canonical Pandora list. Filtering first produces canonical pointer matches;
the view then uses its current A-Z or original-Pandora ordering. A-Z is the TUI
default, and `z` cycles the presentation-only mode. Renderer selection remains
a view index re-anchored by canonical pointer, while active and duplicate-name
station identities remain canonical pointers carrying their Pandora IDs. Enter
emits a structured activation command that reuses `nextStation` →
`drainPlaylist()` → playlist retrieval. That transition advances enrichment
generation immediately, and prefetch publication also checks playlist
generation, target/current station agreement, and station ID.

In TUI mode the canonical `PianoSong_t` history is also retained for the full
process lifetime so historical info, station creation, and bookmark actions
continue to operate. Classic mode retains its configured bounded-history
behavior. Neither representation is persisted; both are destroyed at shutdown.
The curses renderer owns only RECENT focus, selection, and scroll indexes. An
Enter event carries the selected index back to the main loop, which resolves it
against canonical history and reuses the existing history action flow.

The permanent UPCOMING projection borrows nodes following the current playlist
head only while the single-threaded main loop renders. Interactive selection
pauses that loop, and its action/details modal completes before playback can
detach a node. Signalbox does not relink the playlist: libpiano has no
queue-promotion request or ownership contract for client-side reordering.

Focusing, navigating, filtering, and sorting the station pane issue no Pandora
requests; the rest of the retained main view continues rendering from the same
UI model.

The same synchronous primitives support advanced station operations.
The action layer fetches genre, seed, feedback, and station-mode data and owns
all Pandora requests. Labels are borrowed only during one blocking modal or
copied into short-lived action-owned arrays. For QuickMix, the renderer toggles
a caller-owned boolean snapshot; `ui_act.c` copies it to canonical station
flags only after Enter and restores the snapshot if the request fails. Esc
therefore cannot mutate account state. Station-info and search results retain
their existing libpiano destruction paths after the modal closes.

Supported interactive terminals select the full-screen backend by default.
`--tui` forces that backend, while `--classic` selects the line-oriented
compatibility backend.
Terminal suitability is checked before termios or curses initialization. The
startup login uses a native, masked curses form. Initial TUI station
choice uses autostart or the first station without leaving curses, while FIFO
input remains on the existing byte-to-command path. The curses allowlist covers
create/add-music search, rename/delete, QuickMix, hierarchical genre selection,
shared station IDs, create-from-song, bookmarks, session history, upcoming
display, and seed/feedback/mode management. Account settings remain disabled.
Classic startup retains the inherited masked readline prompt and password
helper behavior.

Other blocking prompts and full readline behavior remain on the classic path.
Event-command serialization remains machine-facing direct
output, and fatal/developer diagnostics remain at their existing layers.
Player-thread messages only update mutex-protected notice state and never call
ncurses. The main thread performs all rendering on the existing refresh cadence.

With spectrum enabled, curses uses an 80 ms timed input poll (otherwise the
existing one-second cadence) and redraws only from `SbUiModel`. Approximate
buckets are 40–90, 90–180, 180–350, 350–700, 700–1400, 1400–2800,
2800–5600, and 5600–12000 Hz, clipped at Nyquist. The displayed center labels
are not precision instrumentation. Uppercase `V` is local and collision-checked;
lowercase `v` remains the inherited create-station-from-song action.

### Settings and configuration: `src/settings.c`, `src/settings.h`

The settings layer defines defaults and reads configuration and state. It owns
credentials, network and audio options, key bindings, output formats,
event-command settings, and protocol configuration. It selects one active
configuration directory: `signalbox` when its config exists, otherwise the
legacy `pianobar` directory when its config exists, otherwise `signalbox`. State
and the default control FIFO use that same directory; explicit paths configured
for the FIFO, event command, audio pipe, or CA bundle remain unchanged.

The Signalbox-only ``account`` file stores the remembered active email, never a
password. It is created with mode `0600` by temporary-file write, `fsync`, and
rename, and is consulted only when the selected config supplies no user. The
credential precedence is explicit plaintext `password`, explicit
`password_command`, an exact secure-store match for the active user, then an
interactive prompt. Signalbox does not merge, rewrite, or migrate legacy
pianobar configuration.

### Credential boundary: `src/credential.c`, `src/credential.h`

Authentication calls a narrow load/store/delete/availability interface keyed
by service `org.signalbox.pandora` and Pandora email. macOS implements it with
Security.framework's `SecItem` APIs and updates an existing generic-password
item rather than accumulating duplicates. Linux uses libsecret synchronously
when the optional `libsecret-1` development package is present and fails closed
when no Secret Service provider is reachable. Builds without a native backend
retain session-only TUI login and unchanged explicit classic configuration. A
native Windows build can implement the same interface with Credential
Manager `CredRead`, `CredWrite`, and `CredDelete` without exposing Windows
headers to shared authentication code.

### Platform integration

`platform.h`/`platform.c` owns configuration/data path construction, atomic
replacement, monotonic time, safe local time, sleep, executable-sibling lookup,
and shutdown notification. UTF-8 remains the internal encoding; the Windows
implementation converts only at Win32 API boundaries, uses Known Folders, and
maps console control events to a deferred shutdown request.

macOS and Linux use ncursesw terminal/readline implementations. Windows builds
the same `ui_renderer_curses.c` against PDCursesMod WinCon by default, with a
diagnostic VT output alternate. `terminal_win32.c` saves/restores console modes,
screen state, and code pages, while `terminal_input_win32.c` owns
`ReadConsoleInputW` key and resize decoding. Layout and command code do not
contain Win32 console calls.

Playback remains FFmpeg plus libao on all three platforms; the Windows build
uses libao's WMM output. Windows secure credential storage, subprocess helpers,
FIFO/Named Pipe control, packaging, and CI remain explicit gaps. Scripts under
`contrib/` are Unix-oriented helpers outside the core executable. See
[`WINDOWS.md`](WINDOWS.md) for current setup, validation status, and limits.

## Target state

Signalbox should preserve a small, testable protocol core while separating
playback and application state from any particular presentation or operating
system. The transition will be incremental; it is not a commitment to rewrite
working code.

### Protocol boundary

Keep Pandora request/response details behind a narrow service interface based on
`libpiano`. Protocol identifiers and compatibility-sensitive behavior should not
leak into view code. Changes here require focused compatibility testing and
clear upstream provenance.

### Playback boundary

Expose explicit playback commands and observable state: load, play, pause, skip,
stop, volume, duration, position, and errors. FFmpeg and audio-output details
should remain behind this boundary so the TUI, headless mode, and platform
adapters consume the same state.

### Application core

An application layer should own the state machine for authentication, station
and playlist selection, playback transitions, retries, and shutdown. It should
publish structured state changes rather than terminal-formatted strings and
accept structured commands rather than raw key presses.

### Settings and credentials

Configuration parsing should remain independent of presentation. Non-secret
preferences should have documented defaults and migrations. Credentials should
be obtained through a provider boundary so macOS Keychain, Linux secret-service,
an external command, and interactive input can coexist without embedding
platform code in the protocol layer.

### TUI layer

The interactive mode is a consumer of application state. It owns layout,
station, now-playing and recent panes, progress display, navigation, themes,
resize behavior, accessible reduced/non-animated behavior, and human-readable
status and error presentation. It must not become the only way to operate the
application.

The architecture decision is to use `ncursesw` behind the small
Signalbox-specific renderer boundary now established in `ui_renderer.h`.
Terminal input should map to named
application commands, and the renderer should consume a read-only UI model
rather than mutable libpiano/player internals. Rendering should be event-driven
with a bounded progress timer and batched curses updates. The current-state
analysis, dependency comparison, event model, responsive layout, and migration
sequence are in [`TUI.md`](TUI.md).

### CLI/headless layer

A separate non-interactive entry point should expose predictable startup,
control, status, and exit behavior for terminals, scripts, services, and remote
front ends. It should reuse the application core rather than automate the TUI.
The inherited FIFO and event-command facilities are compatibility inputs to
that design, not yet the finished interface.

Migration should retain the line-oriented interface as a classic fallback.
Headless operation must skip termios/curses setup and route FIFO, future IPC,
and platform controls through the same command dispatcher as the TUI.
Application events should feed classic output, the TUI model, event commands,
and platform hooks without any consumer owning playback or Pandora policy.

### Platform adapters

Platform-specific code should translate common application state and commands:

- macOS: Now Playing, media keys, notifications, and Keychain.
- Linux: MPRIS, desktop notifications, and secret-service/keyring support.

Adapters should not own playback policy or Pandora requests. Builds must be able
to include only the adapters available on their target platform.

### Dependency direction

The intended direction is presentation and platform adapters → application core
→ protocol and playback boundaries. Settings and credential providers support
the application core. Protocol and playback code must not depend on the TUI or
desktop integrations.
