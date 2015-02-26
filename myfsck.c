/*
 * Siyu Wei
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


int device;
struct ext2_super_block super_block;

int main(int argc, char *argv[]){
	/*
	 */

	int partition_num = -1;
	char *disk_name = NULL;
    

    int opt;
	while((opt = getopt(argc, argv, ":p:i")) != -1){
		switch(opt){
			case 'p':
				partition_num = atoi(optarg);
				break;
			case 'i':
				disk_name = argv[optind];
				break;
			default:
				break;
		}
	}

	if(partition_num == -1 || disk_name == NULL){
		printf("Usage: ./myfsck -p <partition number> -i /path/to/disk/image");
		return(64);
	}


	if ((device = open(disk_name, O_RDWR)) == -1) {
        perror("Could not open device file");
        exit(-1);
    }

	partition partitions[MAX_PARTITIONS];
	get_partitions(partitions, disk_name);

	if(partitions[partition_num - 1].num == 0){
		return(-1);
	}

	partition p = partitions[partition_num - 1];
	printf("0x%02X %d %d\n", p.type, p.start, p.length);

	
	//read the super block information into the global variable
	read_sectors(partitions[0].start + 2, 2, &super_block);
	
	printf("size of: %d\n", sizeof(struct ext2_inode));
	struct ext2_inode* root_node = get_inode(EXT2_ROOT_INO, partitions[0].start);
	
	

	//read the directories
	struct ext2_dir_entry_2 directories[256];
	read_entries(63, root_node, directories);

	int i;
	// for(i = 0; directories[i].file_type != 0; i++){
	// 	printf("inode: %d, length %d, name: %s\n", directories[i].inode, directories[i].rec_len,
	// 		directories[i].name);
	// }

	int count[super_block.s_inodes_count];
	dfs_directory(63, count, directories[0]);

	for(i = 0; i < 100; i++){
		printf("inode links count %d\n", count[i]);
	}

	int bit;
	for(i = 1; i < 11; i++){
		bit = get_bit_map(63, i);
		printf("bit map for node %d: %d\n", i, bit);
    }

	return 0;
}

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
