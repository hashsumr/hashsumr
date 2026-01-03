#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "hashsum.h"
#include "wrappers-openssl.h"
#include "wrappers-blake3.h"

/* available algorithms */
#define OPENSSL_TYPICAL	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final
#define OPENSSL_XOF32	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final_xof32
#define OPENSSL_XOF64	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final_xof64

md_t algs[] = {
	{ "SHA1",	EVP_sha1, OPENSSL_TYPICAL },
	{ "SHA224",	EVP_sha224, OPENSSL_TYPICAL },
	{ "SHA256",	EVP_sha256, OPENSSL_TYPICAL },
	{ "SHA384",	EVP_sha384, OPENSSL_TYPICAL },
	{ "SHA512",	EVP_sha512, OPENSSL_TYPICAL },
	{ "SHA512/224",	EVP_sha512_224, OPENSSL_TYPICAL },
	{ "SHA512/256",	EVP_sha512_256, OPENSSL_TYPICAL },
	{ "SHA3/224",	EVP_sha3_224, OPENSSL_TYPICAL },
	{ "SHA3/256",	EVP_sha3_256, OPENSSL_TYPICAL },
	{ "SHA3/384",	EVP_sha3_384, OPENSSL_TYPICAL },
	{ "SHA3/512",	EVP_sha3_512, OPENSSL_TYPICAL },
	{ "SHAKE128",	EVP_shake128, OPENSSL_XOF32 },
	{ "SHAKE256",	EVP_shake256, OPENSSL_XOF64 },
#ifndef OPENSSL_NO_MD5
	{ "MD5",	EVP_md5,    OPENSSL_TYPICAL },
#endif
#ifndef OPENSSL_NO_BLAKE2
	{ "BLAKE2b", EVP_blake2b512,  OPENSSL_TYPICAL },
	{ "BLAKE2s", EVP_blake2s256,  OPENSSL_TYPICAL },
#endif
	{ "BLAKE3",  NULL, blake3_new, blake3_init, blake3_free, blake3_update, blake3_final },
	{ NULL, NULL }
};

char *
digest(unsigned char *hash, unsigned int hlen, char *digest, unsigned int dlen) {
	int i, sz, clen = 0;
	char *wptr = digest;
	for (i = 0; i < hlen; i++) {
		sz = snprintf(wptr, dlen-clen, "%02x", hash[i]);
		clen += sz;
		wptr += sz;
	}
	return digest;
}

long
jobstate(job_t *job, long code, const char *fmt, ...) {
	va_list ap;
	job->code = code;
	va_start(ap, fmt);
	vsnprintf(job->errmsg, ERRMSG_SIZE, fmt, ap);
	va_end(ap);
	return code;
}

void *
hash1(job_t *job, visualizer_t vzer, void *varg) {
	int fd = -1, sz;
	unsigned char buf[32768];
	struct stat st;
	ctx_t *ctx = job->md->fnew();
	long state = STATE_UNKNOWN;

	job->checked = 0;

	if(stat(job->filename, &st) < 0) {
		if(errno == ENOENT)
			return (void *) jobstate(job, ERR_MISSING, "file not found");
		return (void *) jobstate(job, ERR_STAT, "stat failed (%d): %s", errno, strerror(errno));
	}

	if(S_ISREG(st.st_mode) == 0) {
		if(S_ISDIR(st.st_mode)) {
			return (void *) jobstate(job, ERR_NOTREG, "is a directory", st.st_mode);
		}
		if(S_ISFIFO(st.st_mode)) {
			return (void *) jobstate(job, ERR_NOTREG, "is a fifo", st.st_mode);
		}
		if(S_ISSOCK(st.st_mode)) {
			return (void *) jobstate(job, ERR_NOTREG, "is a socket", st.st_mode);
		}
		return (void *) jobstate(job, ERR_NOTREG, "not a regular file (0x%x)", st.st_mode);
	}

	job->filesz = st.st_size;

	if(job->md->finit(ctx, job->md->arginit) != 1) {
		return (void *) jobstate(job, ERR_INIT, "hash init failed");
	}

	if((fd = open(job->filename, O_RDONLY)) < 0) {
		return (void *) jobstate(job, ERR_OPEN, "open failed (%d): %s", errno, strerror(errno));
	}

	while((sz = read(fd, buf, sizeof(buf))) > 0) {
		if(job->md->fupdate(ctx, buf, sz) != 1) {
			state = jobstate(job, ERR_UPDATE, "hash update failed");
			goto cleanup;
		}
		job->checked += sz;
		if(vzer != NULL) vzer(job, varg);
	}

	if(job->md->ffinal(ctx, job->hash, &job->hashlen) != 1) {
		state = jobstate(job, ERR_FINAL, "hash final failed");
		goto cleanup;
	}

	digest(job->hash, job->hashlen, job->digest, HASHSUM_MAX_DIGEST_SIZE);
	state = job->code = STATE_DONE;

cleanup:
	if(fd > -1) close(fd);
	job->md->ffree(ctx);

	return (void *) state;
}

