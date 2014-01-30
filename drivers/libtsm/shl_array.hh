/*
 * shl - Dynamic Array
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * A dynamic array implementation
 */

#ifndef SHL_ARRAY_H
#define SHL_ARRAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct shl_array {
	size_t element_size;
	size_t length;
	size_t size;
	void *data;
};

#define SHL_ARRAY_AT(_arr, _type, _pos) \
	(&((_type*)shl_array_get_array(_arr))[(_pos)])

static inline int shl_array_new(struct shl_array **out, size_t element_size,
				size_t initial_size)
{
	struct shl_array *arr;

	if (!out || !element_size)
		return -EINVAL;

	if (!initial_size)
		initial_size = 4;

	arr = reinterpret_cast<struct shl_array *>(malloc(sizeof(*arr)));
	if (!arr)
		return -ENOMEM;
	memset(arr, 0, sizeof(*arr));
	arr->element_size = element_size;
	arr->length = 0;
	arr->size = initial_size;

	arr->data = malloc(arr->element_size * arr->size);
	if (!arr->data) {
		free(arr);
		return -ENOMEM;
	}

	*out = arr;
	return 0;
}

static inline void shl_array_free(struct shl_array *arr)
{
	if (!arr)
		return;

	free(arr->data);
	free(arr);
}

/* Compute next higher power-of-2 of @v. Returns 4 in case v is 0. */
static inline size_t shl_array_pow2(size_t v)
{
	size_t i;

	if (!v)
		return 4;

	--v;

	for (i = 1; i < 8 * sizeof(size_t); i *= 2)
		v |= v >> i;

	return ++v;
}

/* resize to length=size and zero out new array entries */
static inline int shl_array_zresize(struct shl_array *arr, size_t size)
{
	void *tmp;
	size_t newsize;

	if (!arr)
		return -EINVAL;

	if (size > arr->size) {
		newsize = shl_array_pow2(size);
		tmp = realloc(arr->data, arr->element_size * newsize);
		if (!tmp)
			return -ENOMEM;

		arr->data = tmp;
		arr->size = newsize;

		memset(((uint8_t*)arr->data) + arr->element_size * arr->length,
		       0, arr->element_size * (size - arr->length));
	}

	arr->length = size;
	return 0;
}

static inline int shl_array_push(struct shl_array *arr, const void *data)
{
	void *tmp;
	size_t newsize;

	if (!arr || !data)
		return -EINVAL;

	if (arr->length >= arr->size) {
		newsize = arr->size * 2;
		tmp = realloc(arr->data, arr->element_size * newsize);
		if (!tmp)
			return -ENOMEM;

		arr->data = tmp;
		arr->size = newsize;
	}

	memcpy(((uint8_t*)arr->data) + arr->element_size * arr->length,
	       data, arr->element_size);
	++arr->length;

	return 0;
}

static inline void shl_array_pop(struct shl_array *arr)
{
	if (!arr || !arr->length)
		return;

	--arr->length;
}

static inline void *shl_array_get_array(struct shl_array *arr)
{
	if (!arr)
		return NULL;

	return arr->data;
}

static inline size_t shl_array_get_length(struct shl_array *arr)
{
	if (!arr)
		return 0;

	return arr->length;
}

static inline size_t shl_array_get_bsize(struct shl_array *arr)
{
	if (!arr)
		return 0;

	return arr->length * arr->element_size;
}

static inline size_t shl_array_get_element_size(struct shl_array *arr)
{
	if (!arr)
		return 0;

	return arr->element_size;
}

#endif /* SHL_ARRAY_H */
