main: main.c
	cc view.c main.c -o ng-editor

.PHONY: run
run: main
	./ng-editor
