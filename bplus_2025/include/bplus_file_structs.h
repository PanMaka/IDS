//
// Created by theofilos on 11/4/25.
//

#ifndef BPLUS_BPLUS_FILE_STRUCTS_H
#define BPLUS_BPLUS_FILE_STRUCTS_H
#include "bf.h"
#include "record.h"

typedef enum type_of_index IndexType;

typedef struct bplus_metadata_block BPlusMeta;

typedef int BlockId;

// Identify if a node is internal (it has keys and children) or a leaf (it has Records inside)
enum type_of_index{
    INTERNAL,
    LEAF
};

struct bplus_metadata_block
{
    int totalBlocks;
    int indexBlocksCount;
    int dataBlocksCount;
    int maxRecordsInBlock;
    int height;
    BlockId rootBlockId;
    TableSchema schema;
};


#endif //BPLUS_BPLUS_FILE_STRUCTS_H