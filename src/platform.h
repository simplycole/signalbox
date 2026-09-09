#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Returned paths are UTF-8 and owned by the caller. */
char *SbPlatformConfigPath (const char *filename);
char *SbPlatformJoinPath (const char *directory, const char *filename);
#ifdef _WIN32
/* Returns an absolute UTF-8 path when the executable sibling exists. */
char *SbPlatformFindExecutableSibling (const char *filename);
#endif
uint64_t SbPlatformMonotonicMs (void);
bool SbPlatformLocalTime (time_t value, struct tm *result);
bool SbPlatformInstallShutdownHandler (void (*handler) (void));
#ifdef SIGNALBOX_PDCURSES_VT
bool SbPlatformWaitForConsoleInput (int timeoutMs);
void SbPlatformSleepMs (unsigned int milliseconds);
#endif
