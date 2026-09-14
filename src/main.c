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
#include "playlist_prefetch.h"
#include "terminal.h"
#include "tui_presentation.h"
#include "ui.h"
#include "ui_act.h"
#include "ui_dispatch.h"
#include "ui_readline.h"

static const char *BarMainLookupState (const SbLookupStatus status) {
	switch (status) {
		case SB_LOOKUP_LOADING: return "Loading";
		case SB_LOOKUP_AVAILABLE: return "Available";
		case SB_LOOKUP_INSTRUMENTAL: return "Instrumental";
		case SB_LOOKUP_NO_MATCH: return "No match";
		case SB_LOOKUP_UNAVAILABLE: return "Temporarily unavailable";
		case SB_LOOKUP_ERROR: return "Error";
		default: return "Not requested";
	}
}

static void BarMainAppendField (char *text, const size_t size,
		const char *label, const char *value) {
	if (value == NULL || value[0] == '\0') return;
	size_t used = strlen (text);
	if (used < size) snprintf (text + used, size - used, "%s%s: %s",
			used > 0 ? "\n" : "", label, value);
}

static char *BarMainTrackInfoText (BarApp_t *app) {
	if (app->playlist == NULL) return strdup ("No song playing");
	const SbMetadataResult *m = &app->metadata; const PianoSong_t *song = app->playlist;
	char text[3000] = "PANDORA";
	BarMainAppendField (text, sizeof (text), "Artist", app->trackIdentity.artist);
	BarMainAppendField (text, sizeof (text), "Track", app->trackIdentity.title);
	BarMainAppendField (text, sizeof (text), "Album", app->trackIdentity.album);
	BarMainAppendField (text, sizeof (text), "Station", app->trackIdentity.station);
	if (app->trackIdentity.duration > 0) { char duration[32];
		snprintf (duration, sizeof (duration), "%u:%02u", app->trackIdentity.duration / 60,
				app->trackIdentity.duration % 60); BarMainAppendField (text, sizeof (text), "Length", duration); }
	BarMainAppendField (text, sizeof (text), "Rating",
			song->rating == PIANO_RATE_LOVE ? "Loved" : song->rating == PIANO_RATE_BAN ? "Banned" : NULL);
	strncat (text, "\n\nENRICHMENT", sizeof (text) - strlen (text) - 1);
	BarMainAppendField (text, sizeof (text), "Metadata", BarMainLookupState (m->status));
	BarMainAppendField (text, sizeof (text), "Provider", m->provider);
	if (m->status == SB_LOOKUP_AVAILABLE) {
		char confidence[32]; snprintf (confidence, sizeof (confidence), "%.0f%%", m->confidence * 100.0);
		BarMainAppendField (text, sizeof (text), "Canonical Artist", m->artist);
		BarMainAppendField (text, sizeof (text), "Canonical Track", m->title);
		BarMainAppendField (text, sizeof (text), "Release", m->release);
		char releaseDate[16];
		if (SbMusicBrainzFormatDate (m->releaseDate, releaseDate, sizeof (releaseDate)))
			BarMainAppendField (text, sizeof (text), "Release Date", releaseDate);
		BarMainAppendField (text, sizeof (text), "Confidence", confidence);
	}
	strncat (text, "\n\nLYRICS", sizeof (text) - strlen (text) - 1);
	BarMainAppendField (text, sizeof (text), "State",
			SbTuiPresentationLyricsState (app->lyrics.status,
					app->lyrics.plainLyrics != NULL,
					app->uiModel.syncedLyrics.count));
	BarMainAppendField (text, sizeof (text), "Provider", app->lyrics.provider);
	strncat (text, "\n\nALBUM ART", sizeof (text) - strlen (text) - 1);
	BarMainAppendField (text, sizeof (text), "State",
			app->settings.albumArtMode == SB_ALBUM_ART_OFF ? "Disabled" :
			BarMainLookupState (app->albumArt.status));
	BarMainAppendField (text, sizeof (text), "Provider", app->albumArt.provider);
	return strdup (text);
}

