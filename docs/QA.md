# TUI supervised authenticated QA

This checklist prepares a manual session against a real Pandora account. It
does not authorize automated account changes. Perform the read-only sections
first, and stop before any mutation you do not explicitly want on the account.

## Safety legend and action inventory

- **SAFE — read-only/local:** browse stations, move the selection, activate a
  station, pause/resume, next, volume changes, Now Playing and progress, help,
  theme and `NO_COLOR` rendering, resize, connection/retry display, song info,
  explanations, history, upcoming tracks, search-result browsing, genre
  browsing, and opening/cancelling any modal before submission.
- **MUTATES ACCOUNT — low-risk:** love (including returning a song to neutral),
  tired/shelf, create a station, add a shared or genre station, create from a
  song/artist, rename, edit QuickMix membership, bookmark song/artist, add
  music (a seed), change station mode, and remove feedback. “Low-risk” means
  usually limited or reversible; it does **not** mean safe/read-only.
- **MUTATES ACCOUNT — destructive:** ban, delete a station, and remove an
  artist/song/station seed. These remove content or personalization and may be
  difficult or impossible to reconstruct exactly.

The TUI uses configured key bindings. The keys named below are defaults (`?`,
`+`, `-`, `a`, `c`, `d`, `g`, `h`, `j`, `n`, `p`, `r`, `u`, `x`, `b`, `=`,
`v`, `(`, `)`, and `^`); use the help overlay as the authority if the config
remaps them.

## Diagnostic capture

Use the metadata-only trace for a supervised run:

```sh
SIGNALBOX_DEBUG_TUI=1 ./signalbox --tui 2>signalbox-tui.log
```

It records renderer lifecycle, modal titles, command names, selection
indices/counts, model generations, activity transitions, and station-list
counts. It does not record credentials, tokens, station names/IDs, track
metadata, config contents, or request/response bodies. Do not enable
`PIANOBAR_DEBUG` for authenticated QA: its network mode can print protocol
response bodies. Review the diagnostic log before sharing it anyway.

## Recommended order

1. Complete A–H and the browse/cancel portion of I (zero account mutations).
2. Create one disposable test station in I.
3. Rename only that station in J.
4. Exercise QuickMix with the disposable station in K, then restore membership.
5. Test bookmarks only if an enduring library change is acceptable.
6. Test seeds, feedback, and station mode only with explicit consent and notes
   about the original state.
7. Delete the disposable station in N last.
8. Complete reconnect and terminal-restoration checks in O–P.

## A. Startup

- **Action:** Run `./signalbox --tui`; authenticate through the existing config,
  password helper, or masked prompt. **Expected:** curses starts, login and
  station retrieval status is visible, and playback begins on autostart or the
  first station. **Account:** SAFE/read-only. **Real account:** Yes. **Cleanup:**
  None; confirm no email/password appears in the screen or diagnostic log.
- **Action:** Run `./signalbox --classic` separately. **Expected:** classic mode
  still starts and behaves normally. **Account:** SAFE/read-only. **Real
  account:** Yes. **Cleanup:** Quit normally.

## B. Station browser

- **Action:** Use arrows or `j`/`k`, Home/End, and Page Up/Down; press `s` once.
  **Expected:** selection and scrolling remain in bounds; `s` explains that
  arrows plus Enter tune a station. **Account:** SAFE/read-only. **Real
  account:** Yes. **Cleanup:** None.
- **Action:** Press Enter on a different station. **Expected:** a switching
  notice appears, the active marker changes after retrieval, and the selected
  station plays. **Account:** SAFE/read-only. **Real account:** Yes. **Cleanup:**
  Return to the original station if desired.

## C. Playback

- **Action:** Press `p` (or Space) twice. **Expected:** audio pauses and resumes;
  the state text follows. **Account:** SAFE/read-only. **Real account:** Yes.
  **Cleanup:** Leave playback resumed.
- **Action:** Press `n`. **Expected:** the current track stops and the next
  queued track starts without changing station. **Account:** SAFE/read-only
  (Pandora may count a skip under its service rules, but no library/profile
  object is edited). **Real account:** Yes, if one skip is acceptable.
  **Cleanup:** None.
- **Action:** Open song info/explanation only if exposed by configured bindings.
  **Expected:** readable data or a clear failure notice. **Account:**
  SAFE/read-only. **Real account:** Yes. **Cleanup:** Cancel/close.

## D. Now Playing/progress

