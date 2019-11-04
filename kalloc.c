// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#define MAX_FRAME_NUMBER 16384

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct frame {
  int pid;
  struct run *currentFrame;
};

struct {
  struct spinlock lock;
  int use_lock;
  int framesAllocated;
  struct run *freelist;
  struct frame allFrames[MAX_FRAME_NUMBER];
} kmem;

int useOriginal = 1;

void kfree1(char *v) {
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

void freerange1(void *vstart, void *vend) {
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree1(p);
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange1(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  useOriginal = 0;
  for (int i = 0; i < MAX_FRAME_NUMBER; i++) {
    kmem.allFrames[i].currentFrame = 0;
    kmem.allFrames[i].pid = -1; 
  }
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  kmem.framesAllocated = 0;
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  
  // START Added
  int i = 0;
  for (; i < kmem.framesAllocated; i++) {
    if (kmem.allFrames[i].currentFrame == r) {
      for (;i < kmem.framesAllocated - 1; i++) {
        kmem.allFrames[i].currentFrame = kmem.allFrames[i + 1].currentFrame;
        kmem.allFrames[i].pid = kmem.allFrames[i + 1].pid;
      }
      kmem.framesAllocated--;
      break;
    } 
  }
  if (i == MAX_FRAME_NUMBER) {
    panic("kfree no frame number");
  }

  if (kmem.freelist == 0) {
    kmem.freelist = r;
  } else {
    struct run *curr = kmem.freelist;
    if (r > curr) {
      r->next = kmem.freelist;
      kmem.freelist = r;
    } else {
      struct run *prev = curr;
      curr = curr->next;
      while (r < curr) {
        prev = curr;
        curr = curr->next;
      }
      prev->next = r;
      r->next = curr;
    }
  }
  // END ADDED
  if(kmem.use_lock)
    release(&kmem.lock);
}

int getPid(char* v) {
  if (v == 0) {
    return -1;
  }
  int pid = -1;
  struct run* target = (struct run*) v;
  for (int i = 0; i < MAX_FRAME_NUMBER; i++) {
    if (kmem.allFrames[i].currentFrame == target) {
      pid = kmem.allFrames[i].pid;
      break;
    } 
  }

  if (pid == -2) {
    pid = -1;
  }

  return pid;
}

int satisfiesRules(struct run *r, int pid) {
  if (r == 0) {
    return 0;
  }
  int leftPid = getPid((char*)r - PGSIZE);
  int rightPid = getPid((char*)r + PGSIZE);
  if ((leftPid == pid || leftPid == -1) && (rightPid == pid || rightPid == -1)) {
    return 1;
  } else {
    return 0;
  }
}

void addToAllocated(struct run *current, int pid) {
  if (kmem.framesAllocated == 0) {
    kmem.allFrames[0].currentFrame = current;
    kmem.allFrames[0].pid = pid;
  } else {
    for (int i = 0; i < kmem.framesAllocated; i++) {
      if (kmem.allFrames[i].currentFrame < current) {
        int pos = i;
        for (i = kmem.framesAllocated; i > pos; i--) {
          kmem.allFrames[i] = kmem.allFrames[i - 1];
        }
        kmem.allFrames[pos].currentFrame = current;
        kmem.allFrames[pos].pid = pid;
        break;
      }
      if (i == kmem.framesAllocated - 1) {
        kmem.allFrames[kmem.framesAllocated].currentFrame = current;
        kmem.allFrames[kmem.framesAllocated].pid = pid;
      }
    }
  }
  kmem.framesAllocated++;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(int pid)
{
  struct run *r;
  if(kmem.use_lock)
      acquire(&kmem.lock);
  r = kmem.freelist;
  if (useOriginal) {
    if(r)
      kmem.freelist = r->next;
    if(kmem.use_lock)
      release(&kmem.lock);
    return (char*)r;
  }
  struct run *prev = r;  

  // START ADDED
  if (r == 0) {
    return 0;
  }
  if (satisfiesRules(r, pid) || pid == -2) {
    kmem.freelist = r->next;
    addToAllocated(r, pid);
    if(kmem.use_lock)
      release(&kmem.lock);
    return (char*)r;
  } else {
    prev = r;
    r = r->next;
  }

  while(r != 0) {
    if (satisfiesRules(r, pid)) {
      prev->next=r->next;
      addToAllocated(r, pid);
      if(kmem.use_lock)
        release(&kmem.lock);
      return (char*)r;
    } else {
      prev = r;
      r = r->next;
    }
  }

  // END ADDED
  if(kmem.use_lock)
    release(&kmem.lock);
  return 0;
}

int dump_physmem_helper(int *frames, int *pids, int numframes) {
  if (frames == 0 || pids == 0) {
    return -1;
  }
  int index = 0;
  for(int i = 0; i < kmem.framesAllocated && index < numframes; i++) {
    if (kmem.allFrames[i].pid != -1) {
      frames[index] = V2P(kmem.allFrames[i].currentFrame) >> 12;
      pids[index] = kmem.allFrames[i].pid;
      index++;
    }
  }
  for (; index < numframes; index++) {
    frames[index] = -1;
    pids[index] = -1;
  }
  return 0;
}

