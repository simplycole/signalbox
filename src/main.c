/*
Copyright (c) 2008-2018
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "config.h"

/* system includes */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
/* fork () */
#include <unistd.h>
#include <sys/select.h>
#endif
#include <time.h>
#include <ctype.h>
/* open () */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef _WIN32
/* tcset/getattr () */
#include <termios.h>
#endif
#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#ifndef _WIN32
/* waitpid () */
#include <sys/types.h>
#include <sys/wait.h>
#endif

/* pandora.com library */
#include <piano.h>

#include "main.h"
#include "platform.h"
#include "credential.h"
#include "debug.h"
#include "terminal.h"
#include "ui.h"
#include "ui_act.h"
#include "ui_dispatch.h"
#include "ui_readline.h"

/*	authenticate user
 */
static bool BarMainLoginUser (BarApp_t *app, PianoReturn_t *pianoReturn,
		CURLcode *curlReturn) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataLogin_t reqData;
	bool ret;

	reqData.user = app->settings.username;
	reqData.password = app->settings.password;
	reqData.step = 0;

	BarUiMsg (&app->settings, MSG_INFO, "Login... ");
	ret = BarUiPianoCall (app, PIANO_REQUEST_LOGIN, &reqData, &pRet, &wRet);
	BarUiStartEventCmd (&app->settings, "userlogin", NULL, NULL, &app->player,
			NULL, pRet, wRet);

	if (pianoReturn != NULL) *pianoReturn = pRet;
	if (curlReturn != NULL) *curlReturn = wRet;
	return ret;
}

static void BarMainReplacePassword (BarSettings_t *settings, char *password) {
	SbCredentialFreeSecret (settings->password);
	settings->password = password;
}

static bool BarMainPromptTuiLogin (BarApp_t *app, const char *error) {
	char username[256] = "";
	char password[256] = "";
	if (app->settings.username != NULL) {
		snprintf (username, sizeof (username), "%s", app->settings.username);
	}
	bool remember = SbCredentialBackendAvailable ();
	if (!SbUiRendererPromptLogin (&app->uiRenderer, &app->uiModel, username,
			sizeof (username), password, sizeof (password), &remember, error)) {
		SbCredentialClear (password, sizeof (password));
		return false;
	}
	char *newUser = strdup (username);
	char *newPassword = strdup (password);
	SbCredentialClear (password, sizeof (password));
	if (newUser == NULL || newPassword == NULL) {
		free (newUser); SbCredentialFreeSecret (newPassword); return false;
	}
	free (app->settings.username);
	app->settings.username = newUser;
	BarMainReplacePassword (&app->settings, newPassword);
	app->passwordFromSecureStore = false;
	app->rememberLogin = remember;
	return true;
}

static SbCredentialStatus BarMainLoadSecureCredential (BarApp_t *app) {
	if (app->settings.username == NULL || app->settings.password != NULL ||
			app->settings.passwordCmd != NULL) return SB_CREDENTIAL_NOT_FOUND;
	char *secret = NULL;
	const SbCredentialStatus status = SbCredentialLoad (SB_CREDENTIAL_SERVICE,
			app->settings.username, &secret);
	if (status == SB_CREDENTIAL_OK) {
		app->settings.password = secret;
		app->passwordFromSecureStore = true;
	}
	tuiDebugPrint ("credential_source=%s\n", status == SB_CREDENTIAL_OK ?
			"secure_store" : status == SB_CREDENTIAL_NOT_FOUND ? "not_found" :
			status == SB_CREDENTIAL_UNAVAILABLE ? "unavailable" : "error");
	return status;
}

