#include <stddef.h>
#include <libft.h>
#include <heap.h>
#include <utils.h>
#include <assert.h>

extern t_heap heap;

static void build_pools(void);
static inline size_t get_chunk_size(t_chunk* chunk);
static inline void* get_chunk_data(t_chunk* chunk);
static uint8_t init_pool(t_pool* pool);
static inline size_t get_pool_unmapped_size(t_pool* pool);
static inline void update_pool_smallest_freed_chunk(t_pool* pool, t_chunk* chunk);
static t_chunk* build_pool_chunk(t_pool* pool, size_t requested_size);
static inline void merge_two_chunks(t_pool* pool, t_chunk* a, t_chunk* b);
static bool grow_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size);
static t_chunk* build_large_pool_chunk(t_pool* pool, size_t requested_size);
static uint8_t can_split_chunk(t_pool* pool, t_chunk* chunk, size_t split_size);
static t_chunk* split_pool_chunk(t_pool* pool, t_chunk* chunk, size_t requested_size);
static t_chunk* merge_pool_chunks(t_pool* pool, t_chunk* chunk);
static t_chunk* find_next_unused_chunk(t_pool* pool, t_chunk* chunk, size_t size);
static t_chunk* alloc_pool_chunk(t_pool* pool, size_t requested_size);
static bool dealloc_pool_chunk(t_pool* pool, t_chunk* chunk);
static t_chunk* find_pool_chunk_by_data(t_pool* pool, void* ptr, bool large_pool);
static t_chunk* find_chunk_by_data(void* ptr, t_pool** pool);
static t_chunk* alloc(size_t size);
static bool dealloc(void* ptr);
static t_chunk* realloc_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size);
static void show_chunk(t_chunk* chunk, size_t indent, bool dump);
static void show_heap(bool dump);


static void build_pools(void) {
  if (heap.page_size)
    return;
  heap.page_size = getpagesize();
  if (getrlimit(RLIMIT_AS, &heap.limits) == -1)
    return;
  ft_bzero(&heap.pools, sizeof(heap.pools));
  TINY_POOL.size = TINY_POOL_SIZE_MULTIPLIER * heap.page_size;
  TINY_POOL.max_chunk_size = TINY_POOL_CHUNK_MAX_SIZE_MULTIPLIER(TINY_POOL.size);
  TINY_POOL.max_chunk_size = align_down(TINY_POOL.max_chunk_size);
  SMALL_POOL.size = SMALL_POOL_SIZE_MULTIPLIER * heap.page_size;
  SMALL_POOL.max_chunk_size = SMALL_POOL_CHUNK_MAX_SIZE_MULTIPLIER(SMALL_POOL.size);
  SMALL_POOL.max_chunk_size = align_down(SMALL_POOL.max_chunk_size);
  for (uint8_t i = 0; i < HEAP_POOLS + 1; i++) {
    if (i == 0)
      heap.pools[i].min_chunk_size = align_up(1) + sizeof(t_chunk);
    else
      heap.pools[i].min_chunk_size = align_up(heap.pools[i - 1].max_chunk_size + 1);
    heap.pools[i].free_chunks = NULL;
    heap.pools[i].chunks = NULL;
    heap.pools[i].last_chunk = NULL;
  }
}

static inline size_t get_chunk_size(t_chunk* chunk) {
  return chunk->size + sizeof(t_chunk);
}

static inline void* get_chunk_data(t_chunk* chunk) {
  return (void*)chunk + sizeof(t_chunk);
}

static inline void assert_chunk_data(t_chunk* chunk) {
  assert(chunk && "assert_chunk_data: chunk is NULL");
  assert(chunk->size > 0 && "assert_chunk_data: chunk size is 0");
  assert(chunk->size < heap.limits.rlim_cur && "assert_chunk_data: chunk size is too large");
  assert(chunk->size % 16 == 0 && "assert_chunk_data: chunk size is not aligned");
  assert(chunk->size % 8 == 0 && "assert_chunk_data: chunk size is not aligned");
  assert(get_chunk_size(chunk) % 16 == 0 && "assert_chunk_data: chunk total size is not aligned");
  assert(get_chunk_size(chunk) % 8 == 0 && "assert_chunk_data: chunk total size is not aligned");
  if (chunk->next)
    assert((void*)chunk + get_chunk_size(chunk) == (void*)chunk->next && "assert_chunk_size: chunk size is incorrect");
}

