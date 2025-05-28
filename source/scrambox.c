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
#define SCRAM_MAX_AUTH_MSG_LEN 1024 // Adjust as needed
#define SCRAM_KEY_LEN SHA512_DIGEST_LENGTH // SHA512 hash output size

// Helper for Base64 encoding (a simple one for demonstration, production might use a more robust one)
// Note: This is a very basic implementation. For production, consider a proper base64 library.
static size_t base64_encode(const unsigned char *data, size_t input_length, char *encoded_data) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newlines

    BIO_write(bio, data, input_length);
    BIO_flush(bio);

    BIO_get_mem_ptr(bio, &bufferPtr);
    memcpy(encoded_data, bufferPtr->data, bufferPtr->length);
    encoded_data[bufferPtr->length] = '\0';

    BIO_free_all(bio);
    return bufferPtr->length;
}

// Helper for Base64 decoding (similar note as encoding)
static size_t base64_decode(const char *encoded_data, unsigned char *decoded_data, size_t max_len) {
    BIO *bio, *b64;
    size_t len = strlen(encoded_data);

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf((void*)encoded_data, len);
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newlines

    size_t decoded_len = BIO_read(bio, decoded_data, len); // Max length for read
    if (decoded_len > max_len) { // Check for buffer overflow
        decoded_len = -1; // Indicate error
    }
    
    BIO_free_all(bio);
    return decoded_len;
}


// Function to normalize a string (e.g., password) based on SASLPrep.
// For SCRAM, this is generally just using the string directly unless specific
// processing is required (e.g., Unicode normalization).
// For simple ASCII passwords, a direct copy is sufficient.
// For full SASLPrep, you'd need a Unicode library like ICU.
// For now, we'll just copy it.
static void saslprep_normalize(const char *input, char *output, size_t output_len) {
    strlcpy(output, input, output_len);
    output[output_len - 1] = '\0'; // Ensure null termination
}

// Function to safely XOR two byte arrays
static void xor_buffers(unsigned char *result, const unsigned char *a, const unsigned char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        result[i] = a[i] ^ b[i];
    }
}


typedef struct {
    char username[SCRAM_MAX_USERNAME_LEN];
    char password[SCRAM_MAX_PASSWORD_LEN];
    char client_nonce[SCRAM_MAX_NONCE_LEN * 2]; // For base64 encoding
    char server_nonce[SCRAM_MAX_NONCE_LEN * 2]; // For base64 encoding
    unsigned char salt[SCRAM_MAX_SALT_LEN];
    size_t salt_len;
    unsigned int iteration_count;

    char client_first_message_bare[SCRAM_MAX_AUTH_MSG_LEN];
    char server_first_message[SCRAM_MAX_AUTH_MSG_LEN];
    char client_final_message_bare[SCRAM_MAX_AUTH_MSG_LEN];
    char auth_message[SCRAM_MAX_AUTH_MSG_LEN * 3]; // client-first, server-first, client-final

    unsigned char salted_password[SCRAM_KEY_LEN];
    unsigned char client_key[SCRAM_KEY_LEN];
    unsigned char stored_key[SCRAM_KEY_LEN];
    unsigned char client_signature[SCRAM_KEY_LEN];
    unsigned char client_proof[SCRAM_KEY_LEN];
    unsigned char server_key[SCRAM_KEY_LEN];
    unsigned char server_signature[SCRAM_KEY_LEN];

} scram_state_t;

scram_state_t *		scrambox[128] = { NULL};	/* XXX hardcoded limit is bogus. */

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


static int	scram_client_first_message (scram_state_t *state, const char *username_, const char *password, char *output_buffer, size_t output_buffer_len) 
{
    if (!state || !username_ || !password || !output_buffer) {
        yell("Error: Invalid arguments to scram_client_first_message");
        return -1;
    }

    // Initialize libsodium (important if not already done)
    if (sodium_init() == -1) {
        yell("Error: sodium_init() failed");
        return -1;
    }

    // 1. Store username_ and password (after optional SASLPrep normalization)
    saslprep_normalize(username_, state->username, sizeof(state->username));
    // For password, we'll normalize it when we use it for PBKDF2 later.
    // For now, store it directly.
    strlcpy(state->password, password, sizeof(state->password));
    state->password[sizeof(state->password) - 1] = '\0';


    // 2. Generate client-nonce using libsodium
    unsigned char nonce_bytes[SCRAM_MAX_NONCE_LEN];
    randombytes_buf(nonce_bytes, SCRAM_MAX_NONCE_LEN);

    // Encode nonce to base64
    size_t encoded_nonce_len = base64_encode(nonce_bytes, SCRAM_MAX_NONCE_LEN, state->client_nonce);
    if (encoded_nonce_len == (size_t)-1) {
        yell("Error encoding client nonce to base64");
        return -1;
    }

    // 3. Construct client-first-message-bare
    int written = snprintf(output_buffer, output_buffer_len, "n=%s,r=%s", state->username, state->client_nonce);
    if (written < 0 || (size_t)written >= output_buffer_len) {
        yell("Error: Output buffer too small or snprintf error for client-first-message-bare");
        return -1;
    }
    strncpy(state->client_first_message_bare, output_buffer, sizeof(state->client_first_message_bare));
    state->client_first_message_bare[sizeof(state->client_first_message_bare) - 1] = '\0';

    return 0;
}


