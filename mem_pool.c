#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/

static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = MEM_FILL_FACTOR;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = MEM_EXPAND_FACTOR;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = MEM_FILL_FACTOR;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = MEM_EXPAND_FACTOR;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = MEM_FILL_FACTOR;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = MEM_EXPAND_FACTOR;

/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/

typedef struct _node {
    
    alloc_t alloc_record;
    
    unsigned used;
    
    unsigned allocated;
    
    struct _node *next, *prev;
    
} node_t, *node_pt;

typedef struct _gap {
    
    size_t size;
    
    node_pt node;
    
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    
    pool_t pool;
    
    node_pt node_heap;
    
    unsigned total_nodes;
    
    unsigned used_nodes;
    
    gap_pt gap_ix;
    
    unsigned gap_ix_capacity;
    
} pool_mgr_t, *pool_mgr_pt;

/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/

static pool_mgr_pt *pool_store = NULL;
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;

/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/

static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);

// TESTED - GOOD - DEVIATED
static alloc_status _remove_gap(pool_mgr_pt pool_mgr, gap_pt gap) {
    
    // find the position of the node in the gap index
    // loop from there to the end of the array:
    //    pull the entries (i.e. copy over) one position up
    //    this effectively deletes the chosen node
    
    // Get a reference to the last gap
    const gap_pt lastGap = &(pool_mgr->gap_ix[pool_mgr->pool.num_gaps - 1]);
    
    // Copy the end gap to the position of the to be deleted gap
    *gap = *lastGap;
    
    // Nothing directly links to gaps, other than the gap_ix table,
    // so there's no cleanup work to do
    
    // update metadata (num_gaps)
    // Our to-be-deleted gap is now gone, decrement the used_gaps count
    --(pool_mgr->pool.num_gaps);
    
    // NOTE: This does leave a copy of the last gap in the gap table,
    // however we're now considering that gap as available space, not
    // to be indexed, sorted and available for overriting.
    
    // zero out the element at position num_gaps!
    // Nevertheless, let's nuke it anyway (just in case)
    lastGap->node = NULL;
    lastGap->size = 0;
    
    // We've altered the gap list, so let's resort it
    return _mem_sort_gap_ix(pool_mgr);
    
}

// TESTED - GOOD - DEVIATED
static alloc_status _remove_node(pool_mgr_pt pool_mgr, node_pt node) {

    // Cannot move nodes, because it breaks the alloc pointers
    // just mark the node as not used
    node->used = 0;
    
    // Remove the node from the linked list
    
    // Rewire the node's previous connection
    if(node->prev != NULL) {
        
        // Rewire [n-1]'s next to [n+1]
        node->prev->next = node->next;
        
    } else {
        
        // Node is the first element, set [n+1]'s previous to NULL
        node->next->prev = NULL;
        
    }
    
    // Rewire the node's next connection
    if(node->next != NULL) {
        
        // Rewire [n+1]'s previous to [n-1]
        node->next->prev = node->prev;
        
    } else {
        
        // Node is the last element, set [n-1]'s next to NULL
        node->prev->next = NULL;
        
    }
    
    return ALLOC_OK;
    
}

