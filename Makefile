main: main.c line.c slice.c
	cc view.c main.c -o ng-editor

test_binaries/line_test: test.h line_test.c line.c
	cc line_test.c -o test_binaries/line_test

test_binaries/slice_test: test.h slice_test.c slice.c
	cc slice_test.c -o test_binaries/slice_test

test_binaries:
	mkdir ./test_binaries

.PHONY: test
test: test_binaries test_binaries/line_test test_binaries/slice_test
	./test_binaries/line_test
	./test_binaries/slice_test

.PHONY: run
run: main
	./ng-editor
