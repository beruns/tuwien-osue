/**
* @file chstat.c
* @brief This file creates an executable for reading from stdin (compile vi -D_BUID_READIN) and one for creating a character statistic
* @author Georg Hubinger <georg.hubinger@tuwien.ac.at> 9947673
* @version 
* @date 2013-12-30
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/shm.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <msem182.h>
#include <sem182.h>

/* === Constants === */

/** Maximum Line Length to be read / processed */
#define MAX_BUF_SIZE		(256)

/** First Uppercase Ascii Letter Offset */
#define ASCII_CHAR_OFFSET	(65)

/** Number of seperately counted Ascii letters (A-Z) */
#define ASCII_CHAR_MAX		(25)

#define READER_F		(1 << 0)
#define EOF_F			(1 << 1)

/* === Types === */

/** Structure for shared data */
typedef struct _ipc_data_t {

	char data[MAX_BUF_SIZE]; /**< ipc communicating channel */
	int stat[ASCII_CHAR_MAX + 2]; /**< array that holds the statitistics */
	int total; /**< total chars processed */
	uint8_t flag; /**< signals EOF and LISTENER_READY */

} ipc_data_t;

/* === Global Vars === */

/** Semaphore (two fields) to conditionally sync read/write access to shmem */
int cnd = -1;

/** Semaphore (single field) to mutually exclusive protect access to shmem */
int mtx = -1;

/** SHM Segment ID */
int shm = -1;

/** Shared data to mount SHM Segment */
ipc_data_t *shared = (void *) -1;

/** cleanup bit for syncing signal / main process cleanup */
volatile sig_atomic_t cleanup = 0;

#ifdef _BUILD_READIN

/** Program name (as printed via usage) */
char *pname = "readin";

#else

/** Program name (as printed via usage) */
char *pname = "chstat";

/** Flag to set -v cmd line option */
int opt_v = 0;

#endif

/* === MACROS === */

/** Formatted output of error message (perror_f(fmt, ...). Prints out Formatted String followed by error string based upon errno  */
#define perror_f(...) { \
	(void) fprintf(stderr, "%s: ", pname); \
	(void) fprintf(stderr, __VA_ARGS__); \
	if(errno != 0) { \
		(void) fprintf(stderr, ": %s", strerror(errno)); \
	} \
	errno = 0; \
	(void) fprintf(stderr, "\n"); \
} 

/** Shutdown Process after printing formatted message (bail_out(EXIT_CODE, fmt, ...)  */
#define bail_out(eval, ...) { \
	perror_f(__VA_ARGS__); \
	ipc_shutdown(); \
	exit(eval); \
}

/* === Implimentation === */

/**
* Print Usage message
*/
static void usage(void) {
	fprintf(stderr, "Usage: %s", pname);
#ifndef _BUILD_READIN
	fprintf(stderr, " [-v]");
#endif
	fprintf(stderr, "\n");

	exit(EXIT_FAILURE);
}

/**
* Parse / check command line args / options
*
* @param argc number of command line arguments / options (as provided to main())
* @param argv command line args / options array
*/
static void parse_args(int argc, char **argv) 
{

	/* Set pname here */
	if(argc > 0) {
		pname = argv[0];
	}

#ifdef _BUILD_READIN

	/* readin doesn't take any args / options */
	if(getopt(argc, argv, "") != -1 || optind != argc) {
		usage();
	}
	return;

#else
	{ 
		int c;
		/* chstat has optional -v option */
		while((c = getopt(argc, argv, "v")) != -1) {
			switch(c) {
				case 'v':
					if(opt_v == 1) {
						(void) fprintf(stderr, "Option -v shall only be provided once\n");
						usage();
					}
					opt_v = 1;			
				break;
				default:
					usage();
				break;
			}
		}

		/* No other args / options are allowed */
		if(optind != argc) {
			usage();
		}
	
	}
	
#endif

}

/**
* Detach SHM Segment from 'shared' variable 
*/
static void shm_detach(void) 
{

	if(shared != (void *) -1) {
		(void) shmdt(shared);
	}

}