static uint8_t init_pool(t_pool* pool) {
  if (pool->data || pool->size == 0)
    return true;
  pool->data = mmap(NULL, pool->size, MMAP_FLAGS);
  if (pool->data == MAP_FAILED)
    return false;
  pool->unmapped = pool->data;
  return true;
}

static inline size_t get_pool_unmapped_size(t_pool* pool) {
  return pool->data + pool->size - pool->unmapped < 0 ? 0 : pool->data + pool->size - pool->unmapped;
}

static inline void update_pool_smallest_freed_chunk(t_pool* pool, t_chunk* chunk) {
  if (!pool->free_chunks || chunk->size < pool->free_chunks->size)
    pool->free_chunks = chunk;
}

// add a chunk to the pool
// is assumed that requested_size <= pool->max_chunk_size
static t_chunk* build_pool_chunk(t_pool* pool, size_t requested_size) {
  size_t data_size = align_up(requested_size);
  size_t chunk_size = data_size + sizeof(t_chunk);
  assert(chunk_size <= pool->max_chunk_size && "build_pool_chunk: chunk_size > pool->max_chunk_size");
  if (get_pool_unmapped_size(pool) < chunk_size)
    return NULL;
  t_chunk* chunk = pool->unmapped;
  chunk->size = data_size;
  chunk->used = true;
  chunk->next = NULL;
  chunk->prev = pool->last_chunk;
  if (!pool->chunks)
    pool->chunks = chunk;
  else
    pool->last_chunk->next = chunk;
  pool->last_chunk = chunk;
  pool->unmapped = (void*)chunk + chunk_size;
  if (pool->unmapped > pool->data + pool->size)
    pool->unmapped = pool->data + pool->size;
  assert(pool->unmapped <= pool->data + pool->size && "build_pool_chunk: pool->unmapped is out of bounds");
  assert_chunk_data(chunk);
  return chunk;
}

static inline void merge_two_chunks(t_pool* pool, t_chunk* a, t_chunk* b) {
  assert(a->next == b && b->prev == a && "merge_two_chunks: chunks are not adjacent");
  a->size += get_chunk_size(b);
  a->next = b->next;
  if (b->next)
    b->next->prev = a;
  if (pool->last_chunk == b)
    pool->last_chunk = a;
  if (pool->free_chunks == b)
    pool->free_chunks = a;
  assert_chunk_data(a);
  assert_chunk_data(b);
}

static t_chunk* grow_large_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size) {
  assert(IS_LARGE_POOL(pool) && "grow_large_pool_chunk: pool is not large");
  size_t new_size = align_up_to_power_of_2(align_up(new_req_size) + sizeof(t_chunk), heap.page_size);
  size_t new_chunk_size = new_size + sizeof(t_chunk);
  t_chunk* new_chunk = mmap(NULL, new_chunk_size, MMAP_FLAGS);
  if (new_chunk == MAP_FAILED)
    return NULL;
  new_chunk->size = new_size;
  new_chunk->used = true;
  new_chunk->next = chunk->next;
  new_chunk->prev = chunk->prev;
  if (chunk->next)
    chunk->next->prev = new_chunk;
  if (chunk->prev)
    chunk->prev->next = new_chunk;
  ft_memmove8(get_chunk_data(new_chunk), get_chunk_data(chunk), chunk->size);
  munmap(chunk, get_chunk_size(chunk));
  return new_chunk;
}

