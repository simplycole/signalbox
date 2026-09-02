# Terminal UI architecture decision

Status: architecture phases A and B and renderer Phase C1 are complete. The
ncursesw shell is experimental and opt-in, with a usable station browser and
core playback controls.

## Decision

Use **ncursesw** for Signalbox's eventual full-screen interface. Keep the
line-oriented interface during migration and preserve it as a classic mode. A
headless mode must run the same application core without initializing a
terminal renderer.

The renderer boundary should be small and Signalbox-specific: lifecycle, input
polling, size/capability reporting, and rendering a read-only UI model. It
should enable classic, full-screen, and headless front ends without attempting
to hide every ncurses concept. Album art can later be an optional capability
beside the text renderer, not a reason to choose a heavier core library now.

Notcurses is the runner-up. Raw ANSI/termios and termbox2 are not recommended
as the primary renderer.

## Existing architecture

### Startup, loop, and state

`main()` in `src/main.c` owns one static `BarApp_t`. It saves terminal
attributes and disables `ECHO` and `ICANON`; installs signal handling;
initializes player, settings, libpiano, curl, stdin, and the control FIFO; gets
credentials; logs in; fetches stations; selects a station; enters
`BarMainLoop()`; and finally writes state and restores termios.

`BarApp_t` is both composition root and mutable application state. It contains
Pandora and curl handles, player and settings, current playlist and history,
current/next station, input descriptors, retry count, and quit flag. Station,
song, playlist, and history state are direct libpiano objects. Player mode,
pause/quit, position, and duration live in `player_t`; some are mutex-protected.
Volume is a mutable setting.

`BarMainLoop()` synchronously coordinates the application. Each iteration
cleans up a finished player thread, moves a song to history, fetches a playlist,
starts playback, waits for input for up to one second, and prints time. Network
requests and prompts block this thread. Audio decode/output use worker threads.

### Input and dispatch

`BarReadline()` in `src/ui_readline.c` calls `select()` over stdin and the FIFO.
Top-level input reads one byte, then `BarUiCommandFromKey()` maps it through the
configured shortcut table to an `SbUiCommand`. `BarUiDispatchCommand()` routes
that command to the existing action. The same reader implements blocking string,
integer, yes/no, filtering, and selection prompts. It has UTF-8-aware deletion
for simple code points, discards simple escape sequences, and does not produce
structured special-key events. FIFO bytes therefore share the keyboard path.

`src/terminal.c` globally disables canonical input and echo, but does not set a
complete raw mode: signal generation and other flags remain active. `SIGCONT`
reapplies this mode. There is no TTY/capability check, alternate screen, cursor
lifecycle, or renderer-aware suspend handling.

`src/ui_dispatch.c` binds shortcut IDs to default single-byte keys, named
commands, contexts, help, and configuration keys. A separate command-handler
table binds each command to a `BarUiAct*` function. Help remains generated from
the binding metadata.

### Output and events

`BarUiMsg()` writes synchronously to stdout, emits ANSI erase-line for most
message types, applies configured formats, and flushes. `BarReadline()` emits
ANSI cursor-left and erase-to-end while editing. Now-playing, stations, lists,
and time are synchronous output; time overwrites a carriage-return line about
once per second.

`src/ui.c` also owns blocking protocol calls/retries and starts event commands.
`src/ui_act.c` combines prompts, rendering, Pandora requests, `BarApp_t`
mutation, direct player locking/signalling, and event commands. Player error
paths call `BarUiMsg()` directly and could write while a future renderer owns
the screen. Event-command children are asynchronous; rendering itself is not.

No code queries terminal rows/columns. There is no `SIGWINCH`, responsive
layout, mouse input, frame model, dirty state, or redraw scheduler.

### Implemented Phase B seam

`BarApp_t` owns one `SbUiModel` and one `SbUiRenderer`. The model is a small
renderer-facing projection containing borrowed current-station/current-song
references, the optional real song station used by QuickMix formatting,
elapsed/duration, playback state, and a generation counter. Pandora and player
structures remain canonical; the model neither copies nor owns them.

