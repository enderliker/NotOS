#ifndef NOTOS_VFS_H
#define NOTOS_VFS_H
#include "file_abi.h"
#define VFS_NODE_COUNT 24u
#define VFS_FILE_COUNT 16u
#define VFS_FILE_BYTES (64u*1024u)
#define VFS_PAGE_COUNT 64u
struct vfs_node;
/* Trusted, terminated kernel paths only. Open returns one owned node reference. */
int vfs_open(const char *path,u32 flags,u32 mode,struct vfs_node **result);
void vfs_release(struct vfs_node *node);
int vfs_read(struct vfs_node *node,u32 offset,void *buffer,u32 bytes);
int vfs_write(struct vfs_node *node,u32 offset,const void *buffer,u32 bytes);
int vfs_truncate(struct vfs_node *node,u32 bytes);
int vfs_unlink(const char *path);
int vfs_rename(const char *old_path,const char *new_path);
int vfs_stat(const char *path,struct notos_file_info *result);
void vfs_node_info(const struct vfs_node *node,struct notos_file_info *result);
u32 vfs_max_size(const struct vfs_node *node);
/* Copy a linked tmpfs entry by slot; return 1 for present, 0 for absent. */
int vfs_entry(u32 index, char name[24], u32 *bytes);
int vfs_mkdir(const char *path, u32 mode);
int vfs_rmdir(const char *path);
/* Dense direct-child index; 1 entry, 0 EOF or a negative ABI error. */
int vfs_directory_entry(const char *path, u32 index, struct notos_directory_entry *entry);
int vfs_readdir(struct vfs_node *node, u32 index, struct notos_directory_entry *entry);
void vfs_status(void);
struct vfs_counts { u32 nodes,files,pages,references; };
struct vfs_counts vfs_counts(void);
#endif
