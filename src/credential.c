#include <stdlib.h>
#include <string.h>

#include "credential.h"

#ifdef __APPLE__
#include <Security/Security.h>

static CFStringRef SbCredentialString (const char *value) {
	return CFStringCreateWithCString (kCFAllocatorDefault, value,
			kCFStringEncodingUTF8);
}

static CFMutableDictionaryRef SbCredentialQuery (const char *service,
		const char *account) {
	CFStringRef serviceValue = SbCredentialString (service);
	CFStringRef accountValue = SbCredentialString (account);
	if (serviceValue == NULL || accountValue == NULL) {
		if (serviceValue != NULL) CFRelease (serviceValue);
		if (accountValue != NULL) CFRelease (accountValue);
		return NULL;
	}
	CFMutableDictionaryRef query = CFDictionaryCreateMutable (kCFAllocatorDefault,
			0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (query != NULL) {
		CFDictionarySetValue (query, kSecClass, kSecClassGenericPassword);
		CFDictionarySetValue (query, kSecAttrService, serviceValue);
		CFDictionarySetValue (query, kSecAttrAccount, accountValue);
	}
	CFRelease (serviceValue);
	CFRelease (accountValue);
	return query;
}
#endif

void SbCredentialClear (void *data, size_t size) {
	volatile unsigned char *p = data;
	while (size-- > 0) *p++ = 0;
}

void SbCredentialFreeSecret (char *secret) {
	if (secret != NULL) {
		SbCredentialClear (secret, strlen (secret));
		free (secret);
	}
}

bool SbCredentialBackendAvailable (void) {
#ifdef __APPLE__
	return true;
#else
	return false;
#endif
}

SbCredentialStatus SbCredentialLoad (const char *service, const char *account,
		char **secretOut) {
	if (secretOut == NULL || service == NULL || account == NULL) {
		return SB_CREDENTIAL_ERROR;
	}
	*secretOut = NULL;
#ifdef __APPLE__
	CFMutableDictionaryRef query = SbCredentialQuery (service, account);
	if (query == NULL) return SB_CREDENTIAL_ERROR;
	CFDictionarySetValue (query, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue (query, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef result = NULL;
	const OSStatus status = SecItemCopyMatching (query, &result);
	CFRelease (query);
	if (status == errSecItemNotFound) return SB_CREDENTIAL_NOT_FOUND;
	if (status != errSecSuccess) return SB_CREDENTIAL_ERROR;
	CFDataRef data = (CFDataRef) result;
	const CFIndex length = CFDataGetLength (data);
	char *secret = malloc ((size_t) length + 1);
	if (secret == NULL) {
		CFRelease (data);
		return SB_CREDENTIAL_ERROR;
	}
	memcpy (secret, CFDataGetBytePtr (data), (size_t) length);
	secret[length] = '\0';
	CFRelease (data);
	*secretOut = secret;
	return SB_CREDENTIAL_OK;
#else
	(void) service; (void) account;
	return SB_CREDENTIAL_UNAVAILABLE;
#endif
}

SbCredentialStatus SbCredentialStore (const char *service, const char *account,
		const char *secret) {
	if (service == NULL || account == NULL || secret == NULL) {
		return SB_CREDENTIAL_ERROR;
	}
#ifdef __APPLE__
	CFMutableDictionaryRef query = SbCredentialQuery (service, account);
	if (query == NULL) return SB_CREDENTIAL_ERROR;
	CFDataRef data = CFDataCreate (kCFAllocatorDefault,
			(const UInt8 *) secret, (CFIndex) strlen (secret));
	if (data == NULL) { CFRelease (query); return SB_CREDENTIAL_ERROR; }
	const void *keys[] = {kSecValueData};
	const void *values[] = {data};
	CFDictionaryRef update = CFDictionaryCreate (kCFAllocatorDefault, keys,
			values, 1, &kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);
	OSStatus status = SecItemUpdate (query, update);
	if (status == errSecItemNotFound) {
		CFDictionarySetValue (query, kSecValueData, data);
		status = SecItemAdd (query, NULL);
	}
	CFRelease (update); CFRelease (data); CFRelease (query);
	return status == errSecSuccess ? SB_CREDENTIAL_OK : SB_CREDENTIAL_ERROR;
#else
	(void) service; (void) account; (void) secret;
	return SB_CREDENTIAL_UNAVAILABLE;
#endif
}

SbCredentialStatus SbCredentialDelete (const char *service,
		const char *account) {
	if (service == NULL || account == NULL) return SB_CREDENTIAL_ERROR;
#ifdef __APPLE__
	CFMutableDictionaryRef query = SbCredentialQuery (service, account);
	if (query == NULL) return SB_CREDENTIAL_ERROR;
	const OSStatus status = SecItemDelete (query);
	CFRelease (query);
	if (status == errSecItemNotFound) return SB_CREDENTIAL_NOT_FOUND;
	return status == errSecSuccess ? SB_CREDENTIAL_OK : SB_CREDENTIAL_ERROR;
#else
	(void) service; (void) account;
	return SB_CREDENTIAL_UNAVAILABLE;
#endif
}
