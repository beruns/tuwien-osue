/**
 * @file client.c
 * @author Georg Hubinger (9947673) <georg.hubinger@tuwien.ac.at>
 * @brief A Mastermind client that automatically solves an 8 color / 5 slots mastermind game
 * @details Communicates with a server via TCP/IP Socket. Briefly does the following
 * * Splits the available color into two partition and tests them (partly) against the server
 * * Tests all colors of the partition (taht need testing) against the server
 * * Optimises possible slots for each color in secret code
 * * Creates a list of all possible combinations and tests of them against the server
 * * Then tests the next combination that equals the first one in exactely the same number of slots, that the first one received as 'red' marks
 * * Then tests the next combination that equals the first and the second one n exactely the same number of slots, that each received as 'red' marks
 * * and so on
 * @date 1.11.2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>
#include <signal.h>

/* === Constants / Macros === */

#define SLOTS (5)
#define COLOR_WIDTH (3)
#define COLORS (1 << COLOR_WIDTH)

#define REQUEST_WIDTH (2)
#define RESPONSE_WIDTH (1)

#define ERROR_PARITY 1
#define ERROR_GAME_LOST 2
#define ERROR_MULTIPLE 3

/* Read Server Response */
#define RED(resp) ((resp) & 0x7)
#define WHITE(resp) (((resp) & (0x7 <<3)) >> 3)
#define TOTAL(resp) RED((resp)) + WHITE((resp));
#define RESPONSE_ERROR(resp) ((resp) >> 6)

/* Indicates an error on start_game()'s return value */
#define GAME_ERROR_BIT ((uint8_t) 1 << 7)
#define RETURN_ERROR(err) return (uint8_t) ((err) | GAME_ERROR_BIT)
#define IS_ERROR(err) (err & GAME_ERROR_BIT)

/* init all slots as possible (00011111) */
#define ALL_SLOTS  (uint8_t) ~(0x7 << (SLOTS))

/* Types  */

/* Color enum */
typedef enum { beige, darkblue, green, orange, red, black, violet, white} color_t;

/* the result structure of a guess */
typedef struct _result_t {

	uint8_t red;
	uint8_t white;
	uint8_t total;

	uint8_t real_hits;

} result_t;

/* A list of all possible combinations */
typedef struct _perm_list_t perm_list_t;
struct _perm_list_t {

	uint16_t combination;
	uint8_t red;

	perm_list_t *prev;
	perm_list_t *next;

};

/* 
* Actually some kind of tree. A node represent one combination of all needed occurances of one color.
* The sibling nodes represent all other possible combinations of that color
* The child nodes represet all possible combinations of the next computed color that are possible with the node's and all it's parent's combinations.
*/
typedef struct _combination_list_t combination_list_t;
struct _combination_list_t {

	color_t  color;
	color_t  next_color;
	
	uint8_t mask;
	uint8_t combination;

	combination_list_t *first_child;
	combination_list_t *next;

};

/* Main data structure. Holds all information about the game */
typedef struct _gameinfo_t {

	/* Server */
	int connfd;

	uint16_t guess;

	uint8_t request_buf[REQUEST_WIDTH];
	uint8_t response_buf[RESPONSE_WIDTH];
	uint8_t error;
	uint8_t round;

	color_t* partitions[2];
	result_t result[2];
	uint8_t colors_total;

	uint8_t *possible;

	/* combination tree */
	combination_list_t *comb;	

	/* all possible combinations */
	perm_list_t *first_perm;
	perm_list_t *last_perm;

	/* all combinations we have already tried */
	perm_list_t *processed;
	perm_list_t *processed_last;

} gameinfo_t;

/*=== Debugging ===*/

/* Some debugging helpers */
#ifdef ENDEBUG
#define DEBUG(...) do { (void) fprintf(stderr, __VA_ARGS__); } while(0)

/**
 * @brief Print out a number in binary representation
 * @param c the number to print
 * @param width the binary representation's width
 */
#define print_binary(c, w) _print_binary(c, w)
void _print_binary(int c, int width) {

	for(int j = 0; j < width; j++) {
		DEBUG("%d", ((c >> ((width - 1) - j)) &  0x1));
	}
}

/**
 * @brief Print out some info about the possiblity of all colors 
 * @param info The game info
 */
