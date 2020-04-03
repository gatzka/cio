/*
 *  sha1.h
 *
 *  Description:
 *      This is the header file for code which implements the Secure
 *      Hashing Algorithm 1 as defined in FIPS PUB 180-1 published
 *      April 17, 1995.
 *
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *      Please read the file sha1.c for more information.
 *
 */

#ifndef CIO_SHA1_H
#define CIO_SHA1_H

#include <stdint.h>
/*
 * If you do not have the ISO standard stdint.h header file, then you
 * must typdef the following:
 *    name              meaning
 *  uint32_t         unsigned 32 bit integer
 *  uint8_t          unsigned 8 bit integer (i.e., unsigned char)
 *  int_least16_t    integer of >= 16 bits
 *
 */

enum {
	SHA_SUCCESS = 0,
	SHA_NULL,           /* Null pointer parameter */
	SHA_INPUT_TOO_LONG, /* input data too long */
	SHA_STATE_ERROR     /* called Input after Result */
};

enum { SHA1_HASH_SIZE = 20 };

/*
 *  This structure will hold context information for the SHA-1
 *  hashing operation
 */
typedef struct sha1_context {
	uint32_t intermediate_hash[SHA1_HASH_SIZE / 4]; /* Message Digest  */

	uint32_t length_low;  /* Message length in bits      */
	uint32_t length_high; /* Message length in bits      */

	/* Index into message block array   */
	int_least16_t message_block_index;
	uint8_t message_block[64]; /* 512-bit message blocks      */

	int computed;  /* Is the digest computed?         */
	int corrupted; /* Is the message digest corrupted? */
} sha1_context;

/*
 *  Function Prototypes
 */

int sha1_reset(sha1_context *context);
int sha1_input(sha1_context *context,
               const uint8_t *message_array,
               unsigned int length);
int sha1_result(sha1_context *context,
                uint8_t Message_Digest[SHA1_HASH_SIZE]);

#endif
