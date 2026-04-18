/*
 * scrambox.c: an interface for doing SCRAM-SHA-512
 *
 * Vibe coded from Gemini 2.5 Flash in 2025.
 * To every extent permitted by law...
 *    I disclaim all copyright to code I did not write.  
 *    I consider and dedicate this entire file to the public domain.
 *
 * To any extent the above dedication is not permitted by law...
 *    Copyright 2025 EPIC Software Labs.
 *
 *    Redistribution and use in every form, with or without modification
 *    is permitted.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 *    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 *    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *    IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *    AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *   SUCH DAMAGE.
 */
#include "irc.h"
#include "ircaux.h"
#include "output.h"
#include "functions.h"
#include "server.h"
#include "scrambox.h"

// Define maximum sizes for buffers
#define SCRAM_MAX_NONCE_LEN 48 // A reasonable length for a nonce
#define SCRAM_MAX_USERNAME_LEN 256
#define SCRAM_MAX_PASSWORD_LEN 256
#define SCRAM_MAX_SALT_LEN 256
#define SCRAM_MAX_AUTH_MSG_LEN 500 // Adjust as needed  -- 512 for irc purposes
#define SCRAM_KEY_LEN SHA512_DIGEST_LENGTH // SHA512 hash output size

/*
 * base64_encode - Perform a RFC 4648 serialization on a bitstream
 *
 * Arguments:
 *	data		- A whole bunch of bits (should be (char *))
 *	input_length 	- How many bytes are in 'data'
 *	encoded_data	- Where to put the output.  I hope it's big enough!
 *
 * Notes:
 *	XXX - This is a hack.  we have a perfectly good base64 transformer already.
 */
static size_t	base64_encode (const unsigned char *data, size_t input_length, char *encoded_data) 
{
	BIO 	*bio, 
		*b64;
	BUF_MEM *bufferPtr;
	size_t	retval;

	b64 = BIO_new(BIO_f_base64());
	bio = BIO_new(BIO_s_mem());
	bio = BIO_push(b64, bio);

	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newlines

	BIO_write(bio, data, input_length);
	BIO_flush(bio);

	BIO_get_mem_ptr(bio, &bufferPtr);
	memmove(encoded_data, bufferPtr->data, bufferPtr->length);
	encoded_data[bufferPtr->length] = 0;

	retval = bufferPtr->length;
	BIO_free_all(bio);
	return retval;
}

/*
 * base64_decode - Perform a RFC 4648 deserialization on a string
 *
 * Arguments:
 *	encoded_data	- A string containing a RFC4648 serialized data stream
 *	decoded_data	- Where to put the bunch of bits
 *	max_len		- How many bytes 'decoded_data' has.
 *
 * Notes:
 *	XXX - This is a hack.  we have a perfectly good base64 transformer already.
 */
static size_t	base64_decode (const char *encoded_data, unsigned char *decoded_data, size_t max_len) 
{
	BIO 	*bio, 
		*b64;
	size_t	len;
	int	decoded_len;

	if (!encoded_data || !decoded_data || max_len == 0)
		return (size_t) -1;

	b64 = BIO_new(BIO_f_base64());

	len = strlen(encoded_data);
	bio = BIO_new_mem_buf((void *)encoded_data, len);
	bio = BIO_push(b64, bio);
	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newlines

	/* This might return -1, which is fine */
	decoded_len = BIO_read(bio, decoded_data, len);
	if (decoded_len > (int)max_len) {
		decoded_len = -1; 
	}

	BIO_free_all(bio);
	return (size_t) decoded_len;
}


/*
 * saslprep_normalize - Convert a UTF-8 string into a RFC4013 string
 *
 * Arguments:
 *	input	   - A (UTF-8) string (typically) containing a username or a password
 *	output	   - Where to put the compliant equivalent to 'input'
 *	output_len - How big 'output' is.
 *
 * Notes:
 *	RFC 3454 and RFC 4013 collecitively define rules for sanitizing 
 *	unicode text strings so they can be used for SASL handshakes.
 *	What it amounts to is a bunch of characters you should either 
 *	strip out or substitute.   If your string doesn't contain any
 * 	shenanigans, then this is generally unnecessary.
 */
