#ifndef __LOADCHECK_H__
#define __LOADCHECK_H__

#include "hashsum.h"

int scan_checks(const TCHAR *filename);
int load_checks(const TCHAR *filename, job_t *jobs, int njobs, md_t *alg, int init_mutex, int *err);

#endif
