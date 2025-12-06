#ifndef BP_DATANODE_H
#define BP_DATANODE_H
/* Στο αντίστοιχο αρχείο .h μπορείτε να δηλώσετε τις συναρτήσεις
 * και τις δομές δεδομένων που σχετίζονται με τους Κόμβους Δεδομένων.*/
#include "bf.h"
#include "record.h"
#include "bplus_file_structs.h"
#include "bplus_index_node.h"
#include <stdbool.h>

#define MAX_LEAF_RECORDS 5

typedef struct bplus_index_leaf Leaf;

struct bplus_index_leaf {
    IndexType type;                     // Must be LEAF
    int numRecords;                     // Current number of records stored
    BlockId nextLeaf;                   // Pointer to the next Leaf Block (for range queries)
    Record records[MAX_LEAF_RECORDS];   // The records inside the block
};

//HELPER FUNCTIONS

int split_leaf(int fileDesc, BPlusMeta* metadata, BlockId leftBlockId, Leaf* leftNode, const Record* newRecord, BlockId* parents, int parentCount);

BlockId find_Leaf(int fileDesc, BlockId rootBlockId, int key);

bool insert_into_leaf_simple(Leaf *leaf, const Record *record, const TableSchema *schema);

#endif
