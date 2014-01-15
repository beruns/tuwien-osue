/**
* @file websh.c
* @brief read commands from stdin, spawn two worker processes: one to execute the command, one to format it's output in html
* @author Georg Hubinger 9947673 <georg.hubinger@tuwien.ac.at>
* @date 2013-11-17
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fork_function.h"

/* === Constants === */

/**
* @brief Maximum Length of a command, or of a command's output line that's being formatted
*/
#define MAX_LINE_LENGTH 255

/* === Global Variables === */

/**
* @brief command line options 
*/
static struct {

	int opt_e; /**< true if called with -e  */
	int opt_h; /**< true if called with -h  */
	int opt_s; /**< true if called with -s  */
	char *s_word; /**< if called with -s search for output lines containing s_word ... */
	char *s_tag; /**< and wrap the line withing s_tag  */

} opts;

/**
* @brief Name of executeable
*/
char *pgname = "websh";

/* === Structures === */

/**
* @brief Params structure for execute and format callbacks 
*/
struct worker_params {

	pipe_t pipe; /**< pipe to handle communication between processes */
	char *cmd /**< command to execute */;

};

/* === Prototypes === */

/**
* @brief This is the callback, that handles execution of an issued command. stdout is redirected to pipe
*
* @param param worker_params type argument for the worker
*
* @return 1 if redirect or execlp failed, nothing otherwise, because execlp has taken over
*/
static unsigned int execute(fork_func_param_t param);

/**
* @brief Remove trailing newline char in string
*
* @param str string to work on
*/
static void trim(char *str);

/**
* @brief This is the callback, that handles the formatted output. stdin is redirected from pipe
*
* @param param  worker_params type argument for the worker
* @details uses opts global var
*
* @return 1 if redirect fails, 0 otherwise
*/
static unsigned int format(fork_func_param_t param);

/**
* @brief spawn execute and format worker for given command
*
* @param cmd command to be executed.
* @details uses pgname global var
*
* @return -1 if pipe can't be created or one of the processes could not be forked, 0 otherwise (even if one of the processes returns nonzero)
*/
static int spawn_worker(char *cmd);

/**
* @brief Parse command line arguments
*
* @param argc argument counter
* @param argv argument array
* @details uses opts global var
*
* @return 0 if all arguments have been provided correctly, -1 otherwise
*/
static int parse_args(int argc, char **argv);
 
/**
* @brief Print usage message
* @details uses pgname global var
*/
void usage(void);
 
static unsigned int execute(fork_func_param_t param) 
{
	/* Cast argument */
	struct worker_params *params = (struct worker_params *) param;

	/* We dont need the read end of the pipe */
	close_pipe(params->pipe, channel_read);
	
	/* Redirect stdout to write end */
	if(redirect(params->pipe, stdout, channel_write) == -1) {
		return 1;
	}
	
	/* We use sh here, to circumvent parsing the command string */
	(void) execlp("/bin/sh", "sh", "-c", params->cmd, (char *) NULL);

	// Should not be reached;
	return 1;
}

static void trim(char *str)
{
	while(str[strlen(str) - 1] == '\n') {
		str[strlen(str) - 1] = '\0' ;
	}
}

static unsigned int format(fork_func_param_t param) 
{
	
	/* Cast argument */
	struct worker_params *params = (struct worker_params *) param;
	/* To read the commands output in, line by line */
	char output[MAX_LINE_LENGTH];

	/* close write end of pipe */
	close_pipe(params->pipe, channel_write);
	/* redirect stdin from read end */
	if(redirect(params->pipe, stdin, channel_read) == -1) {
		return 1;
	}

	/* Print out issued command if -h*/
	if(opts.opt_h) {
		(void) fprintf(stdout, "<h1>%s</h1>\n", params->cmd);
	}

	/* Read cmd's output line by line */
	while(fgets(output, MAX_LINE_LENGTH, stdin) != NULL) {

		trim(output);

		/* put special lines in special tags */
		if(opts.opt_s && strstr(output, opts.s_word)) {
			(void) fprintf(stdout, "<%s>%s</%s><br />\n", opts.s_tag, output, opts.s_tag);
		} else { /* standard format */
			(void) fprintf(stdout, "%s<br />\n", output);
		}

	}

	return 0;
}

