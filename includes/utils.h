/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.h                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: martiper <martiper@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/04/02 12:48:52 by martiper          #+#    #+#             */
/*   Updated: 2024/04/02 23:18:47 by martiper         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <heap.h>
#include <libft.h>

static inline size_t align_up_to_power_of_2(size_t size, size_t power) {
  return (size + (power - 1)) & ~(power - 1);
}

static inline size_t align_down_to_power_of_2(size_t size, size_t power) {
  return size & ~(power - 1);
}

static inline size_t align_up(size_t size) {
 return align_up_to_power_of_2(size, ALIGNMENT);
}

static inline size_t align_down(size_t size) {
  return align_down_to_power_of_2(size, ALIGNMENT);
}

// move 8 bytes at a time
static void* ft_memmove8(void* dst, const void* src, size_t n) {
  size_t i = 0;
  n = align_down(n);
  while (i + 8 <= n) {
    *(uint64_t*)(dst + i) = *(uint64_t*)(src + i);
    i += 8;
  }
  return dst;
}

static void* ft_bzero8(void* dst, size_t n) {
  size_t i = 0;
  n = align_down(n);
  while (i + 8 <= n) {
    *(uint64_t*)(dst + i) = 0;
    i += 8;
  }
  return dst;
}

#define DUMP_BYTES_PER_LINE 16

static void dump_addr(void* ptr, size_t size) {
  ft_printf("%016p", ptr);
  ft_printf("  ");
  for (size_t i = 0; i < size; i++) {
    if (i == DUMP_BYTES_PER_LINE / 2)
      ft_printf("  ");
    else if (i > 0)
      ft_printf(" ");
    ft_printf("%02x", *((uint8_t*)ptr + i));
  }
  // fill the rest of the line with spaces if size < DUMP_BYTES_PER_LINE
  ft_printf("%*s", (DUMP_BYTES_PER_LINE - size) * 3 + (size < DUMP_BYTES_PER_LINE / 2 ? 1 : 0), "");
  ft_printf("  ");
  ft_printf("|");
  size_t i = 0;
  for (i = 0; i < size; i++) {
    char c = *((uint8_t*)ptr + i);
    ft_printf("%c", ft_isprint(c) ? c : '.');
  }
  for (; i < DUMP_BYTES_PER_LINE; i++)
    ft_printf(" ");
  ft_printf("|\n");
}

static void hexdump(void* ptr, size_t size) {
  if (!ptr || !size)
    return;
  size_t addresses = size / DUMP_BYTES_PER_LINE;
  size_t remaining = size % DUMP_BYTES_PER_LINE;
  for (size_t i = 0;  i < addresses; i++) {
    dump_addr(ptr, DUMP_BYTES_PER_LINE);
    ptr += DUMP_BYTES_PER_LINE;
  }
  if (remaining)
    dump_addr(ptr, remaining);
}
