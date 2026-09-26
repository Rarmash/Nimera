#include <nimera/panic.h>
#include <nimera/ramfs.h>
#include <nimera/version.h>
#include <nimera/vfs.h>

static struct vfs_node *root_node;
static const char *mount_filesystem;
static const char *mount_device;
#define VFS_MAX_MOUNTS 8U
struct vfs_mount_record {
	struct vfs_node *mountpoint;
	struct vfs_node *root;
	char path[VFS_PATH_MAX];
	char label[VFS_VOLUME_LABEL_MAX + 1U];
	const char *filesystem;
	const char *device;
	int owned_mountpoint;
};
static struct vfs_mount_record mounts[VFS_MAX_MOUNTS];
static unsigned int mount_count;
enum vfs_error vfs_format_path(const struct vfs_node *node, char *buffer,
			       u64 capacity);

static unsigned int vfs_string_length(const char *text)
{
	unsigned int length = 0U;

	while (text[length] != '\0') {
		++length;
	}
	return length;
}

static int vfs_string_equals(const char *left, const char *right)
{
	unsigned int index = 0U;

	while (left[index] != '\0' && right[index] != '\0') {
		if (left[index] != right[index]) {
			return 0;
		}
		++index;
	}
	return left[index] == '\0' && right[index] == '\0';
}

static enum vfs_error vfs_component(const char **path, char *component)
{
	unsigned int length = 0U;

	while (**path == '/') {
		++*path;
	}
	while (**path != '\0' && **path != '/') {
		if (length == VFS_NAME_MAX) {
			return VFS_INVALID_PATH;
		}
		component[length++] = **path;
		++*path;
	}
	component[length] = '\0';
	return VFS_OK;
}

void vfs_init(void)
{
	struct ramfs *filesystem = ramfs_create();
	struct vfs_node *node;
	mount_filesystem = "RAMFS";
	mount_device = (const char *)0;
	mount_count = 0U;
	static const char *directories[] = {
		"system", "apps", "users", "volumes", "devices", "config",
		"var", "tmp"
	};

	if (filesystem == (struct ramfs *)0 ||
	    vfs_mount_root(ramfs_root(filesystem)) != VFS_OK) {
		panic("unable to mount RAMFS root");
	}
	for (unsigned int index = 0U;
	     index < sizeof(directories) / sizeof(directories[0]); ++index) {
		if (vfs_mkdir(root_node, directories[index], &node) != VFS_OK) {
			panic("unable to create root directory");
		}
	}
	if (vfs_create_file(root_node, "/system/version", NIMERA_VERSION,
				    (u64)vfs_string_length(NIMERA_VERSION), &node) != VFS_OK) {
		panic("unable to create version file");
	}
}

void vfs_set_mount_info(const char *filesystem, const char *device)
{
	mount_filesystem = filesystem;
	mount_device = device;
	if (mount_count != 0U) {
		mounts[0].filesystem = filesystem;
		mounts[0].device = device;
	}
}

const char *vfs_mount_filesystem(void) { return mount_filesystem; }
const char *vfs_mount_device(void) { return mount_device; }

struct vfs_node *vfs_root(void)
{
	return root_node;
}

enum vfs_error vfs_mount_root(struct vfs_node *root)
{
	if (root == (struct vfs_node *)0 || root->type != VFS_NODE_DIRECTORY) {
		return VFS_INVALID_PATH;
	}
	root_node = root;
	root->parent = (struct vfs_node *)0;
	mount_count = 1U;
	mounts[0] = (struct vfs_mount_record){(struct vfs_node *)0, root, "/", "",
		"RAMFS", (const char *)0, 0};
	return VFS_OK;
}

enum vfs_error vfs_mount_at(struct vfs_node *mountpoint,
		struct vfs_node *root, const char *filesystem, const char *device,
		const char *label, int owned_mountpoint)
{
	if (mountpoint == (struct vfs_node *)0 || root == (struct vfs_node *)0 ||
		mountpoint->type != VFS_NODE_DIRECTORY || root->type != VFS_NODE_DIRECTORY ||
		mount_count == VFS_MAX_MOUNTS) {
		return VFS_INVALID_PATH;
	}
	root->parent = mountpoint;
	if (vfs_format_path(mountpoint, mounts[mount_count].path,
				 VFS_PATH_MAX - 1U) != VFS_OK) {
		return VFS_TOO_LARGE;
	}
	mounts[mount_count].mountpoint = mountpoint;
	mounts[mount_count].root = root;
	for (unsigned int i = 0U; i <= VFS_VOLUME_LABEL_MAX; ++i)
		mounts[mount_count].label[i] = '\0';
	if (label != (const char *)0)
		for (unsigned int i = 0U; i < VFS_VOLUME_LABEL_MAX && label[i] != '\0'; ++i)
			mounts[mount_count].label[i] = label[i];
	mounts[mount_count].filesystem = filesystem;
	mounts[mount_count].device = device;
	mounts[mount_count].owned_mountpoint = owned_mountpoint;
	++mount_count;
	return VFS_OK;
}

