#include "bplus_index_node.h"

// Defines for invalid block pointers if not in header
#define INVALID_BLOCK -1

#define MAX_TREE_HEIGHT 10

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK)        \
    {                         \
      BF_PrintError(code);    \
      return -1;     \
    }                         \
  }


typedef union bplus_type_of_block{
    Internal internal;
    Leaf leaf;
} BlockType;

// Track the path towards the leaf and track the path
BlockId find_Leaf_and_Path(int fileDesc, BlockId rootBlockId, int key, BlockId* parents, int* parentCount) {
    BlockId currentBlockId = rootBlockId;
    BF_Block *block = NULL;
    BlockType *node = NULL;

    *parentCount = 0;

    if (currentBlockId == INVALID_BLOCK) {
        return INVALID_BLOCK;
    }

    while (true) {
        BF_Block_Init(&block);
        CALL_BF(BF_GetBlock(fileDesc, currentBlockId, block));
        node = (BlockType*)BF_Block_GetData(block);

        if (node->internal.type == LEAF) {
            BF_UnpinBlock(block);
            BF_Block_Destroy(&block);
            return currentBlockId;
        }

        // We are in an INTERNAL node -> Add to Path Stack
        parents[(*parentCount)++] = currentBlockId;

        Internal *internalNode = &node->internal;
        int i = 0;
        // Standard B+ Tree search
        while (i < internalNode->capacity && key >= internalNode->keys[i]) {
            i++;
        }
        
        BlockId nextBlockId = internalNode->children[i];
        
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
        currentBlockId = nextBlockId;
    }
}

// Split the internal nodes
int split_internal(int fileDesc, BPlusMeta* metadata, BlockId leftBlockId, Internal* leftNode, int key, BlockId rightChildId, BlockId* parents, int parentCount) {
    // 1. Create Temporary Arrays larger than capacity
    int tempKeys[NUM_OF_KEYS + 1];
    BlockId tempChildren[NUM_OF_KEYS + 2];

    // 2. Insert existing data + new Key/Child into temp arrays (sorted)
    int i = 0, j = 0;
    
    // Find where the new key goes
    while (i < leftNode->capacity && leftNode->keys[i] < key) {
        tempKeys[j] = leftNode->keys[i];
        tempChildren[j] = leftNode->children[i];
        i++; j++;
    }
    
    // Insert new item
    tempKeys[j] = key;
    tempChildren[j] = leftNode->children[i]; // The child to the left of insertion
    tempChildren[j+1] = rightChildId;        // The child to the right of insertion
    j++; 

    // Copy the rest
    while (i < leftNode->capacity) {
        tempKeys[j] = leftNode->keys[i];
        // Note: children are offset by 1 because we inserted a new child
        tempChildren[j+1] = leftNode->children[i+1];
        i++; j++;
    }

    // 3. Determine Split Point
    // For Internal nodes, one key goes on the previous level. It does not stay in the nodes.
    int splitIndex = (NUM_OF_KEYS + 1) / 2;
    int upKey = tempKeys[splitIndex];

    // 4. Allocate New Block (Right Node)
    BF_Block *newBlock;
    BF_Block_Init(&newBlock);
    CALL_BF(BF_AllocateBlock(fileDesc, newBlock));
    
    int rightBlockId;
    BF_GetBlockCounter(fileDesc, &rightBlockId);
    rightBlockId--;

    BlockType *newBlockData = (BlockType*)BF_Block_GetData(newBlock);
    newBlockData->internal.type = INTERNAL;
    
    // 5. Distribute Data
    // Reset Left Node
    leftNode->capacity = 0;
    
    // Fill Left Node (0 to splitIndex - 1)
    for (int k = 0; k < splitIndex; k++) {
        leftNode->keys[k] = tempKeys[k];
        leftNode->children[k] = tempChildren[k];
        leftNode->capacity++;
    }
    leftNode->children[leftNode->capacity] = tempChildren[splitIndex];

    // Fill Right Node (splitIndex + 1 to End)
    newBlockData->internal.capacity = 0;
    for (int k = splitIndex + 1; k < NUM_OF_KEYS + 1; k++) {
        newBlockData->internal.keys[k - (splitIndex + 1)] = tempKeys[k];
        newBlockData->internal.children[k - (splitIndex + 1)] = tempChildren[k];
        newBlockData->internal.capacity++;
    }
    
    // Don't forget the last child pointer for right node
    newBlockData->internal.children[newBlockData->internal.capacity] = tempChildren[NUM_OF_KEYS + 1];

    // 6. Write Right Node to Disk
    metadata->totalBlocks++;
    metadata->indexBlocksCount++;
    BF_Block_SetDirty(newBlock);
    BF_UnpinBlock(newBlock);
    BF_Block_Destroy(&newBlock);

    // 7. Recursively Insert 'upKey' into Parent
    return insert_into_parent(fileDesc, metadata, leftBlockId, upKey, rightBlockId, parents, parentCount);
}

