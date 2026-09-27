#include <nimera/heap.h>
#include <nimera/ramfs.h>

struct ramfs_node {
	struct vfs_node vfs;
	struct ramfs_node *first_child;
	struct ramfs_node *next_sibling;
	char *contents;
	u64 size;
	u64 capacity;
};

struct ramfs {
	struct ramfs_node *root;
};

static enum vfs_error ramfs_lookup(struct vfs_node *directory,
					   const char *name,
					   struct vfs_node **result);
static enum vfs_error ramfs_readdir(struct vfs_node *directory,
					    unsigned int index,
					    struct vfs_node **result);
static enum vfs_error ramfs_mkdir(struct vfs_node *directory, const char *name,
					  struct vfs_node **result);
static enum vfs_error ramfs_read(struct vfs_node *file, char *buffer,
					 u64 capacity, u64 *size);
static enum vfs_error ramfs_read_at(struct vfs_node *file, u64 offset,
					char *buffer, u64 length, u64 *completed);
static enum vfs_error ramfs_write(struct vfs_node *file, const char *data,
					  u64 size);
static enum vfs_error ramfs_write_at(struct vfs_node *file, u64 offset,
					 const char *data, u64 length);
static enum vfs_error ramfs_append(struct vfs_node *file, const char *data,
					   u64 size);
static enum vfs_error ramfs_remove(struct vfs_node *node);
static enum vfs_error ramfs_rename(struct vfs_node *node, const char *name);
static enum vfs_error ramfs_move(struct vfs_node *node,
					 struct vfs_node *directory);
static enum vfs_error ramfs_create_file(struct vfs_node *directory,
						const char *name, const char *contents,
						u64 size, struct vfs_node **result);

static const struct vfs_operations ramfs_operations = {
	.lookup = ramfs_lookup,
	.readdir = ramfs_readdir,
	.mkdir = ramfs_mkdir,
	.create = ramfs_create_file,
	.read = ramfs_read,
	.read_at = ramfs_read_at,
	.write = ramfs_write,
	.write_at = ramfs_write_at,
	.append = ramfs_append,
	.release = (void (*)(struct vfs_node *))0,
	.remove = ramfs_remove,
	.rename = ramfs_rename,
	.move = ramfs_move
};

static unsigned int ramfs_string_length(const char *text)
{
	unsigned int length = 0U;

	while (text[length] != '\0') {
		++length;
	}
	return length;
}

static int ramfs_string_equals(const char *left, const char *right)
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

static struct ramfs_node *ramfs_from_vfs(struct vfs_node *node)
{
	return (struct ramfs_node *)(void *)node;
}

static enum vfs_error ramfs_make_node(struct ramfs_node *parent,
					      const char *name,
					      enum vfs_node_type type,
					      struct ramfs_node **result)
{
	unsigned int length = ramfs_string_length(name);
	struct ramfs_node *node;
	char *stored_name;

	if (length == 0U || length > VFS_NAME_MAX) {
		return VFS_INVALID_PATH;
	}
	if (parent != (struct ramfs_node *)0) {
		struct ramfs_node *child = parent->first_child;

		while (child != (struct ramfs_node *)0) {
			if (ramfs_string_equals(child->vfs.name, name)) {
				return VFS_ALREADY_EXISTS;
			}
			child = child->next_sibling;
		}
	}
	node = (struct ramfs_node *)kmalloc(sizeof(*node));
	stored_name = (char *)kmalloc((u64)length + 1ULL);
	if (node == (struct ramfs_node *)0 || stored_name == (char *)0) {
		if (node != (struct ramfs_node *)0) {
			kfree(node);
		}
		if (stored_name != (char *)0) {
			kfree(stored_name);
		}
		return VFS_NO_MEMORY;
	}
	for (unsigned int index = 0U; index < length; ++index) {
		stored_name[index] = name[index];
	}
	stored_name[length] = '\0';
	node->vfs.name = stored_name;
	node->vfs.type = type;
	node->vfs.parent = parent == (struct ramfs_node *)0 ?
		(struct vfs_node *)0 : &parent->vfs;
	node->vfs.private_data = node;
	node->vfs.operations = &ramfs_operations;
	node->first_child = (struct ramfs_node *)0;
	node->next_sibling = (struct ramfs_node *)0;
	node->contents = (char *)0;
	node->size = 0ULL;
	node->capacity = 0ULL;
	if (parent != (struct ramfs_node *)0) {
		struct ramfs_node *last = parent->first_child;

		if (last == (struct ramfs_node *)0) {
			parent->first_child = node;
		} else {
			while (last->next_sibling != (struct ramfs_node *)0) {
				last = last->next_sibling;
			}
			last->next_sibling = node;
		}
	}
	if (result != (struct ramfs_node **)0) {
		*result = node;
	}
	return VFS_OK;
}

