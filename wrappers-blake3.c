#include <stdlib.h>
#include "wrappers-blake3.h"

ctx_t *
blake3_new() {
	ctx_t *ctx = (ctx_t *) malloc(sizeof(ctx_t));
	if(ctx == NULL) return NULL;
	if((ctx->b3hasher = (blake3_hasher*) malloc(sizeof(blake3_hasher))) == NULL) {
		free(ctx);
		ctx = NULL;
	}
	return ctx;
}

typedef EVP_MD * (*evp_md_t )(void);

int
blake3_init(ctx_t *ctx, void *arg) {
	blake3_hasher_init(ctx->b3hasher);
	return 1;
}

void
blake3_free(ctx_t *ctx) {
	if(ctx == NULL) return;
	free(ctx->b3hasher);
	free(ctx);
}

int
blake3_update(ctx_t *ctx, const void *buf, size_t bufsz) {
	blake3_hasher_update(ctx->b3hasher, buf, bufsz);
	return 1;
}

int
blake3_final(ctx_t *ctx, unsigned char *digest, unsigned int *dlen) {
	*dlen = BLAKE3_OUT_LEN;
	blake3_hasher_finalize(ctx->b3hasher, digest, BLAKE3_OUT_LEN);
	return 1;
}

