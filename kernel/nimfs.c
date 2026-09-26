#include <nimera/heap.h>
#include <nimera/nimfs.h>
#include <nimera/panic.h>
#include <nimera/version.h>

#define NIMFS_MAGIC 0x53464d4eU
#define NIMFS_VERSION 1U
#define NIMFS_BLOCK_SIZE 512ULL
#define NIMFS_INODES 256U
#define NIMFS_INODE_SIZE 256U
#define NIMFS_DIRECT 60U
#define NIMFS_ENTRY_SIZE 64U
#define NIMFS_NAME_MAX 58U
#define NIMFS_FILE_MAX ((u64)NIMFS_DIRECT * NIMFS_BLOCK_SIZE)
#define NIMFS_MAX_BITMAP_BLOCKS 32U
typedef unsigned int u32;

#define INODE_FREE 0U
#define INODE_FILE 1U
#define INODE_DIRECTORY 2U

struct nimfs_inode {
  u32 type;
  u32 parent;
  u64 size;
  u32 blocks[NIMFS_DIRECT];
};

struct nimfs_node {
  struct vfs_node vfs;
  char name[VFS_NAME_MAX + 1U];
  u32 inode;
  struct nimfs_state *context;
};

struct nimfs_state {
  struct block_device *device;
  u64 total_blocks;
  u32 bitmap_start;
  u32 bitmap_blocks;
  u32 inode_start;
  u32 inode_blocks;
  u32 data_start;
  u32 root_inode;
  unsigned char *bitmap;
  struct nimfs_inode *inodes;
};

