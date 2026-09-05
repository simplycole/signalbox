# Windows portability plan

This document records the Phase W0 source audit, implementation plan, and the
completed W1 compile foundation and the W2 implementation. Windows remains a developer target while the
later runtime, packaging, and release milestones are completed.

## W2 Windows Terminal TUI — WinCon default, VT diagnostic alternate

W2 compiles the existing `src/ui_renderer_curses.c` against PDCursesMod; there
is no separate Windows renderer. Native Windows mode selection deliberately
does not depend on `TERM`: an interactive console selects TUI automatically,
explicit `--tui` attempts it, redirected handles reject it, and `--classic`
continues to win when requested. Windows Terminal remains the user-facing
target even though the selected curses backend is now WinCon.

### Installed PDCursesMod backends

The MSYS2 UCRT64 `mingw-w64-ucrt-x86_64-pdcurses` 4.5.4-1 package builds every
port with `WIDE=Y UTF8=Y` and installs these static archives:

- `/ucrt64/lib/libpdcurses_wincon.a` — WinCon, link with
  `-lpdcurses_wincon`;
- `/ucrt64/lib/libpdcurses_vt.a` — VT, link with `-lpdcurses_vt`;
- `/ucrt64/lib/libpdcurses_wingui.a` — WinGUI, link with
  `-lpdcurses_wingui`;
- `/ucrt64/lib/libpdcurses.a` — the package's default **WinGUI** static
  archive, not WinCon; and
- `/ucrt64/lib/libpdcurses.dll.a` — import library for the default
  `libpdcurses.dll`, also WinGUI.

It also installs correspondingly named `libpdcurses_wincon.dll`,
`libpdcurses_vt.dll`, and `libpdcurses_wingui.dll` under `/ucrt64/bin`, plus
the unqualified WinGUI DLL. Signalbox uses the explicit backend static archive
so there is no ambiguity or runtime DLL swap.

WinCon is the default build:

```sh
make clean
make WINDOWS_CURSES_BACKEND=wincon
```

The VT output path uses the same renderer and native Win32 input adapter. It is
retained as an explicit diagnostic alternate for backend comparisons:

```sh
make clean
make WINDOWS_CURSES_BACKEND=vt
```

The Makefile checks the selected archive and rejects any backend other than
`wincon` or `vt`. macOS and Linux builds are unchanged.

PDCursesMod's core and WinCon port are public domain; its repository documents the
status of the few separately licensed ancillary files. Signalbox consumes the
packaged library and header and does not vendor those ancillary build files.

The renderer API audit found direct compatibility for `newterm`, `set_term`,
`delscreen`, `endwin`, cbreak/noecho, cursor/keypad/timed input, `KEY_*`
constants including `KEY_RESIZE` and `KEY_BTAB`, windows, borders,
ACS lines, batched refresh, attributes, colors, and wide-string output.
`use_default_colors` remains ncurses-only, so PDCursesMod uses a black
background. The compatibility shim uses PDCursesMod's Unicode-aware
`PDC_wcwidth` instead of the platform C runtime's `wcwidth`/`wcswidth`.
Signalbox supplies keypad translation on Windows through the native adapter;
PDCursesMod does not consume keyboard input.

### Input ownership and Windows Terminal diagnostic

Signalbox owns Windows keyboard input through `ReadConsoleInputW`; PDCursesMod
is used for drawing only. The adapter clears `ENABLE_VIRTUAL_TERMINAL_INPUT`,
`ENABLE_LINE_INPUT`, `ENABLE_ECHO_INPUT`, and quick-edit mode, and enables
`ENABLE_WINDOW_INPUT`. It preserves the host's `ENABLE_PROCESSED_INPUT` choice.
`terminal_win32.c` saves the original input/output modes and code pages before
curses starts and restores them after curses shuts down. For the VT renderer it
enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on output only; WinCon retains its
native output mode. The input adapter reapplies its native mode after
PDCursesMod initialization so the two components do not compete over input
mode. Startup identifies the selected pairing as either
`renderer=wincon input=win32_event` or `renderer=vt input=win32_event`.

With `SIGNALBOX_DEBUG_KEYS=1`, the start of `signalbox-keys.log` reports
`GetFileType`, `GetConsoleMode`, the relevant mode bits, and whether
`PeekConsoleInputW` succeeds. Each subsequent input line reports
`source=win32_event`, the raw virtual key and Unicode value, and the normalized
logical key. Password characters are redacted.

Login and modal reads explicitly request an infinite native wait. The main TUI
passes its renderer cadence directly to the adapter (80 ms with the spectrum,
otherwise 1000 ms); it does not infer that value from `wtimeout`, `nodelay`, or
`wgetdelay`. A short 35 ms bounded read is used only while completing an
application-keypad escape sequence. On Windows each bounded read uses one
`GetTickCount64` deadline: `WaitForSingleObject` waits only for the remaining
budget, then `ReadConsoleInputW` consumes records until it finds a key-down or
resize event. Key-up, mouse, focus, and menu records are consumed without
turning the read into an immediate `ERR`.

The former VT timing workaround was removed. Real Windows tests showed that
PDCursesMod's VT decoder fragmented escape sequences, while its WinCon decoder
largely returned `ERR`; neither decoder is used by the project-owned adapter.

