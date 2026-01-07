#include <stdlib.h>

#ifndef _WIN32

#include "wrappers-openssl.h"

ctx_t *
openssl_new() {
	ctx_t *ctx = (ctx_t *) malloc(sizeof(ctx_t));
	if(ctx == NULL) return NULL;
	if((ctx->evp = EVP_MD_CTX_new()) == NULL) {
		free(ctx);
		ctx = NULL;
	}
	return ctx;
}

typedef EVP_MD * (*evp_md_t )(void);

int
openssl_init(ctx_t *ctx, void *arg) {
	evp_md_t fevp = (evp_md_t) arg;
	if(arg == NULL) return 0;
	return EVP_DigestInit_ex(ctx->evp, fevp(), NULL);
}

void
openssl_free(ctx_t *ctx) {
	if(ctx == NULL) return;
	EVP_MD_CTX_free(ctx->evp);
	free(ctx);
}

int
openssl_update(ctx_t *ctx, void *buf, size_t bufsz) {
	return EVP_DigestUpdate(ctx->evp, buf, bufsz);
}

int
openssl_final(ctx_t *ctx, unsigned char *digest, unsigned int *dlen) {
	return EVP_DigestFinal_ex(ctx->evp, digest, dlen);
}

int
openssl_final_xof(ctx_t *ctx, unsigned int outlen, unsigned char *digest, unsigned int *dlen) {
	if(EVP_DigestFinalXOF(ctx->evp, digest, outlen) == 1) {
		*dlen = outlen;
		return 1;
	}
	return 0;
}

int
openssl_final_xof32(ctx_t *ctx, unsigned char *digest, unsigned int *dlen) {
	return openssl_final_xof(ctx, 32, digest, dlen);
}

int
openssl_final_xof64(ctx_t *ctx, unsigned char *digest, unsigned int *dlen) {
	return openssl_final_xof(ctx, 64, digest, dlen);
}

#endif