static struct vfs_node *vfs_follow_mount(struct vfs_node *node)
{
	char path[VFS_PATH_MAX];
	if (vfs_format_path(node, path, sizeof(path)) != VFS_OK) return node;
	for (unsigned int i = 1U; i < mount_count; ++i) {
		if (vfs_string_equals(mounts[i].path, path)) {
			return mounts[i].root;
		}
	}
	return node;
}

static struct vfs_node *vfs_mount_parent(const struct vfs_node *node)
{
	for (unsigned int i = 1U; i < mount_count; ++i) {
		if (mounts[i].root == node) {
			return mounts[i].mountpoint->parent;
		}
	}
	return (struct vfs_node *)0;
}

int vfs_node_is_mountpoint(const struct vfs_node *node)
{
	char path[VFS_PATH_MAX];
	if (vfs_format_path(node, path, sizeof(path)) != VFS_OK) return 0;
	for (unsigned int i = 1U; i < mount_count; ++i) {
		if (vfs_string_equals(mounts[i].path, path)) return 1;
	}
	return 0;
}

static int vfs_node_is_mount_root(const struct vfs_node *node)
{
	for (unsigned int i = 1U; i < mount_count; ++i)
		if (mounts[i].root == node) return 1;
	return 0;
}

static const struct vfs_mount_record *vfs_owner(const struct vfs_node *node)
{
	const struct vfs_node *cursor = node;
	while (cursor != (const struct vfs_node *)0) {
		for (unsigned int i = 0U; i < mount_count; ++i) {
			if (mounts[i].root == cursor) return &mounts[i];
		}
		cursor = cursor->parent;
	}
	return &mounts[0];
}

int vfs_same_mount(const struct vfs_node *left, const struct vfs_node *right)
{
	return vfs_owner(left) == vfs_owner(right);
}

unsigned int vfs_mount_count(void)
{
	return mount_count;
}

enum vfs_error vfs_mount_path(unsigned int index, char *buffer, u64 capacity)
{
	if (index >= mount_count) return VFS_NOT_FOUND;
	return vfs_format_path(mounts[index].root, buffer, capacity);
}

const char *vfs_mount_filesystem_at(unsigned int index)
{
	return index < mount_count ? mounts[index].filesystem : (const char *)0;
}

const char *vfs_mount_device_at(unsigned int index)
{
	return index < mount_count ? mounts[index].device : (const char *)0;
}

const char *vfs_mount_label_at(unsigned int index)
{
	return index < mount_count && mounts[index].label[0] != '\0' ?
		mounts[index].label : (const char *)0;
}

enum vfs_error vfs_unmount_path(struct vfs_node *cwd, const char *path)
{
	struct vfs_node *root;
	unsigned int index = 0U;
	struct vfs_mount_record removed;
	enum vfs_error error = vfs_resolve(cwd, path, &root);

	if (error != VFS_OK) return error;
	for (; index < mount_count; ++index)
		if (mounts[index].root == root) break;
	if (index == 0U) return VFS_BUSY;
	if (index == mount_count) return VFS_NOT_MOUNTED;
	if (vfs_same_mount(cwd, root)) return VFS_BUSY;
	removed = mounts[index];
	for (unsigned int i = index + 1U; i < mount_count; ++i) {
		unsigned int destination = i - 1U;
		mounts[destination].mountpoint = mounts[i].mountpoint;
		mounts[destination].root = mounts[i].root;
		for (unsigned int j = 0U; j < VFS_PATH_MAX; ++j)
			mounts[destination].path[j] = mounts[i].path[j];
		for (unsigned int j = 0U; j <= VFS_VOLUME_LABEL_MAX; ++j)
			mounts[destination].label[j] = mounts[i].label[j];
		mounts[destination].filesystem = mounts[i].filesystem;
		mounts[destination].device = mounts[i].device;
		mounts[destination].owned_mountpoint = mounts[i].owned_mountpoint;
	}
	--mount_count;
	root->parent = (struct vfs_node *)0;
	if (removed.owned_mountpoint != 0 && removed.mountpoint != (struct vfs_node *)0)
		return removed.mountpoint->operations->remove(removed.mountpoint);
	return VFS_OK;
}

