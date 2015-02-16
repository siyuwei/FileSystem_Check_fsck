#ifndef MYFSCK
#define MYFSCK

#define TABLE_OFFSET 446
#define ENTRY_SIZE 16
#define MBR_NUM 4
#define MBR_SECTOR 0
#define EXTENDED 0x05
#define MAX_PARTITIONS 256

/* 
 * A partition which contains a number, type, start offset and length
 */
typedef struct {
	int num;
	unsigned char type;
	int start;
	int length;
} partition;

void get_partitions(partition *partitions, char *disk_name);

#endif