// TESTED - GOOD - DEVIATED
static alloc_status _add_gap(pool_mgr_pt pool_mgr, node_pt node) {
    
    gap_pt gap = NULL;
    
    // Look above for gaps (gap merging)
    
    // Get the ending address of this node
    const char * nodeEndingAddress = (((char *) node->alloc_record.mem) + node->alloc_record.size);
    
    for(unsigned int i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        
        //printf("Merge above, comparing %p with %p\r\n", nodeEndingAddress, pool_mgr->gap_ix[i].node->alloc_record.mem);
        
        // Does this gap begin where the node ends?
        if(nodeEndingAddress == pool_mgr->gap_ix[i].node->alloc_record.mem) {
            
            // There's an adjacent gap above us, let's use it
            
            //printf("Merging above gap.\r\n");
            
            // Remove the node attached to the adjacent gap
            _remove_node(pool_mgr, pool_mgr->gap_ix[i].node);
            
            // Set the passed in node to gap
            pool_mgr->gap_ix[i].node = node;
            node->allocated = 0;
            
            // Expand previous gap
            pool_mgr->gap_ix[i].size += node->alloc_record.size;
            pool_mgr->gap_ix[i].node->alloc_record.size = pool_mgr->gap_ix[i].size;
            
            // Store the gap for a later return
            gap = &(pool_mgr->gap_ix[i]);
            
            break;
            
        }
        
    }
    
    // Look below for gaps (gap merging)
    
    for(unsigned int i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        
        // Get the ending address of this gap
        const char * gapEndingAddress = (((char *) pool_mgr->gap_ix[i].node->alloc_record.mem) + pool_mgr->gap_ix[i].node->alloc_record.size);
        
        //printf("Merge below, comparing %p with %p\r\n", node->alloc_record.mem, pool_mgr->gap_ix[i].node->alloc_record.mem);
        
        // Does this gap begin where the node ends?
        if(node->alloc_record.mem == gapEndingAddress) {
            
            // There's an adjacent gap below us, let's use it
            
            //printf("Merging below gap.\r\n");
            
            // Expand previous gap
            pool_mgr->gap_ix[i].size += node->alloc_record.size;
            pool_mgr->gap_ix[i].node->alloc_record.size = pool_mgr->gap_ix[i].size;
            pool_mgr->gap_ix[i].node->allocated = 0;
            
            // Remove the node passed in
            _remove_node(pool_mgr, node);
            
            if(gap != NULL) {
                
                _remove_gap(pool_mgr, gap);
                
                return ALLOC_OK;
                
            }
            
            break;
            
        }
        
    }
    
    if(gap != NULL) {
        return ALLOC_OK;
    }
    
    // No gap to merge, create a new one
    
    // expand the gap index, if necessary (call the function)
    // Do we need to grab more space?
    if(_mem_resize_gap_ix(pool_mgr) != ALLOC_OK) {
        
        // Return ALLOC_FAIL on failure to resize
        return ALLOC_FAIL;
        
    }
    
    // add the entry at the end
    // Increment used_gaps
    ++(pool_mgr->pool.num_gaps);
    
    // Get a reference to the new gap we just "created"
    const gap_pt newGap = &(pool_mgr->gap_ix[pool_mgr->pool.num_gaps - 1]);
    
    // update metadata (num_gaps)
    // Setup the new gap
    newGap->size = node->alloc_record.size;
    newGap->node = node;
    node->allocated = 0;
    
    // We've altered the gap list, so let's resort it
    
    // sort the gap index (call the function)
    // check success
    return _mem_sort_gap_ix(pool_mgr);
    
}

// TESTED - GOOD - DEVIATED
static node_pt _add_node(pool_mgr_pt pool_mgr) {
    
    // Do we need to grab more space?
    if(_mem_resize_node_heap(pool_mgr) != ALLOC_OK) {
        
        // Return NULL on failure to resize
        return NULL;
        
    }
    
    // Increment used_nodes
    ++(pool_mgr->used_nodes);
    
    // Get a reference to the new node we just "created"
    const node_pt newNode = &(pool_mgr->node_heap[pool_mgr->used_nodes - 1]);
    
    // There's a chance this node's memory came from a realloc and contains
    // garbage, let's nuke it (just in case)
    
    newNode->next = NULL;
    newNode->prev = NULL;
    newNode->allocated = 0;
    newNode->alloc_record.mem = NULL;
    newNode->alloc_record.size = 0;
    
    // We're actually not going to setup the node here, only wire it up
    
    // Is this the first node to be added to the heap?
    if(pool_mgr->used_nodes == 1) {
        
        // It is, special case, next and prev are already NULL, just return it
        
        return newNode;
        
    }
    
    // More than one node exists in the list
    
    // Find the last node in the linked list by parsing it to the end
    node_pt endNode = pool_mgr->node_heap;
    
    while(endNode->next != NULL) {
        
        // Just keep swimming...
        endNode = endNode->next;
        
    }
    
    // Wire the end node's next to the new node
    endNode->next = newNode;
    
    // Wire the new node's prev to the end node
    newNode->prev = endNode;
    
    return newNode;
    
}

