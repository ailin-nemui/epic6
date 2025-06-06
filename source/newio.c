/*
 * newio.c:  Passive, callback-driven IO handling for sockets-n-stuff.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2007 EPIC Software Labs.
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

#include "irc.h"
#include "ircaux.h"
#include "output.h"
#include "newio.h"
#include "ssl.h"
#include "timer.h"

/*
 * Issue 3 defines _SC_OPEN_MAX as the maximum number of file descriptors 
 * a process may have at runtime.  Because this is controlled by ulimits, 
 * we can't hardcode any number for this.  We ask for it at startup and 
 * then go with that.
 */
#define IO_ARRAYLEN 	sysconf(_SC_OPEN_MAX)

/*
 * Long ago there used to be a DOS attack where a remote peer would
 * send one byte every <1s and the client would block waiting for the 
 * data to stop.  We (1) don't block on incomplete lines and (2) shut 
 * off a remote peer that is just yanking our chain.
 */
#define MAX_SEGMENTS 	16


/*
 * The main event looper uses a two-cycle engine, seperating the physical
 * I/O from the application's consumption of the data.  We coordinate this 
 * buffering through this data structure.  
 * It contains:
 *	1) A file descriptor
 *	2) A data buffer and metadata about the buffer
 *	3) Application callback functions to consume the buffer
 *	4) Metadata about the fd itself.
 *
 * Cycle 1:
 *	I/O is possible on the file descriptor
 *	Data is read from the fd, and placed into the buffer
 *		with dgets_buffer().
 *	The fd is "dirty" ("clean == 0")
 * Cycle 2:
 *	For any dirty fd's, the application callback is called.
 *	Data is consumed from the buffer with dgets().
 *	Once all the data is consumed, the fd is "clean"
 */
typedef	struct	myio_struct
{
	/* Cycle 1 members */
	int		fd;
	char *		buffer;
	size_t		buffer_size,
			read_pos,
			write_pos;
	short		clean;
	short		segments,
			error,
			eof;
	int		(*io_callback) 	(int fd, int quiet, int revents);

	/* Cycle 2 members */
	void		(*callback) 		(int fd);
	void		(*failure_callback) 	(int fd, int error);

	/* Poll(2) members */
	struct pollfd	poll;
	int		poll_events;

	/* Metadata members */
	int		quiet;
	int		server;			/* For message routing */
}           MyIO;

static	MyIO **	io_rec = NULL;
static	int	global_max_fd = -1;

static	void	new_io_event (int fd, int revents);
static	void	fd_is_invalid (int fd);
static	int	unix_close (int fd, int quiet);

/**************************************************************************/
/**************************************************************************/

/*************************************************************************/
/*                             CYCLE 1                                   */
/*************************************************************************/
/*
 * do_wait -- The main sleeping routine.  When all of the fd's are clean,
 *	      go to sleep until an fd is dirty or a /TIMER goes off.
 *
 * Arguments:
 *	timeout	- A value previously returned by TimerTimeout().
 *		  'Timeout' is decremented by the time spent waiting.
 *
 * Return Value:
 *	-1	Interrupted System Call (ie, EINTR caused by ^C)
 *	 0	The timeout has expired (ie, call ExecuteTimers())
 *	 1	An fd is dirty (ie, call do_filedesc())
 */
