#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "test.h"
#include "line.c"

void compare_lines(LineT* a, LineT* b) {
	char* line_str_a = line_to_str(a);
}

int test_line_to_str() {
	LineItemT* line_item_1 = line_item_new('a');
	LineItemT* line_item_2 = line_item_new('b');
	LineItemT* line_item_3 = line_item_new('c');

	line_item_add_next(line_item_1, line_item_2);
	line_item_add_next(line_item_2, line_item_3);

	LineT* line = line_new(line_item_1);

	char* line_str = line_to_str(line);

	int result = 0;
	if (strcmp("abc", line_str)) {
		printf("FAIL: test_line_to_str, expected 'abc', got: %s\n", line_str);
		result = 1;
	}

	free(line_str);
	line_free(line);

	return result;
}

int test_line_from_str() {
	LineT* line = line_new_from_str("abc");
	char* line_str = line_to_str(line);

	int result = 0;
	if (strcmp("abc", line_str)) {
		printf("FAIL: test_line_from_str, expected 'abc', got: %s\n", line_str);
		result = 1;
	}

	line_free(line);
	free(line_str);

	return result;
}

int test_line_copy() {
	LineT* line_a = line_new_from_str("abc");
	LineT* line_b = line_copy(line_a);
	char* line_str = line_to_str(line_a);

	int result = 0;

	if (line_symbols_count(line_a) != line_symbols_count(line_b)) {
		printf("FAIL: test_line_copy, expected symbols count: %d, but got: %d\n",
			line_symbols_count(line_a),
			line_symbols_count(line_b));

		line_free(line_a);
		free(line_str);

		return 1;
	}

	LineItemT* l_a_item = line_a->item_head;
	LineItemT* l_b_item = line_b->item_head;
	int position = 0;

	while (l_a_item != NULL) {
		if (l_a_item->symbol != l_b_item->symbol) {
			printf("FAIL: test_line_copy, expected symbol '%c' at position %d, but got '%c'\n",
				l_a_item->symbol,
				position,
				l_b_item->symbol);

			line_free(line_a);
			line_free(line_b);
			free(line_str);

			return 1;
		}

		if (l_a_item == l_b_item) {
			printf("FAIL: test_line_copy, equal pointers in line at position %d\n",
				position);

			line_free(line_a);
			line_free(line_b);
			free(line_str);

			return 1;
		}

		l_a_item = l_a_item->next;
		l_b_item = l_b_item->next;
		position++;
	}

	line_free(line_a);
	line_free(line_b);
	free(line_str);

	return result;
}

int test_line_copy_lines_from() {
	LineT* line_a = line_new_from_str("abc");
	LineT* line_a_2 = line_new_from_str("def");
	LineT* line_a_3 = line_new_from_str("ghi");

	line_add_next(line_a, line_a_2);
	line_add_next(line_a_2, line_a_3);

	LineT* line_b = line_copy_lines_from(line_a);

	if (line_count_from(line_a) != line_count_from(line_b)) {
		printf("FAIL: test_line_copy_lines_from, expected lines count: %d, but got: %d\n",
			line_count_from(line_a),
			line_count_from(line_b));

		line_free(line_a);
		line_free(line_b);

		return 1;
	}

	int line = 0;

	while (line_a != NULL) {
		int position = 0;

		if (line_a == line_b) {
			printf("FAIL: test_line_copy_lines_from, equal pointers in line %d at position %d\n",
				line,
				position);

			line_free(line_a);
			line_free(line_b);

			return 1;
		}

		char* line_str = line_to_str(line_a);

		if (line_symbols_count(line_a) != line_symbols_count(line_b)) {
			printf("FAIL: test_line_copy_lines_from, line %d expected symbols count: %d, but got: %d\n",
				line,
				line_symbols_count(line_a),
				line_symbols_count(line_b));

			line_free(line_a);
			line_free(line_b);
			free(line_str);

			return 1;
		}

		LineItemT* l_a_item = line_a->item_head;
		LineItemT* l_b_item = line_b->item_head;

		while (l_a_item != NULL) {
			if (l_a_item->symbol != l_b_item->symbol) {
				printf("FAIL: test_line_copy_lines_from, line %d expected symbol '%c' at position %d, but got '%c'\n",
					line,
					l_a_item->symbol,
					position,
					l_b_item->symbol);

				line_free(line_a);
				line_free(line_b);
				free(line_str);

				return 1;
			}

			if (l_a_item == l_b_item) {
				printf("FAIL: test_line_copy_lines_from, line %d equal pointers in line at position %d\n",
					line,
					position);

				line_free(line_a);
				line_free(line_b);
				free(line_str);

				return 1;
			}

			l_a_item = l_a_item->next;
			l_b_item = l_b_item->next;
			position++;
		}

		free(line_str);

		line_a = line_a->next;
		line_b = line_b->next;
		line++;
	}

	line_free(line_a);
	line_free(line_b);

	return 0;
}

int main() {
	bool test_failed = run_tests(4,
		test_line_to_str,
		test_line_from_str,
		test_line_copy,
		test_line_copy_lines_from
	);

	if (test_failed)
		return 1;

	return 0;
}
