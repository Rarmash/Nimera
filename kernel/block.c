#include <nimera/block.h>
#include <nimera/panic.h>

#define NIMERA_MAX_BLOCK_DEVICES 4U

static struct block_device *devices[NIMERA_MAX_BLOCK_DEVICES];
static unsigned int device_count;

void block_init(void)
{
	device_count = 0U;
}

enum block_result block_register(struct block_device *device)
{
	if (device == (struct block_device *)0 || device->name == (const char *)0 ||
		device->block_size == 0ULL || device->block_count == 0ULL ||
		device->read == (block_read_fn)0 || device->write == (block_write_fn)0) {
		return BLOCK_INVALID;
	}
	if (device_count == NIMERA_MAX_BLOCK_DEVICES) {
		return BLOCK_INVALID;
	}
	devices[device_count++] = device;
	return BLOCK_OK;
}

unsigned int block_count(void)
{
	return device_count;
}

const struct block_device *block_find(const char *name)
{
	for (unsigned int device_index = 0U; device_index < device_count;
	     ++device_index) {
		unsigned int index = 0U;
		while (devices[device_index]->name[index] != '\0' &&
		       name != (const char *)0 &&
		       devices[device_index]->name[index] == name[index]) {
			++index;
		}
		if (name != (const char *)0 &&
		    devices[device_index]->name[index] == '\0' && name[index] == '\0') {
			return devices[device_index];
		}
	}
	return (const struct block_device *)0;
}

const struct block_device *block_get(unsigned int index)
{
	return index < device_count ? devices[index] : (const struct block_device *)0;
}

enum block_result block_read(const struct block_device *device, u64 block,
				     void *buffer)
{
	if (device == (const struct block_device *)0 || buffer == (void *)0 ||
		block >= device->block_count) {
		return BLOCK_INVALID;
	}
	return device->read((struct block_device *)device, block, buffer);
}

enum block_result block_write(const struct block_device *device, u64 block,
				      const void *buffer)
{
	if (device == (const struct block_device *)0 || buffer == (const void *)0 ||
		block >= device->block_count) {
		return BLOCK_INVALID;
	}
	return device->write((struct block_device *)device, block, buffer);
}