int 	do_wait (Timespec *timeout)
{
static	int	polls = 0;
	int	fd;
	int	ms;
	int	retval;
	int	i, j, k;
	struct pollfd	*pollers;

	if (!timeout)
		panic(1, "do_wait: timeout is NULL.");

	/*
	 * Sanity Check -- A polling loop is caused when the
	 * timeout is 0, and occurs whenever timer needs to go off.  The
	 * main io() loop depends on us to tell it when a timer wants to go
	 * off by returning 0, so we check for that here.
	 *
	 * If we get more than 10,000 polls in a row, then something is 
	 * broken somewhere else, and we need to abend.
	 */
	if (timeout)
	{
	    if (timeout->tv_sec == 0 && timeout->tv_nsec == 0)
	    {
		if (polls++ > 10000)
		{
		    dump_timers();
		    panic(1, "Stuck in a polling loop. Help!");
		}
		return 0;		/* Timers are more important */
	    }
	    else
		polls = 0;
	}

	/* 
	 * It is possible that as a result of the previous I/O run, we are
	 * running recursively in io(). (/WAIT, /REDIRECT, etc).  This will
	 * obviously result in the IO buffers not being cleaned yet.  So we
	 * check for whether there are any dirty buffers, and if there are,
	 * we shall just return and allow them to be cleaned.
	 */
	for (fd = 0; fd <= global_max_fd; fd++)
		if (io_rec[fd] && !io_rec[fd]->clean)
			return 1;

	/* How long shall we sleep for? */
	ms = timeout->tv_sec * 1000;
	ms += (timeout->tv_nsec / 1000000);

	/* What shall we sleep waiting for? */
	pollers = new_malloc(sizeof(struct pollfd) * global_max_fd);
	memset(pollers, 0, (sizeof(struct pollfd) * global_max_fd));
	for (i = j = 0; i <= global_max_fd; i++)
	{
		if (io_rec[i])
			pollers[j++] = io_rec[i]->poll;
	}

	/* Go to sleep */
	retval = poll(pollers, j, ms);

	/* What happened? */
	if (retval < 0 && errno != EINTR)
		syserr(-1, "do_wait: poll() failed: %s", strerror(errno));
	else if (retval > 0)
	{
		for (k = 0; k < j; k++)
		{
		    if (pollers[k].revents)
		    {
			new_io_event(pollers[k].fd, pollers[k].revents);
			break;
		    }
		}
	}

	new_free(&pollers);
	return retval;
}

/*
 * Perform a synchronous i/o operation on a file descriptor.  
 * This function is called by do_wait() after we wake back up.
 *
 * The way I/O works in EPIC:
 *	+ main loop calls do_wait()
 *	  + If do_wait() decides an fd is ready, it looks at all fd's
 *		and for any fd's that are "ready", calls new_io_event().
 *	    + new_io_event() [us] does an I/O operation and queues up some
 *		data using dgets_buffer(), and marks the fd as "dirty"
 *	+ main loop calls do_filedesc() is called, and that calls the 
 *		user's callback, which uses dgets() to return 
 *		whatever was queued up here, and marks the fd as "clean".
 */
static void	new_io_event (int fd, int revents)
{
	MyIO *ioe;
	int	c = 0;

	if (!(ioe = io_rec[fd]))
		panic(1, "new_io_event: fd [%d] isn't set up!", fd);

	/* If it's dirty, something is wrong. */
	if (!ioe->clean)
		panic(1, "new_io_event: fd [%d] hasn't been cleaned yet", fd);

	if (ioe->io_callback)
	{
#if 0
		/* These flags tell us that further IO is pointless */
		if (revents & POLLHUP)
		{
			ioe->eof = 1;
			ioe->clean = 0;
			syserr(SRV(fd), "new_io_event: fd %d POLLHUP", fd);
			ioe->poll.events = 0;
		}
#endif
		if (revents & POLLNVAL && !ioe->quiet)
		{
			ioe->eof = 1;
			syserr(SRV(fd), "new_io_event: fd %d POLLNVAL - I will stop tracking this fd for io events", fd);
			ioe->poll.events = 0;
			fd_is_invalid(fd);
			return;
		}

		/* 
		 * We may expect ioe->io_callback() to either call dgets_buffer
		 * (which sets ioe->clean = 0) or to return an error (in which
		 * case we do it ourselves right here)
		 */
		else if ((c = ioe->io_callback(fd, ioe->quiet, revents)) <= 0)
		{
			ioe->error = -1;
			ioe->clean = 0;
			if (!ioe->quiet)
				syserr(SRV(fd), "new_io_event: fd %d must be closed", fd);

			if (x_debug & DEBUG_INBOUND) 
				yell("FD [%d] FAILED [%d] [%d]", fd, revents, c);
			return;
		}

		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d], did [%d]", fd, c);
	}
	else
	{
		/* 
		 * XXX It might have been more elegant to create a passthrough
		 * callback that just sets ioe->clean instead of having special
		 * handling here.  Oh well.
		 */
		ioe->clean = 0;
		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d], did pass-through", fd);
	}
}

/* 
 * These are the functions that get called above in "ioe->io_callback".
 * They are expected to "handle" the fd and dgets_buffer() whatever 
 * is appropriate for the application.
 */

