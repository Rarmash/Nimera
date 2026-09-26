#ifndef NIMERA_RAMFS_H
#define NIMERA_RAMFS_H

#include <nimera/vfs.h>

struct ramfs;

struct ramfs *ramfs_create(void);
struct vfs_node *ramfs_root(struct ramfs *filesystem);

#endif