#define print_possible(i) _print_possible(i)
void _print_possible(gameinfo_t *info)
{
	for(int i = 0; i < COLORS; i++) {
		
		uint8_t c = info->possible[i];
		DEBUG("Color: %d %d,  total %d: ", i, c, (c & ((uint8_t) 0x7 << SLOTS)) >> SLOTS);
		print_binary(c, SLOTS);
		DEBUG("\n");
	}
}

/**
 * @brief Print out the combination tree in a (hardly ;) ) human readable structure 
 * @param curr first Tree Element
 * @param level indicates the indentation of the output
 */
#define print_combination(c, l) _print_combination(c, l)
void _print_combination(combination_list_t *curr, int level) {

	for(int i = 0; i<=level; ++i) {
		DEBUG("  ");
	}
	DEBUG("Color %d ", curr->color);
	print_binary(curr->combination, SLOTS);
	DEBUG("\n");

	if(curr->first_child) {
		print_combination(curr->first_child, level);
	}

	if(curr->next) {
		print_combination(curr->next, level +1);
	}

}

#else
#define DEBUG(...)
#define print_binary(c, w)
#define print_possible(i)
#define print_combination(c, l)
#endif

/* === Global Variables ===  */

/* Main game info */
static gameinfo_t *game = NULL;

/* Programm name*/
static char *progname = "client";

/* This variable is set to ensure cleanup is performed only once */
volatile sig_atomic_t terminating = 0;

/* === Prototypes === */

/**
 * @brief Signal handler
 * @param sig Signal number catched
 */
static void signal_handler(int sig);

/**
 * @brief free all allocated space. close socket
 */

static void free_resources(void);
/**
 * @brief Print out formatted message, free_resources, exit(eval)
 * @param eval exit value
 * @param fmt format string for vfprintf
 * @param ... va_list for vfprintf
 */
static void bail_out(int eval, const char *fmt, ...);

/**
 * @brief creates a socket and connects to server:port
 * @param host servername or address. will be figured out by getaddrinfo()
 * @param port port number will be converted to correct endianess by getaddrinfo()
 * @return Server socket fd
 */
static int connect_to_server(char *host, char *service); 

/** 
 * @brief reads n bytes from server response into buffer
 * @param fd Server socket fd
 * @param buffer buffer to read response into
 * @param n bytes to read totally
 * @return size read or -1 on error
 */
static size_t read_from_server(int fd, uint8_t *buffer, size_t n);

/** 
 * @brief write n bytes buffer to server
 * @param fd Server socket fd
 * @param buffer data to write
 * @param n bytes to write totally
 * @return size written or -1 on error
 */
static size_t write_to_server(int fd, uint8_t *buffer, size_t n);

/**
 * @brief compute parity bit of request. this is a xor over all slot bits
 * @param req pointer to reuqest to be sent
 */
static void compute_parity(uint16_t *req);

/**
 * @brief adds color to req at position (0 = leftmost slot, 5 = rightmost slot
 * @param req Pointer to request to be sent
 * @param color Number of color (see enum color_t)
 * @param position add color at this position
 * @return 0 on success, -1 if position is larger than slots available
 */
static int add_color(uint16_t *req, color_t color, int position);

/**
 * @brief calls add_color on all colors in colors[]
 * @param req Pointer to request to be sent
 * @param num size of colors[]
 * @param colors[] color_t array with colors to add
 * @return 0 on success, -1 if num >= SLOTS
 */
static int init_request(uint16_t *req, int num, color_t colors[]); 

/**
 * @brief compute_parity and swap bytes in request (to ensure reverse network byte order)
 * @param buffer request buffer (uint8_t[2])
 * @param req Pointer to 2bytes request
 */
static void generate_request(uint8_t *buffer, uint16_t *req);

/**
 * @brief inits request (if partition is provided), sends it to server and analyses the response
 * @param info Main game info
 * @param partion if provided fills info->guess with all colors in partition
 * @param result Pointer to result structure the result will be writte into
 * @return 0 on success, server response error else. can bail_out !!!
 */
static uint8_t commit_guess(gameinfo_t *info, color_t *partition, result_t *result);

/**
 * @brief counts the set bits in a number right to left till length
 * @param num number to check
 * @param length how many bits shall be checked
 * @returns number of set bits
 */
static inline uint8_t calculate_set_bits(uint8_t num, int length);

/**
 * @brief based upon the last combination computes the next one that matched the mask and has count bits set
 * @param last_combination last combination that was computed
 * @param mask combintaion needs to fit that mask
 * @param count how many set bits have to be in the combination
 * @return new combination
 */