// TESTED - GOOD - DEVIATED
node_pt _convert_gap_to_node_and_gap(pool_mgr_pt pool_mgr, gap_pt gap, size_t size) {
    
    //printf("Before Node\t%p\r\n", node->alloc_record.mem);
    //printf("Before Gap\t%p\r\n", gap->node->alloc_record.mem);
    
    // What about the case where the gap is just large enough?
    if(gap->size == size) {
        
        // Special case, the new node exactly fits the provided space
        const node_pt node = gap->node;
        
        node->allocated = 1;
        
        // Remove the associated gap
        _remove_gap(pool_mgr, gap);
        
        // Return the node
        return node;
        
    }
    
    // Use the existing node for allocated space
    const node_pt allocatedNode = gap->node;
    
    // Setup allocatedNode
    allocatedNode->allocated = 1;
    allocatedNode->used = 1;
    
    // Generate a new node to represent the gap
    gap->node = _add_node(pool_mgr);
    
    // Set the existing node to the correct memory
    allocatedNode->alloc_record.size = size;
    
    // Calculate new gap/node size
    gap->size = gap->node->alloc_record.size = gap->size - size;
    
    // Point the new (gap) node at the correct starting memory
    gap->node->alloc_record.mem = ((char *) allocatedNode->alloc_record.mem) + size;
    
    // Setup gap->node
    gap->node->allocated = 0;
    gap->node->used = 1;
    
    //printf("After Node\t%p\r\n", node->alloc_record.mem);
    //printf("After Gap\t%p\r\n", gap->node->alloc_record.mem);
    
    return allocatedNode;
    
}

/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/

// TESTED - GOOD - CHECKED
alloc_status mem_init() {
    
    // Ensure that it's called only once until mem_free
    if(pool_store != NULL) {
        
        // Yes it has
        return ALLOC_CALLED_AGAIN;
        
    }
    
    // allocate the pool store with initial capacity
    // Create room for MEM_POOL_STORE_INIT_CAPACITY pools
    pool_store = (pool_mgr_pt *) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_t));
    
    // Did the malloc call succeed?
    if(pool_store == NULL) {
        
        // Return ALLOC_FAIL on failure
        printf("Failed to allocate necessary memory.\r\n");
        return ALLOC_FAIL;
        
    }
    
    // Initialize the memory management system
    // Set the size and capacity
    pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
    pool_store_size = 0;
    
    // TODO ???
    // Has the system already been initialized?
    
    return ALLOC_OK;
    
}

// TESTED - GOOD - CHECKED
alloc_status mem_free() {
    
    // ensure that it's called only once for each mem_init
    if(pool_store == NULL) {
        // TODO is this the right return status?
        return ALLOC_CALLED_AGAIN;
    }
    
    // make sure all pool managers have been deallocated
    for(unsigned int i = 0; i < pool_store_size; ++i) {
        
        // Free each pool currently in use
        mem_pool_close((pool_pt) pool_store[i]);
        
    }
    
    // can free the pool store array
    // Free the pool_store
    free(pool_store);
    
    // update static variables
    pool_store = NULL;
    pool_store_size = 0;
    pool_store_capacity = 0;

    return ALLOC_OK;
    
}

// TESTED - GOOD - CHECKED
pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    
    // Make sure there the pool store is allocated
    // Has the pool_store been initialized?
    if(pool_store == NULL) {
        
        // I'm going to choose to go ahead and initialize it here
        if(mem_init() != ALLOC_OK) {
            return NULL;
        }
        
    }
    
    // Expand the pool store, if necessary
    // Do we need to grab more space?
    if(_mem_resize_pool_store() != ALLOC_OK) {
        
        // Return NULL on failure to resize
        return NULL;
        
    }
    
    // Allocate a new mem pool_mgr
    // Create the pool
    pool_mgr_pt pool_mgr = (pool_mgr_pt) calloc(1, sizeof(pool_mgr_t));
    
    // Check success, on error return null
    // Did the calloc call succeed?
    if(pool_mgr == NULL) {
        
        // It didn't :(
        return NULL;
        
    }
    
    // Initialize pool mgr
    // Setup the public facing pool
    pool_mgr->pool.alloc_size = size;
    pool_mgr->pool.total_size = 0;
    pool_mgr->pool.policy = policy;
    pool_mgr->pool.num_gaps = 0;
    pool_mgr->pool.num_allocs = 0;
    
    // Allocate a new memory pool
    // Attempt to allocate the size requested
    pool_mgr->pool.mem = (char*) malloc(size);
    
    // check success, on error deallocate mgr and return null
    // Did the malloc call succeed?
    if(pool_mgr->pool.mem == NULL) {
        
        // It didn't :(
        
        free(pool_mgr);
        
        return NULL;
        
    }
    
    // allocate a new node heap
    pool_mgr->node_heap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));
    pool_mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    pool_mgr->used_nodes = 0;
    
    // check success, on error deallocate mgr/pool and return null
    if(pool_mgr->node_heap == NULL) {
        
        // It didn't :(
        
        free(pool_mgr->pool.mem);
        free(pool_mgr);
        
        return NULL;
        
    }
    
    // allocate a new gap index
    pool_mgr->gap_ix = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    pool_mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;
    
    // check success, on error deallocate mgr/pool/heap and return null
    if(pool_mgr->gap_ix == NULL) {
        
        // It didn't :(
        
        free(pool_mgr->pool.mem);
        free(pool_mgr->node_heap);
        free(pool_mgr);
        
        return NULL;
        
    }
    
    // initialize top node of node heap
    // Add the starting node / gap representing a completely empty pool
    const node_pt node = _add_node(pool_mgr);
    
    // Configure the node
    node->alloc_record.mem = pool_mgr->pool.mem;
    node->alloc_record.size = pool_mgr->pool.alloc_size;
    node->next = NULL;
    node->prev = NULL;
    node->used = 1;
    node->allocated = 1;
    
    // initialize top node of gap index
    // Add the gap
    _add_gap(pool_mgr, node);
    
    // link pool mgr to pool store
    // Connect our new pool manager to the pointer table
    pool_store[pool_store_size] = pool_mgr;
    
    // Increment pool_store_size
    ++pool_store_size;
    
    // Return the addLess of the mgr, cast to (pool_pt)
    // Return the pointer (casted to a pool_pt)
    return (pool_pt) pool_mgr;
    
}

