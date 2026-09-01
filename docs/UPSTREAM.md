# Upstream and provenance

Signalbox descends directly from **pianobar**, created and maintained by
Lars-Dominik Braun and its contributors.

- Upstream project: <https://github.com/PromyLOPh/pianobar>
- Signalbox repository: `simplycole/signalbox`
- Local remote convention: `origin` is Signalbox; `upstream` is pianobar
- Known-good boundary: `upstream-baseline-2026-09-01`

## Preserved history

The repository retains pianobar's Git history rather than importing a source
snapshot. That history records authorship, intent, and the evolution of
compatibility-sensitive code. It also keeps individual changes traceable and
makes future comparisons or selective integrations from upstream practical.

The baseline tag marks the point from which Signalbox-specific development
begins. It must not be moved or recreated to point at a later commit.

## Verified baseline

The tagged baseline was verified with:

| Component | Verified value |
| --- | --- |
| Operating system | macOS 26.6.2 |
| Architecture | Apple Silicon / arm64 |
| Package manager | Homebrew 6.0.21 |
| Compiler | Apple Clang 21.0.0 |
| Media stack | FFmpeg 9.0.1 |

The verification established that the unmodified upstream application could:

1. compile successfully;
2. launch from the source tree;
3. authenticate to Pandora;
4. retrieve the authenticated account's station list; and
5. play audio.

This is a point-in-time validation, not a promise that every account, service
response, or platform works. Signalbox has not yet recorded equivalent Linux
validation.

## Incorporating future upstream work

The `upstream` remote should continue to track the canonical pianobar
repository. Before bringing in a future change:

1. fetch upstream without rewriting Signalbox history;
2. review the change and its surrounding upstream context;
3. identify any protocol, playback, build, or user-facing compatibility impact;
4. integrate with a merge or a clearly attributed cherry-pick, as appropriate;
5. resolve conflicts deliberately rather than preferring one tree wholesale;
6. build and test the affected paths on supported platforms; and
7. record provenance in the commit message and relevant documentation.

Large upstream syncs should remain separate from Signalbox feature work. The
baseline tag and published history must not be rebased, squashed, or rewritten.

## License and attribution

The inherited code is licensed under the MIT License in `COPYING` and in source
headers. This repository must retain the copyright and permission notices in
copies or substantial portions of that code. New work must not remove or obscure
upstream attribution, and reused upstream changes should preserve their authorship
in Git history and commit metadata.

If code under another compatible license is proposed later, its terms and
notices must be reviewed and documented before integration. No relicensing of
the inherited pianobar code is implied by the Signalbox name.
