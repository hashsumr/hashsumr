#ifndef __HASHSUM_H__
#define __HASHSUM_H__

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif
#include <errno.h>
#include "blake3/blake3.h"
#include "pthread_compat/pthread_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EVP_MAX_MD_SIZE
#define EVP_MAX_MD_SIZE	128
#endif
#define	HASHSUM_MAX_DIGEST_SIZE EVP_MAX_DIGEST_SIZE

typedef union ctx_s {
#ifdef _WIN32
	struct {
		BCRYPT_ALG_HANDLE hAlg;
		BCRYPT_HASH_HANDLE hHash;
		DWORD len;
	} bcryptHandle;
#else
	EVP_MD_CTX *evp;
#endif
	blake3_hasher *b3hasher;
}	ctx_t;

typedef ctx_t* (*ctx_new_t)();
typedef int    (*ctx_init_t)(ctx_t *ctx, void *arg);
typedef void   (*ctx_free_t)(ctx_t *ctx);
typedef int    (*ctx_update_t)(ctx_t *ctx, void *buf, size_t bufsz);
typedef int    (*ctx_final_t)(ctx_t *ctx, unsigned char *digest, unsigned int *dlen);

typedef struct md_s {
	const char *name;
	void *arginit; /* argument passed to finit */
	ctx_new_t    fnew;
	ctx_init_t   finit;
	ctx_free_t   ffree;
	ctx_update_t fupdate;
	ctx_final_t  ffinal;
}	md_t;

#define	EVP_MAX_DIGEST_SIZE	((EVP_MAX_MD_SIZE<<1) + 2)
#define	ERRMSG_SIZE	256

typedef struct job_s {
	pthread_mutex_t mutex;
	md_t *md;
	char *filename;
	unsigned long long checked;
	unsigned long long filesz;
	long code;	/* job state code */
	unsigned int hashlen;
	unsigned char hash[EVP_MAX_MD_SIZE];
	char digest[EVP_MAX_DIGEST_SIZE];
	char dcheck[EVP_MAX_DIGEST_SIZE];	/* for opt_check */
	char errmsg[ERRMSG_SIZE];
}	job_t;

typedef void   (*visualizer_t)(job_t *job, void *arg);

/* state codes */

enum {           // job state codes
	STATE_UNKNOWN = 0,
	STATE_DONE,	 // done!
	ERR_STAT,    // stat(2) failed
	ERR_MISSING, // file not found
	ERR_NOTREG,  // not a regular file
	ERR_OPEN,    // open(2) failed
	ERR_INIT,    // hash init failed
	ERR_UPDATE,  // hash update failed
	ERR_FINAL,   // hash final failaed
};

char * herrmsg(char *buf, size_t sz, int errnum);

md_t * get_hashes();
md_t * lookup_hash(const char *name);
void * hash1(job_t *job, visualizer_t vzer, void *varg);

#ifdef _WIN32
#define close	_close
#define read	_read
#define strdup	_strdup
#endif

#ifdef __cplusplus
}
#endif

#endif /* __HASHSUM_H__ */