static void	saslprep_normalize (const char *input, char *output, size_t output_len) 
{
	if (input && output)
		strlcpy(output, input, output_len);
	else if (output)
		*output = 0;
	/* Else, oh well! */
}

// Function to safely XOR two byte arrays
/*
 * xor_buffers - Exclusive-OR two buffers together into a third buffer
 *
 * Arguments:
 *	result	- Where to write the results
 *	a	- One source buffer
 *	b	- Another source buffer
 *	len	- How many bytes to XOR together.
 *
 * Notes:
 *	Obviously, all of 'result', 'a', and 'b' must bye 'len' bytes long.
 *	There's no way to easily prove this, so the caller is responsible.
 *
 *	XXX - 
 *	(unsigned char *) is not the appropriate type for "a bag of bits",
 *	even if it is the appropriate type for the XOR operation itself.
 *	They should be (void *)s and cast to (unsigned char) as used.
 */
static void	xor_buffers (unsigned char *result, const unsigned char *a, const unsigned char *b, size_t len) {
	size_t	i;

	for (i = 0; i < len; ++i) {
		result[i] = a[i] ^ b[i];
	}
}


typedef struct 
{
	char		username     [SCRAM_MAX_USERNAME_LEN];
	char		password     [SCRAM_MAX_PASSWORD_LEN];
	char		client_nonce [SCRAM_MAX_NONCE_LEN * 2]; // For base64 encoding
	char		server_nonce [SCRAM_MAX_NONCE_LEN * 2]; // For base64 encoding
	unsigned char	salt	     [SCRAM_MAX_SALT_LEN];
	size_t		salt_len;
	unsigned	iteration_count;

	char 		client_first_message_bare [SCRAM_MAX_AUTH_MSG_LEN];
	char 		server_first_message      [SCRAM_MAX_AUTH_MSG_LEN];
	char 		client_final_message_bare [SCRAM_MAX_AUTH_MSG_LEN];
	// client-first, server-first, client-final
	char 		auth_message		  [SCRAM_MAX_AUTH_MSG_LEN * 3]; 

	/* 
	 * XXX Again, (unsigned char []) is not an appropriate type for 'bag of bits'
	 * even if the operations performed on each element is (unsigned char).
	 */
	unsigned char	salted_password [SCRAM_KEY_LEN];
	unsigned char	client_key	[SCRAM_KEY_LEN];
	unsigned char	stored_key	[SCRAM_KEY_LEN];
	unsigned char	client_signature[SCRAM_KEY_LEN];
	unsigned char	client_proof	[SCRAM_KEY_LEN];
	unsigned char	server_key	[SCRAM_KEY_LEN];
	unsigned char	server_signature[SCRAM_KEY_LEN];
} scram_state_t;

/* XXX This should not be a hardcoded limit */
scram_state_t *		scrambox[128] = { NULL};

static scram_state_t *	get_scrambox (int refnum)
{
	if (refnum < 0 || refnum >= 128)
		return NULL;
	if (!scrambox[refnum])
	{
		scrambox[refnum] = new_malloc(sizeof(scram_state_t));
		memset(scrambox[refnum], 0, sizeof(scram_state_t));
	}

	return scrambox[refnum];
}

static	void	reset_scrambox (int refnum)
{
	if (refnum < 0 || refnum >= 128)
		return;
	if (scrambox[refnum])
	{
		memset(scrambox[refnum], 0, sizeof(scram_state_t));
		new_free(&(scrambox[refnum]));
	}
}


/*
 * scram_client_first_message - Create a SASL message with username and nonce
 *
 * Arguments:
 *	state	  	  - The scrambox for this sasl scram negotiation
 *	username_         - The username for this negotiation
 *	password          - The password for this negotiation
 *	output_buffer     - Where to write the SASL SCRAM message
 *	output_buffer_len - How big output_buffer is.
 *
 * Return value:
 *	-1	- An error of some sort
 *		  * One of the parameters was NULL
 *		  * Libsodium failed to initialize
 *		  * The base64 encoding of the message failed
 *		  * snprintf failed (possibly the buffer was too small)
 *
 * Notes:
 *	- Because this message must be sent over RFC1459, we know that 
 *	  buffer and output_buffer_len are capped at 510.  Other uses
 *	  might consider malloc()ing off memory so it does't truncate.
 *
 *	- Scramboxes should be referred to only via refnums, but that's
 *	  a project for another day.
 */