/**
* Cleanup all ipc resources
*/
static void ipc_shutdown(void)
{

	/* We need to block all signals to avoid race condition */
	sigset_t blocked_signals;
	(void) sigfillset(&blocked_signals);
	(void) sigprocmask(SIG_BLOCK, &blocked_signals, NULL);
	
	/* cleanup has already been done */
	if(cleanup == 1) {
		return;
	}
	cleanup = 1;

	/* If cnd has already been aquired, try to remove it (would return -1 if cnd had already been removed by other process) */
	if(cnd != -1) {
		(void) msemrm(cnd);
	}

	/* detach SHM Segment (So it can be free'd later, or by other process) */
	shm_detach();

	if(mtx != -1) {

		/* This fails with -1 if onother process has already done the cleanup of smh segment */
		if(semdown(mtx) != -1) {

			/*
			 * It might happen, that we only fail to aquire shm segment, but have a working mtx.
			 * In this case we will not destroy the mtx, because other processes might have created an shm segment and will therefor need a working mtx to free it.
			 */
			if(shm != -1) {

				(void) shmctl(shm, IPC_RMID, NULL);
				(void) semrm(mtx);

			} else {
				(void) semup(mtx);
			}

		}

	}

}

/** Initialize all variables needed for ipc */
static void ipc_init(void)
{

	/* All communicating processes need to be started from within the ssame working dir in order to make key have the same value */
	key_t key = ftok(".", 'x');

	if(key == -1) {
		bail_out(EXIT_FAILURE, "Couldn't create ipc key via ftok");
	}

	/* cnd is a two field semaphore (0 = write, 1 = read)*/
	if((cnd = mseminit(key, (0600), 2, 1, 0)) == -1) {
		if((cnd = msemgrab(key, 2)) == -1) {
			bail_out(EXIT_FAILURE, "Error aquiring conditional semephores");
		}
	}

	/* mtx is a mutex semaphore to protect access to shm */
	if((mtx = seminit(key + 1, (0600), 0)) == -1) {
		if((mtx = semgrab(key + 1)) == -1) {
			bail_out(EXIT_FAILURE, "Error aquiring mutual exclusive semephore");
		}
	}
	

	/* Create or fetch shm segment */
	if((shm = shmget(key, sizeof(ipc_data_t), IPC_CREAT | IPC_EXCL | (0600))) == -1) {

		if((shm = shmget(key, sizeof(ipc_data_t), 0)) == -1) {
			bail_out(EXIT_FAILURE, "Error aquiring shared mem");
		}

		/* The process that creates the shm segment will up the mtx, after it's initialized */
		if(semdown(mtx) == -1) {
			bail_out(EXIT_FAILURE, "Error downing mutex");

		}

		/* Mount shm */
		if((shared = shmat(shm, NULL, 0)) == (void *) -1) {
			bail_out(EXIT_FAILURE, "Error attaching shm segment");
		}

#ifndef _BUILD_READIN
		/* Signal listening reader */
		shared->flag |= READER_F;	
#endif

		/* Let other processes proceed */
		if(semup(mtx) == -1) {
			bail_out(EXIT_FAILURE, "Error upping mutex");
		}

	} else {

		/* Mount shm */
		if((shared = shmat(shm, NULL, 0)) == (void *) -1) {
			bail_out(EXIT_FAILURE, "Error attaching shm segment (creator)");
		}

		/* If we created the segment, we'll initialize it */
		(void) memset(shared, 0, sizeof(ipc_data_t));

#ifndef _BUILD_READIN
		/* Signal listening reader */
		shared->flag |= READER_F;	
#endif

		/* And signal other processes to proceed */
		if(semup(mtx) == -1) {
			bail_out(EXIT_FAILURE, "Error opening mutex");
		}
		
	} 

}

/**
* Signal handler for SIGINT / SIGQUIT
*
* @param sig signal number
*/
static void signal_handler(int sig)
{

	/* if a process receives a shutdown signal, it will clear all ipc resourcses (in case of normal shutdown only chstat would do this) */
	ipc_shutdown();
	exit(EXIT_SUCCESS);

}

#ifdef _BUILD_READIN

