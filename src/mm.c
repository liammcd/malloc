/*
 * Uses segregated doubly-linked free lists to keep track of free blocks
 * For upper half of free lists, insert free blocks according to address
 * For lower half of free lists, insert free blocks FIFO
 * Delay coalescing to mm_free calls
 * Realloc attempts to coalesce with the next block or extend heap by new size less the size of current block
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "Team O3",
    "Liam McDermott",
    "liam.mcdermott@mail.utoronto.ca",
    "",
    ""
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (bytes) */
#define DSIZE       (2 * WSIZE)            /* doubleword size (bytes) */
#define CHUNKSIZE   (1<<7)      /* initial heap size (bytes) */

#define MAX(x,y) ((x) > (y)?(x) :(y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uintptr_t *)(p))
#define PUT(p,val)      (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given block ptr bp, compute address of next and previous free blocks */
#define NEXT_FBLK(bp) ((char *) (bp))
#define PREV_FBLK(bp) ((char *) (bp) + WSIZE)

/* Number of segregated lists*/
#define LISTS 16

void* free_lists[LISTS];
void* heap_listp = NULL;

/* Functions to manage blocks in free lists */
static void mm_remove_fblock(void *bp, int idx);
static void mm_insert_fblock(void *bp, int idx);
static int mm_find_list_idx(size_t size);

/* Extra debugging functions */
void mm_print_flist(void* list);

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
 int mm_init(void)
 {
   int i;

   //initialize array of free list pointers
   for(i=0; i < LISTS; i++) {
	   free_lists[i] = NULL;
   }

   if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
         return -1;
     PUT(heap_listp, 0);                         // alignment padding
     PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));   // prologue header
     PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));   // prologue footer
     PUT(heap_listp + (3 * WSIZE), PACK(0, 1));    // epilogue header
     heap_listp += DSIZE;
 	
     return 0;
 }

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 **********************************************************/
void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {       /* Case 1 */
		mm_insert_fblock(bp,-1);
        return bp;	// Add block to free list
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 */
		mm_remove_fblock(NEXT_BLKP(bp),-1);	// Remove next block from free list
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(bp), PACK(size, 0));
		mm_insert_fblock(bp,-1);		// Add new contiguous free block to free list
        return (bp);
    }

    else if (!prev_alloc && next_alloc) { /* Case 3 */
		mm_remove_fblock(PREV_BLKP(bp),-1);	// Remove previous block from free list
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		mm_insert_fblock(PREV_BLKP(bp),-1);
        return (PREV_BLKP(bp));
    }

    else {            /* Case 4 */
		mm_remove_fblock(NEXT_BLKP(bp),-1);	// Remove next block from free list
		mm_remove_fblock(PREV_BLKP(bp),-1);	// Remove previous block from free list
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)))  ;
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
		mm_insert_fblock(PREV_BLKP(bp),-1);
        return (PREV_BLKP(bp));
    }

    //mm_check();
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ( (bp = mem_sbrk(size)) == (void *)-1 )
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));                // free block header
    PUT(FTRP(bp), PACK(size, 0));                // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));        // new epilogue header

    /* Coalesce if the previous block was free */
    //return coalesce(bp);
	return bp;
}

/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void *find_fit(size_t asize)
{
	int idx = mm_find_list_idx(asize);
    void *bp = free_lists[idx];
	
	while (idx < LISTS) {
		
		bp = free_lists[idx];
		while (bp != NULL)
		{ 
			if (asize <= GET_SIZE(HDRP(bp)))
			{
				mm_remove_fblock(bp,idx);
				return bp;
			}
			bp = (void *)GET(bp);
		}
		idx++;
	}
    return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void* bp, size_t asize)
{
	/* Get the current block size */
	size_t bsize = GET_SIZE(HDRP(bp));
	size_t remain = bsize - asize;

	if (remain >= 4*WSIZE) {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		PUT(HDRP(NEXT_BLKP(bp)), PACK(remain,0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(remain,0));
		mm_insert_fblock(NEXT_BLKP(bp),-1);
	}
	else {
	  PUT(HDRP(bp), PACK(bsize, 1));
	  PUT(FTRP(bp), PACK(bsize, 1));
	}
}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp)
{
    if(bp == NULL){
      return;
    }
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));

    coalesce(bp);
}

