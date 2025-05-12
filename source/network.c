/*
 * network.c -- handles stuff dealing with connecting and name resolving
 *
 * Copyright 1995, 2007 Jeremy Nelson
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
#include "vars.h"
#include "newio.h"
#include "output.h"
#include "cJSON.h"

static socklen_t sasocklen (const struct sockaddr *sockaddr);
static int	safamily (const struct sockaddr *sa);
static	int	Socket (int domain, int type, int protocol);
static	int	Connect (int fd, SSu *addr);
static  int     Getaddrinfo (const char *nodename, const char *servname, AI *hints, AI **res);
static  void    Freeaddrinfo (AI *ai);
static	int	Getnameinfo (SSu *ssu, socklen_t ssulen, char *host, size_t hostlen, char *serv, size_t servlen, int flags);
static  void    do_ares_callback (int vfd);
static  void    ares_sock_state_cb_ (void *data, ares_socket_t socket_fd, int readable, int writable);
static	int	paddr_to_ssu (const char *host, SSu *storage_, int flags);
static	void	ares_addrinfo_callback_ (void *arg, int status, int timeouts, struct ares_addrinfo *result);
static	void	ares_nameinfo_callback_ (void *arg, int status, int timeouts, char *host, char *port);
	void	my_addrinfo_json_callback (void *arg, int status, int timeouts, struct ares_addrinfo *result);
	int	json_to_sockaddr_array (const char *json_string, int *error_code, SSu **addr_array) ;

/****************************************************************************/
int	set_non_blocking (int fd)
{
	int	rval;

	if ((rval = fcntl(fd, F_GETFL, 0)) == -1)
	{
		syserr(-1, "set_non_blocking: fcntl(%d, F_GETFL) failed: %s",
				fd, strerror(errno));
		return -1;
	}
	rval |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, rval) == -1)
	{
		syserr(-1, "set_non_blocking: fcntl(%d, F_SETFL) failed: %s",
				fd, strerror(errno));
		return -1;
	}
	return 0;
}

int	set_blocking (int fd)
{
	int	rval;

	if ((rval = fcntl(fd, F_GETFL, 0)) == -1)
	{
		syserr(-1, "set_blocking: fcntl(%d, F_GETFL) failed: %s",
				fd, strerror(errno));
		return -1;
	}
	rval &= (~(O_NONBLOCK));
	if (fcntl(fd, F_SETFL, rval) == -1)
	{
		syserr(-1, "set_blocking: fcntl(%d, F_SETFL) failed: %s",
				fd, strerror(errno));
		return -1;
	}
	return 0;
}

socklen_t	socklen (const SSu *sockaddr)
{
	if (family(sockaddr) == AF_INET)
		return sizeof(sockaddr->si);
	else if (family(sockaddr) == AF_INET6)
		return sizeof(sockaddr->si6);
	else
		return 0;
}

static socklen_t	sasocklen (const struct sockaddr *sockaddr_)
{
	if (safamily(sockaddr_) == AF_INET)
		return sizeof(struct sockaddr_in);
	else if (safamily(sockaddr_) == AF_INET6)
		return sizeof(struct sockaddr_in6);
	else
		return 0;
}

int	family (const SSu *sockaddr_)
{
	return (sockaddr_->sa).sa_family;
}

static int	safamily (const struct sockaddr *sa)
{
	return sa->sa_family;
}

static const char *	familystr (int family)
{
	if (family == AF_INET)
		return "IPv4";
	else if (family == AF_INET6)
		return "IPv6";
	else
		return "<family not supported>";
}

/****************************************************************************/
/*
 * Socket -- Create a new socket and set baseline preferences
 * 
 * Arguments:
 *	domain	- The domain passed to socket(2), usually AF_INET
 *	type	- The type passed to socket(2), usually SOCK_STREAM
 *	protocol - The protocol passed to socket(2), usually 0
 *
 * Return value:
 *	The fd of a new socket
 *
 * Notes:
 *	All sockets come with:
 *	- lingering off (close(2) won't block if jammed)
 *	- reuseaddr on (let someone re-use our port after we close(2))
 *	- keepalive on (fail the socket if TCP ping/pongs fail)
 */
static int	Socket (int domain, int type, int protocol)
{
        int     opt;
        int     optlen = sizeof(opt);
	int	s;

	if ((s = socket(domain, type, protocol)) < 0)
	{
		syserr(-1, "Socket: socket(%d,%d,%d) failed: %s",
				domain, type, protocol, strerror(errno));
		return -1;
	}

	{
		struct linger   lin;

		/* Turning of "lingering" ordinarily makes close(2) non-blocking if the socket is jammed */
		lin.l_onoff = lin.l_linger = 0;
		setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lin, optlen);
	}

	opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, optlen);
        opt = 1;
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&opt, optlen);
	return s;
}

/*
 * It is possible for a race condition to exist; such that poll()
 * indicates that a listen()ing socket is able to recieve a new connection
 * and that a later accept() call will still block because the connection
 * has been closed in the interim.  This wrapper for accept() attempts to
 * defeat this by making the accept() call nonblocking.
 */
int	Accept (int s, SSu *addr, socklen_t *addrlen)
{
	int	retval;

	set_non_blocking(s);
	if ((retval = accept(s, &addr->sa, addrlen)) < 0)
		syserr(-1, "Accept: accept(%d) failed: %s", 
				s, strerror(errno));
	set_blocking(s);
	set_blocking(retval);		/* Just in case */
	return retval;
}

static int Connect (int fd, SSu *addr)
{
	int	retval;

	set_non_blocking(fd);
	if ((retval = connect(fd, &addr->sa, socklen(addr))))
	{
	    if (errno != EINPROGRESS)
		syserr(-1, "Connect: connect(%d) failed: %s", fd, strerror(errno));
	    else
		retval = 0;
	}
	set_blocking(fd);
	return retval;
}

/* * * * * */
/*
 * This may ONLY be used to convert p-addrs to an SSU, which is nonblocking.
 */
static	int	Getaddrinfo (const char *nodename, const char *servname, AI *hints, AI **res)
{
	hints->ai_flags |= AI_NUMERICHOST;
	return getaddrinfo(nodename, servname, hints, res);
}

static	void	Freeaddrinfo (AI *ai)
{
	freeaddrinfo(ai);
}

/*
 * This may ONLY be used to convert SSUs to p-addrs, which is nonblocking.
 */
static int	Getnameinfo (SSu *ssu, socklen_t ssulen, char *host, size_t hostlen, char *serv, size_t servlen, int flags)
{
	return getnameinfo(&ssu->sa, ssulen, host, hostlen, serv, servlen, flags | NI_NUMERICHOST | NI_NUMERICSERV);
}


