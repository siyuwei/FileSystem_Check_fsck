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
#define LOST_FOUND_NAME "lost+found"
#define MAX_FILES 256
#define I_MODE(X) *((unsigned char *)&X + 1) >> 4
#define DIRECTORY_MODE 0x4
#define DIRECT_BLOCKS 12
#define IN_DIRECT_ONE 12
#define IN_DIRECT_TWO 13
#define IN_DIRECT_THREE 14
#define NUM_INT_BLOCK 256


#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))



static struct ext2_group_desc *desc_table = NULL;
static int partition_offset = -1;
static int lost_found_index;
static int* blocks;

int get_block_bit_map(int start, int block);
void exhaust_data_blocks(int offset, int inode_index);
int* get_blocks(int block, int offset);
void write_block_bit_map(int start, int block, int value);


/* 
 * given an inode number and the partition offset, return the inode
 * @id: inode id
 * @start_sector: the starting sector of the logic partition
 * @address: this variable will be set to the disk address of the inode
 */

 struct ext2_inode *get_inode(int id, int start_sector, int *address){
 	struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
    if(inode == NULL) exit(1);


 	//calculate the group the inode belongs to and its offset within the group
 	int group = (id - 1) / super_block.s_inodes_per_group;
 	int offset = (id - 1) % super_block.s_inodes_per_group;

    int i, j;

    int inode_table_size = (super_block.s_inodes_per_group * sizeof(struct ext2_inode)) / BLOCK_SIZE;

    //if the descriptor table has not been initialized
    if(start_sector != partition_offset) {
        if(desc_table != NULL){
            free(desc_table);
        }
        partition_offset = start_sector;

        /*
         * read the descriptor group table from the disk
         */
        int num_block_groups = (super_block.s_blocks_count - 1)/ super_block.s_blocks_per_group + 1;
     	int descriptor_table_size = sizeof(struct ext2_group_desc) * num_block_groups;
        desc_table = malloc(descriptor_table_size);
        if(desc_table == NULL) exit(1);

     	read_disk((DESCRIPTOR_TABLE_OFFSET + start_sector) * sector_size_bytes, descriptor_table_size, desc_table);
        
        if(blocks != NULL){
            free(blocks);
        }

        //allocate an array to keep the blocks occupy info
        blocks = (int *)calloc(super_block.s_blocks_count, sizeof(int));
        if(blocks == NULL) exit(1);

        int start;
        /*
         * set the pre-allocated blocks such as bitmap, inode table, super_block
         */
        for(i = 0; i < num_block_groups; i++){
            start = super_block.s_blocks_per_group * i + 1;
            for(j = start; j <= desc_table[i].bg_inode_bitmap; j++){
                blocks[j]++;
            }
            for(j = desc_table[i].bg_inode_table; j <= desc_table[i].bg_inode_table + inode_table_size - 1; j++){
                blocks[j]++;
            }
        }

    }

    //calculate the address of the i_node
    *address = ( start_sector + desc_table[group].bg_inode_table * 2) * sector_size_bytes 
               + sizeof(struct ext2_inode) * offset;
    
    //read the inode
 	read_disk(*address, sizeof(struct ext2_inode), inode);
 	
    //leave free to the function that calls the procedure
 	return inode;


 }



/*
 * @offset: read offset
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
 * @offset: write offset
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
 * @start_sector: the starting sector of the partition
 * @inode: the inode of the dir
 * @dirs: the buffer for storing entries
 * @addrs: store the disk addresses of all these entries
 * Read the directory entries of a given directory as an array
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

        //read the next entry
        read_disk(start + offset, DIRECTORY_SIZE, &dirs[i]);
        addrs[i] = start + offset;
    }
}


/*
 * @count: the i_links count for each inode
 * @dir: starting dir
 * @parent: the parent dir of this dir
 * exhaust the directory structure with a DFS approach
 */
