#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define SLICE_GROW_AMP 2

typedef struct {
	int size;
	int cap;
	int sizeof_item;
	void* items;
} Slice;

Slice* slice_new(int cap) {
	Slice* slice = (Slice*)malloc(sizeof(Slice));
	int sizeof_item = sizeof(void*);
	void* items = malloc(cap * sizeof_item);

	slice->size = 0;
	slice->cap = cap;
	slice->sizeof_item = sizeof_item;
	slice->items = items;

	return slice;
}

Slice* slice_grow(Slice* slice) {
	int new_slice_cap = slice->cap * SLICE_GROW_AMP;
	if (new_slice_cap == 0)
		new_slice_cap = 8;

	Slice* new_slice = slice_new(new_slice_cap);
	memcpy(new_slice->items, slice->items, slice->size * slice->sizeof_item);
	new_slice->size = slice->size;

	free(slice);

	return new_slice;
}

Slice* slice_append(Slice* slice, void* item) {
	if (slice->cap == slice->size)
		slice = slice_grow(slice);

	memcpy(slice->items + slice->size * slice->sizeof_item, item, slice->sizeof_item);
	slice->size++;

	return slice;
}

void* slice_get(Slice* slice, int index) {
	assert(index < slice->cap);

	return slice->items + index * slice->sizeof_item;
}

void* slice_pop(Slice* slice) {
	assert(slice->cap);

	void* item = slice_get(slice, slice->size - 1);
	slice->size--;

	return item;
}
