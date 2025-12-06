#ifndef BP_INDEX_NODE_H
#define BP_INDEX_NODE_H
/* Στο αντίστοιχο αρχείο .h μπορείτε να δηλώσετε τις συναρτήσεις
 * και τις δομές δεδομένων που σχετίζονται με τους Κόμβους Δεδομένων.*/
#include "bf.h"
#include "record.h"
#include "bplus_file_structs.h"
#include "bplus_datanode.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#define NUM_OF_KEYS 4

typedef struct bplus_index_node Internal;

struct bplus_index_node
{
    IndexType type;                         // Internal Type
    int capacity;                           // Amount of keys the index has
    int keys[NUM_OF_KEYS];                  // Key's value
    BlockId children[NUM_OF_KEYS + 1];      // Children of the index (other internal indexes or leaf nodes), type BlockId for readability
};

//INTERFACE FUNCTIONS

int insert_into_parent(int fileDesc, BPlusMeta* metadata, BlockId leftBlockId, int key, BlockId rightBlockId, BlockId* parents, int parentCount);

int insertIndex(BPlusMeta* metadata, const Record *record, const int file_desc);

#endif