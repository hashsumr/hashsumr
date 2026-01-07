
#ifdef _WIN32

#include <stdlib.h>
#include <windows.h>
#include <bcrypt.h>
#include "wrappers-win32.h"

ctx_t *
bcrypt_new() {
	ctx_t *ctx = (ctx_t *) malloc(sizeof(ctx_t));
	if(ctx == NULL) return NULL;
	memset(ctx, 0, sizeof(ctx));
	return ctx;
}

int
bcrypt_init(ctx_t *ctx, void *arg) {
	NTSTATUS status;
	DWORD cbResult;
	status = BCryptOpenAlgorithmProvider(&ctx->bcryptHandle.hAlg, arg, NULL, 0);
	if (!BCRYPT_SUCCESS(status)) return 0;
	status = BCryptGetProperty(ctx->bcryptHandle.hAlg,
		BCRYPT_HASH_LENGTH,
		(PUCHAR) &ctx->bcryptHandle.len, sizeof(DWORD), &cbResult, 0);
	if (!BCRYPT_SUCCESS(status) || cbResult != sizeof(DWORD)) return 0;
	status = BCryptCreateHash(ctx->bcryptHandle.hAlg,
		&ctx->bcryptHandle.hHash, NULL, 0, NULL, 0, 0);
	if (!BCRYPT_SUCCESS(status)) return 0;
	return 1;
}

void
bcrypt_free(ctx_t *ctx) {
	if(ctx == NULL) return;
	free(ctx);
}

int
bcrypt_update(ctx_t *ctx, void *buf, size_t bufsz) {
	NTSTATUS status; 
	status = BCryptHashData(ctx->bcryptHandle.hHash, buf, bufsz, 0);
	if (!BCRYPT_SUCCESS(status)) return 0;
	return 1;
}

int
bcrypt_final(ctx_t *ctx, unsigned char *digest, unsigned int *dlen) {
	NTSTATUS status; 
	int ret = 1;
	*dlen = ctx->bcryptHandle.len;
	status = BCryptFinishHash(ctx->bcryptHandle.hHash, digest, *dlen, 0);
	if (!BCRYPT_SUCCESS(status)) ret = 0;
	BCryptDestroyHash(&ctx->bcryptHandle.hHash);
	BCryptCloseAlgorithmProvider(&ctx->bcryptHandle.hAlg, 0);
	return ret;
}

#endif
