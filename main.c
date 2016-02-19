#include <stdio.h>
#include <assert.h>

#include "mem_pool.h"

#define MEGABYTE * 1 * 1000 * 1000

int main(int argc, char *argv[]) {

    mem_init();
    
    pool_pt pool = mem_pool_open(100 * 1000 * 1000, BEST_FIT);
    
    print_pool(pool);
    
    alloc_pt alloc1 = mem_new_alloc(pool, 10 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc2 = mem_new_alloc(pool, 12 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc3 = mem_new_alloc(pool, 2 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc4 = mem_new_alloc(pool, 30 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc5 = mem_new_alloc(pool, 4 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc6 = mem_new_alloc(pool, 1 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc7 = mem_new_alloc(pool, 12 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc8 = mem_new_alloc(pool, 9 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc9 = mem_new_alloc(pool, 3 MEGABYTE);
    
    print_pool(pool);
    
    alloc_pt alloc10 = mem_new_alloc(pool, 1 MEGABYTE);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc6);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc8);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc7);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc5);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc4);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc3);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc2);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc1);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc10);
    
    print_pool(pool);
    
    mem_del_alloc(pool, alloc9);
    
    print_pool(pool);
    
    mem_free();
    
    return 0;
    
}