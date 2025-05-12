/*
 * Copyright (c) 2013 William Pitcock <nenolod@dereferenced.org>.
 * Copyright 2025 EPIC Software Labs
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* 
 * This is the 'ecdsatool' library and tool adapted for our needs.
 * THIS IS FORKED FROM from https://github.com/kaniini/ecdsatool
 * commit 7c0b2c51e2e64d1986ab1dc2c57c2d895cc00ed1
 *
 * Please do not bother the original authors about problems with this fork.
 * Contact EPIC Software Labs (list@epicsol.org) only.
 */
/*
 * If the original authors of this code ever happen to read this,
 * thank you for the great code you gave the world and making this
 * so easy to adapt/adopt. :) Come see us on #epic on EFNet!
 */

#include "irc.h"
#include "irc_std.h"
#include "ircaux.h"
#include "output.h"
#include "functions.h"
#include "ecdsatool.h"

#define OPENSSL_SUPPRESS_DEPRECATED 1
#define OPENSSL_SUPPRESS_DEPRECATED_3_0 1

typedef struct auth_key_s { EC_KEY *eckey; } ecdsa_key_t;

static char *		tool_keygen			(int argc, char *argv[]);
static char *		tool_pubkey			(int argc, char *argv[]);
static char *		tool_sign			(int argc, char *argv[]);
static char *		tool_verify			(int argc, char *argv[]);

static	ecdsa_key_t *	ecdsa_key__new          	(void);
static	ecdsa_key_t *	ecdsa_key__load         	(const char *);
static	ecdsa_key_t *	ecdsa_key__from_pubkey  	(char *, size_t);
static	ecdsa_key_t *	ecdsa_key__from_base64_pubkey   (const char *);
static	void		ecdsa_key__free         	(ecdsa_key_t **);
static	size_t		ecdsa_key__public_key_length    (ecdsa_key_t *);
static	char *		ecdsa_key__public_key_blob      (ecdsa_key_t *);
static	char *		ecdsa_key__public_key_base64    (ecdsa_key_t *);
static	bool		ecdsa_auth__verify		(ecdsa_key_t *, char *, size_t, char *, size_t);
static	bool		ecdsa_auth__verify_base64	(ecdsa_key_t *, const char *, const char *);
static	bool		ecdsa_auth__sign		(ecdsa_key_t *, char *, size_t, char **, size_t *);
static	bool		ecdsa_auth__sign_base64		(ecdsa_key_t *, char *, char **, size_t *);


typedef struct tool_applet_s {
        const char *	name;
        char * 		(*main) (int argc, char **argv);
} tool_applet_t;

static tool_applet_t tool_applets[] = {
        {"keygen", tool_keygen},
        {"pubkey", tool_pubkey},
        {"sign", tool_sign},
        {"verify", tool_verify},
        {NULL, NULL}
};

static tool_applet_t *	tool_applet_find (const char *name)
{
        tool_applet_t *iter;

        for (iter = tool_applets; iter->name != NULL; iter++)
        {
                if (!strcmp(iter->name, name))
                        return iter;
        }

        return NULL;
}

BUILT_IN_FUNCTION(function_ecdsatool, args)
{
        tool_applet_t *	tool;
	int		argc;
	char *		argv[10];

	if (!args || !*args)
		RETURN_EMPTY;

	argc = split_args(args, argv, 10);
        if (!(tool = tool_applet_find(argv[0])))
	{
		yell("ecdsatool: no such tool: %s", argv[0]);
		RETURN_EMPTY;
	}

	yell("Running ecdsatool %s (%d)", argv[0], argc);
        return tool->main(argc, argv);
}

/* * */
static char *	tool_keygen (int argc, char *argv[])
{
	FILE *		pubout;
	ecdsa_key_t *	key;
	char *		pubkey;
	int		fileno_;

	if (argv[1] == NULL)
	{
		yell("usage: ecdsatool keygen privatekey.pem\n");
		RETURN_EMPTY;
	}

	key = ecdsa_key__new();

	if (!(pubout = fopen(argv[1], "w")))
	{
		yell("ecdsatool keygen: Could not create file %s -- check permissions?", argv[1]);
		RETURN_EMPTY;
	}

	if ((fileno_ = fileno(pubout)) < 0)
	{
		yell("ecdsatool keygen: fileno() failed on %s: %s", argv[1], strerror(errno));
		RETURN_EMPTY;
	}

	if (fchmod(fileno_, S_IRUSR) < 0)
	{
		yell("ecdsatool keygen: fchmod() failed on %s: %s", argv[1], strerror(errno));
		RETURN_EMPTY;
	}

	PEM_write_ECPrivateKey(pubout, key->eckey, NULL, NULL, 0, NULL, NULL);
	fclose(pubout);

	pubkey = ecdsa_key__public_key_base64(key);
	ecdsa_key__free(&key);
	return pubkey;
}

