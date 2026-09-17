# Terminal UI

Signalbox's retained full-screen interface is the default on supported
interactive terminals. macOS and Linux use ncursesw; Windows uses the same
renderer with PDCursesMod WinCon and a native Win32 input adapter. `--classic`
keeps the inherited line-oriented interface available for compatibility and
headless use.

## Mode selection and lifecycle

With no mode flag, Signalbox selects the TUI only when stdin and stdout are
interactive and terminal capabilities are usable. `--tui` forces the TUI and
reports an error when those requirements are absent. `--classic` never
initializes curses.

The renderer owns its screen, cursor, colors, timed input, modal windows, and
resize handling. Normal shutdown restores terminal state. Below 50x15 it shows
a minimum-size message and accepts only the configured quit binding until the
terminal is enlarged.

## Current controls

The in-app HELP overlay is authoritative because configured `act_*` bindings
are reflected at runtime. It is scrollable and includes the following default
retained-TUI surface.

| Area | Key | Action and context |
| --- | --- | --- |
| Global | `?`, `q` | Open/close Help; quit Signalbox |
| Navigation | Arrow keys or `j`/`k` | Navigate the focused list or scroll text |
| Navigation | Page Up/Down, Home/End | Page or jump to a boundary |
| Navigation | Tab / Shift+Tab | Switch Stations/Recent focus when both panes are visible |
| Navigation | Enter, Escape, mouse wheel | Activate; back/close/cancel; navigate/scroll |
| Playback | `p` or Space, `n` | Pause/resume; next track |
| Playback | `P`, `S` | Explicitly resume; explicitly pause |
| Rating | `+`, `-` | Love; ban the current track |
| Volume | `(`, `)`, `^` | Down; up; reset to 0 dB |
| Stations | `s` | Focus the Stations pane |
| Stations | `a`, `c`, `d`, `g` | Add music; create; delete; create by genre |
| Stations | `r`, `x`, `=` | Rename; edit QuickMix; manage station |
| Stations | `v` | Create a station from the current song or artist |
| Stations | `z` | Cycle A-Z/original Pandora display order |
| Stations | `/`, `#` | Filter or start a visible-number jump while Stations is focused |
| Station jump | digits/keypad, Backspace/Delete, Enter, Escape | Edit, tune, or cancel a station-number jump |
| Filter editing | printable text, Backspace, Enter, Escape | Edit, keep, or clear the station filter |
| History | `h`, Tab, Enter | Full history; focus Recent; selected-track actions |
| Upcoming | `u`, Enter | Browse upcoming tracks; selected-track action |
| Track | `i` or `I`, `l` or `L` | Toggle Track Info; toggle Lyrics |
| Track | `e`, `b` | Explain why the track is playing; bookmark song/artist |
| Display | `V` | Toggle the spectrum when it does not collide with a configured action |

Text prompts accept printable text, Left/Right, Home/End, Backspace/Delete,
Enter, and Escape. Choice/list modals use the navigation keys; QuickMix uses
Space to toggle an item, and confirmations also accept `y`/`n` or change choice
with Left/Right/Tab. Help, Track Info, Lyrics, and other long text views accept
arrows, `j`/`k`, Page Up/Down, Home/End, and the mouse wheel. Enter, Escape, or
the opening key closes a retained text view where applicable.

Configured action keys are shown using their active runtime values, including
multiple aliases. A configured binding shadowed by a fixed retained-TUI key is
not advertised. Consequently the inherited defaults `j` (Add Shared) and `i`
(song/station information) are navigation and Track Info in the retained TUI;
those inherited actions become available and appear in Help when remapped to
unreserved keys. Tired/shelf, debug, and account-settings actions are not in
Help because the retained TUI does not dispatch them.

Escape closes retained views. While editing a station filter, Enter keeps the
filter and Escape clears it. Filtering happens in the existing Stations pane;
the resulting borrowed-pointer view is then sorted using the current A-Z or
original-order mode. There is no separate full-screen station browser. The
current station marker is independent of selection so it remains visible in
monochrome themes.

Track Info and Lyrics scroll with the navigation keys and update in place when
background enrichment completes. Enter, Escape, or the opening key closes the
view. Resize redraws the active view without starting a nested renderer loop.

## Layout and accessibility

Large layouts show Stations, Now Playing, Recent, Upcoming, status, and—when
enabled and space permits—the spectrum and album art. Medium layouts reduce or
hide secondary panes. Narrow layouts stack the essential station and playback
content. Resizing preserves bounded selections and reconstructs modal windows.

