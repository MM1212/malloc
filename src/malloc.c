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
static t_chunk* grow_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size);
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


static void build_pools(void) {
  if (heap.page_size)
    return;
  heap.enable_asserts = getenv("FT_MALLOC_ASSERT") ? true : false;
  heap.enable_log_chunk_alloc = getenv("FT_MALLOC_LOG_CHUNK_ALLOC") ? true : false;
  heap.page_size = getpagesize();
  if (getrlimit(RLIMIT_AS, &heap.limits) == -1)
    return;
  ft_bzero(&heap.pools, sizeof(heap.pools));
  TINY_POOL.slug = "TINY";
  TINY_POOL.size = TINY_POOL_SIZE_MULTIPLIER * heap.page_size;
  TINY_POOL.max_chunk_size = TINY_POOL_CHUNK_MAX_SIZE_MULTIPLIER(TINY_POOL.size);
  TINY_POOL.max_chunk_size = align_down(TINY_POOL.max_chunk_size);
  SMALL_POOL.slug = "SMALL";
  SMALL_POOL.size = SMALL_POOL_SIZE_MULTIPLIER * heap.page_size;
  SMALL_POOL.max_chunk_size = SMALL_POOL_CHUNK_MAX_SIZE_MULTIPLIER(SMALL_POOL.size);
  SMALL_POOL.max_chunk_size = align_down(SMALL_POOL.max_chunk_size);
  LARGE_POOL.slug = "LARGE";
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

#define ASSERT(...) { if (heap.enable_asserts) assert(__VA_ARGS__); }
static inline void assert_chunk_data(t_chunk* chunk) {
  if (!chunk || !heap.enable_asserts)
    return;
  ASSERT(chunk->size > 0 && "assert_chunk_data: chunk size is 0");
  ASSERT(chunk->size < heap.limits.rlim_cur && "assert_chunk_data: chunk size is too large");
  ASSERT(chunk->size % 16 == 0 && "assert_chunk_data: chunk size is not aligned");
  ASSERT(chunk->size % 8 == 0 && "assert_chunk_data: chunk size is not aligned");
  ASSERT(get_chunk_size(chunk) % 16 == 0 && "assert_chunk_data: chunk total size is not aligned");
  ASSERT(get_chunk_size(chunk) % 8 == 0 && "assert_chunk_data: chunk total size is not aligned");
  if (chunk->next)
    ASSERT((void*)chunk + get_chunk_size(chunk) == (void*)chunk->next && "assert_chunk_size: chunk size is incorrect");
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
  DEBUG_LOG("build_pool_chunk: pool %s[%p], requested_size %u\n", pool->slug, pool, requested_size);
  size_t data_size = align_up(requested_size);
  size_t chunk_size = data_size + sizeof(t_chunk);
  ASSERT(chunk_size <= pool->max_chunk_size && "build_pool_chunk: chunk_size > pool->max_chunk_size");
  if (get_pool_unmapped_size(pool) < chunk_size)
    return NULL;
  t_chunk* chunk = pool->unmapped;
  ft_bzero8(chunk, sizeof(t_chunk));
  chunk->size = data_size;
  chunk->used = true;
  chunk->next = NULL;
  chunk->prev = pool->last_chunk;
  DEBUG_POOL(pool);
  DEBUG_CHUNK(chunk);
  if (!pool->chunks)
    pool->chunks = chunk;
  else
    pool->last_chunk->next = chunk;
  pool->last_chunk = chunk;
  pool->unmapped = (void*)chunk + chunk_size;
  if (pool->unmapped > pool->data + pool->size)
    pool->unmapped = pool->data + pool->size;
  DEBUG_POOL(pool);
  ASSERT(pool->unmapped <= pool->data + pool->size && "build_pool_chunk: pool->unmapped is out of bounds");
  assert_chunk_data(chunk);
  return chunk;
}

static inline void merge_two_chunks(t_pool* pool, t_chunk* a, t_chunk* b) {
  ASSERT(a->next == b && b->prev == a && "merge_two_chunks: chunks are not adjacent");
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
  ASSERT(IS_LARGE_POOL(pool) && "grow_large_pool_chunk: pool is not large");
  size_t new_size = align_up_to_power_of_2(align_up(new_req_size) + sizeof(t_chunk), heap.page_size);
  size_t new_chunk_size = new_size + sizeof(t_chunk);
  DEBUG_LOG("grow_large_pool_chunk: pool %s[%p], chunk %p, new_req_size %u, new_chunk_size: %u\n", pool->slug, pool, chunk, new_req_size, new_chunk_size);
  t_chunk* new_chunk = mmap(NULL, new_chunk_size, MMAP_FLAGS);
  if (new_chunk == MAP_FAILED)
    return NULL;
  ft_bzero8(new_chunk, sizeof(t_chunk));
  new_chunk->size = new_size;
  new_chunk->used = true;
  new_chunk->next = chunk->next;
  new_chunk->prev = chunk->prev;
  if (chunk->next)
    chunk->next->prev = new_chunk;
  if (chunk->prev)
    chunk->prev->next = new_chunk;
  if (pool->chunks == chunk)
    pool->chunks = new_chunk;
  if (pool->free_chunks == chunk)
    pool->free_chunks = new_chunk;
  if (pool->last_chunk == chunk)
    pool->last_chunk = new_chunk;
  ft_memmove8(get_chunk_data(new_chunk), get_chunk_data(chunk), chunk->size);
  munmap(chunk, get_chunk_size(chunk));
  return new_chunk;
}

static t_chunk* grow_pool_chunk(t_pool* pool, t_chunk* chunk, size_t new_req_size) {
  DEBUG_LOG("grow_pool_chunk: pool %s[%p], chunk %p, new_req_size %u\n", pool->slug, pool, chunk, new_req_size);
  if (IS_LARGE_POOL(pool))
    return grow_large_pool_chunk(pool, chunk, new_req_size);
  size_t new_size = align_up(new_req_size);
  size_t new_chunk_size = new_size + sizeof(t_chunk);
  if (new_size <= chunk->size)
    return chunk;
  if (new_chunk_size > pool->max_chunk_size)
    return NULL;
  if (!chunk->next) {
    if (get_pool_unmapped_size(pool) < new_chunk_size)
      return NULL;
    chunk->size = new_size;
    pool->unmapped = (void*)chunk + new_chunk_size;
    return chunk;
  }
  else if (
    !chunk->next->used &&
    (get_chunk_size(chunk->next) + get_chunk_size(chunk) >= new_chunk_size)
    ) {
    merge_two_chunks(pool, chunk, chunk->next);
    if (can_split_chunk(pool, chunk, new_size)) {
      split_pool_chunk(pool, chunk, new_req_size);
      return chunk;
    }
    return chunk;
  }
  return NULL;
}

static t_chunk* build_large_pool_chunk(t_pool* pool, size_t requested_size) {
  size_t chunk_size = align_up_to_power_of_2(align_up(requested_size) + sizeof(t_chunk), heap.page_size);
  if (chunk_size > heap.limits.rlim_cur)
    return NULL;
  if (chunk_size == align_up(requested_size))
    chunk_size += heap.page_size;
  size_t data_size = chunk_size - sizeof(t_chunk);
  DEBUG_LOG("build_large_pool_chunk: pool %s[%p], requested_size %u, chunk_size %u\n", pool->slug, pool, requested_size, chunk_size);
  t_chunk* chunk = mmap(NULL, chunk_size, MMAP_FLAGS);
  if (chunk == MAP_FAILED)
    return NULL;
  ft_bzero8(chunk, sizeof(t_chunk));
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
  DEBUG_LOG("can_split_chunk: pool %p, chunk %p, split_size %u\n", pool, chunk, split_size);
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
  DEBUG_LOG("split_pool_chunk: pool %p, chunk %p, requested_size %u\n", pool, chunk, requested_size);
  size_t size = align_up(requested_size);
  ASSERT(can_split_chunk(pool, chunk, size) && "split_pool_chunk: can't split chunk");
  size_t chunk_size = get_chunk_size(chunk);
  size_t left_split_chunk_size = size + sizeof(t_chunk);
  size_t right_split_chunk_size = chunk_size - left_split_chunk_size;
  t_chunk* right_chunk = (void*)chunk + left_split_chunk_size;
  ft_bzero8(right_chunk, sizeof(t_chunk));
  DEBUG_LOG("split_pool_chunk: left_chunk_size: %u, left_chunk %p\n", left_split_chunk_size, chunk);
  if (right_chunk == chunk->next)
    ASSERT(0 && "split_pool_chunk: chunk == chunk->next");
  DEBUG_LOG("split_pool_chunk: right_chunk_size: %u, right_chunk %p\n", right_split_chunk_size, right_chunk);
  right_chunk->size = right_split_chunk_size - sizeof(t_chunk);
  right_chunk->used = false;
  if (chunk->next) {
    DEBUG_CHUNK(chunk);
    chunk->next->prev = right_chunk;
  }
  right_chunk->next = chunk->next;
  right_chunk->prev = chunk;
  chunk->size = size;
  chunk->used = true;
  chunk->next = right_chunk;
  if (pool->last_chunk == chunk)
    pool->last_chunk = right_chunk;
  DEBUG_LOG("split_pool_chunk: left_chunk %p, right_chunk %p\n", chunk, right_chunk);
  // if the right chunk is smaller than the free_chunks, update free_chunks
  pool->free_chunks = NULL;
  pool->free_chunks = find_next_unused_chunk(pool, NULL, 0);
  right_chunk = merge_pool_chunks(pool, right_chunk);
  assert_chunk_data(chunk);
  assert_chunk_data(right_chunk);
  return right_chunk;
}

static t_chunk* merge_pool_chunks(t_pool* pool, t_chunk* chunk) {
  DEBUG_LOG("merge_pool_chunks: pool %p, chunk %p\n", pool, chunk);
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
  DEBUG_LOG("find_next_unused_chunk: chunk %p, size %u\n", chunk, size);
  if (!chunk) {
    DEBUG_LOG("find_next_unused_chunk: no chunk\n");
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
  DEBUG_LOG("alloc_pool_chunk: requested_size %u, size: %u\n", requested_size, size);
  ASSERT(size <= pool->max_chunk_size && "alloc_pool_chunk: requested_size > pool->max_chunk_size");
  t_chunk* chunk = find_next_unused_chunk(pool, NULL, size);
  DEBUG_LOG("alloc_pool_chunk: next unused chunk %p, is tracked one? %b\n", chunk, chunk == pool->free_chunks);
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
  DEBUG_LOG("alloc_pool_chunk: chunk %p of size %u bytes\n", chunk, chunk->size);
  assert_chunk_data(chunk);
  return chunk;
}

static bool dealloc_pool_chunk(t_pool* pool, t_chunk* chunk) {
  DEBUG_LOG("dealloc_pool_chunk: chunk %p\n", chunk);
  ASSERT(chunk->used && "dealloc_pool_chunk: chunk is not used");
  chunk->used = false;
  chunk = merge_pool_chunks(pool, chunk);
  // if the chunk is the last one, we can move the unmapped pointer
  if (!chunk->next) {
    DEBUG_LOG("dealloc_pool_chunk: deleting chunk %p from pool %s[%p]\n", chunk, pool->slug, pool->data);
    DEBUG_CHUNK(chunk);
    if (pool->chunks == chunk)
      pool->chunks = NULL;
    pool->last_chunk = chunk->prev;
    if (chunk->prev)
      chunk->prev->next = NULL;
    pool->unmapped = (void*)chunk;
    pool->free_chunks = NULL;
    DEBUG_POOL(pool);
    pool->free_chunks = find_next_unused_chunk(pool, NULL, 0);
  }
  else
    update_pool_smallest_freed_chunk(pool, chunk);
  return true;
}

static bool dealloc_large_pool_chunk(t_pool* pool, t_chunk* chunk) {
  DEBUG_LOG("dealloc_large_pool_chunk: chunk %p\n", chunk);
  ASSERT(chunk->used && "dealloc_large_pool_chunk: chunk is not used");
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
  DEBUG_LOG("find_pool_chunk_by_data: pool %s[%p], ptr %p, is_large: %b\n", pool->slug, pool->data, ptr, large_pool);
  DEBUG_POOL(pool);
  if (!ptr)
    return NULL;
  t_chunk* chunk = pool->chunks;
  if (!large_pool) {
    DEBUG_LOG("find_pool_chunk_by_data: searching in pool %s[%p -> %p], belongs to it? %b\n", pool->slug, pool->data, pool->unmapped, ptr >= pool->data && ptr < pool->unmapped);
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
  DEBUG_LOG("alloc: req_size %u\n", req_size);
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
  DEBUG_LOG("dealloc: ptr %p\n", ptr);
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
  DEBUG_LOG("realloc_pool_chunk: pool %s[%p], chunk %p, new_req_size %u\n", pool->slug, pool, chunk, new_req_size);
  size_t new_size = align_up(new_req_size);
  if (chunk->size >= new_req_size) {
    DEBUG_LOG("realloc_pool_chunk: chunk %p has enough size -> %u bytes\n", chunk, chunk->size);
    if (can_split_chunk(pool, chunk, new_size)) {
      split_pool_chunk(pool, chunk, new_req_size);
      DEBUG_LOG("realloc_pool_chunk: splitted chunk %p\n", chunk);
    }
    DEBUG_CHUNK(chunk);
    DEBUG_CHUNK(chunk->next);
    return chunk;
  }
  DEBUG_LOG("realloc_pool_chunk: chunk %p doesn't have enough size\n", chunk);
  t_chunk* grown_chunk = grow_pool_chunk(pool, chunk, new_req_size);
  if (grown_chunk) {
    DEBUG_LOG("realloc_pool_chunk: grown chunk %p\n", grown_chunk);
    return grown_chunk;
  }
  DEBUG_LOG("realloc_pool_chunk: couldn't grow chunk %p, will try to alloc a new one of %d bytes\n", chunk, new_req_size);
  t_chunk* new_chunk = alloc(new_req_size);
  if (!new_chunk)
    return NULL;
  DEBUG_LOG("realloc_pool_chunk: new_chunk %p\n", new_chunk);
  ft_memmove8(get_chunk_data(new_chunk), get_chunk_data(chunk), chunk->size);
  DEBUG_LOG("realloc_pool_chunk: moved data from chunk %p to new_chunk %p\n", chunk, new_chunk);
  if (dealloc_pool_chunk(pool, chunk))
    DEBUG_LOG("realloc_pool_chunk: dealloced chunk %p\n", chunk);
  return new_chunk;
}

void* malloc(size_t size) {
  pthread_mutex_lock(&lock);
  build_pools();
  t_chunk* chunk = alloc(size);
  assert_chunk_data(chunk);
  if (heap.enable_log_chunk_alloc)
    show_chunk(2, chunk, 0, false);
  pthread_mutex_unlock(&lock);
  if (!chunk) {
    DEBUG_LOG("malloc: couldn't alloc %u bytes\n", size);
    return NULL;
  }
  return get_chunk_data(chunk);
}

void free(void* ptr) {
  pthread_mutex_lock(&lock);
  build_pools();
  dealloc(ptr);
  pthread_mutex_unlock(&lock);
}

void* realloc(void* ptr, size_t size) {
  DEBUG_LOG("realloc: ptr %p, size %u\n", ptr, size);
  if (!ptr)
    return malloc(size);
  if (size == 0) {
    free(ptr);
    return NULL;
  }
  pthread_mutex_lock(&lock);
  t_pool* pool;
  t_chunk* chunk = find_chunk_by_data(ptr, &pool);
  DEBUG_LOG("realloc: chunk %p, next\n", chunk);

  if (!chunk) {
    pthread_mutex_unlock(&lock);
    return NULL;
  }
  DEBUG_CHUNK(chunk);
  DEBUG_CHUNK(chunk->next);
  chunk = realloc_pool_chunk(pool, chunk, size);
  DEBUG_LOG("realloc: new_chunk %p from pool %u\n", chunk, pool->size);
  if (heap.enable_log_chunk_alloc)
    show_chunk(2, chunk, 0, false);
  pthread_mutex_unlock(&lock);
  if (!chunk)
    return NULL;
  return get_chunk_data(chunk);
}

void* calloc(size_t nmemb, size_t size) {
  DEBUG_LOG("calloc: nmemb %u, size %u\n", nmemb, size);
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

void* reallocarray(void* ptr, size_t nmemb, size_t size) {
  DEBUG_LOG("reallocarray: ptr %p, nmemb %u, size %u\n", ptr, nmemb, size);
  if (nmemb == 0 || size == 0)
    return NULL;
  if (nmemb > INT32_MAX / size)
    return NULL;
  size_t total_size = nmemb * size;
  return realloc(ptr, total_size);
}

static void show_chunk(int target, t_chunk* chunk, size_t indent, bool dump) {
  if (!chunk)
    return;
  ft_fprintf(target, "%*s- chunk %p:\n", indent, "", chunk);
  ft_fprintf(target, "%*s  - header_size: %u bytes\n", indent, "", sizeof(t_chunk));
  ft_fprintf(target, "%*s  - data_size: %u bytes\n", indent, "", chunk->size);
  ft_fprintf(target, "%*s  - total_size: %u bytes\n", indent, "", get_chunk_size(chunk));
  ft_fprintf(target, "%*s  - used: %b\n", indent, "", chunk->used);
  ft_fprintf(target, "%*s  - next: %p\n", indent, "", chunk->next);
  ft_fprintf(target, "%*s  - prev: %p\n", indent, "", chunk->prev);
  if (dump && chunk->used)
    hexdump(get_chunk_data(chunk), chunk->size);
}

static void show_pool(t_pool* pool, size_t indent, bool dump, bool data) {
  ft_printf("%*sPool %s[%p]:\n", indent, "", pool->slug, pool->data);
  ft_printf("%*s- size: %u bytes\n", indent, "", pool->size);
  ft_printf("%*s- max_chunk_size: %u bytes\n", indent, "", pool->max_chunk_size);
  ft_printf("%*s- min_chunk_size: %u bytes\n", indent, "", pool->min_chunk_size);
  ft_printf("%*s- free_chunks: %p\n", indent, "", pool->free_chunks);
  if (pool->free_chunks)
    show_chunk(1, pool->free_chunks, indent + 2, dump);
  ft_printf("%*s- chunks: %p\n", indent, "", pool->chunks);
  if (pool->chunks)
    show_chunk(1, pool->chunks, indent + 2, dump);
  ft_printf("%*s- last_chunk: %p\n", indent, "", pool->last_chunk);
  if (pool->last_chunk)
    show_chunk(1, pool->last_chunk, indent + 2, dump);

  if (data) {
    ft_printf("%*s- data:\n", indent, "");
    t_chunk* chunk = pool->chunks;
    while (chunk) {
      show_chunk(1, chunk, indent + 2, dump);
      chunk = chunk->next;
    }
  }
}

void show_heap(bool dump) {
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
    show_pool(pool, 0, dump, false);
    ft_printf("- data:\n");
    size_t pool_total_size = 0;
    size_t pool_used_size = 0;
    size_t pool_freed_size = 0;
    while (chunk) {
      show_chunk(1, chunk, 2, dump);
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
    show_chunk(1, chunk, 2, dump);
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
  pthread_mutex_lock(&lock);
  size_t total = 0;
  for (uint8_t i = 0; i < HEAP_POOLS; i++) {
    t_pool* pool = &heap.pools[i];
    t_chunk* chunk = pool->chunks;
    ft_printf("%s pool : %p\n", pool->slug, pool->data);
    while (chunk) {
      if (chunk->used) {
        ft_printf("%p - %p : %u bytes\n", get_chunk_data(chunk), (void*)chunk + get_chunk_size(chunk), chunk->size);
        total += chunk->size;
      }
      chunk = chunk->next;
    }
  }
  t_pool* pool = &heap.large_pool;
  t_chunk* chunk = pool->chunks;
  ft_printf("%s pool : %p\n", pool->slug, pool->chunks ? pool->chunks : pool->data);
  while (chunk) {
    if (chunk->used) {
      ft_printf("%p - %p : %u bytes\n", get_chunk_data(chunk), (void*)chunk + get_chunk_size(chunk), chunk->size);
      total += chunk->size;
    }
    chunk = chunk->next;
  }
  ft_printf("Total : %u bytes\n", total);
  pthread_mutex_unlock(&lock);
}

void show_alloc_mem_ex(void) {
  show_heap(true);
}

#include <sys/ioctl.h>
#define COLOR_RED "\033[0;31m"
#define COLOR_GREEN "\033[0;32m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_RESET "\033[0m"
// scale each chunk size based on the term width <-> pool->size
static void draw_pool(t_pool* pool, size_t term_width) {
  t_chunk* chunk = pool->chunks;
  size_t total_pool_size = 0;
  while (chunk) {
    total_pool_size += get_chunk_size(chunk);
    chunk = chunk->next;
  }
  ft_printf("Pool %s[%p]:\n", pool->slug, pool->data);
  ft_printf("Size: %u bytes\n", pool->size);
  ft_printf("In Use: %u bytes\n", total_pool_size);
  for (size_t i = 0; i < term_width; i++) {
    ft_printf("-");
  }
  ft_printf("\n");
  ft_printf("|");
  term_width -= 2;
  chunk = pool->chunks;
  size_t written = 0;
  while (chunk) {
    size_t chunk_size = get_chunk_size(chunk);
    size_t chunk_width = chunk_size * term_width / total_pool_size;
    if (chunk_width == 0)
      chunk_width = 1;
    written += chunk_width;
    for (size_t i = 0; i < chunk_width; i++) {
      if (chunk->used)
        ft_printf(COLOR_GREEN"|"COLOR_RESET);
      else
        ft_printf(COLOR_RED"|"COLOR_RESET);
    }
    chunk = chunk->next;
  }
  for (size_t i = written; i < term_width; i++) {
    ft_printf(COLOR_YELLOW"."COLOR_RESET);
  }
  ft_printf("|\n");
  for (size_t i = 0; i < term_width + 2; i++) {
    ft_printf("-");
  }
  ft_printf("\n\n");
}

void draw_heap(void) {
  pthread_mutex_lock(&lock);
  struct winsize w;
  if (ioctl(0, TIOCGWINSZ, &w) == -1)
    return;
  for (uint8_t i = 0; i < HEAP_POOLS; i++) {
    t_pool* pool = &heap.pools[i];
    draw_pool(pool, w.ws_col);
  }
  t_pool* pool = &heap.large_pool;
  draw_pool(pool, w.ws_col);
  pthread_mutex_unlock(&lock);
}