### Native adapter and rendering backend

The adapter consumes key-down `KEY_EVENT_RECORD`s, honors `wRepeatCount`, and
maps arrows, Shift+Tab, PgUp/PgDn, Home/End, Tab, Enter, and Escape to the same
logical values used by the shared UI. Other nonzero `UnicodeChar` values remain
ordinary characters; uppercase A/B/C/D are never treated as arrows. UTF-16
surrogate pairs are combined. `WINDOW_BUFFER_SIZE_EVENT` becomes `KEY_RESIZE`,
so the existing `resize_term(0, 0)` and 50x15 fallback remain in use.

WinCon runs inside Windows Terminal. It detects `WT_SESSION`, writes Unicode
with `WriteConsoleW`, and uses Windows Terminal's VT output support for richer
colors. The packaged wide/UTF-8 build is compatible with Signalbox's
wide-string renderer, so no renderer adapter is needed for metadata such as
`Café`, `♥`, or block glyphs. Signalbox
continues to use ASCII `+`, `-`, and `|` structural borders for W2. Bold is
represented by bright foreground color; `A_DIM` is unavailable in this
32-bit-wide build and is effectively normal text. WinCon normally creates and
activates a separate console screen buffer, then restores the original buffer
and cursor during `endwin`/screen teardown.

`terminal_win32.c` still saves/restores the host modes and UTF-8 code pages.
The WinCon build is the default output backend: it uses `WriteConsoleW` for
reliable Unicode, integrates with Windows Terminal's VT output support for
richer colors, and provides predictable cursor and screen-buffer lifecycle
behavior. Input is independent of this selection. The VT output build remains
available only as a useful diagnostic comparison without enabling VT input.

### Known limitation

Live horizontal resizing in Windows Terminal may show temporary visual tearing
or artifacts while the window is actively being dragged. The TUI redraws
correctly once resizing stops. Vertical resizing and normal operation are
unaffected. This Windows Terminal behavior has been observed with both the
PDCursesMod WinCon and VT renderers.

Build and exercise the native target from PowerShell in Windows Terminal with:

```powershell
git pull origin develop
make clean
make WINDOWS_CURSES_BACKEND=wincon
$env:SIGNALBOX_DEBUG_KEYS=1
.\signalbox.exe --tui
Get-Content .\signalbox-keys.log
```

Confirm the diagnostic says `file_type=2`, `get_console_mode=yes`,
`peek_console_input=yes`, `handle_kind=console_buffer`, `vt_input=off`, and
`window_input=on`. Test arrows, Shift+Tab, PgUp/PgDn, Home/End, ordinary `j`,
`k`, `?`, Enter, Escape, and Tab;
then shrink, grow, maximize, restore, and cross the 50x15 minimum. Expected
special-key log entries have `status=KEY_CODE_YES` and their corresponding
`KEY_*` names, while ordinary characters remain `status=OK`. Also check all
themes, `NO_COLOR`, Unicode metadata/block rendering, login editing, and host
screen/cursor restoration. W2 does not add Windows audio playback, Credential
Manager, or Named Pipe control.

## W1 native compile foundation — complete

W1 adds a small `src/platform.c` boundary for UTF-8 configuration paths,
monotonic time, local time, and shutdown notification. Windows configuration
paths come from `FOLDERID_RoamingAppData` and resolve to
`%APPDATA%\Signalbox\config`, `account`, and `favorites`; conversion to UTF-8
happens inside the platform module. Unix continues to use
`$XDG_CONFIG_HOME/signalbox`.

GNU Make now detects `Windows_NT`, `MINGW*`, and `MSYS*`, emits
`signalbox.exe`, excludes ncurses from W1, and selects Windows-only terminal,
readline, and curses stubs. `--help` returns before terminal, settings,
credentials, audio, or network initialization. `--tui` reports that the TUI
is unavailable in this W1 build. The classic input stub exists only to keep the
real application linkable; authenticated playback and audio are not W1 claims.

In an MSYS2 UCRT64 shell:

```sh
pacman -S --needed make \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-curl \
  mingw-w64-ucrt-x86_64-json-c \
  mingw-w64-ucrt-x86_64-libgcrypt \
  mingw-w64-ucrt-x86_64-libao \
  mingw-w64-ucrt-x86_64-pdcurses
make clean all
make spectrum-test
./signalbox.exe --help
./signalbox.exe --help > help.txt
```

GCC brings the UCRT64 winpthreads dependency. W1 retains pthread calls and
links `-lpthread`. libao is retained only as a link-time dependency; Windows
device playback has not been validated. Credential Manager, durable account
writes, event/password subprocesses, audio/FIFO paths, and Named Pipe control
are unavailable. Unix `contrib/` tools remain Unix-only.

W1 was validated on Windows 11 using MSYS2 UCRT64 and MinGW-w64 GCC 16.2.0.
The native build produced an x86-64 PE32+ `signalbox.exe`; the native spectrum
test passed; direct and redirected `--help` calls exited 0; both orders of the
conflicting `--tui`/`--classic` flags produced the expected diagnostic and
exited 2; and explicit `--tui` produced the W1 unavailable diagnostic and
exited 1. MinGW links `-luuid` alongside `-lshell32` and `-lole32` for the
`FOLDERID_RoamingAppData` GUID used by Known Folder lookup.

