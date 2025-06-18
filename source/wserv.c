/*
 * wserv.c -- A little program to act as a pipe between the ircII process
 * 	      and an xterm window or GNU screen.
 * Version 1 - Classic ircII wserv, Used Unix sockets, took /tmp/irc_xxxxxx arg
 * Version 2 - Circa 1997 EPIC, supported 4.4BSD Unix sockets as well
 * Version 3 - Circa 1998 EPIC, Used TCP sockets
 * Version 4 - Circa 1999 EPIC, Seperate Data and Command TCP sockets.
 *
 * Copyright (c) 1992 Troy Rollo.
 * Copyright (c) 1993 Matthew Green.
 * Copyright 1995-2002 EPIC Software Labs
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote 
 *    products derived from this software without specific prior written
 *    permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#define WSERV_C
#define CURRENT_WSERV_VERSION 	4

#include "defs.h"
#include "config.h"
#include "irc_std.h"

static 	int 	data = -1;
static	int	cmd = -1;
static	char	buffer[256];
static	int	got_sigwinch = 0;
static	int	tty_des;
static	struct termios	oldb, newb;

static	void 	my_exit(int);
static	void 	ignore (int);
static	void	sigwinch_func (int);
static	int 	term_init (void);
static	void 	term_resize (void);
	void	yell (const char *format, ...);
static	int	connectory (const char *, const char *);


int	main (int argc, char **argv)
{
	ssize_t	nread;
	char * 	port;
	char *	host;
	char *	tmp;
	char	stuff[100];
	struct pollfd	fds[2];

	my_signal(SIGHUP, SIG_IGN);
	my_signal(SIGQUIT, SIG_IGN);
	my_signal(SIGINT, ignore);
	my_signal(SIGWINCH, sigwinch_func);

	if (argc != 3)    		/* no socket is passed */
		my_exit(1);

	host = argv[1];
	port = argv[2];

	if ((data = connectory(host, port)) < 0)
		my_exit(23);

	if ((cmd = connectory(host, port)) < 0)
		my_exit(25);

	/*
	 * First thing we do is tell the parent what protocol we plan on
	 * talking.  We will start the protocol at version 4, for this is
	 * the fourth generation of wservs.
	 *
	 * We cannot use 'snprintf' here because we do not have the compat
	 * functions and some systems do not have snprintf!  This use of
	 * sprintf is demonstrably safe.
	 */
	snprintf(stuff, sizeof(stuff), "version=%d\n", CURRENT_WSERV_VERSION);
	if (!write(cmd, stuff, strlen(stuff))) 
		(void) 0;

	/*
	 * Next thing we take care of is to tell the parent client
	 * what tty we are using and tell them what geometry our screen
	 * is.  This provides some amount of sanity against whatever
	 * brain damage might occur.  The parent client takes our word
	 * for what size this screen should be, and handles it accordingly.
	 *
	 * Warning:  I am using sprintf() and write() because previous
	 * attempts to use writev() have shown that linux does not have
	 * an atomic writev() function (boo, hiss).  The parent client
	 * does not take kindly to this data coming in multiple packets.
	 *
	 * Again, we use sprintf() here because we can't be guaranteed
	 * that snprintf() is available in this program.  So I do the 
	 * length check first, and fail if it would overrun.
	 */
	tmp = ttyname(0);
	if (strlen(tmp) > 90)
		my_exit(90);
	snprintf(stuff, sizeof(stuff), "tty=%s\n", tmp);
	if (!write(cmd, stuff, strlen(stuff))) 
		(void) 0;

	term_init();		/* Set up the xterm */
	term_resize();		/* Figure out how big xterm is and tell epic */

	/*
	 * The select call..  reads from the socket, and from the window..
	 * and pipes the output from out to the other..  nice and simple.
	 * The command pipe is write-only.  The parent client does not
	 * send anything to us over the command pipe.
	 *
	 * XXX This should use poll(); but since the fds will be < 1024,
	 * I will do the refactor later.
	 */
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[1].fd = data;
	fds[1].events = POLLIN;
	for (;;)
	{
		int	poll_result;

		fds[0].revents = 0;
		fds[1].revents = 0;

		/* The magic value -1 is defined by the standard */
		poll_result = poll(fds, 2, -1);
		if (poll_result < 0)
		{
			if (errno == EINTR && got_sigwinch)
			{
				got_sigwinch = 0;
				term_resize();
			}
			continue;
		}

		if (fds[0].revents & POLLIN)
		{
			if ((nread = read(0, buffer, sizeof(buffer))) > 0)
			{
				if (!write(data, buffer, (size_t)nread)) 
					(void) 0;
			}
			else
				my_exit(3);
		}
		else if (fds[1].revents & POLLIN)
		{
			if ((nread = read(data, buffer, sizeof(buffer))) > 0)
			{
				if (!write(0, buffer, (size_t)nread)) 
					(void) 0;
			}
			else
				my_exit(4);
		}
	}

	my_exit(8);
}