static int spawn_worker(char *cmd) 
{
	/* params struct for both workers */
	struct worker_params params;
	/* children's pids */
	pid_t c1, c2;
	int status, ret = 0;

	trim(cmd);
	params.cmd = cmd;

	if(open_pipe(params.pipe) == -1) {
		(void) fprintf(stderr, "%s: Could not create pipe\n", pgname);
		return -1;
	}

	/* We flush all our standard fd's so we'll have them empty in the workers */
	fflush(stdin);
	fflush(stdout);
	fflush(stderr);

	/* Fork execute worker */
	if((c1 = fork_function(execute, &params)) == -1) {
		(void) fprintf(stderr, "%s: Could not spawn execute worker\n", pgname);
		close_pipe(params.pipe, channel_all);
		return -1;
	}

	/* Fork format worker */
	if((c2 = fork_function(format, &params)) == -1) {
		(void) fprintf(stderr, "%s: Could not spawn format worker\n", pgname);
		/* Wait for child 1 */
		if(wait_for_child(c1) == -1) {
			(void) fprintf(stderr, "%s: Error waiting for execute worker to finish\n", pgname);
		}
		close_pipe(params.pipe, channel_all);
		return -1;
	}

	/* We need to close the pipe in parent, so that the format worker will quit working when execute's output has finished */
	close_pipe(params.pipe, channel_all);
	
	if((status = wait_for_child(c1)) != 0) {
		(void) fprintf(stderr, "%s: Execute worker returned %d\n", pgname, status);
		/* not neccessarily an error. If there was a typo in cmd don't quit the whole programm */
	//	ret = -1;
	}

	if((status = wait_for_child(c2)) != 0) {
		(void) fprintf(stderr, "%s: Format worker returned %d\n", pgname, status);
	//	ret = -1;
	}
	
	return ret;
}

static int parse_args(int argc, char **argv)
{
	char c, *s_arg = NULL;

	if(argc > 0) {
		pgname = argv[0];
	}

	while((c = getopt(argc, argv, "ehs:")) != -1) {
		switch(c) {
		
			case 'e':
				if(opts.opt_e == 1) {
					(void) fprintf(stderr, "option '-e' may only be given once\n");
					return -1;
				}
				opts.opt_e = 1;
			break;
			case 'h':
				if(opts.opt_h == 1) {
					(void) fprintf(stderr, "option '-h' may only be given once\n");
					return -1;
				}
				opts.opt_h = 1;
			break;
			case 's':	
				if(opts.opt_s == 1) {
					(void) fprintf(stderr, "option '-s' may only be given once\n");
					return -1;
				}
				
				opts.opt_s = 1;
				s_arg = opts.s_word = optarg;
			break;
			default:
				return -1;
			break;

		}
	}

	/* no positional args allowed */
	if(optind != argc) {
		return -1;
	}

	if(opts.opt_s) {

		int colon = 0;

		/* do we have an argument for -s? */
		if(opts.s_word == NULL) {
			return -1;
		}

		/* is the argument of the form WORD:TAG? */
		while(*s_arg != '\0') {

			if(*s_arg == ':') {
				colon = 1;
				*s_arg = '\0';
				opts.s_tag = s_arg + 1;
				break;
			}

			s_arg++;
		}
	
		if(!colon) {
			(void) fprintf(stderr, "Argument for -s has to be in the form 'WORD:TAG'\n");
			return -1;
		}

	}

	return 0;
}

void usage(void) 
{
	(void) fprintf(stderr, "Usage: %s [-e] [-h] [-s WORD:TAG]\n", pgname);
}

/**
* @brief Main entry point
*
* @param argc argument counter 
* @param argv argument array 
* @details uses opts global var
*
* @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise
*/
int main(int argc, char **argv)
{

	char cmd[MAX_LINE_LENGTH];
	
	/* chack opts */
	if(parse_args(argc, argv) == -1) {
		usage();
		return EXIT_FAILURE;
	}

	/* needs to be done here */
	if(opts.opt_e) {
		(void) fprintf(stdout, "<html><head></head><body>\n");
	}

	/* read commands */
	while(fgets(cmd, MAX_LINE_LENGTH, stdin) != NULL) {

		/* and spanw the workers */
		if(spawn_worker(cmd) == -1) {
			return EXIT_FAILURE;
		}

	}

	if(opts.opt_e) {
		(void) fprintf(stdout, "</body></head>\n");
	}

	return EXIT_SUCCESS;
}