// Insert the split index 
int insert_into_parent(int fileDesc, BPlusMeta* metadata, BlockId leftBlockId, int key, BlockId rightBlockId, BlockId* parents, int parentCount) {

    // CASE A: We just split the Root. Need a new Root.
    if (parentCount == 0) {
        BF_Block *newRootBlock;
        BF_Block_Init(&newRootBlock);
        CALL_BF(BF_AllocateBlock(fileDesc, newRootBlock));

        int newRootId;
        BF_GetBlockCounter(fileDesc, &newRootId);
        newRootId--;

        BlockType *rootNode = (BlockType*)BF_Block_GetData(newRootBlock);
        rootNode->internal.type = INTERNAL;
        rootNode->internal.capacity = 1;
        rootNode->internal.keys[0] = key;
        rootNode->internal.children[0] = leftBlockId;
        rootNode->internal.children[1] = rightBlockId;

        metadata->rootBlockId = newRootId;
        metadata->totalBlocks++;
        metadata->indexBlocksCount++;
        metadata->height++;

        BF_Block_SetDirty(newRootBlock);
        BF_UnpinBlock(newRootBlock);
        BF_Block_Destroy(&newRootBlock);
        return newRootId; // Or success code
    }

    // CASE B: Parent exists.
    BlockId parentId = parents[parentCount - 1];
    parentCount--; // Pop stack

    BF_Block *block;
    BF_Block_Init(&block);
    CALL_BF(BF_GetBlock(fileDesc, parentId, block));
    BlockType *node = (BlockType*)BF_Block_GetData(block);
    Internal *parent = &node->internal;

    // Check if Parent has space
    if (parent->capacity < NUM_OF_KEYS) {
        // Simple Insert (Sorted)
        int i = parent->capacity - 1;
        while (i >= 0 && parent->keys[i] > key) {
            parent->keys[i+1] = parent->keys[i];
            parent->children[i+2] = parent->children[i+1];
            i--;
        }
        parent->keys[i+1] = key;
        parent->children[i+2] = rightBlockId;
        parent->capacity++;

        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
        return 0; // Success
    } 
    else {
        // Parent is full -> Split Internal
        // Note: we don't unpin here because split_internal handles the read/write logic 
        // (Actually, split_internal needs to read the block again or we pass the pointer. 
        //  To keep it clean, we unpin here and let split_internal re-read or pass data).
        // For this implementation, let's pass the data pointer logic. 
        // BUT split_internal signature I wrote takes IDs. So let's unpin but NOT set dirty (data not valid yet).
        
        // Actually, better to do the logic right here or call split:
        int result = split_internal(fileDesc, metadata, parentId, parent, key, rightBlockId, parents, parentCount);
        
        // Since 'parent' was modified inside split_internal (it became the Left Node), we mark Dirty
        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
        return result;
    }
}


/* ----------------------------------------------------------------- */
/* MAIN FUNCTION: Insert Index (Insert Record)                       */
/* ----------------------------------------------------------------- */
int insertIndex(BPlusMeta* metadata, const Record *record, const int file_desc) {
    BF_Block *block = NULL;
    BlockType *node = NULL; 

    // --- CASE 1: Empty Tree (Create Root) ---
    // LOGIC CHANGE: We use dataBlocksCount to detect an empty tree.
    if (metadata->dataBlocksCount == 0) {
        
        BF_Block *newBlock;
        BF_Block_Init(&newBlock); 
        CALL_BF(BF_AllocateBlock(file_desc, newBlock));

        int newBlockId;
        BF_GetBlockCounter(file_desc, &newBlockId);
        newBlockId--; 

        node = (BlockType*)BF_Block_GetData(newBlock);
        
        // Initialize the first block as a LEAF
        node->leaf.type = LEAF;
        node->leaf.numRecords = 0;
        
        insert_into_leaf_simple(&node->leaf, record, &metadata->schema);

        // Update Metadata
        metadata->rootBlockId = newBlockId; // We still need to SET the root ID for future finds
        metadata->totalBlocks++;
        metadata->dataBlocksCount++;        // Now it becomes 1
        metadata->height = 1; 

        BF_Block_SetDirty(newBlock);
        BF_UnpinBlock(newBlock);
        BF_Block_Destroy(&newBlock); 

        return newBlockId;
    }

    // --- CASE 2: Find Leaf (Tree is not empty) ---
    BlockId parents[MAX_TREE_HEIGHT];
    int parentCount = 0;

    int key = record_get_key(&metadata->schema, record);
    
    // We still use rootBlockId here because we need to know WHERE the root is to start searching
    BlockId leafId = find_Leaf_and_Path(file_desc, metadata->rootBlockId, key, parents, &parentCount);

    if (leafId == INVALID_BLOCK) return -1;

    // --- CASE 3: Insert into Leaf ---
    BF_Block_Init(&block);
    CALL_BF(BF_GetBlock(file_desc, leafId, block));
    node = (BlockType*)BF_Block_GetData(block);

    if (node->leaf.numRecords < MAX_LEAF_RECORDS) {
        // Simple Insert
        insert_into_leaf_simple(&node->leaf, record, &metadata->schema);
        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
        return leafId;
    } 
    else {
        // --- CASE 4: Split Required ---
        int result = split_leaf(file_desc, metadata, leafId, &node->leaf, record, parents, parentCount);
        
        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
        return result; 
    }
}