// TESTED - GOOD - CHECKED
alloc_status mem_pool_close(pool_pt pool) {
    
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // Upcast the pool pointer to a pool_mgr pointer
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // check if this pool is allocated
    if(pool_mgr == NULL) {
        return ALLOC_FAIL;
    }
    
    // check if pool has only one gap
    if(pool_mgr->pool.num_gaps <= 1) {
        return ALLOC_FAIL;
    }
    
    // check if it has zero allocations
    if(pool_mgr->pool.num_allocs <= 0) {
        // TODO How does this case differ?
        //return ALLOC_FAIL;
    }

    // find mgr in pool store and set to null
    for(unsigned i = 0; i < pool_store_size; ++i) {
        
        if(pool_store[i] == pool_mgr) {
            pool_store[i] = NULL;
            break;
        }
        
    }
    
    // Free the allocated memory
    // free memory pool
    free(pool_mgr->pool.mem);
    
    // Free the array of gaps
    // free gap index
    free(pool_mgr->gap_ix);
    
    // Free the array of nodes
    // free node heap
    free(pool_mgr->node_heap);
    
    // Free the pool_mgr struct
    // free mgr
    free(pool_mgr);
    
    return ALLOC_OK;
    
}

// TESTED - GOOD
// Allocates a chunk of memory
alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    
    // Get pool_mgr from pool by casting the pointer to (pool_mgr_pt)
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // Check if any gaps, return null if none
    if(pool_mgr->pool.num_gaps < 1) {
        return NULL;
    }
    
    // expand heap node, if necessary, quit on error
    // check used nodes fewer than total nodes, quit on error
    // get a node for allocation:
    // Get a new node
    
    // if FIRST_FIT, then find the first sufficient node in the node heap
    
    // if BEST_FIT, then find the first sufficient node in the gap index
    
    // check if node found
    // update metadata (num_allocs, alloc_size)
    // calculate the size of the remaining gap, if any
    // remove node from gap index
    // convert gap_node to an allocation node of given size
    // adjust node heap:
    //   if remaining gap, need a new node
    //   find an unused one in the node heap
    //   make sure one was found
    //   initialize it to a gap node
    //   update metadata (used_nodes)
    //   update linked list (new node right after the node for allocation)
    //   add to gap index
    //   check if successful
    // return allocation record by casting the node to (alloc_pt)
    
    // Configure the node
    
    // Steal some memory from the gap table
    
    // Find a block of memory from the gap table
    node_pt newNode = NULL;
    if(pool_mgr->pool.policy == FIRST_FIT) {
        
        // Look through the gaps, choose the first one (closest fitting)
        for(unsigned i = 0; i < pool_mgr->pool.num_gaps; ++i) {
            
            // Is this gap large enough?
            if(pool_mgr->gap_ix[i].size >= size) {
                
                // We found one
                newNode = _convert_gap_to_node_and_gap(pool_mgr, &(pool_mgr->gap_ix[i]), size);
                
            }
            
        }
        
    }
    
    if(pool_mgr->pool.policy == BEST_FIT) {
        
        // Look through all the gaps, find the best one (closest fitting)
        gap_pt best = NULL;
        
        for(unsigned i = 0; i < pool_mgr->pool.num_gaps; ++i) {
            
            // Is this gap large enough?
            if(pool_mgr->gap_ix[i].size >= size) {
                
                // We found one, is it smaller than best?
                if(best != NULL) {
                    
                    if(pool_mgr->gap_ix[i].size < best->size) {
                        
                        // This gap is large enough, and smaller than best, it's now the new best
                        best =  &(pool_mgr->gap_ix[i]);
                        
                    }
                    
                } else {
                    
                    // best is unpopulated, set it to this gap
                    best = &(pool_mgr->gap_ix[i]);
                    
                }
                
            }
            
        }
        
        if(best != NULL) {
            
            newNode = _convert_gap_to_node_and_gap(pool_mgr, best, size);
            
        }
        
    }
    
    if(newNode) {
        
        ++(pool_mgr->pool.num_allocs);
        
        pool_mgr->pool.total_size += size;
        
        return &(newNode->alloc_record);
        
    } else {
        
        printf("Failed to alloc memory!\r\n");
        
        return NULL;
        
    }
    
}

