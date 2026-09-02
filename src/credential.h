#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SB_CREDENTIAL_SERVICE "org.signalbox.pandora"

typedef enum {
	SB_CREDENTIAL_OK = 0,
	SB_CREDENTIAL_NOT_FOUND,
	SB_CREDENTIAL_UNAVAILABLE,
	SB_CREDENTIAL_ERROR,
} SbCredentialStatus;

bool SbCredentialBackendAvailable (void);
SbCredentialStatus SbCredentialLoad (const char *, const char *, char **);
SbCredentialStatus SbCredentialStore (const char *, const char *, const char *);
SbCredentialStatus SbCredentialDelete (const char *, const char *);
void SbCredentialClear (void *, size_t);
void SbCredentialFreeSecret (char *);
