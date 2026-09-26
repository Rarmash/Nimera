#ifndef NIMERA_VFS_H
#define NIMERA_VFS_H

#include <nimera/types.h>

#define VFS_NAME_MAX 31U
#define VFS_PATH_MAX 128U
#define VFS_VOLUME_LABEL_MAX 31U

enum vfs_node_type {
	VFS_NODE_FILE,
	VFS_NODE_DIRECTORY
};

enum vfs_error {
	VFS_OK,
	VFS_NOT_FOUND,
	VFS_NOT_DIRECTORY,
	VFS_IS_DIRECTORY,
	VFS_ALREADY_EXISTS,
	VFS_NO_MEMORY,
	VFS_INVALID_PATH,
	VFS_TOO_LARGE,
	VFS_NOT_EMPTY,
	VFS_BUSY,
	VFS_CROSS_DEVICE,
	VFS_NOT_MOUNTED
};

struct vfs_node;

struct vfs_operations {
	enum vfs_error (*lookup)(struct vfs_node *directory, const char *name,
				 struct vfs_node **result);
	enum vfs_error (*readdir)(struct vfs_node *directory, unsigned int index,
				  struct vfs_node **result);
	enum vfs_error (*mkdir)(struct vfs_node *directory, const char *name,
			       struct vfs_node **result);
	enum vfs_error (*create)(struct vfs_node *directory, const char *name,
				const char *contents, u64 size,
				struct vfs_node **result);
	enum vfs_error (*read)(struct vfs_node *file, char *buffer, u64 capacity,
			      u64 *size);
	enum vfs_error (*write)(struct vfs_node *file, const char *data, u64 size);
	enum vfs_error (*append)(struct vfs_node *file, const char *data, u64 size);
	enum vfs_error (*remove)(struct vfs_node *node);
	enum vfs_error (*rename)(struct vfs_node *node, const char *name);
	enum vfs_error (*move)(struct vfs_node *node, struct vfs_node *directory);
};

struct vfs_node {
	const char *name;
	enum vfs_node_type type;
	struct vfs_node *parent;
	void *private_data;
	const struct vfs_operations *operations;
};

void vfs_init(void);
void vfs_set_mount_info(const char *filesystem, const char *device);
const char *vfs_mount_filesystem(void);
const char *vfs_mount_device(void);
struct vfs_node *vfs_root(void);
enum vfs_error vfs_mount_root(struct vfs_node *root);
enum vfs_error vfs_mount_at(struct vfs_node *mountpoint,
				struct vfs_node *root, const char *filesystem,
				const char *device, const char *label, int owned_mountpoint);
int vfs_node_is_mountpoint(const struct vfs_node *node);
int vfs_same_mount(const struct vfs_node *left, const struct vfs_node *right);
unsigned int vfs_mount_count(void);
enum vfs_error vfs_mount_path(unsigned int index, char *buffer, u64 capacity);
const char *vfs_mount_filesystem_at(unsigned int index);
const char *vfs_mount_device_at(unsigned int index);
const char *vfs_mount_label_at(unsigned int index);
enum vfs_error vfs_unmount_path(struct vfs_node *cwd, const char *path);
enum vfs_error vfs_resolve(struct vfs_node *cwd, const char *path,
			   struct vfs_node **result);
enum vfs_error vfs_mkdir(struct vfs_node *cwd, const char *path,
				struct vfs_node **result);
enum vfs_error vfs_create_file(struct vfs_node *cwd, const char *path,
			       const char *contents, u64 size,
			       struct vfs_node **result);
enum vfs_error vfs_lookup(struct vfs_node *directory, const char *name,
				  struct vfs_node **result);
enum vfs_error vfs_readdir(struct vfs_node *directory, unsigned int index,
				   struct vfs_node **result);
enum vfs_error vfs_read(struct vfs_node *file, char *buffer, u64 capacity,
			 u64 *size);
enum vfs_error vfs_touch(struct vfs_node *cwd, const char *path,
			 struct vfs_node **result);
enum vfs_error vfs_write(struct vfs_node *cwd, const char *path,
			 const char *data, u64 size, struct vfs_node **result);
enum vfs_error vfs_append(struct vfs_node *cwd, const char *path,
			  const char *data, u64 size, struct vfs_node **result);
enum vfs_error vfs_remove(struct vfs_node *cwd, const char *path);
enum vfs_error vfs_rmdir(struct vfs_node *cwd, const char *path);
enum vfs_error vfs_rename(struct vfs_node *cwd, const char *source,
			  const char *destination);
enum vfs_error vfs_format_path(const struct vfs_node *node, char *buffer,
			       u64 capacity);
const char *vfs_error_string(enum vfs_error error);
const char *vfs_node_name(const struct vfs_node *node);
struct vfs_node *vfs_node_parent(struct vfs_node *node);
enum vfs_node_type vfs_node_type(const struct vfs_node *node);

#endif
