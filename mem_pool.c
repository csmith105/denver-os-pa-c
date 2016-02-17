/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "mem_pool.h"

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

typedef struct _node {
    
    alloc_t alloc_record;
    
    unsigned used;
    
    unsigned allocated;
    
    struct _node *next, *prev; // doubly-linked list for gap deletion
    
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
    
    unsigned total_gaps;
    
    unsigned used_gaps;
    
} pool_mgr_t, *pool_mgr_pt;

/* Static global variables */
static pool_mgr_pt * pool_store = NULL;
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;

/* Forward declarations of static functions */
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);

/* Definitions of user-facing functions */

alloc_status mem_init() {
    
    // Initialize the memory management system
    
    // Has the system already been initialized?
    if(pool_store != NULL) {
        
        // Yes it has
        return ALL0C_CALLED_AGAIN;
        
    }
    
    // Create room for MEM_POOL_STORE_INIT_CAPACITY pools
    pool_store = (pool_mgr_pt *) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_t));
    
    // Did the malloc call succeed?
    if(pool_store == NULL) {
        
        // Return ALLOC_FAIL on failure
        return ALLOC_FAIL;
        
    }
    
    // Set the size and capacity
    pool_store_size = MEM_POOL_STORE_INIT_CAPACITY;
    pool_store_capacity = 0;

    return ALLOC_OK;
    
}

alloc_status mem_free() {
    
    // Basically free everything, a rather tricky endevour to do correctly
    
    for(unsigned int i = 0; i < pool_store_capacity; ++i) {
        
        // For each pool in use
        mem_pool_close((pool_pt) pool_store[i]);
        
    }
    
    // Free the pool_store
    free(pool_store);
    
    pool_store = NULL;

    return ALLOC_OK;
    
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    
    // Has the pool_store been initialized?
    if(pool_store == NULL) {
        
        // I'm going to choose to go ahead and initialize it here
        mem_init();
        
    }
    
    // Are too many pools in use?
    if(pool_store_capacity >= pool_store_size * MEM_POOL_STORE_FILL_FACTOR) {
        
        // We need to resize
        if (_mem_resize_pool_store() != ALLOC_OK) {
            
            // Return NULL on failure to resize
            return NULL;
            
        }
        
    }
    
    // Setup the pool
    ++pool_store_capacity;
    
    // Grab a pointer to the actual pool manager we're setting up
    pool_mgr_pt ourPoolMgr = pool_store[pool_store_capacity - 1];
    
    // Create the pool
    ourPoolMgr = calloc(1, sizeof(pool_mgr_t));
    
    // Did the calloc call succeed?
    if(ourPoolMgr == NULL) {
        
        // It didn't :(
        return NULL;
        
    }
    
    // Setup the policy
    ourPoolMgr->pool.policy = policy;
    
    // May as well actually allocate the size requested, or at least try to
    ourPoolMgr->pool.mem = malloc(size);
    
    // Did the malloc call succeed?
    if(ourPoolMgr->pool.mem == NULL) {
        
        // It didn't :(
        return NULL;
        
    }
    
    // Setup the size
    ourPoolMgr->pool.alloc_size = size;
    
    ourPoolMgr->gap_ix = calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));

    // Return dat pointer (casted to a pool_pt) to stop the compiler from bitching
    return (pool_pt) ourPoolMgr;
    
}