static void 	ignore (int __U(value))
{
	/* send a ^C */
	char foo = 3;
	if (!write(data, &foo, 1)) 
		(void) 0;
}

static void	sigwinch_func (int __U(value))
{
	got_sigwinch = 1;
}

static void 	my_exit(int value)
{
	printf("exiting with %d!\n", value);
	printf("errno is %d (%s)\n", errno, strerror(errno));
	exit(value);
}


static int	connectory (const char *host, const char *port)
{
	AI	hints;
	AI *	results;
	int 	retval;
	int	s;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	if ((retval = getaddrinfo(host, port, &hints, &results))) {
	    yell("getaddrinfo(%s:%s): %s", host, port, gai_strerror(retval));
	    my_exit(6);
	}

	if ((s = socket(results->ai_family, results->ai_socktype, results->ai_protocol)) < 0) {
	    yell("socket(%s:%s): %s", host, port, strerror(errno));
	    my_exit(6);
	}

	if (connect(s, results->ai_addr, results->ai_addrlen) < 0) {
	    yell("connect(%s:%s): %s", host, port, strerror(errno));
	    my_exit(6);
	}

	freeaddrinfo(results);
	return s;
}


/*
 * term_init: does all terminal initialization... sets the terminal to 
 * CBREAK, no ECHO mode.    (Maybe should set RAW mode?)
 */
static int 	term_init (void)
{
	/* Set up the terminal discipline */
	tty_des = 0;			/* Has to be. */
	tcgetattr(tty_des, &oldb);

	newb = oldb;
	newb.c_lflag &= ~(unsigned int)(ICANON | ECHO); /* set equ. of CBREAK, no ECHO */
	newb.c_cc[VMIN] = 1;	          /* read() satified after 1 char */
	newb.c_cc[VTIME] = 0;	          /* No timer */

	newb.c_cc[VQUIT] = _POSIX_VDISABLE;
#	ifdef VDSUSP
		newb.c_cc[VDSUSP] = _POSIX_VDISABLE;
# 	endif
#	ifdef VSUSP
		newb.c_cc[VSUSP] = _POSIX_VDISABLE;
#	endif

	newb.c_iflag &= ~(unsigned long)(IXON | IXOFF); /* No XON/XOFF */
	tcsetattr(tty_des, TCSADRAIN, &newb);
	return 0;
}


/*
 * term_resize: gets the terminal height and width.  Trys to get the info
 * from the tty driver about size, if it can't... it punts.  If the size
 * has changed, it tells the parent client about the change.
 */
static void 	term_resize (void)
{
	static	int	old_li = -1,
			old_co = -1;
	struct winsize window;

#ifdef HAVE_TCGETWINSIZE
	if (tcgetwinsize(tty_des, &window) < 0)
#else
# if defined(TIOCGWINSZ)
	if (ioctl(tty_des, TIOCGWINSZ, &window) < 0)
# else
	if (1)
# endif
#endif
		return;		/* *Shrug* What can we do? */

	if (window.ws_row == 0 || window.ws_col == 0)
		return;		/* Ugh.  Bummer. */

	window.ws_col--;

	if ((old_li != window.ws_row) || (old_co != window.ws_col))
	{
		old_li = window.ws_row;
		old_co = window.ws_col;
		snprintf(buffer, sizeof(buffer), "geom=%d %d\n", old_li, old_co);
		if (!write(cmd, buffer, strlen(buffer))) 
			(void) 0;
	}
}

void	yell (const char *format, ...)
{
        if (format)
        {
                va_list args;
                va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
		sleep(5);
        }
}

/* End of file */
