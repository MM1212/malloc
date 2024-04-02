/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   test.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: martiper <martiper@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/04/02 17:25:43 by martiper          #+#    #+#             */
/*   Updated: 2024/04/02 23:34:59 by martiper         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stdio.h>
// void show_alloc_mem(void);
int main() {
  // show_alloc_mem();
  void* ptr = malloc(96);
  // show_alloc_mem();
  free(ptr);
  void* ptr2 = malloc(16);
  free(ptr2);
  void* ptr3 = malloc(5235);
  // show_alloc_mem();
  free(ptr3);
  // show_alloc_mem();
  printf("malloc(0) = %p\n", malloc(0));

  return 0;
}