static char *BarMainLyricsText (BarApp_t *app) {
	if (app->playlist == NULL) return strdup ("No song playing");
	const SbLyricsResult *lyrics = &app->lyrics; char *body = NULL;
	SbLyricsDisplayText (lyrics, &body); const char *message = NULL;
	if (lyrics->status == SB_LOOKUP_LOADING) message = "Looking up lyrics...";
	else if (lyrics->status == SB_LOOKUP_INSTRUMENTAL) message = "Instrumental track\nNo lyrics";
	else if (lyrics->status == SB_LOOKUP_NO_MATCH) message = "Lyrics unavailable\nNo match found";
	else if (lyrics->status == SB_LOOKUP_ERROR) message = "Lyrics unavailable\nProvider error";
	else if (body == NULL) message = "Lyrics unavailable\nNo match found";
	const char *artist = lyrics->artist[0] ? lyrics->artist : app->trackIdentity.artist;
	const char *title = lyrics->title[0] ? lyrics->title : app->trackIdentity.title;
	const char *album = lyrics->album[0] ? lyrics->album : app->trackIdentity.album;
	size_t needed = strlen (artist) + strlen (title) + strlen (album) +
			strlen (body != NULL ? body : message) + 24;
	char *text = malloc (needed);
	if (text != NULL) {
		if (!SbTuiPresentationLyricsHeader (text, needed, artist, title, album))
			text[0] = '\0';
		strncat (text, body != NULL ? body : message, needed - strlen (text) - 1);
	}
	free (body);
	return text != NULL ? text : strdup ("Lyrics unavailable\nProvider error");
}

typedef struct { BarApp_t *app; bool lyrics; } BarEnrichmentModal;

static char *BarMainEnrichmentModalText (void *opaque) {
	BarEnrichmentModal *modal = opaque; BarApp_t *app = modal->app;
	return modal->lyrics ? BarMainLyricsText (app) : BarMainTrackInfoText (app);
}

