/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   malloc.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: martiper <martiper@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/04/02 16:38:35 by martiper          #+#    #+#             */
/*   Updated: 2024/04/03 15:01:06 by martiper         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include <stddef.h>
#include <stdbool.h>

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void* reallocarray(void* ptr, size_t nmemb, size_t size);
void show_alloc_mem(void);
void show_alloc_mem_ex(void);
void show_heap(bool dump);
void draw_heap(void);
