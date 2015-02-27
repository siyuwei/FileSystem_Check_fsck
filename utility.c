#include "utility.h"
#include "ext2_fs.h"
#include "readwrite.h"
#include "myfsck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* for memcpy() */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

#define DESCRIPTOR_TABLE_OFFSET 4
#define BLOCK_SIZE 1024
#define INODE_OFFSET 4
#define DIRECTORY_SIZE 8
#define UNKOWN 0
#define NAME_OFFSET 8
#define MAX_ENTRY_DIR 4096
#define THIS_DIR "."
#define PARENT_DIR ".."


static struct ext2_group_desc *desc_table = NULL;
static int partition_offset = -1;


/*
 * given an inode number and the partition offset, return the inode
 */

 struct ext2_inode *get_inode(int id, int start_sector, int *address){
 	struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));


 	//calculate the group the inode blongs to and its offset within the group
 	int group = (id - 1) / super_block.s_inodes_per_group;
 	int offset = (id - 1) % super_block.s_inodes_per_group;


    //if the descriptor table has not been initialized
    if(start_sector != partition_offset) {
        if(desc_table != NULL){
            free(desc_table);
        }
        partition_offset = start_sector;
     	int descriptor_table_size = sizeof(struct ext2_group_desc) * 
                                    (super_block.s_blocks_count / super_block.s_blocks_per_group + 1);
     	
        desc_table = malloc(descriptor_table_size);
     	read_disk((DESCRIPTOR_TABLE_OFFSET + start_sector) * sector_size_bytes, descriptor_table_size, desc_table);
    }


    *address = ( start_sector + desc_table[group].bg_inode_table * 2) * sector_size_bytes 
               + sizeof(struct ext2_inode) * offset;
    //read the inode
 	read_disk(*address, sizeof(struct ext2_inode), inode);
 	
    //leave free to the function that calls the procedure
 	return inode;


 }



/*
 * read arbitrary bytes from arbitrary offset
 */
void read_disk(int64_t offset, unsigned int bytes_to_read, void *into){
	ssize_t ret;
    int64_t lret;
   

    if ((lret = lseek64(device, offset, SEEK_SET)) != offset) {
        fprintf(stderr, "Seek to position %"PRId64" failed: "
                "returned %"PRId64"\n", offset, lret);
        exit(-1);
    }

    if ((ret = read(device, into, bytes_to_read)) != bytes_to_read) {
        fprintf(stderr, "Read byte %"PRId64" length %d failed: "
                "returned %"PRId64"\n", offset, bytes_to_read, ret);
        exit(-1);
    }
}


/*
 * write arbitrary number of bytes to an arbitrary offset
 */

void write_disk(int64_t offset, unsigned int bytes_to_write, void *buffer){
    ssize_t ret;
    int64_t lret;

    if ((lret = lseek64(device, offset, SEEK_SET)) != offset) {
        fprintf(stderr, "Seek to position %"PRId64" failed: "
                "returned %"PRId64"\n", offset, lret);
        exit(-1);
    }


    if ((ret = write(device, buffer, bytes_to_write)) != bytes_to_write) {
        fprintf(stderr, "Write sector %"PRId64" length %d failed: "
                "returned %"PRId64"\n", offset, bytes_to_write, ret);
        exit(-1);
    }
}



/*
 * read a ext2 file entry
 */
void read_entries(int start_sector, struct ext2_inode *inode, struct ext2_dir_entry_2 *dirs, int *addrs){
    int block_index = 0;
    int start = (start_sector + 2 * inode->i_block[block_index]) * sector_size_bytes;
    
    //read the head of the linked list
    read_disk(start, DIRECTORY_SIZE, dirs);
    int i = 0, offset = 0;
    addrs[i] = start;
    while(dirs[i].file_type != 0){
        
        //after we read the basic info, read in the name with the info
        read_disk(start + offset + NAME_OFFSET, dirs[i].name_len, dirs[i].name);
        dirs[i].name[dirs[i].name_len] = 0;

        //calculate the offset for next entry
        offset += dirs[i].rec_len;
        i++;
        
        //if reach the end of the block, search for entries in the next data block
        if(offset == BLOCK_SIZE){
            start = (start_sector + 2 * inode->i_block[++block_index]) * sector_size_bytes;
            offset = 0;
        }

        read_disk(start + offset, DIRECTORY_SIZE, &dirs[i]);
        addrs[i] = start + offset;
    }
}