// TESTED - GOOD
// Deallocates a chunk of memory
alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    
    const size_t size = alloc->size;
    
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // get node from alloc by casting the pointer to (node_pt)
    // find the node in the node heap
    // this is node-to-delete
    // make sure it's found
    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    // if the next node in the list is also a gap, merge into node-to-delete
    //   remove the next node from gap index
    //   check success
    //   add the size to the node-to-delete
    //   update node as unused
    //   update metadata (used nodes)
    //   update linked list:
    /*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success
    //   add the size of node-to-delete to the previous
    //   update node-to-delete as unused
    //   update metadata (used_nodes)
    //   update linked list
    /*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // Upcast to node
    const node_pt node = (node_pt) alloc;
    
    // Call the internal function that will handle this
    
    if(_mem_add_to_gap_ix(pool_mgr, alloc->size, node) == ALLOC_OK) {
        
        --(pool_mgr->pool.num_allocs);
        
        pool_mgr->pool.total_size -= size;
        
        return ALLOC_OK;
        
    } else {
        
        return ALLOC_FAIL;
        
    }
    
}

// TESTED - GOOD - CHECKED
// Allocates a dynamic array. Caller responsible for releasing.
void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {
    
    // get the mgr from the pool
    // Upcast the pool pointer to a pool_mgr pointer
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // allocate the segments array with size == used_nodes
    *segments = (pool_segment_pt) calloc(pool_mgr->used_nodes, sizeof(pool_segment_t));
    
    // check successful
    if(segments == NULL) {
        return;
    }
    
    // loop through the node heap and the segments array
    int currentSegment = 0;
    
    /*node_pt currentNode = pool_mgr->node_heap;
    
    // Traverse the linked list
    while(currentNode) {
        
        if(currentNode->used != 0) {
            (*segments)[currentSegment].size = currentNode->alloc_record.size;
            (*segments)[currentSegment].allocated = currentNode->allocated;
            ++currentSegment;
        }
        
        currentNode = currentNode->next;
    }*/
    
    for(int i = 0; i < pool_mgr->used_nodes; ++i) {
        
        // Skip unused nodes
        if(pool_mgr->node_heap[i].used == 0) {
            continue;
        }
        
        // for each node, write the size and allocated in the segment
        (*segments)[currentSegment].size = pool_mgr->node_heap[i].alloc_record.size;
        (*segments)[currentSegment].allocated = pool_mgr->node_heap[i].allocated;
        
        ++currentSegment;
        
    }
    
    // "return" the values:
    *num_segments = currentSegment;
    return;
    
}

/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/

