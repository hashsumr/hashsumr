#include <stdio.h>
#include <string.h>

#include "getopt.h"

int optind = 1;       // Index of next argv element
wchar_t *optarg = NULL;  // Option argument

// Example minimal parsing function
int
getopt_long(int argc, wchar_t **argv, const wchar_t *optstring,
	const struct option *longopts, int *longindex)
{
	if (optind >= argc) return -1;

	wchar_t *arg = argv[optind];

	// Long option
	if (wcsncmp(arg, L"--", 2) == 0) {
		arg += 2;
		for (int i = 0; longopts[i].name != NULL; i++) {
			size_t len = wcslen(longopts[i].name);
			if (wcsncmp(arg, longopts[i].name, len) == 0) {
				if (longindex) *longindex = i;
				optind++;
				// Handle required argument
				if (longopts[i].has_arg == 1) {
					if (optind < argc) {
						optarg = argv[optind++];
					} else {
						fwprintf(stderr, L"Option '--%s' requires an argument\n", longopts[i].name);
						return L'?';
					}
				} else {
					optarg = NULL;
				}
				return longopts[i].val;
			}
		}
		fwprintf(stderr, L"Unknown option: %s\n", arg);
		optind++;
		return '?';
	}

	// Short option
	if (arg[0] == L'-' && arg[1] != L'\0') {
		wchar_t opt = arg[1];
		const wchar_t *p = wcschr(optstring, opt);
		if (!p) {
			fwprintf(stderr, L"Unknown option: -%c\n", opt);
			optind++;
			return '?';
		}
		if (*(p + 1) == L':') { // option requires argument
			if (arg[2] != L'\0') { // e.g., -fvalue
				optarg = &arg[2];
			} else if (optind + 1 < argc) { // next argv
				optarg = argv[++optind];
			} else {
				fwprintf(stderr, L"Option -%c requires an argument\n", opt);
				optind++;
				return L'?';
			}
		} else {
			optarg = NULL;
		}
		optind++;
		return opt;
	}

	// Not an option
	return -1;
}