## Executive summary

Signalbox can become a native `signalbox.exe` without giving up the TUI,
spectrum, station browser, history/upcoming, credentials, or classic mode. The
protocol, HTTP, JSON, UI model, command dispatch, station model, and almost all
PCM analysis are already portable. The portability work is concentrated in six
boundaries: build/dependency selection, terminal/input, threads and process
control, paths and durable files, audio output, and credentials. Keeping those
boundaries narrow avoids a forest of `_WIN32` branches.

Use **MSYS2 UCRT64 with MinGW-w64 GCC** for the first supported x64 build. It
has current native-Windows packages for the project's major libraries and
produces a PE executable that does not require an MSYS2 installation at run
time when the required MinGW DLLs are bundled. Use **PDCursesMod's WinCon backend**
inside Windows Terminal, initially retain winpthreads, and use the packaged libao
only as a W3 bootstrap if a runtime spike proves reliable. Put audio behind a
small interface before adopting a direct WASAPI backend (the preferred final
backend). Ship a ZIP containing the executable, dependency DLLs, licenses, and
notices.

The next milestone after W2 validation is **W3: Windows authenticated playback
+ audio backend bring-up**.

## Classification

The inventory uses these labels:

- **A — already portable**
- **B — compiler/runtime compatibility issue**
- **C — small platform wrapper needed**
- **D — major subsystem replacement needed**
- **E — Windows omission or fallback acceptable**
- **F — packaging-only concern**

## Toolchain decision

| Strategy | Dependencies and source impact | Artifact and maintenance | Verdict |
|---|---|---|---|
| MSYS2 UCRT64 + MinGW-w64 GCC | FFmpeg, curl, json-c, libgcrypt, libao, PDCursesMod, pkgconf, and winpthreads are available in one ABI-coherent ecosystem. The existing GNU Make/pkg-config build is close to usable. | Produces native PE/COFF. Bundle MinGW and library DLLs; users do not install MSYS2. Easy to reproduce on GitHub-hosted Windows runners. | **Primary** |
| MSYS2 CLANG64 / clang targeting Windows | The same ecosystem also publishes CLANG64 packages for major dependencies. Source compatibility should be similar to UCRT64 GCC. | Native executable and comparable DLL bundle. Useful as a later compiler-diversity job, but adds no initial portability advantage. | Secondary after GCC works |
| clang-cl | Good Windows API integration, but uses the MSVC command-line/ABI world. Unix flags, make assumptions, pkg-config discovery, and the dependency graph need more adaptation. | Native runtime and good diagnostics; dependency acquisition would likely move toward vcpkg or custom FFmpeg builds. | Do not lead with it |
| Visual Studio / MSVC | curl, json-c, FFmpeg, and PDCurses have Windows routes, but the complete current graph is less uniform; POSIX declarations and pthread-bearing public structs are immediate blockers. | Excellent native tooling and redistributable story, but highest W1 build-system and compatibility cost. | Long-term optional toolchain, not W1 |
| Cygwin | Broad POSIX compatibility including ncurses, pthreads, signals, and FIFOs minimizes source edits. | The result depends on the Cygwin runtime and behaves as a POSIX application hosted on Windows. Distribution and terminal semantics do not meet the native-product goal. | Development experiment only |
| WSL | Existing Linux build should work with little change. | Produces a Linux ELF program inside WSL, not a native `.exe`; users must install WSL. | Not a Windows release strategy |

The supported development shell may be MSYS2, but release binaries must not
depend on the MSYS POSIX runtime (`msys-2.0.dll`). Build and link only against
UCRT64/MinGW packages. Prefer the UCRT runtime over the older MSVCRT target.

## Source inventory and blocker matrix