// Helper to parse key-value pairs (basic implementation)
// Returns 0 on success, -1 on error
static int	parse_kv_pair (const char *msg, const char *key_prefix, char *value_buffer, size_t value_buffer_len) 
{
    const char *start = strstr(msg, key_prefix);
    if (!start) {
        return -1; // Key not found
    }
    start += strlen(key_prefix); // Move past the key prefix

    const char *end = strchr(start, ',');
    size_t len;
    if (end) {
        len = end - start;
    } else {
        len = strlen(start); // Last key-value pair
    }

    if (len >= value_buffer_len) {
        return -1; // Buffer too small
    }
    strncpy(value_buffer, start, len);
    value_buffer[len] = '\0';
    return 0;
}


static int	scram_process_server_first_message(scram_state_t *state, const char *server_first_message) 
{
    if (!state || !server_first_message) {
        yell("Error: Invalid arguments to scram_process_server_first_message");
        return -1;
    }

    if (x_debug & DEBUG_SCRAM)
	yell("scram_process_server_first_message: %s", server_first_message);

    strncpy(state->server_first_message, server_first_message, sizeof(state->server_first_message));
    state->server_first_message[sizeof(state->server_first_message) - 1] = '\0';

    char r_val[SCRAM_MAX_NONCE_LEN * 6]; // client_nonce + server_nonce
    char s_val[SCRAM_MAX_SALT_LEN * 4]; // Base64 encoded salt
    char i_val[16]; // Iteration count as string

    // 1. Parse 'r' (client-nonce + server-nonce)
    if (parse_kv_pair(server_first_message, "r=", r_val, sizeof(r_val)) != 0) {
        yell("Error: Could not parse 'r' from server-first-message");
        return -1;
    }

    // Verify that the client-nonce part matches what we sent
    if (strncmp(r_val, state->client_nonce, strlen(state->client_nonce)) != 0) {
        yell("Error: Client nonce mismatch in server-first-message");
        return -1;
    }
    // Extract server-nonce part (after our client_nonce and the '+')
    // const char *server_nonce_start = r_val + strlen(state->client_nonce) + 1; // +1 for the '+'
    // I think libera doesn't include the + here.
    const char *server_nonce_start = r_val + strlen(state->client_nonce); 
    strncpy(state->server_nonce, server_nonce_start, sizeof(state->server_nonce));
    state->server_nonce[sizeof(state->server_nonce) - 1] = '\0';

    // 2. Parse 's' (salt)
    if (parse_kv_pair(server_first_message, "s=", s_val, sizeof(s_val)) != 0) {
        yell("Error: Could not parse 's' from server-first-message");
        return -1;
    }
    state->salt_len = base64_decode(s_val, state->salt, sizeof(state->salt));
    if (state->salt_len == (size_t)-1) {
        yell("Error: Base64 decoding of salt failed");
        return -1;
    }

    // 3. Parse 'i' (iteration-count)
    if (parse_kv_pair(server_first_message, "i=", i_val, sizeof(i_val)) != 0) {
        yell("Error: Could not parse 'i' from server-first-message");
        return -1;
    }
    state->iteration_count = (unsigned int)atoi(i_val);
    if (state->iteration_count <= 0) {
        yell("Error: Invalid iteration count: %u", state->iteration_count);
        return -1;
    }

    // 4. Derive SaltedPassword using PBKDF2-HMAC-SHA512
    // Note: The password should be normalized before use in PBKDF2.
    // Assuming ASCII for now. For full SASLPrep, you'd process state->password here.
    char normalized_password[SCRAM_MAX_PASSWORD_LEN];
    saslprep_normalize(state->password, normalized_password, sizeof(normalized_password));

    if (PKCS5_PBKDF2_HMAC(normalized_password, strlen(normalized_password),
                          state->salt, state->salt_len,
                          state->iteration_count, EVP_sha512(),
                          SCRAM_KEY_LEN, state->salted_password) == 0) {
        yell("Error: PKCS5_PBKDF2_HMAC failed: %s", ERR_error_string(ERR_get_error(), NULL));
        return -1;
    }

    return 0;
}

