#include "bf.h"
#include "bplus_datanode.h"

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK)        \
    {                         \
      BF_PrintError(code);    \
      return -1;     \
    }                         \
  }

#define INVALID_BLOCK -1

typedef union bplus_type_of_block{
    Internal internal;
    Leaf leaf;
} BlockType;


int split_leaf(int fileDesc, BPlusMeta* metadata, BlockId leftBlockId, Leaf* leftNode, const Record* newRecord, BlockId* parents, int parentCount) {
    
    // Create Temporary Array (Size MAX + 1)
    Record tempRecords[MAX_LEAF_RECORDS + 1];
    int i = 0, j = 0;
    int key = record_get_key(&metadata->schema, newRecord);

    // Insert Sorted into Temp
    while (i < leftNode->numRecords && record_get_key(&metadata->schema, &leftNode->records[i]) < key) {
        tempRecords[j++] = leftNode->records[i++];
    }
    tempRecords[j++] = *newRecord;
    while (i < leftNode->numRecords) {
        tempRecords[j++] = leftNode->records[i++];
    }

    // Determine where to split
    int total = MAX_LEAF_RECORDS + 1;
    int splitIndex = (total + 1) / 2;

    // 4. Allocate New Block (Right Node)
    BF_Block *newBlock;
    BF_Block_Init(&newBlock);
    CALL_BF(BF_AllocateBlock(fileDesc, newBlock));
    
    int rightBlockId;
    BF_GetBlockCounter(fileDesc, &rightBlockId);
    rightBlockId--;

    BlockType *newBlockData = (BlockType*)BF_Block_GetData(newBlock);
    newBlockData->leaf.type = LEAF;
    // (nextLeaf logic removed as requested)

    // 5. Distribute Records
    leftNode->numRecords = splitIndex;
    for (int k = 0; k < splitIndex; k++) {
        leftNode->records[k] = tempRecords[k];
    }

    newBlockData->leaf.numRecords = total - splitIndex;
    for (int k = splitIndex; k < total; k++) {
        newBlockData->leaf.records[k - splitIndex] = tempRecords[k];
    }

    // 6. Determine "Up Key"
    // In B+ Tree, the separator key is the FIRST key of the RIGHT node.
    int upKey = record_get_key(&metadata->schema, &newBlockData->leaf.records[0]);

    // 7. Write Right Node
    metadata->totalBlocks++;
    metadata->dataBlocksCount++;
    BF_Block_SetDirty(newBlock);
    BF_UnpinBlock(newBlock);
    BF_Block_Destroy(&newBlock);

    // 8. Insert into Parent
    return insert_into_parent(fileDesc, metadata, leftBlockId, upKey, rightBlockId, parents, parentCount);
}

BlockId find_Leaf(int fileDesc, BlockId rootBlockId, int key) {
    BlockId currentBlockId = rootBlockId;
    BF_Block *block = NULL;
    BlockType *node = NULL; // Uses the Local Union

    if (currentBlockId == INVALID_BLOCK) {
        return INVALID_BLOCK;
    }

    while (true) {
        // Init block wrapper if necessary (depending on BF implementation), 
        // usually BF_Block_Init(&block) is needed before GetBlock 
        // if block is not allocated. Assuming user environment handles this.
        BF_Block_Init(&block); 

        CALL_BF(BF_GetBlock(fileDesc, currentBlockId, block));

        node = (BlockType*)BF_Block_GetData(block);

        // Check the type using the common 'type' field
        if (node->internal.type == LEAF) {
            BF_UnpinBlock(block);
            BF_Block_Destroy(&block); // Clean up memory
            return currentBlockId;
        }

        // We are in an INTERNAL node
        Internal *internalNode = &node->internal;
        int i = 0;
        
        while (i < internalNode->capacity && key >= internalNode->keys[i]) {
            i++;
        }
        
        BlockId nextBlockId = internalNode->children[i];
        
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block); // Clean up memory
        currentBlockId = nextBlockId;
    }
}

bool insert_into_leaf_simple(Leaf *leaf, const Record *record, const TableSchema *schema) {
    if (leaf->numRecords >= MAX_LEAF_RECORDS) {
        return false; 
    }

    int key = record_get_key(schema, record);
    int insertionPoint = 0;

    // 1. Find sorted position
    while (insertionPoint < leaf->numRecords) {
        int currentKey = record_get_key(schema, &leaf->records[insertionPoint]);
        if (key < currentKey) {
            break;
        }
        insertionPoint++;
    }

    // 2. Shift records right
    for (int i = leaf->numRecords; i > insertionPoint; i--) {
        leaf->records[i] = leaf->records[i - 1];
    }

    // 3. Insert new record
    leaf->records[insertionPoint] = *record;
    leaf->numRecords++;

    return true;
}