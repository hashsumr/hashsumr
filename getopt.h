#ifndef __GETOPT_H__
#define __GETOPT_H__

extern int optind;
extern char *optarg;

#define no_argument 0
#define required_argument 1
#define optional_argument 2

// Structure similar to getopt_long
struct option {
	const char *name;
	int has_arg;  // 0 = no arg, 1 = required, 2 = optional
	int *flag;    // not used in this minimal version
	int val;
};

int getopt_long(int argc, char **argv, const char *optstring,
	const struct option *longopts, int *longindex);

#endif /* __GETOPT_LONG_H__ */
