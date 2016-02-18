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

// REVIEWED - LOOKS GOOD
// Simply removes a gap from the gap array, no holes, resorts
static alloc_status _remove_gap(pool_mgr_pt pool_mgr, gap_pt gap) {
    
    // Get a reference to the last gap
    const gap_pt lastGap = &(pool_mgr->gap_ix[pool_mgr->used_gaps - 1]);
    
    // Copy the end gap to the position of the to be deleted gap
    *gap = *lastGap;
    
    // Nothing directly links to gaps, other than the gap_ix table,
    // so there's no cleanup work to do
    
    // Our to-be-deleted gap is now gone, decrement the used_gaps count
    --(pool_mgr->used_gaps);
    
    // NOTE: This does leave a copy of the last gap in the gap table,
    // however we're now considering that gap as available space, not
    // to be indexed, sorted and available for overriting.
    
    // Nevertheless, let's nuke it anyway (just in case)
    lastGap->node = NULL;
    lastGap->size = 0;
    
    // We've altered the gap list, so let's resort it
    return _mem_sort_gap_ix(pool_mgr);
    
}

// REVIEWED - LOOKS GOOD
// Simply removes a node from the node array, no holes, handles relinking
static alloc_status _remove_node(pool_mgr_pt pool_mgr, node_pt node) {
    
    // We're going to remove the node from the linked list, but first we must deal with relinking
    // (just like a tyical linked-list node removal)
    
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
    
    // Get a reference to the last gap
    const node_pt lastNode = &(pool_mgr->node_heap[pool_mgr->used_nodes - 1]);
    
    // Copy the end node to the position of the to be deleted node
    *node = *lastNode;
    
    // Other nodes may have directly linked to the node we just moved
    // (lastNode, now node) so we'll need to relink them
    if(node->prev != NULL) {
        
        node->prev->next = node;
        
    }
    
    if(node->next != NULL) {
        
        node->next->prev = node;
        
    }
    
    // We'll also need to relink any gaps that pointed to this node
    for(unsigned int i = 0; i < pool_mgr->used_gaps; ++i) {
        
        if(pool_mgr->gap_ix[i].node == lastNode) {
            
            // Gap pointed to the old node position, relink it to the new position
            pool_mgr->gap_ix[i].node = node;
            
        }
        
    }
    
    // Our to-be-deleted node is now gone, decrement the used_nodes count
    --(pool_mgr->used_nodes);
    
    // NOTE: This does leave a copy of the last node in the node table,
    // however we're now considering that node as available space, not
    // to be indexed, sorted and available for overriting.
    
    // Nevertheless, let's nuke it anyway (just in case)
    lastNode->next = NULL;
    lastNode->prev = NULL;
    lastNode->allocated = 0;
    lastNode->alloc_record.mem = NULL;
    lastNode->alloc_record.size = 0;
    
    return ALLOC_OK;
    
}

// REVIEWED - LOOKS GOOD
// Creates a new gap from an existing node
static gap_pt _add_gap(pool_mgr_pt pool_mgr, node_pt node) {
    
    // Look above for gaps (gap merging)
    for(unsigned int i = 0; i < pool_mgr->used_gaps; ++i) {
        
        // Calculate the position in memory where the currently selected gap ends
        
        // Start with the address of the currently selected gap, add it's size as an offset
        const void * endingLocation = &(pool_mgr->gap_ix[i].node->alloc_record.mem) + pool_mgr->gap_ix[i].size;

        // Does this gap end where we begin?
        if(endingLocation == &(node->alloc_record.mem)) {
            
            // There's an adjacent gap above us, let's use it
            
            // Expand previous gap
            pool_mgr->gap_ix[i].size += node->alloc_record.size;
            pool_mgr->gap_ix[i].node->alloc_record.size = pool_mgr->gap_ix[i].size;
            
            // Remove the node we were passed
            _remove_node(pool_mgr, node);
            
            return &(pool_mgr->gap_ix[i]);
            
        }
        
    }
    
    // No gap to merge, create a new one
    
    // Do we need to grab more space?
    if(_mem_resize_gap_ix(pool_mgr) != ALLOC_OK) {
        
        // Return NULL on failure to resize
        return NULL;
        
    }
    
    // Increment used_gaps
    ++(pool_mgr->used_gaps);
    
    // Get a reference to the new gap we just "created"
    const gap_pt newGap = &(pool_mgr->gap_ix[pool_mgr->used_gaps - 1]);

    // Setup the new gap
    newGap->size = node->alloc_record.size;
    newGap->node = node;
    
    // We've altered the gap list, so let's resort it
    return (_mem_sort_gap_ix(pool_mgr) == ALLOC_OK) ? newGap : NULL;
    
}