| File/module | Finding | Class | Severity | Planned treatment |
|---|---|---:|---:|---|
| `src/main.c`, `src/main.h` | `unistd`, file descriptors, `select`, FIFO open/type check, termios includes, pthread type, `sigaction`, `SIGPIPE`, `isatty`, `fork`/`pipe`/`dup2`/`waitpid` password helper | C/D/E | Critical | Move input/control and shutdown notification behind platform APIs. Omit `password_command` on the first build with a clear diagnostic; later use explicit Windows process creation, not a shell emulation layer. |
| `src/player.c`, `src/player.h` | libao handle/API is embedded in player state; pthread mutexes/conditions and `sig_atomic_t` are exposed; `unistd`, `fcntl`, `arpa/inet`, and `sys/stat` are included. FFmpeg produces packed native-endian S16. | B/C/D | Critical | Keep FFmpeg pipeline. Bootstrap with winpthreads and tested libao if viable; introduce `SbAudio` and `SbThread` types before replacing the backend. Replace `htons` use with a portable endian helper or Winsock boundary. |
| `src/settings.c`, `src/settings.h` | `$HOME`, passwd database, XDG, `/`, tilde expansion, `access`, `setenv`, POSIX mkdir modes, `mkstemp`, `fchmod`, `fsync`, `rename`, and `unlink` | B/C | High | Centralize known-folder/path joining and durable replacement. Keep UTF-8 internally; use wide Windows filesystem calls. |
| `src/ui_renderer_curses.c` | Private ncurses API surface, wide strings/input, `wcwidth`, `localtime_r`, locale, colors, resize, `newterm`/`delscreen`, pthread notice lock | B/C | Critical | Build against PDCursesMod VT. Add tiny width/time/thread wrappers only where compatibility tests require them. Keep curses types private. |
| `src/ui_renderer.c`, `src/ui_renderer.h` | Model/render abstraction and classic renderer use standard C; time values only cross as `time_t` | A | Low | Retain. This is the seam that makes a Windows curses backend practical. |
| `src/ui.c` | `strings.h`/`strcasecmp`; event commands use `fork`, pipe, `execl`, and `waitpid`; pthread lock access | B/D/E | High | Compatibility wrapper for case folding. Disable event commands initially; later implement an explicit opt-in Windows subprocess adapter if demand justifies it. |
| `src/ui_act.c` | `unistd` include and direct pthread mutex/condition operations; otherwise action/protocol logic is portable | B/C | Medium | Thread wrapper or short-term winpthreads. Remove unused POSIX include if confirmed during implementation. |
| `src/ui_dispatch.c`, headers | Named-command mapping and dispatch are standard C | A | Low | Retain unchanged. |
| `src/ui_readline.c`, `.h` | Public `fd_set`, `select` over stdin/FIFO, POSIX `read`, ANSI editing sequences | C/D | High | Separate command event source from byte editing. On Windows, curses owns TUI input; classic input uses console/CRT APIs and control transport has its own waitable handle. |
| `src/terminal.c`, `.h` | termios raw mode and `SIGCONT` restoration | D | High | Unix implementation remains. Windows implementation saves/restores console modes; Signalbox owns input modes while the TUI is active. |
| `src/credential.c`, `.h` | Existing narrow backend abstraction is clean; current fallback is unavailable outside Apple/libsecret | C | Medium | Add a Windows-only implementation using Credential Manager; no call-site changes. |
| `src/spectrum.c`, `.h` | DSP and S16 ingest are platform-neutral; only `clock_gettime` and exposed pthread mutex are nonportable | B/C | Medium | Use monotonic-time and mutex wrappers. DSP should otherwise remain byte-for-byte unchanged. |
| `src/station_browser.c`, `.h` | Model is portable; `strings.h`/`strcasecmp`, POSIX mkdir modes, slash trimming, and replace/unlink persistence are not | B/C | Medium | Reuse paths and durable-file helpers; case-fold remains ASCII-compatible initially. |
| `src/libpiano/*` | Standard C plus curl/json-c/libgcrypt. `strdup` declarations depend on compiler mode. | A/B | Medium | Keep logic. Supply compatibility declarations/helpers only if UCRT compilation proves necessary. |
| `src/debug.*`, `src/config.h` | Standard I/O/environment and FFmpeg version macros | A | Low | Retain. |
| `contrib/*` | Shell, Perl, Ruby, Python, dmenu, `mkfifo`, `/tmp`, chmod, XDG, `nc`, and Unix executable conventions | E | Low | Document as Unix-only. Do not block Windows core on ports. |
| `Makefile` | `uname`, GNU shell functions, pkg-config, `-lpthread`, `.so`, symlinks, POSIX install/man/chmod conventions, and no `.exe` suffix | B/C | Critical | Keep GNU Make for W1 with one centralized Windows platform block and UCRT64 pkgconf. Suppress shared-lib/install targets on Windows. Revisit CMake only after behavior is proven. |
| `.github/workflows/build.yml` | Linux/macOS package/install and binary inspection only | F | Medium | W6 adds a pinned Windows runner and MSYS2 UCRT64 setup, tests, smoke test, dependency collection, and artifact upload. |

No uses of `poll`, `nanosleep`, `usleep`, `strtok_r`, `realpath`, `dirname`,
`basename`, `PATH_MAX`, or application-level `getopt` were found. `ssize_t`
appears in the password-helper path. There is no `SIGWINCH`; curses supplies
`KEY_RESIZE`. The source uses forward slashes mostly to construct config paths,
not to parse arbitrary user paths.

## TUI and terminal strategy

The curses renderer uses windows, borders/lines, batched refresh, attributes,
eight/256-color pairs, timed input, keypad translation, wide input/output,
and resize events. Notable calls are `newterm`, `set_term`, `delscreen`,
`wget_wch`, `mv[w]addnwstr`, `wtimeout`, `keypad`, `KEY_RESIZE`,
`use_default_colors`, `wnoutrefresh`, and `doupdate`. It deliberately keeps
all curses types inside `ui_renderer_curses.c`.

Use PDCursesMod rather than original PDCurses. MSYS2's current `pdcurses`
package is PDCursesMod and supplies separate VT, WinCon, and GUI libraries.
The WinCon backend preserves native console key events inside Windows Terminal
and uses wide output plus VT colors there. Test every API above; `newterm`,
default-color behavior, character width, resize notification, and screen-buffer
restoration remain compatibility edges. If PDCursesMod lacks a small ncurses
extension, isolate the shim next to the renderer rather than forking it.