/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void *mm_malloc(size_t size)
{
	int result;

    size_t asize; /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char * bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);
    /* Search the free lists for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;

	
	//result = !mm_check(); // Uncomment this line to run mm_check();
	//if (result) {
	//	printf("Heap consistency check failed.\n");
	//}

    place(bp, asize);

    return bp;
}

/**********************************************************
 * mm_realloc
 * Looks at next block of the request blocks: 
 * 	Case 1: block is free -> coalesce with needed size and leave the rest free
 *	Case 2: block is the epilogue -> extend heap an appropriate size
 *	Case 3: block is allocated -> use mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
      mm_free(ptr);
      return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
      return (mm_malloc(size));

    void *oldptr = ptr;
    void *newptr;
    size_t copySize, asize, cmbsize_next, cmbsize_prev;

	// Adjust size to fit overhead and alignment requirements 
	if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

	// Requested size is smaller than current block
	if ( asize < GET_SIZE(HDRP(ptr)) ) {
		// Shrink it
		int remain = GET_SIZE(HDRP(ptr)) - asize;
		if (remain >= 4*WSIZE) {
			PUT(HDRP(ptr), PACK(asize, 1));
			PUT(FTRP(ptr), PACK(asize, 1));
			PUT(HDRP(NEXT_BLKP(ptr)), PACK(remain, 0));
			PUT(FTRP(NEXT_BLKP(ptr)), PACK(remain, 0));
			mm_insert_fblock(NEXT_BLKP(ptr),-1);
		}	
		newptr = ptr;
	}
	else {
		// Get size of combined blocks
		cmbsize_next = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		cmbsize_prev = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(PREV_BLKP(ptr)));

		// Next block is free and the combined size will accomodate the realloc
		if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && (cmbsize_next >= asize)) {
			mm_remove_fblock(NEXT_BLKP(ptr),-1);
			// Put remainder of free block (not used to service realloc request) back into the free list
			int remain = GET_SIZE(HDRP(NEXT_BLKP(ptr))) - (asize - GET_SIZE(HDRP(ptr)));
			PUT(HDRP(ptr), PACK(asize, 1));
			PUT(FTRP(ptr), PACK(asize, 1));
			PUT(HDRP(NEXT_BLKP(ptr)), PACK(remain,0));
			PUT(FTRP(NEXT_BLKP(ptr)), PACK(remain,0));
			mm_insert_fblock(NEXT_BLKP(ptr),-1);
			newptr = ptr;
		}
		// Next block is epilogue, can extend heap
		else if (GET_SIZE(HDRP(NEXT_BLKP(ptr))) == 0) {
			// Extend heap by only the needed size to accomodate the realloc
			int diff = asize - GET_SIZE(HDRP(ptr));
			if (extend_heap(diff/WSIZE) == NULL) return NULL;
			PUT(FTRP(NEXT_BLKP(ptr)), PACK( GET_SIZE(HDRP(ptr))+diff, 1));
			PUT(HDRP(ptr), PACK( GET_SIZE(HDRP(ptr))+diff, 1));
			newptr = ptr;
		}
		// Previous block is free, join that block
		else if (!GET_ALLOC(HDRP(PREV_BLKP(ptr))) && (cmbsize_prev >= asize)) {
			newptr = PREV_BLKP(ptr);
			int remain = GET_SIZE(HDRP(newptr)) - ( asize - GET_SIZE(HDRP(ptr)));	
				size_t cmbsize = GET_SIZE(HDRP(newptr)) + GET_SIZE(HDRP(ptr));		
				mm_remove_fblock(newptr,-1);
				memmove(newptr, ptr, GET_SIZE(HDRP(ptr)));
				PUT(HDRP(newptr), PACK( cmbsize, 1));
				PUT(FTRP(newptr), PACK( cmbsize, 1));
		}	
		// Can't combine with following block / extend heap -> need to malloc
		else {
				newptr = mm_malloc(size);
				if (!newptr) return NULL;
				copySize = GET_SIZE(HDRP(oldptr));
				if (size < copySize)
					copySize = size;
				memcpy(newptr, oldptr, copySize);
				mm_free(oldptr);
		}
	}
	return newptr;
}

/**********************************************************
 * mm_find_list_idx
 * Given a size, find the appropriate index for the segregated
 * free list array.
 *********************************************************/
