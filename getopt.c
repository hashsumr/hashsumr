#include <stdio.h>
#include <string.h>

#include "getopt.h"

int optind = 1;       // Index of next argv element
char *optarg = NULL;  // Option argument

// Example minimal parsing function
int
getopt_long(int argc, char **argv, const char *optstring,
	const struct option *longopts, int *longindex)
{
	if (optind >= argc) return -1;

	char *arg = argv[optind];

	// Long option
	if (strncmp(arg, "--", 2) == 0) {
		arg += 2;
		for (int i = 0; longopts[i].name != NULL; i++) {
			size_t len = strlen(longopts[i].name);
			if (strncmp(arg, longopts[i].name, len) == 0) {
				if (longindex) *longindex = i;
				optind++;
				// Handle required argument
				if (longopts[i].has_arg == 1) {
					if (optind < argc) {
						optarg = argv[optind++];
					} else {
						fprintf(stderr, "Option '--%s' requires an argument\n", longopts[i].name);
						return '?';
					}
				} else {
					optarg = NULL;
				}
				return longopts[i].val;
			}
		}
		fprintf(stderr, "Unknown option: %s\n", arg);
		optind++;
		return '?';
	}

	// Short option
	if (arg[0] == '-' && arg[1] != '\0') {
		char opt = arg[1];
		const char *p = strchr(optstring, opt);
		if (!p) {
			fprintf(stderr, "Unknown option: -%c\n", opt);
			optind++;
			return '?';
		}
		if (*(p + 1) == ':') { // option requires argument
			if (arg[2] != '\0') { // e.g., -fvalue
				optarg = &arg[2];
			} else if (optind + 1 < argc) { // next argv
				optarg = argv[++optind];
			} else {
				fprintf(stderr, "Option -%c requires an argument\n", opt);
				optind++;
				return '?';
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

