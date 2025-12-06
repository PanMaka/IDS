#include "bplus_file_funcs.h"
#include "bplus_file_structs.h"
#include "bf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALL_BF(call)       \
  {                         \
    BF_ErrorCode code = call; \
    if (code != BF_OK)      \
    {                       \
      BF_PrintError(code);  \
      return -1;            \
    }                       \
  }

#define INVALID_BLOCK -1

typedef union {
    Internal internal;
    Leaf leaf;
} BlockType;

// Global counter (as per your snippet)
int filesOpened = 0;

int bplus_create_file(const TableSchema *schema, const char *fileName)
{
  CALL_BF(BF_CreateFile(fileName));

  int handler;
  BF_Block *block;
  
  BF_Block_Init(&block);

  // Open file and allocate Block 0
  CALL_BF(BF_OpenFile(fileName, &handler));
  CALL_BF(BF_AllocateBlock(handler, block));

  BPlusMeta* metadata = (BPlusMeta*)BF_Block_GetData(block);
  
  // Initialize Metadata
  metadata->totalBlocks = 1; // Block 0 exists
  metadata->indexBlocksCount = 0;
  metadata->dataBlocksCount = 0;
  metadata->maxRecordsInBlock = MAX_LEAF_RECORDS; // Use the macro from bplus_datanode.h
  metadata->height = 0;
  metadata->rootBlockId = INVALID_BLOCK; // CRITICAL: Marks tree as empty
  metadata->schema = *schema;

  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  
  BF_Block_Destroy(&block);
  CALL_BF(BF_CloseFile(handler));

  return 0;
}

int bplus_open_file(const char *fileName, int *file_desc, BPlusMeta **metadata)
{
  if (filesOpened >= BF_MAX_OPEN_FILES) {
    return -1;
  }

  CALL_BF(BF_OpenFile(fileName, file_desc));
  
  BF_Block* block;
  BF_Block_Init(&block);

  // Read Block 0
  CALL_BF(BF_GetBlock(*file_desc, 0, block));
  
  void* data = BF_Block_GetData(block);
  
  // Allocate memory for the metadata struct in RAM
  (*metadata) = (BPlusMeta*)malloc(sizeof(BPlusMeta));
  
  // Copy data from Disk Block to RAM struct
  memcpy((*metadata), data, sizeof(BPlusMeta));

  // CRITICAL: Must unpin Block 0 after reading
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  filesOpened++;
  return 0;
}

int bplus_close_file(const int file_desc, BPlusMeta* metadata)
{
  if (filesOpened == 0)
    return -1;

  filesOpened--;

  BF_Block* block;
  BF_Block_Init(&block);

  // Get Block 0 to update it before closing
  CALL_BF(BF_GetBlock(file_desc, 0, block));
  
  void* data = BF_Block_GetData(block);
  
  // Copy RAM metadata back to Disk Block
  memcpy(data, metadata, sizeof(BPlusMeta));

  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  CALL_BF(BF_CloseFile(file_desc));
  
  // Free the RAM copy
  free(metadata);

  return 0;
}

int bplus_record_insert(const int file_desc, BPlusMeta *metadata, const Record *record)
{
    // Call the core logic function that returns the block ID it inserted at
    int resultBlockId = insertIndex(metadata, record, file_desc);

    if (resultBlockId == -1) {
        return -1;            // Error on insert
    }

    // Success
    return 0; 
}

int bplus_record_find(const int file_desc, const BPlusMeta* metadata, const int key, Record** out_record)
{
    *out_record = NULL;
  
    BlockId currentBlockId = metadata->rootBlockId;
    if (currentBlockId == INVALID_BLOCK) {
        return -1;          // Error, tree is empty
    }

    BF_Block *block;
    BF_Block_Init(&block);
    BlockType *node = NULL;

    // 1. TRAVERSE: Root -> Leaf
    while (true) {
        CALL_BF(BF_GetBlock(file_desc, currentBlockId, block));
        node = (BlockType*)BF_Block_GetData(block);

        // Check if we reached a LEAF
        if (node->internal.type == LEAF) {
            // Stop traversing, we are at the data level
            break; 
        }

        // We are in an INTERNAL node
        Internal *internalNode = &node->internal;
        int i = 0;
        
        // Find the correct child
        while (i < internalNode->capacity && key >= internalNode->keys[i]) {
            i++;
        }
        
        BlockId nextBlockId = internalNode->children[i];
        
        CALL_BF(BF_UnpinBlock(block)); // Unpin current before moving to next
        currentBlockId = nextBlockId;
    }

    // SEARCH: Inside the Leaf
    // 'block' is currently pinned and holds the Leaf node
    Leaf *leafNode = &node->leaf;
    int foundIndex = -1;

    for (int i = 0; i < leafNode->numRecords; i++) {
        int currentKey = record_get_key(&metadata->schema, &leafNode->records[i]);
        if (currentKey == key) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex != -1) {
        // Found it!
        *out_record = (Record*)malloc(sizeof(Record));
        memcpy(*out_record, &leafNode->records[foundIndex], sizeof(Record));
        
        CALL_BF(BF_UnpinBlock(block));
        BF_Block_Destroy(&block);
        
        return 0;
    } else {
        // Not found in the leaf where it should have been
        CALL_BF(BF_UnpinBlock(block));
        BF_Block_Destroy(&block);
        return -1; // Not found
    }
}

