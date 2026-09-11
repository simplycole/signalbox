#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#endif

static void (*shutdownHandler) (void);

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>

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

static wchar_t *SbPlatformUtf8ToWide (const char *value) {
	const int size = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, value,
			-1, NULL, 0);
	if (size <= 0) return NULL;
	wchar_t *result = malloc ((size_t) size * sizeof (*result));
	if (result == NULL || MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
			value, -1, result, size) <= 0) {
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

#ifdef _WIN32
char *SbPlatformFindExecutableSibling (const char *filename) {
	if (filename == NULL) return NULL;

	const DWORD capacity = 32768;
	wchar_t *module = malloc ((size_t) capacity * sizeof (*module));
	if (module == NULL) return NULL;
	const DWORD length = GetModuleFileNameW (NULL, module, capacity);
	if (length == 0 || length >= capacity) {
		free (module);
		return NULL;
	}

	wchar_t *separator = wcsrchr (module, L'\\');
	if (separator == NULL) separator = wcsrchr (module, L'/');
	if (separator == NULL) {
		free (module);
		return NULL;
	}
	const size_t directoryLength = (size_t) (separator - module + 1);
	wchar_t *wideFilename = SbPlatformUtf8ToWide (filename);
	if (wideFilename == NULL) {
		free (module);
		return NULL;
	}
	const size_t filenameLength = wcslen (wideFilename);
	wchar_t *path = malloc ((directoryLength + filenameLength + 1) *
			sizeof (*path));
	if (path == NULL) {
		free (wideFilename);
		free (module);
		return NULL;
	}
	wmemcpy (path, module, directoryLength);
	wmemcpy (path + directoryLength, wideFilename, filenameLength + 1);
	free (wideFilename);
	free (module);

	const DWORD attributes = GetFileAttributesW (path);
	char *result = NULL;
	if (attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		result = SbPlatformWideToUtf8 (path);
	}
	free (path);
	return result;
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

bool SbPlatformEnsureDirectory (const char *path) {
 if (path == NULL || *path == '\0') return false;
#ifdef _WIN32
 wchar_t *wide = SbPlatformUtf8ToWide (path); if (wide == NULL) return false;
 const BOOL ok = CreateDirectoryW (wide, NULL); const DWORD error = GetLastError ();
 free (wide); return ok || error == ERROR_ALREADY_EXISTS;
#else
 return mkdir (path, 0700) == 0 || errno == EEXIST;
#endif
}

char *SbPlatformCachePath (const char *filename) {
 if (filename == NULL) return NULL;
#ifdef _WIN32
 wchar_t *local = NULL;
 if (SHGetKnownFolderPath (&FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL,
   &local) != S_OK) return NULL;
 char *base = SbPlatformWideToUtf8 (local); CoTaskMemFree (local);
#else
 const char *baseValue = getenv ("XDG_CACHE_HOME"); char *fallback = NULL;
 if (baseValue == NULL || *baseValue == '\0') {
  const char *home = getenv ("HOME"); if (home == NULL || *home == '\0') return NULL;
  fallback = SbPlatformJoinPath (home, ".cache"); baseValue = fallback;
 }
 char *base = strdup (baseValue); free (fallback);
#endif
 if (base == NULL) return NULL;
 SbPlatformEnsureDirectory (base);
 char *directory = SbPlatformJoinPath (base,
#ifdef _WIN32
   "Signalbox"
#else
   "signalbox"
#endif
 ); free (base);
 if (directory == NULL || !SbPlatformEnsureDirectory (directory)) { free (directory); return NULL; }
 char *path = SbPlatformJoinPath (directory, filename); free (directory); return path;
}

bool SbPlatformAtomicReplace (const char *temporary, const char *destination) {
#ifdef _WIN32
 wchar_t *from = SbPlatformUtf8ToWide (temporary), *to = SbPlatformUtf8ToWide (destination);
 if (from == NULL || to == NULL) { free (from); free (to); return false; }
 const bool ok = MoveFileExW (from, to, MOVEFILE_REPLACE_EXISTING |
   MOVEFILE_WRITE_THROUGH) != 0; free (from); free (to); return ok;
#else
 return rename (temporary, destination) == 0;
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

#ifdef SIGNALBOX_PDCURSES_VT
bool SbPlatformWaitForConsoleInput (const int timeoutMs) {
	const HANDLE input = GetStdHandle (STD_INPUT_HANDLE);
	if (input == NULL || input == INVALID_HANDLE_VALUE) return false;
	const DWORD timeout = timeoutMs < 0 ? INFINITE : (DWORD) timeoutMs;
	return WaitForSingleObject (input, timeout) == WAIT_OBJECT_0;
}

#endif

void SbPlatformSleepMs (const unsigned int milliseconds) {
#ifdef _WIN32
	Sleep ((DWORD) milliseconds);
#else
	struct timespec delay = {(time_t) (milliseconds / 1000),
			(long) (milliseconds % 1000) * 1000000L};
	while (nanosleep (&delay, &delay) != 0 && errno == EINTR) {}
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