Phase C1 also borrows the canonical station-list head. The synchronous main
loop owns and serializes that list's lifetime; the renderer only traverses it
for display and returns a borrowed selected pointer in an activation command.
Selection index and scroll offset are renderer-local and do not increment the
canonical model generation.

The renderer interface in `src/ui_renderer.h` has `init`, event-oriented
`render`, and `shutdown` operations. The classic backend in
`src/ui_renderer.c` is the sole implementation. Current station announcements,
current-song announcements, and progress are now model updates followed by
renderer calls. `BarUiMsg()` delegates to the same classic implementation and
retains its inherited signature for incremental compatibility.

There is no frame loop. Each model mutation increments `generation`, while the
classic renderer continues printing synchronously at exactly the existing
events. A future ncurses renderer can compare generations or introduce finer
dirty categories when its snapshot and event queue exist. A null/headless
renderer can use no-op lifecycle/render operations without changing canonical
state ownership.

Prompts, selection lists, and readline cursor editing stay classic/direct
because they are blocking interactions rather than passive model rendering.
Event-command pipe output is serialization, not terminal UI. Player-thread
errors remain on the inherited message path until structured notices can be
queued to the main/UI thread; calling ncurses from those threads would be
unsafe.

## Coupling to remove incrementally

- `BarMainLoop()` mixes state transitions, network work, playback lifecycle,
  history, input timing, and rendering; its one-second timeout is the refresh
  clock.
- `BarMainStartPlayback()` renders and emits events while configuring the
  player thread. `BarMainPlayerCleanup()` emits events, blocks in
  `pthread_join()`, applies retry policy, and mutates player state.
- `BarUiPianoCall()` is a UI function that owns curl/libpiano behavior,
  login continuation, retries, and error presentation.
- `BarUiAct*` callbacks receive all of `BarApp_t`, make service calls, conduct
  nested prompts, mutate canonical objects/settings, and manipulate player
  synchronization directly.
- `BarUiMsg()` is callable from orchestration, services, actions, and the player.
  Background output would corrupt curses.
- `BarReadline()` merges terminal input, FIFO commands, prompt editing, and
  refresh timing. Terminal key events must not become the scripting API.
- `BarReadline()` still represents only bytes and simple escape filtering; it
  cannot yet express structured special keys, focus, or overlays.
- `BarSettings_t` combines core/network/audio settings with bindings and output
  formats; consumers need narrower views even if parsing stays unified.
- `BarUiStartEventCmd()` consumes broad internal state instead of a stable,
  renderer-independent application event snapshot.

These are extraction targets, not reasons to rewrite libpiano or the player.

## Target boundaries and event flow

| Boundary | Responsibility | Must not own |
| --- | --- | --- |
| Application core | Session state machine, stations, queue/history, retries, shutdown, canonical snapshots | Terminal cells or raw keys |
| Pandora service | Adapt libpiano/curl requests and results | Rendering or prompts |
| Player service | Playback commands and thread-safe state/events | UI messages |
| UI model | Presentation snapshot plus selection/focus/overlay/status | Business policy |
| TUI renderer | ncurses lifecycle, layout, cells, colors, resize | Pandora/player actions |
| Input/keymap | Decode terminal events and map them by context | Business operations |
| Command dispatcher | Validate and route named commands | Terminal key codes |
| Platform hooks | Translate common state/commands for Now Playing/MPRIS | Playback policy |
| Classic/headless | Compatibility output or no interactive output; automation adapters | Separate core logic |

```text
keyboard -> input/keymap ----\
FIFO/platform/CLI -----------> command dispatcher -> application core
                                                   -> Pandora/player services

service results -> canonical state -> structured events
                                  -> UI model -> renderer
                                  -> eventcmd/platform hooks
```

Only the UI thread may call ncurses. Workers publish state changes/notices.

### Commands and keymaps

