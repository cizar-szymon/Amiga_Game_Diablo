/*
 * amiga_fastmalloc.c - a size-class allocator in place of libnix's malloc
 * (copied from AmiXcom, where it was built with AMIGA_FASTMALLOC=1).
 *
 * Replaces libnix's malloc/free/calloc/realloc (their one object, malloc.o,
 * is then never linked), as amiga_heapguard.c does. libnix takes every block
 * from an exec memory pool, and AllocPooled/FreePooled search the pool's
 * puddles first-fit. The ruleset load makes a few hundred thousand small
 * allocations; on a fresh heap that is quick, but after a reload has freed
 * the whole old Mod the puddles are full of holes and every allocation
 * searches them: the reload after a change of the mod list took 102 s for
 * the rulesets against 27 s at a cold start, plus 18.5 s to free the old
 * Mod (Warp1260, 2026-10-21).
 *
 * Blocks up to SMALL_MAX bytes come from 64 size classes, 8 bytes apart,
 * each with its own free list: allocating and freeing is a pop and a push,
 * whatever the heap has been through. Fresh blocks of a class are carved
 * from CHUNK-sized pieces of AllocMem; memory of a class is reused only by
 * that class and goes back to the system at exit. Bigger blocks go to an
 * exec pool, as before. Every block has an 8-byte header in front (the
 * class, or POOLED and the size) so free() needs no size, and user data
 * stays 8-aligned. A semaphore serialises the game task and the others
 * that allocate (the watchdog, the log).
 */
#include <stddef.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <stabs.h>

#define SMALL_MAX   512UL                 /* bigger blocks: the exec pool */
#define NCLASS      (SMALL_MAX / 8)       /* class c: (c+1)*8 bytes */
#define CHUNK       (64UL * 1024)         /* carving unit for the classes */
#define POOLED      0xFFFFFFFFUL          /* header tag of a pool block */

typedef struct FmHdr {
	ULONG tag;     /* class index, or POOLED */
	ULONG size;    /* user size (pool blocks; informative for classes) */
} FmHdr;           /* 8 bytes */

typedef struct FmFree { struct FmFree *next; } FmFree;

typedef struct FmChunk { struct FmChunk *next; ULONG size; ULONG pad[2]; } FmChunk;

static FmFree *s_free[NCLASS];
static UBYTE *s_carve, *s_carveEnd;       /* the rest of the current chunk */
static FmChunk *s_chunks;                 /* all chunks, freed at exit */
static APTR s_pool;
static struct SignalSemaphore s_sem;
static int s_ready;

static void fm_exit(void)
{
	FmChunk *c, *n;
	if (!s_ready) return;
	for (c = s_chunks; c != NULL; c = n) { n = c->next; FreeMem(c, c->size); }
	s_chunks = NULL;
	if (s_pool) { DeletePool(s_pool); s_pool = NULL; }
	s_ready = 0;
}

static void fm_init(void)
{
	if (s_ready) return;
	InitSemaphore(&s_sem);
	s_pool = CreatePool(MEMF_ANY, 64 * 1024, 16 * 1024);
	s_ready = 1;
}

/* libnix's startup calls these - but only through its init/exit lists,
 * and the entries that put them there lived in its malloc.o too. Without
 * the ADD2INIT/ADD2EXIT below fm_exit never ran: every chunk stayed
 * allocated after the game quit, ~20 MB of Fast RAM gone per run until a
 * reboot (Warp4070, 2026-10-23). Same priority as libnix's own. */
void __initmalloc(void) { fm_init(); }
void __exitmalloc(void) { fm_exit(); }
ADD2INIT(__initmalloc, -50);
ADD2EXIT(__exitmalloc, -50);

/* a fresh block of class c from the current chunk, or a new chunk */
static FmHdr *fm_carve(ULONG c)
{
	const ULONG bytes = (c + 1) * 8 + sizeof(FmHdr);
	FmHdr *h;
	if (s_carve == NULL || (ULONG)(s_carveEnd - s_carve) < bytes) {
		FmChunk *k = (FmChunk *)AllocMem(CHUNK, MEMF_ANY);
		if (k == NULL) return NULL;
		k->size = CHUNK;
		k->next = s_chunks;
		s_chunks = k;
		s_carve = (UBYTE *)(k + 1);       /* 16 bytes in: 8-aligned */
		s_carveEnd = (UBYTE *)k + CHUNK;
	}
	h = (FmHdr *)s_carve;
	s_carve += bytes;
	return h;
}

static void *fm_alloc(size_t n)
{
	FmHdr *h;
	if (!s_ready) fm_init();
	if (n == 0) n = 1;
	ObtainSemaphore(&s_sem);
	if (n <= SMALL_MAX) {
		const ULONG c = (ULONG)((n + 7) >> 3) - 1;
		FmFree *f = s_free[c];
		if (f != NULL) {
			s_free[c] = f->next;
			h = (FmHdr *)f - 1;
		} else {
			h = fm_carve(c);
			if (h == NULL) { ReleaseSemaphore(&s_sem); return NULL; }
		}
		h->tag = c;
	} else {
		h = (FmHdr *)AllocPooled(s_pool, (ULONG)n + sizeof(FmHdr));
		if (h == NULL) { ReleaseSemaphore(&s_sem); return NULL; }
		h->tag = POOLED;
	}
	h->size = (ULONG)n;
	ReleaseSemaphore(&s_sem);
	return h + 1;
}

static void fm_free(void *p)
{
	FmHdr *h;
	if (p == NULL) return;
	h = (FmHdr *)p - 1;
	ObtainSemaphore(&s_sem);
	if (h->tag == POOLED) {
		FreePooled(s_pool, h, h->size + sizeof(FmHdr));
	} else if (h->tag < NCLASS) {
		FmFree *f = (FmFree *)p;
		f->next = s_free[h->tag];
		s_free[h->tag] = f;
	}
	/* anything else is not ours (a double free leaves the tag of the class,
	 * so it is not caught here - the heapguard build is for that) */
	ReleaseSemaphore(&s_sem);
}

void *malloc(size_t n) { return fm_alloc(n); }
void free(void *p) { fm_free(p); }

void *calloc(size_t a, size_t b)
{
	size_t n = a * b;
	void *p = fm_alloc(n);
	if (p) memset(p, 0, n);
	return p;
}

void *realloc(void *p, size_t n)
{
	FmHdr *h;
	void *q;
	size_t old;
	if (p == NULL) return fm_alloc(n);
	if (n == 0) { fm_free(p); return NULL; }
	h = (FmHdr *)p - 1;
	/* still fits its class: keep the block */
	if (h->tag < NCLASS && n <= (size_t)(h->tag + 1) * 8) { h->size = (ULONG)n; return p; }
	old = h->size;
	q = fm_alloc(n);
	if (q == NULL) return NULL;
	memcpy(q, p, old < n ? old : n);
	fm_free(p);
	return q;
}
