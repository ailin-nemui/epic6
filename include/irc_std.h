/*
 * irc_std.h: This is where we make up for operating system lossage
 * Originally written by Matthew Green, Copyright 1993
 * Various modifications by various people since then.
 *
 * See the copyright file, or do a help ircii copyright 
 */

/*
 * A note from the author...
 *
 * UNIX portability has been fraught with peril over the years,
 * especially in the early 90s, the early days of ircII
 * However, instead of trying to support a dozen different commercial 
 * flavors of UNIX all of which were bizarre in some unique way, 
 * the IRC world has collapsed onto Linux and the BSDs.  People no
 * longer try to run irc clients on AIX, Solaris, HP/UX, Xenix, and
 * other silliness like that.  And if they did, they wouldn't be
 * using this client.
 *
 * In this file, I refer to the Open Group Base Specification Issue, 
 * which corresponds with one or more editions of POSIX (IEEE Std-1003.1-YYYY)
 *
 *	Issues 1-4 were released in the 80s and early 90s
 *	Issue 5 was released in the late 90s
 *	Issue 6 was released in 2001.
 *	Issue 7 was released in 2008, 2013, 2016, and 2018
 *	Issue 8 was released in 2024
 *
 * The point of this is that Issue 6 is the baseline that every 
 * self-respecting unix-like system should be supporting by now.
 *
 * To any extent this software uses anything newer than Issue 6,
 * the configure file checks for it since either Linux or BSD
 * probably won't have some things.
 *
 * If anybody reports a compile error because I #include'd a header
 * file that your system doesn't have, please note the Issue number
 * and then explain to me why your system isn't up to snuff on that.
 */

#ifndef __irc_std_h
#define __irc_std_h

/* Bah. */
#define _GNU_SOURCE 1

#include "defs.h"

/*
 * Everybody needs these ANSI headers...
 */
#include <ctype.h>		/* C90 */
#include <math.h>		/* C90 */
#include <setjmp.h>		/* C90 */
#include <stdarg.h>		/* C90 */
#include <stdio.h>		/* C90 */
#include <stdlib.h>		/* C90 */
#include <string.h>		/* C90 */
#include <time.h>		/* C90 */

#include <wctype.h>		/* C95 */

#include <stdint.h>		/* C99 */
#include <inttypes.h>		/* C99 */
#include <stddef.h>		/* C99 */
#include <locale.h>		/* C99 */
#include <float.h>		/* C99 */

/*
 * Everybody needs these POSIX headers...
 */
#include <errno.h>		/* Issue 1 */
#include <fcntl.h>		/* Issue 1 */
#include <limits.h>		/* Issue 1 */
#include <pwd.h>		/* Issue 1 */
#include <signal.h>		/* Issue 1 */
#include <sys/stat.h>		/* Issue 1 */
#include <sys/types.h>		/* Issue 1 */
#include <sys/utsname.h>	/* Issue 1 */
#include <unistd.h>		/* Issue 1 */

#include <langinfo.h>		/* Issue 2 */

#include <sys/wait.h>		/* Issue 3 */
#ifdef HAVE_TERMIOS_WINSIZE
#include <termios.h>		/* Issue 3 */
#endif

#include <glob.h>		/* Issue 4 */
#include <iconv.h>		/* Issue 4 */
#include <regex.h>		/* Issue 4 */
#include <poll.h>		/* Issue 4.2 */

#include <sys/socket.h>		/* Issue 6 */
#include <netinet/in.h>		/* Issue 6 */
#include <arpa/inet.h>		/* Issue 6 */
#include <netdb.h>		/* Issue 6 */
#include <sys/un.h>		/* Issue 6 */

/*
 * Issue 6 requires this to be #define'd, 
 * but I guess I found at least one system 
 * somewhere that doesn't have it.  Maybe 
 * delete this in the future and see if 
 * anything breaks...
 */
#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif

/*
 * This header existed in Issue 2 but disappeared
 * before at least Issue 6.
 * Some systems define tputs, etc in this header.
 */
#ifdef HAVE_TERM_H
# include <term.h>		/* Non-standard */
#endif

/*
 * C99 requires 'isnan' and 'isinf' and the like 
 * to be defined in <math.h>, but some systems 
 * still have them in <ieeefp.h>
 */
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>		/* Non-standard */
#endif

#ifdef HAVE_XLOCALE_H
#include <xlocale.h>		/* Non-standard */
#endif