static int	unix_read (int fd, int quiet, int revents)
{
	ssize_t	c;
	char	buffer[8192];

	c = read(fd, buffer, sizeof buffer);
	if (c == 0)
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_read: EOF for fd %d ", fd);
		return 0;
	}
	else if (c < 0)
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_read: read(%d) failed: %s", 
				fd, strerror(errno));
		return -1;
	}

	if (dgets_buffer(fd, buffer, c))
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_read: dgets_buffer(%d, %*s) failed",
				fd, (int)c, buffer);
		return -1;
	}

	return c;
}

static int	unix_recv (int fd, int quiet, int revents)
{
	ssize_t	c;
	char	buffer[8192];

	c = recv(fd, buffer, sizeof buffer, 0);
	if (c == 0)
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_recv: EOF for fd %d ", fd);
		return 0;
	}
	else if (c < 0)
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_recv: read(%d) failed: %s", 
				fd, strerror(errno));
		return -1;
	}

	if (dgets_buffer(fd, buffer, c))
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_recv: dgets_buffer(%d, %*s) failed",
				fd, (int)c, buffer);
		return -1;
	}

	return c;
}

static int	unix_accept (int fd, int quiet, int revents)
{
	int	newfd;
	SSu	addr;
	socklen_t len;

	memset(&addr.ss, 0, sizeof(addr.ss));
	len = sizeof(addr.ss);
	if ((newfd = Accept(fd, &addr, &len)) < 0)
	{
	    if (!quiet)
		syserr(SRV(fd), 
			"unix_accept: Accept(%d) failed: %s", fd, strerror(errno));
	}

	dgets_buffer(fd, &newfd, sizeof(newfd));
	dgets_buffer(fd, &addr.ss, sizeof(addr.ss));
	return sizeof(newfd) + sizeof(addr);
}

static int	unix_connect (int fd, int quiet, int revents)
{
	int	sockerr;
	int	gso_result;
	int	gsn_result;
	SSu	localaddr;
	int	gpn_result;
	SSu	remoteaddr;
	socklen_t len;

	/* * */
	len = sizeof(sockerr);
	errno = 0;
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &len);
	gso_result = errno;

	dgets_buffer(fd, &gso_result, sizeof(gso_result));
	dgets_buffer(fd, &sockerr, sizeof(sockerr));

	/* * */
	len = sizeof(localaddr.ss);
	errno = 0;
	getsockname(fd, &localaddr.sa, &len);
	gsn_result = errno;

	dgets_buffer(fd, &gsn_result, sizeof(gsn_result));
	dgets_buffer(fd, &localaddr.ss, sizeof(localaddr.ss));

	/* * */
	len = sizeof(remoteaddr.ss);
	errno = 0;
	getpeername(fd, &remoteaddr.sa, &len);
	gpn_result = errno;

	dgets_buffer(fd, &gpn_result, sizeof(gpn_result));
	dgets_buffer(fd, &remoteaddr.ss, sizeof(localaddr));

	return (sizeof(int) + sizeof(localaddr.ss)) * 2;
}

static int	passthrough_event (int fd, int quiet, int revents)
{
	char	revents_str[1024];

	/* Tell the caller what the revents are */
	snprintf(revents_str, 1024, "%d\n", revents);
	dgets_buffer(fd, revents_str, strlen(revents_str));
	return 1;
}

/*
 * dgets_buffer: Cycle 1 -- Buffer some data from a file descriptor
 *
 * Arguments:
 *	fd	- A file descriptor that was ready, and generated data
 *	data	- The data from the file descriptor
 *	len	- The number of bytes in 'data'.
 *
 * Return value:
 *	-1 	- I call shenanigans!  The fd is to be aborted.
 *	 0	- The data was buffered
 *
 * Notes:
 *	- Calling this function with len == 0 is a no-op.
 *	- Calling this function with len > 0 marks the fd as "not clean".
 *	  This will later cause the fd's Cycle 2 callback to be called
 *	  so they can consume this data.
 *	- Writing more than MAX_SEGMENTS segments without cycle 2 being
 *	  interested in what you have to say is an error.
 */
