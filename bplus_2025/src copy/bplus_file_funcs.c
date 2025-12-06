#include "bplus_file_funcs.h"
#include <stdio.h>
#include <string.h>

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK)        \
    {                         \
      BF_PrintError(code);    \
      return -1;     \
    }                         \
  }

int filesOpened = 0;

int bplus_create_file(const TableSchema *schema, const char *fileName)
{
  CALL_BF(BF_CreateFile(fileName));

  //Create Header Block
  int handler;
  BF_Block *block;
  void *data;
  
  // Initialize block
  BF_Block_Init(&block);

  // Open file and allocate a new block
  CALL_BF(BF_OpenFile(fileName, &handler));
  CALL_BF(BF_AllocateBlock(handler, block));

  data = BF_Block_GetData(block); // Get the data of the new block

  BPlusMeta* metadata = data;
  memcpy(metadata, data, sizeof(BPlusMeta));
  memset(metadata, 0, sizeof(BPlusMeta));

  metadata->totalBlocks = 1;
  metadata->indexBlocksCount = 0;
  metadata->dataBlocksCount = 0;
  metadata->maxRecordsInBlock = BF_BLOCK_SIZE / schema->record_size;
  metadata->schema = *schema;

  //Dirty, unpin and destroy the block, then close file
  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);
  CALL_BF(BF_CloseFile(handler));

  return 0;
}

int bplus_open_file(const char *fileName, int *file_desc, BPlusMeta **metadata)
{
  // If the opened files exceed the maximum allowed then stop
  if (filesOpened++ > BF_MAX_OPEN_FILES) {
    return -1;
  }

  // Open the BF file and then initialize a block
  CALL_BF(BF_OpenFile(fileName, file_desc));
  BF_Block* block;
  BF_Block_Init(&block);

  // Create a pointer to manipulate the data of the block and a dummy heap pointer
  void* data;
  BPlusMeta* dummy;

  /** Get the block at index 0 "header block" and then assign it's content to the data variable
  Assign the data to the dummy, initialize through malloc the header_info double pointer
  And add to it all the attributes of the dummy header and then destroy the node
  **/
  CALL_BF(BF_GetBlock(*file_desc, 0, block));
  data = BF_Block_GetData(block);
  dummy = data;
  memcpy(dummy, data, sizeof(BPlusMeta));
  
  (*metadata) = (BPlusMeta*)malloc(sizeof(BPlusMeta));
  memcpy((*metadata), data, sizeof(BPlusMeta));
  // (*metadata)->totalBlocks = dummy->totalBlocks;
  // (*metadata)->indexBlocksCount = dummy->indexBlocksCount;
  // (*metadata)->firstIndexBlock = dummy->firstIndexBlock;
  // (*metadata)->dataBlocksCount = dummy->dataBlocksCount;
  // (*metadata)->firstDataBlock = dummy->firstDataBlock;
  // (*metadata)->maxRecordsInBlock = dummy->maxRecordsInBlock;
  // (*metadata)->maxIndexNodesInBlock = dummy->maxIndexNodesInBlock;
  // (*metadata)->schema = dummy->schema;
  BF_Block_Destroy(&block);

  return 0;
}

int bplus_close_file(const int file_desc, BPlusMeta* metadata)
{
  // If there is no open file to close return 0
  if (filesOpened == 0)
    return -1;

  // Subtracting the amount of files opened
  filesOpened--;

  // Initializing a block and another dummy header
  BF_Block* header_block;
  BF_Block_Init(&header_block);
  BPlusMeta* dummy;

  // Get the header block
  CALL_BF(BF_GetBlock(file_desc, 0, header_block));

  // Fill the dummy header with the data from the first block and the attributes of the actual header
  void* data = BF_Block_GetData(header_block);
  dummy = data;
  memcpy(dummy, metadata, sizeof(BPlusMeta));
  
  // dummy->totalBlocks = metadata->totalBlocks;
  // dummy->indexBlocksCount = metadata->indexBlocksCount;
  // dummy->firstIndexBlock = metadata->firstIndexBlock;
  // dummy->dataBlocksCount = metadata->dataBlocksCount;
  // dummy->firstDataBlock = metadata->firstDataBlock;
  // dummy->maxRecordsInBlock = metadata->maxRecordsInBlock;
  // dummy->maxIndexNodesInBlock = metadata->maxIndexNodesInBlock;
  // dummy->schema = metadata->schema;

  // Dirty and unpin the header block
  BF_Block_SetDirty(header_block);
  CALL_BF(BF_UnpinBlock(header_block));


  // Destroy the block because we don't need it and then close the file and free the header to avoid leaks
  BF_Block_Destroy(&header_block);
  CALL_BF(BF_CloseFile(file_desc));
  free(metadata);

  return 0;
}

int bplus_record_insert(const int file_desc, BPlusMeta *metadata, const Record *record)
{
  
}

int bplus_record_find(const int file_desc, const BPlusMeta* metadata, const int key, Record** out_record)
{
  *out_record = NULL;
  BF_Block* block;
  BF_Block_Init(&block);
  void* data;
  CALL_BF(BF_GetBlock(file_desc, 0, block));
  data = BF_Block_GetData(block);
  memcpy((BPlusMeta*)metadata, data, sizeof(BPlusMeta));
  pointer_t cursor;
  cursor.index = metadata->firstIndexBlock.index;
  cursor.offset = metadata->firstIndexBlock.offset;
  
  printf("%d index, %d data", metadata->indexBlocksCount, metadata->dataBlocksCount);

  BF_UnpinBlock(block);

  while(cursor.index != -1){
    CALL_BF(BF_GetBlock(file_desc, cursor.index, block));
    data = BF_Block_GetData(block);
    DataNode* db = data;
    if(findRecordInDataNode((BPlusMeta*)metadata, block, key, out_record) == 0){
      break;

    }
    BF_UnpinBlock(block);

    cursor = db->siblingIndex;
    BF_UnpinBlock(block);
  }

  BF_Block_Destroy(&block);
  
  return 0;
}

