#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include "hashsum.h"
#include "minibar/minibar.h"
#include "wrappers-openssl.h"
#include "wrappers-blake3.h"
#include "pthread-extra.h"

#define PREFIX	"hashsum: "
#define VERSION	"0.0.1"

/* available algorithms */
#define OPENSSL_TYPICAL	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final
#define OPENSSL_XOF32	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final_xof32
#define OPENSSL_XOF64	openssl_new, openssl_init, openssl_free, openssl_update, openssl_final_xof64

static md_t algs[] = {
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

/* options */

static md_t *opt_alg = &algs[2];
static int opt_one = 0;
static int opt_bin = 1;
static int opt_check = 0;
static int opt_tag = 0;
static int opt_zero = 0;
static int opt_workers = 0;
static int opt_np = 0;
static int opt_ignore_missing = 0;
static int opt_quiet = 0;
static int opt_status = 0;
static int opt_strict = 0;
static int opt_warn = 0;

/* global state */
static int    running = 0;
static int    njobs = 0;
static job_t *jobs = NULL;
static int    nextjob = 0;
static pthread_mutex_t mutex_jobs = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t barrier;;

/* function prototypes */
void print_digest1(job_t *job);

int
usage() {
	int algstrlen = 0;
	char algstr[1024] = "", *wptr = algstr;
	for(md_t *a = algs; a->name != NULL; a++) {
		int sz;
		sz = snprintf(wptr, sizeof(algstr)-algstrlen, " %s", a->name);
		algstrlen += sz;
		wptr += sz;
	}
	fprintf(stderr, "Usage: hashsum [OPTION]... [FILE]...\n"
"Print or check hash-based checksums.\n"
"\n"
"AVAILABLE ALGORITHMS: (case insensitive)\n"
" %s\n"
"\n"
"OPTION: (* - not implemented, for compatibility only)\n"
"  -1, --one             classic mode (no progress bar, no workers)\n"
"  -a, --algorithm       choose the algorithm (default: %s)\n"
"  -b, --binary          read in binary mode (default)\n"
"  -c, --check           read checksums from the FILEs and check them\n"
"      --tag             create a BSD-style checksum\n"
"  -t, --text            (*) read in text mode\n"
"  -z, --zero            end each output line with NUL, not newline,\n"
"                          and disable file name escaping\n"
"      --workers         set the number or parallel workers\n"
"      --np              no progress bar\n"
"\n"
"The following five options are useful only when verifying checksums:\n"
"      --ignore-missing  (*) don't fail or report status for missing files\n"
"  -q, --quiet           (*) don't print OK for each successfully verified file\n"
"      --status          (*) don't output anything, status code shows success\n"
"      --strict          (*) exit non-zero for improperly formatted checksum lines\n"
"  -w, --warn            (*) warn about improperly formatted checksum lines\n"
"\n"
"  -h, --help            display this help and exit\n"
"  -v, --version         output version information and exit\n"
		"",
		algstr,
		opt_alg->name);
	return -1;
}

int
parse_opts(int argc, char *argv[]) {
	md_t *a;
	int ch, optidx = 0;
	static struct option opts[] = {
		{ "one",             no_argument, NULL, '1' },
		{ "algorithm", required_argument, NULL, 'a' },
		{ "binary",          no_argument, NULL, 'b' },
		{ "check",           no_argument, NULL, 'c' },
		{ "tag",             no_argument, NULL,  0  },
		{ "text",            no_argument, NULL, 't' },
		{ "zero",            no_argument, NULL, 'z' },
		{ "workers",         required_argument, NULL, 0 },
		{ "np",              no_argument, NULL,  0  },
		{ "ignore-missing",  no_argument, NULL,  0  },
		{ "quiet",           no_argument, NULL, 'q' },
		{ "status",          no_argument, NULL,  0  },
		{ "strict",          no_argument, NULL,  0  },
		{ "warn",            no_argument, NULL,  0  },
		{ "help",            no_argument, NULL, 'h' },
		{ "version",         no_argument, NULL, 'v' },
		{ 0, 0 }
	};
	if(argc < 2) {
		return usage();
	}
	while((ch = getopt_long(argc, argv, "1a:bctzqhv", opts, &optidx)) != -1) {
		switch(ch) {
		case 0: /* for longopts */
			if(strcmp(opts[optidx].name, "tag") == 0) {
				opt_tag = 1;
			} else if(strcmp(opts[optidx].name, "np") == 0) {
				opt_np = 1;
			} else if(strcmp(opts[optidx].name, "workers") == 0) {
				opt_workers = strtol(optarg, NULL, 0);
				if(opt_workers < 0) opt_workers = 0;
			} else if(strcmp(opts[optidx].name, "ignore-missing") == 0) {
				opt_ignore_missing = 1;
			} else if(strcmp(opts[optidx].name, "status") == 0) {
				opt_status = 1;
			} else if(strcmp(opts[optidx].name, "strict") == 0) {
				opt_strict = 1;
			} else if(strcmp(opts[optidx].name, "warn") == 0) {
				opt_warn = 1;
			}
			break;
		case '1':
			opt_one = 1;
			break;
		case 'a':
			for(a = algs; a->name != NULL; a++) {
				if(strcasecmp(a->name, optarg) == 0) {
					opt_alg = a;
					break;
				}
			}
			if(a->name == NULL) {
				fprintf(stderr, PREFIX "unsupported algorithm `%s'.\n", optarg); 
				exit(-1);
			}
			break;
		case 'b':
			opt_bin = 1;
			break;
		case 'c':
			opt_check = 1;
			break;
		case 't':
			/* XXX: not implemented - opt_bin = 0 */
			break;
		case 'z':
			opt_zero = 1;
			break;
		case 'q':
			opt_quiet = 1;
			break;
		case 'v':
			fprintf(stderr, PREFIX "version " VERSION "\n");
			break;
		case 'h':
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;
	return optind;
}

int
get_ncores() {
	if(opt_one) {
		return 1;
	} else {
		long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
		return nprocs;
	}
}

int	/* return 0 if not escaped, otherwise > 0 (# of escaped chars) */
escape(char *input, char *output, int outlen) {
	int escaped = 0, wlen = 0;
	char *iptr = input, *optr = output;
	if(opt_zero) {
		strncpy(output, input, outlen);
		return 0;
	}
	for(iptr = input; *iptr && (outlen-wlen) > 3; iptr++) {
		wlen++;
		switch(*iptr) {
		case '\\':
			*optr++ = '\\';
			*optr++ = '\\';
			escaped++;
			wlen++;
			break;
		case '\n':
			*optr++ = '\\';
			*optr++ = 'n';
			escaped++;
			wlen++;
			break;
		case '\r':
			*optr++ = '\\';
			*optr++ = 'r';
			escaped++;
			wlen++;
			break;
		default:
			*optr++ = *iptr;
			break;
		}
	}
	*optr = '\0';
	return escaped;
}

int	/* return 0 if not unescaped, otherwise > 0 (# of unescaped chars) */
unescape(char *input) {
	int unescaped = 0;
	char *iptr = input, *optr = input;
	if(opt_zero) return 0;
	for(iptr = input; *iptr; iptr++) {
		switch(*iptr) {
		case '\\':
			unescaped++;
			switch(*(iptr+1)) {
			case '\\':
				iptr++;
				*optr++ = '\\';
				break;
			case 'n':
				iptr++;
				*optr++ = '\n';
				break;
			case 'r':
				iptr++;
				*optr++ = '\r';
				break;
			default:
				/* drop the backslash */
				unescaped--;
				break;
			}
			break;
		default:
			*optr++ = *iptr;
			break;
		}
	}
	*optr = '\0';
	return unescaped;
}

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

void
vzupdater(job_t *job, void *arg) {
	minibar_t *bar = (minibar_t*) arg;
	minibar_setvalue(bar, ((double) job->checked / (double) job->filesz) * 100.0);
}

void *
visualizer(void *__) {
	while(running) {
		minibar_refresh();
		sleep(1);
	}
	return NULL;
}

void *
worker(void *__) {
	int count = 0;
	visualizer_t updater = vzupdater;
	job_t *job;
	if(opt_np) updater = NULL;
	while(1) {
		minibar_t *bar = NULL;
		/* get a job */
		pthread_mutex_lock(&mutex_jobs);
		if(nextjob < njobs) {
			job = &jobs[nextjob++];
		} else {
			job = NULL;
		}
		pthread_mutex_unlock(&mutex_jobs);
		if(job == NULL) goto quit;
		count++;
		/* run the job */
		if(opt_np == 0)
			bar = minibar_get(job->filename);
		hash1(job, updater, bar);
		if(opt_np == 0)
			minibar_complete(bar);
		else
			print_digest1(job);
	}
quit:
	pthread_barrier_wait(&barrier);
	return NULL;
}

void
print_digest1(job_t *job) {
	int escaped;
	char EOL = opt_zero ? '\0' : '\n';
	char escname[PATH_MAX];
	escaped = escape(job->filename, escname, sizeof(escname));
	if(job->code == STATE_UNKNOWN) {
		fprintf(stderr, "%s: INVALID JOB STATE, PLEASE REPORT!\n", escname);
		return;
	}
	if(job->code != STATE_DONE) {
		fprintf(stderr, PREFIX "%s: %s\n", escname, job->errmsg);
		return;
	}
	if(opt_tag == 0) {
		printf("%s%s %c%s%c",
			escaped > 0 ? "\\" : "",
			job->digest,
			opt_bin ? '*' : ' ',
			escname, EOL);
	} else {
		printf("%s%s (%s) = %s%c",
			escaped > 0 ? "\\" : "",
			job->md->name, escname, job->digest, EOL);
	}
}

void
print_digest(int njobs, job_t *jobs) {
	for(int i = 0; i < njobs; i++) {
		print_digest1(&jobs[i]);
	}
}

void
print_check(int njobs, job_t *jobs) {
}

int
main(int argc, char *argv[]) {
	int i, idx, err;
	int ncores = get_ncores();
	pthread_t tid;

	if((idx = parse_opts(argc, argv)) < 0) return -1;

	if(opt_check == 0) {
		njobs = argc - idx;
		if((jobs = (job_t *) malloc(sizeof(job_t) * njobs)) == NULL) {
			perror("malloc");
			return -1;
		}
		bzero(jobs, sizeof(job_t) * njobs);
		for(i = 0; i < njobs; i++) {
			if(opt_one == 0)
				pthread_mutex_init(&jobs[i].mutex, NULL);
			jobs[i].md = opt_alg;
			jobs[i].filename = strdup(argv[idx+i]);
		}
	} else {
		/* TODO */
	}

	if(opt_workers <= 0) opt_workers = 1 + (ncores>>1);
	if(opt_workers > njobs) opt_workers = njobs;
	fprintf(stderr, PREFIX "%d processor(s) detected; workers = %d.\n", ncores, opt_workers);

	if(opt_one) {
		for(i = 0; i < njobs; i++) {
			hash1(&jobs[i], NULL, NULL);
			print_digest1(&jobs[i]);
		}
	} else {
		if(opt_np == 0) {
			if(minibar_open(stderr, opt_workers) < 0) {
				fprintf(stderr, PREFIX "minibar init failed.\n");
				abort();
			}
			/* create visualizer */
			running = 1;
			if((err = pthread_create(&tid, NULL, visualizer, NULL)) != 0) {
				fprintf(stderr, PREFIX "create visualizer thread failed (%d): %s\n",
					err, strerror(err));
				abort();
			}
		}
		/* setup barrier */
		if((err = pthread_barrier_init(&barrier, NULL, opt_workers+1)) != 0) {
			fprintf(stderr, PREFIX "create thread failed (%d): %s\n",
				err, strerror(err));
			abort();
		}
		/* run workers */
		for(i = 0; i < opt_workers; i++) {
			if((err = pthread_create(&tid, NULL, worker, NULL)) != 0) {
				fprintf(stderr, PREFIX "create worker thread failed (%d): %s\n",
					err, strerror(err));
				abort();
			}
			pthread_detach(tid);
		}
		/* wait for workers */
		pthread_barrier_wait(&barrier);
		running = 0;

		if(opt_np == 0) {
			minibar_close();
		}
	}

	if(opt_check == 0) {
		if(opt_one == 0 && opt_np == 0)
			print_digest(njobs, jobs);
	} else {
		/* TODO */
	}

	free(jobs);

	return 0;
}