alloc_status mem_pool_close(pool_pt pool) {
    
    // Upcast the pool pointer to a pool_mgr pointer
    const pool_mgr_pt poolManager = (pool_mgr_pt) pool;
    
    // Find the provided pool in the pool_store list
    
    // We're going to do this first, so if the pool isn't found we haven't modified it
    
    unsigned int storeIndex = 0;
    
    while(pool_store[storeIndex] != poolManager) {
        
        if(storeIndex >= pool_store_capacity) {
            
            // We've exceeded the store capacity, pool not found
            return ALLOC_NOT_FREED;
            
        }
        
        // Increment storeIndex
        ++storeIndex;
        
    }
    
    // Free each gap
    for(unsigned int j = 0; j < poolManager->used_gaps; ++j) {
        
        free(&(poolManager->gap_ix[j]));
        
    }
    
    // Free the actual array of gap pointers
    free(poolManager->gap_ix);
    
    // Free each node
    for(unsigned int j = 0; j < poolManager->used_nodes; ++j) {
        
        free(&(poolManager->node_heap[j]));
        
    }
    
    // Free the actual array of gap pointers
    free(poolManager->node_heap);
    
    /*// Set peter to the first node in the heap
    node_pt pete = poolManager->node_heap;
    
    // Traverse the linked list to the end, clearing all the nodes before us
    while(pete->next != NULL) {
        
        pete = pete->next;
        
        free(pete->prev);
        
    }
    
    // We may have to kill my step-dad...
    free(pete);*/
    
    // Reorganize the pool store pointers (basically send the last pointer here, unless we are the last)
    if(storeIndex == pool_store_capacity - 1) {
        
        // Last store, just set it to NULL
        
        pool_store[storeIndex] = NULL;
        
    } else {
        
        // Not the last store, swap it for the last pointer then set that pointer to NULL
        
        pool_store[storeIndex] = pool_store[pool_store_capacity - 1];
        
        pool_store[pool_store_capacity - 1] = NULL;
        
    }
    
    // Decrement pool_store_capacity
    --pool_store_capacity;
    
    // Free the pool
    free(poolManager);

    return ALLOC_OK;
    
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    
    const pool_mgr_pt poolManager = (pool_mgr_pt) pool;
    
    // Find a block of memory
    
    if(poolManager->pool.policy == FIRST_FIT) {
        
        // Look through the gaps, choose the first one big enough
        
    }
    
    if (poolManager->pool.policy == BEST_FIT) {
        
        // Look through all the gaps, find the best one
        
    }

    return NULL;
    
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {

    return ALLOC_FAIL;
    
}

// NOTE: Allocates a dynamic array. Caller responsible for releasing.
void mem_inspect_pool(pool_pt pool, pool_segment_pt segments, unsigned *num_segments) {
    
    
    
}

static alloc_status _mem_resize_pool_store() {
    
    // Are too many pools in use?
    if(pool_store_capacity < pool_store_size * MEM_POOL_STORE_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails,
    // we'd rather deny the mem_pool_open call rather than obliterate the entire pool_store
    
    pool_mgr_pt * bob = (pool_mgr_pt *) realloc(pool_store, pool_store_size * MEM_POOL_STORE_EXPAND_FACTOR * sizeof(pool_mgr_pt));
    
    // Did the realloc call succeed?
    if(bob == NULL) {
        
        // Return NULL on failure
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_store = bob;
        
        // Modify the pool_store_size to match the new size
        pool_store_size *= MEM_POOL_STORE_EXPAND_FACTOR;
        
    }

    return ALLOC_OK;
    
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {

    // Are too many pools in use?
    if(pool_mgr->used_nodes < pool_mgr->total_nodes * MEM_NODE_HEAP_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails
    
    node_pt bob = (node_pt) realloc(pool_mgr->node_heap, pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR * sizeof(node_pt));
    
    // Did the realloc call succeed?
    if(bob == NULL) {
        
        // Return NULL on failure
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_mgr->node_heap = bob;
        
        // Modify the pool_store_size to match the new size
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
        
    }
    
    return ALLOC_OK;
    
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    
    // Are too many gaps in use?
    if(pool_mgr->used_gaps < pool_mgr->total_gaps * MEM_GAP_IX_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails
    
    gap_pt bob = (gap_pt) realloc(pool_mgr->gap_ix, pool_mgr->total_gaps * MEM_GAP_IX_EXPAND_FACTOR * sizeof(gap_pt));
    
    // Did the realloc call succeed?
    if(bob == NULL) {
        
        // Return NULL on failure
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_mgr->gap_ix = bob;
        
        // Modify the pool_store_size to match the new size
        pool_mgr->total_gaps *= MEM_GAP_IX_EXPAND_FACTOR;
        
    }
    
    return ALLOC_OK;
    
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // Make sure gap_ix is large enough
    if(_mem_resize_gap_ix(pool_mgr) == ALLOC_FAIL) {
        
        // Resize needed but could not be preformed
        return ALLOC_FAIL;
        
    }
    
    // Grab the next existing gap and configure it
    gap_pt gap = &(pool_mgr->gap_ix[pool_mgr->used_gaps++]);
    
    // Setup the gap
    gap->node = node;
    gap->size = size;
    
    // Remove node from node_heap
    for(unsigned int i = 0; i < pool_mgr->used_gaps; ++i) {
        
        // Is this the node?
        if(&(pool_mgr->node_heap[i]) == node) {
            
            // It is
            
            // We're going to remove it from the linked list, but first we must deal with relinking
            // (just like a tyical linked-list node removal)
            
            if(node->prev != NULL) {
                node->prev->next = node->next;
            }
            
            if(node->next != NULL) {
                node->next->prev = node->prev;
            }
            
            // Then we're going to take the last node (last position) and swap it into the hole
            node = &(pool_mgr->node_heap[pool_mgr->used_nodes - 2]);
            
            // Deal with relinking
            if(node->prev != NULL) {
                node->prev->next = node;
            }
            
            if(node->next != NULL) {
                node->next->prev = node;
            }
            
            --(pool_mgr->used_nodes);
            
            break;
            
        }
        
    }
    
    // Reorder gaps
    _mem_sort_gap_ix(pool_mgr);

    return ALLOC_FAIL;
    
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    

    return ALLOC_FAIL;
    
}


static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {

    

    return ALLOC_FAIL;
    
}


