#ifndef NIMERA_UTF8_H
#define NIMERA_UTF8_H

#include <nimera/types.h>

#define UTF8_REPLACEMENT_CODEPOINT 0x0000fffdU

/* State survives between writes, so a split UTF-8 sequence is not lost. */
struct utf8_decoder {
	u32 value;
	u32 minimum;
	unsigned char expected;
	unsigned char seen;
};

void utf8_decoder_init(struct utf8_decoder *decoder);
/* Returns 0, 1, or 2 completed codepoints in output[]. */
unsigned int utf8_decoder_push(struct utf8_decoder *decoder,
				       unsigned char byte, u32 output[2]);
/* Emits U+FFFD only for an incomplete sequence at an explicit stream end. */
unsigned int utf8_decoder_flush(struct utf8_decoder *decoder, u32 output[1]);
int utf8_self_test(void);

#endif
