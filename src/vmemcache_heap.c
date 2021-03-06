/*
 * Copyright 2018-2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * vmemcache_heap.c -- implementation of simple vmemcache linear allocator
 */

#include "vmemcache.h"
#include "vmemcache_heap.h"
#include "vec.h"
#include "sys_util.h"

struct heap {
	os_mutex_t lock;
	size_t fragment_size;
	VEC(, struct heap_entry) entries;

	/* statistic - current size of memory pool used for values */
	stat_t size_used;
};

/*
 * vmcache_heap_create -- create vmemcache heap
 */
struct heap *
vmcache_heap_create(void *addr, size_t size, size_t fragment_size)
{
	LOG(3, "addr %p size %zu", addr, size);

	struct heap_entry whole_heap = {addr, size};
	struct heap *heap;

	heap = Zalloc(sizeof(struct heap));
	if (heap == NULL) {
		ERR("!Zalloc");
		return NULL;
	}

	util_mutex_init(&heap->lock);
	heap->fragment_size = fragment_size;
	VEC_INIT(&heap->entries);
	VEC_PUSH_BACK(&heap->entries, whole_heap);

	return heap;
}

/*
 * vmcache_heap_destroy -- destroy vmemcache heap
 */
void
vmcache_heap_destroy(struct heap *heap)
{
	LOG(3, "heap %p", heap);

	VEC_DELETE(&heap->entries);
	util_mutex_destroy(&heap->lock);
	Free(heap);
}

/*
 * vmcache_alloc -- allocate memory (take it from the queue)
 */
struct heap_entry
vmcache_alloc(struct heap *heap, size_t size)
{
	LOG(3, "heap %p size %zu", heap, size);

	struct heap_entry he = {NULL, 0};

	size = ALIGN_UP(size, heap->fragment_size);

	util_mutex_lock(&heap->lock);

	if (VEC_POP_BACK(&heap->entries, &he) != 0)
		goto error_no_mem;

	if (he.size > size) {
		struct heap_entry f;
		f.ptr = (void *)((uintptr_t)he.ptr + size);
		f.size = he.size - size;
		VEC_PUSH_BACK(&heap->entries, f);

		he.size = size;
	}

	__sync_fetch_and_add(&heap->size_used, he.size);

error_no_mem:
	util_mutex_unlock(&heap->lock);

	return he;
}

/*
 * vmcache_free -- free memory (give it back to the queue)
 */
void
vmcache_free(struct heap *heap, struct heap_entry he)
{
	LOG(3, "heap %p he.ptr %p he.size %zu", heap, he.ptr, he.size);

	util_mutex_lock(&heap->lock);

	__sync_fetch_and_sub(&heap->size_used, he.size);

	VEC_PUSH_BACK(&heap->entries, he);

	util_mutex_unlock(&heap->lock);
}

/*
 * vmcache_get_heap_used_size -- get the 'size_used' statistic
 */
stat_t
vmcache_get_heap_used_size(struct heap *heap)
{
	return heap->size_used;
}
