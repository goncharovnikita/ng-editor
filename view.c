#include <stdlib.h>

#include "view.h"
#include "calc.h"

ViewT* view_new(int origin_x, int origin_y, int end_x, int end_y, ViewT* parent) {
	ViewT* view =  (ViewT*)malloc(sizeof(ViewT));

	if (parent != NULL) {
		end_x = MIN(parent->end.x, end_x);
		end_y = MIN(parent->end.y, end_y);
	}

	view->origin.x = origin_x;
	view->origin.y = origin_y;
	view->end.x = end_x;
	view->end.y = end_y;
	view->parent = parent;

	return view;
}

ViewT* view_new_embedded(ViewT* parent) {
	return view_new(parent->origin.x, parent->origin.y, parent->end.x, parent->end.y, parent);
}

int view_x(ViewT* view, int x) {
	while (view->parent != NULL) {
		x += view->origin.x;
		view = view->parent;
	}

	return x;
}

int view_y(ViewT* view, int y) {
	while (view->parent != NULL) {
		y += view->origin.y;
		view = view->parent;
	}

	return y;
}

int view_cols(ViewT* view) {
	return view->end.x - view->origin.x;
}

int view_rows(ViewT* view) {
	return view->end.y - view->origin.y;
}