int	dgets_buffer (int fd, const void *data, ssize_t len)
{
	MyIO *	ioe;

	if (len < 0)
		return 0;			/* XXX ? */

	if (!(ioe = io_rec[fd]))
		panic(1, "dgets called on unsetup fd %d", fd);

	/* 
	 * An old exploit just sends us characters every .8 seconds without
	 * ever sending a newline.  Cut off anyone who tries that.
	 */
	if (ioe->segments > MAX_SEGMENTS)
	{
		if (!ioe->quiet)
		    syserr(ioe->server, 
			"dgets_buffer: Too many read()s on fd [%d] "
			"without a newline -- shutting off bad peer", fd);
		ioe->error = -1;
		ioe->clean = 0;
		return -1;
	}
	/* If the buffer completely empties, then clean it.  */
	else if (ioe->read_pos == ioe->write_pos)
	{
		ioe->read_pos = ioe->write_pos = 0;
		ioe->buffer[0] = 0;
		ioe->segments = 0;
	}
	/*
	 * If read_pos is non-zero, then some of the data was consumed,
	 * but not all of it (or it would be caught above), so we have
	 * an incomplete line of data in the buffer.  Move it to the 
	 * start of the buffer.
	 */
	else if (ioe->read_pos)
	{
		size_t	mlen;

		mlen = ioe->write_pos - ioe->read_pos;
		memmove(ioe->buffer, ioe->buffer + ioe->read_pos, mlen);
		ioe->read_pos = 0;
		ioe->write_pos = mlen;
		ioe->buffer[mlen] = 0;
		ioe->segments = 1;
	}

	if ((ssize_t)ioe->buffer_size - (ssize_t)ioe->write_pos < len)
	{
		while ((ssize_t)ioe->buffer_size - (ssize_t)ioe->write_pos < len)
			ioe->buffer_size += IO_BUFFER_SIZE;
		RESIZE(ioe->buffer, char, ioe->buffer_size);
	}

	memcpy((ioe->buffer) + (ioe->write_pos), data, len);
	ioe->write_pos += len;
	ioe->clean = 0;
	ioe->segments++;
	return 0;
}




/*************************************************************************/
/*                             CYCLE 2                                   */
/*************************************************************************/
/*
 * do_filedesc -- Call application callbacks for dirty fd's.
 *
 * Notes:
 *	An fd is "dirty" if cycle 1 buffered data for it (with dgets_buffer())
 *	We call the application callback for that fd, which is expected to
 *	call dgets() until dgets() marks the fd as clean.
 *	If the callback does not clean the buffer, this will busy-loop!
 *	(Perhaps there should be a failsafe for that...)
 */
void	do_filedesc (void)
{
	int	fd;

	for (fd = 0; fd <= global_max_fd; fd++)
	{
		/* Then tell the user they have data ready for them. */
		while (io_rec[fd] && !io_rec[fd]->clean)
			io_rec[fd]->callback(fd);
	}
}

/*
 * dgets - Cycle 2 - Return the next logical chonk of data to the application
 *
 * Arguments:
 * 	fd    	- A "dirty" newio file descriptor.  You know that a newio file 
 *	          descriptor is dirty when your new_open() callback is called.
 * 	buf    	- A buffer into which to copy the data from the file descriptor
 * 	buflen 	- The size of 'buf'.
 * 	buffer 	- The type of buffering to perform.
 *		-2	Fully buffered.  
 *			   - Give me buflen bytes if you have it.
 *			   - Don't give me anything if you don't have buflen bytes.
 *		-1	Fully unbuffered.  
 *			   - Give me whatever you have.
 *		 0	Partial line buffering.  
 *			   - Give me a line of data if you have one.
 *			   - Give me everything if it's not a complete line.
 *		 1	Full line buffering.  
 *			   - Give me a line of data if you have one.
 *			   - Don't give me anything if you don't have a complete line
 *
 * Return values:
 *	buffer == -2		(The results are NOT null terminated)
 *		-1	The file descriptor is dead
 *		 0	There is not "buflen" bytes available to be read
 *		>0	The number of bytes returned.
 *	buffer == -1		(The results are NOT null terminated)
 *		-1	The file descriptor is dead
 *		 0	There is no data available to read.
 *		>0	The number of bytes returned.
 *	buffer == 0		(The results are null terminated)
 *		-1	The file descriptor is dead
 *		 0	Some data was returned, but it was an incomplete line.
 *		>0	A full line of data was returned.
 *	buffer == 1		(The results are null terminated)
 *		-1	The file descriptor is dead
 *		 0	No data was returned.
 *		>0	A full line of data was returned.
 *
 * Notes:
 * 	This function should only be called from the function we called above
 *	in do_filedesc()  [io_rec[fd]->callback(fd)]
 */
