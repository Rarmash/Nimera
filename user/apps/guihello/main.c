#include <nimera/user.h>

static void put_pixel(u32 *pixels, u64 stride, u64 width, u64 height,
			     u64 x, u64 y, u32 color)
{
	if (x < width && y < height) pixels[y * stride + x] = color;
}

static void fill_rect(u32 *pixels, u64 stride, u64 width, u64 height,
		      u64 x, u64 y, u64 w, u64 h, u32 color)
{
	for (u64 row = 0ULL; row < h; ++row)
		for (u64 column = 0ULL; column < w; ++column)
			put_pixel(pixels, stride, width, height, x + column, y + row, color);
}

static void glyph(char character, unsigned char rows[7])
{
	static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
	for (unsigned int index = 0U; index < 7U; ++index) rows[index] = blank[index];
	switch (character) {
	case 'A': rows[0]=14; rows[1]=17; rows[2]=17; rows[3]=31; rows[4]=17; rows[5]=17; rows[6]=17; break;
	case 'E': rows[0]=31; rows[1]=16; rows[2]=16; rows[3]=30; rows[4]=16; rows[5]=16; rows[6]=31; break;
	case 'F': rows[0]=31; rows[1]=16; rows[2]=16; rows[3]=30; rows[4]=16; rows[5]=16; rows[6]=16; break;
	case 'H': rows[0]=17; rows[1]=17; rows[2]=17; rows[3]=31; rows[4]=17; rows[5]=17; rows[6]=17; break;
	case 'I': rows[0]=31; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=31; break;
	case 'L': rows[0]=16; rows[1]=16; rows[2]=16; rows[3]=16; rows[4]=16; rows[5]=16; rows[6]=31; break;
	case 'M': rows[0]=17; rows[1]=27; rows[2]=21; rows[3]=21; rows[4]=17; rows[5]=17; rows[6]=17; break;
	case 'P': rows[0]=30; rows[1]=17; rows[2]=17; rows[3]=30; rows[4]=16; rows[5]=16; rows[6]=16; break;
	case 'D': rows[0]=30; rows[1]=17; rows[2]=17; rows[3]=17; rows[4]=17; rows[5]=17; rows[6]=30; break;
	case 'e': rows[0]=0; rows[1]=14; rows[2]=17; rows[3]=31; rows[4]=16; rows[5]=17; rows[6]=14; break;
	case 'f': rows[0]=6; rows[1]=9; rows[2]=8; rows[3]=30; rows[4]=8; rows[5]=8; rows[6]=8; break;
	case 'h': rows[0]=16; rows[1]=16; rows[2]=22; rows[3]=25; rows[4]=17; rows[5]=17; rows[6]=17; break;
	case 'l': rows[0]=12; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=14; break;
	case 'm': rows[0]=0; rows[1]=26; rows[2]=21; rows[3]=21; rows[4]=21; rows[5]=21; rows[6]=21; break;
	case 'o': rows[0]=0; rows[1]=14; rows[2]=17; rows[3]=17; rows[4]=17; rows[5]=17; rows[6]=14; break;
	case 'r': rows[0]=0; rows[1]=22; rows[2]=25; rows[3]=16; rows[4]=16; rows[5]=16; rows[6]=16; break;
	case '0': rows[0]=14; rows[1]=17; rows[2]=19; rows[3]=21; rows[4]=25; rows[5]=17; rows[6]=14; break;
	case '1': rows[0]=4; rows[1]=12; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=14; break;
	case ':': rows[2]=4; rows[4]=4; break;
	default: break;
	}
}

static void draw_text(u32 *pixels, const struct nimera_window_info *window,
		      u64 x, u64 y, const char *text, u32 color)
{
	for (unsigned int index = 0U; text[index] != '\0'; ++index) {
		unsigned char rows[7];
		glyph(text[index], rows);
		for (unsigned int row = 0U; row < 7U; ++row)
			for (unsigned int column = 0U; column < 5U; ++column)
				if ((rows[row] & (1U << (4U - column))) != 0U)
					fill_rect(pixels, window->stride_pixels, window->width,
						window->height, x + index * 7ULL + column * 2ULL,
						y + row * 2ULL, 2ULL, 2ULL, color);
	}
}

