#ifndef __WRAPPER_WIN32_H__
#define __WRAPPER_WIN32_H__

#include "hashsumr.h"

#ifdef _WIN32
/* openssl wrappers */
ctx_t* bcrypt_new();
int    bcrypt_init(ctx_t *ctx, void *arg);
void   bcrypt_free(ctx_t *ctx);
int    bcrypt_update(ctx_t *ctx, void *buf, size_t bufsz);
int    bcrypt_final(ctx_t *ctx, unsigned char *digest, unsigned int *dlen);
#endif	/* _WIN32 */

#endif	/* __WRAPPER_OPENSSL_H__ */
