#include <nimera/utf8.h>

static void utf8_reset(struct utf8_decoder *decoder)
{
	decoder->value = 0U;
	decoder->minimum = 0U;
	decoder->expected = 0U;
	decoder->seen = 0U;
}

static int utf8_valid(u32 value, u32 minimum)
{
	return value >= minimum && value <= 0x0010ffffU &&
		!(value >= 0x0000d800U && value <= 0x0000dfffU);
}

void utf8_decoder_init(struct utf8_decoder *decoder) { utf8_reset(decoder); }

unsigned int utf8_decoder_push(struct utf8_decoder *decoder,
				       unsigned char byte, u32 output[2])
{
	unsigned int count = 0U;
	unsigned char current = byte;

	/* A bad continuation emits replacement, then the same byte starts anew. */
	for (;;) {
		if (decoder->expected == 0U) {
			if (current <= 0x7fU) {
				output[count++] = (u32)current;
				return count;
			}
			if (current >= 0xc2U && current <= 0xdfU) {
				decoder->value = (u32)(current & 0x1fU);
				decoder->minimum = 0x80U;
				decoder->expected = 1U;
				decoder->seen = 0U;
				return count;
			}
			if (current >= 0xe0U && current <= 0xefU) {
				decoder->value = (u32)(current & 0x0fU);
				decoder->minimum = 0x800U;
				decoder->expected = 2U;
				decoder->seen = 0U;
				return count;
			}
			if (current >= 0xf0U && current <= 0xf4U) {
				decoder->value = (u32)(current & 0x07U);
				decoder->minimum = 0x10000U;
				decoder->expected = 3U;
				decoder->seen = 0U;
				return count;
			}
			output[count++] = UTF8_REPLACEMENT_CODEPOINT;
			return count;
		}

		if (current < 0x80U || current > 0xbfU) {
			utf8_reset(decoder);
			output[count++] = UTF8_REPLACEMENT_CODEPOINT;
			if (count == 2U) return count;
			continue;
		}
		decoder->value = (decoder->value << 6) | (u32)(current & 0x3fU);
		++decoder->seen;
		if (decoder->seen != decoder->expected) return count;
		if (utf8_valid(decoder->value, decoder->minimum))
			output[count++] = decoder->value;
		else
			output[count++] = UTF8_REPLACEMENT_CODEPOINT;
		utf8_reset(decoder);
		return count;
	}
}

unsigned int utf8_decoder_flush(struct utf8_decoder *decoder, u32 output[1])
{
	if (decoder->expected == 0U) return 0U;
	output[0] = UTF8_REPLACEMENT_CODEPOINT;
	utf8_reset(decoder);
	return 1U;
}

static int utf8_expect(struct utf8_decoder *decoder, const unsigned char *bytes,
			       unsigned int count, u32 expected)
{
	u32 output[2];
	u32 actual = 0U;
	unsigned int output_count = 0U;
	for (unsigned int index = 0U; index < count; ++index) {
		unsigned int n = utf8_decoder_push(decoder, bytes[index], output);
		for (unsigned int item = 0U; item < n; ++item) actual = output[item];
		output_count += n;
	}
	return output_count == 1U && actual == expected;
}

int utf8_self_test(void)
{
	struct utf8_decoder decoder;
	unsigned char cent[] = {0xc2U, 0xa2U};
	unsigned char euro[] = {0xe2U, 0x82U, 0xacU};
	unsigned char smile[] = {0xf0U, 0x9fU, 0x99U, 0x82U};
	unsigned char split[] = {0xd0U, 0x9fU};
	unsigned char bad[] = {0xe2U, 0x28U};
	utf8_decoder_init(&decoder);
	if (!utf8_expect(&decoder, cent, 2U, 0x00a2U)) return 0;
	if (!utf8_expect(&decoder, euro, 3U, 0x20acU)) return 0;
	if (!utf8_expect(&decoder, smile, 4U, 0x1f642U)) return 0;
	if (!utf8_expect(&decoder, split, 2U, 0x041fU)) return 0;
	utf8_decoder_init(&decoder);
	{
		u32 output[2];
		if (utf8_decoder_push(&decoder, 0xe2U, output) != 0U) return 0;
		if (utf8_decoder_push(&decoder, 'x', output) != 2U ||
			output[0] != UTF8_REPLACEMENT_CODEPOINT || output[1] != 'x') return 0;
	}
	utf8_decoder_init(&decoder);
	{
		u32 output[2];
		if (utf8_decoder_push(&decoder, bad[0], output) != 0U) return 0;
		if (utf8_decoder_push(&decoder, bad[1], output) != 2U ||
			output[0] != UTF8_REPLACEMENT_CODEPOINT || output[1] != '(') return 0;
	}
	utf8_decoder_init(&decoder);
	{
		u32 output[2];
		if (utf8_decoder_push(&decoder, 0xc0U, output) != 1U ||
			output[0] != UTF8_REPLACEMENT_CODEPOINT) return 0;
		if (utf8_decoder_push(&decoder, 0xafU, output) != 1U ||
			output[0] != UTF8_REPLACEMENT_CODEPOINT) return 0;
	}
	return 1;
}