static bool grow_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size) {
  if (IS_LARGE_POOL(pool))
    return grow_large_pool_chunk(pool, chunk, new_req_size);
  size_t new_size = align_up(new_req_size);
  size_t new_chunk_size = new_size + sizeof(t_chunk);
  if (new_size <= chunk->size)
    return true;
  if (new_chunk_size > pool->max_chunk_size)
    return false;
  if (!chunk->next) {
    if (get_pool_unmapped_size(pool) < new_chunk_size)
      return false;
    chunk->size = new_size;
    pool->unmapped = (void*)chunk + new_chunk_size;
    return true;
  }
  else if (
    !chunk->next->used &&
    (get_chunk_size(chunk->next) + get_chunk_size(chunk) >= new_chunk_size)
    ) {
    merge_two_chunks(pool, chunk, chunk->next);
    if (can_split_chunk(pool, chunk, new_size)) {
      split_pool_chunk(pool, chunk, new_req_size);
      return true;
    }
    return true;
  }
  return false;
}

static t_chunk* build_large_pool_chunk(t_pool* pool, size_t requested_size) {
  size_t chunk_size = align_up_to_power_of_2(align_up(requested_size) + sizeof(t_chunk), heap.page_size);
  if (chunk_size > heap.limits.rlim_cur)
    return NULL;
  if (chunk_size == align_up(requested_size))
    chunk_size += heap.page_size;
  size_t data_size = chunk_size - sizeof(t_chunk);
  t_chunk* chunk = mmap(NULL, chunk_size, MMAP_FLAGS);
  if (chunk == MAP_FAILED)
    return NULL;
  chunk->size = data_size;
  chunk->used = true;
  chunk->next = NULL;
  chunk->prev = pool->last_chunk;
  if (!pool->chunks)
    pool->chunks = chunk;
  else {
    pool->last_chunk->next = chunk;
  }
  pool->last_chunk = chunk;
  assert_chunk_data(chunk);
  return chunk;
}

// can split the chunk in two chunks
// - check if there's enough space for (chunk-header + split_size, chunk-header + data)
static uint8_t can_split_chunk(t_pool* pool, t_chunk* chunk, size_t split_size) {
  if (pool->size == 0)
    return false;
  size_t chunk_size = get_chunk_size(chunk);
  if (chunk_size < split_size + sizeof(t_chunk))
    return false;
  size_t left_split_chunk_size = split_size + sizeof(t_chunk);
  size_t right_split_chunk_size = chunk_size - left_split_chunk_size;
  if (right_split_chunk_size < pool->min_chunk_size)
    return false;
  return true;
}

// split the chunk in two chunks, returning the right half
static t_chunk* split_pool_chunk(t_pool* pool, t_chunk* chunk, size_t requested_size) {
  size_t size = align_up(requested_size);
  assert(can_split_chunk(pool, chunk, size) && "split_pool_chunk: can't split chunk");
  size_t chunk_size = get_chunk_size(chunk);
  size_t left_split_chunk_size = size + sizeof(t_chunk);
  size_t right_split_chunk_size = chunk_size - left_split_chunk_size;
  t_chunk* right_chunk = (void*)chunk + left_split_chunk_size;
  if (right_chunk == chunk->next)
    assert(0 && "split_pool_chunk: chunk == chunk->next");
  right_chunk->size = right_split_chunk_size - sizeof(t_chunk);
  right_chunk->used = false;
  if (chunk->next)
    chunk->next->prev = right_chunk;
  right_chunk->next = chunk->next;
  right_chunk->prev = chunk;
  chunk->size = size;
  chunk->used = true;
  chunk->next = right_chunk;
  if (pool->last_chunk == chunk)
    pool->last_chunk = right_chunk;
  // if the right chunk is smaller than the free_chunks, update free_chunks
  pool->free_chunks = NULL;
  pool->free_chunks = find_next_unused_chunk(pool, NULL, 0);
  right_chunk = merge_pool_chunks(pool, right_chunk);
  assert_chunk_data(chunk);
  assert_chunk_data(right_chunk);
  return right_chunk;
}

static t_chunk* merge_pool_chunks(t_pool* pool, t_chunk* chunk) {
  t_chunk* next = chunk->next;
  while (next && !next->used) {
    merge_two_chunks(pool, chunk, next);
    assert_chunk_data(chunk);
    next = next->next;
  }
  t_chunk* prev = chunk->prev;
  while (prev && !prev->used) {
    merge_two_chunks(pool, prev, chunk);
    chunk = prev;
    assert_chunk_data(chunk);
    prev = prev->prev;
  }
  return chunk;
}