/*
 * OK.  I admit it -- I'm just trusting to luck
 * that every system will have this.  I have no
 * justification for that assumption.
 */
#include <ifaddrs.h>		/* Non-standard */

/*
 * Some systems do not have tcgetwinsz (Issue 7),
 * so we must include this and use ioctl(TIOCGETWZ)
 */
#include <sys/ioctl.h>		/* Non-standard */


#define __A(x) __attribute__((format (printf, x, x + 1)))
#define __N    __attribute__((noreturn))

#ifdef HAVE_ATTRIBUTE_MAY_ALIAS
#define MAY_ALIAS __attribute__((may_alias))
#else
#define MAY_ALIAS
#endif

#ifdef HAVE_ATTRIBUTE_FALLTHROUGH
#define FALLTHROUGH __attribute__((fallthrough));
#else
#define FALLTHROUGH 
#endif

/*
 * Figure out how to make alloca work
 * I took this from the autoconf documentation
 * XXX Surely this is not the way to still do this, right?
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
 * XXX These are heinous and should go away...
 */
#ifndef MIN
# define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b))?(a):(b))
#endif

/* 
 * Previously i tried to define PATH_MAX if it was missing.
 * But PATH_MAX was required in Issue 1, so there's no excuse.
 */

/*
 * NSIG is a pain in my [censored]
 * NSIG is non-standard before Issue 8.
 * Issue 8 defined it as sysconf(_SC_NSIG), 
 * so someday we should add that to the mix here.
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
typedef void 		sigfunc 		(int);
	int		block_signal 		(int);
	int		unblock_signal 		(int);
	sigfunc *	my_signal 		(int, sigfunc *);
#define SIGNAL_HANDLER(x) void x 		(int unused)
	sigfunc *	init_signals		(void);
	void		init_signal_names	(void);
	const char *	get_signal_name 	(int);
	int		get_signal_by_name	(const char *);	/* Returns -1 on error */

extern	volatile sig_atomic_t	signals_caught[NSIG];

#define BUILT_IN_COMMAND(x)	void x 	(const char *command, char *args, const char *subargs)
#define BUILT_IN_KEYBINDING(x)	void x 	(unsigned int key, char *string)

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
 * All these are in C99's <inttypes.h>
 * They first appeared in Issue 5.
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

/*
 * C99 requires that type-punned aliases must be accessed through a union.
 * So, that's why we do this.  Always write to 'ss' and then read back
 * through whatever type you need.
 */
typedef union SSu {
	struct sockaddr 	sa;		/* Issue 6 */
	struct sockaddr_storage	ss;		/* Issue 6 */
	struct sockaddr_in	si;		/* Issue 6 */
	struct sockaddr_in6	si6;		/* Issue 6 */
	struct sockaddr_un	su;		/* Issue 6 */
} SSu;

typedef struct timespec		Timespec;	/* Issue 1 */
typedef struct stat		Stat;		/* Issue 1 */
typedef struct addrinfo		AI;		/* Issue 6 */

/*
 * See if we are supposed to give valgrind a hand in memory leak checking
 */
#ifdef HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>			/* Non-standard */
#else
#define VALGRIND_CREATE_MEMPOOL(x,y,z)
#define VALGRIND_MEMPOOL_ALLOC(x,y,z)
#define VALGRIND_MEMPOOL_TRIM(x,y,z)
#define VALGRIND_MEMPOOL_FREE(x,y)
#define VALGRIND_DESTROY_MEMPOOL(x)
#endif



/* Everybody needs these OpenSSL headers */
#define OPENSSL_SUPPRESS_DEPRECATED 1
#define OPENSSL_SUPPRESS_DEPRECATED_3_0 1

#include <openssl/crypto.h>			/* Non-standard */
#include <openssl/err.h>			/* Non-standard */
#include <openssl/evp.h>			/* Non-standard */
#include <openssl/hmac.h>			/* Non-standard */
#include <openssl/opensslconf.h>		/* Non-standard */
#include <openssl/pem.h>			/* Non-standard */
#include <openssl/rand.h>			/* Non-standard */
#include <openssl/sha.h>			/* Non-standard */
#include <openssl/ssl.h>			/* Non-standard */
#include <openssl/x509.h>			/* Non-standard */
#include <openssl/x509v3.h>			/* Non-standard */

/* Everybody needs these libsodium headers */
#include <sodium.h>

/* Everytbody needs these c-ares headers */
#include <ares.h>				/* Non-standard */

#endif /* __irc_std_h */
