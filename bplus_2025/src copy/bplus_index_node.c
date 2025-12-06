// Μπορείτε να προσθέσετε εδώ βοηθητικές συναρτήσεις για την επεξεργασία Κόμβων Δεδομένων.
#include "bplus_index_node.h"

#define assignPointer(A, B, D) { \
                    A.index = B; \
                    A.offset = D; \
                    };

//HELPER FUNCTIONS
int splitIndexes(BPlusMeta* metadata, const int file_desc, Index *node, pointer_t nodeptr, int newKey, pointer_t newP, pointer_t* ret);

int compare(const void* a, const void* b) {
   return (*(int*)a - *(int*)b);
}

int createIndexNode(BPlusMeta* metadata, const int file_desc, pointer_t *newP, int keyCount, ... )
{
    //Get root block
    pointer_t cursor;
    assignPointer(cursor, metadata->firstIndexBlock.index, 0);
    

    BF_Block *block;
    BF_Block_Init(&block);
    
    //Helper variables 
    void *data;
    IndexBlock *traversor;
    IndexBlock *new;

    //New node info
    int offset = -1, index = -1, sibling;

    while(1){
        BF_GetBlock(file_desc, cursor.index, block);
        data = BF_Block_GetData(block);
        traversor = data;

        if (traversor->numOfNodes != metadata->maxIndexNodesInBlock || traversor->siblingIndex.index == -1)
            break;

        cursor = traversor->siblingIndex;

        BF_UnpinBlock(block);
    }

    if (traversor->numOfNodes == metadata->maxIndexNodesInBlock){
        metadata->totalBlocks++;
        metadata->indexBlocksCount++;
        traversor->siblingIndex.index = metadata->totalBlocks - 1;
        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);
        
        //Create new block
        BF_AllocateBlock(file_desc, block);
        data = BF_Block_GetData(block);

        new = data;

        new->numOfNodes = 1;
        new->siblingIndex.index = -1;
        offset = 0;
        index = metadata->totalBlocks - 1;
        
    } else {
        ++traversor->numOfNodes;
        offset = traversor->numOfNodes;
        index = cursor.index;
        new = traversor;
    }

    newP->index = index;
    newP->offset = offset;

    va_list newKeys;
    va_start(newKeys, keyCount);
    
    for (int i = 0; i < keyCount; i ++) {
        new->node[offset].keys[i] = va_arg(newKeys, int);
    }
    va_end(newKeys);
    
    BF_Block_SetDirty(block);
    BF_UnpinBlock(block);

    return 0;
}

int findParent(BPlusMeta* metadata, const int file_desc, pointer_t target_child, pointer_t* parent_res){
    pointer_t cursor = metadata->firstIndexBlock;

    BF_Block* block;
    BF_Block_Init(&block);
    void* data;

    while(cursor.index != -1){
        BF_GetBlock(file_desc, cursor.index, block);
        data = BF_Block_GetData(block);
        IndexBlock *idb = data;

        for(int i = 0; i < idb->numOfNodes; i++){
            Index* node = &idb->node[i];

            int children_count = (NUM_OF_KEYS - node->capacity) + 1;

            for(int j = 0; j < children_count; j++){
                if(node->children[j].index == target_child.index && 
                    node->children[j].offset == target_child.offset){
                    parent_res->index = cursor.index;
                    parent_res->offset = i;
                    
                    BF_UnpinBlock(block);
                    return -1;
                }
            }
        }

        cursor = idb->siblingIndex;
        BF_UnpinBlock(block);
    }
    BF_Block_Destroy(&block);
    return 0;
}

int splitIndexes(BPlusMeta* metadata, const int file_desc, Index *node, pointer_t nodeptr, int newKey, pointer_t newP, pointer_t* ret)
{   
    //Add the new key
    node->keys[NUM_OF_KEYS] = newKey;
    qsort(node->keys, NUM_OF_KEYS+1, sizeof(int), compare);
    
    //Add the pointer to the children in the correct place
    for(int i = 0; i < NUM_OF_KEYS + 1; i++){
        if(node->keys[i] == newKey){
            if (i < NUM_OF_KEYS + 1)   
                node->children[i + 1] = newP;
            break;
        }
    }

    //Find which key will get pushed up
    int pushKey = node->keys[NUM_OF_KEYS/2];
    pointer_t newNode;
    int check = createIndexNode(metadata, file_desc, &newNode, 2, node->keys[NUM_OF_KEYS - 1], node->keys[NUM_OF_KEYS]);

    if (check == -1)
        exit(1);

    BF_Block* block;
    BF_Block_Init(&block);
    BF_GetBlock(file_desc, newNode.index, block);

    void* data;
    data = BF_Block_GetData(block);

    node->capacity = NUM_OF_KEYS/2;

    for(int i = 0; i < NUM_OF_KEYS/2; i++){

        node->keys[i + NUM_OF_KEYS/2] = -1;
        assignPointer(node->children[i+ NUM_OF_KEYS/2], -1, -1);
    }


    pointer_t parent;
    parent.index = -1;
    parent.offset = -1;

    findParent(metadata, file_desc, nodeptr , &parent);
    
    if(ret)
        ret = &newNode;
    return -1;
}