Do not replace the renderer with direct Win32 drawing. The classic Win32
console screen-buffer API should not become an application-owned abstraction,
while a new bespoke VT renderer would duplicate curses layout, clipping, input,
and window lifecycle. PDCursesMod contains the WinCon-specific implementation.

Initial support target:

1. Windows Terminal running PowerShell or Command Prompt, UTF-8 enabled by the
   program at its OS boundary.
2. Modern conhost/cmd.exe as a best-effort sanity target using either VT mode
   or the WinCon backend.
3. Other terminal emulators only after capability testing.

Internally retain UTF-8. Enable virtual-terminal processing where the backend
requires it, set/restore input and output console modes, and convert OS-facing
strings through `MultiByteToWideChar`/`WideCharToMultiByte`. Heart, chevron,
and full-block glyphs remain runtime capability tests with existing ASCII
fallbacks. Do not infer Windows suitability from `TERM`; use console handle and
VT capability checks. `KEY_RESIZE` should replace any desire for `SIGWINCH`.
Keypad digits must continue through the renderer's numeric-jump state rather
than the classic byte-command path.

## Audio

The current audio contract is unusually small: open a live device (or raw
file), synchronously submit packed native-endian signed 16-bit interleaved PCM,
and close. The analyzer observes that exact PCM immediately before output, so
changing the output backend does not change its identity.

The MSYS2 UCRT64 repository does contain `libao` and `libao-4.dll`, making it a
reasonable low-cost W3 experiment. It must pass real-device enumeration,
default-device playback, pause/skip, repeated open/close, and long playback on
a clean VM before becoming a release dependency. Its old release cadence and
uncertain behavior across current Windows audio stacks make it unsuitable as
the architectural endpoint.

Backend comparison:

| Backend | Fit |
|---|---|
| libao | Smallest initial diff and preserves synchronous writes; acceptable bootstrap only after runtime proof. |
| WASAPI | Native, low-dependency, current Windows API; best final backend, but requires buffering/event handling behind the synchronous `write` contract. |
| waveOut | Simple and broadly compatible, but legacy and still needs buffer lifecycle code; reasonable fallback, not preferred final design. |
| SDL2 | Reliable cross-platform audio but adds a large general multimedia dependency to an otherwise terminal client. |
| PortAudio | Mature abstraction with another DLL and callback/blocking adaptation; useful if a second cross-platform backend is desired. |
| miniaudio | Lightweight and permissively licensed; easy to vendor, but adds vendored code and an internal callback/ring-buffer layer. Strong fallback if direct WASAPI cost is excessive. |

Introduce `platform_audio` with `open(format)`, `write(bytes)`, `pause(bool)` if
needed, and `close`. Keep FFmpeg format negotiation and spectrum ingest above
it. Short term: test libao. Long term: direct shared-mode WASAPI, with miniaudio
as the fallback decision before writing substantial custom device code.

## Spectrum and PCM

`spectrum.c` uses fixed-width integers, float math, a fixed FFT, no allocation
in the hot path, and reads `int16_t` samples already negotiated by FFmpeg as
packed native-endian S16. It has no file or terminal dependency and makes no
wire-endian assumption. Windows work is limited to replacing
`clock_gettime(CLOCK_MONOTONIC)` and the pthread mutex type/calls. Use
`QueryPerformanceCounter` through `SbPlatformMonotonicMs`; do not use wall
clock. Preserve the existing 80 ms cadence and run `spectrum-test` in W6.

## Threads and shutdown control

Current use is modest: player and audio-output thread creation/join, ordinary
mutexes, condition variables, and broadcasts/waits. There is no cancellation,
timed condition wait, thread-local storage, or detached-thread lifecycle.
MinGW's winpthreads is sufficient for the first native build.

C11 threads are not a useful migration target until compiler/library support is
proven across every supported toolchain. Direct Win32 threads are unnecessary.
Define a narrow `SbThread`, `SbMutex`, and `SbCondition` adapter after W1 or as
soon as a second toolchain is attempted. Avoid exposing platform thread types
from `player.h`, `spectrum.h`, and renderer state over the long term.

Signalbox currently handles `SIGINT`, ignores `SIGPIPE`, and reinstalls terminal
settings on `SIGCONT`. There is no `SIGTERM` or `SIGWINCH` handler. On Windows:

- use `SetConsoleCtrlHandler` for Ctrl+C, Ctrl+Break, close/logoff/shutdown;
- have it set an atomic shutdown flag or signal an event consumed by the main
  loop; do not call UI/audio cleanup from the handler;
- omit `SIGPIPE` handling because Winsock/curl reports errors normally;
- omit `SIGCONT`; console-mode restoration belongs to terminal lifecycle;
- translate native `WINDOW_BUFFER_SIZE_EVENT` records to `KEY_RESIZE`.

Call this small boundary `platform_control_event`, distinct from remote-control
IPC.

## Remote control and subprocesses

The Unix control path opens a caller-created FIFO read/write, verifies it with
`fstat`, and multiplexes it with stdin through `select`. POSIX FIFOs do not map
cleanly to Windows console handles. Windows named pipes offer correct local IPC,
ACLs, message/byte modes, and waitable overlapped I/O and should be the final
Windows transport. Use a stable name such as
`\\.\pipe\Signalbox\control-<user-scope>` and restrict its security descriptor
to the current user. Preserve the same command bytes at first.

