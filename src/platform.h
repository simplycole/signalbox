#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Returned paths are UTF-8 and owned by the caller. */
char *SbPlatformConfigPath (const char *filename);
char *SbPlatformJoinPath (const char *directory, const char *filename);
uint64_t SbPlatformMonotonicMs (void);
bool SbPlatformLocalTime (time_t value, struct tm *result);
bool SbPlatformInstallShutdownHandler (void (*handler) (void));
#ifdef _WIN32
bool SbPlatformWaitForConsoleInput (int timeoutMs);
void SbPlatformSleepMs (unsigned int milliseconds);
#endif