enum vfs_error vfs_lookup(struct vfs_node *directory, const char *name,
				  struct vfs_node **result)
{
	if (directory == (struct vfs_node *)0 ||
	    directory->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	return directory->operations->lookup(directory, name, result);
}

enum vfs_error vfs_readdir(struct vfs_node *directory, unsigned int index,
				   struct vfs_node **result)
{
	if (directory == (struct vfs_node *)0 ||
	    directory->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	return directory->operations->readdir(directory, index, result);
}

enum vfs_error vfs_resolve(struct vfs_node *cwd, const char *path,
				   struct vfs_node **result)
{
	struct vfs_node *current;
	const char *cursor = path;
	char component[VFS_NAME_MAX + 1U];

	if (path == (const char *)0 || path[0] == '\0' ||
	    (cwd == (struct vfs_node *)0 && path[0] != '/')) {
		return VFS_INVALID_PATH;
	}
	current = path[0] == '/' ? root_node : cwd;
	for (;;) {
		enum vfs_error error = vfs_component(&cursor, component);

		if (error != VFS_OK) {
			return error;
		}
		if (component[0] == '\0') {
			*result = current;
			return VFS_OK;
		}
		if (vfs_string_equals(component, ".")) {
			continue;
		}
		if (vfs_string_equals(component, "..")) {
			struct vfs_node *parent = vfs_mount_parent(current);
			if (parent != (struct vfs_node *)0) {
				current = parent;
			} else if (current->parent != (struct vfs_node *)0) {
				current = current->parent;
			}
			continue;
		}
		error = vfs_lookup(current, component, &current);
		if (error != VFS_OK) {
			return error;
		}
		current = vfs_follow_mount(current);
	}
}

static enum vfs_error vfs_parent_and_name(struct vfs_node *cwd,
						 const char *path,
						 struct vfs_node **parent,
						 char *name)
{
	char copy[VFS_PATH_MAX];
	unsigned int length = vfs_string_length(path);
	unsigned int slash = 0U;
	unsigned int has_slash = 0U;
	unsigned int index;

	if (length == 0U || length >= VFS_PATH_MAX) {
		return VFS_INVALID_PATH;
	}
	if (path[length - 1U] == '/') {
		return length == 1U ? VFS_ALREADY_EXISTS : VFS_INVALID_PATH;
	}
	for (index = 0U; index <= length; ++index) {
		copy[index] = path[index];
		if (copy[index] == '/') {
			slash = index;
			has_slash = 1U;
		}
	}
	while (length != 0U && copy[length - 1U] == '/') {
		copy[--length] = '\0';
	}
	if (length == 0U) {
		return VFS_ALREADY_EXISTS;
	}
	if (has_slash == 0U) {
		for (index = 0U; index <= length; ++index) {
			name[index] = copy[index];
		}
		*parent = cwd;
		return VFS_OK;
	}
	for (index = slash + 1U; index <= length; ++index) {
		name[index - slash - 1U] = copy[index];
	}
	copy[slash] = '\0';
	if (slash == 0U) {
		*parent = root_node;
		return name[0] == '\0' ? VFS_INVALID_PATH : VFS_OK;
	}
	return vfs_resolve(cwd, copy, parent);
}

enum vfs_error vfs_mkdir(struct vfs_node *cwd, const char *path,
				struct vfs_node **result)
{
	char name[VFS_NAME_MAX + 1U];
	struct vfs_node *parent;
	enum vfs_error error = vfs_parent_and_name(cwd, path, &parent, name);

	if (error != VFS_OK) {
		return error;
	}
	if (vfs_string_length(name) > VFS_NAME_MAX) {
		return VFS_INVALID_PATH;
	}
	if (parent == (struct vfs_node *)0 ||
		parent->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	return parent->operations->mkdir(parent, name, result);
}

enum vfs_error vfs_create_file(struct vfs_node *cwd, const char *path,
				       const char *contents, u64 size,
				       struct vfs_node **result)
{
	char name[VFS_NAME_MAX + 1U];
	struct vfs_node *parent;
	enum vfs_error error = vfs_parent_and_name(cwd, path, &parent, name);