/*
 * exhaust the directory structure with a DFS approach
 */
void dfs_directory(int offset, int *count, struct ext2_dir_entry_2 dir, struct ext2_dir_entry_2 parent){
    struct ext2_dir_entry_2 *dirs = malloc(sizeof(struct ext2_dir_entry_2) * MAX_ENTRY_DIR);

    int addr;
    struct ext2_inode *inode = get_inode(dir.inode, offset, &addr);

    //printf("Search directory: %s %d\n", dir.name, dir.inode);

    int addrs[MAX_ENTRY_DIR];
    read_entries(offset, inode, dirs, addrs);
    int i;
    for(i = 0; i < MAX_ENTRY_DIR && dirs[i].inode != 0; i++){
        //printf("name: %s inode: %d\n", dirs[i].name, dirs[i].inode - 1);
    

        if(!strcmp(dirs[i].name, THIS_DIR)){
            if(dirs[i].inode != dir.inode){
                printf("node %d should be %d\n", dirs[i].inode, dir.inode);
                dirs[i].inode = dir.inode;
                write_disk(addrs[i], DIRECTORY_SIZE, &dirs[i]);
            }
            count[dirs[i].inode - 1]++;
        } else if(!strcmp(dirs[i].name, PARENT_DIR)) {
            if(dirs[i].inode != parent.inode){
                printf("node %d should be %d\n", dirs[i].inode, parent.inode);
                dirs[i].inode = parent.inode;
                write_disk(addrs[i], DIRECTORY_SIZE, &dirs[i]);
            }
            count[dirs[i].inode - 1]++;
        } else{
            count[dirs[i].inode - 1]++;
            if(dirs[i].file_type == EXT2_FT_DIR){
                dfs_directory(offset, count, dirs[i], dir);
            }
        }
    }

    free(dirs);
    free(inode);

}

/*
 * repair the corrupted number of links count of an inode
 */
void repair_ilink_count(int offset, int* count){
    int i;

    int addr;
    for(i = 0; i < super_block.s_inodes_count; i++){
        if(count[i] != 0){
            struct ext2_inode *inode = get_inode(i + 1, offset, &addr);
            if(inode->i_links_count != count[i]){
                printf("Inconsistency inode count: %d, real count: %d, node: %d\n", 
                    inode->i_links_count, count[i], i + 1);
                inode->i_links_count = count[i];
                write_disk(addr, sizeof(struct ext2_inode), inode);
                struct ext2_inode *new_node = get_inode(i + 1, offset, &addr);
                printf("new count: %d\n", new_node->i_links_count);
            }
        }
    }
}


/*
 * read the bit map 
 */
int get_bit_map(int start, int inode){

    int group = (inode - 1) / super_block.s_inodes_per_group;
    int offset = (inode - 1) % super_block.s_inodes_per_group;

    int descriptor_table_size = sizeof(struct ext2_group_desc) * super_block.s_blocks_count / super_block.s_blocks_per_group;
    struct ext2_group_desc *desc = malloc(descriptor_table_size);
    read_disk((DESCRIPTOR_TABLE_OFFSET + start) * sector_size_bytes, descriptor_table_size, desc);

    int num_byte = offset / 8;

    unsigned char bit_map;    
    read_disk((start + desc[group].bg_inode_bitmap * 2) * sector_size_bytes + num_byte, 1, &bit_map);

    free(desc);

    return (int)(bit_map >> (offset % 8) & 0x1);

}
