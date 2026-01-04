#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include "hashsum.h"
#include "loadcheck.h"

int
is_hex_string(const char *s) {
	const char *ptr;
	for(ptr = s; *ptr; ptr++) {
		if(isxdigit(*ptr) == 0) return 0;
	}
	return 1;
}

int	/* return 0 if not unescaped, otherwise > 0 (# of unescaped chars) */
unescape(char *input) {
	int unescaped = 0;
	char *iptr = input, *optr = input;
	//if(opt_zero) return 0;
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

int
process_line(char *line, job_t *job, md_t *alg, int init_mutex) {
	int escaped = 0;
	char *ptr, *name, *hash = NULL;
	/* escaped? */
	if(line[0] == '\\') {
		line++;
		escaped = 1;
	}
	/* is bsd-style? - alg (filename) = hash */
	do {
		if((ptr = strrchr(line, ')')) != NULL) {
			if(strncmp(ptr, ") = ", 4) != 0) break;
			if(is_hex_string(ptr+4) == 0) break;
			hash = ptr+4;
		}
	} while(0);

	if(hash) {
		/* bsd-style - alg (filename) = hash */
		name = strstr(line, " (");
		if(name == NULL) return -1;
		*ptr = '\0';
		*name = '\0';
		name += 2;
		if((job->md = lookup_hash(line)) == NULL)
			return -1;
	} else {
		/* non-bsd-style - hash; ' '; ' ' or '*'; filename */
		if((ptr = strchr(line, ' ')) == NULL)  return -1;
		if(*(ptr+1) != ' ' && *(ptr+1) != '*') return -1;
		*ptr = '\0';
		if(is_hex_string(line) == 0) return -1;
		hash = line;
		name = ptr+2;
		job->md = alg;
	}
	/* fill the rest of job fields */
	if(init_mutex)
		pthread_mutex_init(&job->mutex, NULL);
	job->filename = strdup(name);
	if(escaped)
		unescape(job->filename);
	strncpy(job->dcheck, hash, EVP_MAX_DIGEST_SIZE);
	return 0;
}

int
scan_checks(const char *filename) {
	int fd;
	int count_nl = 0;
	int count_null = 0;
	size_t sz;
	char buf[32768];
	if((fd = open(filename, O_RDONLY)) < 0) {
		return -1;
	}
	while((sz = read(fd, buf, sizeof(buf))) > 0) {
		size_t i = 0;
		while (i < sz) {
			char *p_nl = memchr(buf + i, '\n', sz - i);
			char *p_null = memchr(buf + i, '\0', sz - i);

			if (!p_nl && !p_null) break;  // no more in this buffer

			if (p_nl && (!p_null || p_nl < p_null)) {
				count_nl++;
				i = (p_nl - buf) + 1;
			} else {
				count_null++;
				i = (p_null - buf) + 1;
			}
		}
	}
	close(fd);
	return count_nl + count_null + 1;
}

int
load_checks(const char *filename, job_t *jobs, int njobs, md_t *alg, int init_mutex, int *err) {
	int count = 0, error = 0;
	size_t sz, leftover = 0;
	FILE *fp;
	char buf[65537];
	if((fp = fopen(filename, "rb")) == NULL)
		return -1;
	while((sz = fread(buf+leftover, 1, sizeof(buf)-leftover-1, fp)) > 0) {
		size_t total = leftover + sz;
		size_t start = 0;
		for (size_t i = 0; i < total; i++) {
			if(buf[i] == '\0' || buf[i] == '\n') {
				buf[i] = '\0';
				if(process_line(buf+start, &jobs[count], alg, init_mutex) == 0)
					count++;
				else
					error++;
				start = i + 1;
			}
		}
		/* copy leftover bytes to start of buffer for next iteration */
		leftover = total - start;
		if (leftover > 0)
			memmove(buf, buf + start, leftover);
	}
	if(leftover > 0) {
		buf[leftover] = '\0';
		if(process_line(buf, &jobs[count], alg, init_mutex) == 0)
			count++;
		else
			error++;
	}
	fclose(fp);
	if(err) *err = error;
	return count;
}