/*****************************************************************************/
/*
   Retval    Meaning
   --------  -------------------------------------------
   -1        System call error occured (check errno)
   -2        The operation isn't supported for the requested family.
   -3        The hostname has an address, but not in the family you want.
   -4        The "address presentation" hostname can't be converted.
   -5        The hostname does not resolve.
   -6        The requested family does not have a virtual host.
   -7        The remote sockaddr to connect to was not provided.
   -8        The local sockaddr and remote sockaddr are different families.
   -9        The timeout expired before the connection was established.
   -10       The requested local address is in use or not available.
   -11       The connect()ion failed (at connect() time)
   -12       The connection could not be established (after connect() time)
   -13       The local sockaddr to bind was not provided.
   -14       The family request does not make sense.
*/

/*
 * NAME: network_client
 * USAGE: Create a new socket and establish both endpoints with the 
 *        arguments given.
 * ARGS: l - A local sockaddr structure representing the local side of
 *           the connection.  NULL is permitted.  If this value is NULL,
 *           then the local side of the connection will not be bind()ed.
 *       ll - The sizeof(l) -- if 0, then 'l' is treated as a NULL value.
 *       r - A remote sockaddr structure representing the remote side of
 *           the connection.  NULL is not permitted.
 *       rl - The sizeof(r) -- if 0, then 'r' is treated as a NULL value.
 *            Therefore, 0 is not permitted.
 * RETURN VALUE:
 *	An fd for which a nonblocking connect is underway and ready to go!
 *	Just pass it to new_open(fd, ..., NEWIO_CONNECT, ...);
 */
int	network_client (SSu *l, socklen_t ll, SSu *r, socklen_t rl)
{
	int	fd = -1;
	int	family_ = AF_UNSPEC;

	if (ll == 0)
		l = NULL;
	if (rl == 0)
		r = NULL;

	if (!r)
	{
		syserr(-1, "network_client: remote addr missing (connect to who?)");
		return -1;
	}

	if (l && r && family(l) != family(r))
	{
		syserr(-1, "network_client: local addr protocol (%d) is different "
			"from remote addr protocol (%d)", 
			family(l), family(r));
		return -1;
	}

	if (l)
		family_ = family(l);
	if (r)
		family_ = family(r);

	if ((fd = Socket(family_, SOCK_STREAM, 0)) < 0)
	{
		syserr(-1, "network_client: socket(%d) failed: %s", family_, strerror(errno));
		return -1;
	}

	if (l && bind(fd, &l->sa, ll))
	{
	    syserr(-1, "network_client: bind(%d) failed: %s", fd, strerror(errno));
	    close(fd);
	    return -1;
	}

	if (Connect(fd, r))
	{
	    syserr(-1, "network_client: connect(%d) failed: %s", fd, strerror(errno));
	    close(fd);
	    return -1;
	}

	if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Connect begun on des [%d]", fd);
	return fd;
}

/*
 * NAME: network_server
 * USAGE: Create a network server with the address provided
 * ARGS: local - INPUT - A local sockaddr structure representing the local side of
 *                       the connection.  NULL is not permitted.
 *			 If you don't care what port to listen on, port = 0;
 *		 OUTPUT - The sockaddr of the server (which will include the port)
 *       local_len - The sizeof(local) -- if 0, then 'local' is treated as a NULL 
 *		     value.  Therefore, 0 is not permitted.
 * RETURN VALUE:
 *	A fd for a listening server that is live and ready for action.
 *	Just pass it to new_open(fd, ..., NEWIO_ACCEPT, ...).
 */