static void BarMainPersistLogin (BarApp_t *app) {
	if (!app->rememberLogin) return;
	if (!SbCredentialBackendAvailable ()) {
		BarUiMsg (&app->settings, MSG_ERR,
				"Signed in, but secure credential storage is unavailable; using this session only.\n");
		return;
	}
	if (!BarSettingsWriteAccount (app->settings.username)) {
		BarUiMsg (&app->settings, MSG_ERR,
				"Signed in, but the remembered account could not be saved.\n");
		return;
	}
	if (SbCredentialStore (SB_CREDENTIAL_SERVICE, app->settings.username,
			app->settings.password) != SB_CREDENTIAL_OK) {
		BarUiMsg (&app->settings, MSG_ERR,
				"Signed in, but the password could not be saved securely.\n");
	}
}

static bool BarMainRecoverStoredLogin (BarApp_t *app, bool *retry) {
	const char *items[] = {"Retry", "Edit credentials",
			"Forget saved credentials", "Cancel"};
	const int selected = SbUiRendererSelectList (&app->uiRenderer, &app->uiModel,
			"STORED LOGIN REJECTED", items, 4);
	if (selected == 0 && !*retry) { *retry = true; return true; }
	if (selected == 1) {
		BarMainReplacePassword (&app->settings, NULL);
		return BarMainPromptTuiLogin (app, "Enter updated Pandora credentials");
	}
	if (selected == 2) {
		const SbCredentialStatus status = SbCredentialDelete (
				SB_CREDENTIAL_SERVICE, app->settings.username);
		BarMainReplacePassword (&app->settings, NULL);
		app->passwordFromSecureStore = false;
		if (status != SB_CREDENTIAL_OK && status != SB_CREDENTIAL_NOT_FOUND) {
			SbUiRendererTextModal (&app->uiRenderer, &app->uiModel,
					"CREDENTIALS", "Saved credentials could not be forgotten.");
			return false;
		}
		return BarMainPromptTuiLogin (app, "Saved password forgotten");
	}
	return false;
}

/*	ask for username/password if none were provided in settings
 */
static bool BarMainGetLoginCredentials (BarSettings_t *settings,
		BarReadlineFds_t *input) {
	bool usernameFromConfig = true;

	if (settings->username == NULL) {
		char nameBuf[100];

		BarUiMsg (settings, MSG_QUESTION, "Email: ");
		if (BarReadlineStr (nameBuf, sizeof (nameBuf), input, BAR_RL_DEFAULT) == 0) {
			return false;
		}
		settings->username = strdup (nameBuf);
		usernameFromConfig = false;
	}

	if (settings->password == NULL) {
		char passBuf[100];

		if (usernameFromConfig) {
			BarUiMsg (settings, MSG_QUESTION, "Email: %s\n", settings->username);
		}

		if (settings->passwordCmd == NULL) {
			BarUiMsg (settings, MSG_QUESTION, "Password: ");
			if (BarReadlineStr (passBuf, sizeof (passBuf), input, BAR_RL_NOECHO) == 0) {
				puts ("");
				SbCredentialClear (passBuf, sizeof (passBuf));
				return false;
			}
			/* write missing newline */
			puts ("");
			settings->password = strdup (passBuf);
			SbCredentialClear (passBuf, sizeof (passBuf));
		} else {
#ifdef _WIN32
			BarUiMsg (settings, MSG_NONE,
					"Error: password_command is unavailable on Windows W1.\n");
			return false;
#else
			pid_t chld;
			int pipeFd[2];

			BarUiMsg (settings, MSG_INFO, "Requesting password from external helper... ");

			if (pipe (pipeFd) == -1) {
				BarUiMsg (settings, MSG_NONE, "Error: %s\n", strerror (errno));
				return false;
			}

			chld = fork ();
			if (chld == 0) {
				/* child */
				close (pipeFd[0]);
				dup2 (pipeFd[1], fileno (stdout));
				execl ("/bin/sh", "/bin/sh", "-c", settings->passwordCmd, (char *) NULL);
				BarUiMsg (settings, MSG_NONE, "Error: %s\n", strerror (errno));
				close (pipeFd[1]);
				exit (1);
			} else if (chld == -1) {
				BarUiMsg (settings, MSG_NONE, "Error: %s\n", strerror (errno));
				return false;
			} else {
				/* parent */
				int status;

				close (pipeFd[1]);
				memset (passBuf, 0, sizeof (passBuf));
				read (pipeFd[0], passBuf, sizeof (passBuf)-1);
				close (pipeFd[0]);

				/* drop trailing newlines */
				ssize_t len = strlen (passBuf)-1;
				while (len >= 0 && passBuf[len] == '\n') {
					passBuf[len] = '\0';
					--len;
				}

				waitpid (chld, &status, 0);
				if (WEXITSTATUS (status) == 0) {
					settings->password = strdup (passBuf);
					SbCredentialClear (passBuf, sizeof (passBuf));
					BarUiMsg (settings, MSG_NONE, "Ok.\n");
				} else {
					SbCredentialClear (passBuf, sizeof (passBuf));
					BarUiMsg (settings, MSG_NONE, "Error: Exit status %i.\n", WEXITSTATUS (status));
					return false;
				}
			}
#endif
		} /* end else passwordCmd */
	}

	return true;
}

