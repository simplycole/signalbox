# Architecture

This document describes two different things: the pianobar-derived code that
exists today, and the architecture Signalbox intends to grow toward. Target
components named below are design boundaries, not implemented modules.

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

### Current terminal interaction: `src/ui*`, `src/terminal*`

The current interface is a line-oriented console UI rather than a full-screen
TUI:

- `ui.c` formats messages and lists, mediates protocol calls, and launches
  event commands.
- `ui_act.c` implements user actions such as station selection, love, ban,
  history, pause, and volume changes.
- `ui_dispatch.c` maps configured keys to named `SbUiCommand` values, then
  dispatches commands to the inherited actions.
- `ui_readline.c` provides terminal input and filtered selection.
- `terminal.c` establishes and restores terminal attributes.

These files both present information and participate in application control, so
the current UI boundary is not fully isolated from orchestration or service
calls. Phase B adds a narrow implemented seam in `ui_renderer.h`: `BarApp_t`
owns an `SbUiModel` and `SbUiRenderer`, and the current station, current song,
and progress flow through that model to the classic renderer. `BarUiMsg()` now
delegates its byte-for-byte-compatible formatting to the classic renderer
implementation. The inherited message API remains a compatibility facade while
call sites are migrated incrementally.

`SbUiModel` is view state, not a second application model. It borrows the
canonical station list and current `PianoStation_t` and `PianoSong_t` objects
owned by the application, and stores the small playback projection needed
to render progress: duration, elapsed time, playing/paused state, and signed-dB
software volume. Ten owned, bounded recent-track display rows are copied when
the current song transitions away, avoiding dangling libpiano pointers. A monotonically
increasing generation records updates for a future invalidation-driven
renderer. It does not own or copy Pandora lists or objects. The synchronous
main loop serializes their lifetime; renderer-local selection and scrolling do
not mutate the model.

The renderer has an `init`/`render`/input/notice/`shutdown` lifecycle. Its
classic synchronous line backend preserves
configured prefixes, postfixes, formats, ANSI erase-line behavior, flushing,
and carriage-return progress. Phase C2's opt-in ncursesw backend
owns its `SCREEN`, maps ordinary keys through the shared command table, stores
captures notices under a mutex for the status line, expires normal notices
after four seconds and errors after eight, redraws on `KEY_RESIZE`, and calls
`endwin()` during normal shutdown. It owns station selection and scrolling,
keeps the active station independent, and sends Enter activation plus audited
prompt-free playback actions through named commands. ncurses types remain
private to `ui_renderer_curses.c`.

`--tui` selects the full-screen backend while classic remains the default.
Terminal suitability is checked before termios or curses initialization. The
inherited blocking login interaction runs in classic mode. Initial TUI station
choice uses autostart or the first station without leaving curses, while FIFO
input remains on the existing byte-to-command path. Interactive legacy actions
with nested prompts remain disabled in curses.

Blocking prompts and selection/readline editing intentionally remain on the
classic path. Event-command serialization remains machine-facing direct
output, and fatal/developer diagnostics remain at their existing layers.
Player-thread messages only update mutex-protected notice state and never call
ncurses. The main thread performs all rendering on the existing refresh cadence.

### Settings and configuration: `src/settings.c`, `src/settings.h`

The settings layer defines defaults and reads configuration and state. It owns
credentials, network and audio options, key bindings, output formats,
event-command settings, and protocol configuration. It selects one active
configuration directory: `signalbox` when its config exists, otherwise the
legacy `pianobar` directory when its config exists, otherwise `signalbox`. State
and the default control FIFO use that same directory; explicit paths configured
for the FIFO, event command, audio pipe, or CA bundle remain unchanged.

### Platform integration

There is no native macOS or Linux desktop-integration layer in the core program.
Portable/POSIX facilities, libao, terminal handling, event commands, and FIFO
control provide the current integration points. Scripts under `contrib/` offer
examples outside the core executable.

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