- **Action:** Observe a track for at least two refreshes and through one track
  transition. **Expected:** artist/title/album/station are correct; elapsed time
  advances, duration is stable, progress is bounded, and the old song enters
  recent history once. **Account:** SAFE/read-only. **Real account:** Yes.
  **Cleanup:** None.

## E. Volume

- **Action:** Press `(`, `)`, then `^`. **Expected:** volume changes by 1 dB,
  Now Playing reflects it, and reset returns to 0 dB. **Account:** SAFE/local.
  **Real account:** Yes. **Cleanup:** Restore the preferred level.

## F. Resize

- **Action:** Resize across large, medium, small, and below 50x15 while the main
  view, help, a list, a text prompt, and a confirmation are open. **Expected:**
  no crash or stale window; the minimum-size message appears; restoring size
  redraws content and preserves/cancels modal state predictably. **Account:**
  SAFE/read-only if all mutation modals are cancelled. **Real account:** Yes.
  **Cleanup:** Press Esc in every mutation modal.

- **Action:** With a filter and non-default sort active, press `#`, `4`, and
  Enter using the number row; repeat with the physical numeric keypad. Also try
  `0`, an out-of-range number, empty Enter, a long digit sequence, Backspace,
  Delete, and keypad navigation with Num Lock changed. **Expected:** valid input
  selects and immediately tunes that one-based row in the visible list with no
  second Enter; invalid input reports out of
  range; unsupported keypad codes remain in jump mode and never quit or invoke
  another command. **Account:** SAFE/local. **Real account:** Yes.

## G. Themes / NO_COLOR

- **Action:** Launch separate sessions with `--theme phosphor`, `amber`, `mono`,
  and `neutral`, then `NO_COLOR=1 ./signalbox --tui`. **Expected:** each layout
  remains legible; mono/`NO_COLOR` retains selection and warning meaning via
  text/attributes. **Account:** SAFE/local. **Real account:** Yes. **Cleanup:**
  None.

## H. History/upcoming

- **Action:** Accumulate more history than fits in RECENT. Press `Tab`, then use
  arrows/`j`/`k`, Page Up/Page Down, and Home/End; resize through roughly 90,
  143, and 219 columns and back to a narrow layout. **Expected:** RECENT focus
  is visible, every session entry is reachable, selection remains valid, a new
  track does not yank an actively browsed selection, wide rows stay left-packed,
  and wrapped rows are complete. Enter opens the existing history action menu.
  **Account:** SAFE/read-only if actions are cancelled. **Real account:** Yes.
- **Action:** After several tracks, press `h`, browse, open Song information,
  and cancel other history actions. **Expected:** session history is ordered,
  selection is stable, info matches the chosen track, and nested Esc returns
  cleanly. **Account:** SAFE/read-only only when Create/Bookmark are cancelled.
  **Real account:** Yes. **Cleanup:** None.
- **Action:** Press `u` and browse the queue. **Expected:** only upcoming tracks
  appear and Esc closes without changing the queue. **Account:** SAFE/read-only.
  **Real account:** Yes. **Cleanup:** None.

## I. Search/create

- **Action:** Press `c`, enter a generic term such as `instrumental radio`,
  browse results, then Esc. Repeat with `g` (genre hierarchy), `j` (shared ID
  prompt), and `v` (current/history song source), cancelling before submission.
  **Expected:** results/lists display, nested genre Esc backs out one level, and
  cancellation creates nothing. **Account:** SAFE/read-only while cancelled.
  **Real account:** Yes. **Cleanup:** Verify station count is unchanged.
- **Action:** With explicit consent, use `c` to create one station from a
  harmless generic result and record its exact name. **Expected:** success
  notice and one new browser entry. **Account:** MUTATES ACCOUNT — low-risk.
  **Real account:** Only with consent. **Cleanup:** Treat it as the disposable
  station for J/K/M/N; do not create it automatically.

## J. Rename

- **Action:** Select the disposable station, press `r`, cancel once, then rename
  it to an unmistakable temporary name. **Expected:** cancel preserves the
  name; submit updates exactly one entry without losing selection/playback.
  **Account:** MUTATES ACCOUNT — low-risk. **Real account:** Only on disposable
  data. **Cleanup:** Record the final temporary name for deletion.

## K. QuickMix

- **Action:** Select QuickMix, press `x`, toggle the disposable station, then
  Esc. Reopen and verify canonical membership was unchanged. **Expected:** Esc
  discards scratch changes. **Account:** SAFE/read-only because no save occurs.
  **Real account:** Yes. **Cleanup:** None.
