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
/* Working
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

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
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
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
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
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
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

int dump_physmem_helper(int *frames, int *pids, int numframes) {
  return -1;
}*/

struct run {
  struct run *next;
  int frameIndex;
};

struct frame {
  int pid;
  struct run *currentFrame;
};

struct {
  struct spinlock lock;
  int use_lock;
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
  int currentIndex = 0;
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE) {
    kmem.allFrames[currentIndex].currentFrame = (struct run*) p;
    kfree(p);
    ((struct run*)p)->frameIndex = currentIndex++;
    if (currentIndex >= MAX_FRAME_NUMBER) {
      break;
    }
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
  for (; i < MAX_FRAME_NUMBER; i++) {
    if (kmem.allFrames[i].currentFrame == r) {
      //cprintf("Found its i: %d\n", i);
      break;
    } 
  }
  if (i == MAX_FRAME_NUMBER) {
    panic("This shouldn't happen!");
  } else {
    //cprintf("Found its i: %d\n", i);
    r->frameIndex = i;
    kmem.allFrames[i].pid = -1;
    //kmem.allFrames[i].currentFrame = r;
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
  // TODO When freeing, put in first spot where it is smaller
  // END ADDED
  //r->next = kmem.freelist;
  //kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

int satisfiesRules(struct run *r, int pid) {
  if (r == 0) {
    return 0;
  }
  int frameIndex = r->frameIndex;
  if (frameIndex == 0) {
    if (kmem.allFrames[frameIndex + 1].pid == pid || kmem.allFrames[frameIndex + 1].pid == -1) {
      return 1; // True
    } else {
      return 0; // False
    }
  } else if (frameIndex == MAX_FRAME_NUMBER - 1) {
    if (kmem.allFrames[frameIndex - 1].pid == pid || kmem.allFrames[frameIndex - 1].pid == -1) {
      return 1; // True
    } else {
      return 0; // False
    }
  } else {
    //cprintf("Pre: %d\n", kmem.allFrames[frameIndex - 1].pid);
    //cprintf("Post: %d\n", kmem.allFrames[frameIndex + 1].pid);
    if ((kmem.allFrames[frameIndex - 1].pid == pid || kmem.allFrames[frameIndex - 1].pid == -1)
     && (kmem.allFrames[frameIndex + 1].pid == pid || kmem.allFrames[frameIndex + 1].pid == -1)) {
      return 1; // True
    } else {
      return 0; // False
    }
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(int pid)
{
  // TODO Try iterating to find frame index instead of storing
  // TODO it in free block
  struct run *r;
  //cprintf("enter kalloc\n");
  if(kmem.use_lock)
      acquire(&kmem.lock);
  r = kmem.freelist;
  if (useOriginal || r->frameIndex < 0 || r->frameIndex > MAX_FRAME_NUMBER) {
    if(r)
      kmem.freelist = r->next;
    if(kmem.use_lock)
      release(&kmem.lock);
    return (char*)r;
  }
  struct run *prev = r;  

  //cprintf("BLAH%d\n", r->next->frameIndex);

  // START ADDED
  if (r == 0) {
    return 0;
  }
  if (satisfiesRules(r, pid)) {
    //cprintf("first\n");
    kmem.freelist = r->next;
    kmem.allFrames[r->frameIndex].pid = pid;
    //kmem.allFrames[r->frameIndex].currentFrame = r;
    //cprintf("hiiiiii%d\n", r->next->frameIndex);
    if(kmem.use_lock)
      release(&kmem.lock);
    //cprintf("physical: %x\n ---- pid: %d\n", (int) V2P(r), pid);
    return (char*)r;
  } else {
    prev = r;
    r = r->next;
  }

  while(r != 0) {
    //cprintf("not first%p\n", r->next);
    if (satisfiesRules(r, pid)) {
      prev->next=r->next;
      kmem.allFrames[r->frameIndex].pid = pid;
      //kmem.allFrames[r->frameIndex].currentFrame = r;
      if(kmem.use_lock)
        release(&kmem.lock);
      //cprintf("physical: %x ---- pid: %d\n", (int) V2P(r), pid);
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
  for(int i = 0; i < MAX_FRAME_NUMBER && index < numframes; i++) {
    if (kmem.allFrames[i].pid != -1) {
      frames[index] = PTX(PHYSTOP - V2P(kmem.allFrames[i].currentFrame));
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