void dfs_directory(int offset, int *count, struct ext2_dir_entry_2 dir, struct ext2_dir_entry_2 parent){

    struct ext2_dir_entry_2 *dirs = malloc(sizeof(struct ext2_dir_entry_2) * MAX_ENTRY_DIR);
    if(dirs == NULL) exit(1);

    int addr;
    struct ext2_inode *inode = get_inode(dir.inode, offset, &addr);

    int addrs[MAX_ENTRY_DIR];
    read_entries(offset, inode, dirs, addrs);
    int i;
    for(i = 0; i < MAX_ENTRY_DIR && dirs[i].inode != 0; i++){

        //mark the inode number of lost+found for future use
        if(!strcmp(dirs[i].name, LOST_FOUND_NAME)) {
            lost_found_index = dirs[i].inode;
        }

        //get the data blocks indices of a given inode
        if(dirs[i].inode > 0 && dirs[i].inode < super_block.s_inodes_count){
            exhaust_data_blocks(offset, dirs[i].inode);
        }

        //if this entry points to the current dir
        if(!strcmp(dirs[i].name, THIS_DIR)){
            if(dirs[i].inode != dir.inode){
                printf("node %d should be %d\n", dirs[i].inode, dir.inode);
                dirs[i].inode = dir.inode;
                write_disk(addrs[i], DIRECTORY_SIZE, &dirs[i]);
            }
            count[dirs[i].inode - 1]++;
        }
        // if the entry points to the parent 
        else if(!strcmp(dirs[i].name, PARENT_DIR)) {
            if(parent.inode != 0){
                if(dirs[i].inode != parent.inode){
                    printf("node %d should be %d\n", dirs[i].inode, parent.inode);
                    dirs[i].inode = parent.inode;
                    write_disk(addrs[i], DIRECTORY_SIZE, &dirs[i]);
                }
            }
            count[dirs[i].inode - 1]++;

        } else{
            count[dirs[i].inode - 1]++;
            //recursively search if this is also a directory
            if(dirs[i].file_type == EXT2_FT_DIR){
                dfs_directory(offset, count, dirs[i], dir);
            }
        }
    }

    free(dirs);
    free(inode);

}

/* 
 * @offset: partition offset
*  @count: count of i links
 * repair the corrupted number of links count of an inode
 */
void repair_ilink_count(int offset, int* count){
    int i;

    int addr;
    for(i = 0; i < super_block.s_inodes_count; i++){
        if(count[i] != 0){
            struct ext2_inode *inode = get_inode(i + 1, offset, &addr);
            //if the i_links count in node differs of the real count
            if(inode->i_links_count != count[i]){
                printf("Inconsistency inode count: %d, real count: %d, node: %d\n", 
                    inode->i_links_count, count[i], i + 1);
                inode->i_links_count = count[i];
                write_disk(addr, sizeof(struct ext2_inode), inode);
            }
            free(inode);
        }
    }
}

/*
 * convert an i_mode to the corresponding file_type
 */
void i_mode_2_file_type(__u8 *type, int i_mode){
    switch(i_mode){
        case 0xC:
            *type = 6;
            break;
        case 0x8:
            *type = 1;
            break;
        case 0x4:
            *type = 2;
            break;
        default:
            break;
    }
}

/*
 * @offset: partition offset
 */
void correct_bit_map(int offset){
    int difference = 0;
    int i;

    for(i = 1; i < super_block.s_blocks_count; i++){
        //if the block is actually occupied but not marked in the bitmap
        if(blocks[i] > 0 && get_block_bit_map(offset, i) != 1){
            printf("blocks: %d not marked in bitmap\n", i);
            write_block_bit_map(offset, i, 1);
            difference++;
        }
        //if the block is not occupied but marked in the bitmap
        if(blocks[i] == 0 && get_block_bit_map(offset, i)){
            printf("blocks %d is incorrectly marked in bitmap\n", i);
            difference++;
            write_block_bit_map(offset, i, 0);
        }
    }
    if(difference > 0){
        printf("num of %d blocks differ\n", difference);
    }
}

/*
 * @offset: partition offset
 * @count: i links count
 * Find the unreferenced inodes and put them in the lost+found directory
 */