	if (error != VFS_OK) {
		return error;
	}
	if (parent == (struct vfs_node *)0 ||
		parent->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	return parent->operations->create(parent, name, contents, size, result);
}

enum vfs_error vfs_read(struct vfs_node *file, char *buffer, u64 capacity,
				u64 *size)
{
	if (file == (struct vfs_node *)0) {
		return VFS_NOT_FOUND;
	}
	return file->operations->read(file, buffer, capacity, size);
}

static int vfs_current_or_ancestor(struct vfs_node *node,
					   struct vfs_node *cwd)
{
	while (cwd != (struct vfs_node *)0) {
		if (cwd == node) {
			return 1;
		}
		cwd = cwd->parent;
	}
	return 0;
}

static enum vfs_error vfs_existing_or_parent(struct vfs_node *cwd,
					     const char *path,
					     struct vfs_node **node,
					     struct vfs_node **parent,
					     char *name)
{
	enum vfs_error error = vfs_resolve(cwd, path, node);

	if (error == VFS_OK) {
		return VFS_OK;
	}
	if (error != VFS_NOT_FOUND) {
		return error;
	}
	*node = (struct vfs_node *)0;
	return vfs_parent_and_name(cwd, path, parent, name);
}

enum vfs_error vfs_touch(struct vfs_node *cwd, const char *path,
				 struct vfs_node **result)
{
	struct vfs_node *node;
	struct vfs_node *parent;
	char name[VFS_NAME_MAX + 1U];
	enum vfs_error error = vfs_existing_or_parent(cwd, path, &node, &parent,
							      name);