static void BarMainShowEnrichmentModal (BarApp_t *app, const bool lyrics) {
	static BarEnrichmentModal modal;
	modal = (BarEnrichmentModal) {app, lyrics};
	SbUiRendererDynamicTextModal (&app->uiRenderer, &app->uiModel,
			lyrics ? "LYRICS" : "TRACK INFO",
			lyrics ? SB_UI_CMD_LYRICS : SB_UI_CMD_TRACK_INFO,
			BarMainEnrichmentModalText, &modal);
}

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
		} else if (event.command == SB_UI_CMD_TRACK_INFO) {
			BarMainShowEnrichmentModal (app, false);
		} else if (event.command == SB_UI_CMD_LYRICS) {
			BarMainShowEnrichmentModal (app, true);
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
	/* On Windows the curses TUI owns the console through
	 * SbTerminalReadInput().  BarReadline() uses fgets(stdin) there, which
	 * would be a second console-input consumer and can take KEY_EVENT records
	 * before the native adapter sees them.  Unix still needs this nonblocking
	 * pass while the TUI is active so commands from the control FIFO work. */
#ifdef _WIN32
	if (!app->useTui && BarReadline (buf, sizeof (buf), NULL, &app->input,
			BAR_RL_FULLRETURN | BAR_RL_NOECHO | BAR_RL_NOINT, 1) > 0) {
#else
	if (BarReadline (buf, sizeof (buf), NULL, &app->input,
			BAR_RL_FULLRETURN | BAR_RL_NOECHO | BAR_RL_NOINT,
			app->useTui ? 0 : 1) > 0) {
#endif
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
		app->playlistGeneration++;
		app->prefetch.lastAttemptRemaining = SIZE_MAX;
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

static void *BarMainPrefetchThread (void *data) {
	BarApp_t *app = data;
	CURL *http = curl_easy_init ();
	PianoSong_t *result = NULL;
	PianoReturn_t pRet = PIANO_RET_P_INTERNAL;
	CURLcode wRet = CURLE_FAILED_INIT;
	if (http != NULL) {
		PianoStation_t station = {0};
		station.id = app->prefetch.stationId;
		PianoRequestDataGetPlaylist_t request = {.station = &station,
				.quality = app->settings.audioQuality, .retPlaylist = NULL};
		(void) BarUiPianoCallQuiet (app, http, PIANO_REQUEST_GET_PLAYLIST,
				&request, &pRet, &wRet);
		result = request.retPlaylist;
		curl_easy_cleanup (http);
	}
	pthread_mutex_lock (&app->prefetch.lock);
	app->prefetch.result = result;
	app->prefetch.pianoResult = pRet;
	app->prefetch.curlResult = wRet;
	app->prefetch.complete = true;
	pthread_mutex_unlock (&app->prefetch.lock);
	return NULL;
}

static void BarMainPollPrefetch (BarApp_t *app) {
	if (!app->prefetch.inFlight) return;
	pthread_mutex_lock (&app->prefetch.lock);
	const bool complete = app->prefetch.complete;
	pthread_mutex_unlock (&app->prefetch.lock);
	if (!complete) return;
	pthread_join (app->prefetch.thread, NULL);
	const bool current = app->prefetch.generation == app->playlistGeneration &&
			app->curStation != NULL && app->nextStation == app->curStation &&
			app->curStation->id != NULL && strcmp (app->curStation->id,
			app->prefetch.stationId) == 0;
	if (app->prefetch.pianoResult == PIANO_RET_OK &&
			app->prefetch.curlResult == CURLE_OK && current) {
		const size_t added = SbPlaylistAppendUnique (&app->playlist,
				app->prefetch.result);
		app->prefetch.result = NULL;
		tuiDebugPrint ("prefetch complete added=%zu generation=%llu\n", added,
				(unsigned long long) app->playlistGeneration);
		SbUiRendererRender (&app->uiRenderer, &app->uiModel, SB_UI_RENDER_STATE);
	} else {
		if (!current) tuiDebugPrint ("prefetch discarded stale_station generation=%llu current_generation=%llu\n",
				(unsigned long long) app->prefetch.generation,
				(unsigned long long) app->playlistGeneration);
		else tuiDebugPrint ("prefetch failed piano=%d curl=%d\n",
				(int) app->prefetch.pianoResult, (int) app->prefetch.curlResult);
		PianoDestroyPlaylist (app->prefetch.result);
		app->prefetch.result = NULL;
	}
	free (app->prefetch.stationId); app->prefetch.stationId = NULL;
	app->prefetch.complete = false; app->prefetch.inFlight = false;
}

static void BarMainMaybePrefetch (BarApp_t *app) {
	BarMainPollPrefetch (app);
	if (app->prefetch.inFlight || app->playlist == NULL ||
			app->curStation == NULL || app->nextStation != app->curStation) return;
	const size_t queued = SbPlaylistCount (app->playlist);
	const size_t remaining = BarPlayerGetMode (&app->player) == PLAYER_DEAD ?
			queued : queued > 0 ? queued - 1 : 0;
	if (!SbPlaylistPrefetchNeeded (remaining, app->prefetch.inFlight,
			app->prefetch.lastAttemptRemaining)) return;
	app->prefetch.lastAttemptRemaining = remaining;
	app->prefetch.stationId = strdup (app->curStation->id);
	if (app->prefetch.stationId == NULL) return;
	app->prefetch.generation = app->playlistGeneration;
	app->prefetch.complete = false; app->prefetch.inFlight = true;
	if (pthread_create (&app->prefetch.thread, NULL,
			BarMainPrefetchThread, app) != 0) {
		free (app->prefetch.stationId); app->prefetch.stationId = NULL;
		app->prefetch.inFlight = false;
		return;
	}
	tuiDebugPrint ("prefetch started queue_remaining=%zu station=%s generation=%llu\n",
			remaining, app->curStation->id,
			(unsigned long long) app->playlistGeneration);
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
	const PianoStation_t *identityStation = app->curStation->isQuickMix ?
			PianoFindStationById (app->ph.stations, curSong->stationId) : app->curStation;
	SbTrackIdentitySet (&app->trackIdentity, curSong->artist, curSong->title,
			curSong->album, identityStation != NULL ? identityStation->name : NULL,
			curSong->length);
	app->enrichmentGeneration++;
	SbMetadataResultInit (&app->metadata);
	app->metadata.status = SB_LOOKUP_LOADING;
	SbLyricsResultDestroy (&app->lyrics); app->lyrics.status = SB_LOOKUP_LOADING;
	snprintf (app->lyrics.provider, sizeof (app->lyrics.provider), "LRCLIB");
	SbAlbumArtResultInit(&app->albumArt); app->albumArt.status=SB_LOOKUP_LOADING;
	app->uiModel.artState=SB_LOOKUP_LOADING; app->uiModel.artCachedPath[0]='\0';
	tuiDebugPrint ("art state=loading generation=%llu artist=\"%s\" title=\"%s\" album=\"%s\"\n",
			(unsigned long long) app->enrichmentGeneration, curSong->artist,
			curSong->title, curSong->album);
	if (app->metadataResolver.started) SbMetadataResolverRequest (
			&app->metadataResolver, &app->trackIdentity, app->enrichmentGeneration);
	else { app->metadata.status = SB_LOOKUP_ERROR; snprintf (app->metadata.error,
			sizeof (app->metadata.error), "Metadata worker unavailable"); }
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

	player_t * const player = &app->player;

	while (!app->doQuit) {
		BarMainMaybePrefetch (app);
		SbMetadataResult enriched;
		if (SbMetadataResolverPoll (&app->metadataResolver,
				app->enrichmentGeneration, &enriched)) {
			app->metadata = enriched;
			tuiDebugPrint ("enrichment result provider=musicbrainz generation=%llu state=%d detail=%s\n",
					(unsigned long long) app->enrichmentGeneration,
					(int) enriched.status, enriched.error);
			SbUiRendererRender (&app->uiRenderer, &app->uiModel, SB_UI_RENDER_STATE);
		}
		SbLyricsResult lyrics;
		if (SbLyricsResolverPoll (&app->metadataResolver,
				app->enrichmentGeneration, &lyrics)) {
			SbLyricsResultDestroy (&app->lyrics); app->lyrics = lyrics;
			SbUiModelSetSyncedLyrics (&app->uiModel,
					lyrics.status == SB_LOOKUP_AVAILABLE ? lyrics.syncedLyrics : NULL);
			if (lyrics.status == SB_LOOKUP_AVAILABLE)
				tuiDebugPrint ("lyrics_sync mode=%s lines=%zu\n",
						app->uiModel.syncedLyrics.count > 0 ? "synced" : "plain",
						app->uiModel.syncedLyrics.count);
			tuiDebugPrint ("lyrics_sync identity pandora_artist=\"%s\" pandora_title=\"%s\" pandora_album=\"%s\" pandora_duration=%u lrclib_artist=\"%s\" lrclib_title=\"%s\" lrclib_album=\"%s\" lrclib_duration=%.3f\n",
					app->trackIdentity.artist, app->trackIdentity.title,
					app->trackIdentity.album, app->trackIdentity.duration,
					lyrics.artist, lyrics.title, lyrics.album, lyrics.duration);
			if (app->uiModel.syncedLyrics.count > 0 && app->trackIdentity.duration > 0) {
				const int64_t finalMs = app->uiModel.syncedLyrics.lines[
						app->uiModel.syncedLyrics.count - 1].timestamp_ms;
				if (finalMs > (int64_t) app->trackIdentity.duration * 1000 + 15000)
					tuiDebugPrint ("lyrics_sync suspicious_timeline final_timestamp_ms=%lld track_duration_ms=%lld\n",
							(long long) finalMs,
							(long long) app->trackIdentity.duration * 1000);
			}
			tuiDebugPrint ("enrichment result provider=lrclib generation=%llu state=%d detail=%s\n",
					(unsigned long long) app->enrichmentGeneration,
					(int) lyrics.status, lyrics.error);
			SbUiRendererRender (&app->uiRenderer, &app->uiModel, SB_UI_RENDER_STATE);
		}
		SbAlbumArtResult art;
		if (SbAlbumArtResolverPoll(&app->metadataResolver,app->enrichmentGeneration,&art)) {
			app->albumArt=art; app->uiModel.artState=art.status;
			snprintf(app->uiModel.artProvider,sizeof(app->uiModel.artProvider),"%s",art.provider);
			snprintf(app->uiModel.artCachedPath,sizeof(app->uiModel.artCachedPath),"%s",art.cachedPath);
			app->uiModel.artWidth=art.width; app->uiModel.artHeight=art.height;
			tuiDebugPrint("art state=%s generation=%llu path=%s\n",art.reason,
					(unsigned long long)app->enrichmentGeneration,art.cachedPath);
			SbUiRendererRender(&app->uiRenderer,&app->uiModel,SB_UI_RENDER_STATE);
		}
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
	if (app->prefetch.inFlight) {
		pthread_join (app->prefetch.thread, NULL);
		PianoDestroyPlaylist (app->prefetch.result);
		free (app->prefetch.stationId);
		app->prefetch.inFlight = false;
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
	pthread_mutexattr_t pianoLockAttr;
	pthread_mutexattr_init (&pianoLockAttr);
	pthread_mutexattr_settype (&pianoLockAttr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init (&app.pianoLock, &pianoLockAttr);
	pthread_mutexattr_destroy (&pianoLockAttr);
	pthread_mutex_init (&app.prefetch.lock, NULL);
	app.prefetch.lastAttemptRemaining = SIZE_MAX;
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
	(void) tuiDebugInit (app.useTui);
	BarPlayerConfigureAvLogging (app.useTui);

#if defined(SIGNALBOX_PDCURSES_WINCON)
	if (app.useTui)
		fputs ("[signalbox:windows] renderer=wincon input=win32_event\n", stderr);
#elif defined(SIGNALBOX_PDCURSES_VT)
	if (app.useTui)
		fputs ("[signalbox:windows] renderer=vt input=win32_event\n", stderr);
#endif

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
	SbMetadataResolverInit (&app.metadataResolver);
	SbMetadataResolverStart (&app.metadataResolver);
	SbMetadataResultInit (&app.metadata);
	SbLyricsResultInit (&app.lyrics);

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
	SbMetadataResolverDestroy (&app.metadataResolver);
	SbLyricsResultDestroy (&app.lyrics);
	curl_easy_cleanup (app.http);
	curl_global_cleanup ();
	BarPlayerDestroy (&app.player);
	SbUiRendererShutdown (&app.uiRenderer);
	SbUiRendererSetActive (NULL);
	SbUiModelDestroy (&app.uiModel);
	BarSettingsDestroy (&app.settings);
	pthread_mutex_destroy (&app.prefetch.lock);
	pthread_mutex_destroy (&app.pianoLock);

	/* restore terminal attributes, zsh doesn't need this, bash does... */
	BarTermRestore ();

	return 0;
}