Do not block W1–W4 on IPC. In W1 the Windows control endpoint is unavailable and
configuration of Unix `fifo` should produce a clear unsupported notice. W5 adds
the named-pipe server and a small PowerShell sender. Local TCP adds firewall and
authentication concerns; AF_UNIX availability varies by supported Windows
version and offers no advantage over a named pipe here.

`password_command` and event commands are also POSIX process pipelines. They
may be unavailable in the initial Windows build. A later implementation should
use `CreateProcessW`, explicit pipe inheritance, argument rules, timeouts, and
no implicit `cmd.exe /c`. This is separate from core playback.

## Paths, Unicode, time, and durable files

Use Windows Known Folders, not guessed environment-variable concatenation:

- roaming user configuration and small user-authored state:
  `%APPDATA%\Signalbox\config`, `account`, and `favorites`;
- machine-local cache, logs, and future runtime diagnostics:
  `%LOCALAPPDATA%\Signalbox\`;
- the named pipe has no filesystem path.

Keep explicit user paths accepted by configuration. Do not automatically copy
the legacy Unix `~/.config/pianobar` tree on Windows. If a user deliberately
points at such a tree, parsing CRLF is already supported.

Use UTF-8 for all core strings and convert only at Win32 boundaries. A path
module should obtain `FOLDERID_RoamingAppData`/`FOLDERID_LocalAppData` with
`SHGetKnownFolderPath`, join wide paths safely, and convert results. Avoid
`MAX_PATH`-sized fixed buffers and use long-path-capable absolute wide paths.
The TUI can keep multibyte/wide conversions locally. `wcwidth` behavior must be
tested because Windows CRT/PDCurses width rules are not identical to Unix.
Wrap `localtime_r` as a boolean local-time helper using `localtime_s` on Windows.

For `account`, favorites, and state writes, expose one durable replacement
helper:

1. create a uniquely named temporary file in the destination directory;
2. restrict its ACL to the user (directory ACL plus explicit protection for
   account metadata where appropriate);
3. flush stdio and call `FlushFileBuffers` on the handle;
4. close it and replace the destination with `ReplaceFileW`, or use
   `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` when
   no destination exists;
5. delete the temporary file on failure.

Windows replacement can fail while another process holds the destination, so
return a useful error and retain the old file. POSIX `chmod(0600)` has no exact
semantic equivalent; use ACLs rather than emulating mode bits.

## Credentials

The existing `SbCredentialLoad/Store/Delete/BackendAvailable` API is already
the correct boundary. Add a Windows branch in `credential.c` or a separately
compiled `credential_windows.c`:

- service remains `org.signalbox.pandora`;
- Credential Manager target name is `org.signalbox.pandora:<account>`;
- persist a `CRED_TYPE_GENERIC` credential with local-machine persistence;
- encode target and username as UTF-16; store the UTF-8 password bytes as the
  credential blob and validate its size against the API limit;
- `CredReadW` copies the blob into a new NUL-terminated allocation, then
  `CredFree` releases the OS object;
- `CredWriteW` receives only call-scoped buffers; clear temporary secret
  buffers after use;
- `CredDeleteW` maps not-found to `SB_CREDENTIAL_NOT_FOUND` and other Win32
  errors to `SB_CREDENTIAL_ERROR` without logging secrets.

Use exact account keys; do not case-fold email addresses silently. The account
metadata file continues to identify which credential to request and never
contains the password.

## Dependency and packaging inventory

Package names below are verified against the MSYS2 UCRT64 package repository;
the exact transitive DLL set must be generated from the final executable, not
copied from this planning table.

| Dependency | UCRT64 package / risk | Linking and licensing note |
|---|---|---|
| FFmpeg/libavcodec, format, util, filter | `mingw-w64-ucrt-x86_64-ffmpeg`; available but has a large codec-dependent DLL graph | Prefer dynamic libraries. The repository build's enabled features/license may be broader than Signalbox needs; record exact configuration and comply with LGPL/GPL obligations. |
| libcurl | `mingw-w64-ucrt-x86_64-curl-winssl` is preferred to reduce TLS runtime dependencies; base `curl` variants also exist | MIT; dynamic. Verify the selected `.pc` file resolves Schannel and does not mix ABI variants. |
| json-c | `mingw-w64-ucrt-x86_64-json-c` | MIT; dynamic or static feasible. |
| libgcrypt | `mingw-w64-ucrt-x86_64-libgcrypt`, plus libgpg-error | LGPL family; dynamic is the conservative default. |
| curses | `mingw-w64-ucrt-x86_64-pdcurses` (PDCursesMod; VT/WinCon libraries) | Public-domain project; link the explicit static WinCon archive for the W2 prototype. |
| libao | `mingw-w64-ucrt-x86_64-libao` | Package metadata reports GPL; use dynamically only for a bootstrap and verify runtime backend and redistribution obligations. |
| pthreads | MinGW toolchain/winpthreads runtime | Bundle its runtime DLL when dynamically linked; later hide it behind project types. |
| pkg-config | `mingw-w64-ucrt-x86_64-pkgconf` | Build-time only. |
| libsecret | Not used on Windows | No package/runtime requirement. |
| Security.framework | Apple-only | No Windows requirement. |
| Credential Manager, Known Folders, WASAPI | Windows SDK | System DLLs; do not bundle. |

Do not attempt an all-static release first. Static FFmpeg/curl/crypto builds can
change license obligations and make security updates harder; static system
runtime choices also need deliberate review. Prefer a reproducible dynamic DLL
bundle, with system Windows libraries left unbundled. Before release, inventory
licenses from the exact package versions and FFmpeg build configuration. This
is engineering guidance, not legal advice.

Expected artifact:

```text
Signalbox-Windows-x64.zip
  signalbox.exe
  *.dll                    # exact non-system runtime closure
  README-Windows.txt
  LICENSE
  THIRD-PARTY-NOTICES.txt