/*	get station list
 */
static bool BarMainGetStations (BarApp_t *app) {
	PianoReturn_t pRet;
	CURLcode wRet;
	bool ret;

	BarUiMsg (&app->settings, MSG_INFO, "Get stations... ");
	ret = BarUiPianoCall (app, PIANO_REQUEST_GET_STATIONS, NULL, &pRet, &wRet);
	SbUiModelSetStations (&app->uiModel, app->ph.stations);
	BarUiStartEventCmd (&app->settings, "usergetstations", NULL, NULL, &app->player,
			app->ph.stations, pRet, wRet);
	return ret;
}

/*	get initial station from autostart setting or user input
 */
static void BarMainGetInitialStation (BarApp_t *app) {
	/* try to get autostart station */
	if (app->settings.autostartStation != NULL) {
		app->nextStation = PianoFindStationById (app->ph.stations,
				app->settings.autostartStation);
		if (app->nextStation == NULL) {
			BarUiMsg (&app->settings, MSG_ERR,
					"Error: Autostart station not found.\n");
		}
	}
	/* no autostart? ask the user */
	if (app->nextStation == NULL && app->useTui) {
		app->nextStation = app->ph.stations;
	} else if (app->nextStation == NULL) {
		app->nextStation = BarUiSelectStation (app, app->ph.stations,
				"Select station: ", NULL, app->settings.autoselect);
	}
}

/*	wait for user input
 */
static void BarMainHandleUserInput (BarApp_t *app) {
	if (app->useTui) {
		const SbUiCommandEvent event = SbUiRendererReadCommand (&app->uiRenderer,
				&app->uiModel);
		if (event.command == SB_UI_CMD_TOGGLE_VISUALIZER) {
			app->visualizerEnabled = !app->visualizerEnabled;
			BarPlayerSetSpectrumEnabled (&app->player, app->visualizerEnabled);
			SbSpectrumSnapshot snapshot;
			BarPlayerGetSpectrum (&app->player, &snapshot);
			SbUiModelSetSpectrum (&app->uiModel, &snapshot,
					app->visualizerEnabled);
			SbUiRendererRender (&app->uiRenderer, &app->uiModel,
					SB_UI_RENDER_STATE);
		} else if (event.historySelected) {
			BarUiActHistorySelected (app, event.historyIndex);
		} else if (event.command != SB_UI_CMD_NONE) {
			BarUiDispatchCommand (app, event.command,
					event.station != NULL ? event.station : app->curStation,
					app->playlist,
					true, BAR_DC_GLOBAL);
		}
	}
	char buf[2];
	if (BarReadline (buf, sizeof (buf), NULL, &app->input,
			BAR_RL_FULLRETURN | BAR_RL_NOECHO | BAR_RL_NOINT,
			app->useTui ? 0 : 1) > 0) {
		const SbUiCommand command = BarUiCommandFromKey (&app->settings, buf[0]);
		if (command != SB_UI_CMD_NONE) {
			BarUiDispatchCommand (app, command, app->curStation, app->playlist,
					true, BAR_DC_GLOBAL);
		}
	}
}