Introduce stable command IDs for playback, rating, volume, station operations,
and quit, plus UI-only navigation, focus, overlay, and dismiss commands. Each
binding should contain a terminal key, command ID, contexts, display label, and
help. Resolve overlay/modal, then active-pane, then global bindings. Generate
help from the effective map.

FIFO and future IPC should submit named commands, or translate legacy key bytes
for compatibility; they should not masquerade as terminal input.

### Canonical state and UI model

Canonical state owns connection/session phase, station collection and active
station, queue/current song/history, playback snapshot, rating, volume, errors,
and outstanding operations. It remains usable without a UI.

The TUI model is a safe snapshot/projection with stable IDs and display strings
for stations and tracks; elapsed/duration, playback, volume, and rating state;
connection/activity and structured notices; bounded history/upcoming rows;
selected station, scroll positions, focus, overlay and prompt state; terminal
size, layout, capabilities and theme; and dirty categories or a generation.

Selection, focus, scroll, notices, layout, and theme are transient UI state.
Active station/song, lists, rating, playback, and connection are canonical.
Renderer windows must not point into mutable libpiano lists.

## Rendering, resize, and responsive layout

Use an event-driven loop with a bounded progress timer:

- wake for terminal/FIFO input, application/player events, resize, and the next
  progress deadline;
- coalesce changes and render at most once per loop turn;
- update progress at most 4 Hz during playback, or once per second in reduced
  motion and compact views; render only on events while paused/idle;
- redraw whole dirty panes initially, then batch with `wnoutrefresh()` and
  `doupdate()`; do not build a second cell-diff engine unless measured data
  justifies it.

This is responsive without a high-FPS loop and limits flicker, CPU use, and
laptop wakeups. Reduced motion disables optional animation, not state updates.

Handle `SIGWINCH` by setting a `sig_atomic_t` flag or notifying a self-pipe; do
not draw or allocate in the handler. On the UI thread, update curses' size,
rebuild geometry/windows, clamp selection/scroll, invalidate layout, and render
once. Below a tested minimum, show compact playback plus required dimensions
while keeping quit/playback controls usable.

Suggested breakpoints, to tune after prototypes:

- **Large (about 100+ columns, 28+ rows):** stations left; now playing right;
  history/upcoming and status below.
- **Medium (about 70–99 columns):** station/now-playing split; collapse history
  to a count or overlay; shorten secondary metadata.
- **Small:** one switchable station or now-playing pane; one-line progress and
  status; remove decoration before information.

Treat height independently. Truncation must be Unicode-cell-aware.

## Visual language and accessibility

Use restrained broadcast-console cues: clear hierarchy, rules and meters,
monospaced alignment, and a small semantic palette. Avoid blinking, scan-line
effects, large ASCII logos, and decorative continuous motion.

Represent themes as semantic roles—background, primary, muted, border, focus,
accent, playing, loved, warning, error, and filled/empty progress—mapped to
colors and attributes after capability detection. Initial themes: phosphor
green, amber, monochrome, modern neutral, and high contrast. Never communicate
status by color alone.

Honor `NO_COLOR`, monochrome/high-contrast choices, reduced motion, and
terminals without color. Disable blink. Provide ASCII-safe symbols and avoid
ambiguous-width glyphs in aligned content. Classic/headless output remains the
screen-reader-friendly alternative.

## Library evaluation

### ncursesw — preferred

The wide-character ncurses API is sufficient for full-screen/split layouts,
Unicode, navigation, progress bars, themes, resize, optional mouse, alternate
screen, terminal capabilities, and efficient batched refresh. It has a mature
C API, broad documentation, and near-universal macOS/Linux packaging.

Its portable baseline is not truecolor. Traditional color-pair APIs are
awkward, and extended/direct color depends on build options and terminfo, so
Signalbox must look good at 256 colors. Unicode grapheme/emoji width still
needs care. Ncurses does not abstract Kitty/iTerm2/sixel images. macOS system
curses can be older than Homebrew ncurses, so builds must use one coherent
header/library pair and avoid untested extensions. Mouse and modified-key
reporting varies and remains optional. None of these limits blocks the core UI.

