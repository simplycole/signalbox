#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_readline.h"

size_t BarReadline (char *buffer, const size_t size, const char *mask,
		BarReadlineFds_t *input, const BarReadlineFlags_t flags, int timeout) {
	(void) input; (void) flags; (void) timeout;
	if (buffer == NULL || size == 0) return 0;
	if (fgets (buffer, (int) size, stdin) == NULL) return 0;
	buffer[strcspn (buffer, "\r\n")] = '\0';
	if (mask != NULL) {
		for (char *p = buffer; *p != '\0'; p++) {
			if (strchr (mask, *p) == NULL) { *p = '\0'; break; }
		}
	}
	return strlen (buffer);
}

size_t BarReadlineStr (char *buffer, const size_t size,
		BarReadlineFds_t *input, const BarReadlineFlags_t flags) {
	return BarReadline (buffer, size, NULL, input, flags, -1);
}

size_t BarReadlineInt (int *result, BarReadlineFds_t *input) {
	char buffer[16];
	const size_t count = BarReadline (buffer, sizeof (buffer), "0123456789",
			input, BAR_RL_DEFAULT, -1);
	*result = atoi (buffer);
	return count;
}

bool BarReadlineYesNo (const bool defaultValue, BarReadlineFds_t *input) {
	char buffer[2];
	BarReadline (buffer, sizeof (buffer), "yYnN", input, BAR_RL_FULLRETURN, -1);
	return buffer[0] == 'y' || buffer[0] == 'Y' ||
			(defaultValue && buffer[0] == '\0');
}
