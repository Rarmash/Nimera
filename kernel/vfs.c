#include <nimera/panic.h>
#include <nimera/ramfs.h>
#include <nimera/version.h>
#include <nimera/vfs.h>

static struct vfs_node *root_node;

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
			if (current->parent != (struct vfs_node *)0) {
				current = current->parent;
			}
			continue;
		}
		error = vfs_lookup(current, component, &current);
		if (error != VFS_OK) {
			return error;
		}
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
	case VFS_BUSY: return "Cannot modify the current directory or its ancestor";
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
