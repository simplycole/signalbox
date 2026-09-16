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
are reflected at runtime. Defaults relevant to the retained UI are:

| Key | Action |
| --- | --- |
| Arrow keys or `j`/`k` | Navigate the focused list or scroll a modal |
| Page Up/Down, Home/End | Page or jump to a boundary |
| Tab / Shift+Tab | Switch Stations/Recent focus when both panes are visible |
| `s` | Focus the existing Stations pane |
| `/` | Edit the focused station pane's case-insensitive filter |
| `#` | Jump to a visible station number |
| Enter | Tune the selected station or open the focused row's actions |
| `i` or `I` | Toggle Track Info |
| `L` or `l` | Toggle Lyrics |
| `p` or Space / `n` | Pause-resume / next track |
| `+` / `-` | Love / ban |
| `(` / `)` / `^` | Volume down / up / reset |
| `h` / `u` | Session history / upcoming tracks |
| `V` | Toggle the spectrum when it does not collide with a configured action |
| `?` / `q` | HELP / quit |

Escape closes retained views. While editing a station filter, Enter keeps the
filter and Escape clears it. Filtering happens in the existing Stations pane;
there is no separate full-screen station browser. The current station marker is
independent of selection so it remains visible in monochrome themes.

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
