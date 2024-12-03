main: main.c
	cc main.c -o ng-editor

.PHONY: run
run: main
	./ng-editor