### notcurses — runner-up

Notcurses offers native RGB, planes/compositing, rich capability detection,
direct and alternate-screen modes, multimedia, and graphics. Its C API is
capable, it supports current macOS/Linux, and it remains maintained.

It is still a poor default fit. Full packages commonly bring libunistring,
ncurses/terminfo, image/multimedia support, build tooling, and FFmpeg-related
dependencies. A core-only link reduces runtime surface but also removes much of
the album-art rationale, while distro versions/availability vary. Its smaller,
faster-moving ecosystem raises packaging and debugging risk for features the
first TUI does not need. Reconsider it only if a prototype proves truecolor or
inline media is a core requirement.

### Raw ANSI/termios — rejected as primary

Raw control minimizes dependency weight, and Signalbox already emits a few
ANSI sequences. A polished implementation would still need input escape
decoding, terminfo negotiation, Unicode cell width, resize/mouse, alternate-
screen and suspend/crash safety, screen differencing, and terminal quirks.
Assuming ANSI without terminfo is not portable. Raw sequences remain reasonable
only for carefully detected optional graphics or the existing classic UI.

### termbox2 — not selected

Termbox2 is a serious small C library with a cell buffer/event API, Unicode,
resize, mouse, and 256/truecolor modes. It is preferable to hand-rolled ANSI.
Its package reach, documentation depth, widget/layout ecosystem, and operational
history are weaker than ncurses; Signalbox would own more pane, editing, key,
and portability behavior for little gain. It is a lightweight fallback if a
concrete ncurses blocker appears, not the present runner-up.

## Album artwork

Artwork is optional and deferred. Navigation, metadata, and status must never
depend on it; support explicit off/auto/on policy. No protocol is universal:
Kitty/WezTerm and some other modern terminals support Kitty graphics; iTerm2
has its own inline protocol; sixel is present in selected terminals/builds;
macOS Terminal should be treated as text-only; block mosaics are lossy and can
harm readability and screen-reader use.

Later, a small optional provider may report none, Kitty, iTerm2, sixel, or block
fallback after conservative detection and user override. Direct protocol output
must prove safe alongside curses redraw/cursor state. Prefer an external helper
or separate backend over image decoding in the core renderer. Defer fetching,
caching, and decoding policy too.

## Classic/headless behavior and terminal safety

Preserve three conceptual modes without fixing flag names yet:

- interactive TUI only when stdin/stdout are suitable TTYs and capabilities
  meet the minimum;
- classic line-oriented output, prompts, inherited keys, FIFO, and eventcmd;
- headless with no terminal mode, curses, prompts, carriage-return progress, or
  decoration, controlled by configured startup/automation inputs.

Ship TUI as opt-in first and keep classic as fallback. Change the default only
after parity/recovery testing. `TERM=dumb`, unsuitable capabilities, or
non-interactive stdout must not initialize curses. Do not infer suitability
from `TERM` alone. Always restore state on normal/handled fatal exit and make
suspend/resume leave and re-enter full-screen mode cleanly.

FIFO and `eventcmd` are compatibility contracts. Route FIFO through the common
dispatcher and structured application events to the existing serializer.
Preserve legacy byte commands during a documented transition while adding
named commands/versioned machine output later. Services must not need a PTY.

## Packaging and CI

For ncursesw:

- macOS ships curses; Homebrew's current `ncurses` is keg-only. A formula may
  depend on it for predictable features but must use its flags consistently;
- Debian/Ubuntu use `libncurses-dev` and link/detect `ncursesw`;
- Fedora uses `ncurses-devel`; Arch uses `ncurses`;
- source builds should prefer `pkg-config ncursesw`, with documented config-
  script/platform fallback and an actual compile/link feature check.