void lost_found(int offset, int* count){
    int i, j;
    struct ext2_dir_entry_2 new_entry;

    int addr_node;
    struct ext2_inode *inode;

    int *count_copy = calloc(super_block.s_inodes_count, sizeof(int));
    if(count_copy == NULL) exit(1);

    //copy the i links count
    memcpy(count_copy, count, super_block.s_inodes_count * sizeof(int));


    //go over all the missing files
    for(i = 12; i <= super_block.s_inodes_count; i++){
        if(get_bit_map(offset, i) && count[i - 1] == 0){

            struct ext2_dir_entry_2 lost_entries[MAX_FILES];
            int lost_addrs[MAX_FILES];
            inode = get_inode(i, offset, &addr_node);
            exhaust_data_blocks(offset, i);

            /*
             * if the found inode refers to a directory, reads in all 
             * the directory entries and mark them as found.
             */
            if(I_MODE(inode->i_mode) == DIRECTORY_MODE){
                read_entries(offset, inode, lost_entries, lost_addrs);

                //increase the count for every entry except the true root
                for(j = 0; j < MAX_FILES && lost_entries[j].inode != 0; j++){
                    if(strcmp(lost_entries[j].name, THIS_DIR) && strcmp(lost_entries[j].name, PARENT_DIR)){
                        count_copy[lost_entries[j].inode - 1]++;
                        printf("lost node %d found\n", lost_entries[j].inode);
                    }
                }
            }
            

        }
    }
    

    /*
     * find the missing files that is not referenced by another missing directory or file,
     * link them into the lost+found directory
     */ 
    for(i = 12; i <= super_block.s_inodes_count; i++){
        if(get_bit_map(offset, i) && count_copy[i - 1] == 0){

            printf("unreferenced inode: %d, %d\n", i, count[i - 1]);
            struct ext2_dir_entry_2 dirs[MAX_FILES];
            
            //get the unreferenced inode
            inode = get_inode(i, offset, &addr_node);

            //get the lost+found dir inode
            int addr_lost_found;
            struct ext2_inode *lost_found_node = get_inode(lost_found_index, offset, &addr_lost_found);
            
            //construct a new entry for the found inode
            sprintf(new_entry.name, "%d", i);
            i_mode_2_file_type(&new_entry.file_type, I_MODE(inode->i_mode));
            new_entry.inode = i;
            new_entry.name_len = strlen(new_entry.name);
            new_entry.rec_len 
                = (DIRECTORY_SIZE + new_entry.name_len) % 4 == 0 ?
                DIRECTORY_SIZE + new_entry.name_len :  ((DIRECTORY_SIZE + new_entry.name_len) / 4 + 1) * 4;

            //read the lost_found directory entries
            int addrs[MAX_FILES];
            read_entries(offset, lost_found_node, dirs, addrs);

            //skip the entries that are already occupied
            for(j = 0; dirs[j].file_type != 0; j++);
            struct ext2_dir_entry_2 lost_found_entry;
            memcpy(&lost_found_entry, dirs, DIRECTORY_SIZE);
            
            //write the entry for the unreferenced inode into the lost+found dir
            write_disk(addrs[j], new_entry.rec_len, &new_entry);
            //update the last entry for the directory and write it to the disk
            dirs[j].rec_len -= new_entry.rec_len;
            write_disk(addrs[j] + new_entry.rec_len, dirs[j].rec_len, dirs + j);

            /*
             * Manually update the i links count for the found file, if it's
             * a dir, dfs it and update the info for its descendants.
             */
            count[i - 1]++;
            if(I_MODE(inode->i_mode) == DIRECTORY_MODE){
                read_entries(offset, inode, dirs, addrs);
                dfs_directory(offset, count, dirs[0], lost_found_entry);
            }
            
            free(inode);
            free(lost_found_node);


        }
    }


}

/*
 * @start: partition start
 * read the group descriptor table for future use as this would be referred repeatedly
 */
void read_descriptor_table(int start){

    //if the tables has not already been read in
    if(start != partition_offset) {
        if(desc_table != NULL){
            free(desc_table);
        }
        partition_offset = start;
        int descriptor_table_size = sizeof(struct ext2_group_desc) * 
                                    ((super_block.s_blocks_count  -1 ) / super_block.s_blocks_per_group + 1);
        
        desc_table = malloc(descriptor_table_size);
        if(desc_table == NULL) exit(1);

        read_disk((DESCRIPTOR_TABLE_OFFSET ) * sector_size_bytes, descriptor_table_size, desc_table);
    }
}