// REVIEWED - LOOKS GOOD
// Simply adds a node to the end of the node array, no holes, handles relinking
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

// INCOMPLETE
// Splits a given gap into an allocated node and a gap node
static alloc_status _convert_gap_to_node_and_gap(pool_mgr_pt pool_mgr, node_pt node, gap_pt gap) {
    
    // The provided node should already be in the node heap
    
    // The provided gap can be reused to represent the extra left over space
    // however it will need a new node
    
}

// TESTED - GOOD
// Initializes the pool_store struct
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

// TESTED - GOOD
// Frees entire system
alloc_status mem_free() {
    
    for(unsigned int i = 0; i < pool_store_capacity; ++i) {
        
        // Free each pool currently in use
        mem_pool_close((pool_pt) pool_store[i]);
        
    }
    
    // Free the pool_store
    free(pool_store);
    
    pool_store = NULL;

    return ALLOC_OK;
    
}

// TESTED - GOOD
// Creates a new memory pool, and initializes it
pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    
    // Has the pool_store been initialized?
    if(pool_store == NULL) {
        
        // I'm going to choose to go ahead and initialize it here
        mem_init();
        
    }
    
    // Do we need to grab more space?
    if(_mem_resize_pool_store() != ALLOC_OK) {
        
        // Return NULL on failure to resize
        return NULL;
        
    }
    
    // Increment pool_store_capacity
    ++pool_store_capacity;
    
    // Create the pool
    pool_mgr_pt pool_mgr = calloc(1, sizeof(pool_mgr_t));
    
    // Did the calloc call succeed?
    if(pool_mgr == NULL) {
        
        // It didn't :(
        return NULL;
        
    }
    
    // Connect our new pool manager to the pointer table
    pool_store[pool_store_capacity - 1] = pool_mgr;
    
    // Setup the policy
    pool_mgr->pool.policy = policy;
    
    // May as well actually allocate the size requested, or at least try to
    pool_mgr->pool.mem = malloc(size);
    
    // Did the malloc call succeed?
    if(pool_mgr->pool.mem == NULL) {
        
        // It didn't :(
        
        free(pool_mgr);
        
        return NULL;
        
    }
    
    pool_mgr->pool.alloc_size = size;
    
    pool_mgr->gap_ix = calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    pool_mgr->total_gaps = MEM_GAP_IX_INIT_CAPACITY;
    pool_mgr->used_gaps = 0;
    
    pool_mgr->node_heap = calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));
    pool_mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    pool_mgr->used_nodes = 0;
    
    // Did those calloc calls succeed?
    if(pool_mgr->gap_ix == NULL || pool_mgr->node_heap == NULL) {
        
        // It didn't :(
        
        free(pool_mgr->pool.mem);
        free(pool_mgr->gap_ix);
        free(pool_mgr->node_heap);
        free(pool_mgr);
        
        return NULL;
        
    }

    // Return dat pointer (casted to a pool_pt)
    return (pool_pt) pool_mgr;
    
}

// TESTED - GOOD
// Destroys a memory pool, freeing any used memory and removes it from the pool store
alloc_status mem_pool_close(pool_pt pool) {
    
    // Upcast the pool pointer to a pool_mgr pointer
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // Find the provided pool in the pool_store list
    
    // We're going to do this first, so if the pool isn't found we haven't modified it
    
    unsigned int storeIndex = 0;
    
    while(pool_store[storeIndex] != pool_mgr) {
        
        if(storeIndex >= pool_store_capacity) {
            
            // We've exceeded the store capacity, pool not found
            return ALLOC_NOT_FREED;
            
        }
        
        // Increment storeIndex
        ++storeIndex;
        
    }
    
    // Free the allocated memory
    free(pool_mgr->pool.mem);
    
    // Free the array of gaps
    free(pool_mgr->gap_ix);
    
    // Free the array of nodes
    free(pool_mgr->node_heap);
    
    // Free the pool_mgr struct
    free(pool_mgr);
    
    // Reorganize the pool store pointers
    // (send the last pointer here, unless we are the last)
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
    free(pool_mgr);

    return ALLOC_OK;
    
}

// REVIEWED - LOOKS GOOD
// Allocates a chunk of memory
alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // Get a new node
    const node_pt node = _add_node(pool_mgr);
    
    // Configure the node
    node->used = 1;         // TODO: Check on this, not sure what it actually is
    node->allocated = 1;
    node->alloc_record.size = size;
    
    // Steal some memory from the gap table
    return (_mem_remove_from_gap_ix(pool_mgr, size, node) == ALLOC_OK) ? &(node->alloc_record) : NULL;
    
}