Themes are `phosphor`, `amber`, `mono`, and `neutral`. `NO_COLOR` disables color
without removing selection, warning, rating, or active-station meaning. Unicode
symbols degrade to ASCII where required. The interface does not blink.

## Spectrum

The visualizer observes the final packed signed-16-bit PCM immediately before
libao output. The audio thread publishes a fixed snapshot; it never calls
curses. A 1024-sample window, bounded transform cadence, smoothing, and peak
hold produce twelve canonical bands. Medium panes aggregate these to eight;
narrow or short layouts hide them.

`visualizer = spectrum|off`, `--visualizer spectrum|off`, and the local `V`
toggle control the feature. Classic mode does not analyze or draw the spectrum.
The 80 ms TUI input cadence used while it is visible is bounded and is not a
busy loop.

## Album artwork

Cover Art Archive files are decoded through FFmpeg into bounded RGBA data,
resized with aspect preservation, and converted to ANSI half-block cells.
`album_art = auto|pixel|off` defaults to `auto`. Truecolor is preferred when
advertised; otherwise a 256-color conversion is used. Monochrome, unsupported,
small, loading, and failed-art states retain the complete text layout.

The prepared-cell cache is keyed by source path, geometry, and color mode.
Normal progress redraws therefore do not decode or resize the image. Native
Kitty, iTerm2, and Sixel protocols are intentionally not used.

## Lyrics and enrichment

MusicBrainz metadata, LRCLIB lyrics, and album-art resolution run away from the
audio thread. Track generations prevent a late result from replacing the
current track. User-facing metadata states are `Available`, `No match`, and
`Temporarily unavailable`; lyrics use `Synced`, `Plain`, `Instrumental`, `No
match`, and `Temporarily unavailable`; art uses transient `Loading`, then
`Ready`, `None`, or `Unavailable`.

Synchronized lyrics are parsed once on publication. Sequential redraws advance
a cursor, while playback discontinuities recover with binary search.
`lyrics_display = three-line|line|off` controls only the inline strip; the full
Lyrics view remains available for plain text and when inline display is off.

The schema-versioned persistent cache lives under the platform cache/data path.
Successful results and genuine misses are reusable; transient provider failures
are never persisted as permanent misses. Encoded art is stored separately from
the JSON metadata/lyrics cache.

## Station model and queue prefetch

The station browser stores a lightweight array of borrowed canonical station
pointers. It rebuilds only after station-list generation changes or filter
edits, never on ordinary redraws. Filtering does not relink or deep-copy the
Pandora list. Activation resolves the selected canonical pointer and reuses the
normal station-switch pipeline.

Near playlist exhaustion, one bounded worker may fetch the next playlist.
Publication checks playlist generation, station pointer/ID, and current target;
station changes discard stale results. Prefetch does not reorder tracks or
change Pandora queue semantics.

## Renderer and ownership boundaries

`SbUiModel` is a projection rather than a second application model. Current
Pandora station/song/playlist pointers are borrowed under the serialized main
loop. Session history copies bounded display fields so it does not retain freed
libpiano strings. Renderer-local focus, selection, scrolling, modal, lyric
cursor, and prepared-art state are owned and destroyed by the renderer.

Provider parsing, cache writes, image downloads, image decode/resize, and
station-filter allocation are event-driven. The normal redraw path only reads
snapshots, advances lyric lookup, formats bounded text, and emits cells.

## Classic/headless compatibility

Classic mode retains byte keybindings, FIFO control on Unix, event commands,
audio pipes, configured formats, and line prompts. It remains the automatic
fallback for redirected or unsuitable terminals. TUI-only renderer state is not
part of the FIFO protocol, and services do not need a pseudo-terminal.

## Diagnostics

`SIGNALBOX_DEBUG_TUI=1` writes `signalbox-tui-debug.log` instead of writing over
the curses screen. It records lifecycle, input routing, generation decisions,
provider outcomes, and cache/render transitions without credentials, tokens, or
raw API bodies. It may contain track metadata, station-filter text, provider
IDs/URLs, and local paths; review it before sharing.

On Windows, `SIGNALBOX_DEBUG_KEYS=1` additionally writes
`signalbox-keys.log`. Password keystrokes are redacted. Neither diagnostic is
enabled during normal operation.