- **Action:** With consent, save one membership change, reopen to verify it,
  then restore and save the original membership. **Expected:** successful save
  persists; a failed save rolls the visible flags back. **Account:** MUTATES
  ACCOUNT — low-risk. **Real account:** Only with the original state recorded.
  **Cleanup:** Restore the original membership before proceeding.

## L. Bookmark

- **Action:** Press `b`, inspect Song/Artist choices, then Esc. **Expected:** no
  request or success notice. **Account:** SAFE/read-only while cancelled. **Real
  account:** Yes. **Cleanup:** None.
- **Action:** Optionally bookmark a deliberately chosen song or artist.
  **Expected:** one success notice and no playback interruption. **Account:**
  MUTATES ACCOUNT — low-risk. **Real account:** Only if an enduring bookmark is
  acceptable. **Cleanup:** The TUI has no bookmark-removal flow; use Pandora’s
  normal account UI if removal is desired.

## M. Seeds/feedback

- **Action:** On the disposable station press `a`, search and cancel; press `=`,
  browse each available seed/feedback/mode list, decline confirmations, and
  cancel mode selection. **Expected:** fetched lists remain usable, nested
  cancellation returns cleanly, and nothing changes. **Account:** SAFE/read-only
  while cancelled/declined. **Real account:** Yes. **Cleanup:** None.
- **Action:** Only with explicit consent, add one seed or change mode after
  recording the original state. **Expected:** success and continued/restarted
  playback as appropriate. **Account:** MUTATES ACCOUNT — low-risk. **Real
  account:** Prefer disposable data. **Cleanup:** Restore mode; remove the added
  seed only if accepting the destructive action.
- **Action:** Only with explicit consent, remove a seed or feedback item after
  selecting it and accepting the default-No confirmation. **Expected:** exactly
  the chosen item is removed or a clear error is shown. **Account:** seed
  removal is DESTRUCTIVE; feedback deletion MUTATES ACCOUNT — low-risk. **Real
  account:** Avoid on valued personalization. **Cleanup:** Seeds/feedback may
  not be exactly recoverable; rely on disposable data and written notes.

## N. Delete

- **Action:** Select the disposable station, press `d`, and decline. **Expected:**
  station remains and playback/selection are unchanged. **Account:**
  SAFE/read-only while declined. **Real account:** Yes. **Cleanup:** None.
- **Action:** Delete that station only after all other mutation tests. Test both
  inactive deletion and, only if desired, active deletion in separate sessions.
  **Expected:** confirmation defaults to No; on Yes the entry disappears,
  selection stays in bounds, no freed station remains active/pending, and the
  UI waits for a new station if the active one was deleted. **Account:** MUTATES
  ACCOUNT — destructive. **Real account:** Only for the disposable station.
  **Cleanup:** None; deletion may be irreversible.

## O. Reconnect/error handling

- **Action:** During a read-only request, briefly interrupt connectivity under
  supervision, then restore it before configured retries are exhausted.
  **Expected:** Requesting/Reconnecting/error notices are visible, the existing
  retry count/backoff is unchanged, recovery is reported, and playback resumes.
  **Account:** SAFE/read-only. **Real account:** Yes, with care. **Cleanup:**
  Restore connectivity immediately. Do not trigger this during a mutation: an
  ambiguous timeout may have succeeded server-side.
- **Action:** After a successful station mutation, continue normal playback or
  tune another station. **Expected:** refreshed stations remain selectable and
  no stale pointer/crash occurs. **Account:** no additional mutation beyond the
  already-consented action. **Real account:** Yes under the preceding limits.

## P. Quit/terminal restoration

- **Action:** Quit with `q` from the main view; separately use Ctrl-C during
  playback and during a read-only modal. **Expected:** audio/thread shutdown
  completes, curses exits, echo/cursor/line discipline are restored, and the
  shell accepts normal input. **Account:** SAFE/local. **Real account:** Yes.
  **Cleanup:** If the shell is visibly damaged, run `reset` and preserve the
  diagnostic log for the bug report.

## Disposable station strategy

One disposable station is the smallest containment boundary for create,
rename, add-music/seed inspection, QuickMix membership, mode testing, and final
delete. It does not make mutations safe: QuickMix affects a global mix,
bookmarks live outside the station, and seed/feedback removal can erase server
personalization. Record names and original membership/mode before changes,
restore reversible global state, and delete the temporary station last.