int	network_server (SSu *local, socklen_t local_len)
{
	int	fd = -1;
	int	family_ = AF_UNSPEC;

	if (local_len == 0)
		local = NULL;
	if (!local)
	{
		syserr(-1, "client_bind: address to bind to not provided");
		return -1;
	}

	family_ = family(local);

	if ((fd = Socket(family_, SOCK_STREAM, 0)) < 0)
	{
		syserr(-1, "client_bind: socket(%d) failed: %s", family_, strerror(errno));
		return -1;
	}

	if (bind(fd, &local->sa, local_len))
	{
		syserr(-1, "client_bind: bind(%d) failed: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	/*
	 * Get the local sockaddr of the passive socket,
	 * specifically the port number, and stash it in
	 * 'port' which is the return value to the caller.
	 */	
	if (getsockname(fd, &local->sa, &local_len))
	{
		syserr(-1, "client_bind: getsockname(%d) failed: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	if (listen(fd, 4) < 0)
	{
		syserr(-1, "client_bind: listen(%d,4) failed: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

/******************************************************************************************/
static	ares_channel_t *	ares_channel_;
static	struct ares_options	ares_options_;
static	int			ares_optmask_;

static	void	do_ares_callback (int vfd)
{
	char	datastr[128];
	int	revents, readable = 0, writable = 0;;

	if ((dgets(vfd, datastr, sizeof(datastr), 1)) <= 0)
	{
		yell("I closed ares_callback fd %d.", vfd);
		new_close(vfd);
		return;
	}

	revents = atol(datastr);

	if (revents & POLLIN)
		readable = vfd;
	if (revents & POLLOUT)
		writable = vfd;
	yell("ares_process_fd: vfd=%d, readable=%d, writable=%d", vfd, readable, writable);
	ares_process_fd(ares_channel_, readable, writable);
}

static	void	ares_sock_state_cb_ (void *data, ares_socket_t socket_fd, int readable, int writable)
{
	int	revents = 0;

	if (readable)
		revents |= POLLIN;
	if (writable)
		revents |= POLLOUT;

	new_open(socket_fd, do_ares_callback, NEWIO_PASSTHROUGH, revents, 0, -2);
}

void	init_ares (void)
{
	int	retval;

	memset(&ares_options_, 0, sizeof(ares_options_));
	ares_options_.sock_state_cb = ares_sock_state_cb_;
	ares_options_.sock_state_cb_data = NULL;

	ares_optmask_ = ARES_OPT_SOCK_STATE_CB;

	retval = ares_init_options(&ares_channel_, &ares_options_, ares_optmask_);
	if (retval == ARES_SUCCESS)
		return;
	else if (retval == ARES_EFILE)
	{
		panic(1, "init_ares failed with ARES_EFILE");
		/* */ return;
	}
	else if (retval == ARES_ENOMEM)
	{
		panic(1, "init_ares failed with ARES_ENOMEM");
		/* */ return;
	}
	else if (retval == ARES_ENOTINITIALIZED)
	{
		panic(1, "init_ares failed with ARES_ENOTINITIALIZED");
		/* */ return;
	}
	else if (retval == ARES_ENOSERVER)
	{
		panic(1, "init_ares failed with ARES_ENOSERVER");
		/* */ return;
	}
}


/*****************************************************************************
 *		IN THIS PLACE ARE THINGS THAT DO NOT BLOCK.
 *****************************************************************************/

/*
 * NAME: my_inet_pton
 * USAGE: Like inet_pton(), but uses getaddrinfo() so we don't have to muck
 *	  around inside of sockaddrs.
 * ARGS: hostname - The address to convert.  It may be any of the following:
 *		IPv4 "Presentation Address"	(A.B.C.D)
 *		IPv6 "Presentation Address"	(A:B::C:D)
 *	 storage - A pointer to a (union SSU_) (aka, a (struct sockaddr_storage)) with the 
 *		"family" argument filled in (AF_INET or AF_INET6).  
 *		If "hostname" is a p-addr, then the form of the p-addr
 *		must agree with the family in 'storage'.
 */
static int	paddr_to_ssu (const char *host, SSu *storage_, int flags)
{
	AI	hints;
	AI *	results;
	int	retval;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = flags | AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	if ((retval = Getaddrinfo(host, NULL, &hints, &results))) 
	{
		syserr(-1, "paddr_to_ssu: Getaddrinfo(%s) failed: %s", host, gai_strerror(retval));
		return -1;
	}

	/* memcpy can bite me. */
	memcpy(&(storage_->ss), results->ai_addr, results->ai_addrlen);

	Freeaddrinfo(results);
	return 0;
}

/*
 * NAME: ssu_to_paddr
 * USAGE: Like inet_ntop(), but uses getnameinfo() so we don't have to much
 *	  around inside of sockaddrs.
 * PLAIN ENGLISH: Convert getpeername() into "1.2.3.4"
 * ARGS: name - The socket address, possibly returned by getpeername().
 *       retval - A string to store the hostname/paddr (RETURN VALUE)
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: 0 for success, -1 for error
 *
 * NOTES: 'flags' should be set to NI_NAMEREQD if you don't want the remote
 *        host's p-addr if it does not have a DNS hostname.
 */
int	ssu_to_paddr (SSu *name_, char *host, int hsize, char *port, int psize, int flags)
{
	int		retval;
	socklen_t 	len;

	len = socklen(name_);
	if ((retval = Getnameinfo(name_, len, host, hsize, port, psize, flags))) 
	{
		syserr(-1, "ssu_to_paddr: Getnameinfo(sockaddr->p_addr) failed: %s", 
					gai_strerror(retval));
		return -1;
	}

	return 0;
}

char *	ssu_to_paddr_quick (SSu *name_)
{
	int		retval;
	socklen_t 	len;
	char		host[256];
	char		port[256];

	len = socklen(name_);
	if ((retval = Getnameinfo(name_, len, host, sizeof(host), port, sizeof(port), 0)))
	{
		syserr(-1, "ssu_to_paddr_quick: Getnameinfo(sockaddr->p_addr) failed: %s", 
					gai_strerror(retval));
		return malloc_strdup("invalid p-addr");
	}

	return malloc_strdup(host);
}

int	ssu_to_port_quick (SSu *name_)
{
	int		retval;
	socklen_t 	len;
	char		host[256];
	char		port[256];

	len = socklen(name_);
	if ((retval = Getnameinfo(name_, len, host, sizeof(host), port, sizeof(port), 0)))
	{
		syserr(-1, "ssu_to_paddr_quick: Getnameinfo(sockaddr->p_addr) failed: %s", 
					gai_strerror(retval));
		return -1;
	}

	return atol(port);
}

/*
 * XXX I lament that this is necessary. :( 
 * Perhaps someday it won't be.
 */
static	char * sa_to_paddr_quick (struct sockaddr *name_)
{
	char	addrbuf[128];

	if (name_->sa_family == AF_INET)
		inet_ntop(name_->sa_family, &((struct sockaddr_in *)name_)->sin_addr, addrbuf, sizeof(addrbuf));
	else if (name_->sa_family == AF_INET6)
		inet_ntop(name_->sa_family, &((struct sockaddr_in6 *)name_)->sin6_addr, addrbuf, sizeof(addrbuf));
	else
		*addrbuf = 0;

	return malloc_strdup(addrbuf);
}


/*****************************************************************************
 *		IN THIS PLACE ARE THINGS THAT BLOCK FOR YOU.
 *****************************************************************************/
struct	hostname_to_ssu_data {
	int			fd;

	int			status;
	int			timeouts;
	struct ares_addrinfo *	result;
	int			done;
};

/* * * * * * */
static void	ares_addrinfo_callback_ (void *arg, int status, int timeouts, struct ares_addrinfo *result)
{
	struct hostname_to_ssu_data	*data;

	if (!arg)
		return;
	data = (struct hostname_to_ssu_data *)arg;
	data->status = status;
	data->timeouts = timeouts;
	data->result = result;
	data->done = 1;
}

/*
 */
int	hostname_to_ssu (int fd, int family, const char *host, const char *port, SSu *ssu, int flags)
{
	struct hostname_to_ssu_data	data;
	struct ares_addrinfo_hints 	hints;

	memset(&data, 0, sizeof(data));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = flags | ARES_AI_NUMERICSERV | ARES_AI_NOSORT | ARES_AI_ENVHOSTS;

	if (fd >= 0)
		data.fd = fd;
	ares_getaddrinfo(ares_channel_, host, port, &hints, ares_addrinfo_callback_, &data);
	if (fd < 0)
	{
		while (!data.done)
			io("hostname_to_ssu");

		if (data.status != ARES_SUCCESS) {
			yell("ares_getaddrinfo(%s,%s) failed: %d", host, port, data.status);
			return -1;
		}

		memcpy(ssu, data.result->nodes->ai_addr, data.result->nodes->ai_addrlen);
		ares_freeaddrinfo(data.result);
		return 0;
	}
	return 0;
}


/* * * * * * * * * * */
int	hostname_to_json (int fd, int family, const char *host, const char *port, int flags)
{
	struct ares_addrinfo_hints 	hints;
	int	*fd_ = new_malloc(sizeof(int));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = flags | ARES_AI_NUMERICSERV | ARES_AI_NOSORT | ARES_AI_ENVHOSTS;

	*fd_ = fd;
	ares_getaddrinfo(ares_channel_, host, port, &hints, my_addrinfo_json_callback, fd_);
	return 0;
}
struct	ssu_to_hostname_data {
	SSu *	ssu;
	int	status;
	int	timeouts;
	char *	host;
	size_t	hostsize;
	char *	port;
	size_t	portsize;
	int	done;
};

/* * * * * * */
static void	ares_nameinfo_callback_ (void *arg, int status, int timeouts, char *host, char *port)
{
	struct ssu_to_hostname_data	*data;

	if (!arg)
		return;
	data = (struct ssu_to_hostname_data *)arg;
	data->status = status;
	data->timeouts = timeouts;
	if (data->host)
	{
		if (!host) {
			char *x = ssu_to_paddr_quick(data->ssu);
			strlcpy(data->host, x, data->hostsize);
			new_free(&x);
		}
		else
			strlcpy(data->host, host, data->hostsize);
	}
	if (data->port)
	{
		if (!port)
			*(data->port) = 0;
		else
			strlcpy(data->port, port, data->portsize);
	}
	data->done = 1;
}

int	ssu_to_hostname (SSu *ssu, char *host, size_t hostsize, char *port, size_t portsize, int flags)
{
	struct ssu_to_hostname_data	data;

	memset(&data, 0, sizeof(data));
	data.ssu = ssu;
	data.host = host;
	data.hostsize = hostsize;
	data.port = port;
	data.portsize = portsize;

	ares_getnameinfo(ares_channel_, &(ssu->sa), socklen(ssu), flags | ARES_NI_NAMEREQD | ARES_NI_LOOKUPHOST | ARES_NI_NUMERICSERV, ares_nameinfo_callback_, &data);

	while (!data.done)
		io("hostname_to_ssu");

	if (data.status != ARES_SUCCESS) {
		yell("ares_getnameinfo failed: %d", data.status);
		return -1;
	}
	return 0;
}


/**********************************************************************/
/* Function backends */
/*
 * NAME: hostname_to_paddr (formerly inet_hntop)
 * USAGE: Convert a Hostname into a "presentation address" (p-addr)
 * PLAIN ENGLISH: Convert "A.B.C.D" into "foo.bar.com"
 * ARGS: family - The family whose presesentation address format to use
 *	 host - The hostname to convert
 *       retval - A string to store the p-addr (RETURN VALUE)
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 */
int	hostname_to_paddr (const char *host, char *retval, int size)
{
	SSu	buffer;

	buffer.sa.sa_family = AF_INET;
	if (hostname_to_ssu(-1, AF_INET, host, NULL, &buffer, AI_ADDRCONFIG))
	{
		syserr(-1, "hostname_to_paddr: hostname_to_ssu(%s) failed", host);
		return -1;
	}

	if (ssu_to_paddr(&buffer, retval, size, NULL, 0, NI_NUMERICHOST))
	{
		syserr(-1, "hostname_to_paddr: ssu_to_paddr(%s) failed", host);
		return -1;
	}

	return 0;
}

/*
 * NAME: inet_ptohn
 * USAGE: Convert a "presentation address" (p-addr) into a Hostname
 * PLAIN ENGLISH: Convert "foo.bar.com" into "A.B.C.D"
 * ARGS: family - The family whose presesentation address format to use
 *	 ip - The presentation-address to look up
 *       retval - A string to store the hostname (RETURN VALUE)
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 */
int	paddr_to_hostname (const char *ip, char *retval, int size)
{
	SSu	buffer;

	memset((char *)&buffer, 0, sizeof(buffer));
	if (paddr_to_ssu(ip, &buffer, 0))
	{
		syserr(-1, "inet_ptohn: paddr_to_ssu(%s) failed", ip);
		return -1;
	}

	if (ssu_to_hostname(&buffer, retval, size, NULL, 0, NI_NAMEREQD))
	{
		syserr(-1, "inet_ptohn: ssu_to_hostname(%s) failed", ip);
		return -1;
	}

	return 0;
}

/*
 * NAME: one_to_another
 * USAGE: Convert a p-addr to a Hostname, or a Hostname to a p-addr.
 * PLAIN ENGLISH: Convert "A.B.C.D" to "foo.bar.com" or convert "foo.bar.com"
 *                into "A.B.C.D"
 * ARGS: family - The address family in which to convert (AF_INET/AF_INET6)
 *	 what - Either a Hostname or a p-addr.
 *       retval - If "what" is a Hostname, a place to store the p-addr
 *                If "what" is a p-addr, a place to store the Hostname
 *       size - The length of 'retval' in bytes
 * RETURN VALUE: "retval" is returned upon success
 *		 "empty_string" is returned for any error.
 *
 * NOTES: If "what" is a p-addr and there is no hostname associated with that 
 *        address, that is considered an error and empty_string is returned.
 */
int	one_to_another (int family, const char *what, char *retval, int size)
{
	/* XXX I wish this wasn't necessary */
	int	old_window_display;

	old_window_display = swap_window_display(0);
	if (paddr_to_hostname(what, retval, size))
	{
		if (hostname_to_paddr(what, retval, size))
		{
			swap_window_display(old_window_display);
			syserr(-1, "one_to_another: both inet_ptohn and "
					"hostname_to_paddr failed (%d,%s)", 
					family, what);
			return -1;
		}
	}
	swap_window_display(old_window_display);

	return 0;
}


/**********************************************************************************************/
/*
 * ObDisclaimer -- I "vibe coded" this with Gemini 2.5 Flash (Preview).
 *
 * To every extent permitted by law, I disclaim all copyright 
 * to code which I did not write.  If anybody does has any rights 
 * to this, it isn't me.
 */

// Helper function to convert sockaddr to string
// Returns the buffer 's' on success, NULL on failure
char *	sockaddr_to_string (const struct sockaddr *sa, char *s, size_t maxlen) 
{
    if (!sa || !s || maxlen == 0) return NULL;

    switch (sa->sa_family) {
        case AF_INET: {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)sa;
            if (inet_ntop(AF_INET, &(ipv4->sin_addr), s, maxlen) == NULL) {
                yell("inet_ntop (IPv4)");
                return NULL;
            }
            return s;
        }
        case AF_INET6: {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)sa;
            if (inet_ntop(AF_INET6, &(ipv6->sin6_addr), s, maxlen) == NULL) {
                yell("inet_ntop (IPv6)");
                return NULL;
            }
            return s;
        }
        default:
            snprintf(s, maxlen, "Unknown family %d", sa->sa_family);
            return s; // Return the buffer with error message
    }
}

cJSON *	convert_ares_addrinfo_to_json (const struct ares_addrinfo *result) 
{
    if (!result) {
        return NULL; // Nothing to convert
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL; // Failed to create root object
    }

    // Add the "name" field
    if (result->name) {
        if (!cJSON_AddItemToObject(root, "name", cJSON_CreateString(result->name))) {
            // Failed to add name, clean up and return
            cJSON_DeleteItem(&root);
            return NULL;
        }
    } else {
         // Add a null or empty string for name if not present
        if (!cJSON_AddItemToObject(root, "name", cJSON_CreateString(""))) {
             cJSON_DeleteItem(&root);
             return NULL;
        }
    }


    // Add the "cnames" array
    cJSON *cnames_array = cJSON_CreateArray();
    if (!cnames_array) {
        cJSON_DeleteItem(&root); // Clean up previously created root
        return NULL; // Failed to create cnames array
    }
    if (!cJSON_AddItemToObject(root, "cnames", cnames_array)) {
        cJSON_DeleteItem(&root); // Clean up previously created root + array
        return NULL; // Failed to add cnames array to root
    }

    struct ares_addrinfo_cname *current_cname = result->cnames;
    while (current_cname) {
        if (current_cname->name) {
            cJSON *cname_string = cJSON_CreateString(current_cname->name);
            if (!cname_string) {
                 // Handle error: failed to create string, clean up everything
                 cJSON_DeleteItem(&root);
                 return NULL;
            }
            if (!cJSON_AddItemToArray(cnames_array, cname_string)) {
                 // Handle error: failed to add string to array, clean up everything
                 cJSON_DeleteItem(&root);
                 return NULL;
            }
        }
        current_cname = current_cname->next;
    }

    // Add the "nodes" array
    cJSON *nodes_array = cJSON_CreateArray();
    if (!nodes_array) {
        cJSON_DeleteItem(&root); // Clean up previously created root + cnames array
        return NULL; // Failed to create nodes array
    }
     if (!cJSON_AddItemToObject(root, "nodes", nodes_array)) {
        cJSON_DeleteItem(&root); // Clean up previously created root + cnames array + nodes array
        return NULL; // Failed to add nodes array to root
    }


    struct ares_addrinfo_node *current_node = result->nodes;
    while (current_node) {
        cJSON *node_obj = cJSON_CreateObject();
        if (!node_obj) {
            // Handle error: failed to create node object, clean up everything
            cJSON_DeleteItem(&root);
            return NULL;
        }
         if (!cJSON_AddItemToArray(nodes_array, node_obj)) {
             // Handle error: failed to add node object to array, clean up everything
             cJSON_DeleteItem(&root);
             return NULL;
         }


        // Add fields to the node object
        // Family
        const char *family_str = "Unknown";
        if (current_node->ai_family == AF_INET) family_str = "IPv4";
        else if (current_node->ai_family == AF_INET6) family_str = "IPv6";
        if (!cJSON_AddItemToObject(node_obj, "family", cJSON_CreateString(family_str))) {
             cJSON_DeleteItem(&root); return NULL; // Cleanup on error
        }

        // Socktype and Protocol (as numbers)
         if (!cJSON_AddItemToObject(node_obj, "socktype", cJSON_CreateNumber(current_node->ai_socktype))) {
             cJSON_DeleteItem(&root); return NULL; // Cleanup on error
         }
         if (!cJSON_AddItemToObject(node_obj, "protocol", cJSON_CreateNumber(current_node->ai_protocol))) {
             cJSON_DeleteItem(&root); return NULL; // Cleanup on error
         }


        // Address
        char addr_str[INET6_ADDRSTRLEN]; // Max size for IPv6
        if (sockaddr_to_string(current_node->ai_addr, addr_str, sizeof(addr_str))) {
            if (!cJSON_AddItemToObject(node_obj, "address", cJSON_CreateString(addr_str))) {
                cJSON_DeleteItem(&root); return NULL; // Cleanup on error
            }
        } else {
             // Handle case where address conversion failed (e.g., add null or error string)
            if (!cJSON_AddItemToObject(node_obj, "address", cJSON_CreateString("Conversion Error"))) {
                cJSON_DeleteItem(&root); return NULL; // Cleanup on error
            }
        }

        if (current_node->ai_addr) {
            int port = 0; // Default to 0 if extraction fails or address family is unknown

            if (current_node->ai_family == AF_INET) {
                struct sockaddr_in *ipv4 = (struct sockaddr_in *)current_node->ai_addr;
                port = ntohs(ipv4->sin_port); // Convert from network byte order
            } else if (current_node->ai_family == AF_INET6) {
                struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)current_node->ai_addr;
                port = ntohs(ipv6->sin6_port); // Convert from network byte order
            }

            // Add the port to the JSON node object
            if (!cJSON_AddItemToObject(node_obj, "port", cJSON_CreateNumber(port))) {
                 // Handle error: failed to add port, clean up everything
                 cJSON_DeleteItem(&root); 
		return NULL; // Cleanup on error
            }
        }

        // TTL (if available in your ares_addrinfo_node struct)
        // Note: TTL might not be a standard field in all ares versions or results.
        // Check the ares documentation/header for your specific version.
        // Example if it were available:
        // if (!cJSON_AddItemToObject(node_obj, "ttl", cJSON_CreateNumber(current_node->ai_ttl))) {
        //    cJSON_DeleteItem(&root); return NULL; // Cleanup on error
        // }

        current_node = current_node->ai_next;
    }

    return root; // Success! Return the created JSON object
}

void	my_addrinfo_json_callback (void *arg, int status, int timeouts, struct ares_addrinfo *result) 
{
    char *json_string;
    int   fd;

    fd = *(int *)arg;
    new_free(&arg);

    // Your context data can be retrieved from arg if needed
    // MyContext *ctx = (MyContext *)arg;

    if (status == ARES_SUCCESS) {
        say("my_addrinfo_json_callback: DNS lookup successful.");

        // Convert the result to JSON
        cJSON *json_output = convert_ares_addrinfo_to_json(result);

        if (json_output) {
            // Print the JSON (or do something else with it)
            json_string = cJSON_Generate(json_output, true_);
            if (json_string) {
                say("my_addrinfo_json_callback: JSON Result: %s", json_string);
            } else {
                yell("my_addrinfo_json_callback: Failed to print JSON.");
            }

            // IMPORTANT: Free the cJSON object when you are done
            cJSON_DeleteItem(&json_output);
        } else {
            yell("my_addrinfo_json_callback: Failed to convert ares_addrinfo to JSON.");
        }

    } else {
        yell("my_addrinfo_json_callback: DNS lookup failed with status: %d (%s)", status, ares_strerror(status));
        // Handle the error appropriately
	json_string = NULL;
	malloc_sprintf(&json_string, "{\"failure\":%d}", status);
    }

    // IMPORTANT: Free the ares_addrinfo result when you are done
    // This must be done whether the status is success or failure,
    // but ONLY if 'result' is not NULL.
    if (result) {
        ares_freeaddrinfo(result);
    }

    yell("my_addrinfo_json_callback: Writing JSON results to fd %d", fd);
    write(fd, json_string, strlen(json_string));
    new_free(&json_string);

    // If this callback signals the end of a specific request/task,
    // you might need to clean up the context data 'arg' as well,
    // depending on how you managed it.
    // e.g., free(arg);
    new_close(fd);

}

// Function to convert JSON (from convert_ares_addrinfo_to_json)
// into a dynamically allocated array of sockaddr_storage.
//
// json_string: The input JSON string.
// addr_array:  An output parameter. On success, this will point to
//              a dynamically allocated array of sockaddr_storage.
//              The caller is responsible for freeing this memory using free().
// port:	Port (in host order)
//
// Returns:     The number of sockaddr_storage structures successfully added
//              to the array, or -1 on parsing or allocation error.
int	json_to_sockaddr_array (const char *json_string, int *failure_code_, SSu **addr_array) 
{
    if (!json_string || !addr_array) {
        yell("json_to_sockaddr_array: Invalid input parameters.");
        return -1;
    }

    *addr_array = NULL; // Initialize the output parameter
    yell("json_to_sockaddr_array: I got: %s", json_string);

    size_t consumed = 0;
    cJSON *root = cJSON_Parse(json_string, strlen(json_string), &consumed);
    if (!root) {
        const char *error_ptr = json_string + consumed;
        yell("json_to_sockaddr_array: Error before: %s", error_ptr);
        return -1; // Failed to parse JSON
    }

    cJSON *failure_code = cJSON_GetObjectItemCaseSensitive(root, "failure");
    if (cJSON_IsNumber(failure_code)) {
        *failure_code_ = cJSON_GetNumberValue(failure_code);
        return 0;	/* Treat as empty */
    } else {
 	*failure_code_ = 0;
    }


    // Get the "nodes" array
    cJSON *nodes_array = cJSON_GetObjectItemCaseSensitive(root, "nodes");
    if (!cJSON_IsArray(nodes_array)) {
        yell("json_to_sockaddr_array: 'nodes' is not an array or does not exist.");
        cJSON_DeleteItem(&root);
        return 0; // No nodes to process, treat as empty but not an error
    }

    int num_potential_nodes = cJSON_GetArraySize(nodes_array);
    if (num_potential_nodes <= 0) {
        cJSON_DeleteItem(&root);
        return 0; // Empty array or size 0
    }

    // Allocate memory for the sockaddr_storage array
    // We allocate for the maximum potential nodes, and will track actual valid ones.
    *addr_array = new_malloc(num_potential_nodes * sizeof(struct sockaddr_storage));
    if (!*addr_array) {
        yell("json_to_sockaddr_array: Failed to allocate memory for sockaddr_storage array");
        cJSON_DeleteItem(&root);
        return -1; // Allocation failed
    }
    memset(*addr_array, 0, num_potential_nodes * sizeof(struct sockaddr_storage));

    int valid_address_count = 0;

    // Iterate through the nodes array
    for (int i = 0; i < num_potential_nodes; i++) {
        cJSON *node_obj = cJSON_GetArrayItem(nodes_array, i);
        if (!cJSON_IsObject(node_obj)) {
            yell("json_to_sockaddr_array: Item %d in 'nodes' is not an object, skipping.", i);
            continue; // Skip non-object items
        }

        cJSON *address_item = cJSON_GetObjectItemCaseSensitive(node_obj, "address");
        if (!cJSON_IsString(address_item) || cJSON_GetStringValue(address_item) == NULL || strlen(cJSON_GetStringValue(address_item)) == 0) {
            yell("json_to_sockaddr_array: Node %d missing or invalid 'address' string, skipping.", i);
            continue; // Skip nodes without a valid address string
        }
        const char *address_str = cJSON_GetStringValue(address_item);

        cJSON *port_item = cJSON_GetObjectItemCaseSensitive(node_obj, "port");
        if (!cJSON_IsNumber(port_item) || cJSON_GetNumberValue(port_item) < 0 || cJSON_GetNumberValue(port_item) > 65535) {
            yell("json_to_sockaddr_array: Node %d missing or invalid 'port' string, skipping.", i);
            continue; // Skip nodes without a valid port
        }
        unsigned short	port = (unsigned short)cJSON_GetNumberValue(port_item);

        SSu *current_storage = &((*addr_array)[valid_address_count]);
        memset(current_storage, 0, sizeof(struct sockaddr_storage)); // Clear the structure

        // Attempt to convert the address string using inet_pton
        // Try IPv4 first
        if (inet_pton(AF_INET, address_str, &((current_storage->si).sin_addr)) == 1) {
            current_storage->ss.ss_family = AF_INET;
            valid_address_count++;
            // Note: We are not setting port here. Typically, you would set the port
            // needed for your connection (e.g., using htons) after this function
            // returns, based on the service you are connecting to.
            ((struct sockaddr_in*)current_storage)->sin_port = htons(port);
        }
        // Else, try IPv6
        else if (inet_pton(AF_INET6, address_str, &((current_storage->si6).sin6_addr)) == 1) {
            current_storage->ss.ss_family = AF_INET6;
            valid_address_count++;
            // Note: Set IPv6 port similarly
            ((struct sockaddr_in6*)current_storage)->sin6_port = htons(port);
            // You might also need to set flowinfo and scope_id for IPv6
        } else {
            yell("json_to_sockaddr_array: Could not parse address '%s' as IPv4 or IPv6, skipping.", address_str);
            // Address conversion failed, this slot in the array remains unused
        }
    }

    // If the number of valid addresses is less than allocated,
    // we could realloc to save memory, but for simplicity,
    // we return the actual count and the caller uses only the first 'count' elements.
    // If count is 0, free the allocated memory as it's not needed.
    if (valid_address_count == 0 && *addr_array) {
        new_free(addr_array);
        *addr_array = NULL;
    }
    // Optional: Reallocate to the exact size if valid_address_count < num_potential_nodes
    /*
    else if (valid_address_count > 0 && valid_address_count < num_potential_nodes) {
         struct sockaddr_storage *realloced_array = new_realloc(*addr_array, valid_address_count * sizeof(struct sockaddr_storage));
         if (realloced_array) {
             *addr_array = realloced_array;
         } else {
             // Realloc failed, the original array is still valid.
             // Log a warning if desired. We return the count of valid addresses.
             yell("json_to_sockaddr_array: Failed to reallocate array to exact size.");
         }
    }
    */


    // Free the cJSON object
    cJSON_DeleteItem(&root);

    return valid_address_count;
}
/************* end vibe code **********************/


/**********************************************************************************************/
#include <ifaddrs.h>

typedef struct Vhosts {
	char *	hostname;
	char *	paddr;
	int	family;
	SSu	ssu;
	socklen_t sl;
	int	is_default;
} Vhosts;

static	Vhosts	vhosts[1024];
static	int	next_vhost = 0;
static	int	max_vhost = 1024;

void	init_one_vhost (struct ifaddrs *addr)
{
	char	addrbuf[128];
	int	i;

	if (!addr->ifa_addr)
		return;

	if (addr->ifa_addr->sa_family != AF_INET && addr->ifa_addr->sa_family != AF_INET6)
		return;

	if (addr->ifa_addr->sa_family == AF_INET)
		inet_ntop(addr->ifa_addr->sa_family, &((struct sockaddr_in *)addr->ifa_addr)->sin_addr, addrbuf, sizeof(addrbuf));
	if (addr->ifa_addr->sa_family == AF_INET6)
		inet_ntop(addr->ifa_addr->sa_family, &((struct sockaddr_in6 *)addr->ifa_addr)->sin6_addr, addrbuf, sizeof(addrbuf));

	/* Cache the result */
	if (next_vhost >= max_vhost)
	{
		yell("I'm plum full up on vhosts -- sorry!");
		return;
	}

	i = next_vhost++;
	memset(&vhosts[i], 0, sizeof(vhosts[i]));

	vhosts[i].family = addr->ifa_addr->sa_family;
	vhosts[i].hostname = NULL;
	vhosts[i].hostname = malloc_strdup(addrbuf);
	vhosts[i].paddr = malloc_strdup(addrbuf);
	memcpy(&(vhosts[i].ssu), addr->ifa_addr, sasocklen(addr->ifa_addr));
	vhosts[i].sl = sasocklen(addr->ifa_addr);
	vhosts[i].is_default = 0;

	yell("Successfully created vhost for %s", addrbuf);
}

void	init_vhosts_stage1 (void)
{
	struct ifaddrs	*addrs, *tmp;

	if (getifaddrs(&addrs)) 
	{
		yell("getifaddrs failed: %s", strerror(errno));
		return;
	}

	for (tmp = addrs; tmp; tmp = tmp->ifa_next)
		init_one_vhost(tmp);

	freeifaddrs(addrs);
	return;
}

BUILT_IN_COMMAND(vhostscmd)
{
	int	i;
	char	familystr[128];

	if (args && *args)
	{
		hostname_to_json(1, AF_UNSPEC, args, "0", 0);
		return;
	}

	for (i = 0; i < next_vhost; i++)
	{
		if (vhosts[i].family == AF_INET)
			strlcpy(familystr, "ipv4", sizeof(familystr));
		else if (vhosts[i].family == AF_INET6)
			strlcpy(familystr, "ipv6", sizeof(familystr));
		else
			strlcpy(familystr, "????", sizeof(familystr));

		say("Vhost=%d, family=%s, hostname=%s, paddr=%s, sl=%d, is_default=%d", 
			i, familystr, vhosts[i].hostname, vhosts[i].paddr, vhosts[i].sl, vhosts[i].is_default);
	}
}


int	lookup_vhost (int family_, const char *something, SSu *ssu_, socklen_t *sl)
{
	int	i;
	struct addrinfo *res = NULL, *res_save;
	struct addrinfo hints;
	int	err;

	if (empty(something))
		something = NULL;

	/* This is a protection mechanism in case anything goes sideways */
	*sl = 0;

	yell("Looking up [%s] vhost for %s", something?something:"<default>", familystr(family_));

	/*
	 * Check to see if it's cached...
	 */
	for (i = 0; i < next_vhost; i++)
	{
		if (vhosts[i].family != family_)
			continue;

		if (empty(something) && vhosts[i].is_default)
		{
			yell("Vhost family %d has default. yay.", family_);
			memcpy(ssu_, &vhosts[i].ssu, sizeof(SSu));
			*sl = socklen(ssu_);
			return 0;
		}

		if (!my_stricmp(something, vhosts[i].hostname) || 
		    !my_stricmp(something, vhosts[i].paddr))
		{
			yell("Vhost %s is cached. yay.", something?something:"<default>");
			memcpy(ssu_, &vhosts[i].ssu, sizeof(SSu));
			*sl = socklen(ssu_);
			return 0;
		}
	}

	/* If you asked for the default and I don't have one, then you get nothing */
	if (empty(something))
		return 0;

	/*
	 * No matter how you spin it, vhosts might get looked up before
	 * the main event loop is "ready", The solution to this is to have
	 * vhost lookups be a new server state.  Maybe someday.   Until then,
	 * we have to do it synchronously.
	 * XXX I hate this.
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family_;
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;
	if ((err = getaddrinfo(something, zero, &hints, &res)))
	{
		yell("lookup_vhost: Could not convert %s to hostname: %s", something, gai_strerror(err));
		return -1;
	}
	for (res_save = res; res; res = res->ai_next)
	{
		int	fd;

		if (res->ai_family != family_)
			continue;

		if ((fd = socket(family_, SOCK_STREAM, 0)) < 0)
		{
			syserr(-1, "lookup_vhost: socket(%d) failed: %s", family_, strerror(errno));
			continue;
		}

		if (bind(fd, res->ai_addr, res->ai_addrlen))
		{
			syserr(-1, "lookup_vhost: bind(%s) failed: %s", something, strerror(errno));
			close(fd);
			continue;
		}

		close(fd);

		/* Cache the result */
		if (next_vhost >= max_vhost)
		{
			yell("I'm plum full up on vhosts -- sorry!");
			return -1;
		}

		i = next_vhost++;
		memset(&vhosts[i], 0, sizeof(vhosts[i]));

		vhosts[i].family = res->ai_family;
		if (empty(something))
		{
			vhosts[i].hostname = NULL;
			vhosts[i].paddr = NULL;
		}
		else
		{
			vhosts[i].hostname = malloc_strdup(something);
			vhosts[i].paddr = sa_to_paddr_quick(res->ai_addr);
		}
		memcpy(&(vhosts[i].ssu), res->ai_addr, res->ai_addrlen);
		vhosts[i].sl = res->ai_addrlen;

		memcpy(ssu_, res->ai_addr, res->ai_addrlen);
		*sl = res->ai_addrlen;

		yell("Successfully created vhost for %s", something);
		freeaddrinfo(res_save);
		return 0;
	}

	freeaddrinfo(res_save);
	return -1;
}

/*
 * get_default_vhost -- give me a sockaddr i can use for my side of a socket.
 *
 * Arguments:
 *	family	 - (INPUT) AF_INET, AF_INET6, or AF_UNSPEC
 *	wanthost - (INPUT) The vhost you want to use (if not the system defualt)
 *	ssu	 - (OUTPUT) The sockaddr you should use
 *	sl	 - (OUTPUT) The sockaddr's length
 *
 * Return value:
 *	 0	- Everything went fine,  You can use 'ssu' and 'sl'
 *	-1	- Something went wrong -- do not use 'ssu'.
 *
 * Notes:
 *	Every socket connection, whether inbound or outbound, has a sockaddr
 *	for both ends.  In 99% of the circumstances, you don't do anything
 *	special and the OS chooses a sockaddr for you.  But if your machine
 *	has multiple IP addresses, you might want to use one other than the
 *	system's default.  You can set a default with -H or /hostname or you
 *	can specify the :vhost=: field in a server desc.
 *
 *	You use this function to get an ssu for the local side of the socket,
 *	and it should be passed to network_client() or network_server().
 *
 * 	Specifying an invalid vhost causes it to be ignored.  It will then
 *	try the next vhost on the list, and in the end will fall back to 
 *	"let the OS use the default address"
 */
int	get_default_vhost (int family, const char *wanthost, SSu *ssu_, socklen_t *sl)
{
	/* CHOICE #1 - Did you have something specific in mind? */
	if (!empty(wanthost) && !lookup_vhost(family, wanthost, ssu_, sl))
	{
		yell("vhost: %s was fine", wanthost);
		return 0;		/* Success */
	}

	/* CHOICE #2 - Do we have a "default" vhost? */
	if ((family == AF_UNSPEC || family == AF_INET) && LocalIPv4HostName)
	{
		if (!lookup_vhost(AF_INET, LocalIPv4HostName, ssu_, sl))
		{
			yell("vhost: I used %s instead", LocalIPv4HostName);
			return 0;	/* Success */
		}
	}
	if ((family == AF_UNSPEC || family == AF_INET6) && LocalIPv6HostName)
	{
		if (!lookup_vhost(AF_INET6, LocalIPv6HostName, ssu_, sl))
		{
			yell("vhost: I used %s instead", LocalIPv6HostName);
			return 0;	/* Success */
		}
	}

	/* CHOICE #3 - Let the system just decide what's best */
	if (!lookup_vhost(family, NULL, ssu_, sl))
	{
		yell("vhost: I fell back to the default, you know?");
		return 0;	/* Success */
	}

	return -1;			/* Failure */
}

int	make_vhost_default (int family_, const char *something)
{
	int	i;
	int	count = 0;

	/*
	 * Check to see if it's cached...
	 */
	for (i = 0; i < next_vhost; i++)
	{
		/* We are going to reset all values for this family */
		if (vhosts[i].family != family_)
			continue;

		if (!my_stricmp(something, vhosts[i].hostname) || 
		    !my_stricmp(something, vhosts[i].paddr))
		{
			vhosts[i].is_default = 1;
			count++;
		}
		else
			vhosts[i].is_default = 0;
	}

	return count;
}



/*
 * set_default_hostnames -- convert a paddr/hostname to a sockaddr you can use
 *
 * Hosts can have more than one ip addresses.  By default when you 
 * use INADDR_ANY the system uses the "primary ip address" associated
 * with your internet access.  But you can specify any ip address that
 * your system is configured to use with bind().  But how do you tell
 * epic to use a non-default ip address?
 *
 * This function converts a paddr or hostname into ip addresses -- and 
 * then if your system is using those ip addresses, will return the sockaddrs
 * so you can use them later.
 *
 * Furthermore, if you use both ipv4 and ipv6, you might have a different
 * vhost for each of them, using different hostnames.  You can seperate
 * them with a slash  (e.g.   "ipv4.host.com/ipv6.host.com")
 */
char *	set_default_hostnames (const char *hostname)
{
	char 	*workstr = NULL, 
		*v4 = NULL, 
		*v6 = NULL;
	char 	*retval4 = NULL, 
		*retval6 = NULL, 
		*retval = NULL;
	char 	*v4_error = NULL, 
		*v6_error = NULL;
	SSu	new_4;
	SSu	new_6;
	int	accept4 = 0, 
		accept6 = 0;
	socklen_t placeholder = 0;

	if (hostname == NULL)
	{
		new_free(&LocalIPv4HostName);
		new_free(&LocalIPv6HostName);
		goto summary;
	}

	workstr = LOCAL_COPY(hostname);
	v4 = workstr;
	if ((v6 = strchr(workstr, '/')))
		*v6++ = 0;
	else
		v6 = workstr;

	if (v4 && *v4)
	{
		if (lookup_vhost(AF_INET, v4, &new_4, &placeholder))
			malloc_strcpy(&v4_error, "see above");
		else
		{
			accept4 = 1;
			malloc_strcpy(&LocalIPv4HostName, v4);
			make_vhost_default(AF_INET, LocalIPv4HostName);
		}
	}
	else
		malloc_strcpy(&v4_error, "not specified");

	if (v6 && *v6)
	{
		if (lookup_vhost(AF_INET6, v6, &new_6, &placeholder))
			malloc_strcpy(&v6_error, "see above");
		else
		{
			accept6 = 1;
			malloc_strcpy(&LocalIPv6HostName, v6);
			make_vhost_default(AF_INET6, LocalIPv4HostName);
		}
	}
	else
		malloc_strcpy(&v6_error, "not specified");

summary:
	if (v4_error)
		malloc_sprintf(&retval4, "IPv4 vhost not changed because (%s)", v4_error);
	else if (LocalIPv4HostName)
	{
	    if (accept4)
		malloc_sprintf(&retval4, "IPv4 vhost changed to [%s]", LocalIPv4HostName);
	    else
		malloc_sprintf(&retval4, "IPv4 vhost unchanged from [%s]", LocalIPv4HostName);
	}
	else
		malloc_sprintf(&retval4, "IPv4 vhost unset");

	if (v6_error)
		malloc_sprintf(&retval6, "IPv6 vhost not changed because (%s)", v6_error);
	else if (LocalIPv6HostName)
	{
	    if (accept6)
		malloc_sprintf(&retval6, "IPv6 vhost changed to [%s]", LocalIPv6HostName);
	    else
		malloc_sprintf(&retval6, "IPv6 vhost unchanged from [%s]", LocalIPv6HostName);
	}
	else
		malloc_sprintf(&retval6, "IPv6 vhost unset");

	if (retval6)
		malloc_sprintf(&retval, "%s, %s", retval4, retval6);
	else
		retval = retval4, retval4 = NULL;

	new_free(&v4_error);
	new_free(&v6_error);
	new_free(&retval4);
	new_free(&retval6);

	return retval;
}