static int	scram_client_first_message (scram_state_t *state, const char *username_, const char *password, char *output_buffer, size_t output_buffer_len) 
{
	unsigned char *	nonce_bytes;
	size_t 		encoded_nonce_len;
	int		written;

	/* PHASE 0 -- setup */
	if (!state || !username_ || !password || !output_buffer) 
	{
		yell("Error: Invalid arguments to scram_client_first_message");
		return -1;
	}

	/* Calling sodium_init() repeatedly is acceptable */
	if (sodium_init() == -1) {
		yell("Error: sodium_init() failed");
		return -1;
	}


	/* PHASE 1 - Stow the username and password in the scrambox */
	/*
	 * Because (in theory) SASL SCRAM usernames must be cleansed,
	 * pass it through the cleaner and store the cleaned string.
	 */
	saslprep_normalize(username_, state->username, sizeof(state->username));

	/*
	 * However, for passwords, we don't clean the password until 
	 * it needs to be run through PBKDF2.  So for now, we store the
	 * unsanitized string and keep track of that.
	 *
	 * Naturally, of course, having size limited buffers is bogus.
	 * But because this is all constrained by RFC1459, it's fine.
	 */
	strlcpy(state->password, password, sizeof(state->password));


	/* PHASE 2 - Create a Nonce for the scrambox */
	nonce_bytes = alloca(SCRAM_MAX_NONCE_LEN);
	randombytes_buf(nonce_bytes, SCRAM_MAX_NONCE_LEN);

	/* Stow the base64-encoded nonce in the scrambox. */
	encoded_nonce_len = base64_encode(nonce_bytes, SCRAM_MAX_NONCE_LEN, state->client_nonce);
	if (encoded_nonce_len == (size_t)-1) 
	{
		yell("Error encoding client nonce to base64");
		return -1;
	}

	/* PHASE 3 - Create a SASL SCRAM first-message */
	/* XXX It is absolutely indefensible that snprintf() returns an (int) */
	written = snprintf(output_buffer, output_buffer_len, 
				"n=%s,r=%s", state->username, 
					     state->client_nonce);

	if (written < 0 || (size_t)written >= output_buffer_len) 
	{
		yell("Error: Output buffer too small or snprintf error for client-first-message-bare");
		return -1;
	}

	/*
	 * Stow the "first message bare" into the scrambox
	 * This is what the user should send to the server.
	 */
	strlcpy(state->client_first_message_bare, output_buffer, sizeof(state->client_first_message_bare));

	return 0;
}


/*
 * parse_kv_pair - extract the value for an expected key
 *
 * Arguments:
 *	msg	         - A comma-seperated key=value SASL string
 *	key_prefix       - The key you're looking for (eg, "r=")
 *	value_buffer     - (OUTPUT) A place to put the value into	
 *	value_buffer_len - How big "value_buffer" is.
 * 
 * Return value:
 *	 0	- The value was successfully extracted
 *	-1	- Something went wrong
 *		  * "key_prefix" was not present in "msg"
 *		  * "value_buffer" is not big enough to hold the value 
 */
static int	parse_kv_pair (const char *msg, const char *key_prefix, char *value_buffer, size_t value_buffer_len) 
{
	const char *	start;
	const char *	end;
	size_t		len;
	size_t		i;

	if (!(start = strstr(msg, key_prefix)))
		return -1; 

	/* The start of the value is the byte after the prefix */
	start += strlen(key_prefix); 

	/* The end of the value is either a comma or a nul */
	if ((end = strchr(start, ',')))
		len = end - start;
	else
		len = strlen(start); 

	/* <sad trombone> */
	if (len >= value_buffer_len)
		return -1; 

	/* Meh, Let the compiler optimize this */
	for (i = 0; i < len; i++)
		*value_buffer++ = *start++;
	*value_buffer = 0;
	return 0;
}


