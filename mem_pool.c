/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "mem_pool.h"

/* Constants */
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

/* Type declarations */
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
    
    gap_pt * gap_ix;
    
    unsigned total_gaps;
    
    unsigned used_gaps;
    
} pool_mgr_t, *pool_mgr_pt;

/* Static global variables */
static pool_mgr_pt * pool_store = NULL; // an array of pointers, only expand
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
        
        // Free each gap
        for(unsigned int j = 0; j < pool_store[i]->used_gaps; ++j) {
            
            free(pool_store[i]->gap_ix[j]);
            
        }
        
        // Free the actual array of gap pointers
        free(pool_store[i]->gap_ix);
        
        // Free each node
            
            // Set peter to the first node in the heap
            node_pt pete = pool_store[i]->node_heap;
            
            // Traverse the linked list to the end, clearing all the nodes before us
            while(pete->next != NULL) {
                
                pete = pete->next;
                
                free(pete->prev);
                
            }
            
            // We may have to kill pete...
            free(pete);
        
        // Finally we free the pool and make sure to set that pool_store pointer to NULL
        free(pool_store[i]);
        pool_store = NULL;
        
    }
    
    // And last but not least, we free the pool_store
    free(pool_store);

    return ALLOC_FAIL;
    
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
        
        // We'll use a temporary pointer, in the event that the realloc call fails,
        // we'd rather deny the mem_pool_open call rather than obliterate the entire pool_store
        
        pool_mgr_pt * bob = (pool_mgr_pt *) realloc(pool_store, pool_store_size * MEM_POOL_STORE_EXPAND_FACTOR * sizeof(pool_mgr_t));

        // Did the realloc call succeed?
        if(bob == NULL) {
            
            // Return NULL on failure
            return NULL;
            
        } else {
            
            // Assign bob to the pool_store
            pool_store = bob;
            
            // Modify the pool_store_size to match the new size
            pool_store_size *= MEM_POOL_STORE_EXPAND_FACTOR;
            
        }
        
    }
    
    // Setup the pool
    ++pool_store_capacity;
    
    // Grab a pointer to the actual pool manager we're setting up
    pool_mgr_pt ourPoolMgr = pool_store[pool_store_capacity - 1];
    
    // Setup the policy
    ourPoolMgr->pool.policy = policy;
    
    // May as well actually allocate the size requested, or at least try to
    ourPoolMgr->pool.mem = malloc(size);
    
    // Did the malloc call succeed?
    if(ourPoolMgr->pool.mem == NULL) {
        
        // Return NULL on failure
        return NULL;
        
    }
    
    // Setup the size
    ourPoolMgr->pool.alloc_size = size;
    
    // NULL everything else, because this memory may have been obtained with a realloc call
    // meaning we'll have garbage everywhere, which is not good
    
    ourPoolMgr->pool.num_allocs = 0;
    ourPoolMgr->pool.num_gaps = 0;
    ourPoolMgr->pool.total_size = 0;
    ourPoolMgr->gap_ix = NULL;
    ourPoolMgr->node_heap = NULL;
    ourPoolMgr->total_nodes = 0;
    ourPoolMgr->used_nodes = 0;

    // Return dat pointer (casted to a pool_pt) to stop XCode from bitching
    return (pool_pt) ourPoolMgr;
    
}

alloc_status mem_pool_close(pool_pt pool) {
    
    // TODO implement

    return ALLOC_FAIL;
    
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    
    // TODO implement

    return NULL;
    
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    
    // TODO implement

    return ALLOC_FAIL;
    
}

// NOTE: Allocates a dynamic array. Caller responsible for releasing.
void mem_inspect_pool(pool_pt pool, pool_segment_pt segments, unsigned *num_segments) {
    
    // TODO implement
    
}


/* Definitions of static functions */
static alloc_status _mem_resize_pool_store() {
    
    // TODO implement

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    
    // TODO implement

    return ALLOC_FAIL;
    
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    
    // TODO implement

    return ALLOC_FAIL;
    
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // TODO implement

    return ALLOC_FAIL;
    
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // TODO implement

    return ALLOC_FAIL;
    
}


static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {

    // TODO implement

    return ALLOC_FAIL;
    
}