```

Validate it in a clean Windows VM with no MSYS2, Cygwin, WSL, Visual Studio,
Python, or package manager installed.

## Build-system plan

For W1, preserve GNU Make and add a single platform selection block: `.exe`
suffix, PDCursesMod library choice, Windows credential/system libraries when
introduced, and exclusion of Unix install/shared-library targets. Continue
using UCRT64 `pkgconf`; avoid repeated `uname` probes. This is the least
disruptive route to evidence.

After W3, reassess CMake versus Meson using actual packaging needs. CMake has
the strongest Visual Studio/vcpkg and Windows-user familiarity; Meson is tidy
for pkg-config-heavy C but adds another tool. A second build system before the
platform boundaries settle would duplicate unstable logic. If MSVC becomes a
supported compiler, CMake is the likely eventual primary build description.

## Proposed platform APIs

Names are sketches; W1 should implement only what compilation requires.

```c
/* paths and durable files */
bool SbPlatformConfigPath(const char *leaf, char **utf8_path);
bool SbPlatformLocalDataPath(const char *leaf, char **utf8_path);
bool SbPlatformEnsureParent(const char *utf8_path);
bool SbPlatformReplaceFile(const char *utf8_path,
        bool (*writer)(FILE *, void *), void *context);

/* time */
uint64_t SbPlatformMonotonicMs(void);
bool SbPlatformLocalTime(time_t value, struct tm *result);
void SbPlatformSleepMs(unsigned int milliseconds);

/* terminal and shutdown */
bool SbPlatformTerminalProbe(SbTerminalCapabilities *out);
bool SbPlatformTerminalEnter(bool tui);
void SbPlatformTerminalLeave(void);
bool SbPlatformShutdownRequested(void);

/* threads (initially backed by pthreads everywhere) */
bool SbThreadCreate(SbThread *, void *(*entry)(void *), void *);
void SbThreadJoin(SbThread *);
/* plus mutex/condition init, lock, wait, broadcast, destroy */

/* audio */
bool SbAudioOpen(SbAudio *, unsigned rate, unsigned channels, SbSampleFormat);
bool SbAudioWrite(SbAudio *, const void *pcm, size_t bytes);
void SbAudioClose(SbAudio *);