/*
 * read the bit map for an inode from the bitmap
 */
int get_bit_map(int start, int inode){

    int group = (inode - 1) / super_block.s_inodes_per_group;
    int offset = (inode - 1) % super_block.s_inodes_per_group;

    read_descriptor_table(start);
    int num_byte = offset / 8;
    unsigned char bit_map;    
    read_disk((start + desc_table[group].bg_inode_bitmap * 2) * sector_size_bytes + num_byte, 1, &bit_map);


    return (int)(bit_map >> (offset % 8) & 0x1);

}

/*
 * read the bit map for a block from the block bitmap
 */
int get_block_bit_map(int start, int block){
    int group = (block - super_block.s_first_data_block)/ super_block.s_blocks_per_group;
    int offset = (block - super_block.s_first_data_block) % super_block.s_blocks_per_group;

    read_descriptor_table(start);
    int num_byte = offset / 8;
    unsigned char bit_map;
    read_disk((start + desc_table[group].bg_block_bitmap * 2) * sector_size_bytes + num_byte, 1, &bit_map);

    return (int)(bit_map >> (offset % 8) & 0x1);

}

/*
 * @value: the intended value
 * @block: the block index
 * write the intended block occupation info into the bit map.
 */
void write_block_bit_map(int start, int block, int value){
    int group = (block - super_block.s_first_data_block) / super_block.s_blocks_per_group;
    int offset = (block - super_block.s_first_data_block) % super_block.s_blocks_per_group;
    
    read_descriptor_table(start);
    int num_byte = offset / 8;
    unsigned char bit_map;
    read_disk((start + desc_table[group].bg_block_bitmap * 2) * sector_size_bytes + num_byte, 1, &bit_map);

    if(value == 1){
        bit_map |= (1 << (offset % 8));
    } else{
        bit_map &= (1 << (offset % 8) ^ -1);
    }

    write_disk((start + desc_table[group].bg_block_bitmap * 2) * sector_size_bytes + num_byte, 1, &bit_map);
}


/*
 * @inode_index the inode index
 * given an inode, read the indexes of all the data blocks occupied by the node
 */
void exhaust_data_blocks(int offset, int inode_index){

    int addr;
    struct ext2_inode *inode = get_inode(inode_index, offset, &addr);

    int block_count = inode->i_blocks / 2 + inode->i_blocks % 2;
    int i, j;

    //set the direct data blocks
    for(i = 0; i < MIN(block_count, DIRECT_BLOCKS); i++){
        blocks[inode->i_block[i]]++;
    }

    //set the level one indirect blocks
    if(inode->i_block[IN_DIRECT_ONE] != 0){
        blocks[inode->i_block[IN_DIRECT_ONE]]++;
        int* in_direct_1 = get_blocks(inode->i_block[IN_DIRECT_ONE], offset);
        for(i = 0; i < NUM_INT_BLOCK && in_direct_1[i] != 0; i++){
            blocks[in_direct_1[i]]++;
        }
    }

    //set the level 2 indirect blocks
    if(inode->i_block[IN_DIRECT_TWO] != 0){
        int* in_direct_2[NUM_INT_BLOCK];
        int* in_direct_2_blocks = get_blocks(inode->i_block[IN_DIRECT_TWO], offset);
        blocks[inode->i_block[IN_DIRECT_TWO]]++;

        for(i = 0; i < NUM_INT_BLOCK && in_direct_2_blocks[i] != 0; i++){
            blocks[in_direct_2_blocks[i]]++;
            in_direct_2[i] = get_blocks(in_direct_2_blocks[i], offset);    
        }

        int refers = i;

        for(i = 0; i < refers; i++){
            for(j = 0; j < NUM_INT_BLOCK; j++){
                if(in_direct_2[i][j] == 0){
                    return;
                }
                blocks[in_direct_2[i][j]]++;
            }
        }
    }



    free(inode);


}

/*
 * A helper method that reads in a block as 256 number of integers
 */
int *get_blocks(int block, int offset){
    int *num = (int *)calloc(NUM_INT_BLOCK, sizeof(int));
    if(num == NULL) exit(1);

    read_disk((offset + block * 2) * sector_size_bytes, BLOCK_SIZE, num);
    return num;
}