	if (error == VFS_OK && node != (struct vfs_node *)0) {
		if (node->type == VFS_NODE_DIRECTORY) {
			return VFS_IS_DIRECTORY;
		}
		if (result != (struct vfs_node **)0) {
			*result = node;
		}
		return VFS_OK;
	}
	if (error != VFS_OK || parent->type != VFS_NODE_DIRECTORY) {
		return error != VFS_OK ? error : VFS_NOT_DIRECTORY;
	}
	return vfs_create_file(cwd, path, (const char *)0, 0ULL, result);
}

enum vfs_error vfs_write(struct vfs_node *cwd, const char *path,
			 const char *data, u64 size, struct vfs_node **result)
{
	struct vfs_node *node;
	enum vfs_error error = vfs_resolve(cwd, path, &node);

	if (error == VFS_NOT_FOUND) {
		return vfs_create_file(cwd, path, data, size, result);
	}
	if (error != VFS_OK) {
		return error;
	}
	if (node->type == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	error = node->operations->write(node, data, size);
	if (result != (struct vfs_node **)0) {
		*result = node;
	}
	return error;
}

enum vfs_error vfs_append(struct vfs_node *cwd, const char *path,
			  const char *data, u64 size, struct vfs_node **result)
{
	struct vfs_node *node;
	enum vfs_error error = vfs_resolve(cwd, path, &node);

	if (error == VFS_NOT_FOUND) {
		return vfs_create_file(cwd, path, data, size, result);
	}
	if (error != VFS_OK) {
		return error;
	}
	if (node->type == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	error = node->operations->append(node, data, size);
	if (result != (struct vfs_node **)0) {
		*result = node;
	}
	return error;
}

enum vfs_error vfs_remove(struct vfs_node *cwd, const char *path)
{
	struct vfs_node *node;
	enum vfs_error error = vfs_resolve(cwd, path, &node);

	if (error != VFS_OK) {
		return error;
	}
	if (node == root_node) {
		return VFS_INVALID_PATH;
	}
	if (vfs_node_is_mount_root(node) || vfs_node_is_mountpoint(node)) {
		return VFS_BUSY;
	}
	if (vfs_current_or_ancestor(node, cwd) != 0) {
		return VFS_BUSY;
	}
	if (node->type == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	return node->operations->remove(node);
}

enum vfs_error vfs_rmdir(struct vfs_node *cwd, const char *path)
{
	struct vfs_node *node;
	enum vfs_error error = vfs_resolve(cwd, path, &node);
	struct vfs_node *child;

	if (error != VFS_OK) {
		return error;
	}
	if (node == root_node) {
		return VFS_INVALID_PATH;
	}
	if (vfs_node_is_mount_root(node) || vfs_node_is_mountpoint(node)) {
		return VFS_BUSY;
	}
	if (vfs_current_or_ancestor(node, cwd) != 0) {
		return VFS_BUSY;
	}
	if (node->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	if (vfs_readdir(node, 0U, &child) == VFS_OK) {
		return VFS_NOT_EMPTY;
	}
	return node->operations->remove(node);
}

enum vfs_error vfs_rename(struct vfs_node *cwd, const char *source,
			  const char *destination)
{
	struct vfs_node *node;
	struct vfs_node *parent;
	struct vfs_node *existing;
	char name[VFS_NAME_MAX + 1U];
	enum vfs_error error = vfs_resolve(cwd, source, &node);

	if (error != VFS_OK) {
		return error;
	}
	if (node == root_node) {
		return VFS_INVALID_PATH;
	}
	if (vfs_node_is_mount_root(node) || vfs_node_is_mountpoint(node)) {
		return VFS_BUSY;
	}
	if (vfs_current_or_ancestor(node, cwd) != 0) {
		return VFS_BUSY;
	}
	error = vfs_parent_and_name(cwd, destination, &parent, name);
	if (error != VFS_OK) {
		return error;
	}
	if (parent == (struct vfs_node *)0 ||
		parent->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	if (!vfs_same_mount(node, parent)) {
		return VFS_CROSS_DEVICE;
	}
	if (vfs_lookup(parent, name, &existing) == VFS_OK) {
		return VFS_ALREADY_EXISTS;
	}
	if (node->type == VFS_NODE_DIRECTORY) {
		struct vfs_node *ancestor = parent;

		while (ancestor != (struct vfs_node *)0) {
			if (ancestor == node) {
				return VFS_INVALID_PATH;
			}
			ancestor = ancestor->parent;
		}
	}
	error = node->operations->rename(node, name);
	if (error != VFS_OK) {
		return error;
	}
	return node->operations->move(node, parent);
}

enum vfs_error vfs_format_path(const struct vfs_node *node, char *buffer,
			       u64 capacity)
{
	const struct vfs_node *parts[32];
	const struct vfs_node *current = node;
	u64 count = 0ULL;
	u64 length = 1ULL;

	if (node == (const struct vfs_node *)0 || capacity < 2ULL) {
		return VFS_INVALID_PATH;
	}
	while (current != root_node && current != (const struct vfs_node *)0) {
		if (vfs_node_is_mount_root(current)) {
			current = current->parent;
			continue;
		}
		if (count == 32ULL) {
			return VFS_TOO_LARGE;
		}
		parts[count++] = current;
		length += (u64)vfs_string_length(current->name) + 1ULL;
		current = current->parent;
	}
	if (length >= capacity) {
		return VFS_TOO_LARGE;
	}
	if (count == 0ULL) {
		buffer[0] = '/';
		buffer[1] = '\0';
		return VFS_OK;
	}
	buffer[0] = '/';
	length = 1ULL;
	while (count != 0ULL) {
		const char *name = parts[--count]->name;

		for (unsigned int index = 0U; name[index] != '\0'; ++index) {
			buffer[length++] = name[index];
		}
		if (count != 0ULL) {
			buffer[length++] = '/';
		}
	}
	buffer[length] = '\0';
	return VFS_OK;
}

const char *vfs_error_string(enum vfs_error error)
{
	switch (error) {
	case VFS_NOT_FOUND: return "No such file or directory";
	case VFS_NOT_DIRECTORY: return "Not a directory";
	case VFS_IS_DIRECTORY: return "Is a directory";
	case VFS_ALREADY_EXISTS: return "Already exists";
	case VFS_NO_MEMORY: return "Out of memory";
	case VFS_TOO_LARGE: return "Path or file is too large";
	case VFS_NOT_EMPTY: return "Directory is not empty";
	case VFS_BUSY: return "Volume is busy";
	case VFS_CROSS_DEVICE: return "Cannot move across filesystems";
	case VFS_NOT_MOUNTED: return "Volume is not mounted";
	default: return "Invalid path";
	}
}

const char *vfs_node_name(const struct vfs_node *node)
{
	return node->name;
}

struct vfs_node *vfs_node_parent(struct vfs_node *node)
{
	return node->parent;
}

enum vfs_node_type vfs_node_type(const struct vfs_node *node)
{
	return node->type;
}