static t_chunk* find_next_unused_chunk(t_pool* pool, t_chunk* chunk, size_t size) {
  ft_printf("find_next_unused_chunk: chunk %p, size %u\n", chunk, size);
  if (!chunk) {
    ft_printf("find_next_unused_chunk: no chunk\n");
    if (pool->free_chunks && pool->free_chunks->size >= size)
      return pool->free_chunks;
    chunk = pool->chunks;
  }
  while (chunk && (chunk->used || chunk->size < size))
    chunk = chunk->next;
  if (chunk)
    assert_chunk_data(chunk);
  return chunk;
}

static t_chunk* alloc_pool_chunk(t_pool* pool, size_t requested_size) {
  size_t size = align_up(requested_size);
  ft_printf("alloc_pool_chunk: requested_size %u, size: %u\n", requested_size, size);
  assert(size <= pool->max_chunk_size && "alloc_pool_chunk: requested_size > pool->max_chunk_size");
  t_chunk* chunk = find_next_unused_chunk(pool, NULL, size);
  ft_printf("alloc_pool_chunk: next unused chunk %p, is tracked one? %b\n", chunk, chunk == pool->free_chunks);
  if (!chunk) {
    return build_pool_chunk(pool, requested_size);
  }
  if (can_split_chunk(pool, chunk, size)) {
    split_pool_chunk(pool, chunk, requested_size);
    pool->free_chunks = NULL;
    pool->free_chunks = find_next_unused_chunk(pool, chunk->next, 0);
  }
  else {
    chunk->used = true;
    pool->free_chunks = NULL;
    pool->free_chunks = find_next_unused_chunk(pool, NULL, 0);
  }
  assert_chunk_data(chunk);
  return chunk;
}

static bool dealloc_pool_chunk(t_pool* pool, t_chunk* chunk) {
  assert(chunk->used && "dealloc_pool_chunk: chunk is not used");
  chunk->used = false;
  update_pool_smallest_freed_chunk(pool, chunk);
  merge_pool_chunks(pool, chunk);
  assert(pool->last_chunk && pool->last_chunk->size > 0 && "dealloc_pool_chunk: pool->last_chunk is NULL");
  return true;
}

static bool dealloc_large_pool_chunk(t_pool* pool, t_chunk* chunk) {
  assert(chunk->used && "dealloc_large_pool_chunk: chunk is not used");
  if (chunk->prev)
    chunk->prev->next = chunk->next;
  if (chunk->next)
    chunk->next->prev = chunk->prev;
  if (pool->chunks == chunk)
    pool->chunks = chunk->next;
  if (pool->last_chunk == chunk)
    pool->last_chunk = chunk->prev;
  bool ok = munmap(chunk, get_chunk_size(chunk)) == 0;
  if (!ok)
    return false;
  return true;
}

static t_chunk* find_pool_chunk_by_data(t_pool* pool, void* ptr, bool large_pool) {
  if (!ptr)
    return NULL;
  t_chunk* chunk = pool->chunks;
  if (!large_pool) {
    if (ptr < pool->data || ptr >= pool->unmapped)
      return NULL;
    // we can make some assumptions and optimize this
    uint8_t closer_to_end = (ptr - pool->data) > (pool->unmapped - ptr);
    if (closer_to_end) {
      chunk = pool->last_chunk;
      while (chunk && get_chunk_data(chunk) != ptr)
        chunk = chunk->prev;
      return chunk;
    }
  }
  while (chunk) {
    if (get_chunk_data(chunk) == ptr)
      return chunk;
    chunk = chunk->next;
  }
  return NULL;
}

static t_chunk* find_chunk_by_data(void* ptr, t_pool** pool) {
  t_chunk* chunk;
  for (uint8_t i = 0; i < HEAP_POOLS; i++) {
    if (pool)
      *pool = &heap.pools[i];
    chunk = find_pool_chunk_by_data(&heap.pools[i], ptr, false);
    if (chunk)
      return chunk;
  }
  if (pool)
    *pool = &heap.large_pool;
  return find_pool_chunk_by_data(&heap.large_pool, ptr, true);
}

