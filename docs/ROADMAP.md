# Roadmap

Signalbox is being developed in deliberate phases. Completed boxes represent
work evidenced in the repository or the recorded baseline; unchecked items are
plans, not promises of a date or release.

## Phase 0 — Upstream baseline

- [x] Preserve pianobar's Git history.
- [x] Preserve an `upstream` remote for the canonical project.
- [x] Establish the known-good tag `upstream-baseline-2026-09-01`.
- [x] Document a working modern macOS and Apple Silicon baseline.
- [x] Verify compile, launch, authentication, station retrieval, and playback.

## Phase 1 — Signalbox identity and build cleanup

- [x] Establish the Signalbox project identity and initial documentation.
- [x] Rename the executable in a dedicated, compatibility-aware change.
- [ ] Clarify build dependencies and supported feature combinations.
- [ ] Clean up compiler warnings without obscuring behavior changes.
- [x] Add repeatable build validation for supported macOS and Linux systems.
- [ ] Add repeatable runtime compatibility tests for macOS and Linux systems.
- [x] Define the transition policy for inherited configuration paths, with a
  legacy pianobar fallback and no automatic file migration.
- [x] Define the transition policy for remaining inherited tooling names.

## Phase 2 — Modern terminal UI

- [x] Define the TUI architecture and select `ncursesw` as the preferred core
  renderer.
- [ ] Remove direct terminal output from playback/service code while preserving
  classic output through structured notices and state snapshots.
- [x] Introduce named commands and separate key/FIFO decoding from action
  execution without changing inherited bindings.
- [x] Establish a minimal read-only transient UI model over canonical current
  station, song, and playback progress state.
- [x] Introduce the renderer lifecycle and classic backend; TUI and headless
  mode selection remain future work.
- [x] Add an opt-in `ncursesw` skeleton with alternate-screen lifecycle,
  resize handling, responsive/minimum-size layouts, model-driven playback
  content, status, and configured quit/help input.
- [x] Add a responsive station browser with selection, scrolling, and active
  station highlighting.
- [x] Route TUI station activation and prompt-free pause/resume, next, love,
  ban, and quit controls through named commands.
- [x] Add now-playing metadata, playback/rating state, volume, and low-frequency
  progress updates.
- [x] Present full-session in-memory history, a height-responsive semantic
  RECENT view, and a newest-first scrollable history modal with preserved song
  actions. History is session-only and is freed rather than persisted. Add expiring status/error notices,
  request retry/recovery state, and configured key help.
- [x] Add a responsive, display-only UPCOMING pane backed by the fetched
  playlist, useful Enter actions, native song-details and explanation modals,
  fixed-width ``♥``/``</3`` ratings, and subtle RECENT focus markers. Queue
  promotion remains deferred because no safe reorder API exists.
- [x] Add native TUI create/search, rename, and delete prompts with bounded
  editing, confirmation, and result selection; retain classic startup
  credential and account-settings prompts.
- [x] Add native TUI QuickMix editing, hierarchical genre selection, shared
  station entry, create-from-song and bookmark choices, history actions, and
  station seed/feedback/mode management with destructive confirmations.
- [x] Add phosphor, amber, monochrome, and neutral themes with `NO_COLOR`
  degradation and no blink/animation. Semantic roles now color stations,
  metadata, progress, status, help, prompts, and footer hints with standard
  eight-color fallbacks and restrained 256-color refinements. A dedicated
  high-contrast theme remains future work.
- [x] Add the C5 large-library station view: default A-Z, original and
  favorites-first modes, local ID-based favorites, incremental filtering,
  compact count/sort headers, and numeric jump with temporary row numbers.
  Numeric jump uses `#` and isolates number-row/keypad input from normal command
  dispatch, and valid confirmation tunes immediately; `G` remains Genres.
  RECENT includes rating markers, album, and snapshotted duration in a compact
  left-packed one-line layout with a width-driven two-line fallback. Tab focus
  makes the full main-pane history directly navigable without removing its modal.
  Recent-activation ordering remains deferred.
- [x] Add native masked TUI startup login, optional remembered-account metadata,
  automatic secure-store login, stale-credential recovery, and a narrow
  cross-platform credential adapter while preserving classic/plaintext compatibility.
- [x] Add a real decoded-PCM spectrum analyzer / visual EQ display with twelve
  approximate logarithmic bands and an eight-band medium-width fallback,
  Hann-window FFT analysis, attack/release
  smoothing, peak hold, responsive right-pane placement, mono/ASCII fallback,
  and config/CLI/runtime disable controls. It is visual-only and does not apply
  audio equalization; a future playback EQ would be a separate feature.
- [x] Make the TUI the default on supported interactive terminals, retain
  explicit `--classic`, and preserve classic fallback for headless execution.
- [ ] Evaluate optional mouse and terminal artwork only after the text TUI and
  packaging are stable.

## Phase 3 — macOS integration

- [ ] Support media keys.
- [ ] Publish metadata through macOS Now Playing.
- [ ] Add native notifications.
- [x] Store credentials securely with Keychain.
- [ ] Maintain explicit Apple Silicon validation.

## Phase 4 — Linux integration

- [ ] Implement MPRIS control and metadata.
- [ ] Add desktop notifications.
- [x] Integrate with secret-service/keyring providers through optional
  libsecret support; runtime validation across selected desktops remains.
- [ ] Validate builds and runtime behavior across selected distributions.

## Phase 5 — Packaging and releases

- [ ] Provide a Homebrew formula.
- [ ] Provide appropriate Linux packages.
- [x] Add automated CI builds and checks.
- [ ] Publish reproducible release artifacts.
- [ ] Document versioning and the release process.

## Phase 6 — Extended features

- [ ] Add optional terminal album artwork where capability and accessibility
  permit it.
- [ ] Present richer track, album, artist, and station metadata.
- [ ] Provide stable scripting and headless control interfaces.
- [ ] Consider external integrations only where technically and legally
  appropriate.

## Native Windows direction

- [x] Complete the W0 source/dependency audit and define the platform boundaries,
  toolchain, packaging policy, risks, and staged implementation plan in
  [`WINDOWS.md`](WINDOWS.md).
- [ ] W1: produce a native MSYS2 UCRT64 `signalbox.exe` and pass a `--help`
  smoke test without requiring TUI/audio parity.
  The source/build foundation is implemented and passes macOS regression and
  CLI smoke tests; native UCRT64 production and execution are still awaiting a
  Windows environment.
- [ ] W2: support the full PDCursesMod VT TUI in Windows Terminal.
- [ ] W3: support stable native audio playback while preserving PCM-driven
  spectrum analysis.
- [ ] W4: add a Windows Credential Manager backend behind the existing
  `CredReadW`, `CredWriteW`, and `CredDeleteW` interface boundary.
- [ ] W5: add per-user Windows Named Pipe command control.
- [ ] W6: add Windows build, spectrum test, smoke test, and artifact CI.
- [ ] W7: ship and fresh-VM validate a self-contained x64 ZIP.

Priorities may change as Pandora compatibility, platform behavior, and
maintainer capacity evolve. Any phase may include documentation, testing,
accessibility, reliability, or security work needed to make its features
maintainable.
