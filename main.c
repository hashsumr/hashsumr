#include <stdio.h>
#include <string.h>
#include <locale.h>
#ifdef _WIN32
#include <windows.h>
#include "getopt.h"
#else
#include <getopt.h>
#endif
#include <errno.h>
#include "hashsumr.h"
#include "loadcheck.h"
#include "minibar/minibar.h"
#include "pthread_compat/pthread_compat.h"

#define PREFIX	"hashsumr: "
#define VERSION	"0.0.1"

#ifdef _WIN32
#define PATH_MAX	256
#endif

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

#ifdef _WIN32
char *
wchar2utf8(wchar_t *src, char *dst, int sz) {
	int result;
	if(src == NULL || dst== NULL || sz < 0)
		return NULL;
	result = WideCharToMultiByte(CP_UTF8,
		0, src, -1, dst, sz, NULL, NULL);
	if(result == 0) return NULL;
	return dst;
}

char *
wchar2utf8_alloc(wchar_t *src) {
	int size_needed, result;
	char *utf8;
	if(src == NULL) return NULL;
	size_needed = WideCharToMultiByte(CP_UTF8,
		0, src, -1, NULL, 0, NULL, NULL);
	if(size_needed <= 0) return NULL;
	if((utf8 = (char*) malloc(size_needed)) == NULL) return NULL;
	result = WideCharToMultiByte(CP_UTF8,
		0, src, -1, utf8, size_needed, NULL, NULL);
	if(result == 0) {
		free(utf8);
		return NULL;
	}
	return utf8;
}
#endif

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
	fprintf(stderr, "Usage: hashsumr [OPTION]... [FILE]...\n"
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
parse_opts(int argc, TCHAR *argv[]) {
	md_t *a;
	int ch, optidx = 0;
#ifdef _WIN32
	char buf[64];
#endif
	static struct option opts[] = {
		{ _T("one"),             no_argument, NULL, _T('1') },
		{ _T("algorithm"), required_argument, NULL, _T('a') },
		{ _T("binary"),          no_argument, NULL, _T('b') },
		{ _T("check"),           no_argument, NULL, _T('c') },
		{ _T("gnu"),             no_argument, NULL,     0   },
		{ _T("tag"),             no_argument, NULL,     0   },
		{ _T("text"),            no_argument, NULL, _T('t') },
		{ _T("zero"),            no_argument, NULL, _T('z') },
		{ _T("workers"),   required_argument, NULL,     0   },
		{ _T("np"),              no_argument, NULL,     0   },
		{ _T("ignore-missing"),  no_argument, NULL,     0   },
		{ _T("quiet"),           no_argument, NULL, _T('q') },
		{ _T("status"),          no_argument, NULL,     0   },
		{ _T("strict"),          no_argument, NULL,     0   },
		{ _T("warn"),            no_argument, NULL, _T('w') },
		{ _T("help"),            no_argument, NULL, _T('h') },
		{ _T("version"),         no_argument, NULL, _T('v') },
		{ 0, 0 }
	};
	if(argc < 2) {
		return usage();
	}
#ifdef _WIN32
#define strcmp	wcscmp
#define strtol	wcstol
#endif
	while((ch = getopt_long(argc, argv, _T("1a:bctzqwhv"), opts, &optidx)) != -1) {
		switch(ch) {
		case 0: /* for longopts */
			if(strcmp(opts[optidx].name, _T("tag")) == 0) {
				opt_tag = 1;
			} else if(strcmp(opts[optidx].name, _T("gnu")) == 0) {
				opt_tag = 0;
			} else if(strcmp(opts[optidx].name, _T("np")) == 0) {
				opt_np = 1;
			} else if(strcmp(opts[optidx].name, _T("workers")) == 0) {
				opt_workers = strtol(optarg, NULL, 0);
				if(opt_workers < 0) opt_workers = 0;
			} else if(strcmp(opts[optidx].name, _T("ignore-missing")) == 0) {
				opt_ignore_missing = 1;
			} else if(strcmp(opts[optidx].name, _T("status")) == 0) {
				opt_status = 1;
				opt_np = 1;
			} else if(strcmp(opts[optidx].name, _T("strict")) == 0) {
				opt_strict = 1;
			}
			break;
		case _T('1'):
			opt_one = 1;
			break;
		case _T('a'):
			for(a = get_hashes(); a->name != NULL; a++) {
#ifdef _WIN32
				if(_stricmp(a->name, wchar2utf8(optarg, buf, sizeof(buf))) == 0) {
#else
				if(strcasecmp(a->name, optarg) == 0) {
#endif
					opt_alg = a;
					break;
				}
			}
			if(a->name == NULL) {
#ifdef _WIN32
				fprintf(stderr, PREFIX);
				fwprintf(stderr, L"unsupported algorithm `%s'.\n", optarg);
#else
				fprintf(stderr, PREFIX "unsupported algorithm `%s'.\n", optarg);
#endif
				exit(-1);
			}
			break;
		case _T('b'):
			opt_bin = 1;
			break;
		case _T('c'):
			opt_check = 1;
			break;
		case _T('t'):
			/* XXX: not implemented - opt_bin = 0 */
			break;
		case _T('z'):
			opt_zero = 1;
			break;
		case _T('q'):
			opt_quiet = 1;
			opt_np = 1;
			break;
		case _T('w'):
			opt_warn = 1;
			break;
		case _T('v'):
			fprintf(stderr, PREFIX "version " VERSION "\n");
			break;
		case _T('h'):
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;
	return optind;
#ifdef _WIN32
#undef strcmp
#undef wcstol
#endif
}

int
get_ncores() {
	if(opt_one) {
		return 1;
	} else {
#ifdef _WIN32
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		return sysinfo.dwNumberOfProcessors;
#else
		long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
		return nprocs;
#endif
	}
}

int	/* return 0 if not escaped, otherwise > 0 (# of escaped chars) */
escape(char *input, char *output, int outlen) {
	int escaped = 0, wlen = 0;
	char *iptr = input, *optr = output;
	if(opt_zero) {
#ifdef _WIN32
		strncpy_s(output, outlen, input, strlen(input));
#else
		strncpy(output, input, outlen);
#endif
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
#ifdef _WIN32
		int ok = (_stricmp(job->dcheck, job->digest) == 0);
#else
		int ok = (strcasecmp(job->dcheck, job->digest) == 0);
#endif
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
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}
	return NULL;
}

void *
worker(void *__) {
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
	char msg[128];
	if((mem = (job_t *) malloc(sizeof(job_t) * n)) == NULL) {
		fprintf(stderr, PREFIX "malloc failed (%d): %s\n",
				errno,
				herrmsg(msg, sizeof(msg), errno));
		return NULL;
	}
	memset(mem, 0, sizeof(job_t) * n);
	return mem;
}

#ifdef _WIN32
wchar_t **
append_argv(wchar_t *value, wchar_t **argv, int *argc, int *sz) {
	if(*argc == *sz) {
		if((argv = (wchar_t**) realloc(argv, sizeof(wchar_t*) * ((*sz) + 16))) == NULL) {
			fprintf(stderr, PREFIX "argv/append: realloc failed\n");
			exit(-1);
		}
		*sz += 16;
	}
	argv[(*argc)++] = value;
	return argv;
}

void
strreplace(wchar_t *s, wchar_t from, wchar_t to) {
	wchar_t *wptr = s;
	while((wptr = wcschr(wptr, from)) != NULL) {
		*wptr++ = to;
	}
}

wchar_t **
expand_files(wchar_t *pattern, wchar_t **argv, int *argc, int *sz) {
	HANDLE h;
	WIN32_FIND_DATAW fd;
	wchar_t *value;
	wchar_t *dirp = NULL;
	wchar_t fullname[256];
	strreplace(pattern, L'/', L'\\');
	dirp = wcsrchr(pattern, L'\\');
	if((h = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) {
		/* no match */
		return 0;
	}
	do {
		if(wcscmp(fd.cFileName, L".") == 0
		|| wcscmp(fd.cFileName, L"..") == 0)
			continue;
		if(dirp == NULL) {
			wcscpy_s(fullname, sizeof(fullname)/sizeof(wchar_t), fd.cFileName);
		} else {
			_snwprintf_s(fullname, sizeof(fullname), _TRUNCATE,
				L"%*.*s%s",
				(int) (dirp-pattern+1),
				(int) (dirp-pattern+1),
				pattern, fd.cFileName);
		}
		if((value = _wcsdup(fullname)) == NULL) {
			fprintf(stderr, PREFIX "argv/expand: alloc filename failed\n");
			exit(-1);
		}
		argv = append_argv(value, argv, argc, sz);
	} while(FindNextFileW(h, &fd));
	FindClose(h);
	return argv;
}

wchar_t **
expand_args(int *argc, wchar_t *argv[]) {
	int i, n, newargc = 0, sz = 16;
	wchar_t **newargv = NULL;
	if(argc == NULL) return NULL;
	if((newargv = (wchar_t **) malloc(sizeof(wchar_t*) * sz)) == NULL)
		return NULL;
	newargc = 0;
	for(i = 0; i < *argc; i++) {
		if(wcschr(argv[i], L'*') == NULL
		&& wcschr(argv[i], L'?') == NULL) {
			newargv = append_argv(argv[i], newargv, &newargc, &sz);
			continue;
		}
		newargv = expand_files(argv[i], newargv, &newargc, &sz);
	}
	*argc = newargc;
	return newargv;
}
#endif

int
#ifdef _WIN32
wmain(int argc, wchar_t *argv[]) {
#else
main(int argc, char *argv[]) {
#endif
	int i, idx, err;
	int ncores = get_ncores();
	char msg[128];
	pthread_t tid;
#ifdef _WIN32
	setlocale(LC_ALL, ".UTF-8");
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif
	if((opt_alg = lookup_hash("SHA256")) == NULL) {
		fprintf(stderr, PREFIX "FATAL: cannot find the default algorithm.\n");
		return -1;
	}
#ifdef _WIN32
	if((argv = expand_args(&argc, argv)) == NULL)
		return -1;
#endif
	if((idx = parse_opts(argc, argv)) < 0) return -1;

	if(opt_check == 0) {
		njobs = argc - idx;
		if((jobs = jobs_alloc(njobs)) == NULL) exit(-1);
		for(i = 0; i < njobs; i++) {
			if(opt_one == 0)
				pthread_mutex_init(&jobs[i].mutex, NULL);
			jobs[i].md = opt_alg;
#ifdef _WIN32
			jobs[i].wfilename = _wcsdup(argv[idx+i]);
			jobs[i].filename = wchar2utf8_alloc(argv[idx+i]);
#else
			jobs[i].filename = strdup(argv[idx+i]);
#endif
		}
	} else {
		/* TODO */
		int files = argc - idx;
		int estjobs = 0;
		for(i = 0; i < files; i++) {
			int n = scan_checks(argv[idx+i]);
			if(n < 0) {
#ifdef _WIN32
				fprintf(stderr, PREFIX);
				fwprintf(stderr, L"%s: open for scanning failed (%d): ",
					argv[idx+1], errno);
				fprintf(stderr, "%s\n",
					herrmsg(msg, sizeof(msg), errno));
#else
				fprintf(stderr, PREFIX "%s: open for scanning failed (%d): %s\n",
					argv[idx+i], errno, herrmsg(msg, sizeof(msg), errno));
#endif
				continue;
			}
			estjobs += n;
		}
		if((jobs = jobs_alloc(estjobs)) == NULL) exit(-1);
		for(i = 0; i < files; i++) {
			int n, e = 0;
			n = load_checks(argv[idx+i], &jobs[njobs], estjobs-njobs, opt_alg, opt_one == 0, &e);
			if(n < 0) continue;
			njobs += n;
			check_linerror += e;
		}
	}

	if(opt_workers <= 0) opt_workers = 1 + (ncores>>1);
	if(opt_workers > njobs) opt_workers = njobs;
	fprintf(stderr, PREFIX "%d processor(s) detected; workers = %d;"
		" algorithm = %s"
		".\n", ncores, opt_workers, opt_alg->name);

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
					err, herrmsg(msg, sizeof(msg), err));
				abort();
			}
		}
		/* setup barrier */
		if((err = pthread_barrier_init(&barrier, NULL, opt_workers+1)) != 0) {
			fprintf(stderr, PREFIX "create thread failed (%d): %s\n",
				err, herrmsg(msg, sizeof(msg), err));
			abort();
		}
		/* run workers */
		for(i = 0; i < opt_workers; i++) {
			if((err = pthread_create(&tid, NULL, worker, NULL)) != 0) {
				fprintf(stderr, PREFIX "create worker thread failed (%d): %s\n",
					err, herrmsg(msg, sizeof(msg), err));
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