/*	fetch new playlist
 */
static void BarMainGetPlaylist (BarApp_t *app) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetPlaylist_t reqData;
	reqData.station = app->nextStation;
	reqData.quality = app->settings.audioQuality;

	BarUiMsg (&app->settings, MSG_INFO, "Receiving new playlist... ");
	SbUiModelSetActivity (&app->uiModel, SB_UI_ACTIVITY_WAITING_PLAYLIST);
	SbUiRendererRender (&app->uiRenderer, &app->uiModel, SB_UI_RENDER_STATE);
	if (!BarUiPianoCall (app, PIANO_REQUEST_GET_PLAYLIST,
			&reqData, &pRet, &wRet)) {
		app->nextStation = NULL;
	} else {
		app->playlist = reqData.retPlaylist;
		if (app->playlist == NULL) {
			BarUiMsg (&app->settings, MSG_INFO, "No tracks left.\n");
			app->nextStation = NULL;
		}
	}
	app->curStation = app->nextStation;
	SbUiModelSetStation (&app->uiModel, app->curStation);
	BarUiStartEventCmd (&app->settings, "stationfetchplaylist",
			app->curStation, app->playlist, &app->player, app->ph.stations,
			pRet, wRet);
}

/*	start new player thread
 */
static void BarMainStartPlayback (BarApp_t *app, pthread_t *playerThread) {
	assert (app != NULL);
	assert (playerThread != NULL);

	const PianoSong_t * const curSong = app->playlist;
	assert (curSong != NULL);

	SbUiModelSetSong (&app->uiModel, curSong, app->curStation->isQuickMix ?
			PianoFindStationById (app->ph.stations, curSong->stationId) : NULL);
	SbUiRendererRender (&app->uiRenderer, &app->uiModel, SB_UI_RENDER_SONG);

	static const char httpPrefix[] = "http://";
	/* avoid playing local files */
	if (curSong->audioUrl == NULL ||
			strncmp (curSong->audioUrl, httpPrefix, strlen (httpPrefix)) != 0) {
		BarUiMsg (&app->settings, MSG_ERR, "Invalid song url.\n");
	} else {
		player_t * const player = &app->player;
		BarPlayerReset (player);

		app->player.url = curSong->audioUrl;
		app->player.gain = curSong->fileGain;
		app->player.songDuration = curSong->length;

		assert (interrupted == &app->doQuit);
		interrupted = &app->player.interrupted;

		/* throw event */
		BarUiStartEventCmd (&app->settings, "songstart",
				app->curStation, curSong, &app->player, app->ph.stations,
				PIANO_RET_OK, CURLE_OK);

		/* prevent race condition, mode must _not_ be DEAD if
		 * thread has been started */
		app->player.mode = PLAYER_WAITING;
		/* start player */
		pthread_create (playerThread, NULL, BarPlayerThread,
				&app->player);
	}
}

/*	player is done, clean up
 */
static void BarMainPlayerCleanup (BarApp_t *app, pthread_t *playerThread) {
	void *threadRet;

	BarUiStartEventCmd (&app->settings, "songfinish", app->curStation,
			app->playlist, &app->player, app->ph.stations, PIANO_RET_OK,
			CURLE_OK);

	/* FIXME: pthread_join blocks everything if network connection
	 * is hung up e.g. */
	pthread_join (*playerThread, &threadRet);

	if (threadRet == (void *) PLAYER_RET_OK) {
		app->playerErrors = 0;
	} else if (threadRet == (void *) PLAYER_RET_SOFTFAIL) {
		++app->playerErrors;
		if (app->playerErrors >= app->settings.maxRetry) {
			/* don't continue playback if thread reports too many error */
			app->nextStation = NULL;
		}
	} else {
		app->nextStation = NULL;
	}

	assert (interrupted == &app->player.interrupted);
	interrupted = &app->doQuit;

	app->player.mode = PLAYER_DEAD;
}

/*	print song duration
 */