int recursiveTraverse(BPlusMeta* metadata, pointer_t cursor, const int file_desc, const Record *record, pointer_t* newP, int *midKey)
{
    // recurse to the correct leaf
    BF_Block *block;
    BF_Block_Init(&block);

    BF_GetBlock(file_desc, cursor.index, block);

    //Get Index block
    void* data = BF_Block_GetData(block);
    IndexBlock* nodeBlock = data;
    
    //Get index Node
    Index* node = &nodeBlock->node[cursor.offset];

    //Get the key we want to insert
    int key = record->values[metadata->schema.key_index].int_value;

    //Helper variables
    int i, j;
    bool rightChild = true;

    //Innitialize return variables
    pointer_t ret;
    assignPointer(ret, -1, -1);
    
    if (node->type == LEAF) {
        //Check if new data is created

        // Figure out appropriate child data
        for (i = 0; i < node->capacity; i++) {
            if (key < node->keys[i])
                break;
        }

        BF_Block *tempBlock;
        BF_Block_Init(&tempBlock);

        BF_GetBlock(file_desc, cursor.index, tempBlock);

        //Get Index block
        data = BF_Block_GetData(tempBlock);

        j = insertRecordInData(metadata, file_desc, (DataNode *)data, record, &ret);

        BF_Block_SetDirty(tempBlock);
        BF_UnpinBlock(tempBlock);

        if (ret.index != -1) {
            if (node->capacity == NUM_OF_KEYS) {
                //Split index node
                int check = splitIndexes(metadata, file_desc, node, cursor, key, *newP, &ret);
                
                if (check == -1)
                    exit(1);
            }
        }
    }

    if (node->type == LEAF && node->capacity == NUM_OF_KEYS) {

        //Split index node
        int check = splitIndexes(metadata, file_desc, node, cursor, key, *newP, &ret);
        
        if (check == -1) {
            exit(2);
        }


    } else if (node->type == LEAF) {

        // Figure out appropriate child data
        for (i = 0; i < node->capacity; i++) {
            if (key < node->keys[i])
                break;
        }


        for (j = node->capacity - 1; j >= i; j--){
            node->keys[j+1] = node->keys[j];
        }

        node->keys[i] = key;
        node->capacity++;

        // Move children to leave space for the new one
        for (j = node->capacity; j >= i; j--){
            node->children[j+1] = node->children[j];
        }

        //Attach to the according data block
        node->children;

    } else {

        // Find which child to go to next
        for (i = 0; i < node->capacity; i++) {
            if (key < node->keys[i]) {
                rightChild = false;
                cursor = node->children[i];
                break;
            }
        }

        // Rightmost child if not found in loop
        if (rightChild) cursor = node->children[i];

        // int recursiveTraverse(BPlusMeta* metadata, pointer_t cursor, const int file_desc, const Record record, pointer_t* newP, int *midKey)
        recursiveTraverse(metadata, cursor, file_desc, record, newP, midKey);

        if (newP->index != -1) {
            // We need to insert *midKey and *newP into the current node
            if (node->capacity == NUM_OF_KEYS) {
                // Current node is full, we need to split THIS node too
                int check = splitIndexes(metadata, file_desc, node, cursor, key, *newP, &ret); // Update midKey for the parent above us
                if (check == -1) exit(1);
                *newP = ret; // Pass the new sibling of THIS node up to the parent
            } else {
                int i, j;
                int keyToInsert = *midKey; // Value passed up from child

                // 1. Find the position where keyToInsert belongs
                //    We look for the first key that is GREATER than our new key.
                for (i = 0; i < node->capacity; i++) {
                    if (keyToInsert < node->keys[i])
                        break;
                }
                
                // 2. Shift Keys to the right to create space at index 'i'
                //    We move from the end backwards.
                for (j = node->capacity; j > i; j--) {
                    node->keys[j] = node->keys[j-1];
                }

                // 3. Shift Children pointers to the right
                //    Internal nodes have (capacity + 1) pointers. 
                //    The new pointer goes to the RIGHT of the new key (at i+1).
                //    So we shift everything from i+1 onwards.
                for (j = node->capacity + 1; j > i + 1; j--) {
                    node->children[j] = node->children[j-1];
                }

                // 4. Insert the Data
                node->keys[i] = keyToInsert;
                node->children[i + 1] = *newP; // The new pointer goes to the right of the key

                // 5. Update Metadata
                node->capacity++;
                assignPointer((*newP), -1, -1); 
            }
        }
    }

    BF_Block_SetDirty(block);
    BF_UnpinBlock(block);
    return 0;
}

