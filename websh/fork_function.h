/**
* @file fork_function.h
* @brief header file for fork_function library (generic fork for callback2)
* @author Georg Hubinger 9947673 <georg.hubinger@tuwien.ac.at>
* @date 2013-11-17
*/


#ifndef FORK_FUNC_H
#define FORK_FUNC_H
#include <sys/types.h> //needed for pid_t

/**
* @brief enumeration of channels in a pipe 01 = read, 10 = write, 11 = all;
*/
typedef enum pipe_channel {
	channel_read = 1,
	channel_write,
	channel_all
} pipe_channel_t;

/**
* @brief typedef pipe_t as int[2]
*/
typedef int pipe_t[2];

/**
* @brief void * Pointer that holds the generic param for the fork callback
*/
typedef void * fork_func_param_t;

/**
* @brief type defination of a forkable callback.
*
* @param param a generic void * Pointer that holds params for callback
*
* @return exit value the forked process shall exit() with
*/
typedef unsigned int (*fork_func_callback_t)(fork_func_param_t param);

/**
* @brief fork a process that only executes the provided callback (with the given param as arg). The process will exit with the value, the callback returns (if any)
*
* @param fork_func callback to be forked
* @param param parameter bag for callback
*
* @return pid_t of forked process that executes the callback
*/
pid_t fork_function(fork_func_callback_t fork_func, fork_func_param_t param);

/**
* @brief wrapper around waitpid
*
* @param child pid of child process
*
* @return exit code of child, or -1 if waitpid didn't work
*/
int wait_for_child(pid_t child);

/**
* @brief wrapper around pipe()
*
* @param pipe pipe_t that shall hold the pipe
*
* @return 0 on success, -1 else
*/
int open_pipe(pipe_t pipe);

/**
* @brief Close given channel of a pipe
*
* @param p pipe to work on
* @param c channel to be close (channel_read, channel_write or channel_all);
*/
void close_pipe(pipe_t p, pipe_channel_t c);

/**
* @brief redirect stream to pipe (or vice verse)
*
* @param p pipe to redirect to or from
* @param fd strem to redirect to or from
* @param c channel to be redirected 
*
* @return 0 on success, -1 else
*/
int redirect(pipe_t p, FILE *fd, pipe_channel_t c);

#endif