static void BarMainPrintTime (BarApp_t *app) {
	player_t * const player = &app->player;

	pthread_mutex_lock (&player->lock);
	const unsigned int songDuration = player->songDuration;
	const unsigned int songPlayed = player->songPlayed;
	const bool doPause = player->doPause;
	pthread_mutex_unlock (&player->lock);

	SbUiModelSetProgress (&app->uiModel, songPlayed, songDuration,
			doPause ? SB_UI_PLAYBACK_PAUSED : SB_UI_PLAYBACK_PLAYING);
	SbUiRendererRender (&app->uiRenderer, &app->uiModel,
			SB_UI_RENDER_PROGRESS);
}

/*	main loop
 */
static void BarMainLoop (BarApp_t *app) {
	pthread_t playerThread;
	const SbCredentialStatus credentialStatus = BarMainLoadSecureCredential (app);
	if (app->useTui && (app->settings.username == NULL ||
			(app->settings.password == NULL && app->settings.passwordCmd == NULL))) {
		const char *credentialNotice = credentialStatus == SB_CREDENTIAL_ERROR ?
				"Secure storage could not be accessed; sign-in will be session-only" :
				credentialStatus == SB_CREDENTIAL_UNAVAILABLE ?
				"Secure storage unavailable; sign-in will be session-only" : NULL;
		if (!BarMainPromptTuiLogin (app, credentialNotice)) return;
	} else if (!BarMainGetLoginCredentials (&app->settings, &app->input)) return;

	bool retriedStored = false;
	for (;;) {
		PianoReturn_t pRet = PIANO_RET_OK;
		CURLcode wRet = CURLE_OK;
		if (app->passwordFromSecureStore) {
			BarUiMsg (&app->settings, MSG_INFO, "Signing in with saved credentials... ");
		}
		if (BarMainLoginUser (app, &pRet, &wRet)) break;
		if (!app->useTui || !app->passwordFromSecureStore || wRet != CURLE_OK ||
				!BarMainRecoverStoredLogin (app, &retriedStored)) return;
	}
	BarMainPersistLogin (app);
	BarUiMsg (&app->settings, MSG_INFO, "Connected.\n");

	if (!BarMainGetStations (app)) {
		return;
	}

	BarMainGetInitialStation (app);
	if (app->useTui) {
		SbUiModelSetStation (&app->uiModel, app->nextStation);
		SbUiRendererRender (&app->uiRenderer, &app->uiModel,
				SB_UI_RENDER_STATION);
	}

#ifdef _WIN32
	/* W2 ends at authenticated station browsing.  Starting the decoder/libao
	 * thread would leave an ambiguous PLAYER_WAITING state; Windows audio is
	 * deliberately a W3 concern. */
	SbUiModelSetActivity (&app->uiModel, SB_UI_ACTIVITY_AUDIO_UNAVAILABLE);
	BarUiMsg (&app->settings, MSG_INFO,
			"Windows audio playback is not available in W2.\n");
	SbUiRendererRender (&app->uiRenderer, &app->uiModel, SB_UI_RENDER_STATE);
	while (!app->doQuit) BarMainHandleUserInput (app);
	return;
#endif

	player_t * const player = &app->player;

	while (!app->doQuit) {
		/* song finished playing, clean up things/scrobble song */
		if (BarPlayerGetMode (player) == PLAYER_FINISHED) {
			if (player->interrupted != 0) {
				app->doQuit = 1;
			}
			BarMainPlayerCleanup (app, &playerThread);
		}

		/* check whether player finished playing and start playing new
		 * song */
		if (BarPlayerGetMode (player) == PLAYER_DEAD) {
			/* what's next? */
			if (app->playlist != NULL) {
				PianoSong_t *histsong = app->playlist;
				app->playlist = PianoListNextP (app->playlist);
				SbUiModelSetSong (&app->uiModel, NULL, NULL);
				histsong->head.next = NULL;
				BarUiHistoryPrepend (app, histsong);
			}
			if (app->playlist == NULL && app->nextStation != NULL && !app->doQuit) {
				if (app->nextStation != app->curStation) {
					SbUiModelSetStation (&app->uiModel, app->nextStation);
					SbUiRendererRender (&app->uiRenderer, &app->uiModel,
							SB_UI_RENDER_STATION);
				}
				BarMainGetPlaylist (app);
			}
			/* song ready to play */
			if (app->playlist != NULL) {
				BarMainStartPlayback (app, &playerThread);
			}
		}

		BarMainHandleUserInput (app);
		if (app->useTui && app->visualizerEnabled) {
			SbSpectrumSnapshot snapshot;
			BarPlayerGetSpectrum (player, &snapshot);
			SbUiModelSetSpectrum (&app->uiModel, &snapshot, true);
		}

		/* show time */
		if (BarPlayerGetMode (player) == PLAYER_PLAYING) {
			BarMainPrintTime (app);
		}
	}

	if (BarPlayerGetMode (player) != PLAYER_DEAD) {
		pthread_join (playerThread, NULL);
	}
}