#define NIMFS_MAX_CONTEXTS 4U
static struct nimfs_state context_storage[NIMFS_MAX_CONTEXTS];
static struct nimfs_state *active_context = &context_storage[0];
#define fs (*active_context)
static unsigned int context_count;
static unsigned char io_buffer[NIMFS_BLOCK_SIZE];
static unsigned char bitmap_cache[NIMFS_MAX_CONTEXTS][NIMFS_MAX_BITMAP_BLOCKS * NIMFS_BLOCK_SIZE];
static struct nimfs_inode inode_cache[NIMFS_MAX_CONTEXTS][NIMFS_INODES];
static unsigned char append_buffer[NIMFS_FILE_MAX];
static const struct vfs_operations ops;
static unsigned int active_context_index(void);
static u32 le32(const unsigned char *p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u64 le64(const unsigned char *p) {
  return (u64)le32(p) | ((u64)le32(p + 4) << 32);
}
static void put32(unsigned char *p, u32 v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}
static void put64(unsigned char *p, u64 v) {
  put32(p, (u32)v);
  put32(p + 4, (u32)(v >> 32));
}
static void zero(void *p, u64 n) {
  unsigned char *x = p;
  for (u64 i = 0; i < n; ++i)
    x[i] = 0;
}
static int same(const char *a, const char *b) {
  u64 i = 0;
  while (a[i] && b[i] && a[i] == b[i])
    ++i;
  return a[i] == '\0' && b[i] == '\0';
}
static unsigned int length(const char *s) {
  unsigned int n = 0;
  while (s[n])
    ++n;
  return n;
}
static struct nimfs_node *node_of(struct vfs_node *n) {
  return (struct nimfs_node *)(void *)n;
}
static int valid_inode(u32 n) {
  return n < NIMFS_INODES && fs.inodes[n].type != INODE_FREE;
}
static int disk_read(u64 b, void *p) {
  return block_read(fs.device, b, p) == BLOCK_OK ? 0 : -1;
}
static int disk_write(u64 b, const void *p) {
  return block_write(fs.device, b, p) == BLOCK_OK ? 0 : -1;
}

static int save_bitmap_block(u32 block) {
  return disk_write(fs.bitmap_start + block,
                    fs.bitmap + (u64)block * NIMFS_BLOCK_SIZE);
}

static int save_inode(u32 number) {
  u32 sector = number / 2U;
  unsigned char *p = io_buffer;
  zero(p, NIMFS_BLOCK_SIZE);
  for (unsigned int i = 0; i < 2U; ++i) {
    u32 n = sector * 2U + i;
    struct nimfs_inode *in = &fs.inodes[n];
    unsigned char *q = p + i * NIMFS_INODE_SIZE;
    put32(q, in->type);
    put32(q + 4, in->parent);
    put64(q + 8, in->size);
    for (unsigned int j = 0; j < NIMFS_DIRECT; ++j)
      put32(q + 16 + j * 4U, in->blocks[j]);
  }
  return disk_write(fs.inode_start + sector, p);
}

static int load_inode_table(void) {
  fs.inodes = inode_cache[active_context_index()];
  for (u32 sector = 0; sector < fs.inode_blocks; ++sector) {
    if (disk_read(fs.inode_start + sector, io_buffer) != 0)
      return -1;
    for (unsigned int i = 0; i < 2U; ++i) {
      u32 n = sector * 2U + i;
      unsigned char *p = io_buffer + i * NIMFS_INODE_SIZE;
      fs.inodes[n].type = le32(p);
      fs.inodes[n].parent = le32(p + 4);
      fs.inodes[n].size = le64(p + 8);
      for (unsigned int j = 0; j < NIMFS_DIRECT; ++j)
        fs.inodes[n].blocks[j] = le32(p + 16 + j * 4U);
      if (fs.inodes[n].type > INODE_DIRECTORY)
        return -1;
    }
  }
  return 0;
}

static int alloc_inode(u32 *result) {
  for (u32 i = 1U; i < NIMFS_INODES; ++i)
    if (fs.inodes[i].type == INODE_FREE) {
      zero(&fs.inodes[i], sizeof(fs.inodes[i]));
      *result = i;
      return save_inode(i) == 0 ? 0 : -1;
    }
  return -1;
}
static int alloc_block(u32 *result) {
  for (u32 b = fs.data_start; b < fs.total_blocks; ++b) {
    u32 byte = b / 8U, bit = b % 8U;
    if ((fs.bitmap[byte] & (1U << bit)) == 0) {
      fs.bitmap[byte] |= (unsigned char)(1U << bit);
      if (save_bitmap_block(byte / NIMFS_BLOCK_SIZE) != 0)
        return -1;
      *result = b;
      return 0;
    }
  }
  return -1;
}
static int free_block(u32 b) {
  if (b < fs.data_start || b >= fs.total_blocks)
    return -1;
  fs.bitmap[b / 8U] &= (unsigned char)~(1U << (b % 8U));
  return save_bitmap_block((b / 8U) / NIMFS_BLOCK_SIZE);
}
static void init_entry(unsigned char *p, u32 inode, const char *name) {
  unsigned int n = length(name);
  zero(p, NIMFS_ENTRY_SIZE);
  put32(p, inode);
  p[4] = 1U;
  p[5] = (unsigned char)n;
  for (unsigned int i = 0; i < n; ++i)
    p[6 + i] = (unsigned char)name[i];
}
static u32 entry_inode(const unsigned char *p) { return le32(p); }
static int entry_valid(const unsigned char *p) { return p[4] != 0U; }
static void entry_name(const unsigned char *p, char *name) {
  unsigned int n = p[5];
  if (n > VFS_NAME_MAX)
    n = VFS_NAME_MAX;
  for (unsigned int i = 0; i < n; ++i)
    name[i] = (char)p[6 + i];
  name[n] = '\0';
}

static enum vfs_error make_node(struct nimfs_node *parent, const char *name,
                                u32 inode, struct vfs_node **result) {
  struct nimfs_node *node = (struct nimfs_node *)kmalloc(sizeof(*node));
  unsigned int n = length(name);
  if (node == (struct nimfs_node *)0)
    return VFS_NO_MEMORY;
  if (n > VFS_NAME_MAX) {
    kfree(node);
    return VFS_INVALID_PATH;
  }
  for (unsigned int i = 0; i <= n; ++i)
    node->name[i] = name[i];
  node->inode = inode;
  node->context = active_context;
  node->vfs.name = node->name;
  node->vfs.type = fs.inodes[inode].type == INODE_DIRECTORY ? VFS_NODE_DIRECTORY
                                                            : VFS_NODE_FILE;
  node->vfs.parent =
      parent == (struct nimfs_node *)0 ? (struct vfs_node *)0 : &parent->vfs;
  node->vfs.private_data = node;
  node->vfs.operations = &ops;
  if (result)
    *result = &node->vfs;
  return VFS_OK;
}

static enum vfs_error nf_lookup(struct vfs_node *, const char *,
                                struct vfs_node **);
static enum vfs_error nf_readdir(struct vfs_node *, unsigned int,
                                 struct vfs_node **);
static enum vfs_error nf_mkdir(struct vfs_node *, const char *,
                               struct vfs_node **);
static enum vfs_error nf_create(struct vfs_node *, const char *, const char *,
                                u64, struct vfs_node **);
static enum vfs_error nf_read(struct vfs_node *, char *, u64, u64 *);
static enum vfs_error nf_write(struct vfs_node *, const char *, u64);
static enum vfs_error nf_append(struct vfs_node *, const char *, u64);
static enum vfs_error nf_remove(struct vfs_node *);
static enum vfs_error nf_rename(struct vfs_node *, const char *);
static enum vfs_error nf_move(struct vfs_node *, struct vfs_node *);
static const struct vfs_operations ops = {
    nf_lookup, nf_readdir, nf_mkdir,  nf_create, nf_read,
    nf_write,  nf_append,  nf_remove, nf_rename, nf_move};

static struct vfs_node *root;
static struct nimfs_node root_nodes[NIMFS_MAX_CONTEXTS];
static unsigned int active_context_index(void) {
  return (unsigned int)(active_context - context_storage);
}
static struct nimfs_state *select_context(struct block_device *device) {
  for (unsigned int i = 0U; i < context_count; ++i)
    if (context_storage[i].device == device) return &context_storage[i];
  if (context_count == NIMFS_MAX_CONTEXTS) return (struct nimfs_state *)0;
  return &context_storage[context_count++];
}
static int dir_slot(struct nimfs_inode *dir, const char *name, u32 *block,
                    unsigned int *slot) {
  char current[VFS_NAME_MAX + 1U];
  for (unsigned int b = 0; b < NIMFS_DIRECT; ++b)
    if (dir->blocks[b] != 0U) {
      if (disk_read(dir->blocks[b], io_buffer) != 0)
        return -1;
      for (unsigned int s = 0; s < NIMFS_BLOCK_SIZE / NIMFS_ENTRY_SIZE; ++s)
        if (entry_valid(io_buffer + s * NIMFS_ENTRY_SIZE)) {
          entry_name(io_buffer + s * NIMFS_ENTRY_SIZE, current);
          if (same(current, name)) {
            *block = dir->blocks[b];
            *slot = s;
            return 0;
          }
        }
    }
  return -1;
}
static int add_entry(u32 dirno, u32 child, const char *name) {
  struct nimfs_inode *dir = &fs.inodes[dirno];
  if (length(name) > NIMFS_NAME_MAX)
    return -2;
  u32 block = 0;
  unsigned int slot = 0;
  if (dir_slot(dir, name, &block, &slot) == 0)
    return -2;
  for (unsigned int b = 0; b < NIMFS_DIRECT; ++b) {
    if (dir->blocks[b] == 0U) {
      if (alloc_block(&block) != 0)
        return -1;
      dir->blocks[b] = block;
      if (save_inode(dirno) != 0)
        return -1;
      zero(io_buffer, NIMFS_BLOCK_SIZE);
      slot = 0U;
      break;
    }
    if (disk_read(dir->blocks[b], io_buffer) != 0)
      return -1;
    for (slot = 0; slot < NIMFS_BLOCK_SIZE / NIMFS_ENTRY_SIZE; ++slot)
      if (!entry_valid(io_buffer + slot * NIMFS_ENTRY_SIZE)) {
        block = dir->blocks[b];
        goto found;
      }
  }
  return -1;
found:
  init_entry(io_buffer + slot * NIMFS_ENTRY_SIZE, child, name);
  return disk_write(block, io_buffer);
}
static int remove_entry(u32 dirno, const char *name) {
  u32 b;
  unsigned int s;
  if (dir_slot(&fs.inodes[dirno], name, &b, &s) != 0)
    return -1;
  if (disk_read(b, io_buffer) != 0)
    return -1;
  zero(io_buffer + s * NIMFS_ENTRY_SIZE, NIMFS_ENTRY_SIZE);
  return disk_write(b, io_buffer);
}

static enum vfs_error nf_lookup(struct vfs_node *d, const char *name,
                                struct vfs_node **result) {
  active_context = node_of(d)->context;
  struct nimfs_node *dir = node_of(d);
  u32 b;
  unsigned int s;
  if (d->type != VFS_NODE_DIRECTORY)
    return VFS_NOT_DIRECTORY;
  if (dir_slot(&fs.inodes[dir->inode], name, &b, &s) != 0)
    return VFS_NOT_FOUND;
  if (disk_read(b, io_buffer) != 0)
    return VFS_NOT_FOUND;
  u32 ino = entry_inode(io_buffer + s * NIMFS_ENTRY_SIZE);
  if (!valid_inode(ino))
    return VFS_INVALID_PATH;
  return make_node(dir, name, ino, result);
}
static enum vfs_error nf_readdir(struct vfs_node *d, unsigned int index,
                                 struct vfs_node **result) {
  active_context = node_of(d)->context;
  struct nimfs_node *dir = node_of(d);
  char name[VFS_NAME_MAX + 1U];
  if (d->type != VFS_NODE_DIRECTORY)
    return VFS_NOT_DIRECTORY;
  for (unsigned int b = 0; b < NIMFS_DIRECT; ++b)
    if (fs.inodes[dir->inode].blocks[b] != 0U) {
      if (disk_read(fs.inodes[dir->inode].blocks[b], io_buffer) != 0)
        return VFS_NOT_FOUND;
      for (unsigned int s = 0; s < NIMFS_BLOCK_SIZE / NIMFS_ENTRY_SIZE; ++s)
        if (entry_valid(io_buffer + s * NIMFS_ENTRY_SIZE)) {
          if (index == 0U) {
            entry_name(io_buffer + s * NIMFS_ENTRY_SIZE, name);
            return nf_lookup(d, name, result);
          }
          --index;
        }
    }
  return VFS_NOT_FOUND;
}
static enum vfs_error nf_mkdir(struct vfs_node *d, const char *name,
                               struct vfs_node **result) {
  active_context = node_of(d)->context;
  u32 ino, block, old_block;
  unsigned int old_slot;
  struct nimfs_node *dir = node_of(d);
  if (d->type != VFS_NODE_DIRECTORY)
    return VFS_NOT_DIRECTORY;
  if (length(name) == 0U || length(name) > VFS_NAME_MAX)
    return VFS_INVALID_PATH;
  if (dir_slot(&fs.inodes[dir->inode], name, &old_block, &old_slot) == 0)
    return VFS_ALREADY_EXISTS;
  if (alloc_inode(&ino) != 0 || alloc_block(&block) != 0)
    return VFS_NO_MEMORY;
  fs.inodes[ino].type = INODE_DIRECTORY;
  fs.inodes[ino].parent = dir->inode;
  fs.inodes[ino].blocks[0] = block;
  if (save_inode(ino) != 0)
    return VFS_NO_MEMORY;
  zero(io_buffer, NIMFS_BLOCK_SIZE);
  if (disk_write(block, io_buffer) != 0 ||
      add_entry(dir->inode, ino, name) != 0)
    return VFS_NO_MEMORY;
  return make_node(dir, name, ino, result);
}
static int replace_data(u32 ino, const char *data, u64 size) {
  struct nimfs_inode *in = &fs.inodes[ino];
  u32 fresh[NIMFS_DIRECT];
  unsigned int need = (unsigned int)((size + 511ULL) / 512ULL);
  if (size > NIMFS_FILE_MAX || need > NIMFS_DIRECT)
    return -2;
  for (unsigned int i = 0; i < need; ++i) {
    if (alloc_block(&fresh[i]) != 0)
      return -1;
    zero(io_buffer, 512);
    for (u64 j = 0; j < 512ULL && ((u64)i * 512ULL + j) < size; ++j)
      io_buffer[j] = (unsigned char)data[(u64)i * 512ULL + j];
    if (disk_write(fresh[i], io_buffer) != 0)
      return -1;
  }
  for (unsigned int i = need; i < NIMFS_DIRECT; ++i)
    fresh[i] = 0;
  for (unsigned int i = 0; i < NIMFS_DIRECT; ++i)
    if (in->blocks[i] != 0U)
      free_block(in->blocks[i]);
  for (unsigned int i = 0; i < NIMFS_DIRECT; ++i)
    in->blocks[i] = fresh[i];
  in->size = size;
  return save_inode(ino);
}
static enum vfs_error nf_create(struct vfs_node *d, const char *name,
                                const char *data, u64 size,
                                struct vfs_node **result) {
  active_context = node_of(d)->context;
  u32 ino, b;
  unsigned int slot;
  struct nimfs_node *dir = node_of(d);
  if (d->type != VFS_NODE_DIRECTORY)
    return VFS_NOT_DIRECTORY;
  if (length(name) == 0U || length(name) > VFS_NAME_MAX)
    return VFS_INVALID_PATH;
  if (dir_slot(&fs.inodes[dir->inode], name, &b, &slot) == 0)
    return VFS_ALREADY_EXISTS;
  if (size > NIMFS_FILE_MAX)
    return VFS_TOO_LARGE;
  if (alloc_inode(&ino) != 0)
    return VFS_NO_MEMORY;
  fs.inodes[ino].type = INODE_FILE;
  fs.inodes[ino].parent = dir->inode;
  if (save_inode(ino) != 0)
    return VFS_NO_MEMORY;
  if (size != 0U && replace_data(ino, data, size) != 0)
    return VFS_TOO_LARGE;
  if (add_entry(dir->inode, ino, name) != 0)
    return VFS_NO_MEMORY;
  return make_node(dir, name, ino, result);
}
static enum vfs_error nf_read(struct vfs_node *f, char *out, u64 cap,
                              u64 *size) {
  active_context = node_of(f)->context;
  struct nimfs_inode *in = &fs.inodes[node_of(f)->inode];
  if (f->type == VFS_NODE_DIRECTORY)
    return VFS_IS_DIRECTORY;
  if (cap < in->size) {
    *size = in->size;
    return VFS_TOO_LARGE;
  }
  for (u64 i = 0; i < in->size; ++i) {
    if (i % 512ULL == 0 && disk_read(in->blocks[i / 512ULL], io_buffer) != 0)
      return VFS_NOT_FOUND;
    out[i] = io_buffer[i % 512ULL];
  }
  *size = in->size;
  return VFS_OK;
}
static enum vfs_error nf_write(struct vfs_node *f, const char *d, u64 n) {
  active_context = node_of(f)->context;
  if (f->type == VFS_NODE_DIRECTORY)
    return VFS_IS_DIRECTORY;
  return replace_data(node_of(f)->inode, d, n) == 0 ? VFS_OK : VFS_TOO_LARGE;
}
static enum vfs_error nf_append(struct vfs_node *f, const char *d, u64 n) {
  active_context = node_of(f)->context;
  struct nimfs_inode *in = &fs.inodes[node_of(f)->inode];
  u64 size = in->size;
  if (f->type == VFS_NODE_DIRECTORY)
    return VFS_IS_DIRECTORY;
  if (size + n > NIMFS_FILE_MAX)
    return VFS_TOO_LARGE;
  if (nf_read(f, (char *)append_buffer, NIMFS_FILE_MAX, &size) != VFS_OK)
    return VFS_NOT_FOUND;
  for (u64 i = 0; i < n; ++i)
    append_buffer[size + i] = (unsigned char)d[i];
  return nf_write(f, (const char *)append_buffer, size + n);
}
static enum vfs_error nf_remove(struct vfs_node *f) {
  active_context = node_of(f)->context;
  struct nimfs_node *n = node_of(f);
  if (f->type == VFS_NODE_DIRECTORY) {
    for (unsigned int i = 0; i < NIMFS_DIRECT; ++i)
      if (fs.inodes[n->inode].blocks[i] != 0U)
        free_block(fs.inodes[n->inode].blocks[i]);
  } else
    for (unsigned int i = 0; i < NIMFS_DIRECT; ++i)
      if (fs.inodes[n->inode].blocks[i] != 0U)
        free_block(fs.inodes[n->inode].blocks[i]);
  if (remove_entry(node_of(f->parent)->inode, n->name) != 0)
    return VFS_NOT_FOUND;
  zero(&fs.inodes[n->inode], sizeof(fs.inodes[n->inode]));
  return save_inode(n->inode) == 0 ? VFS_OK : VFS_NOT_FOUND;
}
static enum vfs_error nf_rename(struct vfs_node *f, const char *name) {
  active_context = node_of(f)->context;
  struct nimfs_node *n = node_of(f);
  u32 b, old_b;
  unsigned int s, old_s;
  if (length(name) == 0U || length(name) > VFS_NAME_MAX)
    return VFS_INVALID_PATH;
  if (dir_slot(&fs.inodes[node_of(f->parent)->inode], name, &b, &s) == 0)
    return VFS_ALREADY_EXISTS;
  if (dir_slot(&fs.inodes[node_of(f->parent)->inode], n->name, &old_b,
               &old_s) != 0 ||
      disk_read(old_b, io_buffer) != 0)
    return VFS_NOT_FOUND;
  for (unsigned int i = 0; i < length(name); ++i)
    n->name[i] = name[i];
  n->name[length(name)] = '\0';
  io_buffer[old_s * NIMFS_ENTRY_SIZE + 5] = (unsigned char)length(name);
  for (unsigned int i = 0; i < length(name); ++i)
    io_buffer[old_s * NIMFS_ENTRY_SIZE + 6 + i] = (unsigned char)name[i];
  return disk_write(old_b, io_buffer) == 0 ? VFS_OK : VFS_NOT_FOUND;
}
static enum vfs_error nf_move(struct vfs_node *f, struct vfs_node *d) {
  active_context = node_of(f)->context;
  struct nimfs_node *n = node_of(f);
  if (node_of(d)->context != active_context)
    return VFS_CROSS_DEVICE;
  u32 ino = n->inode;
  if (remove_entry(node_of(f->parent)->inode, n->name) != 0 ||
      add_entry(node_of(d)->inode, ino, n->name) != 0)
    return VFS_NOT_FOUND;
  fs.inodes[ino].parent = node_of(d)->inode;
  if (save_inode(ino) != 0)
    return VFS_NOT_FOUND;
  f->parent = d;
  return VFS_OK;
}

static int validate_superblock(void) {
  if (le32(io_buffer) != NIMFS_MAGIC)
    return NIMFS_UNFORMATTED;
  if (le32(io_buffer + 4) != NIMFS_VERSION ||
      le32(io_buffer + 8) != NIMFS_BLOCK_SIZE)
    return NIMFS_CORRUPT;
  fs.total_blocks = le64(io_buffer + 12);
  fs.bitmap_start = le32(io_buffer + 20);
  fs.bitmap_blocks = le32(io_buffer + 24);
  fs.inode_start = le32(io_buffer + 28);
  fs.inode_blocks = le32(io_buffer + 32);
  fs.root_inode = le32(io_buffer + 36);
  fs.data_start = le32(io_buffer + 40);
  if (fs.total_blocks != fs.device->block_count ||
      fs.total_blocks > NIMFS_MAX_BITMAP_BLOCKS * 32768ULL ||
      fs.bitmap_start != 1U || fs.bitmap_blocks == 0U ||
      fs.bitmap_blocks > NIMFS_MAX_BITMAP_BLOCKS ||
      fs.inode_start != fs.bitmap_start + fs.bitmap_blocks ||
      fs.inode_blocks != 128U || le32(io_buffer + 44) != NIMFS_INODES ||
      le32(io_buffer + 48) != NIMFS_INODE_SIZE ||
      fs.data_start != fs.inode_start + fs.inode_blocks ||
      fs.data_start >= fs.total_blocks || fs.root_inode == 0U ||
      fs.root_inode >= NIMFS_INODES)
    return NIMFS_CORRUPT;
  return NIMFS_OK;
}

static int nimfs_mount_internal(struct block_device *device,
                                struct vfs_node *mountpoint) {
  if (device == 0 || device->block_size != NIMFS_BLOCK_SIZE)
    return NIMFS_CORRUPT;
  active_context = select_context(device);
  if (active_context == (struct nimfs_state *)0) return NIMFS_NO_SPACE;
  fs.device = device;
  if (disk_read(0, io_buffer) != 0)
    return NIMFS_IO;
  int result = validate_superblock();
  if (result != NIMFS_OK)
    return result;
  fs.bitmap = bitmap_cache[active_context_index()];
  if (load_inode_table() != 0)
    return NIMFS_CORRUPT;
  for (u32 i = 0; i < fs.bitmap_blocks; ++i)
    if (disk_read(fs.bitmap_start + i, fs.bitmap + (u64)i * NIMFS_BLOCK_SIZE) !=
        0)
      return NIMFS_IO;
  if (!valid_inode(fs.root_inode) ||
      fs.inodes[fs.root_inode].type != INODE_DIRECTORY)
    return NIMFS_CORRUPT;
  struct nimfs_node *root_node = &root_nodes[active_context_index()];
  zero(root_node, sizeof(*root_node));
  root_node->inode = fs.root_inode;
  root_node->context = active_context;
  root_node->name[0] = '/';
  root_node->vfs.name = root_node->name;
  root_node->vfs.type = VFS_NODE_DIRECTORY;
  root_node->vfs.private_data = root_node;
  root_node->vfs.operations = &ops;
  root = &root_node->vfs;
  if ((mountpoint == (struct vfs_node *)0 ? vfs_mount_root(root) :
       vfs_mount_at(mountpoint, root, "NimFS", device->name)) != VFS_OK)
    return NIMFS_CORRUPT;
  if (mountpoint == (struct vfs_node *)0) vfs_set_mount_info("NimFS", device->name);
  return NIMFS_OK;
}

int nimfs_mount(struct block_device *device) { return nimfs_mount_internal(device, (struct vfs_node *)0); }

int nimfs_mount_at(struct block_device *device, struct vfs_node *mountpoint) {
  return nimfs_mount_internal(device, mountpoint);
}

int nimfs_format(struct block_device *device) {
  if (device == 0 || device->block_size != NIMFS_BLOCK_SIZE ||
      device->block_count < 1024ULL)
    return NIMFS_CORRUPT;
  active_context = select_context(device);
  if (active_context == (struct nimfs_state *)0) return NIMFS_NO_SPACE;
  fs.device = device;
  fs.total_blocks = device->block_count;
  fs.bitmap_start = 1U;
  fs.bitmap_blocks = (u32)((fs.total_blocks + 32767ULL) / 32768ULL);
  if (fs.bitmap_blocks > NIMFS_MAX_BITMAP_BLOCKS)
    return NIMFS_NO_SPACE;
  fs.inode_start = 1U + fs.bitmap_blocks;
  fs.inode_blocks = 128U;
  fs.data_start = fs.inode_start + fs.inode_blocks;
  fs.root_inode = 1U;
  fs.bitmap = bitmap_cache[active_context_index()];
  fs.inodes = inode_cache[active_context_index()];
  zero(fs.bitmap, (u64)fs.bitmap_blocks * 512ULL);
  zero(fs.inodes, (u64)NIMFS_INODES * sizeof(struct nimfs_inode));
  for (u32 b = 0; b < fs.data_start; ++b)
    fs.bitmap[b / 8U] |= (unsigned char)(1U << (b % 8U));
  fs.inodes[1].type = INODE_DIRECTORY;
  fs.inodes[1].parent = 1U;
  u32 root_block;
  if (alloc_block(&root_block) != 0)
    return NIMFS_NO_SPACE;
  fs.inodes[1].blocks[0] = root_block;
  zero(io_buffer, 512);
  for (u32 b = 0; b < fs.bitmap_blocks; ++b)
    if (save_bitmap_block(b) != 0)
      return NIMFS_IO;
  for (u32 i = 0; i < fs.inode_blocks; ++i)
    if (disk_write(fs.inode_start + i, io_buffer) != 0)
      return NIMFS_IO;
  for (u32 i = 0; i < NIMFS_INODES; ++i)
    if (save_inode(i) != 0)
      return NIMFS_IO;
  zero(io_buffer, 512);
  if (disk_write(root_block, io_buffer) != 0)
    return NIMFS_IO;
  zero(io_buffer, 512);
  put32(io_buffer, NIMFS_MAGIC);
  put32(io_buffer + 4, NIMFS_VERSION);
  put32(io_buffer + 8, 512U);
  put64(io_buffer + 12, fs.total_blocks);
  put32(io_buffer + 20, fs.bitmap_start);
  put32(io_buffer + 24, fs.bitmap_blocks);
  put32(io_buffer + 28, fs.inode_start);
  put32(io_buffer + 32, fs.inode_blocks);
  put32(io_buffer + 36, fs.root_inode);
  put32(io_buffer + 40, fs.data_start);
  put32(io_buffer + 44, NIMFS_INODES);
  put32(io_buffer + 48, NIMFS_INODE_SIZE);
  if (disk_write(0, io_buffer) != 0)
    return NIMFS_IO;
  return NIMFS_OK;
}

enum vfs_error nimfs_create_initial_tree(void) {
  static const char *dirs[] = {"system",  "apps",   "users", "volumes",
                               "devices", "config", "var",   "tmp"};
  struct vfs_node *n;
  if (root == 0)
    return VFS_INVALID_PATH;
  for (unsigned int i = 0; i < 8U; ++i)
    if (vfs_mkdir(root, dirs[i], &n) != VFS_OK)
      return VFS_NO_MEMORY;
  return vfs_create_file(root, "/system/version", NIMERA_VERSION,
                         (u64)length(NIMERA_VERSION), &n);
}
const char *nimfs_error_string(int e) {
  return e == NIMFS_UNFORMATTED ? "NIMFS_UNFORMATTED"
         : e == NIMFS_CORRUPT   ? "NIMFS_CORRUPT"
         : e == NIMFS_NO_SPACE  ? "NIMFS_NO_SPACE"
                                : "NIMFS_IO";
}
u64 nimfs_total_blocks(void) { return fs.total_blocks; }
u64 nimfs_free_blocks(void) {
  u64 n = 0;
  for (u64 b = fs.data_start; b < fs.total_blocks; ++b)
    if ((fs.bitmap[b / 8U] & (1U << (b % 8U))) == 0)
      ++n;
  return n;
}
u64 nimfs_free_inodes(void) {
  u64 n = 0;
  for (unsigned int i = 1; i < NIMFS_INODES; ++i)
    if (fs.inodes[i].type == INODE_FREE)
      ++n;
  return n;
}
