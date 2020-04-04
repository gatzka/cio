/*
 *  sha1.c
 *
 *  Description:
 *      This file implements the Secure Hashing Algorithm 1 as
 *      defined in FIPS PUB 180-1 published April 17, 1995.
 *
 *      The SHA-1, produces a 160-bit message digest for a given
 *      data stream.  It should take about 2**n steps to find a
 *      message with the same digest as a given message and
 *      2**(n/2) to find any two messages with the same digest,
 *      when n is the digest size in bits.  Therefore, this
 *      algorithm can serve as a means of providing a
 *      "fingerprint" for a message.
 *
 *  Portability Issues:
 *      SHA-1 is defined in terms of 32-bit "words".  This code
 *      uses <stdint.h> (included via "sha1.h" to define 32 and 8
 *      bit unsigned integer types.  If your C compiler does not
 *      support 32 bit unsigned integers, this code is not
 *      appropriate.
 *
 *  Caveats:
 *      SHA-1 is designed to work with messages less than 2^64 bits
 *      long.  Although SHA-1 allows a message digest to be generated
 *      for messages of any number of bits less than 2^64, this
 *      implementation only works with messages with a length that is
 *      a multiple of the size of an 8-bit character.
 *
 */

#include "sha1.h"

/*
 *  Define the SHA1 circular left shift macro
 */
#define SHA1_CIRCULAR_SHIFT(bits, word) \
	(((word) << (bits)) | ((word) >> (32U - (bits))))

/*
 *  SHA1Reset
 *
 *  Description:
 *      This function will initialize the SHA1Context in preparation
 *      for computing a new SHA1 message digest.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to reset.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int sha1_reset(sha1_context *context)
{
	if (!context) {
		return SHA_NULL;
	}

	context->length_low = 0;
	context->length_high = 0;
	context->message_block_index = 0;

	context->intermediate_hash[0] = 0x67452301;
	context->intermediate_hash[1] = 0xEFCDAB89;
	context->intermediate_hash[2] = 0x98BADCFE;
	context->intermediate_hash[3] = 0x10325476;
	context->intermediate_hash[4] = 0xC3D2E1F0;

	context->computed = 0;
	context->corrupted = 0;

	return SHA_SUCCESS;
}

/*
 *  sha1_process_message_block
 *
 *  Description:
 *      This function will process the next 512 bits of the message
 *      stored in the Message_Block array.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:

 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the
 *      names used in the publication.
 *
 *
 */
static void sha1_process_message_block(sha1_context *context)
{
	const uint32_t K[] = {/* Constants defined in SHA-1   */
	                      0x5A827999,
	                      0x6ED9EBA1,
	                      0x8F1BBCDC,
	                      0xCA62C1D6};
	int t;          /* Loop counter                */
	uint32_t temp;  /* Temporary word value        */
	uint32_t W[80]; /* Word sequence               */
	uint32_t A;     /* Word buffers                */
	uint32_t B;     /* Word buffers                */
	uint32_t C;     /* Word buffers                */
	uint32_t D;     /* Word buffers                */
	uint32_t E;     /* Word buffers                */

	/*
     *  Initialize the first 16 words in the array W
     */
	for (t = 0; t < 16; t++) {
		W[t] = (uint32_t)(context->message_block[t * 4]) << 24U;
		W[t] |= (uint32_t)(context->message_block[t * 4 + 1]) << 16U;
		W[t] |= (uint32_t)(context->message_block[t * 4 + 2]) << 8U;
		W[t] |= (uint32_t)(context->message_block[t * 4 + 3]);
	}

	for (t = 16; t < 80; t++) {
		W[t] = SHA1_CIRCULAR_SHIFT(1U, W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16]);
	}

	A = context->intermediate_hash[0];
	B = context->intermediate_hash[1];
	C = context->intermediate_hash[2];
	D = context->intermediate_hash[3];
	E = context->intermediate_hash[4];

	for (t = 0; t < 20; t++) {
		temp = SHA1_CIRCULAR_SHIFT(5U, A) +
		       ((B & C) | ((~B) & D)) + E + W[t] + K[0];
		E = D;
		D = C;
		C = SHA1_CIRCULAR_SHIFT(30U, B);

		B = A;
		A = temp;
	}

	for (t = 20; t < 40; t++) {
		temp = SHA1_CIRCULAR_SHIFT(5U, A) + (B ^ C ^ D) + E + W[t] + K[1];
		E = D;
		D = C;
		C = SHA1_CIRCULAR_SHIFT(30U, B);
		B = A;
		A = temp;
	}

	for (t = 40; t < 60; t++) {
		temp = SHA1_CIRCULAR_SHIFT(5U, A) +
		       ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
		E = D;
		D = C;
		C = SHA1_CIRCULAR_SHIFT(30U, B);
		B = A;
		A = temp;
	}

	for (t = 60; t < 80; t++) {
		temp = SHA1_CIRCULAR_SHIFT(5U, A) + (B ^ C ^ D) + E + W[t] + K[3];
		E = D;
		D = C;
		C = SHA1_CIRCULAR_SHIFT(30U, B);
		B = A;
		A = temp;
	}

	context->intermediate_hash[0] += A;
	context->intermediate_hash[1] += B;
	context->intermediate_hash[2] += C;
	context->intermediate_hash[3] += D;
	context->intermediate_hash[4] += E;

	context->message_block_index = 0;
}

