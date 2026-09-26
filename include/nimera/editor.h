#ifndef NIMERA_EDITOR_H
#define NIMERA_EDITOR_H

struct vfs_node;

int editor_run(struct vfs_node *cwd, const char *path);
void editor_self_test(struct vfs_node *cwd);

#endif
