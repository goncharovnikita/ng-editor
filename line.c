#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LineItem {
	char symbol;
	struct LineItem* next;
	struct LineItem* prev;
} LineItemT;

typedef struct Line {
	LineItemT* item_head;
	struct Line* next;
	struct Line* prev;
} LineT;

LineItemT* line_item_new(char symbol) {
	LineItemT* line_item = (LineItemT*)malloc(sizeof(LineItemT));
	line_item->symbol = symbol;

	return line_item;
}

void line_item_add_next(LineItemT* line_item, LineItemT* next) {
	if (next->prev != NULL) {
		line_item->prev = next->prev;
		next->prev->next = line_item;
	}

	line_item->next = next;
	next->prev = line_item;
}

LineItemT* line_item_copy(LineItemT* line_item) {
	if (line_item == NULL)
		return NULL;

	LineItemT* l = line_item_new(line_item->symbol);

	return l;
}

void line_item_next(LineItemT** line_item) {
	LineItemT* line = *line_item;
	*line_item = line->next;
}

void line_item_prev(LineItemT** line_item) {
	LineItemT* line = *line_item;
	*line_item = line->prev;
}

void line_item_concat(LineItemT* a, LineItemT* b) {
	while (a->next != NULL)
		a = a->next;

	a->next = b;
	b->prev = a;
}

LineItemT* line_item_find_next_symbol(LineItemT* line_head, char symbol) {
	while (line_head != NULL && line_head->symbol != symbol)
		line_head = line_head->next;

	return line_head;
}

LineItemT* line_item_find_tail(LineItemT* line_head) {
	while (line_head->next != NULL)
		line_head = line_head->next;

	return line_head;
}

void line_item_remove_next(LineItemT* line_head) {
	LineItemT* tmp = line_head->next;
	if (tmp == NULL)
		return;

	line_head->next = line_head->next->next;
	line_head->next->prev = line_head;

	free(tmp);
}

LineT* line_new(LineItemT* line_head) {
	LineT* line = (LineT*)malloc(sizeof(LineT));
	line->item_head = line_head;

	return line;
}

void line_free(LineT* line) {
	if (line == NULL)
		return;

	LineItemT* line_item = line->item_head;

	while (line_item != NULL) {
		LineItemT* tmp = line_item;
		line_item = line_item->next;

		free(tmp);
	}

	line_free(line->next);

	free(line);
}

int line_symbols_count(LineT* line) {
	int result = 0;
	LineItemT* line_item = line->item_head;

	while (line_item->next != NULL) {
		line_item = line_item->next;
		result++;
	}

	return result;
}

LineT* line_new_from_str(char* str) {
	LineT* line = line_new(NULL);
	LineItemT* line_item = NULL;
	LineItemT* line_item_next = NULL;

	for (int i = 0; i < strlen(str); i++) {
		if (line_item == NULL) {
			line_item = line_item_new(str[i]);
			line->item_head = line_item;

			continue;
		}

		line_item_next = line_item_new(str[i]);

		line_item->next = line_item_next;
		line_item_next->prev = line_item;
		line_item = line_item_next;
	}

	return line;
}

char* line_to_str(LineT* line) {
	int line_str_len = line_symbols_count(line) + 1;
	char* str = (char*)malloc(sizeof(char) * line_str_len);
	LineItemT* line_item = line->item_head;

	while (line_item != NULL) {
		sprintf(str, "%s%c", str, line_item->symbol);
		line_item = line_item->next;
	}

	return str;
}

LineT* line_copy(LineT* line) {
	if (line == NULL)
		return NULL;

	LineItemT* l_item = line->item_head;
	LineItemT* l_item_copy = line_item_copy(l_item);
	LineT* l = line_new(l_item_copy);

	while (l_item->next != NULL) {
		LineItemT* l_item_copy_next = line_item_copy(l_item->next);
		l_item_copy->next = l_item_copy_next;
		l_item_copy_next->prev = l_item_copy;

		l_item_copy = l_item_copy_next;
		l_item = l_item->next;
	}

	return l;
}

LineT* line_copy_lines_from(LineT* line) {
	LineT* head_copy = line_copy(line);
	LineT* current = line;
	LineT* current_copy = head_copy;

	while (current->next != NULL) {
		LineT* next_copy = line_copy(current->next);
		current_copy->next = next_copy;
		next_copy->prev = current_copy;

		current_copy = next_copy;
		current = current->next;
	}

	return head_copy;
}

int line_count_from(LineT* line) {
	int result = 0;

	while (line != NULL) {
		line = line->next;
		result++;
	}

	return result;
}

LineT* line_find_top(LineT* line) {
	while (line->prev != NULL)
		line = line->prev;

	return line;
}

LineItemT* line_find_next_symbol(LineT* line, char symbol) {
	return line_item_find_next_symbol(line->item_head, symbol);
}

void line_set_head(LineT* line, LineItemT* new_head) {
	line->item_head = new_head;
	new_head->prev = NULL;
}

void line_add_next(LineT* line, LineT* next) {
	if (next->prev != NULL) {
		line->prev = next->prev;
		next->prev->next = line;
	}

	line->next = next;
	next->prev = line;
}

void line_new_before(LineT** line) {
	LineT* l = *line;

	LineItemT* new_line_item = line_item_new('\n');
	LineT* new_line = line_new(new_line_item);

	new_line->prev = l->prev;

	if (l->prev != NULL)
		l->prev->next = new_line;

	new_line->next = l;
	l->prev = new_line;
}

void line_delete_after(LineT** line) {
	LineT* l = *line;

	if (l->next == NULL)
		return;

	LineT* line_to_free = l->next;

	l->next = l->next->next;
	if (l->next != NULL)
		l->next->prev = l;

	line_free(line_to_free);
}

void line_delete_before(LineT* line) {
	if (line->prev == NULL)
		return;

	LineT* line_to_free = line->prev;

	line->prev = line->prev->prev;
	if (line->prev != NULL)
		line->prev->next = line;

	line_free(line_to_free);
}

void line_concat_after(LineT* line) {
	LineItemT* line_head = line->item_head;
	LineItemT* line_tail = line_item_find_tail(line_head);

	if (line->next == NULL)
		return;

	LineItemT* next_line_item = line->next->item_head;
	line_tail->next = next_line_item;
	next_line_item->prev = line_tail;
	line->next->item_head = NULL;

	LineItemT* newline_item = line_item_find_next_symbol(line_head, '\n');
	if (newline_item->prev == NULL) {
		line->item_head = newline_item->next;
		newline_item->next->prev = NULL;
	} else {
		newline_item->prev->next = newline_item->next;
		newline_item->next->prev = newline_item->prev;
	}

	free(newline_item);

	line_delete_after(&line);
}

void line_new_after(LineT** line) {
	LineT* l = *line;

	LineItemT* new_line_item = line_item_new('\n');
	LineT* new_line = line_new(new_line_item);

	new_line->item_head = new_line_item;
	new_line->next = l->next;

	if (l->next != NULL)
		l->next->prev = new_line;

	new_line->prev = l;
	l->next = new_line;
}
