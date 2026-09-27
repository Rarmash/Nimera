#ifndef NIMERA_VIRTIO_H
#define NIMERA_VIRTIO_H

#include <nimera/types.h>

struct virtio_mmio_info {
	u64 base;
	u64 size;
	u64 interrupt;
};

unsigned int virtio_mmio_discover(struct virtio_mmio_info *infos,
					unsigned int capacity);
int virtio_block_init(void);
int virtio_gpu_init(void);
int virtio_input_init(void);
int virtio_input_available(void);
u64 virtio_input_interrupt(void);
void virtio_input_handle_irq(void);
int virtio_pointer_available(void);
u64 virtio_pointer_interrupt(void);
void virtio_pointer_handle_irq(void);
int virtio_input_self_test(void);

#endif