static char *	tool_pubkey (int argc, char *argv[])
{
	ecdsa_key_t *	key;
	char *		pubkey;

	if (argv[1] == NULL)
	{
		yell("usage: ecdsatool pubkey privatekey.pem\n");
		RETURN_EMPTY;
	}

	if (!(key = ecdsa_key__load(argv[1])))
	{
		yell("ecdsatool pubkey: loading key %s failed", argv[1]);
		RETURN_EMPTY;
	}

	pubkey = ecdsa_key__public_key_base64(key);
	ecdsa_key__free(&key);
	return pubkey;
}

static char *	tool_sign (int argc, char *argv[])
{
	ecdsa_key_t *	key;
	char *		signature = NULL;
	char *		in = NULL;
	size_t 		siglen;

	if (argc < 3)
	{
		yell("usage: ecdsatool sign privatekey.pem base64challenge");
		RETURN_EMPTY;
	}

	if (!(key = ecdsa_key__load(argv[1])))
	{
		yell("ecdsatool sign: loading key %s failed", argv[1]);
		RETURN_EMPTY;
	}

	in = malloc_strdup(argv[2]);
	ecdsa_auth__sign_base64(key, in, &signature, &siglen);
	new_free(&in);
	ecdsa_key__free(&key);

	if (!signature)
	{
		yell("ecdsatool_sign: signing failed for %s", argv[1]);
		RETURN_EMPTY;
	}

	return signature;
}

static char *	tool_verify (int argc, char *argv[])
{
	ecdsa_key_t *	key;
	char *		inbuf;
	bool 		verify;
	char *		retval;

	if (argc < 4)
	{
		yell("usage: ecdsatool verify privatekey.pem base64challenge base64signature");
		RETURN_EMPTY;
	}

	if (!(key = ecdsa_key__load(argv[1])))
	{
		yell("ecdsatool verify: loading key %s failed", argv[1]);
		RETURN_EMPTY;
	}

	verify = ecdsa_auth__verify_base64(key, argv[2], argv[3]);
	ecdsa_key__free(&key);
	new_free(&inbuf);

	retval = malloc_sprintf(NULL, "%d", verify ? 1 : 0);
	return retval;
}


/* * * * * * */
static inline ecdsa_key_t *	ecdsa_key__alloc (void)
{
	ecdsa_key_t *	key;

	key = new_malloc(sizeof(ecdsa_key_t));
	memset(key, 0, sizeof(ecdsa_key_t));
	key->eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	EC_KEY_set_conv_form(key->eckey, POINT_CONVERSION_COMPRESSED);

	return key;
}

/*
 * Generate an ECC keypair suitable for authentication.
 */
static	ecdsa_key_t *	ecdsa_key__new (void)
{
	ecdsa_key_t *	key;

	key = ecdsa_key__alloc();
	if (key->eckey != NULL)
		EC_KEY_generate_key(key->eckey);
	else
	{
		ecdsa_key__free(&key);
		return NULL;
	}

	return key;
}

static	int	check_permissions (FILE *f, int perm)
{
	int	fd;
	struct stat sb;

	fd = fileno(f);

	memset(&sb, 0, sizeof(struct stat));
	if (fstat(fd, &sb) < 0)
	{
		yell("ecdsatool: check_permissions: fstat failed: %s", strerror(errno));
		return -1;
	}
	if (sb.st_mode != perm)
	{
		yell("ecdsatool: check_permissions: file should have %o permissions, but had %o", perm, sb.st_mode);
		return -1;
	}
	return 0;
}

/*
 * Load an ECC keypair from a PEM file.
 */
static	ecdsa_key_t *	ecdsa_key__load (const char *filename)
{
	ecdsa_key_t *	key;
	FILE *		in;

 	key = ecdsa_key__alloc();
	if (!(in = fopen(filename, "r")))
	{
		ecdsa_key__free(&key);
		return NULL;
	}
	if (check_permissions(in, S_IFREG|S_IRUSR))
	{
		yell("ecdsatool: load_key: file %s had insecure permissions - not loaded", filename);
		return NULL;
	}

	PEM_read_ECPrivateKey(in, &key->eckey, NULL, NULL);
	fclose(in);

	EC_KEY_set_conv_form(key->eckey, POINT_CONVERSION_COMPRESSED);
	if (!EC_KEY_check_key(key->eckey))
	{
		ecdsa_key__free(&key);
		return NULL;
	}

	return key;
}

/*
 * Deserialize a raw public key.
 */
static	ecdsa_key_t *	ecdsa_key__from_pubkey (char *pubkey_raw, size_t len)
{
	ecdsa_key_t *	key;
	unsigned const char *	pubkey_raw_p;

	key = ecdsa_key__alloc();
	pubkey_raw_p = (unsigned char *)pubkey_raw;
	o2i_ECPublicKey(&key->eckey, &pubkey_raw_p, len);
	if (!EC_KEY_check_key(key->eckey))
	{
		ecdsa_key__free(&key);
		return NULL;
	}

	return key;
}