On the validated macOS 26 environment, Homebrew's `ncursesw.pc` compatibility
metadata returns `-D_DARWIN_C_SOURCE` plus `-lncurses`. Compilation uses
Apple's SDK `curses.h`, and the binary links `/usr/lib/libncurses.5.4.dylib`.
A compile/link probe for `cchar_t` and `setcchar()` succeeds, so the system
library supplies the wide-character API needed here. Signalbox accepts this
portable pkg-config decision rather than hardcoding a Homebrew keg path; no
Makefile or CI override is required.

Dynamic binary-size impact should be modest. Static size depends on platform
library/terminfo arrangements.

Homebrew and Arch package Notcurses but expose its broader dependency graph.
Debian stable availability has lagged while newer suites carry current
releases; Fedora must be checked per supported release. Source builds require
CMake/C17 plus terminfo/ncurses and libunistring, with optional graphics
dependencies. Termbox2 may require vendoring or new distro packages. Raw ANSI
has no package but transfers compatibility/test burden into Signalbox.

When ncursesw is added, CI should install development packages, verify feature
detection, and compile TUI-enabled and non-TUI/headless configurations if both
are supported. Normal CI remains build/smoke only. Add PTY integration tests
deliberately later; unit-test command maps, model projection, layout decisions,
and output-free headless startup without a TTY.

## Migration plan

1. **Observation seam (partial):** current station/song/progress use the UI
   model and classic renderer; player/background notices still need a safe
   main-thread queue.
2. **Command seam (complete):** command IDs separate legacy key/FIFO decoding
   from action execution without changing bindings.
3. **Application actions:** move service/player mutations and validation out of
   `BarUiAct*`; represent nested prompts as explicit states.
4. **UI model (minimal phase complete):** expand the existing borrowed
   current-view projection into safe station, queue, history, and notice
   snapshots before a full-screen renderer consumes mutable lists.
5. **Mode lifecycle:** define TUI/classic/headless selection; establish true
   non-interactive startup before curses becomes default.
6. **ncursesw skeleton (complete):** opt-in alternate-screen lifecycle,
   configured quit/help input, full redraw on resize, minimum-size behavior,
   model-driven metadata/progress, and status. Explicit suspend/resume polish
   remains alongside broader terminal recovery testing.
7. **Station browser (complete):** selection, scrolling, active station,
   responsive layout, and direct Enter activation.
8. **Now playing:** metadata, playback/rating, volume, timed progress.
9. **History/notices/help:** upcoming/history, errors, prompts, generated help.
10. **Themes/accessibility:** semantic themes, `NO_COLOR`, high contrast,
    ASCII/monochrome, reduced motion.
11. **Parity/default:** verify classic, FIFO, eventcmd, headless, small terminals,
    and macOS/Linux before changing the interactive default.
12. **Optional integrations:** platform media hooks, useful mouse behavior, and
    artwork only after a separate capability/packaging decision.

## Research sources

- [ncurses upstream](https://invisible-island.net/ncurses/)
- [Homebrew ncurses](https://formulae.brew.sh/formula/ncurses)
- [Debian ncurses](https://packages.debian.org/source/testing/ncurses)
- [Fedora ncurses](https://packages.fedoraproject.org/pkgs/ncurses/)
- [Notcurses upstream](https://github.com/dankamongmen/notcurses)
- [Notcurses manual](https://man.archlinux.org/man/notcurses.3.en)
- [Homebrew Notcurses](https://formulae.brew.sh/formula/notcurses)
- [Debian Notcurses](https://packages.debian.org/src:notcurses)
- [Arch Notcurses](https://archlinux.org/packages/extra/x86_64/notcurses/)
- [Termbox2 upstream](https://github.com/termbox/termbox2)
- [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
- [iTerm2 images](https://iterm2.com/documentation-images.html)
- [Sixel notes](https://vt100.net/docs/vt3xx-gp/chapter14.html)

Package facts are a research-date snapshot and must be rechecked when a
dependency is introduced.
