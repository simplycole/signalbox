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
- [x] Present bounded session history, expiring status/error notices, and
  configured key help. Upcoming tracks, reconnect state, and interactive
  prompts remain deferred.
- [ ] Add semantic green, amber, monochrome, neutral, and high-contrast themes
  with `NO_COLOR` degradation and reduced motion.
- [ ] Verify classic, FIFO, event-command, and headless compatibility before
  making the TUI the interactive default.
- [ ] Evaluate optional mouse and terminal artwork only after the text TUI and
  packaging are stable.

## Phase 3 — macOS integration

- [ ] Support media keys.
- [ ] Publish metadata through macOS Now Playing.
- [ ] Add native notifications.
- [ ] Store credentials securely with Keychain.
- [ ] Maintain explicit Apple Silicon validation.

## Phase 4 — Linux integration

- [ ] Implement MPRIS control and metadata.
- [ ] Add desktop notifications.
- [ ] Integrate with secret-service/keyring providers.
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

Priorities may change as Pandora compatibility, platform behavior, and
maintainer capacity evolve. Any phase may include documentation, testing,
accessibility, reliability, or security work needed to make its features
maintainable.
