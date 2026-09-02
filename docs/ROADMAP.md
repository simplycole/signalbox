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
- [ ] Add repeatable compatibility tests for supported macOS and Linux systems.
- [x] Define the transition policy for inherited configuration paths, with a
  legacy pianobar fallback and no automatic file migration.
- [x] Define the transition policy for remaining inherited tooling names.

## Phase 2 — Modern terminal UI

- [ ] Add an interactive full-screen TUI.
- [ ] Add a station pane and station browser.
- [ ] Add a now-playing pane and playback progress.
- [ ] Add consistent keyboard navigation.
- [ ] Present playback history and upcoming tracks.
- [ ] Present status, reconnect attempts, and errors clearly.
- [ ] Handle terminal resize correctly.
- [ ] Support configurable terminal themes.
- [ ] Provide reduced/non-animated behavior where appropriate.

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
- [ ] Add automated CI builds and checks.
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