/*
 *  sha1_pad_message
 *

 *  Description:
 *      According to the standard, the message must be padded to an even
 *      512 bits.  The first padding bit must be a '1'.  The last 64
 *      bits represent the length of the original message.  All bits in
 *      between should be 0.  This function will pad the message
 *      according to those rules by filling the Message_Block array
 *      accordingly.  It will also call the ProcessMessageBlock function
 *      provided appropriately.  When it returns, it can be assumed that
 *      the message digest has been computed.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to pad
 *      ProcessMessageBlock: [in]
 *          The appropriate SHA*ProcessMessageBlock function
 *  Returns:
 *      Nothing.
 *
 */
static void sha1_pad_message(sha1_context *context)
{
	/*
     *  Check to see if the current message block is too small to hold
     *  the initial padding bits and length.  If so, we will pad the
     *  block, process it, and then continue padding into a second
     *  block.
     */
	if (context->message_block_index > 55) {
		context->message_block[context->message_block_index++] = 0x80;
		while (context->message_block_index < 64) {
			context->message_block[context->message_block_index++] = 0;
		}

		sha1_process_message_block(context);

		while (context->message_block_index < 56) {
			context->message_block[context->message_block_index++] = 0;
		}
	} else {
		context->message_block[context->message_block_index++] = 0x80;
		while (context->message_block_index < 56) {

			context->message_block[context->message_block_index++] = 0;
		}
	}

	/*
     *  Store the message length as the last 8 octets
     */
	context->message_block[56] = (uint8_t)(context->length_high >> 24U);
	context->message_block[57] = (uint8_t)(context->length_high >> 16U);
	context->message_block[58] = (uint8_t)(context->length_high >> 8U);
	context->message_block[59] = (uint8_t)(context->length_high);
	context->message_block[60] = (uint8_t)(context->length_low >> 24U);
	context->message_block[61] = (uint8_t)(context->length_low >> 16U);
	context->message_block[62] = (uint8_t)(context->length_low >> 8U);
	context->message_block[63] = (uint8_t)(context->length_low);

	sha1_process_message_block(context);
}

/*
 *  sha1_result
 *
 *  Description:
 *      This function will return the 160-bit message digest into the
 *      Message_Digest array  provided by the caller.
 *      NOTE: The first octet of hash is stored in the 0th element,
 *            the last octet of hash in the 19th element.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to use to calculate the SHA-1 hash.
 *      Message_Digest: [out]
 *          Where the digest is returned.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int sha1_result(sha1_context *context,
                uint8_t Message_Digest[SHA1_HASH_SIZE])
{
	if ((!context) || (!Message_Digest)) {
		return SHA_NULL;
	}

	if (context->corrupted) {
		return context->corrupted;
	}

	if (!context->computed) {
		sha1_pad_message(context);
		for (uint_fast8_t i = 0; i < 64; ++i) {
			/* message may be sensitive, clear it out */
			context->message_block[i] = 0;
		}
		context->length_low = 0; /* and clear length */
		context->length_high = 0;
		context->computed = 1;
	}

	for (uint_fast8_t i = 0; i < SHA1_HASH_SIZE; ++i) {
		Message_Digest[i] = (uint8_t)(context->intermediate_hash[i >> 2U] >> 8 * (3 - (i & 0x03U)));
	}

	return SHA_SUCCESS;
}

/*
 *  sha1_input
 *
 *  Description:
 *      This function accepts an array of octets as the next portion
 *      of the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of
 *          the message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int sha1_input(sha1_context *context,
               const uint8_t *message_array,
               unsigned length)
{
	if (!length) {
		return SHA_SUCCESS;
	}

	if ((!context) || (!message_array)) {
		return SHA_NULL;
	}

	if (context->computed) {
		context->corrupted = SHA_STATE_ERROR;

		return SHA_STATE_ERROR;
	}

	if (context->corrupted) {
		return context->corrupted;
	}
	while ((length--) && (!context->corrupted)) {
		context->message_block[context->message_block_index++] =
		    (*message_array & 0xFFU);

		context->length_low += 8;
		if (context->length_low == 0) {
			context->length_high++;
			if (context->length_high == 0) {
				/* Message is too long */
				context->corrupted = 1;
			}
		}

		if (context->message_block_index == 64) {
			sha1_process_message_block(context);
		}

		message_array++;
	}

	return SHA_SUCCESS;
}
