#include <stdio.h>
#include <assert.h>

#include "mem_pool.h"

int main(int argc, char *argv[]) {

    printf("Testing C Language Programming Assignment\n");

    mem_init();
    
    pool_pt pool1 = mem_pool_open(1 * 1000 * 1000, BEST_FIT);
    pool_pt pool2 = mem_pool_open(2 * 1000 * 1000, BEST_FIT);
    pool_pt pool3 = mem_pool_open(3 * 1000 * 1000, BEST_FIT);
    pool_pt pool4 = mem_pool_open(4 * 1000 * 1000, BEST_FIT);
    pool_pt pool5 = mem_pool_open(5 * 1000 * 1000, BEST_FIT);
    pool_pt pool6 = mem_pool_open(6 * 1000 * 1000, BEST_FIT);
    pool_pt pool7 = mem_pool_open(7 * 1000 * 1000, BEST_FIT);
    pool_pt pool8 = mem_pool_open(8 * 1000 * 1000, BEST_FIT);
    pool_pt pool9 = mem_pool_open(9 * 1000 * 1000, BEST_FIT);
    pool_pt pool10 = mem_pool_open(10 * 1000 * 1000, BEST_FIT);
    
    mem_pool_close(pool4);
    mem_pool_close(pool1);
    mem_pool_close(pool10);
    mem_pool_close(pool2);
    
    pool_pt pool11 = mem_pool_open(11 * 1000 * 1000, BEST_FIT);
    pool_pt pool12 = mem_pool_open(12 * 1000 * 1000, BEST_FIT);
    pool_pt pool13 = mem_pool_open(13 * 1000 * 1000, BEST_FIT);
    pool_pt pool14 = mem_pool_open(14 * 1000 * 1000, BEST_FIT);
    
    mem_free();
    
    return 0;
    
}