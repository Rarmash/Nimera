#ifndef NIMERA_NIMFS_H
#define NIMERA_NIMFS_H

#include <nimera/block.h>
#include <nimera/vfs.h>

enum nimfs_result {
	NIMFS_OK = 0,
	NIMFS_UNFORMATTED = -1,
	NIMFS_CORRUPT = -2,
	NIMFS_NO_SPACE = -3,
	NIMFS_IO = -4
};

#define NIMFS_LABEL_MAX 31U

int nimfs_format(struct block_device *device);
int nimfs_format_labeled(struct block_device *device, const char *label);
int nimfs_mount(struct block_device *device);
int nimfs_mount_at(struct block_device *device, struct vfs_node *mountpoint);
int nimfs_mount_at_owned(struct block_device *device, struct vfs_node *mountpoint,
			 int owned_mountpoint);
int nimfs_volume_label(struct block_device *device, char *label, u64 capacity);
enum vfs_error nimfs_create_initial_tree(void);
const char *nimfs_error_string(int error);
u64 nimfs_free_blocks(void);
u64 nimfs_free_inodes(void);
u64 nimfs_total_blocks(void);

#endif
