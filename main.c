#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "mem_pool.h"

/* forward declarations */
void print_pool(pool_pt pool);
void print_pool2(pool_pt pool);

#define MEGABYTE * 1 * 1000 * 1000

/* main */
int main(int argc, char *argv[]) {

    mem_init();
    
    pool_pt pool = mem_pool_open(100 MEGABYTE, BEST_FIT);
    
    print_pool2(pool);
    
    alloc_pt alloc1 = mem_new_alloc(pool, 10 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc2 = mem_new_alloc(pool, 12 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc3 = mem_new_alloc(pool, 2 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc4 = mem_new_alloc(pool, 30 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc5 = mem_new_alloc(pool, 4 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc6 = mem_new_alloc(pool, 1 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc7 = mem_new_alloc(pool, 12 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc8 = mem_new_alloc(pool, 9 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc9 = mem_new_alloc(pool, 3 MEGABYTE);
    
    print_pool2(pool);
    
    alloc_pt alloc10 = mem_new_alloc(pool, 1 MEGABYTE);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc6);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc8);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc7);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc5);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc4);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc3);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc2);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc1);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc10);
    
    print_pool2(pool);
    
    mem_del_alloc(pool, alloc9);
    
    print_pool2(pool);
    
    mem_free();
    
    return 0;
    
}

void print_pool(pool_pt pool) {
    
    pool_segment_pt segs = NULL;
    
    unsigned size = 0;

    assert(pool);

    mem_inspect_pool(pool, segs, &size);

    assert(segs);
    assert(size);

    for(unsigned u = 0; u < size; ++u) {
        printf("%10lu - %s\n", (unsigned long) segs[u].size, (segs[u].allocated) ? "alloc" : "gap");
    }
    
    free(segs);

    printf("\n");
    
}