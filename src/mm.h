int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);

typedef struct {
    char *teamname; /* ID1+ID2 or ID1 */
    char *name1;    /* Full name of first member. */
    char *id1;      /* Login ID of first member. */
    char *name2;    /* Full name of second member (if any). */
    char *id2;      /* Login ID of second member. */
} team_t;

extern team_t team;
