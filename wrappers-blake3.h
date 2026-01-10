#ifndef __WRAPPER_BLAKE3_H__
#define __WRAPPER_BLAKE3_H__

#include "hashsumr.h"

/* blake3 wrappers */
ctx_t* blake3_new();
int    blake3_init(ctx_t *ctx, void *arg);
void   blake3_free(ctx_t *ctx);
int    blake3_update(ctx_t *ctx, void *buf, size_t bufsz);
int    blake3_final(ctx_t *ctx, unsigned char *digest, unsigned int *dlen);

#endif	/* __WRAPPER_BLAKE3_H__ */
