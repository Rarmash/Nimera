#ifndef NIMERA_USER_H
#define NIMERA_USER_H

#define NIMERA_USER_STACK_SIZE (16U * 1024U)

void user_test_entry(void);
void user_fault_entry(void *address);
extern unsigned char user_stack[];

#endif
