/*
 * Copyright 2003, 2005 Jeremy Nelson
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

#ifndef __NETWORK_H__
#define __NETWORK_H__


		/* String(any) -> SSu */
	int     inet_strton             (const char *, const char *, SSu *, int);

		/* SSu -> String (paddr or hostname) */
	int     inet_ntostr             (SSu *, char *, int, char *, int, int);

		/* SSu -> String (paddr) */
	char *  inet_ssu_to_paddr	(SSu *name, int flags);

		/* String (hostname) -> String (p-addr) */
	int	inet_hntop             	(int, const char *, char *, int);

		/* String (p-addr) -> String (hostname) */
	int	inet_ptohn             	(int, const char *, char *, int);

		/* String (hostname) -> String (p-addr) 
		   String (p-addr)   -> String (hostname */
	int	one_to_another         	(int, const char *, char *, int);

		/* Accept a connection without blocking race condition into an SSu */
	int     my_accept              	(int, SSu *, socklen_t *);

		/* This lives in ircaux.c -- probably should not */
	char *	switch_hostname        	(const char *);

		/* Create fd with SSu for both sides */
	int     network_client          (SSu *, socklen_t, SSu *, socklen_t);

		/* Create listening server using default vhost */
	int     network_server          (int family, unsigned short port, SSu *storage);

		/* Create listening socket using specified vhost */
	int     client_bind             (SSu *, socklen_t);

		/* Create socket pinned to arbitrary (or default) vhost */
		/* Used by network_server() */
	int     inet_vhostsockaddr 	(int, int, const char *, SSu *, socklen_t *);

		
		/* A getaddrinfo() wrapper that supports AF_UNIX reliably */
	int	my_getaddrinfo		(const char *, const char *, const AI *, AI **);
	void	my_freeaddrinfo		(AI *);

		/* Nonblocking getaddrinfo() that writes its results to an fd */
	pid_t	async_getaddrinfo	(const char *, const char *, const AI *, int);
	void	marshall_getaddrinfo	(int, AI *results);
	void	unmarshall_getaddrinfo	(AI *results);

		/* Set any fd to nonblocking */
	int	set_non_blocking	(int);
		/* Set any fd to blocking */
	int	set_blocking		(int);
		/* Tell me what family (AF_INET) this SSu is for */
	int	family			(SSu *);

#define GNI_INTEGER 0x4000

#endif