/*
 * Deserialize a public key in base64 encapsulation.
 */
static	ecdsa_key_t *	ecdsa_key__from_base64_pubkey (const char *keydata)
{
	char *		workbuf;
	size_t		len;
	ecdsa_key_t *	retval;

	workbuf = transform_string_dyn("-B64", keydata, 0, &len);
	if (!workbuf)
		return NULL;
	retval = ecdsa_key__from_pubkey(workbuf, len);
	new_free(&workbuf);
	return retval;
}

/*
 * Free an ECC key.
 */
static	void	ecdsa_key__free (ecdsa_key_t **key)
{
	if ((*key)->eckey != NULL)
		EC_KEY_free((*key)->eckey);

	new_free(key);
}

/*
 * Return public key component length, if it were a binary blob.
 */
static	size_t	ecdsa_key__public_key_length (ecdsa_key_t *key)
{
	if (key->eckey == NULL)
		return 0;

	return (size_t) i2o_ECPublicKey(key->eckey, NULL);
}

/*
 * Return public key as binary blob.  Use ecdsa_key__public_key_length() to
 * get the key length in bytes.
 */
static	char *	ecdsa_key__public_key_blob (ecdsa_key_t *key)
{
	char 	*	keybuf;
	unsigned char *	keybuf_p;
	size_t 		len;

	if (key->eckey == NULL)
		return NULL;

	len = ecdsa_key__public_key_length(key);
	keybuf = new_malloc(len);
	keybuf_p = (unsigned char *)keybuf;

	i2o_ECPublicKey(key->eckey, &keybuf_p);

	return keybuf;
}

/*
 * Return public key as base64 blob.
 */
static	char *	ecdsa_key__public_key_base64 (ecdsa_key_t *key)
{
	char *		keybuf;
	size_t 		keylen;
	char *		b64buf;

	if (key->eckey == NULL)
		return NULL;

	keylen = ecdsa_key__public_key_length(key);
	keybuf = ecdsa_key__public_key_blob(key);
	b64buf = transform_string_dyn("+B64", keybuf, keylen, NULL);

	new_free(&keybuf);

	return b64buf;
}


/*
 * Verify a signature.
 */
static	bool	ecdsa_auth__verify (ecdsa_key_t *key, char *blob, size_t len, char *sig, size_t siglen)
{
	unsigned char *	ublob;
	unsigned char *	usig;

	ublob = (unsigned char *)blob;
	usig = (unsigned char *)sig;

	if (1 != ECDSA_verify(0, ublob, len, usig, siglen, key->eckey))
		return false;

	return true;
}

/*
 * Verify a base64-encapsulated signature.
 */
static	bool	ecdsa_auth__verify_base64 (ecdsa_key_t *key, const char *blob_, const char *sig_)
{
	char *		blob;
	size_t		bloblen;
	char *		sig;
	size_t		siglen;
	bool		retval;

	blob = transform_string_dyn("-B64", blob_, 0, &bloblen);
	sig = transform_string_dyn("-B64", sig_, 0, &siglen);
	retval = ecdsa_auth__verify(key, blob, bloblen, sig, siglen);

	new_free(&blob);
	new_free(&sig);
	return retval;
}

/*
 * Sign a challenge.
 */
static	bool	ecdsa_auth__sign (ecdsa_key_t *key, char *in_, size_t inlen, char **out, size_t *outlen)
{
	unsigned char *	in;
	char *		sig_buf;
	unsigned	sig_len;
	unsigned char *	sig_buf_p;

	if (key->eckey == NULL)
		return false;

	in = (unsigned char *)in_;

	sig_len = ECDSA_size(key->eckey);
	sig_buf = new_malloc(sig_len);
	sig_buf_p = (unsigned char *)sig_buf;

	if (!ECDSA_sign(0, in, inlen, sig_buf_p, &sig_len, key->eckey))
	{
		new_free(&sig_buf);
		return false;
	}

	*out = sig_buf;
	*outlen = (size_t) sig_len;
	return true;
}

/*
 * Sign a base64 challenge and base64 it.
 */
static	bool	ecdsa_auth__sign_base64 (ecdsa_key_t *key, char *in_, char **out, size_t *outlen)
{
	char *		inbuf;
	size_t		inlen;
	char *		workbuf;
	size_t		workbuflen;
	char *		retval;
	size_t		retvallen;

	inbuf = transform_string_dyn("-B64", in_, 0, &inlen);

	workbuf = NULL;
	if (!ecdsa_auth__sign(key, inbuf, inlen, &workbuf, &workbuflen)) {
		new_free(&inbuf);
		return false;
	}
	if (workbuf == NULL) {
		new_free(&inbuf);
		return false;
	}
	retval = transform_string_dyn("+B64", workbuf, workbuflen, &retvallen);

	new_free(&inbuf);
	new_free(&workbuf);

	*out = retval;
	*outlen = retvallen;
	return true;
}

