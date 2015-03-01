/* 
 * @author: Siyu Wei
 */

 #include "myfsck.h"
 #include "readwrite.h"
 #include "ext2_fs.h"
 #include "utility.h"
 #include <stdlib.h>
 #include <unistd.h>
 #include <string.h>
 #include <stdio.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <fcntl.h>

 #define SUPER_BLOCK_SIZE 2
 #define SUPER_BLOCK_OFFSET 2
 #define EXT2_TYPE 0x83
 #define MAX_DIRS 256

int device;
struct ext2_super_block super_block;

int main(int argc, char *argv[]){

	int partition_num = -1;
	int repair_partition_num = -1;
	char *disk_name = NULL;
    

	/*
	 * read in the arguments
	 */
    int opt;
	while((opt = getopt(argc, argv, "f:p:i")) != -1){
		switch(opt){
			case 'p':
				partition_num = atoi(optarg);
				break;
			case 'i':
				disk_name = argv[optind];
				break;
			case 'f':
				repair_partition_num = atoi(optarg);
			default:
				break;
		}
	}

	//if the input is not correct
	if((repair_partition_num == -1 && partition_num == -1) || disk_name == NULL){
		printf("Usage: ./myfsck -p <partition number> -i /path/to/disk/image");
		printf("Usage: ./myfsck -f <partition number> -i /path/to/disk/image");
		return(64);
	}

	//exit when disk opens failed
	if ((device = open(disk_name, O_RDWR)) == -1) {
        perror("Could not open device file");
        exit(-1);
    }


	partition partitions[MAX_PARTITIONS];
	get_partitions(partitions, disk_name);

	/*
	 * print partition information
	 */
	if(partition_num != -1){
		if(print_partitions(partitions, partition_num)){
			return -1;
		} else{
			return 0;
		}
	}

	

	/*
	 * do a fsck to check and repair the disk image
	 */
	int i;
	if(repair_partition_num != -1){
		if(repair_partition_num == 0){
			for(i = 0; i < MAX_PARTITIONS && partitions[i].num != 0; i++){
				if(partitions[i].type == EXT2_TYPE){
					repair_disk(partitions[i].start);
				}
			}
	    } else{
	    	//do a double scanning to better insure robustness
	    	repair_disk(partitions[repair_partition_num - 1].start);
	    	repair_disk(partitions[repair_partition_num - 1].start);
	    }
	}

	return 0;
}

/*
 * @disk_offset: the offset of the partition
 * check for errors for an ext2 file system and repair inconsistency
 */
void repair_disk(int disk_offset){

	//read in the super_block information
	read_sectors(disk_offset + SUPER_BLOCK_OFFSET, SUPER_BLOCK_SIZE, &super_block);
	int addr;
	struct ext2_inode* root_node = get_inode(EXT2_ROOT_INO, disk_offset, &addr);	

	//read the root directory
	struct ext2_dir_entry_2 directories[MAX_DIRS];
	int addrs[MAX_DIRS];
	read_entries(disk_offset, root_node, directories, addrs);

	//an array that counts the actual i link for each i_node
	int *count = (int*) calloc(super_block.s_inodes_count, sizeof(int));
	if(count == NULL) exit(1);

	//traverse all directories and files with a DFS approach
	dfs_directory(disk_offset, count, directories[0], directories[0]);
	//put the unreferenced inodes into the lost+found directory
	lost_found(disk_offset, count);
	//repair the i_links count
	repair_ilink_count(disk_offset, count);
	correct_bit_map(disk_offset);


}


/*
 * print the partition information
 */
int print_partitions(partition *partitions, int partition_num){
	if(partitions[partition_num - 1].num == 0){
		return(-1);
	}

	partition p = partitions[partition_num - 1];
	printf("0x%02X %d %d\n", p.type, p.start, p.length);

	return 0;
}

/*
 * read all the partitions information
 */
void get_partitions(partition *partitions, char *disk_name){
	unsigned char buf[sector_size_bytes];
	read_sectors(MBR_SECTOR, 1, buf);
	//print_sector(buf);


	int sector_num = 1;
	int i;
	unsigned char *point = buf + TABLE_OFFSET;

	/*
	 * Read the first 4 partitions from sector 0
	 */
	for(i = 0; i < MBR_NUM; i++){
		partitions[i].num = sector_num;
		partitions[i].type = point[4];
		partitions[i].start = *(unsigned int*)(point + 8);
		partitions[i].length = *(unsigned int*)(point + 12);
		sector_num++;
		point += ENTRY_SIZE;
	}

	/*
	 * find first EBR entry in sector 0, get the starting address of EBRs
	 */
	int ebr_start = 0;
	for(i = 0; i < sector_num - 1; i++){
		if(partitions[i].type == EXTENDED){
			ebr_start = partitions[i].start;
			break;
		}
	}

	//caculate the offset for the next partition and reads in the info
	int next_ebr = ebr_start, next_offset = 0;
	while(1){
		read_sectors(next_ebr, 1, buf);
		point = buf + TABLE_OFFSET;
		if(point[4] != EXTENDED){
			partitions[sector_num - 1].num = sector_num;
			partitions[sector_num - 1].type = point[4];
			partitions[sector_num - 1].start = next_ebr + *(unsigned int*)(point + 8);
			partitions[sector_num - 1].length = *(unsigned int*)(point + 12);
			sector_num++;
	    }

		point += ENTRY_SIZE;
		next_offset = *(unsigned int*)(point + 8);
		if(next_offset == 0){
			break;
		}
		next_ebr = ebr_start + next_offset;
		
	}


}