int insertIndex(BPlusMeta* metadata, const Record *record, const int file_desc)
{
    if (metadata->indexBlocksCount == 0) {
        BF_Block* block;
        BF_Block_Init(&block);

        BF_AllocateBlock(file_desc, block);
        void* data = BF_Block_GetData(block);
        IndexBlock* nodeBlock = data;

        nodeBlock->numOfNodes = 1;
        nodeBlock->siblingIndex.index = -1;
        nodeBlock->siblingIndex.offset = 0;
        nodeBlock->node[0].type = LEAF;
        nodeBlock->node[0].capacity = 1;
        nodeBlock->node[0].keys[0] = record->values[metadata->schema.key_index].int_value;
        
        metadata->totalBlocks++;
        metadata->indexBlocksCount++;
        metadata->firstIndexBlock.index = metadata->totalBlocks - 1;

        int check = insertRecordInData(metadata, file_desc, NULL, record, &nodeBlock->node[0].children[0]);
        if (check == -1)
            exit(0);
        
        BF_Block_SetDirty(block);
        BF_UnpinBlock(block);

        BF_Block_Destroy(&block);
        return 0;
    }

    pointer_t newP;
    int midKey;
    
    recursiveTraverse(metadata, metadata->firstIndexBlock, file_desc, record, &newP, &midKey);
    return 0;
}



// int populateIndexNode(Index **head, int key,)
// {
//     int cur_keys = NUM_OF_KEYS - (*head)->capacity;
//     int ret = 0, i, j;

//     //find correct spot for the key
//     for (i = 0; i < cur_keys; i++)
//         if (key < (*head)->keys[i])
//             break;
    
//     //rotate all keys and children to the right of the new key to the next spot
//     for (j = cur_keys - 1; j >= i; j--) {
//         (*head)->keys[j+1] = (*head)->keys[j];

//         if (newChild && !(*head)->leafFlag)
//             (*head)->children[j+2]->indexNode = (*head)->children[j+1]->indexNode;
//         else if (newChild && (*head)->leafFlag)
//             (*head)->children[j+1]->dataNode = (*head)->children[j]->dataNode;
//     }
    
//     //insert key in the spot found
//     (*head)->keys[i] = key;

//     //insert new child in the appropriate spot
//     if (newChild && !(*head)->leafFlag)
//         (*head)->children[i+1]->indexNode = newChild->indexNode;
//     else if (newChild && (*head)->leafFlag)
//         (*head)->children[i]->dataNode = newChild->dataNode;


//     (*head)->capacity--;  

//     return 0;
// }

// int indexNodeSplit(Index **nodeToSplit, int key, DataNode *nodeContaningKey, Index **newNode, int *middleKey){

//     //Create temporary buffer
//     int keysBuff[4];
//     Node *childrenBuff[5];


// }

// //TODO check new key already in tree
// int insertKeyRecursive(BplusTree *cursor, Index *head, Record newEntry, Index **newChild, int *middle)
// {
//     int key = newEntry.values[cursor->schema->key_index].int_value;
//     int cur_keys = NUM_OF_KEYS - head->capacity;
//     int ret = 0, i, j;
//     int split = 0;
//     Index *splitCheck = NULL;

//     if (head->leafFlag) {
//         //node is leaf

//         if (!head->capacity) {
//             //split
//         } else {

//             ret = populateIndexNode(&head, key, NULL);

//             //TODO couple with the correct dataNode
//             //InsertRecordInData
//         }
    
//         return 0;
//     }

//     for(i = 0; i < cur_keys; i++){
//         if(key < head->keys[i])
//         break;
//     }

//     ret = insertKeyRecursive(cursor, head->children[i]->indexNode, newEntry, &splitCheck, &split);

//     if (splitCheck) {
//         if (!head->capacity){
//             //implement split
//         } else {
//             //insert key here
//             ret = populateIndexNode(&head, split, splitCheck);
//         }
//     }

//     return ret;
// }