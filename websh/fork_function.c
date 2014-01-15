/**
* @file fork_function.c
* @brief generic for library for forking callbacks 
* @author Georg Hubinger 9947673 <georg.hubinger@tuwien.ac.at>
* @date 2013-11-17
*/
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include "fork_function.h"

/* fork callback (with param as arg) and, if in child process exit with the returned value or if in parent return pit of child*/
pid_t fork_function(fork_func_callback_t callback, fork_func_param_t param)
{
	pid_t child = -1;

	switch (child = fork()) {

		case 0:
			exit(callback(param));
		break;

		/* Error checking is left to the caller, we don't care if fork returned -1 here */
		default:
			return child;
		break;
	
	}

}

/* wrapper aroung waitpit, that returns the WEXITSTATUS */
int wait_for_child(pid_t child)
{

	int status;

	if (waitpid(child, &status, 0) == -1) {

		return -1;

	}

	return WEXITSTATUS(status);
}

/* for consistency, wrapper around pipe() */
int open_pipe(pipe_t p)
{
	return pipe(p);
}

/* close giben channel of a pipe. 0b01 = read, 0b10 = write, 0b11 = both */
void close_pipe(pipe_t p, pipe_channel_t c)
{
	if(c & channel_read) {
		(void) close(p[0]);
	}

	if(c & channel_write) {
		(void) close(p[1]);
	}
}

/* redirect channel of pipe to fd in f */
int redirect(pipe_t p, FILE *f, pipe_channel_t c)
{
	/* Extract fd */
	int fd = fileno(f);
	/* close current resourcse identified by fd */
	(void) close(fd);

	/* Redirect read end of pipe ... */
	if(c & channel_read) {
		/* ... to fd */
		if(dup2(p[0], fd) == -1) {
			fprintf(stderr, "Couldn't redirect read channel to fd %d\n", fd);
			return -1;
		}
	}

	/* redirect write end of pipe ... */
	if(c & channel_write) {
		/* ... to fd */
		if(dup2(p[1], fd) == -1) {
			fprintf(stderr, "Couldn't redirect fd %d to write channel\n", fd);
			return -1;
		}
	}

	return 0;

}
