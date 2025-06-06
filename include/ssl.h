/*
 * ssl.h -- header file for ssl.c
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright 2000, 2002 EPIC Software Labs
 *
 */

#ifndef __ssl_h__
#define __ssl_h__

#define MAX_ONELINE 256

typedef	struct	ssl_cert_error {
	struct ssl_cert_error *next;
	int	err;
	int	depth;
	char 	oneline[MAX_ONELINE];
} ssl_cert_error;

#if 0
typedef struct ssl_metadata {
	int	fd;
	int	verify_result;
	char *	pem;
	char *	cert_hash;
	int	pkey_bits;
	char *	subject;
	char *	u_cert_subject;
	char *	issuer;
	char *	u_cert_issuer;
	char *	ssl_version;
} ssl_metadata;
#endif

	void	set_ssl_root_certs_location (void *);

	int     ssl_startup (int fd, int channel, const char *hostname, const char *cert);
	int	ssl_connect (int nfd, int quiet, int revents);
	int	ssl_connected (int nfd);
	int	ssl_write (int nfd, const void *, size_t);
	int	ssl_read (int nfd, int quiet, int revents);
	int	ssl_shutdown (int nfd);

	int	is_fd_ssl_enabled (int nfd);
	int	client_ssl_enabled (void);

	const char *	get_ssl_cipher (int nfd);
	const char *	get_ssl_pem (int fd);
	const char *	get_ssl_cert_hash (int fd);
	int		get_ssl_pkey_bits (int fd);
	const char *	get_ssl_subject (int fd);
	const char *	get_ssl_u_cert_subject (int fd);
	const char *	get_ssl_issuer (int fd);
	const char *	get_ssl_u_cert_issuer (int fd);
	const char *	get_ssl_ssl_version (int fd);
	int     	get_ssl_strict_status (int fd, int *retval);
	const char *	get_ssl_sans (int fd);
	int		get_ssl_verify_error (int fd); 
	int		get_ssl_checkhost_error (int fd);
	int		get_ssl_self_signed_error (int fd);
	int		get_ssl_other_error (int fd);
	int		get_ssl_most_serious_error (int fd);

#endif