/* local command transport */
bool SbControlOpen(SbControl *);
SbControlResult SbControlPoll(SbControl *, unsigned timeout_ms, unsigned char *);
void SbControlClose(SbControl *);
```

Credentials should continue to use `credential.h`; creating a second generic
credential API would add no value. Keep implementations in a small
`src/platform/` directory or in platform-specific translation units selected by
the build. Do not put `_WIN32` checks in callers.

## Milestone roadmap and effort

Effort is relative and deliberately broad; it includes code, tests, and docs,
not release support time.

| Phase | Exit criterion | Effort |
|---|---|---|
| W0 | Audited source and dependencies; architecture and risks recorded | Small (this document) |
| W1 — native compile (complete) | UCRT64 builds `signalbox.exe`; native spectrum and CLI smoke tests pass. TUI, audio, FIFO, event/password commands, and credentials remain unavailable at this milestone boundary. | Medium |
| W2 — PDCursesMod VT renderer + Windows Terminal TUI bring-up | PDCursesMod VT TUI starts in Windows Terminal; login/stations, keys, resize, colors, restoration, narrow fallback, history/upcoming, and spectrum rendering work. | Large |
| W3 — playback | Authenticated FFmpeg decode and stable audio; pause/next/station changes; real PCM drives the analyzer without stutter. Decide libao bootstrap versus WASAPI/miniaudio from spike data. | Large |
| W4 — credentials | Credential Manager remember, auto-login, stale recovery, and forget flows pass. | Small–medium |
| W5 — control | Per-user named pipe and a PowerShell sender preserve command semantics. | Medium |
| W6 — CI | Pinned `windows-2025` (or then-current pinned supported image) UCRT64 build, spectrum tests, `--help`, DLL scan, and artifact upload; no Pandora secret. | Medium |
| W7 — release | Clean x64 ZIP, notices/license audit, dependency closure, and fresh-VM tests in Windows Terminal/PowerShell plus cmd.exe sanity check. | Medium–large |

Audio follows TUI in this sequence because W2 can be exercised without live
playback and exposes terminal risk early. W3 must still test the analyzer with
real decoded PCM, not a synthetic display path.

## Prioritized risk register

| Risk | Impact | Likelihood | Mitigation |
|---|---:|---:|---|
| PDCursesMod differences in wide APIs, `newterm`, resize, or default colors | High | Medium | Test WinCon in Windows Terminal; keep the VT comparison switch plus renderer seam and ASCII/mono fallbacks. |
| FFmpeg package has a large DLL closure or unexpected GPL-enabled configuration | High | High | Record exact build config, dynamically bundle only required closure, create notices, consider a purpose-built minimal FFmpeg later. |
| libao default output fails or behaves poorly on modern Windows | High | Medium–high | Time-box W3 spike; keep analyzer above audio seam; switch to WASAPI/miniaudio without changing decode/UI. |
| Console input cannot be multiplexed with named-pipe handles like Unix `select` | High | High | Separate TUI/classic input from IPC and use a waitable event/worker queue rather than emulating file descriptors. |
| Ctrl/close handler races with player shutdown | High | Medium | Handler only signals atomic/event state; normal main-loop teardown owns joins and restoration. |
| pthread types spread and impede MSVC later | Medium | High | Winpthreads for W1; project thread types before supporting a second compiler. |
| UTF-8 width differs for heart/block glyphs | Medium | Medium | Wide PDCursesMod build, explicit UTF-8 console setup, runtime glyph/width checks, ASCII fallback. |
| Windows replace semantics fail with open readers/AV software | Medium | Medium | Same-directory unique temp, flush, `ReplaceFileW`/`MoveFileExW`, retain old file and surface error. |
| DLLs from MSYS/UCRT64 environments are accidentally mixed | High | Medium | Use one UCRT64 prefix, automated dependency scan, clean-VM test, reject `msys-2.0.dll`. |
| Named-pipe endpoint is accessible to other users | High | Low–medium | Explicit current-user ACL and threat-model tests. |
| Static/dynamic licensing assumptions are wrong | High | Medium | Default dynamic, archive exact source/notices/config, obtain project-specific legal review before release. |

## Features intentionally deferred

The initial Windows release does not need ports of `headless_signalbox`,
`headless_pianobar`, `remote.sh`, `addshared.sh`, TLS fingerprint shell tooling,
dmenu helpers, or the shell/Perl/Ruby/Python event examples. The concepts can
later become built-in behavior or PowerShell examples. Unix FIFO compatibility,
`SIGCONT`, Unix mode bits/man-page installation, and arbitrary `/bin/sh` event
or password commands are likewise not first-release requirements. Classic
interactive operation remains a core goal; every Unix automation helper does
not.

## Validation completed through W1

- Inspected every `src/*.c`/`.h` module, `src/libpiano`, the Makefile, current
  Linux/macOS CI, documentation, tests, and all `contrib` entries.
- Searched for POSIX headers, processes, signals, FIFO/file descriptors,
  filesystem mutation, locale/Unicode, time, thread, and string APIs.
- Enumerated the curses calls used by the renderer.
- Verified current MSYS2 UCRT64 package records for FFmpeg, curl variants,
  json-c, libgcrypt, libao, PDCursesMod, and pkgconf.
- On Windows 11 with MSYS2 UCRT64 and MinGW-w64 GCC 16.2.0, built the native
  x86-64 PE32+ `signalbox.exe` and passed `spectrum-test`.
- Verified direct and redirected `--help` calls exit 0, conflicting mode flags
  in either order exit 2, and the explicit W1 `--tui` stub exits 1 with its
  documented unavailable diagnostic.

## Open questions

1. Does MSYS2's PDCursesMod VT library implement the exact `newterm` and wide
   API behavior Signalbox uses, and how does it report resize in Windows
   Terminal versus conhost?
2. Does the packaged libao choose a usable Windows driver and remain stable
   across default-device changes? Its package contents alone cannot answer
   runtime behavior.
3. What exact DLL closure and license configuration results from the selected
   FFmpeg and curl-winssl packages at release time?
4. Should W3 go directly to WASAPI if the libao spike finds any glitch, or
   first adopt miniaudio to reduce schedule risk?
5. Is classic-mode Windows subprocess support sufficiently valuable to justify
   secure `CreateProcessW` argument/config design, or should those settings stay
   explicitly unsupported?
6. What minimum Windows version will releases promise? Windows Terminal-first
   should be explicit even if modern conhost receives best-effort support.

## Primary references checked

- [MSYS2 environments](https://www.msys2.org/docs/environments/)
- [MSYS2 UCRT64 package index](https://packages.msys2.org/packages/)
- [MSYS2 PDCursesMod package](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-pdcurses)
- [MSYS2 libao package](https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-libao)
- [PDCurses manual](https://pdcurses.org/docs/MANUAL.html)
- [Microsoft console virtual-terminal sequences](https://learn.microsoft.com/windows/console/console-virtual-terminal-sequences)
- [Microsoft console control handlers](https://learn.microsoft.com/windows/console/setconsolectrlhandler)
- [Microsoft Credential Manager API](https://learn.microsoft.com/windows/win32/api/wincred/)
- [Microsoft Known Folders](https://learn.microsoft.com/windows/win32/shell/known-folders)
- [Microsoft WASAPI](https://learn.microsoft.com/windows/win32/coreaudio/wasapi)
- [GitHub-hosted runner images](https://github.com/actions/runner-images)