static t_chunk* alloc(size_t req_size) {
  build_pools();
  if (req_size == 0)
    return NULL;
  size_t size = align_up(req_size);
  size_t chunk_size = size + sizeof(t_chunk);
  for (uint8_t i = 0; i < HEAP_POOLS; i++) {
    if (chunk_size <= heap.pools[i].max_chunk_size) {
      if (!init_pool(&heap.pools[i]))
        return NULL;
      t_chunk* chunk = alloc_pool_chunk(&heap.pools[i], req_size);
      if (!chunk)
        continue;
      return chunk;
    }
  }
  return build_large_pool_chunk(&heap.large_pool, req_size);
}

static bool dealloc(void* ptr) {
  if (!ptr)
    return false;
  t_pool* pool;
  t_chunk* chunk;
  for (uint8_t i = 0; i < HEAP_POOLS; i++) {
    pool = &heap.pools[i];
    chunk = find_pool_chunk_by_data(pool, ptr, false);
    if (chunk)
      return dealloc_pool_chunk(pool, chunk);
  }
  if ((chunk = find_pool_chunk_by_data(&heap.large_pool, ptr, true)))
    return dealloc_large_pool_chunk(&heap.large_pool, chunk);
  return false;
}

static t_chunk* realloc_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size) {
  size_t new_size = align_up(new_req_size);
  if (chunk->size >= new_req_size) {
    ft_printf("realloc_pool_chunk: chunk %p has enough size\n", chunk);
    if (can_split_chunk(pool, chunk, new_size))
      split_pool_chunk(pool, chunk, new_req_size);
    ft_printf("realloc_pool_chunk: splitted chunk %p\n", chunk);
    show_chunk(chunk, 0, false);
    show_chunk(chunk->next, 0, false);
    return get_chunk_data(chunk);
  }
  ft_printf("realloc_pool_chunk: chunk %p doesn't have enough size\n", chunk);
  if (grow_pool_chunk(pool, chunk, new_req_size)) {
    ft_printf("realloc_pool_chunk: grown chunk %p\n", chunk);
    return get_chunk_data(chunk);
  }
  ft_printf("realloc_pool_chunk: couldn't grow chunk %p, will try to alloc a new one of %d bytes\n", chunk, new_req_size);
  t_chunk* new_chunk = alloc(new_req_size);
  if (!new_chunk)
    return NULL;
  ft_printf("realloc_pool_chunk: new_chunk %p\n", new_chunk);
  ft_memmove8(get_chunk_data(new_chunk), get_chunk_data(chunk), chunk->size);
  ft_printf("realloc_pool_chunk: moved data from chunk %p to new_chunk %p\n", chunk, new_chunk);
  if (dealloc_pool_chunk(pool, chunk))
    ft_printf("realloc_pool_chunk: dealloced chunk %p\n", chunk);
  return new_chunk;
}

void* malloc(size_t size) {
  build_pools();
  t_chunk* chunk = alloc(size);
  if (!chunk)
    return NULL;
  assert_chunk_data(chunk);
  return get_chunk_data(chunk);
}

void free(void* ptr) {
  build_pools();
  dealloc(ptr);
}
void __test(void) {
  (void)ft_bzero8;
  (void)ft_memmove8;
  (void)find_chunk_by_data;
  (void)realloc_pool_chunk;
}
void* realloc(void* ptr, size_t size) {
  build_pools();
  if (!ptr)
    return malloc(size);
  if (size == 0) {
    free(ptr);
    return NULL;
  }
  t_pool* pool;
  t_chunk* chunk = find_chunk_by_data(ptr, &pool);
  ft_printf("realloc: chunk %p, next\n", chunk);

  if (!chunk)
    return NULL;
  show_chunk(chunk, 0, false);
  if (chunk->next)
    show_chunk(chunk->next, 0, false);
  chunk = realloc_pool_chunk(pool, chunk, size);
  ft_printf("realloc: new_chunk %p from pool %u\n", chunk, pool->size);
  if (!chunk)
    return NULL;
  return get_chunk_data(chunk);
}