static int	scram_client_final_message (scram_state_t *state, char *output_buffer, size_t output_buffer_len) 
{
	HMAC_CTX *hmac_ctx = NULL;
	EVP_MD_CTX *md_ctx = NULL;

    if (!state || !output_buffer) {
        yell("Error: Invalid arguments to scram_client_final_message");
        return -1;
    }

    // Channel Binding: For IRC, this is typically "c=biws" (base64 of "n,,") meaning no channel binding.
    // If you were doing TLS-SRP, it would be different.
    const char *channel_binding_b64 = "c=biws"; // "n,," base64 encoded without padding

    // 1. Construct client-final-message-bare
    // Format: c=base64_encode(channel-binding),r=client-nonce+server-nonce
    int written = snprintf(state->client_final_message_bare, sizeof(state->client_final_message_bare),
                           "%s,r=%s%s", channel_binding_b64, state->client_nonce, state->server_nonce);
    if (written < 0 || (size_t)written >= sizeof(state->client_final_message_bare)) {
        yell("Error: Buffer too small for client-final-message-bare");
        return -1;
    }

    // 2. Construct AuthMessage
    // AuthMessage = client-first-message-bare + "," + server-first-message + "," + client-final-message-bare
    written = snprintf(state->auth_message, sizeof(state->auth_message),
                       "%s,%s,%s",
                       state->client_first_message_bare,
                       state->server_first_message,
                       state->client_final_message_bare);
    if (written < 0 || (size_t)written >= sizeof(state->auth_message)) {
        yell("Error: Buffer too small for AuthMessage");
        return -1;
    }
    if (x_debug & DEBUG_SCRAM)
    {
	yell("state->client_first_message_bare is %s", state->client_first_message_bare);
	yell("state->server_first_message is %s", state->server_first_message);
	yell("state->client_final_message_bare is %s", state->client_final_message_bare);
	yell("state->auth_message is %s", state->auth_message);
    }

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
    unsigned int client_key_len = 0;
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
    unsigned int stored_key_len = 0;
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
    unsigned int client_signature_len = 0;
    if (HMAC_Final(hmac_ctx, state->client_signature, &client_signature_len) == 0) {
        yell("Error: HMAC_Final for ClientSignature failed: %s", ERR_error_string(ERR_get_error(), NULL));
        goto err;
    }
    HMAC_CTX_free(hmac_ctx);
    hmac_ctx = NULL;

    // ClientProof = ClientKey XOR ClientSignature
    xor_buffers(state->client_proof, state->client_key, state->client_signature, SCRAM_KEY_LEN);

    // 4. Base64 encode ClientProof and append to client-final-message-bare
    char client_proof_b64[SCRAM_KEY_LEN * 2]; // Roughly 1.5x + padding for base64
    size_t encoded_proof_len = base64_encode(state->client_proof, SCRAM_KEY_LEN, client_proof_b64);
    if (encoded_proof_len == (size_t)-1) {
        yell("Error encoding client proof to base64");
        goto err;
    }

    written = snprintf(output_buffer, output_buffer_len, "%s,p=%s",
                       state->client_final_message_bare, client_proof_b64);
    if (written < 0 || (size_t)written >= output_buffer_len) {
        yell("Error: Output buffer too small or snprintf error for final client message");
        goto err;
    }

    return 0;

err:
    if (hmac_ctx) HMAC_CTX_free(hmac_ctx);
    if (md_ctx) EVP_MD_CTX_free(md_ctx);
    return -1;
}

