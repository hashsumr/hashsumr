#ifndef __LOADCHECK_H__
#define __LOADCHECK_H__

#include "hashsum.h"

int scan_checks(const char *filename);
int load_checks(const char *filename, job_t *jobs, int njobs, md_t *alg, int init_mutex);

#endif
