/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   heap.h                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: martiper <martiper@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/04/02 12:00:15 by martiper          #+#    #+#             */
/*   Updated: 2024/04/02 23:46:00 by martiper         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/resource.h>
#ifndef __USE_MISC
# define  __USE_MISC
#endif
#include <sys/mman.h>

#define TINY_POOL_SIZE_MULTIPLIER 128 // * PAGE_SIZE
#define SMALL_POOL_SIZE_MULTIPLIER 1024 // * PAGE_SIZE

#define TINY_POOL_CHUNK_MAX_SIZE_MULTIPLIER(x) (x / 300)
#define SMALL_POOL_CHUNK_MAX_SIZE_MULTIPLIER(x) (x / 50)

#define HEAP_POOLS 2
#define TINY_POOL_IDX 0
#define SMALL_POOL_IDX 1

#define ALIGNMENT 16

#define MMAP_FLAGS PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0

// an alloc
typedef struct s_chunk
{
  size_t size; // size of the segment after this header
  bool used; // is the segment in use
  struct s_chunk* next; // next chunk in the pool
  struct s_chunk* prev; // prev chunk in the pool
} t_chunk;

typedef struct s_pool
{
  size_t size; // size of the pool, 0 if infinite
  uint32_t max_chunk_size; // max_chunk_size to use the pool
  uint32_t min_chunk_size; // min_chunk_size to use the pool
  void* data; // ptr to the first byte of the pool
  void* unmapped; // ptr to the first byte of the unmapped pool, so that malloc when the mapped pool is full is at least O(1)
  t_chunk* chunks; // list of mapped chunks
  t_chunk* last_chunk; // ptr to the last chunk in the pool
  t_chunk* free_chunks; // ptr to the first unused chunk, so that malloc can be at least O(1)
} t_pool;

typedef struct s_heap
{
  t_pool pools[HEAP_POOLS]; // tiny, small
  t_pool large_pool; // every chunk comes from mmap directly
  size_t page_size;
  size_t total_allocd;
  struct rlimit limits;
} t_heap;

static t_heap heap = {0};

#define TINY_POOL (heap.pools[TINY_POOL_IDX])
#define SMALL_POOL (heap.pools[SMALL_POOL_IDX])
#define LARGE_POOL (heap.pools[LARGE_POOL_IDX])
#define IS_LARGE_POOL(pool) (pool->size == 0)

static size_t align_up_to_power_of_2(size_t size, size_t power);
static size_t align_down_to_power_of_2(size_t size, size_t power);
static size_t align_up(size_t size);
static size_t align_down(size_t size);
static void* ft_memmove8(void* dst, const void* src, size_t n);
static void* ft_bzero8(void* dst, size_t n);
static void hexdump(void* ptr, size_t size);