// TESTED - GOOD - CHECKED
static alloc_status _mem_resize_pool_store() {
    
    // check if necessary
    /*
                if (((float) pool_store_size / pool_store_capacity)
                    > MEM_POOL_STORE_FILL_FACTOR) {...}
     */
    // don't forget to update capacity variables
    
    // Are too many pools in use?
    if(pool_store_size < pool_store_capacity * MEM_POOL_STORE_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails,
    // we'd rather deny the mem_pool_open call rather than obliterate the entire pool_store
    
    pool_mgr_pt * bob = (pool_mgr_pt *) realloc(pool_store, pool_store_capacity * MEM_POOL_STORE_EXPAND_FACTOR * sizeof(pool_mgr_pt));
    
    // Did the realloc call succeed?
    if(bob == NULL) {
        
        // Return NULL on failure
        printf("Failed to resize node heap.\r\n");
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_store = bob;
        
        // Modify the pool_store_capacity to match the new size
        pool_store_capacity *= MEM_POOL_STORE_EXPAND_FACTOR;
        
    }

    return ALLOC_OK;
    
}

// TESTED - GOOD - CHECKED
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    
    // Are too many pools in use?
    if(pool_mgr->used_nodes < pool_mgr->total_nodes * MEM_NODE_HEAP_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails
    
    node_pt bob = (node_pt) realloc(pool_mgr->node_heap, pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR * sizeof(node_t));
    
    // Did the realloc call succeed?
    if(bob == NULL) {
        
        // Return NULL on failure
        printf("Failed to resize node heap.\r\n");
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_mgr->node_heap = bob;
        
        // Modify the pool_store_capacity to match the new size
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
        
    }
    
    return ALLOC_OK;
    
}

// TESTED - GOOD - CHECKED
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    
    // Are too many gaps in use?
    if(pool_mgr->gap_ix_capacity < pool_mgr->gap_ix_capacity * MEM_GAP_IX_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails
    
    gap_pt bob = (gap_pt) realloc(pool_mgr->gap_ix, pool_mgr->gap_ix_capacity * MEM_GAP_IX_EXPAND_FACTOR * sizeof(gap_t));
    
    // Did the realloc call succeed?
    if(bob == NULL) {
        
        // Return NULL on failure
        printf("Failed to resize gap index.\r\n");
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_mgr->gap_ix = bob;
        
        // Modify the pool_store_capacity to match the new size
        pool_mgr->gap_ix_capacity *= MEM_GAP_IX_EXPAND_FACTOR;
        
    }
    
    return ALLOC_OK;
    
}

// TESTED - GOOD - DEVIATED
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // expand the gap index, if necessary (call the function)
    // add the entry at the end
    // update metadata (num_gaps)
    // sort the gap index (call the function)
    // check success
    
    return _add_gap(pool_mgr, node);
    
}

// DIDN'T USE
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // No gap found, the pool is full
    printf("Failed to find available memory in pool.\r\n");
    return ALLOC_FAIL;
    
}

// TESTED - GOOD - CHECKED
int _partitionGap(gap_pt gaps, int l, int r) {
    
    // Jacked a quick and dirty QuickSort implementation from
    // http://www.comp.dit.ie/rlawlor/Alg_DS/sorting/quickSort.c
    
    // We'll need to modify this to work with the gap index...

    // expand the gap index, if necessary (call the function)
    // add the entry at the end
    // update metadata (num_gaps)
    // sort the gap index (call the function)
    // check success

    int i = l, j = r + 1;
    
    gap_t t;
    gap_pt pivot;
    
    pivot = &(gaps[l]);
    
    while(1) {
        
        do ++i; while(gaps[i].size <= pivot->size && i <= r );
        
        do --j; while(gaps[j].size > pivot->size);
        
        if(i >= j) break;
        
        t = gaps[i];
        gaps[i] = gaps[j];
        gaps[j] = t;
        
    }
    
    t = gaps[l];
    gaps[l] = gaps[j];
    gaps[j] = t;
    
    return j;
    
}

// TESTED - GOOD - CHECKED
void _quickSortGap(gap_pt gaps, int l, int r) {
    
    // Jacked a quick and dirty QuickSort implementation from
    // http://www.comp.dit.ie/rlawlor/Alg_DS/sorting/quickSort.c
    
    // We'll need to modify this to work with the gap index...

    if(l < r) {
        
        int j = _partitionGap(gaps, l, r);
        
        _quickSortGap(gaps, l, j - 1);
        _quickSortGap(gaps, j + 1, r);
        
    }
    
}

// TESTED - GOOD - CHECKED
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    
    // loop from num_gaps - 1 until but not including 0:
    //    if the size of the current entry is less than the previous (u - 1)
    //       swap them (by copying) (remember to use a temporary variable)
    
    // Sort the gap list, smallest sizes first
    _quickSortGap(pool_mgr->gap_ix, 0, pool_mgr->pool.num_gaps - 1);
    
    return ALLOC_OK;
    
}
