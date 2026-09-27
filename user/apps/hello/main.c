#define USER_RODATA __attribute__((section(".rodata")))

extern long long nimera_write(const char *text, unsigned long long length);

static const char hello[] USER_RODATA = "hello from ELF userspace!\n";
static const char bss_ok[] USER_RODATA = "BSS zero: OK\n";
static volatile unsigned long long bss_probe;

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	(void)nimera_write(hello, sizeof(hello) - 1ULL);
	if (bss_probe == 0ULL)
		(void)nimera_write(bss_ok, sizeof(bss_ok) - 1ULL);
	return 0;
}
