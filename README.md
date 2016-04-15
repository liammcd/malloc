# malloc
Memory manager, implementation of malloc

Manages heap memory, toy implementation of the infamous malloc memory manager. 

Build by running make in src/ directory

Tests are run with ./mdriver [options]. Testcases can be found in the traces/ directory.

Implementation found in mm.c 

Uses sbrk system call to manually adjust program memory. Uses segregated, doubly-linked free lists to keep track of free blocks within the heap. Coalescing of free blocks is deferred until free() is called.

16 free lists are kept for different block sizes to improve lookup performance and coalesced free blocks are added to the approriate list.

Realloc will attempt to coalesce with adjacent memory blocks before extending the heap if the block is to be grown. Shrinks in place and divides trailing free block into smaller blocks to be inserted in appropriate free lists.
