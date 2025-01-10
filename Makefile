main: main.c
	cc view.c main.c -o ng-editor

test_binaries/line_test: line_test.c line.c
	cc line_test.c -o test_binaries/line_test

test_binaries:
	mkdir ./test_binaries

.PHONY: test
test: test_binaries test_binaries/line_test
	./test_binaries/line_test

.PHONY: run
run: main
	./ng-editor
