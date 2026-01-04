#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <pthread.h>
#include "pthread-extra.h"
#include "hashsum.h"
#include "loadcheck.h"
#include "minibar/minibar.h"

#define PREFIX	"hashsum: "
#define VERSION	"0.0.1"

/* options */
static md_t *opt_alg = NULL;
static int opt_one = 0;
static int opt_bin = 1;
static int opt_check = 0;
static int opt_tag = 1;
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

/* hash & check statistics */
static int hash_done = 0;
static int hash_err = 0;
static int hash_missing = 0;
static int check_ok = 0;
static int check_failed = 0;
static int check_linerror = 0;

int
usage() {
	int algstrlen = 0;
	char algstr[1024] = "", *wptr = algstr;
	for(md_t *a = get_hashes(); a->name != NULL; a++) {
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
"      --gnu             create a GNU-style checksum\n"
"      --tag             create a BSD-style checksum (default)\n"
"  -t, --text            (*) read in text mode\n"
"  -z, --zero            end each output line with NUL, not newline,\n"
"                          and disable file name escaping\n"
"      --workers         set the number or parallel workers\n"
"      --np              no progress bar\n"
"\n"
"The following five options are useful only when verifying checksums:\n"
"      --ignore-missing  don't fail or report status for missing files\n"
"  -q, --quiet           don't print OK for each successfully verified file\n"
"      --status          don't output anything, status code shows success\n"
"      --strict          exit non-zero for improperly formatted checksum lines\n"
"  -w, --warn            warn about improperly formatted checksum lines\n"
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
		{ "gnu",             no_argument, NULL,  0  },
		{ "tag",             no_argument, NULL,  0  },
		{ "text",            no_argument, NULL, 't' },
		{ "zero",            no_argument, NULL, 'z' },
		{ "workers",   required_argument, NULL,  0  },
		{ "np",              no_argument, NULL,  0  },
		{ "ignore-missing",  no_argument, NULL,  0  },
		{ "quiet",           no_argument, NULL, 'q' },
		{ "status",          no_argument, NULL,  0  },
		{ "strict",          no_argument, NULL,  0  },
		{ "warn",            no_argument, NULL, 'w' },
		{ "help",            no_argument, NULL, 'h' },
		{ "version",         no_argument, NULL, 'v' },
		{ 0, 0 }
	};
	if(argc < 2) {
		return usage();
	}
	while((ch = getopt_long(argc, argv, "1a:bctzqwhv", opts, &optidx)) != -1) {
		switch(ch) {
		case 0: /* for longopts */
			if(strcmp(opts[optidx].name, "tag") == 0) {
				opt_tag = 1;
			} else if(strcmp(opts[optidx].name, "gnu") == 0) {
				opt_tag = 0;
			} else if(strcmp(opts[optidx].name, "np") == 0) {
				opt_np = 1;
			} else if(strcmp(opts[optidx].name, "workers") == 0) {
				opt_workers = strtol(optarg, NULL, 0);
				if(opt_workers < 0) opt_workers = 0;
			} else if(strcmp(opts[optidx].name, "ignore-missing") == 0) {
				opt_ignore_missing = 1;
			} else if(strcmp(opts[optidx].name, "status") == 0) {
				opt_status = 1;
				opt_np = 1;
			} else if(strcmp(opts[optidx].name, "strict") == 0) {
				opt_strict = 1;
			}
			break;
		case '1':
			opt_one = 1;
			break;
		case 'a':
			for(a = get_hashes(); a->name != NULL; a++) {
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
			opt_np = 1;
			break;
		case 'w':
			opt_warn = 1;
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

void
print_check1(job_t *job) {
	if(job->code == STATE_DONE) {
		int ok = (strcasecmp(job->dcheck, job->digest) == 0);
		if(ok) {
			check_ok++;
		} else {
			check_failed++;
		}
		if(opt_status || (ok && opt_quiet)) return;
		fprintf(stderr, "%s: %s\n",
			job->filename, ok ? "OK" : "FAILED");
		return;
	}
	if(opt_status)
		return;
	if(job->code == ERR_MISSING && opt_ignore_missing)
		return;
	fprintf(stderr, PREFIX "%s: %s\n",
		job->filename,
		job->errmsg);
}

int
return_value() {
	if(opt_check == 0) {
		return (hash_err + hash_missing > 0) ? 1 : 0;
	}
	if(opt_status == 0 && opt_warn && check_linerror > 0) {
		fprintf(stderr, PREFIX "WARNING: %d line is improperly formatted\n", check_linerror);
	}
	if(opt_status == 0 && check_failed > 0) {
		fprintf(stderr, PREFIX "WARNING: %d computed checksum did NOT match\n", check_failed);
	}
	if(opt_strict && check_linerror > 0)
		return 1;
	if(opt_ignore_missing)
		return (check_failed > 0) ? 1 : 0;
	return (check_failed + hash_missing> 0) ? 1 : 0;
}

void
print_check(int njobs, job_t *jobs) {
	for(int i = 0; i < njobs; i++) {
		print_check1(&jobs[i]);
	}
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
		/* update statistics */
		if(job->code == STATE_DONE) {
			hash_done++;
		} else if(job->code == ERR_MISSING) {
			hash_missing++;
		} else {
			hash_err++;
		}
		/* output */
		if(opt_np == 0) {
			minibar_complete(bar);
		} else if(opt_check == 0) {
			print_digest1(job);
		} else {
			print_check1(job);
		}
	}
quit:
	pthread_barrier_wait(&barrier);
	return NULL;
}

job_t *
jobs_alloc(int n) {
	job_t *mem;
	if((mem = (job_t *) malloc(sizeof(job_t) * n)) == NULL) {
		fprintf(stderr, PREFIX "malloc failed (%d): %s\n",
				errno, strerror(errno));
		return NULL;
	}
	bzero(mem, sizeof(job_t) * n);
	return mem;
}

int
main(int argc, char *argv[]) {
	int i, idx, err;
	int ncores = get_ncores();
	pthread_t tid;

	if((opt_alg = lookup_hash("SHA256")) == NULL) {
		fprintf(stderr, PREFIX "FATAL: cannot find the default algorithm.\n");
		return -1;
	}

	if((idx = parse_opts(argc, argv)) < 0) return -1;

	if(opt_check == 0) {
		njobs = argc - idx;
		if((jobs = jobs_alloc(njobs)) == NULL) exit(-1);
		for(i = 0; i < njobs; i++) {
			if(opt_one == 0)
				pthread_mutex_init(&jobs[i].mutex, NULL);
			jobs[i].md = opt_alg;
			jobs[i].filename = strdup(argv[idx+i]);
		}
	} else {
		/* TODO */
		int files = argc - idx;
		int n, estjobs = 0;
		for(i = 0; i < files; i++) {
			int n = scan_checks(argv[idx+i]);
			if(n < 0) {
				fprintf(stderr, PREFIX "%s: open for scanning failed (%d): %s\n",
					argv[idx+i], errno, strerror(errno));
				continue;
			}
			estjobs += n;
		}
		if((jobs = jobs_alloc(estjobs)) == NULL) exit(-1);
		for(i = 0; i < files; i++) {
			int e = 0;
			n = load_checks(argv[idx+i], &jobs[njobs], estjobs-njobs, opt_alg, opt_one == 0, &e);
			if(n < 0) continue;
			njobs += n;
			check_linerror += e;
		}
	}

	if(opt_workers <= 0) opt_workers = 1 + (ncores>>1);
	if(opt_workers > njobs) opt_workers = njobs;
	fprintf(stderr, PREFIX "%d processor(s) detected; workers = %d."
		" algorithm = %s"
		"\n", ncores, opt_workers, opt_alg->name);

	if(opt_one) {
		for(i = 0; i < njobs; i++) {
			hash1(&jobs[i], NULL, NULL);
			if(opt_check) {
				print_check1(&jobs[i]);
			} else {
				print_digest1(&jobs[i]);
			}
		}
	} else if(njobs > 0) {
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
		if(opt_one == 0 && opt_np == 0)
			print_check(njobs, jobs);
	}

	free(jobs);

	return return_value();
}
