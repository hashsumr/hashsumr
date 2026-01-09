#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "hashsum.h"
#ifdef _WIN32
#include "wrappers-win32.h"
#else
#include "wrappers-openssl.h"
#endif
#include "wrappers-blake3.h"

/* available algorithms */
#define OPENSSL_TYPICAL	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final
#define OPENSSL_XOF32	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final_xof32
#define OPENSSL_XOF64	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final_xof64

#define BCRYPT_TYPICAL	bcrypt_new, bcrypt_init, bcrypt_free, bcrypt_update, bcrypt_final

md_t algs[] = {
#ifdef _WIN32
	{ "MD5",	BCRYPT_MD5_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA1",	BCRYPT_SHA1_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA256",	BCRYPT_SHA256_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA384",	BCRYPT_SHA384_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA512",	BCRYPT_SHA512_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA3/256",	BCRYPT_SHA3_256_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA3/384",	BCRYPT_SHA3_384_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHA3/512",	BCRYPT_SHA3_512_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHAKE128",	BCRYPT_SHAKE128_ALGORITHM, BCRYPT_TYPICAL },
	{ "SHAKE256",	BCRYPT_SHAKE256_ALGORITHM, BCRYPT_TYPICAL },
#else
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
#endif
	{ "BLAKE3",  NULL, blake3_new, blake3_init, blake3_free, blake3_update, blake3_final },
	{ NULL, NULL }
};

char *	/* should be thread-safe */
herrmsg(char *buf, size_t sz, int errnum) {
#ifdef _WIN32
	strerror_s(buf, sizeof(buf), errnum);
#else
	snprintf(buf, sz, "%s", strerror(errnum));
#endif
	return buf;
}

md_t *
get_hashes() {
	return algs;
}

md_t *
lookup_hash(const char *name) {
	int idx = 0;
	while(algs[idx].name != NULL) {
#ifdef _WIN32
		if(_stricmp(name, algs[idx].name) == 0)
#else
		if(strcasecmp(name, algs[idx].name) == 0)
#endif
			return &algs[idx];
		idx++;
	}
	return NULL;
}

char *
digest(unsigned char *hash, unsigned int hlen, char *digest, unsigned int dlen) {
	unsigned int i;
	int sz, clen = 0;
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

int
get_fileinfo(const char *filename, unsigned long long *sz, int *type) {
#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA fileInfo;
	LARGE_INTEGER li;
	if(GetFileAttributesExA(filename, GetFileExInfoStandard, &fileInfo) == 0) {
		DWORD err = GetLastError();
		switch(err) {
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return ENOENT;
		case ERROR_ACCESS_DENIED:
			return EPERM;
		}
		return EINVAL;
	}
	li.HighPart = fileInfo.nFileSizeHigh;
	li.LowPart  = fileInfo.nFileSizeLow;
	*sz = li.QuadPart;
	if(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		*type = S_IFDIR;
	} else if(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		*type = 0;
	} else {
		*type = S_IFREG;
	}
#else
	struct stat st;
	if(stat(filename, &st) < 0)
		return errno;
	*sz = st.st_size;
	if(S_ISREG(st.st_mode)) {
		*type = S_IFREG;
	} else if(S_ISDIR(st.st_mode)) {
		*type = S_IFDIR;
	} else if(S_ISSOCK(st.st_mode)) {
		*type = S_IFSOCK;
	} else if(S_ISFIFO(st.st_mode)) {
		*type = S_IFIFO;
	} else {
		*type = 0;
	}
#endif
	return 0;
}

void *
hash1(job_t *job, visualizer_t vzer, void *varg) {
	int fd = -1, sz;
	char buf[32768];
	ctx_t *ctx = job->md->fnew();
	long state = STATE_UNKNOWN;
	int err, ftype;
	unsigned long long fsize;

	job->checked = 0;

	if((err = get_fileinfo(job->filename, &fsize, &ftype)) != 0) {
		if(err == ENOENT)
			return (void *) jobstate(job, ERR_MISSING, "no such file or directory");
		return (void *) jobstate(job, ERR_STAT, "stat failed (%d): %s", err,
			herrmsg(buf, sizeof(buf), err));
	}

	if(ftype != S_IFREG) {
		if(ftype == S_IFDIR) {
			return (void *) jobstate(job, ERR_NOTREG, "is a directory");
		}
#ifndef _WIN32
		if(ftype == S_IFIFO) {
			return (void *) jobstate(job, ERR_NOTREG, "is a fifo");
		}
		if(ftype == S_IFSOCK) {
			return (void *) jobstate(job, ERR_NOTREG, "is a socket");
		}
#endif
		return (void *) jobstate(job, ERR_NOTREG, "not a regular file");
	}

	job->filesz = fsize;

	if(job->md->finit(ctx, job->md->arginit) != 1) {
		return (void *) jobstate(job, ERR_INIT, "hash init failed");
	}

#ifdef _WIN32
	if(_sopen_s(&fd, job->filename, O_RDONLY|_O_BINARY, _SH_DENYWR, _S_IREAD) != 0) {
#else
	if((fd = open(job->filename, O_RDONLY)) < 0) {
#endif
		return (void *) jobstate(job, ERR_OPEN, "open failed (%d): %s", errno,
			herrmsg(buf, sizeof(buf), errno));
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

