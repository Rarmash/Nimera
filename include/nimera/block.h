#ifndef NIMERA_BLOCK_H
#define NIMERA_BLOCK_H

#include <nimera/types.h>

enum block_result {
	BLOCK_OK = 0,
	BLOCK_NO_DEVICE = -1,
	BLOCK_INVALID = -2,
	BLOCK_IO_ERROR = -3
};

struct block_device;
typedef enum block_result (*block_read_fn)(struct block_device *, u64, void *);
typedef enum block_result (*block_write_fn)(struct block_device *, u64,
							const void *);

struct block_device {
	const char *name;
	u64 block_size;
	u64 block_count;
	void *private_data;
	block_read_fn read;
	block_write_fn write;
};

void block_init(void);
enum block_result block_register(struct block_device *device);
unsigned int block_count(void);
const struct block_device *block_get(unsigned int index);
const struct block_device *block_find(const char *name);
enum block_result block_read(const struct block_device *device, u64 block,
				     void *buffer);
enum block_result block_write(const struct block_device *device, u64 block,
				      const void *buffer);

#endif
