#ifndef UTILITY
#define UTILITY

#include <inttypes.h>
#include "ext2_fs.h"

struct ext2_inode *get_inode(int id, int start, int *address);
void read_disk(int64_t start_sector, unsigned int bytes_to_read, void *into);
void read_entries(int offset, struct ext2_inode *inode, struct ext2_dir_entry_2 *dirs, int *addrs);
int get_bit_map(int start, int inode);
void dfs_directory(int offset, int *count, struct ext2_dir_entry_2 dir, struct ext2_dir_entry_2 parent);
void repair_ilink_count(int offset, int* count);
void lost_found(int offset, int* count);
void i_mode_2_file_type(__u8 *type, int i_mode);

#endif