// REVIEWED - LOOKS GOOD
// Deallocates a chunk of memory
alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {

    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    
    // Upcast to node
    const node_pt node = (node_pt) alloc;
    
    // Call the internal function that will handle this
    return _mem_add_to_gap_ix(pool_mgr, alloc->size, node);
    
}

// INCOMPLETE
// Allocates a dynamic array. Caller responsible for releasing.
void mem_inspect_pool(pool_pt pool, pool_segment_pt segments, unsigned *num_segments) {
    
    
    
}

// REVIEWED - LOOKS GOOD
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

// REVIEWED - LOOKS GOOD
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
        return ALLOC_FAIL;
        
    } else {
        
        // Assign bob to the pool_store
        pool_mgr->node_heap = bob;
        
        // Modify the pool_store_size to match the new size
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
        
    }
    
    return ALLOC_OK;
    
}

// REVIEWED - LOOKS GOOD
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    
    // Are too many gaps in use?
    if(pool_mgr->used_gaps < pool_mgr->total_gaps * MEM_GAP_IX_FILL_FACTOR) {
        
        // NO, do nothing
        return ALLOC_OK;
        
    }
    
    // We'll use a temporary pointer, in the event that the realloc call fails
    
    gap_pt bob = (gap_pt) realloc(pool_mgr->gap_ix, pool_mgr->total_gaps * MEM_GAP_IX_EXPAND_FACTOR * sizeof(gap_t));
    
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

// REVIEWED - LOOKS GOOD
// Converts a node to a gap, making that memory available to be allocated later
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // Mark node as gap
    node->allocated = 0;
    
    // Get a new gap
    const gap_pt gap = _add_gap(pool_mgr, node);
   
    return (gap != NULL) ? ALLOC_OK : ALLOC_FAIL;
    
}

// REVIEWED - LOOKS GOOD
// Finds a gap based on the allocation policy, splits that gap into an allocated node and a new gap
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    
    // Mark node as used
    node->allocated = 1;
    
    // Find a block of memory from the gap table
    
    if(pool_mgr->pool.policy == FIRST_FIT) {
        
        // Look through the gaps, choose the first one (closest fitting)
        for(unsigned i = 0; i < pool_mgr->used_gaps; ++i) {
            
            // Is this gap large enough?
            if(pool_mgr->gap_ix[i].size >= size) {
                
                // We found one
                return _convert_gap_to_node_and_gap(pool_mgr, node, &(pool_mgr->gap_ix[i]));
                
            }
            
        }
        
    }
    
    if(pool_mgr->pool.policy == BEST_FIT) {
        
        // Look through all the gaps, find the best one (closest fitting)
        gap_pt best = NULL;
        
        for(unsigned i = 0; i < pool_mgr->used_gaps; ++i) {
            
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
            
            return _convert_gap_to_node_and_gap(pool_mgr, node, best);
            
        }
        
    }
    
    // No gap found, the pool is full
    return ALLOC_FAIL;
    
}

// REVIEWED - LOOKS GOOD
int partitionGap(gap_pt gaps, int l, int r) {
    
    // Jacked a quick and dirty QuickSort implementation from
    // http://www.comp.dit.ie/rlawlor/Alg_DS/sorting/quickSort.c
    
    // We'll need to modify this to work with the gap index...

    int i = l, j = r + 1;
    
    gap_pt t;
    gap_pt pivot;
    
    pivot = &(gaps[l]);
    
    while(1) {
        
        do ++i; while(gaps[i].size <= pivot->size && i <= r );
        
        do --j; while(gaps[j].size > pivot->size);
        
        if(i >= j) break;
        
        t = &(gaps[i]);
        
        gaps[i] = gaps[j];
        gaps[j] = *t;
        
    }
    
    t = &gaps[l];
    gaps[l] = gaps[j];
    gaps[j] = *t;
    
    return j;
    
}

// REVIEWED - LOOKS GOOD
void quickSortGap(gap_pt gaps, int l, int r) {
    
    // Jacked a quick and dirty QuickSort implementation from
    // http://www.comp.dit.ie/rlawlor/Alg_DS/sorting/quickSort.c
    
    // We'll need to modify this to work with the gap index...

    if(l < r) {
        
        int j = partitionGap(gaps, l, r);
        
        quickSortGap(gaps, l, j - 1);
        quickSortGap(gaps, j + 1, r);
        
    }
    
}

// REVIEWED - LOOKS GOOD
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    
    // Sort the gap list, smallest sizes first
    quickSortGap(pool_mgr->gap_ix, 0, pool_mgr->used_gaps - 1);
    
    return ALLOC_OK;
    
}


