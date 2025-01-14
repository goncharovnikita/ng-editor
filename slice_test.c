#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "test.h"
#include "slice.c"

int test_slice_append() {
	Slice* slice = slice_new(1);
	int test_int = 42;
	slice = slice_append(slice, (void*)&test_int);

	int* result = (int*)slice_get(slice, 0);

	if (*result != 42) {
		printf("FAIL: test_slice_append, expected 42, got: %d\n", *result);
		free(slice->items);
		free(slice);

		return 1;
	}

	free(slice->items);
	free(slice);

	return 0;
}

int test_slice_reslice() {
	Slice* slice = slice_new(0);
	int test_int = 42;
	slice = slice_append(slice, (void*)&test_int);

	int* result = (int*)slice_get(slice, 0);

	if (*result != 42) {
		printf("FAIL: test_slice_append, expected 42, got: %d\n", *result);
		free(slice->items);
		free(slice);

		return 1;
	}

	free(slice->items);
	free(slice);

	return 0;
}

int test_slice_pop() {
	Slice* slice = slice_new(1);
	int test_int = 42;
	slice = slice_append(slice, (void*)&test_int);

	int* result = (int*)slice_pop(slice);

	if (*result != 42) {
		printf("FAIL: test_slice_append, expected 42, got: %d\n", *result);
		free(slice->items);
		free(slice);

		return 1;
	}

	if (slice->size != 0) {
		printf("FAIL: test_slice_pop, expected size aftter pop equal to 0, got: %d\n", slice->size);
		free(slice->items);
		free(slice);

		return 1;
	}


	free(slice->items);
	free(slice);

	return 0;
}

int main() {
	bool test_failed = run_tests(3,
		test_slice_append,
		test_slice_reslice,
		test_slice_pop
	);

	if (test_failed)
		return 1;

	return 0;
}