ssize_t	dgets (int fd, char *buf, size_t buflen, int buffer)
{
	size_t	cnt = 0;
	size_t	consumed = 0;
	char	h = 0;
	MyIO *	ioe;

	if (buflen == 0)
	{
	    syserr(SRV(fd),
			"dgets: Destination buffer for fd [%d] is zero length."
			" This is surely a bug.", fd);
	    return -1;
	}

	if (!(ioe = io_rec[fd]))
		panic(1, "dgets called on unsetup fd %d", fd);

	if (ioe->error)
	{
		if (!ioe->quiet)
		    syserr(SRV(fd), "dgets: fd [%d] must be closed", fd);
		return -1;
	}


	/*
	 * So the buffer probably has changed now, because we just read
	 * in more data.  Check again to see if there is a newline.  If
	 * there is not, and the caller wants a complete line, just punt.
	 */
	if (buffer == 1 && !memchr(ioe->buffer + ioe->read_pos, '\n', 
					ioe->write_pos - ioe->read_pos))
	{
		ioe->clean = 1;
		return 0;
	}

	/*
	 * So if the caller wants 'buflen' bytes, and we don't have it,
	 * then mark the buffer clean and wait for more.
	 */
	if (buffer == -2 && ioe->write_pos - ioe->read_pos < buflen)
	{
		if (x_debug & DEBUG_NEWIO)
			yell("dgets: Wanted %ld bytes, have %ld bytes", 
				(long)(ioe->write_pos - ioe->read_pos), (long)buflen);
		ioe->clean = 1;
		return 0;
	}

	/*
	 * AT THIS POINT WE'VE COMMITED TO RETURNING WHATEVER WE HAVE.
	 */

	consumed = 0;
	while (ioe->read_pos < ioe->write_pos)
	{
	    h = ioe->buffer[ioe->read_pos++];
	    consumed++;

	    /* Only copy the data if there is some place to put it. */
	    if (cnt <= buflen - 1)
		buf[cnt++] = h;

	    /* 
	     * For buffered data, don't stop until we see the newline. 
	     * For unbuffered data, stop if we run out of space.
	     */
	    if (buffer >= 0)
	    {
		if (h == '\n')
		    break;
	    }
	    else
	    {
		if (cnt == buflen)
		    break;
	    }
	}

	if (ioe->read_pos == ioe->write_pos)
	{
		ioe->read_pos = ioe->write_pos = 0;
		ioe->clean = 1;
	}

	/* Remember, you can't use 'ioe' after this point! */
	ioe = NULL;	/* XXX Don't try to cheat! XXX */

	/* 
	 * Before anyone whines about this, a lot of code in epic 
	 * silently assumes that incoming lines from the server don't
	 * exceed 510 bytes.  Until we "fix" all of those cases, it is
	 * better to truncate excessively long lines than to let them
	 * overflow buffers!
	 */
	if (cnt < consumed)
	{
		if (x_debug & DEBUG_INBOUND) 
			yell("FD [%d], Truncated (did [%ld], max [%ld])", 
					fd, (long)consumed, (long)cnt);

		/* If the line had a newline, then put the newline in. */
		if (buffer >= 0 && h == '\n')
		{
			cnt = buflen - 2;
			buf[cnt++] = '\n';
		}
	}

	/*
	 * Terminate it
	 */
	if (buffer >= 0)
		buf[cnt] = 0;

	/*
	 * If we end in a newline, then all is well.
	 * Otherwise, we're unbuffered, tell the caller.
	 * The caller then would need to do a strlen() to get
 	 * the amount of data.
	 */
	if (buffer < 0 || (cnt > 0 && buf[cnt - 1] == '\n'))
	    return cnt;
	else
	    return 0;
}



/***********************************************************************/
/*               NEWIO UTILITY FUNCTIONS                               */
/***********************************************************************/
void	init_newio (void)
{
	int	fd;
	int	max_fd = IO_ARRAYLEN;

	if (io_rec)
		panic(1, "init_newio() called twice.");

	io_rec = (MyIO **)new_malloc(sizeof(MyIO *) * max_fd);
	for (fd = 0; fd < max_fd; fd++)
		io_rec[fd] = NULL;
}

