#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void (*shutdownHandler) (void);

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>

static BOOL WINAPI SbPlatformConsoleHandler (DWORD event) {
	if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
			event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT ||
			event == CTRL_SHUTDOWN_EVENT) {
		if (shutdownHandler != NULL) shutdownHandler ();
		return TRUE;
	}
	return FALSE;
}

static char *SbPlatformWideToUtf8 (const wchar_t *value) {
	const int size = WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS, value,
			-1, NULL, 0, NULL, NULL);
	if (size <= 0) return NULL;
	char *result = malloc ((size_t) size);
	if (result == NULL || WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS,
			value, -1, result, size, NULL, NULL) <= 0) {
		free (result);
		return NULL;
	}
	return result;
}
#else
#include <signal.h>

static void SbPlatformSignalHandler (int signalNumber) {
	(void) signalNumber;
	if (shutdownHandler != NULL) shutdownHandler ();
}
#endif

char *SbPlatformJoinPath (const char *directory, const char *filename) {
	if (directory == NULL || filename == NULL) return NULL;
#ifdef _WIN32
	const char separator = '\\';
#else
	const char separator = '/';
#endif
	const size_t directoryLength = strlen (directory);
	const bool hasSeparator = directoryLength > 0 &&
			(directory[directoryLength - 1] == '/' ||
			directory[directoryLength - 1] == '\\');
	const size_t size = directoryLength + (hasSeparator ? 0 : 1) +
			strlen (filename) + 1;
	char *path = malloc (size);
	if (path != NULL) snprintf (path, size, "%s%s%s", directory,
			hasSeparator ? "" : (char [2]) {separator, '\0'}, filename);
	return path;
}

char *SbPlatformConfigPath (const char *filename) {
	if (filename == NULL) return NULL;
#ifdef _WIN32
	wchar_t *roaming = NULL;
	if (SHGetKnownFolderPath (&FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, NULL,
			&roaming) != S_OK) return NULL;
	char *base = SbPlatformWideToUtf8 (roaming);
	CoTaskMemFree (roaming);
	if (base == NULL) return NULL;
	char *directory = SbPlatformJoinPath (base, "Signalbox");
	free (base);
	if (directory == NULL) return NULL;
	char *path = SbPlatformJoinPath (directory, filename);
	free (directory);
	return path;
#else
	const char *base = getenv ("XDG_CONFIG_HOME");
	if (base == NULL || *base == '\0') return NULL;
	char *directory = SbPlatformJoinPath (base, "signalbox");
	if (directory == NULL) return NULL;
	char *path = SbPlatformJoinPath (directory, filename);
	free (directory);
	return path;
#endif
}

uint64_t SbPlatformMonotonicMs (void) {
#ifdef _WIN32
	return (uint64_t) GetTickCount64 ();
#else
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return (uint64_t) now.tv_sec * 1000u + (uint64_t) now.tv_nsec / 1000000u;
#endif
}

bool SbPlatformLocalTime (const time_t value, struct tm *result) {
	if (result == NULL) return false;
#ifdef _WIN32
	return localtime_s (result, &value) == 0;
#else
	return localtime_r (&value, result) != NULL;
#endif
}

bool SbPlatformInstallShutdownHandler (void (*handler) (void)) {
	shutdownHandler = handler;
#ifdef _WIN32
	return SetConsoleCtrlHandler (SbPlatformConsoleHandler, TRUE) != 0;
#else
	struct sigaction action = {.sa_handler = SbPlatformSignalHandler};
	sigemptyset (&action.sa_mask);
	action.sa_flags = 0;
	return sigaction (SIGINT, &action, NULL) == 0;
#endif
}