/*
 * scram_process_server_first_message - contains a nonce, a salt, and a pbkdf2 iter count
 *
 * Arguments:
 *	state		     - The scrambox for this negotiation
 *	server_first_message - What the server sent in response to our first message
 *
 * Return Value:
 *	-1 - The message did not indicate we could proceed with SCRAM
 *	 0 - The message was processed successfully and scrambox was updated
 *	     You should call scam_client_final_message() next
 */
static int	scram_process_server_first_message(scram_state_t *state, const char *server_first_message) 
{
	char *	r_val;
	char *	s_val;
	char *	i_val;

	if (!state || !server_first_message) 
	{
		yell("Error: Invalid arguments to scram_process_server_first_message");
		return -1;
	}
	debug(DEBUG_SCRAM, "scram_process_server_first_message: %s", server_first_message);

	/* Stow the server's message in the scrambox */
	strlcpy(state->server_first_message, server_first_message, sizeof(state->server_first_message));


	/*
	 * So the server's first message is supposed to look like:
	 *	r=<client nonce>[+]<server nonce>,
	 *	s=<base64-encoded-salt>,
	 *	i=<iteration count>
	 *
	 * We verify that <client nonce> is what we sent (ie we are talking
	 * to who we expect to be talking to)
	 * Then we can extract the server's data.
	 * 
	 * In theory there should be a + between the two nonces in 'r'
	 * but in practice, libera doesn't do that.
	 * I suppose it could be possible to check for that if I cared
	 */


	// 1. Parse 'r' (client-nonce + server-nonce)
	r_val = alloca(SCRAM_MAX_NONCE_LEN * 6);	/* Client Nonce + Server Nonce */
	if (parse_kv_pair(server_first_message, "r=", r_val, SCRAM_MAX_NONCE_LEN * 6) != 0) {
		yell("Error: Could not parse 'r' from server-first-message");
		return -1;
	}

	// Verify that the client-nonce part matches what we sent
	if (strncmp(r_val, state->client_nonce, strlen(state->client_nonce)) != 0) {
		yell("Error: Client nonce mismatch in server-first-message");
		return -1;
	}

	const char *server_nonce_start;

	server_nonce_start = r_val + strlen(state->client_nonce); 
	strlcpy(state->server_nonce, server_nonce_start, sizeof(state->server_nonce));

	// 2. Parse 's' (salt)
	s_val = alloca(SCRAM_MAX_SALT_LEN * 4);		/* Base64 encoded salt */
	if (parse_kv_pair(server_first_message, "s=", s_val, SCRAM_MAX_SALT_LEN * 4) != 0) {
		yell("Error: Could not parse 's' from server-first-message");
		return -1;
	}
	state->salt_len = base64_decode(s_val, state->salt, sizeof(state->salt));
	if (state->salt_len == (size_t)-1) {
		yell("Error: Base64 decoding of salt failed");
		return -1;
	}

	// 3. Parse 'i' (iteration-count)
	i_val = alloca(16);				/* Iteration count as string */
	if (parse_kv_pair(server_first_message, "i=", i_val, 16) != 0) {
		yell("Error: Could not parse 'i' from server-first-message");
		return -1;
	}
	state->iteration_count = (unsigned int)atoi(i_val);
	if (state->iteration_count >= INT_MAX) {
		yell("Error: Invalid iteration count: %u", state->iteration_count);
		return -1;
	}

	// 4. Derive SaltedPassword using PBKDF2-HMAC-SHA512
	// Note: The password should be normalized before use in PBKDF2.
	// Assuming ASCII for now. For full SASLPrep, you'd process state->password here.
	char *	normalized_password;

	normalized_password = alloca(SCRAM_MAX_PASSWORD_LEN);
	saslprep_normalize(state->password, normalized_password, SCRAM_MAX_PASSWORD_LEN);

	if (PKCS5_PBKDF2_HMAC(normalized_password, strlen(normalized_password),
				state->salt, state->salt_len,
				state->iteration_count, EVP_sha512(),
				SCRAM_KEY_LEN, state->salted_password) == 0) 
	{
		yell("Error: PKCS5_PBKDF2_HMAC failed: %s", ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}

	return 0;
}

/*
 * scram_client_final_message -
 *
 * Arguments:
 *	state		- The scrambox for this session
 *	output_buffer	- Where to put the final message
 *	output_buffer_len - How big output_buffer is
 *
 * Return value:
 *	-1	- Some kind of error occurred - do not use 'output_buffer'
 *	 0	- Success - you are cleared to send 'output_buffer'
 */
static int	scram_client_final_message (scram_state_t *state, char *output_buffer, size_t output_buffer_len) 
{
	HMAC_CTX *	hmac_ctx = NULL;
	EVP_MD_CTX *	md_ctx = NULL;
	int		written;
	unsigned	stored_key_len;
	char *		client_proof_b64;
	size_t		encoded_proof_len;
	size_t		sz;
	unsigned 	client_key_len;
	unsigned 	client_signature_len;

	if (!state || !output_buffer) {
		yell("Error: Invalid arguments to scram_client_final_message");
		return -1;
	}

	// 1. Construct client-final-message-bare
	// Format: c=base64_encode(channel-binding),r=client-nonce+server-nonce
	/* In our case, c=biws is the base 64 encoding of "no channel binding" */
	sz = sizeof(state->client_final_message_bare);
	written = snprintf(state->client_final_message_bare, sz, "c=biws,r=%s%s", 
					state->client_nonce, 
					state->server_nonce);
	if (written < 0 || (size_t)written >= sz)
	{
		yell("Error: Buffer too small for client-final-message-bare");
		return -1;
	}

	// 2. Construct AuthMessage
	// AuthMessage = client-first-message-bare + "," + server-first-message + "," + client-final-message-bare
	sz = sizeof(state->auth_message);
	written = snprintf(state->auth_message, sz, "%s,%s,%s",
					state->client_first_message_bare,
					state->server_first_message,
					state->client_final_message_bare);
	if (written < 0 || (size_t)written >= sz) 
	{
		yell("Error: Buffer too small for AuthMessage");
		return -1;
	}
	debug(DEBUG_SCRAM, "state->client_first_message_bare is %s", state->client_first_message_bare);
	debug(DEBUG_SCRAM, "state->server_first_message is %s", state->server_first_message);
	debug(DEBUG_SCRAM, "state->client_final_message_bare is %s", state->client_final_message_bare);
	debug(DEBUG_SCRAM, "state->auth_message is %s", state->auth_message);

	// 3. Derive ClientKey, StoredKey, ClientSignature, ClientProof
	// ClientKey = HMAC(SaltedPassword, "Client Key")
	hmac_ctx = HMAC_CTX_new();
	if (!hmac_ctx) {
		yell("Error: HMAC_CTX_new failed");
		goto err;
	}
	if (HMAC_Init_ex(hmac_ctx, state->salted_password, SCRAM_KEY_LEN, EVP_sha512(), NULL) == 0) {
		yell("Error: HMAC_Init_ex for ClientKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	if (HMAC_Update(hmac_ctx, (const unsigned char *)"Client Key", strlen("Client Key")) == 0) {
		yell("Error: HMAC_Update for ClientKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	client_key_len = 0;
	if (HMAC_Final(hmac_ctx, state->client_key, &client_key_len) == 0) {
		yell("Error: HMAC_Final for ClientKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	HMAC_CTX_free(hmac_ctx);
	hmac_ctx = NULL;

	// StoredKey = H(ClientKey) (where H is SHA512)
	md_ctx = EVP_MD_CTX_new();
	if (!md_ctx) {
		yell("Error: EVP_MD_CTX_new failed");
		goto err;
	}
	if (EVP_DigestInit_ex(md_ctx, EVP_sha512(), NULL) == 0) {
		yell("Error: EVP_DigestInit_ex for StoredKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	if (EVP_DigestUpdate(md_ctx, state->client_key, SCRAM_KEY_LEN) == 0) {
		yell("Error: EVP_DigestUpdate for StoredKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}

	stored_key_len = 0;
	if (EVP_DigestFinal_ex(md_ctx, state->stored_key, &stored_key_len) == 0) {
		yell("Error: EVP_DigestFinal_ex for StoredKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	EVP_MD_CTX_free(md_ctx);
	md_ctx = NULL;

	// ClientSignature = HMAC(StoredKey, AuthMessage)
	hmac_ctx = HMAC_CTX_new();
	if (!hmac_ctx) {
		yell("Error: HMAC_CTX_new failed");
		goto err;
	}
	if (HMAC_Init_ex(hmac_ctx, state->stored_key, SCRAM_KEY_LEN, EVP_sha512(), NULL) == 0) {
		yell("Error: HMAC_Init_ex for ClientSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	if (HMAC_Update(hmac_ctx, (const unsigned char *)state->auth_message, strlen(state->auth_message)) == 0) {
		yell("Error: HMAC_Update for ClientSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	client_signature_len = 0;
	if (HMAC_Final(hmac_ctx, state->client_signature, &client_signature_len) == 0) {
		yell("Error: HMAC_Final for ClientSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	HMAC_CTX_free(hmac_ctx);
	hmac_ctx = NULL;

	// ClientProof = ClientKey XOR ClientSignature
	xor_buffers(state->client_proof, state->client_key, state->client_signature, SCRAM_KEY_LEN);

	// 4. Base64 encode ClientProof and append to client-final-message-bare
	client_proof_b64 = alloca(SCRAM_KEY_LEN * 2); // Roughly 1.5x + padding for base64
	encoded_proof_len = base64_encode(state->client_proof, SCRAM_KEY_LEN, client_proof_b64);
	if (encoded_proof_len == (size_t)-1) 
	{
		yell("Error encoding client proof to base64");
		goto err;
	}

	/* Now assemble it */
	written = snprintf(output_buffer, output_buffer_len, "%s,p=%s",
					state->client_final_message_bare, 
					client_proof_b64);
	if (written < 0 || (size_t)written >= output_buffer_len) 
	{
		yell("Error: Output buffer too small or snprintf error for final client message");
		goto err;
	}

	return 0;

err:
	if (hmac_ctx) 
		HMAC_CTX_free(hmac_ctx);
	if (md_ctx) 
		EVP_MD_CTX_free(md_ctx);
	return -1;
}

/*
 * scram_verify_server_final_message -
 *
 * Arguments:
 *	state			- A scrambox for this session
 *	server_final_message	- What the server sent us.
 *
 * Return value:
 *	-1	- The negotiation failed or we cannot proceed
 *	 0	- The server's response is convincing that they also have the password
 */
static int	scram_verify_server_final_message(scram_state_t *state, const char *server_final_message) 
{
	char *		v_val_b64;		/* Base64 encoded server signature */
	HMAC_CTX *	hmac_ctx = NULL;
	unsigned	server_key_len = 0;
	unsigned char *	received_server_signature;

	if (!state || !server_final_message) {
		yell("Error: Invalid arguments to scram_verify_server_final_message");
		return -1;
	}

	// 1. Parse 'v' (server-signature)
	v_val_b64 = alloca(SCRAM_KEY_LEN * 2);
	if (parse_kv_pair(server_final_message, "v=", v_val_b64, SCRAM_KEY_LEN * 2) != 0) {
		yell("Error: Could not parse 'v' from server-final-message");
		return -1;
	}

	received_server_signature = alloca(SCRAM_KEY_LEN);
	size_t decoded_sig_len = base64_decode(v_val_b64, received_server_signature, SCRAM_KEY_LEN);
	if (decoded_sig_len == (size_t)-1 || decoded_sig_len != SCRAM_KEY_LEN) {
		yell("Error: Base64 decoding of server signature failed or incorrect length");
		return -1;
	}

	// 2. Derive ServerKey and ServerSignature (client-side calculation)
	// ServerKey = HMAC(SaltedPassword, "Server Key")
	if (!(hmac_ctx = HMAC_CTX_new()))
	{
		yell("Error: HMAC_CTX_new failed");
		goto err;
	}
	if (HMAC_Init_ex(hmac_ctx, state->salted_password, SCRAM_KEY_LEN, EVP_sha512(), NULL) == 0) {
		yell("Error: HMAC_Init_ex for ServerKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	if (HMAC_Update(hmac_ctx, (const unsigned char *)"Server Key", strlen("Server Key")) == 0) {
		yell("Error: HMAC_Update for ServerKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	server_key_len = 0;
	if (HMAC_Final(hmac_ctx, state->server_key, &server_key_len) == 0) {
		yell("Error: HMAC_Final for ServerKey failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	HMAC_CTX_free(hmac_ctx);
	hmac_ctx = NULL;

	// ServerSignature = HMAC(ServerKey, AuthMessage)
	hmac_ctx = HMAC_CTX_new();
	if (!hmac_ctx) {
		yell("Error: HMAC_CTX_new failed");
		goto err;
	}
	if (HMAC_Init_ex(hmac_ctx, state->server_key, SCRAM_KEY_LEN, EVP_sha512(), NULL) == 0) {
		yell("Error: HMAC_Init_ex for ServerSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	if (HMAC_Update(hmac_ctx, (const unsigned char *)state->auth_message, strlen(state->auth_message)) == 0) {
		yell("Error: HMAC_Update for ServerSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	unsigned int server_signature_len = 0;
	if (HMAC_Final(hmac_ctx, state->server_signature, &server_signature_len) == 0) {
		yell("Error: HMAC_Final for ServerSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
		goto err;
	}
	HMAC_CTX_free(hmac_ctx);
	hmac_ctx = NULL;

	// 3. Compare the calculated ServerSignature with the received one
	if (CRYPTO_memcmp(state->server_signature, received_server_signature, SCRAM_KEY_LEN) != 0) {
		yell("Error: Server signature mismatch! Authentication failed.");
		return -1;
	}

	// If we reach here, authentication was successful!
	return 0;

err:
	if (hmac_ctx) 
		HMAC_CTX_free(hmac_ctx);
	return -1;
}


BUILT_IN_FUNCTION(function_scrambox, input)
{
	scram_state_t *	scrambox_ = NULL;
	char *		operation;

	GET_FUNC_ARG(operation, input);
	if (!my_stricmp(operation, "RESET")) {
		reset_scrambox(from_server);
		RETURN_EMPTY;
	} else {
		char *	output_buffer;

		output_buffer = alloca(512);

		if (!(scrambox_ = get_scrambox(from_server))) {
			yell("Error: Could not get scrambox for server %d", from_server);
			RETURN_EMPTY;
		}
		if (!my_stricmp(operation, "BEGIN")) {
			char *	username_;
			char *	password;

			GET_DWORD_ARG(username_, input);
			GET_DWORD_ARG(password, input);

			debug(DEBUG_SCRAM, "Calling scram_client_first_message with %s %s", username_, password);
			memset(output_buffer, 0, 512);
			if (scram_client_first_message(scrambox_, username_, password, output_buffer, 512)) {
				yell("Scrambox first message failed for server %d", from_server);
				RETURN_EMPTY;
			}
			debug(DEBUG_SCRAM, "first client message is %s", output_buffer);
			RETURN_FSTR(output_buffer);
		} else if (!my_stricmp(operation, "RESPONSE")) {
			/* If this is the first response, generate our final message */
			if (!*(scrambox_->client_final_message_bare))
			{
				if (scram_process_server_first_message(scrambox_, input)) {
					yell("Processing server first message failed.");
					RETURN_EMPTY;
				}
				debug(DEBUG_SCRAM, "Calling scram_client_final_message");
				memset(output_buffer, 0, 512);
				if (scram_client_final_message(scrambox_, output_buffer, 512)) {
					yell("Client final message failed.");
					RETURN_EMPTY;
				}
				debug(DEBUG_SCRAM, "Final client message is %s", output_buffer);
				RETURN_FSTR(output_buffer);
			}
			/* If this is the second response, validate it */
			else
			{
				debug(DEBUG_SCRAM, "Calling scram_verify_server_final_message with %s", input);
				if (scram_verify_server_final_message(scrambox_, input)) {
					yell("Server final message verification failed.");
					RETURN_EMPTY;
				} else {
					RETURN_STR("+");
				}
			}
		}
	}
	RETURN_EMPTY;
}

