/**
 * @file mydiff.c
 * @author Georg Hubinger (9947673) <georg.hubinger@tuwien.ac.at>
 * @brief Compare two text files
 * @details Reads two text files line by line and compares the lines char by char.
 * If one of the files reaches EOF, comparisson will stop. If one line is shorter than the other one, comparisson will stop.
 * Outputs the line number where mismatches occured followed by the number of mismatches (Zeile: LINENO Zeichen: COUNT)
 * @date 16.10.2013
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/** Maximum line length to compare  */
#define MAX_LEN	20

/** Bailout with formatted error message */
#define exit_error(fmt, ...) \
	do {\
		(void) fprintf(stderr, fmt, __VA_ARGS__);\
		exit(EXIT_FAILURE); \
	} while(0);

/** True global for storing the programm's path */
char *pname = NULL;

/** Gives the argv index of the first positional argument. Visibility: global. Defined in stdio.h */
extern int optind;

/** FILE pointer for first files to compare */
FILE *f1 = NULL;
/** FILE pointer for second first files to compare */
FILE *f2 = NULL; 

/** Print out usage message */
static void usage(void);

/** Parse command line options
* @param argc The argument counter.
* @param argv The argument vector.
* @return non zero if wrong/unknown options or given files don't exists. zero otherwise
*/
static void parse_args(int argc, char **argv);

/** Compare the two given files. Outputs line number and number of mismatches to stdout */
static void compare(void);

/** close FILE pointers f1 and f2 */
static void cleanup(void);

/**
 * Programm's main entry point
 * @brief Processes arguments and compares the two provided files
 * @param argc The argument counter.
 * @param argv The argument vector.
 * @return EXIT_FAILURE if wrong arguments are provided or if the given files don't exist. EXIT_SUCCESS else.
 */
int main(int argc, char **argv)
{

	if(argc) pname = argv[0]; /* assume argv[0] holds programm name */
	
	parse_args(argc, argv); /* This bails out on wrong/unknown arguments or inexistant file. */

	compare(); /* Compare the two files. Prints out differences to stdout. */

	cleanup(); /* Close previously opened FILE pointers */

	/* quit */
	return 0;
	
}

/** Does given char signal EOL. NULL char means either end of last line in file or line size exceeded buffer size */
#define STOP_COMPARE_CHAR(c) ((c) == 0 || (c) == '\n') 
static void compare(void)
{

	char buf1[MAX_LEN], buf2[MAX_LEN]; /* char buffers of size MAX_LEN */
	int line = 1; /* currently compared line */

	/* if f1 reaches EOF, quit comparing */
	while(fgets(buf1, MAX_LEN, f1) != NULL) {

		int n = 0; /* init mismatching char counter */

		/* if f2 to reaches EOF, quit comparing */
		if(fgets(buf2, MAX_LEN, f2) == NULL) {
			break;
		}
		/* 
		* point c1 and c2 to the beginning of buf1 and buf2.
		* check if we need to continue comparison.
		* move to next chars.
		*/
		for(char *c1 = buf1, *c2 = buf2; !(STOP_COMPARE_CHAR(*c1) || STOP_COMPARE_CHAR(*c2)); c1++, c2++) {

			if(*c1 != *c2) {
				n++; /* increment mismatching char counter */
			}			

		}
	
		if(n) {
			fprintf(stdout, "Zeile: %d Zeichen: %d\n", line, n); /* if we had any mismatch, print out line number and number of mismatching chars */
		}

		line++; /* increment line number */
	
	}	

}

static void cleanup(void)
{

	if(f1) (void) fclose(f1);
	if(f2) (void) fclose(f2);

	f1 = f2 = NULL;

}

static void parse_args(int argc, char **argv)
{

	char *fmt = "%s: Datei '%s' konnte nicht geöffnet werden%s!\n", *err = "";

	/* we don't want any POSIX style options and we expect exactly 2 positional args */
	if(getopt(argc, argv, "") != -1 || (argc - optind) != 2) {
		usage();
	}

	/* test if first file exists */
	if((f1 = fopen(argv[optind], "r")) == NULL) {

		if (errno != 0) {
			fmt = "%s: Datei '%s' konnte nicht geöffnet werden (%s)!\n";
			err = strerror(errno);
		} 

		exit_error(fmt, pname, argv[optind], err); /* bailout */
	}

	/* test if second file exists */
	if((f2 = fopen(argv[optind + 1], "r")) == NULL) {

		cleanup();

		if (errno != 0) {
			fmt = "%s: Datei '%s' konnte nicht geöffnet werden (%s)!\n";
			err = strerror(errno);
		} 

		exit_error(fmt, pname, argv[optind + 1], err); /* bailout */
	}
}

static void usage(void) 
{
	exit_error("Usage: %s FILE1 FILE2\n", pname);
}
