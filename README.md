# malloc
Memory manager, implementation of malloc

Manages heap memory, toy implementation of the infamous malloc memory manager. 

Build by running make in src/ directory

Tests are run with ./mdriver [options]

Implementation found in mm.c 

Uses sbrk system call to manually adjust program memory. Uses segregated, doubly-linked free lists to keep track of free blocks within the heap. Coalescing of free blocks is deferred until free() is called.

16 free lists are kept for different block sizes to improve lookup performance and coalesced free blocks are added to the approriate list.
