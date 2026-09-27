#define USER_TEXT __attribute__((section(".user.text")))
#define USER_RODATA __attribute__((section(".user.rodata")))
#define USER_DATA __attribute__((section(".user.data")))
#define USER_BSS __attribute__((section(".user.bss"), aligned(4096)))

extern long long nimera_write(const char *text, unsigned long long length);
extern void nimera_exit(long long status);

static const char user_hello[] USER_RODATA = "hello from userspace\n";
static const char user_done[] USER_RODATA = "userspace exit\n";
volatile unsigned long long user_counter USER_DATA;
unsigned char user_stack[16U * 1024U] USER_BSS;

void user_test_entry(void) USER_TEXT;
void user_fault_entry(void *address) USER_TEXT;

void user_test_entry(void)
{
	volatile unsigned long long first = 0x1122334455667788ULL;
	volatile unsigned long long second = 0x8877665544332211ULL;
	unsigned long long limit = 40000000ULL;

	(void)nimera_write(user_hello, sizeof(user_hello) - 1ULL);
	for (unsigned long long index = 0ULL; index < limit; ++index) {
		++user_counter;
		if (first != 0x1122334455667788ULL ||
		    second != 0x8877665544332211ULL) {
			nimera_exit(3LL);
		}
	}
	(void)nimera_write(user_done, sizeof(user_done) - 1ULL);
	nimera_exit(0LL);
}

void user_fault_entry(void *address)
{
	*(volatile unsigned long long *)address = 0xdeadbeefULL;
	nimera_exit(1LL);
}
