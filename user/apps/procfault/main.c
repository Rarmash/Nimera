int main(void)
{
	volatile unsigned long long *kernel_address =
		(volatile unsigned long long *)(unsigned long)0x40000000ULL;
	*kernel_address = 0ULL;
	return 1;
}
