// Μπορείτε να προσθέσετε εδώ βοηθητικές συναρτήσεις για την επεξεργασία Κόμβων toy Ευρετηρίου.
#include "../include/bplus_datanode.h"

int findRecordInDataNode(BPlusMeta* metadata, BF_Block* block, int keyToFind, Record **record)
{
    *record = NULL;
    bool foundKey = false;

    void* data = BF_Block_GetData(block);
    DataNode* node = data;

    int key = -1;
    int i;

    for (i = 0; i < node->capacity; i++) {

        key = node->records[i].values[metadata->schema.key_index].int_value;

        if (key == keyToFind) {
            *record = &node->records[i];
            foundKey = true;
            break;
        }
    }
    
    if (foundKey)
        return 0;
    else
        return -1;
}

void dataNodeSplit(BPlusMeta *metadata, const int file_desc, DataNode *node, const Record *record, pointer_t *newNodeIndex)
{
    metadata->dataBlocksCount++;
    metadata->totalBlocks++;

    //Create new node
    BF_Block *block;
    BF_Block_Init(&block);

    BF_AllocateBlock(file_desc, block);
    
    void *data = BF_Block_GetData(block);
    DataNode *new = data;

    int i, curKey;

    new->capacity = 1;
    
    for (i = metadata->maxRecordsInBlock/2; i < metadata->maxRecordsInBlock; i++){
        new->records[i + 1 - metadata->maxRecordsInBlock/2] = node->records[i];
        new->capacity++;
        node->capacity--;
    }
    new->index.index = metadata->totalBlocks - 1;
    new->siblingIndex = node->siblingIndex;
    node->siblingIndex = new->index;


    *newNodeIndex = new->index;
    BF_Block_SetDirty(block);
    BF_UnpinBlock(block);
}



int insertRecordInData(BPlusMeta *metadata, const int file_desc, DataNode *node, const Record *record, pointer_t *newNodeIndex)
{
    if (metadata->dataBlocksCount == 0){
        metadata->dataBlocksCount++;
        metadata->totalBlocks++;
        
        BF_Block *block;
        BF_Block_Init(&block);
        
        BF_AllocateBlock(file_desc, block);
        
        void *data = BF_Block_GetData(block);
        DataNode *new = data;
        
        new->records[0] = *record;
        new->capacity = 1;
        new->index.index = metadata->totalBlocks - 1;
        new->siblingIndex.index = -1;
        
        metadata->firstDataBlock = new->index;
        *newNodeIndex = new->index;

        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);

        BF_Block_Destroy(&block);
        return 0;
    }
    
    int key = record->values[metadata->schema.key_index].int_value, curKey;
    int i, j;

    //Check if the correct data block has space
    if (node->capacity == metadata->maxRecordsInBlock){
        dataNodeSplit(metadata, file_desc, node, record, newNodeIndex);
        return 0;
    }

    //Insert in this block
    for (i = 0; i < node->capacity; i++){
        curKey = node->records[i].values[metadata->schema.key_index].int_value;
        if (key < curKey){
            break;
        }      
    }

    for (j = node->capacity - 1; j >= i; j--){
        node->records[j+1] = node->records[j];
    }

    node->records[i] = *record;
    node->capacity++;

    return 0;

}



// int createBplusData(BplusTree *cursor, DataNode **node, DataNode *sibling, BF_Block *block, int entries)
// {
//     DataNode *new = malloc(sizeof(DataNode));
//     if(!(new))
//         return 1;

//     new->data = block;
//     new->capacity = cursor->maxRecordsInDataNode;
//     new->sibling = sibling;
//     *node = new;
//     return 0;
// }

// int insertRecordInData(BplusTree *cursor, DataNode *node, Record *record)
// {
//     return -1;
// }

// bool dataNodeFull(DataNode *node)
// {
//     return node->capacity == 1;
// }