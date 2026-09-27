#include <nimera/user.h>

static unsigned long long length(const char *text)
{
	unsigned long long result = 0ULL;
	while (text[result] != '\0') ++result;
	return result;
}

static void say(const char *text)
{
	(void)nimera_write(text, length(text));
}

int main(void)
{
	struct nimera_terminal_size size;
	void *first_allocation = nimera_alloc(1ULL);
	void *second_allocation = nimera_alloc(8193ULL);
	int allocation_ok = first_allocation != (void *)0 &&
		second_allocation != (void *)0;
	int invalid_free_ok = nimera_free((void *)(unsigned long)0x1f000000ULL) != 0;
	int free_ok = first_allocation != (void *)0 &&
		nimera_free(first_allocation) == 0 &&
		nimera_free(first_allocation) != 0 &&
		second_allocation != (void *)0 && nimera_free(second_allocation) == 0;
	int pointer_ok = nimera_terminal_size((struct nimera_terminal_size *)0) != 0;
	int partial_ok = nimera_terminal_size(
		(struct nimera_terminal_size *)(unsigned long)0x1ffffffcULL) != 0;
	int event_pointer_ok = nimera_read_key((struct nimera_key_event *)0) != 0;
	int coordinate_ok = nimera_terminal_move_cursor(25U, 0U) != 0 &&
		nimera_terminal_move_cursor(0U, 80U) != 0 &&
		nimera_terminal_move_cursor(0xffffffffU, 0xffffffffU) != 0;
	int visibility_ok = nimera_terminal_cursor_visible(2) != 0;

	if (nimera_terminal_size(&size) != 0) return 1;
	(void)size;
	say("Userspace terminal validation\r\n");
	say("bad user pointer: "); say(pointer_ok && partial_ok && event_pointer_ok ? "OK\r\n" : "FAIL\r\n");
	say("cursor validation: "); say(coordinate_ok ? "OK\r\n" : "FAIL\r\n");
	say("cursor visibility validation: "); say(visibility_ok ? "OK\r\n" : "FAIL\r\n");
	say("dynamic memory validation: "); say(allocation_ok && invalid_free_ok && free_ok ? "OK\r\n" : "FAIL\r\n");
	return (pointer_ok && partial_ok && event_pointer_ok && coordinate_ok &&
		visibility_ok && allocation_ok && invalid_free_ok && free_ok) ? 0 : 1;
}