struct ramfs *ramfs_create(void)
{
	struct ramfs *filesystem = (struct ramfs *)kmalloc(sizeof(*filesystem));
	struct ramfs_node *root;

	if (filesystem == (struct ramfs *)0 ||
	    ramfs_make_node((struct ramfs_node *)0, "/",
			     VFS_NODE_DIRECTORY, &root) != VFS_OK) {
		if (filesystem != (struct ramfs *)0) {
			kfree(filesystem);
		}
		return (struct ramfs *)0;
	}
	filesystem->root = root;
	return filesystem;
}

struct vfs_node *ramfs_root(struct ramfs *filesystem)
{
	return filesystem == (struct ramfs *)0 ? (struct vfs_node *)0 :
		&filesystem->root->vfs;
}

static enum vfs_error ramfs_lookup(struct vfs_node *directory,
					   const char *name,
					   struct vfs_node **result)
{
	struct ramfs_node *node;

	if (directory->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	node = ramfs_from_vfs(directory)->first_child;
	while (node != (struct ramfs_node *)0) {
		if (ramfs_string_equals(node->vfs.name, name)) {
			*result = &node->vfs;
			return VFS_OK;
		}
		node = node->next_sibling;
	}
	return VFS_NOT_FOUND;
}

static enum vfs_error ramfs_readdir(struct vfs_node *directory,
					    unsigned int index,
					    struct vfs_node **result)
{
	struct ramfs_node *node;