/*
 * new_open - Register an fd with the event looper for callbacks.
 *
 * Arguments:
 *	fd - A file descriptor that you got from somewhere [forgive the name]
 *	callback - A function that will be called when you have data ready
 *		int fd	- The fd you registered
 *		int revents - The events returned by poll(2)
 *	io_type - The OS-level handler you want performed on this fd
 *		You can use NEWIO_PASSTHROUGH if you just want to be notified
 *		and don't want any assistance.
 *	poll_events - What sort of notifications you want (POLLIN or POLLOUT)
 *	quiet - Whether you want the handling of this fd to be chatty (usually 0)
 *	server - The IRC server refnum associated with this fd (for output purposes)
 * 
 * When you register an fd with this function, it will:
 *	(1) Wait until it is "ready" at the io level [with poll]
 *	(2) If you specify an io_type, it will run a function to "handle" your fd.
 *	(3) The io handler will buffer (with dgets_buffer()) the data for the activity.
 *	    This will tell the looper to buffer data for your consumption
 *	(4) Your callback will be invoked, telling you which fd is ready and what
 *	    poll() said it was ready for.  Your callback must consume the buffered data.
 *	    If you use NEWIO_PASSTHROUGH, then dgets() will buffer dummy data
 *	    -- You still need to consume it!
 *
 * Return value:  'fd' is returned.
 */
int 	new_open (int fd, void (*callback) (int), int io_type, int poll_events, int quiet, int server)
{
	MyIO *ioe;

	if (fd < 0)
		return fd;		/* Invalid */

	if (x_debug & DEBUG_NEWIO)
		yell("new_open: fd = %d, io_type = %d, poll_events = %d, quiet = %d, server = %d",
			fd, io_type, poll_events, quiet, server);

	/*
	 * Keep track of the highest fd in use.
	 */
	if (fd > global_max_fd)
		global_max_fd = fd;

	if (!(ioe = io_rec[fd]))
	{
		ioe = io_rec[fd] = (MyIO *)new_malloc(sizeof(MyIO));
		ioe->buffer_size = IO_BUFFER_SIZE;
		ioe->buffer = (char *)new_malloc(ioe->buffer_size + 2);
	}

	ioe->fd = fd;
	ioe->read_pos = ioe->write_pos = 0;
	ioe->segments = 0;
	ioe->error = 0;
	ioe->clean = 1;
	ioe->quiet = quiet;
	ioe->server = server;

	if (io_type == NEWIO_READ) {
		ioe->io_callback = unix_read;
		ioe->poll_events = POLLIN;
	} else if (io_type == NEWIO_ACCEPT) {
		ioe->io_callback = unix_accept;
		ioe->poll_events = POLLIN;
	} else if (io_type == NEWIO_SSL_READ) {
		ioe->io_callback = ssl_read;
		ioe->poll_events = POLLIN;
	} else if (io_type == NEWIO_CONNECT) {
		ioe->io_callback = unix_connect;
		ioe->poll_events = POLLOUT;
	} else if (io_type == NEWIO_RECV) {
		ioe->io_callback = unix_recv;
		ioe->poll_events = POLLIN;
	} else if (io_type == NEWIO_NULL) {
		ioe->io_callback = NULL;
		ioe->poll_events = 0;
	} else if (io_type == NEWIO_SSL_CONNECT) {
		ioe->io_callback = ssl_connect;
		ioe->poll_events = POLLIN;
	} else if (io_type == NEWIO_PASSTHROUGH_READ) {
		ioe->io_callback = passthrough_event;
		ioe->poll_events = POLLIN;
	} else if (io_type == NEWIO_PASSTHROUGH_WRITE) {
		ioe->io_callback = passthrough_event;
		ioe->poll_events = POLLOUT;
	} else if (io_type == NEWIO_PASSTHROUGH) {
		ioe->io_callback = passthrough_event;
		ioe->poll_events = poll_events;
	} else {
		panic(1, "New_open doesn't recognize io type %d", io_type);
	}

	ioe->callback = callback;
	ioe->failure_callback = NULL;

	ioe->poll.fd = fd;
	ioe->poll.events = ioe->poll_events;

	return fd;
}

/*
 * On a FD registered with new_open(), you may want a callback when the
 * fd has gone bad, so you can do your own cleanup.
 * This is intended for the Python support.
 * 
 * The callback provides two arguments:
 *	1 - fd - the fd passed to new_open()
 *	2 - error - this is reserved for future expansion
 */
