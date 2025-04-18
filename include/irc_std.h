/*
 * irc_std.h: This is where we make up for operating system lossage
 * Originally written by Matthew Green, Copyright 1993
 * Various modifications by various people since then.
 *
 * See the copyright file, or do a help ircii copyright 
 */

#ifndef __irc_std_h
#define __irc_std_h

/* Bah. */
#define _GNU_SOURCE 1

#include "defs.h"

/*
 * Everybody needs these ANSI headers...
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <setjmp.h>
#include <glob.h>

/*
 * Everybody needs these POSIX headers...
 */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <regex.h>
#include <iconv.h>
#include <time.h>
#include <fcntl.h>
#include <locale.h>

/*
 * Everybody needs these INET headers...
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif
#include <sys/un.h>

/*
 * Some systems define tputs, etc in this header
 */
#ifdef HAVE_TERM_H
# include <term.h>
#endif


#define __A(x) __attribute__((format (printf, x, x + 1)))
#define __N    __attribute__((noreturn))

#ifdef HAVE_ATTRIBUTE_MAY_ALIAS
#define MAY_ALIAS __attribute__((may_alias))
#else
#define MAY_ALIAS
#endif

/*
 * Figure out how to make alloca work
 * I took this from the autoconf documentation
 */
#if defined(__GNUC__) && !defined(HAVE_ALLOCA_H)
# ifndef alloca
#  define alloca __builtin_alloca
# endif
#else
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca
char *alloca();
#   endif
#  endif
# endif
#endif

/*
 * Define the MIN and MAX macros if they don't already exist.
 */
#ifndef MIN
# define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b))?(a):(b))
#endif


/*
 * Deal with brokenness with realpath.
 */
#ifdef HAVE_BROKEN_REALPATH
# define realpath my_realpath
#endif

/*
 * Can you believe some systems done #define this?
 * I was told that hurd doesn't, so this helps us on hurd.
 */
#ifndef PATH_MAX
# ifndef MAXPATHLEN
#  ifndef PATHSIZE
#   define PATH_MAX 1024
#  else
#   define PATH_MAX PATHSIZE
#  endif
# else
#   define PATH_MAX MAXPATHLEN
# endif
#endif

/*
 * NSIG is a pain in my [censored]
 */
#ifndef NSIG
# ifdef _NSIG
#  define NSIG _NSIG
# else
#  define NSIG 32
# endif
#endif

/*
 * Define generic macros for signal handlers and built in commands.
 */
typedef void 		sigfunc 	(int);
	int		block_signal 	(int);
	int		unblock_signal 	(int);
	sigfunc *	my_signal 	(int, sigfunc *);
#define SIGNAL_HANDLER(x) void x 	(int unused)
	sigfunc *	init_signals	(void);
	void		init_signal_names (void);
	const char *	get_signal_name (int);
	int		get_signal_by_name (const char *);	/* Returns -1 on error */

extern	volatile sig_atomic_t	signals_caught[NSIG];

#define BUILT_IN_COMMAND(x)	void x (const char *command, char *args, const char *subargs)
#define BUILT_IN_KEYBINDING(x)	void x (unsigned int key, char *string)

typedef char Filename[PATH_MAX + 1];

/*
 * It's really really important that you never use LOCAL_COPY in the actual
 * argument list of a function call, because bad things can happen.  Always
 * do your LOCAL_COPY as a separate step before you call a function.
 */
#define LOCAL_COPY(y)	strcpy((char *)alloca(strlen((y)) + 1), y)
#define SAFE(x) 	(((x) && *(x)) ? (x) : empty_string)

/*
 * Figure out our intmax_t
 */
#ifdef PRIdMAX
# define INTMAX_FORMAT "%" PRIdMAX
# define UINTMAX_FORMAT "%" PRIuMAX
# define UINTMAX_HEX_FORMAT "%" PRIxMAX
#else
# define INTMAX_FORMAT "%jd"
# define UINTMAX_FORMAT "%ju"
# define UINTMAX_HEX_FORMAT "%jx"
#endif

#include <poll.h>
#ifndef INFTIM
#define INFTIM (-1)
#endif

#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif

/*
 * C99 requires that type-punned aliases must be accessed through a union.
 * So, that's why we do this.  Always write to 'ss' and then read back
 * through whatever type you need.
 */
typedef union SSu {
	struct sockaddr 	sa;
	struct sockaddr_storage	ss;
	struct sockaddr_in	si;
	struct sockaddr_in6	si6;
	struct sockaddr_un	su;
} SSu;

typedef struct addrinfo		AI;

typedef struct timeval		Timeval;
typedef struct stat		Stat;

/*
 * See if we are supposed to give valgrind a hand in memory leak checking
 */
#ifdef HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
#else
#define VALGRIND_CREATE_MEMPOOL(x,y,z)
#define VALGRIND_MEMPOOL_ALLOC(x,y,z)
#define VALGRIND_MEMPOOL_TRIM(x,y,z)
#define VALGRIND_MEMPOOL_FREE(x,y)
#define VALGRIND_DESTROY_MEMPOOL(x)
#endif

#ifdef HAVE_ATTRIBUTE_FALLTHROUGH
#define FALLTHROUGH __attribute__((fallthrough));
#else
#define FALLTHROUGH 
#endif


/* Everybody needs these OpenSSL headers */
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslconf.h>
#include <openssl/rand.h>

/* Everytbody needs these c-ares headers */
#include <ares.h>

#endif /* __irc_std_h */