void* calloc(size_t nmemb, size_t size) {
  build_pools();
  if (nmemb == 0 || size == 0)
    return NULL;
  if (nmemb > INT32_MAX / size)
    return NULL;
  size_t total_size = nmemb * size;
  void* ptr = malloc(total_size);
  if (!ptr)
    return NULL;
  ft_bzero8(ptr, align_up(total_size));
  return ptr;
}

/* void* reallocarray(void* ptr, size_t nmemb, size_t size) {
  build_pools();
  if (nmemb == 0 || size == 0)
    return NULL;
  if (nmemb > INT32_MAX / size)
    return NULL;
  size_t total_size = nmemb * size;
  return realloc(ptr, total_size);
} */

static void show_chunk(t_chunk* chunk, size_t indent, bool dump) {
  ft_printf("%*s- chunk %p:\n", indent, "", chunk);
  ft_printf("%*s  - header_size: %u bytes\n", indent, "", sizeof(t_chunk));
  ft_printf("%*s  - data_size: %u bytes\n", indent, "", chunk->size);
  ft_printf("%*s  - total_size: %u bytes\n", indent, "", get_chunk_size(chunk));
  ft_printf("%*s  - used: %b\n", indent, "", chunk->used);
  if (dump && chunk->used)
    hexdump(get_chunk_data(chunk), chunk->size);
}

static void show_heap(bool dump) {
  build_pools();
  size_t total_allocated = 0;
  size_t total_used = 0;
  size_t total_freed = 0;
  ft_printf("Heap:\n");
  ft_printf("- page_size: %u bytes\n", heap.page_size);
  ft_printf("- limits:\n");
  ft_printf("  - soft: %u bytes\n", heap.limits.rlim_cur);
  ft_printf("  - hard: %u bytes\n", heap.limits.rlim_max);
  for (uint8_t i = 0; i < HEAP_POOLS; i++) {
    t_pool* pool = &heap.pools[i];
    t_chunk* chunk = pool->chunks;
    ft_printf("Pool %d:\n", i);
    ft_printf("- size: %u bytes\n", pool->size);
    ft_printf("- max_chunk_size: %u bytes\n", pool->max_chunk_size);
    ft_printf("- data:\n");
    size_t pool_total_size = 0;
    size_t pool_used_size = 0;
    size_t pool_freed_size = 0;
    while (chunk) {
      show_chunk(chunk, 2, dump);
      pool_total_size += chunk->size;
      if (chunk->used)
        pool_used_size += chunk->size;
      else
        pool_freed_size += chunk->size;
      chunk = chunk->next;
    }
    ft_printf("- total: %u[%d%%] bytes\n", pool_total_size, pool_total_size * 100 / pool->size);
    ft_printf("- used: %u[%d%%] bytes\n", pool_used_size, pool_used_size * 100 / pool->size);
    ft_printf("- freed: %u[%d%%] bytes\n", pool_freed_size, pool_freed_size * 100 / pool->size);
    size_t unmapped_size = get_pool_unmapped_size(pool);
    ft_printf("- unmapped: %u[%d%%] bytes\n", unmapped_size, unmapped_size * 100 / pool->size);
    total_allocated += pool_total_size;
    total_used += pool_used_size;
    total_freed += pool_freed_size;
  }
  t_pool* pool = &heap.large_pool;
  t_chunk* chunk = pool->chunks;
  ft_printf("Large pool:\n");
  ft_printf("- data:\n");
  size_t pool_total_size = 0;
  while (chunk) {
    show_chunk(chunk, 2, dump);
    if (chunk->used)
      pool_total_size += chunk->size;
    chunk = chunk->next;
  }
  ft_printf("- total: %u bytes\n", pool_total_size);
  total_allocated += pool_total_size;
  ft_printf("Total: %u bytes\n", total_allocated);
  ft_printf("Used: %u bytes\n", total_used);
  ft_printf("Freed: %u bytes\n", total_freed);
}

void show_alloc_mem(void) {
  show_heap(false);
}

void show_alloc_mem_dump(void) {
  show_heap(true);
}

void draw_heap(void) {
  assert(0 && "draw_heap: not implemented");
}
