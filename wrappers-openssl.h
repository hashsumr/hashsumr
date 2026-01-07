#ifndef __WRAPPER_OPENSSL_H__
#define __WRAPPER_OPENSSL_H__

#include "hashsum.h"

/* openssl wrappers */
ctx_t* openssl_new();
int    openssl_init(ctx_t *ctx, void *arg);
void   openssl_free(ctx_t *ctx);
int    openssl_update(ctx_t *ctx, void *buf, size_t bufsz);
int    openssl_final(ctx_t *ctx, unsigned char *digest, unsigned int *dlen);
int    openssl_final_xof(ctx_t *ctx, unsigned int outlen, unsigned char *digest, unsigned int *dlen);
int    openssl_final_xof32(ctx_t *ctx, unsigned char *digest, unsigned int *dlen);
int    openssl_final_xof64(ctx_t *ctx, unsigned char *digest, unsigned int *dlen);

#endif	/* __WRAPPER_OPENSSL_H__ */