static int	scram_verify_server_final_message(scram_state_t *state, const char *server_final_message) 
{
    if (!state || !server_final_message) {
        yell("Error: Invalid arguments to scram_verify_server_final_message");
        return -1;
    }

    char v_val_b64[SCRAM_KEY_LEN * 2]; // Base64 encoded server signature
    unsigned char received_server_signature[SCRAM_KEY_LEN];

    // 1. Parse 'v' (server-signature)
    if (parse_kv_pair(server_final_message, "v=", v_val_b64, sizeof(v_val_b64)) != 0) {
        yell("Error: Could not parse 'v' from server-final-message");
        return -1;
    }

    size_t decoded_sig_len = base64_decode(v_val_b64, received_server_signature, sizeof(received_server_signature));
    if (decoded_sig_len == (size_t)-1 || decoded_sig_len != SCRAM_KEY_LEN) {
        yell("Error: Base64 decoding of server signature failed or incorrect length");
        return -1;
    }

    // 2. Derive ServerKey and ServerSignature (client-side calculation)
    HMAC_CTX *hmac_ctx = NULL;

    // ServerKey = HMAC(SaltedPassword, "Server Key")
    hmac_ctx = HMAC_CTX_new();
    if (!hmac_ctx) {
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
    unsigned int server_key_len = 0;
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
    if (hmac_ctx) HMAC_CTX_free(hmac_ctx);
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
		char	output_buffer[512];

		if (!(scrambox_ = get_scrambox(from_server))) {
			yell("Error: Could not get scrambox for server %d", from_server);
			RETURN_EMPTY;
		}
		if (!my_stricmp(operation, "BEGIN")) {
			char *	username_;
			char *	password;

			GET_DWORD_ARG(username_, input);
			GET_DWORD_ARG(password, input);

			if (x_debug & DEBUG_SCRAM)
				yell("Calling scram_client_first_message with %s %s", username_, password);
			memset(output_buffer, 0, sizeof(output_buffer));
			if (scram_client_first_message(scrambox_, username_, password, output_buffer, sizeof(output_buffer))) {
				yell("Scrambox first message failed for server %d", from_server);
				RETURN_EMPTY;
			}
			if (x_debug & DEBUG_SCRAM)
				yell("first client message is %s", output_buffer);
			RETURN_FSTR(output_buffer);
		} else if (!my_stricmp(operation, "RESPONSE")) {
			/* If this is the first response, generate our final message */
			if (!*(scrambox_->client_final_message_bare))
			{
				if (scram_process_server_first_message(scrambox_, input)) {
					yell("Processing server first message failed.");
					RETURN_EMPTY;
				}
				if (x_debug & DEBUG_SCRAM)
					yell("Calling scram_client_final_message");
				memset(output_buffer, 0, sizeof(output_buffer));
				if (scram_client_final_message(scrambox_, output_buffer, sizeof(output_buffer))) {
					yell("Client final message failed.");
					RETURN_EMPTY;
				}
				if (x_debug & DEBUG_SCRAM)
					yell("Final client message is %s", output_buffer);
				RETURN_FSTR(output_buffer);
			}
			/* If this is the second response, validate it */
			else
			{
				if (x_debug & DEBUG_SCRAM)
					yell("Calling scram_verify_server_final_message with %s", input);
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




// Don't forget to include the headers and the main function with an example usage

/*
int main() {
    // Initialize OpenSSL's error reporting
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    scram_state_t scram_session;
    memset(&scram_session, 0, sizeof(scram_state_t)); // Clear the state

    char client_first_msg[512];
    char server_first_msg_simulated[] = "r=exampleclientnoncehere+serversrnjk,s=c2FsdHNraXA=,i=4096"; // Example salt "saltskip"
    char client_final_msg[1024];
    char server_final_msg_simulated[] = "v=rcxN6dD/J/a85y7N7GZ068j10qR71f2N1j2R0qZ90r52"; // Placeholder, calculate this properly for testing

    const char *username_ = "testuser";
    const char *password = "testpassword";

    printf("--- SCRAM Authentication Client Example ---");

    // Step 1: Client sends client-first-message-bare
    printf("Step 1: Generating client-first-message-bare...");
    if (scram_client_first_message(&scram_session, username_, password, client_first_msg, sizeof(client_first_msg)) != 0) {
        yell("Client first message failed.");
        return 1;
    }
    printf("Client First Message: %s", client_first_msg);
    // In a real client, you'd send this to the IRC server.

    // Simulate receiving server-first-message
    printf("Step 2: Processing server-first-message (simulated)...");
    if (scram_process_server_first_message(&scram_session, server_first_msg_simulated) != 0) {
        yell("Processing server first message failed.");
        return 1;
    }
    printf("Server First Message Processed. SaltedPassword derived.");

    // Step 3: Client sends client-final-message-bare
    printf("Step 3: Generating client-final-message-bare...");
    if (scram_client_final_message(&scram_session, client_final_msg, sizeof(client_final_msg)) != 0) {
        yell("Client final message failed.");
        return 1;
    }
    printf("Client Final Message: %s", client_final_msg);
    // In a real client, you'd send this to the IRC server.

    // Simulate receiving server-final-message
    printf("Step 4: Verifying server-final-message (simulated)...");
    // IMPORTANT: The server_final_msg_simulated above needs to be replaced
    // with a correctly calculated signature for "testuser", "testpassword",
    // and the salt/iterations used in server_first_msg_simulated.
    // For a real test, you'd use a SCRAM server or a known test vector.
    // For now, if you run this, it will likely fail the signature check unless
    // you manually generate the server_final_msg_simulated correctly.
    if (scram_verify_server_final_message(&scram_session, server_final_msg_simulated) != 0) {
        yell("Server final message verification failed. (This is expected with dummy signature)");
        // For a real test, this would indicate auth failure
    } else {
        printf("Server final message verified! Authentication successful.");
    }

    // Cleanup OpenSSL
    EVP_cleanup();
    ERR_free_strings();

    return 0;
}
*/

