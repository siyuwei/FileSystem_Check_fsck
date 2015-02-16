#ifndef READWRITE_H
#define READWRITE_H

#include <inttypes.h>

#if defined(__FreeBSD__)
#define lseek64 lseek
#endif

/* linux: lseek64 declaration needed here to eliminate compiler warning. */
extern int64_t lseek64(int, int64_t, int);
const unsigned int sector_size_bytes;
extern int device;


void read_sectors (int64_t start_sector, unsigned int num_sectors, void *into);
void write_sectors (int64_t start_sector, unsigned int num_sectors, void *from);
void print_sector (unsigned char *buf);

#endif