static uint8_t compute_combination(uint8_t last_combination, uint8_t mask, uint8_t count); 

/**
 * @brief compare two combination and see haw many slots match
 * @param cmb1 reference
 * @param cmb2 testee
 * @return number of slots that matched
 */
static uint8_t compare_perm_equal_pos(uint16_t cmb1, uint16_t cmb2);

/**
 * @brief find next combination, that matches each already processed one in exactly n slots where n is the number of red marks, the already processed one had
 * @param info Main Game info
 * @return next matching combination
 */
static perm_list_t *find_best_perm(gameinfo_t *info);

/**
 * @brief goes through all needed colors, generates the different combination and builds a tree of possibilities
 * @param info Main Game info
 * @param parent used for recurive calling, can be a NULL Pointer-Pointer
 */
static void build_combination_list(gameinfo_t *info, combination_list_t **parent);
/**
 * @brief free all malloc'd leaves of list
 * @param list first leave of combination tree to free
 */
static void free_combination_list(combination_list_t *list);

/**
 * @brief walk the combination tree and generates a list of possible guesses
 * @param info Main Game info
 * @param curr used for recursion
 * @param counter used to assure that only comlplete combinations are suggested
 */
static void process_combinations(gameinfo_t *info, combination_list_t *curr, uint8_t counter);

/**
 * @brief free all nodes of list
 * @param list start of list to free
 */
static void free_perm_list(perm_list_t *list);


/**
 * @brief Builds the combination tree and the final guesses list. Then processes the guesses using find_best_perm till game finishes
 * @param info Main Game info
 * @return server response error or num of rounds till won. can bail_out !!!
 */
static uint8_t analyse_combinations(gameinfo_t *info);

/**
 * @brief After all colors have been tested, run over them again and see how we can decrease our possibilities
 * @param info Main Game info
 */
static void analyse_possible(gameinfo_t *info);
/**
 * @brief Test all colors until found all of both partitions
 * @param info Main Game info
 * @return server response error or num of rounds till won. can bail_out !!!
 */
static uint8_t analyse_colors(gameinfo_t *info);
/**
 * @brief test the two partitions against the server
 * @param info Main Game info
 * @return server response error or num of rounds till won. can bail_out !!!
 */
static uint8_t analyse_partitions(gameinfo_t *info);

/**
 * @brief Actual Game Entry point
 * @param connfd server socket
 * @return return code from server
 */
static uint8_t start_game(int connfd);

/**
 * @brief Checks if port argument is numeric and in port range
 * @param argc arg counter
 * @param argv arg array
 * @return 0 if valid port was provided, EXIT_ERROR otherwise
 */
static int check_args(int argc, char **argv);

static void free_resources(void)
{

	/* block signals to prevent concurrency */
	sigset_t blocked_signals;	
	(void) sigfillset(&blocked_signals);
	(void) sigprocmask(SIG_BLOCK, &blocked_signals, NULL);
	
	/* set term flag so if another signal triggers this, it won't run  twice */
	if(terminating == 1) {
		return;
	}
	
	terminating = 1;

	if(game) {

		if(game->connfd >= 0) {
			(void) close(game->connfd);
		}

		if(game->comb) {
			free_combination_list(game->comb);
		}
	
		if(game->first_perm) {
			free_perm_list(game->first_perm);

		}

		if(game->processed) {
			free_perm_list(game->processed);
		}

		free(game);	
		game = NULL;
	}

}

static void signal_handler(int sig)
{
	DEBUG("Caught Signal %d\n", sig);
	free_resources();
	exit(EXIT_SUCCESS);
}