int 	new_open_failure_callback (int fd, void (*failure_callback) (int, int))
{
	MyIO *	ioe;

	if (fd >= 0 && fd <= global_max_fd && (ioe = io_rec[fd]))
	{
		ioe->failure_callback = failure_callback;
		return 0;
	}

	syserr(-1, "new_open_failure_callback: Called for fd %d that is not set up", fd);
	return -1;		/* Oh well. */
}

/*
 * Unregister a filedesc for readable events 
 * and close it down and free its input buffer
 */
int	new_close_with_option (int fd, int virtual)
{
	MyIO *	ioe;

	if (fd < 0)
		return -1;		/* Oh well. */

	if (fd >= 0 && fd <= global_max_fd && (ioe = io_rec[fd]))
	{
		if (x_debug & DEBUG_NEWIO)
			yell("new_close: fd = %d", fd);

		if (ioe->io_callback == ssl_read)	/* XXX */
			ssl_shutdown(ioe->fd);

		ioe->poll.events = 0;

		/* 
		 * If virtual == 1, then the caller is managing the 
		 * fd and must close(2) it themselves.
		 * (ie, fd's from external languages)
		 */
		if (virtual == 0)
			unix_close(ioe->fd, ioe->quiet);

		new_free(&ioe->buffer); 
		new_free((char **)&(io_rec[fd]));

		/*
		 * If we're closing the highest fd in use, then we
		 * want to adjust global_max_fd downward to the next 
		 * highest fd.
		 */
		if (fd >= global_max_fd)
		{
			/* Just in case global_max_fd got lost */
			global_max_fd = fd;
			while (global_max_fd >= 0 && !io_rec[global_max_fd])
				global_max_fd--;
		}
	}
	else if (virtual == 0)
		unix_close(fd, 0);

	return -1;
}

/* 
 * The lower level IO functions call us when an fd is found dead,
 * and has been untracked (FD_CLR) and unregistered (new_close()), so we
 * may tell any owner of this.
 */
static	void	fd_is_invalid (int fd)
{
	MyIO *	ioe;

	if (fd >= 0 && fd <= global_max_fd && (ioe = io_rec[fd]))
	{
		if (ioe->failure_callback)
			ioe->failure_callback(fd, 0);
	}
	else
	{
		syserr(-1, "fd_is_invalid called on unsetup fd %d", fd);
		return;
	}
}

/* This is the guts of the SRV() macro */
int	get_server_by_fd	(int fd)
{
	if (!io_rec[fd])
		panic(1, "get_server_by_fd(%d): fd is not set up!", fd);
	return io_rec[fd]->server;
}

static int	unix_close (int fd, int quiet)
{
	if (close(fd))
	{
		if (!quiet)
		   syserr(SRV(fd), "unix_close: close(%d) failed: %s", 
			fd, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * my_sleep - Block, waiting for a timeout
 *
 * Arguments:
 *	timeout	- How many seconds to wait before returning
 *
 * Return value:
 *	< 0	- Something went wrong with poll()
 *	  1	- The fd is readable.
 *
 * Notes:
 *	This is a blocking function, so do not use it in any situation
 *	where blocking might be noticible by the user; or wherever the
 *	user has asked you to block (ie, /sleep)
 */
int	my_sleep (double timeout)
{
	struct pollfd	pfd;
	int		e;

	pfd.fd = -1;
	pfd.events = pfd.revents = 0;
	e = poll(&pfd, 1, timeout * 1000 + 1);

	if (e < 0)
		return e;
	return 1;
}

/*
 * my_isreadable - Block, waiting for an fd to be readable
 *
 * Arguments:
 *	fd	- A file descriptor that is to become readable
 *	timeout	- How many seconds to wait for it to become readable
 *
 * Return value:
 *	< 0	- Something went wrong with poll()
 *	  0	- The fd is not ready and the timeout occurred
 *	  1	- The fd is readable.
 *
 * Notes:
 *	This is a blocking function, so do not use it in any situation
 *	where blocking might be noticible by the user.
 */
int	my_isreadable (int fd, double timeout)
{
	struct pollfd	pfd;
	int		e;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	e = poll(&pfd, 1, (long)(timeout * 1000) + 1);

	if (e < 0)
		return e;
	if (e > 0 && (pfd.revents & POLLIN))
		return 1;
	return 0;		/* Hrm? */
}