	if (directory->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	node = ramfs_from_vfs(directory)->first_child;
	while (node != (struct ramfs_node *)0 && index != 0U) {
		node = node->next_sibling;
		--index;
	}
	if (node == (struct ramfs_node *)0) {
		return VFS_NOT_FOUND;
	}
	if (result != (struct vfs_node **)0) {
		*result = &node->vfs;
	}
	return VFS_OK;
}

static enum vfs_error ramfs_mkdir(struct vfs_node *directory, const char *name,
					  struct vfs_node **result)
{
	if (directory->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	return ramfs_make_node(ramfs_from_vfs(directory), name,
				       VFS_NODE_DIRECTORY,
				       (struct ramfs_node **)result);
}

static enum vfs_error ramfs_read(struct vfs_node *file, char *buffer,
					 u64 capacity, u64 *size)
{
	struct ramfs_node *node;

	if (file->type == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	node = ramfs_from_vfs(file);
	if (capacity < node->size) {
		*size = node->size;
		return VFS_TOO_LARGE;
	}
	for (u64 index = 0ULL; index < node->size; ++index) {
		buffer[index] = node->contents[index];
	}
	*size = node->size;
	return VFS_OK;
}

static enum vfs_error ramfs_read_at(struct vfs_node *file, u64 offset,
					char *buffer, u64 length, u64 *completed)
{
	struct ramfs_node *node;
	u64 available;

	if (file->type == VFS_NODE_DIRECTORY) return VFS_IS_DIRECTORY;
	node = ramfs_from_vfs(file);
	if (offset > node->size) return VFS_INVALID_PATH;
	available = node->size - offset;
	if (length > available) length = available;
	for (u64 index = 0ULL; index < length; ++index)
		buffer[index] = node->contents[offset + index];
	*completed = length;
	return VFS_OK;
}

static enum vfs_error ramfs_resize(struct ramfs_node *node, u64 size)
{
	u64 capacity = node->capacity;
	char *contents;

	if (size <= capacity) {
		return VFS_OK;
	}
	capacity = capacity == 0ULL ? 16ULL : capacity;
	while (capacity < size) {
		if (capacity > (~0ULL / 2ULL)) {
			capacity = size;
			break;
		}
		capacity *= 2ULL;
	}
	contents = (char *)kmalloc(capacity);
	if (contents == (char *)0) {
		return VFS_NO_MEMORY;
	}
	for (u64 index = 0ULL; index < node->size; ++index) {
		contents[index] = node->contents[index];
	}
	if (node->contents != (char *)0) {
		kfree(node->contents);
	}
	node->contents = contents;
	node->capacity = capacity;
	return VFS_OK;
}

static enum vfs_error ramfs_write(struct vfs_node *file, const char *data,
					  u64 size)
{
	struct ramfs_node *node;
	enum vfs_error error;

	if (file->type == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	node = ramfs_from_vfs(file);
	error = ramfs_resize(node, size);
	if (error != VFS_OK) {
		return error;
	}
	for (u64 index = 0ULL; index < size; ++index) {
		node->contents[index] = data[index];
	}
	node->size = size;
	return VFS_OK;
}

static enum vfs_error ramfs_write_at(struct vfs_node *file, u64 offset,
					 const char *data, u64 length)
{
	struct ramfs_node *node;
	enum vfs_error error;
	u64 end;

	if (file->type == VFS_NODE_DIRECTORY) return VFS_IS_DIRECTORY;
	node = ramfs_from_vfs(file);
	if (offset > node->size) return VFS_INVALID_PATH;
	if (length > ~0ULL - offset) return VFS_TOO_LARGE;
	end = offset + length;
	error = ramfs_resize(node, end);
	if (error != VFS_OK) return error;
	for (u64 index = 0ULL; index < length; ++index)
		node->contents[offset + index] = data[index];
	if (end > node->size) node->size = end;
	return VFS_OK;
}

static enum vfs_error ramfs_append(struct vfs_node *file, const char *data,
					   u64 size)
{
	struct ramfs_node *node;
	u64 old_size;
	enum vfs_error error;

	if (file->type == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	node = ramfs_from_vfs(file);
	old_size = node->size;
	error = ramfs_resize(node, old_size + size);
	if (error != VFS_OK) {
		return error;
	}
	for (u64 index = 0ULL; index < size; ++index) {
		node->contents[old_size + index] = data[index];
	}
	node->size = old_size + size;
	return VFS_OK;
}

static void ramfs_unlink(struct ramfs_node *node)
{
	struct ramfs_node *parent = ramfs_from_vfs(node->vfs.parent);
	struct ramfs_node *previous = (struct ramfs_node *)0;
	struct ramfs_node *child = parent->first_child;

	while (child != node) {
		previous = child;
		child = child->next_sibling;
	}
	if (previous == (struct ramfs_node *)0) {
		parent->first_child = node->next_sibling;
	} else {
		previous->next_sibling = node->next_sibling;
	}
	node->next_sibling = (struct ramfs_node *)0;
}

static enum vfs_error ramfs_remove(struct vfs_node *node)
{
	struct ramfs_node *ram_node;

	ram_node = ramfs_from_vfs(node);
	if (node->type == VFS_NODE_DIRECTORY &&
		ram_node->first_child != (struct ramfs_node *)0) {
		return VFS_NOT_EMPTY;
	}
	ramfs_unlink(ram_node);
	if (ram_node->contents != (char *)0) {
		kfree(ram_node->contents);
	}
	kfree((void *)ram_node->vfs.name);
	kfree(ram_node);
	return VFS_OK;
}

static enum vfs_error ramfs_rename(struct vfs_node *node, const char *name)
{
	unsigned int length = ramfs_string_length(name);
	char *stored_name;

	if (length == 0U || length > VFS_NAME_MAX) {
		return VFS_INVALID_PATH;
	}
	stored_name = (char *)kmalloc((u64)length + 1ULL);
	if (stored_name == (char *)0) {
		return VFS_NO_MEMORY;
	}
	for (unsigned int index = 0U; index < length; ++index) {
		stored_name[index] = name[index];
	}
	stored_name[length] = '\0';
	kfree((void *)node->name);
	node->name = stored_name;
	return VFS_OK;
}

static enum vfs_error ramfs_move(struct vfs_node *node,
					 struct vfs_node *directory)
{
	struct ramfs_node *ram_node = ramfs_from_vfs(node);
	struct ramfs_node *parent = ramfs_from_vfs(directory);

	ramfs_unlink(ram_node);
	ram_node->vfs.parent = directory;
	ram_node->next_sibling = parent->first_child;
	parent->first_child = ram_node;
	return VFS_OK;
}

static enum vfs_error ramfs_create_file(struct vfs_node *directory,
						const char *name, const char *contents,
						u64 size, struct vfs_node **result)
{
	struct ramfs_node *node;
	enum vfs_error error;

	if (directory->type != VFS_NODE_DIRECTORY) {
		return VFS_NOT_DIRECTORY;
	}
	error = ramfs_make_node(ramfs_from_vfs(directory), name, VFS_NODE_FILE,
					&node);
	if (error != VFS_OK) {
		return error;
	}
	if (size != 0ULL && ramfs_resize(node, size) != VFS_OK) {
		ramfs_unlink(node);
		kfree((void *)node->vfs.name);
		kfree(node);
		return VFS_NO_MEMORY;
	}
	for (u64 index = 0ULL; index < size; ++index) {
		node->contents[index] = contents[index];
	}
	node->size = size;
	if (result != (struct vfs_node **)0) {
		*result = &node->vfs;
	}
	return VFS_OK;
}
