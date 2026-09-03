#include "ui_renderer.h"

/* W1 deliberately has no Windows curses backend.  Shared wrappers remain
 * callable so this is still the normal executable and --tui fails plainly. */
bool SbUiRendererInitCurses (SbUiRenderer *renderer,
		const BarSettings_t *settings, SbTuiTheme theme) {
	(void) renderer; (void) settings; (void) theme;
	return false;
}

bool SbUiRendererPromptText (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *label, char *value, size_t size) {
	(void) renderer; (void) model; (void) title; (void) label;
	(void) value; (void) size; return false;
}
bool SbUiRendererPromptLogin (SbUiRenderer *renderer, const SbUiModel *model,
		char *user, size_t userSize, char *password, size_t passwordSize,
		bool *remember, const char *error) {
	(void) renderer; (void) model; (void) user; (void) userSize;
	(void) password; (void) passwordSize; (void) remember; (void) error;
	return false;
}
bool SbUiRendererConfirm (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *message) {
	(void) renderer; (void) model; (void) title; (void) message; return false;
}
int SbUiRendererSelectList (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *const *items, size_t count) {
	(void) renderer; (void) model; (void) title; (void) items; (void) count;
	return -1;
}
int SbUiRendererSelectHistory (SbUiRenderer *renderer,
		const SbUiModel *model) {
	(void) renderer; (void) model; return -1;
}
void SbUiRendererSongDetails (SbUiRenderer *renderer, const SbUiModel *model,
		const PianoSong_t *song, const PianoStation_t *station, time_t playedAt) {
	(void) renderer; (void) model; (void) song; (void) station; (void) playedAt;
}
void SbUiRendererTextModal (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *text) {
	(void) renderer; (void) model; (void) title; (void) text;
}
bool SbUiRendererToggleList (SbUiRenderer *renderer, const SbUiModel *model,
		const char *title, const char *const *items, bool *selected, size_t count) {
	(void) renderer; (void) model; (void) title; (void) items;
	(void) selected; (void) count; return false;
}