static int mm_find_list_idx(size_t size) {
	int idx = 0;

	while ((idx < LISTS - 1) && (size > 32)) { //32

		if(idx > LISTS/2) {
			size >>= 8;
		}
		else
			size >>= 1;

		idx++;
	}
	return idx;
}

/**********************************************************
 * mm_remove_fblock
 * Remove block at location bp from free list, splicing out
 * successor and predecessor if applicable
 *********************************************************/
static void mm_remove_fblock(void *bp, int idx)
{
	void *pre = GET(PREV_FBLK(bp));
	void *suc = GET(NEXT_FBLK(bp));
	if (idx == -1)	
		idx = mm_find_list_idx(GET_SIZE(HDRP(bp)));

	if (pre && suc) {
		PUT(NEXT_FBLK(pre), suc);
		PUT(PREV_FBLK(suc), pre);
	} else if (pre && !suc) {
		PUT(NEXT_FBLK(pre), NULL);
	} else if (!pre && suc) {
		PUT(PREV_FBLK(suc), NULL);
		free_lists[idx] = suc;
	} else {
		free_lists[idx]  = NULL;
	}
}

/**********************************************************
 * mm_insert_fblock
 * Add block at location bp to the free list.
 * For large list sizes, this is done in order based on the addresses of the blocks.
 * For small list sizes, insert is based on FIFO.
 *
 * 4 Possible cases for inserting on address order:
 *
 * 1. Insert block at the beginning of the list.
 * 2. Insert block at the end of the list.
 * 3. Insert block at the second last element of the list.
 * 4. Insert block somewhere in the middle of the list.
 *********************************************************/
