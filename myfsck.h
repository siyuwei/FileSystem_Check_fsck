#ifndef MYFSCK
#define MYFSCK

#define TABLE_OFFSET 446
#define ENTRY_SIZE 16
#define MBR_NUM 4
#define MBR_SECTOR 0
#define EXTENDED 0x05
#define MAX_PARTITIONS 256
#define BLOCK_SIZE 1024

/* 
 * A partition which contains a number, type, start offset and length
 */
typedef struct {
	int num;
	unsigned char type;
	int start;
	int length;
} partition;

extern struct ext2_super_block super_block;
void get_partitions(partition *partitions, char *disk_name);

#endif