#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"

struct COW{
    uint8 cow_ref;
    struct spinlock lock;
} cow[(PHYSTOP - KERNBASE) / PGSIZE];

void 
initlock_cow(void){
    int i;
    for (i = 0; i < (PHYSTOP - KERNBASE) / PGSIZE; ++i)
        initlock(&cow[i].lock, "cow");
}

void 
inc_cowref(uint64 pa){
    pa = (pa - KERNBASE) / PGSIZE;
    acquire(&cow[pa].lock);
    ++cow[pa].cow_ref;
    release(&cow[pa].lock);
}

uint8
dec_cowref(uint64 pa){
    uint8 ref;
    pa = (pa - KERNBASE) / PGSIZE;
    acquire(&cow[pa].lock);
    ref = --cow[pa].cow_ref;
    release(&cow[pa].lock);
    return ref;
}