/**
* @brief Check if any chstat process is listening. Bailout if not.
*/
static void ipc_require_listener(void) 
{

	/* While we are in the mutual exclusive section, we don't listen to signals, because it would be hard to reset the state of the mutex */
	sigset_t blocked_signals, curr_set;
	(void) sigfillset(&blocked_signals);
	(void) sigprocmask(SIG_BLOCK, &blocked_signals, &curr_set);

	/* Mutual exclusive section so no other process removes shm segment meanwhile */
	if(semdown(mtx) == -1) {
		bail_out(EXIT_FAILURE, "Error downing mutex");
	}
	
	/* Do we have any listeners? */
	if((shared->flag & READER_F) != 1) {
		(void) semup(mtx); // Because ipc_shutdown() tries to down the mutex
		bail_out(EXIT_SUCCESS, "No chstat process listening.");
	}

	/* Release Mutex */
	if(semup(mtx) == -1) {
		bail_out(EXIT_FAILURE, "Error downing mutex");
	}
	
	/* Reset to original procmask*/
	(void) sigprocmask(SIG_SETMASK, &curr_set,  NULL);

}

/**
* Write buffer to shared memory
*
* @param data buffer to write to shared memory
*
* @return -1 on error, 0 otherwise
*/
static int ipc_write(char *data)
{

	int ret = -1;

	/* It makes no sense to proceed if nobody is listening */
	ipc_require_listener();

	/*
	 * Firts wait for write queue to let us in 
	 * if cnd or mt are no valid semaphores anymore, another process has already removed them. so we will simply return -1 
	 */
	if(msemdown(cnd, 1, 0) != -1) {

		/* While we are in the mutual exclusive section, we don't listen to signals, because it would be hard to reset the state of the mutex */
		sigset_t blocked_signals, curr_set;
		(void) sigfillset(&blocked_signals);
		(void) sigprocmask(SIG_BLOCK, &blocked_signals, &curr_set);
		
		/* Mutual exclusive section so no other process removes shm segment meanwhile */
		if(semdown(mtx) != -1) {

			if(data == NULL) {

				/* Signal EOF to Listener */
				shared->flag |= EOF_F;

			} else {

				for(int i = 0; i <= MAX_BUF_SIZE; ++i) {

					shared->data[i] = data[i];
					/* Null char */
					if(data[i] == 0) {
						break;
					}
	
				}

			}
	
			ret = (semup(mtx) == -1 ? -1 : 0);
		}

		/* Reset to original procmask*/
		(void) sigprocmask(SIG_SETMASK, &curr_set,  NULL);

		/* Signal read queue to proceed */	
		ret = (msemup(cnd, 1, 1) == -1 ? -1 : 0);

	}
	
	return ret;
}

#else

/**
* Read shared data and calculate statistics
*
* @return -1 on error, 0 on EOF, number of read chars otherwise
*/
static int ipc_process(void) 
{

	int i = -1;

	/*
	 * First, wait for read queue to let us in 
	 * if cnd or mt are no valid semaphores anymore, another process has already removed them. so we will simply return -1 
	 */
	if(msemdown(cnd, 1, 1) != -1) {

		/* While we are in the mutual exclusive section, we don't listen to signals, because it would be hard to reset the state of the mutex */
		sigset_t blocked_signals, curr_set;
		(void) sigfillset(&blocked_signals);
		(void) sigprocmask(SIG_BLOCK, &blocked_signals, &curr_set);
		
		/* Mutual exclusive section so no other process removes shm segment meanwhile */
		if(semdown(mtx) != -1) {

			i = 0;

			/* EOF Flag has been set, return 0 */
			if((shared->flag & EOF_F) != 1) {

				for(i = 0; i <= MAX_BUF_SIZE; ++i) {
	
					int c;
		
					/* Null character */
					if(shared->data[i] == 0) {
						break;
					} 
			
					/* We will only distinguish between uppercase'd chars */	
					c = toupper(shared->data[i]) - ASCII_CHAR_OFFSET;
					if(c < 0 || c > ASCII_CHAR_MAX) { /* No Ascii Letter -> inc 'others' counter */
						shared->stat[ASCII_CHAR_MAX + 1]++;
					} else { /* inc letter counter */
						shared->stat[c]++;
					}
		
					/* Total number of characters */
					shared->total++;
					/* Clear out buffer */
					shared->data[i] = 0;
				
				}
	
			}
	
			if(semup(mtx) == -1) {
				i = -1;
			}

		}

		/* Reset to original procmask*/
		(void) sigprocmask(SIG_SETMASK, &curr_set,  NULL);

		/* Let write queue proceed */
		if(msemup(cnd, 1, 0) == -1) {
			i = -1;
		}

	}
	
	return i;

}