static void bail_out(int eval, const char *fmt, ...)
{
	va_list ap;

	(void) fprintf(stderr, "%s: ", progname);
	if (fmt != NULL) {
		va_start(ap, fmt);
		(void) vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	if (errno != 0) {
		(void) fprintf(stderr, ": %s", strerror(errno));
	}
	(void) fprintf(stderr, "\n");

	free_resources();
	exit(eval);
}

static int connect_to_server(char *host, char *port) 
{

	struct addrinfo* ai, hints;
	int sockfd = -1;
	(void) memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_INET; /* IPv4 only, IPv6: AF_INET6  */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	if(getaddrinfo(host, port, &hints, &ai) != 0 || ai == NULL) {
	
		bail_out(EXIT_FAILURE, "getaddrinfo()");

	}

	if((sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0) {

		bail_out(EXIT_FAILURE, "socket()");
	}

	if(connect(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {

		freeaddrinfo(ai);
		// game->sockfd is not set yet, so we need to close sockfd ourselves
		(void) close(sockfd);
		bail_out(EXIT_FAILURE, "connect()");

	}	

	freeaddrinfo(ai);
	return sockfd;

}

static void compute_parity(uint16_t *req)
{

	uint16_t p = *req;
	uint8_t parity = 0;

	/* Clear out Parity Bit */
	*req &= ~(1 << 15);

	/* Always xor the rightmost bit and shift right by one */
	for (int j = 0; j < (SLOTS * COLOR_WIDTH); ++j) {
		parity ^= (p & 0x1);
		p >>= 1;
	}
	
	/* Set parity */
	*req |= (parity << 15);

}

static int add_color(uint16_t *req, color_t color, int position)
{

	/* Position out of bounds */
	if(position >= SLOTS) return -1;

	/* Calculate position in req */
	position = position * COLOR_WIDTH;

	/* Clear out formerly set color (if set already) */
	//*req &= ~(1 << position) & ~(1 << (position + 1)) & ~(1 << (position + 2));
	*req &= ~((1 << position) | (1 << (position + 1)) | (1 << (position + 2)));

	/* Set color */
	*req |= color << position;

	return 0;
}

static int init_request(uint16_t *req, int num, color_t colors[]) 
{

	/* Clear out the request */
	*req = 0;

	/* And add colors */
	for(int i = 0; i < num; i++) {
		if(add_color(req, colors[i], i) < 0) {
			return -1;
		}
	}

	return 0;

}

static size_t read_from_server(int fd, uint8_t *buffer, size_t n)
{

	size_t bytes_read = 0;

	do {
		ssize_t r;
		r = read(fd, buffer + bytes_read, n - bytes_read);
		if (r <= 0) {
			return -1;
		}
		bytes_read += r;
	} while (bytes_read < n);

	if (bytes_read < n) {
		return -1;
	}

	return bytes_read;
}

static size_t write_to_server(int fd, uint8_t *buffer, size_t n)
{

	size_t bytes_sent = 0;

	do {
		ssize_t r;
		r = write(fd, buffer + bytes_sent, n - bytes_sent);
		if (r <= 0) {
			return -1;
		}
		bytes_sent += r;
	} while (bytes_sent < n);

	if (bytes_sent < n) {
		return -1;
	}

	return bytes_sent;
}

static void generate_request(uint8_t *buffer, uint16_t *req)
{
	/* Before sending, we'll check the parity bit */
	compute_parity(req);

	/* Now we need to flip the 16bit request into a 2*8bit array, to ensure, that the lower bits are sent before the upper ones */
	buffer[0] = *req & ~(0xff << 8);
	buffer[1] = *req >> 8;
}

static uint8_t commit_guess(gameinfo_t *info, color_t *partition, result_t *result)
{

	// Clients round counter
	info->round++;

	// Gereate guesss from color_t[]
	if(partition) {
		if(init_request(&(info->guess), SLOTS, partition) < 0) {
			bail_out(EXIT_FAILURE, "init_request() (round %d)", info->round);
		}
	}

	generate_request(info->request_buf, &(info->guess));

	if(write_to_server(info->connfd, info->request_buf, REQUEST_WIDTH) < 0) {
		bail_out(EXIT_FAILURE, "Error writing to server (round %d)", info->round);
	}

	if(read_from_server(info->connfd, info->response_buf, RESPONSE_WIDTH) < 0) {
		bail_out(EXIT_FAILURE, "Error reading from server (round %d)", info->round);
	}

	// Set error
	info->error = RESPONSE_ERROR(*(info->response_buf));

	// generate result
	result->red = RED(*(info->response_buf));
	result->white = WHITE(*(info->response_buf));
	result->total = TOTAL(*(info->response_buf));

	perm_list_t *pl = malloc(sizeof(perm_list_t));
	(void) memset(pl, 0, sizeof(perm_list_t));

	pl->combination = info->guess;
	pl->red = result->red;

	if(!info->processed) {

		info->processed = info->processed_last = pl;
		pl->prev = pl->next = NULL;

	} else {

		pl->prev = info->processed_last;
		info->processed_last->next = pl;
		pl->next = NULL;
		info->processed_last = pl;

	}
	

	if(info->error > 0) {
		RETURN_ERROR(info->error);
	}

	if(result->red == SLOTS) {
		// won at round i
		return info->round;
	}

	return 0;

}

static inline uint8_t calculate_set_bits(uint8_t num, int length)
{

	uint8_t bits_set = 0;

	for(size_t i = 0; i < length; ++i, num >>= 1) {
	
		if (num & 0x1)  ++bits_set;

	}

	return bits_set;
}

static uint8_t compute_combination(uint8_t last_combination, uint8_t mask, uint8_t count) 
{

	last_combination++;

	// Basically we increase the last combination by one until we find a match or the mask is overrun
	while((last_combination & mask) != last_combination || calculate_set_bits(last_combination, SLOTS) != count) {
		last_combination++;		
		if(last_combination > mask) {
			return 0;
		}
	}
	
	return last_combination;

}

static void free_combination_list(combination_list_t *list) 
{
	if(list->first_child) {
		free_combination_list(list->first_child);
		list->first_child = NULL;
	}

	if(list->next) {
		free_combination_list(list->next);
		list->next = NULL;
	}

	free(list);
}

static void build_combination_list(gameinfo_t *info, combination_list_t **parent)
{

	combination_list_t *self = malloc(sizeof(combination_list_t));
	int is_root = 0;
	(void) memset(self, 0, sizeof(combination_list_t));
	
	if(*parent == NULL) {
		// Non recurive call
		*parent = self;
		self->mask = ~0 & (uint8_t) ~(0x7 << 5);
		is_root = 1;

	}
	/* else {

		(*parent)->first_child = self;

	}
	*/

	// Search the first possible color after the parents color
	for(int i = (*parent)->next_color; i < COLORS; ++i) {

		if(info->possible[i]) {

			uint8_t mask = (info->possible[i] & (uint8_t) ~(0x7 << SLOTS));
			uint8_t p_mask = (*parent)->mask;
			uint8_t needed = info->possible[i] >> SLOTS ;
			int entropy = 1;

			uint8_t combination = (((uint8_t) ~0 ) >> (COLORS - needed)) - 1;

			self->color = i;
			self->next_color = i + 1;

			mask &= p_mask;

			// no need to compute combination with an empty mask
			if(!mask) {
				self->combination = 0;
				return;
			}

			// We'll compute the first combination
			if((combination = self->combination = compute_combination(combination, mask, needed)) == 0) {
				// no need to proceed with an empty combination
				return;
			}
			self->mask = ((p_mask & ~(self->combination)) & (uint8_t) ~(0x7 << SLOTS));

			if(i < COLORS - 1 && self->mask)  {

				// attach all colors below as children
				build_combination_list(info, &self);

				if(self->first_child) {
					if(!self->first_child->combination) {
						//if our child had return w/o combination (see above), wee cann drop it
						free(self->first_child);
						self->first_child = NULL;
					}
				}
			}
			
			// now we compute our sibling combinations and recursivle buid their tree
			while((combination = compute_combination(combination, mask, needed))) {

				combination_list_t *sibling = malloc(sizeof(combination_list_t));
				(void) memset(sibling, 0, sizeof(combination_list_t));
			
				sibling->color = i;	
				sibling->next_color = i + 1;
				sibling->combination = combination;

				sibling->mask = ((p_mask & ~(sibling->combination)) & (uint8_t) ~(0x7 << 5));

				if(entropy % 2) {

					sibling->next = self;
					self = sibling;		
				
				} else {

					sibling->next = self->next;
					self->next = sibling;
				}

				entropy++;

				if(i < COLORS - 1  && sibling->mask) {
					build_combination_list(info, &sibling);

					if(sibling->first_child) {

						if(!sibling->first_child->combination) {
							free(sibling->first_child);
							sibling->first_child = NULL;
						}

					}
					
				}
			}

			break;
			
		}

	}

	if(is_root) {
		*parent = self;
	} else {
		(*parent)->first_child = self;
	}
	

}

static void free_perm_list(perm_list_t *list) {

	if(list->next) {
		free_perm_list(list->next);
		list->next = NULL;
	}

	free(list);

}

static void process_combinations(gameinfo_t *info, combination_list_t *curr, uint8_t counter)
{

	uint8_t i = 0, j = 0;
	uint8_t combination = curr->combination;

	// store guess
	uint16_t guess = info->guess;

	i = calculate_set_bits(combination, SLOTS);

	for(j = 0; j < SLOTS; ++j) {

		// Set our color
		if(combination & 0x1) add_color(&(info->guess), curr->color, j);
		combination >>= 1;
	}


	if(curr->first_child) {
		// process child nodes
		process_combinations(info, curr->first_child, counter + i);

	} else {
		// i am a leave, if all needed colors are set (counter) i can create a possible guess
		perm_list_t *pl = NULL;

		if((counter + i) == SLOTS) {

			
			pl = malloc(sizeof(perm_list_t));
			(void) memset(pl, 0, sizeof(perm_list_t));

			pl->combination = info->guess;

			if(!info->first_perm) {
				info->first_perm = info->last_perm = pl;
			} else {
				info->last_perm->next = pl;
				pl->prev = info->last_perm;
				info->last_perm = pl; 
			}
		}

	}

	if(curr->next) {
		// We need to restore the original guess, and let out siblings process their trees
		info->guess = guess;
		process_combinations(info, curr->next, counter);
	}
	

}

static uint8_t compare_perm_equal_pos(uint16_t cmb1, uint16_t cmb2)
{

	uint8_t eq = 0;

	// count how many slots are equal in both combinations
	for(int i = 0; i < SLOTS;  i++) {

		if((cmb1 & (uint16_t) 0x7) == (cmb2 & (uint16_t) 0x7)) {
			eq++;
		}

		cmb1 >>= 3;		
		cmb2 >>= 3;		

	}

	return eq;

}

static perm_list_t *find_best_perm(gameinfo_t *info)
{

	perm_list_t *p, *q;
	uint8_t j;
	uint8_t matching = 1;

	p = info->first_perm;

	// process all combinations left 
	while(p) {

		q = info->processed;
		matching = 1;

		// process all combinations processed already
		while(q) {

			// if p doesnt fit any of the already processed p cant be correct
			if((j = compare_perm_equal_pos(p->combination, q->combination)) != q->red) {
	
				matching = 0;
				break;
			} 

			q = q->next;
		}

		if(matching) { 

			return p;
		}

		p = p->next;
	}

	return p;
	
}

static uint8_t analyse_combinations(gameinfo_t *info)
{

	combination_list_t *list = NULL;
	perm_list_t *curr = NULL;
	result_t result;
	uint8_t ret = 0;

	info->guess = 0;
	
	// create combination tree
	build_combination_list(info, &list);

	game->comb = list;

	// create list of possible guesses
	process_combinations(info, list, 0);

	curr = info->first_perm;

	// Test all remaining combinations
	while(1) {

		info->guess = curr->combination;

		if((ret = commit_guess(info, NULL, &result)) != 0) {
			break;
		}

		curr->red = result.red;

		/* we need to remove curr from possible combination list {{{ */
		if(!curr->prev) {
			if(curr->next) {
				curr->next->prev = NULL;
			}

			info->first_perm = curr->next;
		} else {

			curr->prev->next = curr->next;			

		}

		if(!curr->next) {
			if(curr->prev) {
				curr->prev->next = NULL;
			}
			info->last_perm = curr->prev;
		} else {
			curr->next->prev = curr->prev;
		}

		/* }}} */


		free(curr);

		// fetch next possible guess			
		if((curr = find_best_perm(info)) == NULL) {
			/* This should never happen */
			bail_out(EXIT_FAILURE, "Could not find more combinations to try. This should not happen\n");
		}

	
	}

	return ret;

}

static void analyse_possible(gameinfo_t *info)
{

	for(int i = 0; i < 2; i++) {

		/* If we didn't have any hits in the partition guess, we can mark all positions for all of it's color as impossible*/
		if(info->result[i].red == 0) {
			// set impossible
			for(int j = 0; j < SLOTS; j++) {

				color_t c = info->partitions[i][j];

				if(info->possible[c]) {
					info->possible[c] &= (uint8_t) ~(1 << j);
				}
	
				
	
			}

		/* 
		 * If the red marks we received when we tested the partition is the sum of red marks we got when we tested the colors of the partition 
		 * this means that each color that is still possible, has to be at exactely the position it was in the partition
		 */
		} else if(info->result[i].red == info->result[i].real_hits) {

			for(int j = 0; j < SLOTS; j++) {

				color_t c = info->partitions[i][j];
				// In the first round we only clear out the slots part
				info->possible[c] &= (uint8_t) (0x7 << SLOTS);

			}

			for(int j = 0; j < SLOTS; j++) {

				color_t c = info->partitions[i][j];
				// then we set the possible bits
				if(info->possible[c]) info->possible[c] |= (uint8_t) (0x1 << j);

			}
			
		}
	}

}

static uint8_t analyse_colors(gameinfo_t *info) 
{

	uint8_t total = 0, ret = 0;
	uint8_t last_color_missing[2] = {0, 0};
	result_t result;

	// Loop through both partitions 
	for(int i = 0; i < 2; i++) {

		// If we didn't have any hits, we can skip the partition
		if(info->result[i].total > 0) {

			uint8_t found = 0;

			// Loop though the colors of the partition 
			for(int j = 0, k = (i * 4); k < (i * 4) + 3; j++, k++) {

				color_t partition[] = { k, k, k, k, k };
				
				// Find out how many occurances the color effectivly has
				if((ret = commit_guess(info, partition, &result)) != 0) {
					return ret;
				}

				// We found a color that's definitly not in the secret code
				if(result.total == 0) {
					info->possible[k] = 0;
					continue;
				}

				total += result.total;
			
				// we need this for analyse_possible
				info->result[i].real_hits += result.total;
				
				// Here we store, how many occurances the color effectivly has
				info->possible[k] |= (result.total << SLOTS);

				// All needed of partition found || all totally needed found || found in first partition = (SLOTS - second partition's total)
				if(++found == info->result[i].total || 	total == SLOTS || 
					(i ==  0 && (total + info->result[1].total) == SLOTS)) {

					size_t s = (3 - j) * sizeof(uint8_t);
					// clear out all possible colors after this one
					(void) memset(&info->possible[k + 1], 0, s);
					break;

				}

			}

			// As we only process the first three colors of each partition, and we are not sure about the last one yet, we'll signal this here
			if(found < info->result[i].total) {
				last_color_missing[i] = 1;
			}
			
		}
	}

	// not all needed positions are known yet	
	if(total < SLOTS) {

		int i = 1;

		if(last_color_missing[0]) { 
			// if the last position of the first and the second partition is missing, we need to test one of them against the server
			if(last_color_missing[1]) {
				
				// Fetch part1 color 4
				color_t partition[] = { 7, 7, 7, 7, 7 };
				
				if((ret = commit_guess(info, partition, &result)) != 0) {
					return ret;
				}

				total += result.total;
				info->result[1].real_hits += result.total;
				info->possible[7] |= (result.total << SLOTS);

			}

			// and then simply use the remaining needed positions for the second one
			i = 0;

		}

		if(SLOTS - total == 0) {
			info->possible[(i * 4) + 3] = 0;
		} else {
			info->possible[(i * 4) + 3] |= ((SLOTS - total) << SLOTS);
		}
		info->result[i].real_hits += (SLOTS - total);

		total = SLOTS;
		
	} else {

		info->possible[3] = 0;
		info->possible[7] = 0;

	}
	
	return 0;	

}

static uint8_t analyse_partitions(gameinfo_t *info) 
{

	int skip = 0;
	
	for(int i = 0, j = 1; i < 2 && !skip; i++, j = (i + 1) % 2)  {

		uint8_t clear_partition, ret;

		if((ret = commit_guess(info, info->partitions[i], &(info->result[i]))) != 0) {
			return ret;
		};

		if(info->result[i].total == SLOTS) {
			// No colors of partition (i + 1) % 2 are correct
			// Clear possible colors from ((i + 1) % 2) * 4

			skip = 1;
			clear_partition = j;

			info->result[clear_partition].total = info->result[clear_partition].white = info->result[clear_partition].red = 0;

		}

		if(info->result[i].total == 0) {

			// No colors of partition i are correct
			// Clear possible colors from i * 4

			info->result[j].total = SLOTS;
			// We don't know how many reds we have at partition ((i + 1) % 2), but we set this for analyse_colors (see below)
			info->result[j].red = 1;
			info->result[j].white = 0;

			skip = 1;
			clear_partition = i;

		} 

		if(skip) {

			uint8_t *start_clear = &(info->possible[(clear_partition * 4)]);

			for(int j = 0; j < 4; j++) {

				*start_clear = 0;
				start_clear++;

			}
			
		}

	}

	return 0;


}

static uint8_t start_game(int connfd)
{

	/* allocate main game info  */
	gameinfo_t *info = game = malloc(sizeof(gameinfo_t));
	/* initialize it */
	(void) memset(info, 0, sizeof(gameinfo_t));

	uint8_t ret;

	/* these are the two partitions we start with */
	color_t *p0 = (color_t []) {beige, beige, darkblue, green, orange};
	color_t *p1 = (color_t []) {red, red, black, violet, white};

	/* We claim, all colors are possible at all positions in the beginning */
	uint8_t possible[] = {

		ALL_SLOTS,
		ALL_SLOTS,
		ALL_SLOTS,
		ALL_SLOTS,
		ALL_SLOTS,
		ALL_SLOTS,
		ALL_SLOTS,
		ALL_SLOTS

	};

	/* store connfd in game struct, so it can be closed by free_resources. Until here no bail_out can have occured */
	info->connfd = connfd;

	/* init result array */
	(void) memset(info->result, 0, 2 * 4 * sizeof(uint8_t));
	
	/* set partitions */
	info->partitions[0] = p0;
	info->partitions[1] = p1;

	/* set possible */
	info->possible = possible;

	/* Test partitions, we can have an error or a game won here. If so, return */
	if((ret = analyse_partitions(info)) != 0) {
		return ret;
	};

	/* Test colors, we can have an error or a game won here. If so, return */
	if((ret = analyse_colors(info)) != 0) {
		return ret;
	}

	/* Fine tune possibilities for colors. No error can occur here */
	analyse_possible(info);

	/* Here we process all combinations, so this has to return something */
	if((ret = analyse_combinations(info)) != 0) {
		return ret;
	}

	/* this should never happen */
	RETURN_ERROR((uint8_t) 0);

}

static int check_args(int argc, char **argv)
{

	long int port;
	char *portend;

	if(argc > 0) {
		progname = argv[0];
	}

	if(getopt(argc, argv, "") != -1 || (argc - optind) != 2) {

		(void) fprintf(stderr, "Usage: %s <host> <port>\n", progname);
		return EXIT_FAILURE;
	}
	
	errno = 0;
	port = strtol(argv[2], &portend, 10);
	
	if ((errno == ERANGE && (port == LONG_MAX || port == LONG_MIN)) || (errno != 0 && port == 0) || argv[2] == portend) {

		(void) fprintf(stderr, "Error parsing port as number\n");
		return EXIT_FAILURE;

	}

	if(port < 1 || port > 65535) {

		(void) fprintf(stderr, "Port needs to be a number from 1 to 65535\n");
		return EXIT_FAILURE;

	}

	return 0;

}

int main(int argc, char **argv) 
{
	
	int ret;
	int sockfd;
	sigset_t blocked_signals;

	if((ret = check_args(argc, argv)) != 0) {
		return ret;
	}

	/* register signal handler for SIGINT, SIGQUITm SIGTERM */
	if(sigfillset(&blocked_signals) < 0) {

		bail_out(EXIT_FAILURE, "sigfillset");

	} else {

		const int signals[] = { SIGINT, SIGQUIT, SIGTERM };
		struct sigaction s;

		s.sa_handler = signal_handler;
		(void) memcpy(&s.sa_mask, &blocked_signals, sizeof(s.sa_mask));
		s.sa_flags   = SA_RESTART;

		for(int i = 0; i < 3; i++) {
	
			if (sigaction(signals[i], &s, NULL) < 0) {

				bail_out(EXIT_FAILURE, "sigaction");

			}
		}
	}

	sockfd = connect_to_server(argv[1], argv[2]);
	ret = start_game(sockfd);

	/* Error bit in return value is set */
	if(IS_ERROR(ret)) {

		/* Clear error bit (so ret++ below will give the correct return value) */
		ret &= ~GAME_ERROR_BIT;
		
		if(ret & ERROR_PARITY) {
			(void) fprintf(stderr, "Parity Error\n");
		}
			
		if(ret & ERROR_GAME_LOST) {
			(void) fprintf(stderr, "Game lost\n");
		}

		if(!ret) {
			/* ret == 0 here */
			(void) fprintf(stderr, "Game unexpactedly interrupted\n");
		}	

		ret++;

	} else {

		/* We have won !! ret holds the current round */
		(void) fprintf(stdout, "Runden: %d\n", ret);
		/* reset return value to SUCCESS */
		ret = 0;

	}

	free_resources();
	return ret;	

}