sig_atomic_t *interrupted = NULL;

static void intHandler (int signal) {
	if (interrupted != NULL) {
		debugPrint(DEBUG_UI, "Received ^C\n");
		*interrupted += 1;
	}
}

static void BarMainRequestShutdown (void) {
	intHandler (0);
}

static void BarMainSetupSigaction () {
	(void) SbPlatformInstallShutdownHandler (BarMainRequestShutdown);
}

int main (int argc, char **argv) {
	static BarApp_t app;
	enum {
		MODE_AUTO,
		MODE_TUI,
		MODE_CLASSIC,
	} mode = MODE_AUTO;
	bool forgetCredentials = false;
	int visualizerOverride = -1;
	SbTuiTheme tuiTheme = SB_TUI_THEME_PHOSPHOR;

	debugEnable();

	memset (&app, 0, sizeof (app));
	for (int i = 1; i < argc; i++) {
		if (strcmp (argv[i], "--tui") == 0) {
			if (mode == MODE_CLASSIC) {
				fputs ("signalbox: --tui and --classic cannot be used together\n", stderr);
				return 2;
			}
			mode = MODE_TUI;
		} else if (strcmp (argv[i], "--classic") == 0) {
			if (mode == MODE_TUI) {
				fputs ("signalbox: --tui and --classic cannot be used together\n", stderr);
				return 2;
			}
			mode = MODE_CLASSIC;
		} else if (strcmp (argv[i], "--help") == 0) {
			printf ("Usage: %s [--tui|--classic] [--theme phosphor|amber|mono|neutral] [--visualizer spectrum|off] [--forget-credentials]\n"
					"  --tui       force curses TUI\n"
					"  --classic   force classic terminal UI\n"
					"TUI is selected automatically on supported interactive terminals.\n",
					argv[0]);
			return 0;
		} else if (strcmp (argv[i], "--forget-credentials") == 0) {
			forgetCredentials = true;
		} else if (strcmp (argv[i], "--visualizer") == 0 && i + 1 < argc) {
			const char * const name = argv[++i];
			if (strcmp (name, "spectrum") == 0) visualizerOverride = 1;
			else if (strcmp (name, "off") == 0) visualizerOverride = 0;
			else {
				fprintf (stderr, "signalbox: unknown visualizer '%s'\n", name);
				return 2;
			}
		} else if (strcmp (argv[i], "--theme") == 0 && i + 1 < argc) {
			const char * const name = argv[++i];
			if (strcmp (name, "phosphor") == 0) tuiTheme = SB_TUI_THEME_PHOSPHOR;
			else if (strcmp (name, "amber") == 0) tuiTheme = SB_TUI_THEME_AMBER;
			else if (strcmp (name, "mono") == 0) tuiTheme = SB_TUI_THEME_MONO;
			else if (strcmp (name, "neutral") == 0) tuiTheme = SB_TUI_THEME_NEUTRAL;
			else {
				fprintf (stderr, "signalbox: unknown theme '%s'\n", name);
				return 2;
			}
		} else {
			fprintf (stderr, "Usage: %s [--tui|--classic] [--theme phosphor|amber|mono|neutral] [--visualizer spectrum|off] [--forget-credentials]\n", argv[0]);
			return 2;
		}
	}
#ifdef _WIN32
	const bool terminalSupportsTui = BarTermIsInteractive ();
#else
	const char * const term = getenv ("TERM");
	const bool terminalSupportsTui = isatty (STDIN_FILENO) &&
			isatty (STDOUT_FILENO) && term != NULL && *term != '\0' &&
			strcmp (term, "dumb") != 0;
#endif
	if (mode == MODE_TUI && !terminalSupportsTui) {
#ifdef _WIN32
		fputs ("signalbox: --tui requires an interactive terminal\n",
				stderr);
#else
		fputs ("signalbox: --tui requires an interactive terminal and a usable TERM\n",
				stderr);
#endif
		return 2;
	}
	app.useTui = mode == MODE_TUI ||
			(mode == MODE_AUTO && terminalSupportsTui);
	app.tuiTheme = tuiTheme;

	/* save terminal attributes, before disabling echoing */
	BarTermInit ();

	/* signals */
#ifndef _WIN32
	signal (SIGPIPE, SIG_IGN);
#endif
	BarMainSetupSigaction ();
	interrupted = &app.doQuit;

	/* init some things */
	gcry_check_version (NULL);
	gcry_control (GCRYCTL_DISABLE_SECMEM, 0);
	gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
	BarPlayerInit (&app.player, &app.settings);

	BarSettingsInit (&app.settings);
	BarSettingsRead (&app.settings);
	if (visualizerOverride >= 0)
		app.settings.visualizerSpectrum = visualizerOverride != 0;
	app.visualizerEnabled = app.useTui && app.settings.visualizerSpectrum;
	BarPlayerSetSpectrumEnabled (&app.player, app.visualizerEnabled);
	if (forgetCredentials) {
		int result = 1;
		if (app.settings.username == NULL) {
			fputs ("signalbox: no configured Pandora account to forget\n", stderr);
		} else {
			const SbCredentialStatus status = SbCredentialDelete (
					SB_CREDENTIAL_SERVICE, app.settings.username);
			if (status == SB_CREDENTIAL_OK || status == SB_CREDENTIAL_NOT_FOUND) {
				if (!app.settings.usernameFromConfig) BarSettingsDeleteAccount ();
				puts (status == SB_CREDENTIAL_OK ? "Forgot saved Signalbox credentials." :
						"No saved Signalbox password was found.");
				result = 0;
			} else if (status == SB_CREDENTIAL_UNAVAILABLE) {
				fputs ("signalbox: secure credential storage is unavailable\n", stderr);
			} else {
				fputs ("signalbox: saved credentials could not be forgotten\n", stderr);
			}
		}
		BarSettingsDestroy (&app.settings);
		BarPlayerDestroy (&app.player);
		BarTermRestore ();
		return result;
	}
	SbUiModelInit (&app.uiModel);
	app.uiModel.visualizerEnabled = app.visualizerEnabled;
	SbUiModelSetVolume (&app.uiModel, app.settings.volume);
	SbUiRendererInitClassic (&app.uiRenderer, &app.settings);
	SbUiRendererSetActive (&app.uiRenderer);
	if (app.useTui && !SbUiRendererInitCurses (&app.uiRenderer, &app.settings,
			tuiTheme)) {
		if (mode == MODE_TUI) {
			fputs ("signalbox: unable to initialize curses TUI\n", stderr);
			SbUiRendererSetActive (NULL);
			SbUiModelDestroy (&app.uiModel);
			BarSettingsDestroy (&app.settings);
			BarPlayerDestroy (&app.player);
			BarTermRestore ();
			return 1;
		}
		fputs ("signalbox: TUI unavailable; falling back to classic mode\n",
				stderr);
		app.useTui = false;
		app.visualizerEnabled = false;
		app.uiModel.visualizerEnabled = false;
		BarPlayerSetSpectrumEnabled (&app.player, false);
	}
	if (app.useTui) {
		SbUiRendererRender (&app.uiRenderer, &app.uiModel,
				SB_UI_RENDER_STATION);
	}

	PianoReturn_t pret;
	if ((pret = PianoInit (&app.ph, app.settings.partnerUser,
			app.settings.partnerPassword, app.settings.device,
			app.settings.inkey, app.settings.outkey)) != PIANO_RET_OK) {
		BarUiMsg (&app.settings, MSG_ERR, "Initialization failed:"
				" %s\n", PianoErrorToStr (pret));
		SbUiRendererShutdown (&app.uiRenderer);
		SbUiRendererSetActive (NULL);
		SbUiModelDestroy (&app.uiModel);
		BarSettingsDestroy (&app.settings);
		BarPlayerDestroy (&app.player);
		BarTermRestore ();
		return 1;
	}

	BarUiMsg (&app.settings, MSG_NONE,
			"Welcome to " PROGRAM_NAME " (" VERSION ")! ");
	if (app.settings.keys[BAR_KS_HELP] == BAR_KS_DISABLED) {
		BarUiMsg (&app.settings, MSG_NONE, "\n");
	} else {
		BarUiMsg (&app.settings, MSG_NONE,
				"Press %c for a list of commands.\n",
				app.settings.keys[BAR_KS_HELP]);
	}

	curl_global_init (CURL_GLOBAL_DEFAULT);
	app.http = curl_easy_init ();
	assert (app.http != NULL);

	/* init fds */
#ifdef _WIN32
	app.input.fds[0] = STDIN_FILENO;
	app.input.fds[1] = -1;
	app.input.maxfd = 1;
	if (app.settings.fifo != NULL) {
		BarUiMsg (&app.settings, MSG_INFO,
				"Control FIFO is unavailable on Windows.\n");
	}
#else
	FD_ZERO(&app.input.set);
	app.input.fds[0] = STDIN_FILENO;
	if (!app.useTui) {
		FD_SET(app.input.fds[0], &app.input.set);
	}

	/* open fifo read/write so it won't EOF if nobody writes to it */
	assert (sizeof (app.input.fds) / sizeof (*app.input.fds) >= 2);
	app.input.fds[1] = open (app.settings.fifo, O_RDWR);
	if (app.input.fds[1] != -1) {
		struct stat s;

		/* check for file type, must be fifo */
		fstat (app.input.fds[1], &s);
		if (!S_ISFIFO (s.st_mode)) {
			BarUiMsg (&app.settings, MSG_ERR, "File at %s is not a fifo\n", app.settings.fifo);
			close (app.input.fds[1]);
			app.input.fds[1] = -1;
		} else {
			FD_SET(app.input.fds[1], &app.input.set);
			BarUiMsg (&app.settings, MSG_INFO, "Control fifo at %s opened\n",
					app.settings.fifo);
		}
	}
	app.input.maxfd = app.input.fds[0] > app.input.fds[1] ? app.input.fds[0] :
			app.input.fds[1];
	++app.input.maxfd;
#endif

	BarMainLoop (&app);

	if (app.input.fds[1] != -1) {
#ifndef _WIN32
		close (app.input.fds[1]);
#endif
	}

	/* write statefile */
	BarSettingsWrite (app.curStation, &app.settings);

	PianoDestroy (&app.ph);
	PianoDestroyPlaylist (app.songHistory);
	PianoDestroyPlaylist (app.playlist);
	curl_easy_cleanup (app.http);
	curl_global_cleanup ();
	BarPlayerDestroy (&app.player);
	SbUiRendererShutdown (&app.uiRenderer);
	SbUiRendererSetActive (NULL);
	SbUiModelDestroy (&app.uiModel);
	BarSettingsDestroy (&app.settings);

	/* restore terminal attributes, zsh doesn't need this, bash does... */
	BarTermRestore ();

	return 0;
}