/** Print out character statistics */
static void print_stat(void) 
{

	/* While we are in the mutual exclusive section, we don't listen to signals, because it would be hard to reset the state of the mutex */
	sigset_t blocked_signals, curr_set;
	(void) sigfillset(&blocked_signals);
	(void) sigprocmask(SIG_BLOCK, &blocked_signals, &curr_set);
	
	/* Mutual exclusive section so no other process removes shm segment meanwhile */
	if(semdown(mtx) != -1) {

		/* Print per letter stat */
		for(int i = 0; i <= ASCII_CHAR_MAX; ++i) {

			(void) fprintf(stdout, "     %c: %d\t%d%%\n", i + ASCII_CHAR_OFFSET, shared->stat[i], (shared->total == 0 ? 0 : shared->stat[i] * 100 / shared->total));

		}

		/* Print 'others' stat */
		(void) fprintf(stdout, "%s: %d\t%d%%\n", "andere", shared->stat[ASCII_CHAR_MAX + 1], (shared->total == 0 ? 0: shared->stat[ASCII_CHAR_MAX + 1] * 100 / shared->total));
		
		/* Print total stat */
		(void) fprintf(stdout, "%s: %d\t%d%%\n", "gesamt", shared->total, 100);

		(void) semup(mtx);
	}

	/* Reset to original procmask*/
	(void) sigprocmask(SIG_SETMASK, &curr_set,  NULL);
}

#endif

/**
* Main entry point
*
* @param argc argument counter
* @param argv argument array
*
* @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise
*/
int main(int argc, char  **argv)
{

	struct sigaction s;
	/* We only catch INT and QUIT (allthough we could catch TERM as well) */
	const int signals[] = { SIGINT, SIGQUIT };

	parse_args(argc, argv);

	s.sa_handler = signal_handler;
	s.sa_flags = SA_RESTART;

	/* Block all signals here ... */
	if(sigfillset(&s.sa_mask) < 0) {
		bail_out(EXIT_FAILURE, "Error creating Signal Block Mask");
	}

	/* ... unblock signals and set handler  */
	for(int i = 0; i < 2; ++i) {

		if(sigaction(signals[i], &s, NULL) < 0) {
			bail_out(EXIT_FAILURE, "Failed to sigaction for signale %d", signals[i]);
		}

	}
	
	/* Init ipc variables */
	ipc_init();

#ifdef _BUILD_READIN

	{
		/* readin reads stdin line wise (max MAX_BUF_SIZE - 1 chars) */
		char buf[MAX_BUF_SIZE];
		while(fgets(buf, MAX_BUF_SIZE, stdin) != NULL) {

			/* ... and writes them to shm */
			if(ipc_write(buf) == -1) { /* Write has failed, propably ipc channels have been removed by other process */
				bail_out(EXIT_FAILURE, "Error writing to shared memory");
			}

		}

		if(ipc_write(NULL) == -1) { /* This signals waiting read processed an EOF */
			bail_out(EXIT_FAILURE, "Error writing to shared memory");
		}

		shm_detach(); /* Normal shutdown for readin */

	}
	

#else
	{

		int c = 0;
		
		/* chstat keeps on listening */
		while(1) {

			/* wait for read queue to open up, generate statistic */
			c = ipc_process();

			if(c == -1) { /* Error reading from shmem, probably ipc channels have been removed by other process */
				bail_out(EXIT_FAILURE, "Error reading from shared memory");
			} else if(c == 0) { /* EOF */
				print_stat();
				break;
			} else if(opt_v == 1) { /* If called with -v */
				print_stat();
			}

		}

		ipc_shutdown();	/* Normal shutdown for chstat */
	}

#endif	

	return 0;

}