static void decimal(char *buffer, unsigned long long value)
{
	char reversed[24];
	unsigned int count = 0U;
	if (value == 0ULL) { buffer[0] = '0'; buffer[1] = '\0'; return; }
	while (value != 0ULL) { reversed[count++] = (char)('0' + value % 10ULL); value /= 10ULL; }
	for (unsigned int index = 0U; index < count; ++index) buffer[index] = reversed[count - index - 1U];
	buffer[count] = '\0';
}

static void size_label(char *buffer, u64 width, u64 height)
{
	char first[24];
	char second[24];
	unsigned int index = 0U;
	decimal(first, width);
	decimal(second, height);
	for (unsigned int i = 0U; first[i] != '\0'; ++i) buffer[index++] = first[i];
	buffer[index++] = 'x';
	for (unsigned int i = 0U; second[i] != '\0'; ++i) buffer[index++] = second[i];
	buffer[index] = '\0';
}

static void redraw(u32 *pixels, const struct nimera_window_info *window,
	char *pid_text)
{
	char dimensions[48];
	fill_rect(pixels, window->stride_pixels, window->width, window->height,
		0ULL, 0ULL, window->width, window->height, 0x00101828U);
	fill_rect(pixels, window->stride_pixels, window->width, window->height,
		24ULL, 32ULL, 18ULL, 18ULL, 0x00e0b040U);
	draw_text(pixels, window, 72ULL, 42ULL, "Hello from EL0", 0x00ffffffU);
	draw_text(pixels, window, 72ULL, 78ULL, "PID: ", 0x00ffffffU);
	draw_text(pixels, window, 114ULL, 78ULL, pid_text, 0x00e0b040U);
	size_label(dimensions, window->width, window->height);
	draw_text(pixels, window, 72ULL, 114ULL, "SIZE: ", 0x00ffffffU);
	draw_text(pixels, window, 156ULL, 114ULL, dimensions, 0x00e0b040U);
}

int main(int argc, char **argv)
{
	struct nimera_window_info window;
	struct nimera_window_event event;
	char pid_text[24];
	long long handle;
	u32 *pixels;
	(void)argc; (void)argv;
	handle = nimera_window_create(480U, 300U, "Hello from EL0", 14ULL,
		NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE, &window);
	if (handle < 0LL) {
		const char message[] = "guihello: create failed\n";
		(void)nimera_write_console(message, sizeof(message) - 1ULL);
		return 1;
	}
	pixels = (u32 *)(unsigned long)window.client_address;
	decimal(pid_text, (unsigned long long)nimera_getpid());
	redraw(pixels, &window, pid_text);
	if (nimera_window_present((unsigned long long)handle, 0ULL, 0ULL,
		window.width, window.height) != 0) {
		const char message[] = "guihello: present failed\n";
		(void)nimera_write_console(message, sizeof(message) - 1ULL);
		return 1;
	}
	for (;;) {
		if (nimera_window_read_event((unsigned long long)handle, &event) != 0) {
			const char message[] = "guihello: event read failed\n";
			(void)nimera_write_console(message, sizeof(message) - 1ULL);
			break;
		}
		if (event.type == NIMERA_WINDOW_EVENT_CLOSE_REQUEST) break;
		if (event.type == NIMERA_WINDOW_EVENT_RESIZED) {
			window.client_address = event.client_address;
			window.width = event.client_width;
			window.height = event.client_height;
			window.stride_pixels = event.stride_pixels;
			pixels = (u32 *)(unsigned long)window.client_address;
			redraw(pixels, &window, pid_text);
			(void)nimera_window_present((unsigned long long)handle, 0ULL, 0ULL,
				window.width, window.height);
			continue;
		}
		if (event.type == NIMERA_WINDOW_KEY &&
			(event.ch == (u32)'q' || event.ch == (u32)'Q')) break;
		if (event.type == NIMERA_WINDOW_POINTER_BUTTON_DOWN) {
			fill_rect(pixels, window.stride_pixels, window.width, window.height,
				24ULL, 32ULL, 18ULL, 18ULL, 0x00ffffffU);
			(void)nimera_window_present((unsigned long long)handle,
				24ULL, 32ULL, 18ULL, 18ULL);
		}
	}
	(void)nimera_window_destroy((unsigned long long)handle);
	return 0;
}
