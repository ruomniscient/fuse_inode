#ifndef INODE_H_INCLUDED
#define INODE_H_INCLUDED

#include "bitmap.h"

typedef int block_n;

#define INODE_INNER_BLOCKS 10
#define INODE_LINKS_PER_BLOCK (BLOCK_SIZE / sizeof(block_n))
#define INODE_EMPTY_BLOCK -1

typedef enum {INODE_FILE = 0, INODE_DIRECTORY = 1} inode_type;

struct inode_t{
    int id;
    inode_type type;
    int size, blocks_count;

    block_n blocks[INODE_INNER_BLOCKS];
    int direct_pointers;
    int indirect_pointers;
};
typedef struct inode_t inode_t;

inode_t *inode_make(bitmap_instance *bitmap);

#endif // INODE_H_INCLUDED