static void mm_insert_fblock(void *bp, int idx)
{
	void* curr = NULL;
	void* next = NULL;
	void* prev = NULL;

	if (idx == -1)
		idx = mm_find_list_idx(GET_SIZE(HDRP(bp)));

	if (idx > (LISTS-1)/2) {	//insert ordered by address
		curr = free_lists[idx];
		next = curr ? GET(NEXT_FBLK(curr)) : NULL;
		if(curr) {
			//find insertion point in free list
			while (next && (bp > next)) {
				curr = next;
				next = GET(NEXT_FBLK(curr));
			}
			prev = GET(PREV_FBLK(curr));

			if(!prev && (bp < curr)) {	//Case 1: insert block as the first entry in the list
				PUT(NEXT_FBLK(bp), curr);
				PUT(PREV_FBLK(curr), bp);
				PUT(PREV_FBLK(bp), NULL);
				free_lists[idx] = bp;
			} else if(!next && (bp > curr)) {	//Case 2: insert block as the last entry in the list
				PUT(NEXT_FBLK(bp), NULL);
				PUT(PREV_FBLK(bp), curr);
				PUT(NEXT_FBLK(curr), bp);
			} else if (!next && (bp < curr)) {	//Case 3: insert block as second last entry in the list
				PUT(NEXT_FBLK(prev), bp);
				PUT(PREV_FBLK(bp), prev);
				PUT(PREV_FBLK(curr), bp);
			} else if(next && (bp > curr) && (bp < next)) {	//Case 4: Insert block somewhere in the middle of the list
				PUT(NEXT_FBLK(bp), next);
				PUT(PREV_FBLK(bp), curr);
				PUT(NEXT_FBLK(curr), bp);
				PUT(PREV_FBLK(next), bp);
			}
		} else {
			//set block as the head of list
			PUT(NEXT_FBLK(bp), NULL);
			PUT(PREV_FBLK(bp), NULL);
			free_lists[idx] = bp;
		}
	} else {	//insert based on FIFO
		if (free_lists[idx] != NULL) {
			//set the next pointer of new free block to the head of list, and set the previous free block of the
			//current head to the free block that is being inserted.
			PUT(NEXT_FBLK(bp), free_lists[idx]);
			PUT(PREV_FBLK(free_lists[idx]), bp);
		} else {
			PUT(NEXT_FBLK(bp), NULL);
		}
		//set the previous block of the new head to NULL and point the free list to the new head.
		PUT(PREV_FBLK(bp), NULL);
		free_lists[idx] = bp;
	}
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistent.
 *********************************************************/
int mm_check(void)
{
	int i = 0;
	int found = 0;
	size_t size;
	void* bp, *free, *next;

	// Check if every block in every free list is marked as unallocated.
	for(i=0; i < LISTS; i++) {
		free = free_lists[i];
		while (free != NULL) {
			if (GET_ALLOC(free)) {
				printf("A free block is marked as allocated at %x FAIL...\n", free);
				return 0;	// A block was allocated. FAIL
			}
			free = GET(NEXT_FBLK(free)); // Advance to next free block
		}
	}

	// Check the heap for contiguous free blocks.
	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
    	if ( (!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(PREV_BLKP(bp)))) || (!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) ) {
			printf("Contiguous free blocks detected at %x FAIL...\n", bp);
			return 0;
		}
    }

	// Check that every free block in the heap belongs in a free list.
	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
	{
		if ( !GET_ALLOC(HDRP(bp)) )	// block is free -> ensure it is in the free list
		{
			i = 0;
			size = GET_SIZE(HDRP(bp));

			i = mm_find_list_idx(size);
			
			free = free_lists[i];
			while (free != NULL && !found)
			{
				if (bp == free) {
					found = 1;
				}
				free = GET(NEXT_FBLK(free));
			}
			if (!found) {
				printf("A free block is not in the free list! at: %x FAIL...\n", free);
				return 0;
			}
		}
	}

	// Check validity of free blocks in every free list. Each free block should not be allocated and for overlap with 
	for (i = 0; i < LISTS; i++) {
		free = free_lists[i];
		while (free != NULL)
		{
			if (GET_ALLOC(HDRP(free)))
			{
				printf("Non-free block in free list %x! FAIL...\n", free);
				return 0;
			}
			next = NEXT_BLKP(free);	// Get address of next free block

			if (next) {
				if (next < (free + GET_SIZE(HDRP(free)))) {
					printf("Overlapping memory in free list. free is [%x], next is [%x] FAIL...\n", free, next, GET_SIZE(HDRP(free)));
					return 0;
				} 
			}
			
			free = GET(NEXT_FBLK(free));
		}
	}

	for (bp = heap_listp; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp))
	{
		if (bp < mem_heap_lo() || bp > mem_heap_hi())
		{
			printf("Block at address [%x] is not a valid heap address FAIL...\n", bp);
		}
	}

	int free_count = 0;
	for (i = 0; i < LISTS; i++) {
		free_count = 0;
		bp = free_lists[i];
		while (bp != NULL) {
			free_count++;
			bp = GET(NEXT_FBLK(bp));
		}
		if (free_count) printf("Free list [%d] has [%d] elements\n", i, free_count);
	}
	
  return 1;
}

/**********************************************************
 * mm_print_flist
 * Traverses a free list and prints the previous, current and next pointer values.
 * Takes a count input to print up to the number of entries in the free list.
 *********************************************************/
void mm_print_flist(void* list) {
	void* free = list;
	while (free) {
		printf("Previous ptr: %x, Current ptr: %x, Next ptr: %x\n", PREV_FBLK(free), free, NEXT_FBLK(free));
		free = GET(NEXT_FBLK(free));
	}
}

