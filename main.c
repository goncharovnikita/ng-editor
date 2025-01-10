#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <sys/ioctl.h>

#include "view.h"
#include "calc.h"

#define MAX_GRID_SIZE 1024
#define MAX_COMMANDS_BUFFER_SIZE 2
#define MAX_COMMAND_SIZE 4096
#define MAX_MESSAGE_SIZE 1024
#define STATUS_COLUMN_WIDTH 5
#define INFO_LINE_HEIGHT 1
#define COMMAND_LINE_HEIGHT 1

static struct termios old_termios, new_termios;

typedef enum {
	MODE_NORMAL,
	MODE_COMMAND,
	MODE_INSERT,
} ModeType;

typedef enum {
	CLEAR,
	CURSOR,
	INFO_LINE,
	HIGHLIGHT,
	WHITE,
} Color;

typedef enum {
	EC_MOVE_CURSOR,
	EC_NORMALIZE_CURSOR,
	EC_SCROLL,
	EC_INSERT,
	EC_SWITCH_WINDOW,
} EditorCommandType;

typedef enum {
	UC_h,
	UC_j,
	UC_k,
	UC_l,
	UC_caret,
	UC_dollar,
	UC_w,
	UC_e,
	UC_b,
	UC_H,
	UC_M,
	UC_L,
	UC_gg,
	UC_G,
	UC_CTRL_d,
	UC_CTRL_u,
	UC_esc,
	UC_colon,
	UC_i,
	UC_I,
	UC_a,
	UC_A,
	UC_insert_symbol,
	UC_CTRL_w_l,
	UC_CTRL_w_h,
	UC_CTRL_w_j,
	UC_CTRL_w_k,
} UserCommandType;

typedef struct {
	char symbol;
	Color color;
} Cell;

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

typedef Cell Grid[MAX_GRID_SIZE][MAX_GRID_SIZE];

typedef struct {
	int scroll;
} EditorConfig;

typedef struct {
	char symbol;
	bool append;
} UserCommandDataSymbol;

typedef struct {
	UserCommandType type;
	int count;
	char* data;
} UserCommand;

typedef struct {
	int count;
	char command[MAX_COMMAND_SIZE];
} NormalModeCommand;

typedef struct {
	char command[MAX_COMMAND_SIZE];
} CommandModeCommand;

typedef struct {
	char command[MAX_COMMAND_SIZE];
	bool append;
} InsertModeCommand;

typedef struct {
	char message[MAX_MESSAGE_SIZE];
	NormalModeCommand* normal_mode_command;
} MessageLineData;

typedef enum {
	ED_CURSOR_UP,
	ED_CURSOR_FORWARD,
	ED_CURSOR_DOWN,
	ED_CURSOR_BACKWARD,
	ED_CURSOR_TO_START_OF_LINE,
	ED_CURSOR_TO_END_OF_LINE,
	ED_CURSOR_TOP,
	ED_CURSOR_MID,
	ED_CURSOR_BOTTOM,
	ED_CURSOR_TO_NEXT_WORD,
	ED_CURSOR_TO_END_OF_WORD,
	ED_CURSOR_TO_PREV_WORD,
	ED_CURSOR_TO_FIRST_LINE,
	ED_CURSOR_TO_LAST_LINE,
} EditorMoveCursorDirection;

typedef enum {
	ED_SWITCH_WINDOW_RIGHT,
	ED_SWITCH_WINDOW_LEFT,
	ED_SWITCH_WINDOW_UP,
	ED_SWITCH_WINDOW_DOWN,
} EditorSwitchWindowDirection;

typedef struct {
	EditorMoveCursorDirection direction;
	int count;
} EditorCommandMoveCursorData;

typedef struct {
	EditorSwitchWindowDirection direction;
	int count;
} EditorCommandSwitchWindowData;

typedef struct {
	char symbol;
	int count;
} EditorCommandInsertSymbolData;

typedef enum {
	ED_SCROLL_DOWN,
	ED_SCROLL_UP,
} EditorScrollDirection;

typedef struct {
	EditorScrollDirection direction;
	int scroll;
} EditorCommandScrollData;

typedef struct {
	EditorCommandType type;
	char data[256];
} EditorCommand;

typedef struct EditorBuffer {
	LineT* head_line;
	char* filename;
	struct EditorBuffer* next;
} EditorBufferT;

typedef struct {
	EditorBufferT* editor_buffer;
	LineItemT* cursor_line_item;
	LineT* cursor_line;
	ViewT* source_view;
	ViewT* status_column_view;
	ViewT* info_line_view;
	Pos cursor_pos;
	int x_offset;
	int y_offset;
} EditorWindow;

typedef struct EditorTabItem {
	int tabno;
	EditorWindow* window;

	struct EditorTabItem* right;
	struct EditorTabItem* left;
	struct EditorTabItem* down;
	struct EditorTabItem* up;
} EditorTabItemT;

typedef struct EditorTab {
	EditorTabItemT* tab_item_head;
	EditorTabItemT* tab_item_current;
	struct EditorTab* next;
	struct EditorTab* prev;
} EditorTabT;

typedef struct {
	int* rows;
	int* cols;
} DebugInformation;

bool exit_loop = false;

const char conf_non_word_symbols[] = {
	' ',
	'\n',
	'\t',
};

const char* conf_normal_mode_valid_commands[] = {
	"",
	"h", "j", "k", "l",
	"^", "$",
	"H", "M", "L",
	"w", "e", "b",
	"G", "gg",
	"\x04", // CTRL-d
	"\x15", // CTRL-u
	"\x1b", // CTRL-[
	":",
	"i", "a", "I", "A",
	"\x17\x6c", // CTRL-w-l
	"\x17\x68", // CTRL-w-h
	"\x17\x6a", // CTRL-w-j
	"\x17\x6b", // CTRL-w-k

	"-1",
};

const char* conf_command_mode_valid_commands[] = {
	"q", "quit",

	"-1",
};

Grid rendered_grid = {};
Grid current_grid = {};

EditorBufferT* buffers;
EditorTabT* current_editor_tab;
int64_t tabno_counter = 0;

ModeType mode_type;

UserCommand user_commands[MAX_COMMANDS_BUFFER_SIZE] = {};
EditorCommand editor_commands[MAX_COMMANDS_BUFFER_SIZE] = {};

EditorConfig editor_config = {.scroll = 1};

NormalModeCommand normal_mode_command = {.count = 0, .command = ""};
CommandModeCommand command_mode_command = {.command = ""};
InsertModeCommand insert_mode_command = {.command = ""};

MessageLineData message_line_data = {
	.message = "",
	.normal_mode_command = &normal_mode_command
};

void mode_set_type(ModeType mt) {
	mode_type = mt;
}

void editor_config_set_scroll(int scroll) {
	editor_config.scroll = scroll;
}

void message_set(char* message) {
	sprintf(message_line_data.message, "%s", message);
}

bool symbol_is_newline(char symbol) {
	return symbol == '\n';
}

bool symbol_is_enter(char symbol) {
	return symbol == 10;
}

bool symbol_is_backspace(char symbol) {
	return symbol == 127;
}

bool symbol_is_escape(char symbol) {
	return symbol == 27;
}

bool symbol_is_printable(char symbol) {
	return symbol >= 32 && symbol < 127;
}

char* symbol_to_printable(char symbol) {
	char* printable = (char*)malloc(64 * sizeof(char));

	if (symbol_is_printable(symbol)) {
		sprintf(printable, "%c", symbol);

		return printable;
	}

	switch (symbol) {
		case '\x17':
			sprintf(printable, "^C");
			break;

		default:
			sprintf(printable, "<non-printable>");
			break;
	}

	return printable;
}

char* string_to_printable(char* str) {
	char* printable = (char*)malloc(sizeof(char) * strlen(str) * strlen("<non-printable>") + 1);

	for (int i = 0; i < strlen(str); i++) {
		char* p = symbol_to_printable(str[i]);

		sprintf(printable, "%s%s", printable, p);

		free(p);
	}

	return printable;
}

EditorBufferT* editor_buffer_new() {
	EditorBufferT* buffer = (EditorBufferT*)malloc(sizeof(EditorBufferT));

	if (buffers == NULL) {
		buffers = buffer;
	} else {
		EditorBufferT* buffers_tail = buffers;

		while (buffers_tail->next != NULL)
			buffers_tail = buffers_tail->next;

		buffers_tail->next = buffer;
	}

	return buffer;
}

EditorBufferT* editor_buffer_find_by_filename(char* filename) {
	if (buffers == NULL)
		return NULL;

	EditorBufferT* editor_buffer = buffers;

	while (strcmp(filename, editor_buffer->filename)) {
		editor_buffer = editor_buffer->next;
	}

	return editor_buffer;
}

EditorWindow* editor_window_new() {
	return (EditorWindow*)malloc(sizeof(EditorWindow));
}

EditorTabItemT* editor_tab_item_new() {
	return (EditorTabItemT*)malloc(sizeof(EditorTabItemT));
}

EditorTabT* editor_tab_new() {
	return (EditorTabT*)malloc(sizeof(EditorTabT));
}

LineItemT* line_item_new() {
	return (LineItemT*)malloc(sizeof(LineItemT));
}

bool line_item_is_newline(LineItemT* line_item) {
	return symbol_is_newline(line_item->symbol);
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
		return ;

	line_head->next = line_head->next->next;
	line_head->next->prev = line_head;

	free(tmp);
}

LineT* line_new() {
	return (LineT*)malloc(sizeof(LineT));
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

void line_new_before(LineT** line) {
	LineT* l = *line;

	LineT* new_line = line_new();
	LineItemT* new_line_item = line_item_new();

	new_line_item->symbol = '\n';
	new_line->item_head = new_line_item;
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

	LineItemT* line_to_free_item = line_to_free->item_head;

	while (line_to_free_item != NULL) {
		LineItemT* tmp = line_to_free_item;
		line_to_free_item = line_to_free_item->next;

		free(tmp);
	}

	free(line_to_free);
}

void line_delete_before(LineT* line) {
	if (line->prev == NULL)
		return;

	LineT* line_to_free = line->prev;

	line->prev = line->prev->prev;
	if (line->prev != NULL)
		line->prev->next = line;

	LineItemT* line_to_free_item = line_to_free->item_head;

	while (line_to_free_item != NULL) {
		LineItemT* tmp = line_to_free_item;
		line_to_free_item = line_to_free_item->prev;

		free(tmp);
	}

	free(line_to_free);
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

	LineT* new_line = line_new();
	LineItemT* new_line_item = line_item_new();

	new_line_item->symbol = '\n';
	new_line->item_head = new_line_item;
	new_line->next = l->next;

	if (l->next != NULL)
		l->next->prev = new_line;

	new_line->prev = l;
	l->next = new_line;
}

bool pos_is_equal(Pos a, Pos b) {
	return a.x == b.x && a.y == b.y;
}

void pos_copy(Pos* to, Pos* from) {
	to->x = from->x;
	to->y = from->y;
}

void s_configure_terminal() {
	tcgetattr(STDIN_FILENO, &old_termios);
	new_termios = old_termios;

	new_termios.c_lflag &= ~(ICANON | ECHO);
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;

	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	printf("\e[?25l");
}

void s_restore_terminal() {
	printf("\e[?25h");
	printf("\e[m");
	fflush(stdout);

	tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

void s_exit_editor() {
	exit_loop = true;
}

void r_move_cursor(int x, int y) {
	fprintf(stdout, "\e[%d;%dH", y + 1, x + 1);
}

void r_clear_screen() {
	fprintf(stdout, "\e[1;1H\e[2J");
}

void r_set_color(Color color) {
	switch (color) {
		case CURSOR:
			fprintf(stdout, "\033[90;107m");
			break;

		case INFO_LINE:
			fprintf(stdout, "\033[30;47m");
			break;

		case HIGHLIGHT:
			fprintf(stdout, "\033[48;5;240m");
			break;

		case WHITE:
			fprintf(stdout, "\033[0m");
			break;

		case CLEAR:
			fprintf(stdout, "\033[0m");
			break;
	}
}

void r_draw_line(int x, int y, int max_len, char* text, Color color) {
	int text_len = strlen(text);

	for (int i = 0; i < max_len; i++) {
		char symbol = ' ';
		if (i < text_len)
			symbol = text[i];

		current_grid[y][x + i].symbol = symbol;
		current_grid[y][x + i].color = color;
	}
}

int b_read_input(void *buf) {
	return read(STDIN_FILENO, buf, sizeof(buf));
}

void render(int rows, int cols) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			Cell old_cell = rendered_grid[y][x];
			Cell new_cell = current_grid[y][x];

			if (old_cell.symbol != new_cell.symbol || old_cell.color != new_cell.color) {
				r_move_cursor(x, y);

				r_set_color(new_cell.color);
				printf("%c", new_cell.symbol);
				r_set_color(CLEAR);
			}
		}
	}

	fflush(stdout);
}

void switch_grids() {
	memcpy(
		rendered_grid,
		current_grid,
		sizeof(current_grid)
	);
}

LineT* read_and_parse_source_file(char *file_name) {
	int y = 0;
	char line[256];
	FILE *source_file = fopen(file_name, "r");

	LineT* head_line = line_new();
	LineT* current_line = head_line;
	LineT* prev_line = NULL;

	LineItemT* head_line_item = line_item_new();
	LineItemT* current_line_item = head_line_item;
	LineItemT* prev_line_item = NULL;

	head_line->item_head = head_line_item;

	while (fgets(line, 100, source_file)) {
		for (int i = 0; i < strlen(line); i++) {
			assert(current_line != NULL);
			assert(current_line_item != NULL);

			current_line_item->symbol = line[i];

			if (prev_line_item != NULL) {
				prev_line_item->next = current_line_item;
				current_line_item->prev = prev_line_item;
			}

			prev_line_item = current_line_item;

			current_line_item = line_item_new();

			if (symbol_is_newline(line[i])) {
				if (prev_line != NULL) {
					prev_line->next = current_line;
					current_line->prev = prev_line;
				}

				current_line_item = line_item_new();

				prev_line = current_line;
				prev_line_item = NULL;

				current_line = line_new();
				current_line->item_head = current_line_item;

				continue;
			}
		}
	}

	fclose(source_file);

	return head_line;
}

void draw_editor_window_source(EditorWindow* window) {
	ViewT* view = window->source_view;
	int y_offset = window->y_offset;
	LineT* cursor_line = window->cursor_line;

	int view_cols_count = view_cols(view);
	int view_rows_count = view_rows(view);

	int source_file_skip_lines = y_offset;

	LineT* source_file_line = window->editor_buffer->head_line;

	while (source_file_skip_lines > 0) {
		assert(source_file_line != NULL);

		source_file_line = source_file_line->next;
		source_file_skip_lines--;
	}

	for (int y = 0; y < view_rows_count; y++) {
		bool line_ended = false;

		LineItemT* source_file_line_item;

		if (source_file_line != NULL) {
			 source_file_line_item = source_file_line->item_head;
		}

		for (int x = 0; x < view_cols_count; x++) {
			if (source_file_line_item == NULL)
				line_ended = true;

			if (line_ended) {
				Cell cell = {.symbol = ' ', .color = CLEAR};

				memcpy(&current_grid[view_y(view, y)][view_x(view, x)], &cell, sizeof(Cell));

				continue;
			}

			if (source_file_line_item->symbol == '\t') {
				for (int j = 0; j < 4 && x + j < view_cols_count; j++) {
					Cell cell = {.symbol = ' ', .color = CLEAR};

					if (j == 0)
						cell.symbol = '>';

					memcpy(&current_grid[view_y(view, y)][view_x(view, x + j)], &cell, sizeof(Cell));
				}

				x += 3;

				source_file_line_item = source_file_line_item->next;
			} else {
				Cell cell = {.symbol = source_file_line_item->symbol, .color = CLEAR};

				if (line_item_is_newline(source_file_line_item))
					cell.symbol = '<';

				memcpy(&current_grid[view_y(view, y)][view_x(view, x)], &cell, sizeof(Cell));

				source_file_line_item = source_file_line_item->next;
			}
		}

		if (source_file_line != NULL)
			source_file_line = source_file_line->next;
	}
}

void draw_editor_window_status_column(EditorWindow* window) {
	ViewT* view = window->status_column_view;
	int y_offset = window->y_offset;
	int total_rows = line_count_from(window->editor_buffer->head_line);
	int view_rows_count = view_rows(view);
	int view_cols_count = view_cols(view);

	for (int y = 0; y < view_rows_count; y++) {
		char column_text[256] = {0};

		if (y + y_offset + 1 <= total_rows) {
			sprintf(column_text, "%d", y + y_offset + 1);
		} else {
			sprintf(column_text, "");
		}

		Color color = WHITE;

		if (y == window->cursor_pos.y)
			color = HIGHLIGHT;

		r_draw_line(view_x(view, 0), view_y(view, y), view_cols_count, column_text, color);
	}
}

void draw_editor_window_info_line(EditorWindow* window) {
	ViewT* view = window->info_line_view;
	char* filename = window->editor_buffer->filename;
	int line = window->cursor_pos.y + window->y_offset;
	int column = window->cursor_pos.x + window->x_offset;
	int view_cols_count = view_cols(view);
	int view_rows_count = view_rows(view);

	for (int y = 0; y < view_rows_count; y++) {
		for (int x = 0; x < view_cols_count; x++) {
			current_grid[view_y(view, y)][view_x(view, x)].symbol = ' ';
			current_grid[view_y(view, y)][view_x(view, x)].color = INFO_LINE;
		}
	}

	for (int x = 0; x < strlen(filename) && x < view_cols_count; x++) {
		current_grid[view_y(view, 0)][view_x(view, x)].symbol = filename[x];
	}

	char line_and_column_text[256];

	sprintf(line_and_column_text, "%d,%d", line, column);

	int line_and_column_text_len = strlen(line_and_column_text);

	for (int x = 0; x < strlen(line_and_column_text); x++) {
		current_grid[view_y(view, 0)][view_x(view, view_cols_count - x - 2)].symbol = line_and_column_text[line_and_column_text_len - 1 - x];
	}
}

void draw_cursor(EditorWindow* editor_window) {
	ViewT* view = editor_window->source_view;
	int x = editor_window->cursor_pos.x;
	int y = editor_window->cursor_pos.y;

	current_grid[view_y(view, y)][view_x(view, x)].color = CURSOR;
}

void highlight_line(EditorWindow* editor_window) {
	int y = editor_window->cursor_pos.y;
	ViewT* view = editor_window->source_view;
	int view_cols_count = view_cols(view);

	for (int x = 0; x < view_cols_count; x++) {
		current_grid[view_y(view, y)][view_x(view, x)].color = HIGHLIGHT;
	}
}

void draw_editor_window(EditorWindow* window) {
	draw_editor_window_source(window);
	draw_editor_window_status_column(window);
	draw_editor_window_info_line(window);
}

void draw_editor_tab_item(EditorTabT* editor_tab, EditorTabItemT* editor_tab_item) {
	if (editor_tab_item == NULL)
		return;

	draw_editor_window(editor_tab_item->window);

	if (editor_tab_item->tabno == editor_tab->tab_item_current->tabno) {
		highlight_line(editor_tab_item->window);
		draw_cursor(editor_tab_item->window);
	}

	draw_editor_tab_item(editor_tab, editor_tab_item->right);
	draw_editor_tab_item(editor_tab, editor_tab_item->down);
}

void draw_editor_tab(EditorTabT* editor_tab) {
	draw_editor_tab_item(editor_tab, editor_tab->tab_item_head);
}

void draw_command_line(ViewT* view) {
	int cols = view_cols(view);
	int y = view_y(view, 0);

	for (int x = view->origin.x; x < view->end.x; x++) {
		current_grid[y][x].symbol = ' ';
		current_grid[y][x].color = CLEAR;
	}

	if (mode_type == MODE_NORMAL) {
		for (int i = 0; i < strlen(message_line_data.message); i++) {
			current_grid[y][i].symbol = message_line_data.message[i];
		}

		char user_modifier_text[256] = {0};

		if (normal_mode_command.count > 0)
			sprintf(user_modifier_text, "%d", normal_mode_command.count);

		if (strlen(normal_mode_command.command) > 0) {
			char* printable_cmd = string_to_printable(normal_mode_command.command);
			sprintf(user_modifier_text, "%s%s", user_modifier_text, printable_cmd);

			free(printable_cmd);
		}

		if (strlen(user_modifier_text) == 0)
			sprintf(user_modifier_text, " ");

		int line_and_column_text_len = strlen(user_modifier_text);

		for (int i = 0; i < strlen(user_modifier_text); i++) {
			current_grid[y][cols - i - 1].symbol = user_modifier_text[line_and_column_text_len - 1 - i];
		}
	} else if (mode_type == MODE_COMMAND) {
		char command_text[MAX_COMMAND_SIZE] = {0};
		sprintf(command_text, ":%s", command_mode_command.command);

		for (int i = 0; i < strlen(command_text); i++) {
			current_grid[y][i].symbol = command_text[i];
		}
	} else if (mode_type == MODE_INSERT) {
		char* mode_name = "-- INSERT --";

		for (int i = 0; i < strlen(mode_name); i++) {
			current_grid[y][i].symbol = mode_name[i];
		}
	}
}

void draw_debug_information(ViewT* view, DebugInformation debug_info) {
	EditorTabItemT* editor_tab_item = current_editor_tab->tab_item_current;
	EditorWindow* editor_window = editor_tab_item->window;
	int view_cols_count = view_cols(view);
	int x_offset = editor_window->x_offset;
	int y_offset = editor_window->y_offset;
	Pos cursor_pos = editor_window->cursor_pos;
	LineT* cursor_line = editor_window->cursor_line;
	LineItemT* cursor_head = editor_window->cursor_line_item;
	int total_rows = line_count_from(editor_window->editor_buffer->head_line);

	for (int y = view->origin.y; y < view->end.y; y++) {
		int lineno = y - view->origin.y;

		char text[4096];

		if (lineno == 0) {
			sprintf(text, "rows: %d, cols: %d, total_rows: %d", *debug_info.rows, *debug_info.cols, total_rows);
			r_draw_line(view->origin.x, y, view_cols_count, text, WHITE);
		}

		if (lineno == 1) {
			sprintf(text, "x_offset: %d, y_offset: %d", x_offset, y_offset);
			r_draw_line(view->origin.x, y, view_cols_count, text, WHITE);
		}

		if (lineno == 2) {
			sprintf(text, "cursor_pos_x: %d, cursor_pos_y: %d", cursor_pos.x, cursor_pos.y);
			r_draw_line(view->origin.x, y, view_cols_count, text, WHITE);
		}

		if (lineno == 3) {
			LineItemT* ch = cursor_head;
			char symbol[256] = {ch->symbol};
			if (symbol_is_newline(symbol[0]))
				strcpy(symbol, "<newline>");
			if (symbol[0] == '\t')
				strcpy(symbol, "<tab>");
			sprintf(text, "cursor source item (symbol: %s, addr: %p)", symbol, ch);
			r_draw_line(view->origin.x, y, view_cols_count, text, WHITE);
		}

		if (lineno == 4) {
			LineT* line = cursor_line;
			LineItemT* ch = line->item_head;

			char line_text[256] = {};

			int i = 0;
			while (ch != NULL) {
				line_text[i] = ch->symbol;
				ch = ch->next;
				i++;
			}

			sprintf(text, "current line %s", line_text);
			r_draw_line(view_x(view, 0), y, view_cols_count, text, WHITE);
		}
	}
}

void cursor_horisontal_set(Pos* cursor_pos, int x) {
	cursor_pos->x = x;
}

void cursor_vertical_set(Pos* cursor_pos, int y) {
	cursor_pos->y = y;
}

void cursor_forward(Pos* cursor_pos, int shift) {
	cursor_pos->x += shift;
}

void cursor_backward(Pos* cursor_pos, int shift) {
	cursor_pos->x -= shift;
}

void cursor_up(Pos* cursor_pos, int shift) {
	cursor_pos->y -= shift;
}

void cursor_down(Pos* cursor_pos, int shift) {
	cursor_pos->y += shift;
}

bool nav_is_word_symbol(char sym) {
	for (int i = 0; i < strlen(conf_non_word_symbols); i++) {
		if (sym == conf_non_word_symbols[i])
			return false;
	}

	return true;
}

int nav_move_count_by_source_symbol(char source_symbol) {
	if (source_symbol == '\t')
		return 4;

	return 1;
}

int nav_oneline_count(int (*cmd)(LineItemT**, Pos*), LineItemT** line_item, Pos* cursor_pos, int count) {
	int move_distance = 0;

	for (int i = 0; i < count; i++) {
		move_distance += cmd(line_item, cursor_pos);
	}

	return move_distance;
}

int nav_multiline_count(int (*cmd)(LineT**, LineItemT**, Pos*), LineT** line, LineItemT** line_item, Pos* cursor_pos, int count) {
	int move_distance = 0;

	for (int i = 0; i < count; i++) {
		move_distance += cmd(line, line_item, cursor_pos);
	}

	return move_distance;
}

int nav_oneline_distance(int (*cmd)(LineItemT** cursor_node, Pos* cursor_pos), LineItemT** cursor_node, Pos* cursor_pos, int count) {
	int move_distance = 0;

	while (move_distance < count) {
		int diff = cmd(cursor_node, cursor_pos);
		if (!diff)
			break;

		move_distance += diff;
	}

	return move_distance;
}

int nav_multiline_distance(int (*cmd)(LineT**, LineItemT**, Pos*), LineT** line, LineItemT** line_item, Pos* cursor_pos, int count) {
	int move_distance = 0;

	while (move_distance < count) {
		int diff = cmd(line, line_item, cursor_pos);
		if (!diff)
			break;

		move_distance += diff;
	}

	return move_distance;
}

int nav_forward(LineItemT** cursor_node, Pos* cursor_pos) {
	LineItemT* cursor_head = *cursor_node;

	if (cursor_head->next == NULL)
		return 0;

	int shift = nav_move_count_by_source_symbol(cursor_head->symbol);
	cursor_forward(cursor_pos, shift);
	cursor_head = cursor_head->next;

	*cursor_node = cursor_head;

	return shift;
}

int nav_backward(LineItemT** cursor_node, Pos* cursor_pos) {
	LineItemT* cursor_head = *cursor_node;

	if (cursor_head->prev == NULL)
		return 0;

	int shift = nav_move_count_by_source_symbol(cursor_head->prev->symbol);
	cursor_backward(cursor_pos, shift);
	cursor_head = cursor_head->prev;

	*cursor_node = cursor_head;

	return shift;
}

int nav_to_next_line(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos) {
	LineT* new_cursor_line = *cursor_line;
	LineItemT* new_cursor_line_item = *cursor_line_item;

	if (new_cursor_line->next == NULL)
		return 0;

	new_cursor_line = new_cursor_line->next;
	new_cursor_line_item = new_cursor_line->item_head;

	cursor_down(cursor_pos, 1);
	cursor_horisontal_set(cursor_pos, 0);

	*cursor_line = new_cursor_line;
	*cursor_line_item = new_cursor_line_item;

	return 1;
}

int nav_to_prev_line(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos) {
	LineT* new_cursor_line = *cursor_line;
	LineItemT* new_cursor_line_item = *cursor_line_item;

	if (new_cursor_line->prev == NULL)
		return 0;

	new_cursor_line = new_cursor_line->prev;
	new_cursor_line_item = new_cursor_line->item_head;

	cursor_up(cursor_pos, 1);
	cursor_horisontal_set(cursor_pos, 0);

	*cursor_line = new_cursor_line;
	*cursor_line_item = new_cursor_line_item;

	return 1;
}

int nav_to_end_of_line(LineItemT** cursor_line_item, Pos* cursor_pos) {
	int move_distance = 0;

	while (true) {
		int diff = nav_forward(cursor_line_item, cursor_pos);
		if (!diff)
			break;

		move_distance += diff;
	}

	return move_distance;
}

int nav_to_start_of_line(LineItemT** cursor_line_item, Pos* cursor_pos) {
	return nav_oneline_distance(nav_backward, cursor_line_item, cursor_pos, cursor_pos->x);
}

bool nav_forward_or_next_line(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos) {
	if (!nav_forward(cursor_line_item, cursor_pos))
		return nav_to_next_line(cursor_line, cursor_line_item, cursor_pos);
	else
		return true;
}

bool nav_backward_or_prev_line(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos) {
	if (!nav_backward(cursor_line_item, cursor_pos)) {
		if (!nav_to_prev_line(cursor_line, cursor_line_item, cursor_pos))
			return false;

		nav_to_end_of_line(cursor_line_item, cursor_pos);
	}

	return true;
}

int nav_up(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos, int count) {
	int original_x = cursor_pos->x;
	int move_distance = nav_multiline_distance(nav_to_prev_line, cursor_line, cursor_line_item, cursor_pos, count);

	if (!move_distance)
		return 0;

	nav_oneline_distance(nav_forward, cursor_line_item, cursor_pos, original_x);

	return move_distance;
}

int nav_down(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos, int count) {
	int original_x = cursor_pos->x;
	int move_distance = nav_multiline_distance(nav_to_next_line, cursor_line, cursor_line_item, cursor_pos, count);

	if (!move_distance)
		return 0;

	nav_oneline_distance(nav_forward, cursor_line_item, cursor_pos, original_x);

	return move_distance;
}

bool nav_vertical(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos, int lines_offset) {
	if (lines_offset > 0) {
		return nav_down(cursor_line, cursor_line_item, cursor_pos, lines_offset);
	}

	return nav_up(cursor_line, cursor_line_item, cursor_pos, lines_offset * -1);
}

bool nav_to_next_word(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos) {
	LineItemT* new_cursor_line_item = *cursor_line_item;

	while (!nav_is_word_symbol(new_cursor_line_item->symbol)) {
		if (!nav_forward_or_next_line(cursor_line, &new_cursor_line_item, cursor_pos)) {
			return false;
		}
	}

	*cursor_line_item = new_cursor_line_item;

	return true;
}

bool nav_to_prev_word(LineT** cursor_line, LineItemT** cursor_line_item, Pos* cursor_pos) {
	LineItemT* new_cursor_line_item = *cursor_line_item;

	while (!nav_is_word_symbol(new_cursor_line_item->symbol)) {
		if (!nav_backward_or_prev_line(cursor_line, &new_cursor_line_item, cursor_pos)) {
			return false;
		}
	}

	*cursor_line_item = new_cursor_line_item;

	return true;
}

int nav_to_end_of_word(LineItemT** cursor_line_item, Pos* cursor_pos) {
	LineItemT* new_cursor_line_item = *cursor_line_item;

	int move_distance = 0;

	while (nav_is_word_symbol(new_cursor_line_item->symbol) &&
			new_cursor_line_item->next != NULL &&
			nav_is_word_symbol(new_cursor_line_item->next->symbol)) {
		int diff = nav_forward(&new_cursor_line_item, cursor_pos);
		if (!diff)
			break;

		move_distance += diff;
	}

	*cursor_line_item = new_cursor_line_item;

	return move_distance;
}

int nav_to_start_of_word(LineItemT** cursor_line_item, Pos* cursor_pos) {
	LineItemT* new_cursor_line_item = *cursor_line_item;

	int move_distance = 0;

	while (nav_is_word_symbol(new_cursor_line_item->symbol) &&
			new_cursor_line_item->prev != NULL &&
			nav_is_word_symbol(new_cursor_line_item->prev->symbol)) {
		int diff = nav_backward(&new_cursor_line_item, cursor_pos);
		if (!diff)
			break;

		move_distance += diff;
	}

	*cursor_line_item = new_cursor_line_item;

	return move_distance;
}

void offset_up(int* y_offset, Pos* cursor_pos, int rows, int count) {
	int target_y_offset = MAX(0, *y_offset - count);

	int y_offset_diff = *y_offset - target_y_offset;

	cursor_pos->y = cursor_pos->y + y_offset_diff;
	*y_offset -= y_offset_diff;
}

void offset_down(int* y_offset, Pos* cursor_pos, int rows, int total_rows, int count) {
	int target_y_offset = MIN(total_rows - rows, *y_offset + count);

	int y_offset_diff = target_y_offset - *y_offset;

	cursor_pos->y = cursor_pos->y - y_offset_diff;
	*y_offset += y_offset_diff;
}

void offset_sync_with_cursor(int* y_offset, Pos* cursor_pos, int rows, int total_rows) {
	if (cursor_pos->y < 0) {
		offset_up(y_offset, cursor_pos, rows, cursor_pos->y * -1);
	} else if (cursor_pos->y >= rows) {
		offset_down(y_offset, cursor_pos, rows, total_rows, cursor_pos->y - rows + 1);
	}
}

int insert_insert_symbol(LineT* current_line, LineItemT* current_line_item, char symbol) {
	int shift = nav_move_count_by_source_symbol(symbol);

	LineItemT* new_line_item = (LineItemT*)malloc(sizeof(LineItemT));
	new_line_item->symbol = symbol;

	if (current_line_item->prev == NULL)
		current_line->item_head = new_line_item;

	new_line_item->next = current_line_item;
	new_line_item->prev = current_line_item->prev;

	if (current_line_item->prev != NULL)
		current_line_item->prev->next = new_line_item;

	current_line_item->prev = new_line_item;

	return shift;
}

int insert_delete_symbol(LineT** line, LineItemT** line_item) {
	LineT* current_line = *line;
	LineItemT* current_line_item = *line_item;

	if (current_line_item->prev == NULL)
		return 0;

	int shift = nav_move_count_by_source_symbol(current_line_item->prev->symbol);

	if (current_line_item->prev->prev == NULL) {
		free(current_line_item->prev);
		current_line_item->prev = NULL;
		current_line->item_head = current_line_item;

		return shift;
	}

	LineItemT* item_to_free = current_line_item->prev;

	current_line_item->prev = current_line_item->prev->prev;
	current_line_item->prev->next = current_line_item;

	free(item_to_free);

	return shift;
}

void normal_mode_command_clear() {
	normal_mode_command.count = 0;

	sprintf(normal_mode_command.command, "");
}

void normal_mode_command_add_count(int count) {
	normal_mode_command.count = normal_mode_command.count * 10 + count;
}

bool normal_mode_command_is_valid_partial() {
	int i = 0;

	while (strcmp("-1", conf_normal_mode_valid_commands[i])) {
		if (!strncmp(normal_mode_command.command, conf_normal_mode_valid_commands[i], strlen(normal_mode_command.command)))
			return true;

		i++;
	}

	return false;
}

bool normal_mode_command_is_valid_full() {
	int i = 0;

	while (strcmp("-1", conf_normal_mode_valid_commands[i])) {
		if (!strcmp(normal_mode_command.command, conf_normal_mode_valid_commands[i]))
			return true;

		i++;
	}

	return false;
}

void normal_mode_command_add_char(char cmd_char) {
	sprintf(normal_mode_command.command, "%s%c", normal_mode_command.command, cmd_char);
}

void command_mode_command_clear() {
	sprintf(command_mode_command.command, "");
}

void command_mode_add_char(char cmd_char) {
	sprintf(command_mode_command.command, "%s%c", command_mode_command.command, cmd_char);
}

void command_mode_strip_tail() {
	if (strlen(command_mode_command.command) == 0)
		return;

	snprintf(command_mode_command.command, strlen(command_mode_command.command), "%s", command_mode_command.command);
}

bool command_mode_command_is_valid() {
	int i = 0;

	while (strcmp("-1", conf_command_mode_valid_commands[i])) {
		if (!strcmp(conf_command_mode_valid_commands[i], command_mode_command.command))
			return true;

		i++;
	}

	return false;
}

void insert_mode_command_add_char(char cmd_char) {
	sprintf(insert_mode_command.command, "%s%c", insert_mode_command.command, cmd_char);
}

void insert_mode_command_set_append(bool append) {
	insert_mode_command.append = append;
}

void insert_mode_command_clear() {
	sprintf(insert_mode_command.command, "");
}

bool add_user_command(
		int *read_index, int *write_index,
		UserCommandType type, int count,
		char* data
) {
	if (*write_index >= MAX_COMMANDS_BUFFER_SIZE) {
		*write_index = 0;
		*read_index = 0;
	}

	user_commands[*write_index].type = type;
	user_commands[*write_index].count = count;
	user_commands[*write_index].data = data;

	*write_index = *write_index + 1;

	normal_mode_command_clear();

	return true;
}

bool add_user_command_with_no_data(
	int *read_index, int *write_index,
	UserCommandType type, int count
) {
	return add_user_command(read_index, write_index, type, count, NULL);
}

bool add_user_command_with_symbol(
	int *read_index, int *write_index,
	UserCommandType type, int count,
	char symbol, bool append
) {
	UserCommandDataSymbol* data = (UserCommandDataSymbol*)malloc(sizeof(UserCommandDataSymbol));
	data->symbol = symbol;
	data->append = append;

	return add_user_command(read_index, write_index, type, count, (char*) data);
}

bool handle_normal_mode_command(int* read_index, int* write_index) {
	if (!strcmp(":", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_colon, normal_mode_command.count);

	if (!strcmp("i", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_i, normal_mode_command.count);

	if (!strcmp("I", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_I, normal_mode_command.count);

	if (!strcmp("a", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_a, normal_mode_command.count);

	if (!strcmp("A", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_A, normal_mode_command.count);

	if (!strcmp("h", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_h, normal_mode_command.count);

	if (!strcmp("j", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_j, normal_mode_command.count);

	if (!strcmp("k", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_k, normal_mode_command.count);

	if (!strcmp("l", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_l, normal_mode_command.count);

	if (!strcmp("^", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_caret, normal_mode_command.count);

	if (!strcmp("$", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_dollar, normal_mode_command.count);

	if (!strcmp("w", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_w, normal_mode_command.count);

	if (!strcmp("e", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_e, normal_mode_command.count);

	if (!strcmp("b", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_b, normal_mode_command.count);

	if (!strcmp("H", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_H, normal_mode_command.count);

	if (!strcmp("M", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_M, normal_mode_command.count);

	if (!strcmp("L", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_L, normal_mode_command.count);

	if (!strcmp("gg", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_gg, normal_mode_command.count);

	if (!strcmp("G", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_G, normal_mode_command.count);

	if (!strcmp("\x04", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_CTRL_d, normal_mode_command.count);

	if (!strcmp("\x15", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_CTRL_u, normal_mode_command.count);

	if (!strcmp("\x1b", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_esc, normal_mode_command.count);

	if (!strcmp("\x17\x6c", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_CTRL_w_l, normal_mode_command.count);

	if (!strcmp("\x17\x68", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_CTRL_w_h, normal_mode_command.count);

	if (!strcmp("\x17\x6a", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_CTRL_w_j, normal_mode_command.count);

	if (!strcmp("\x17\x6b", normal_mode_command.command))
		return add_user_command_with_no_data(read_index, write_index, UC_CTRL_w_k, normal_mode_command.count);

	return false;
}

void handle_command_mode_command() {
	if (!strcmp("q", command_mode_command.command) || !strcmp("quit", command_mode_command.command))
		return s_exit_editor();
}

void handle_insert_mode_command(int* read_index, int* write_index) {
	add_user_command_with_symbol(
		read_index, write_index,
		UC_insert_symbol, 1,
		insert_mode_command.command[0],
		insert_mode_command.append
	);
}

bool handle_user_input(char *input_buf, int *buffer_read_index, int *buffer_write_index) {
	int k = b_read_input(input_buf);

	if (k == 0)
		return false;

	for (int i = 0; i < k; i++) {
		if (symbol_is_escape(input_buf[i])) {
			add_user_command_with_no_data(buffer_read_index, buffer_write_index, UC_esc, 1);

			continue;
		}

		if (mode_type == MODE_NORMAL) {
			switch (input_buf[i]) {
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					normal_mode_command_add_count(input_buf[i] - '0');
					break;

				default:
					normal_mode_command_add_char(input_buf[i]);
					break;
			}

			if (!normal_mode_command_is_valid_partial())
				normal_mode_command_clear();

			if (normal_mode_command_is_valid_full()) {
				handle_normal_mode_command(buffer_read_index, buffer_write_index);
				normal_mode_command_clear();
			}
		} else if (mode_type == MODE_COMMAND) {
			if (symbol_is_enter(input_buf[i])) {
				if (command_mode_command_is_valid()) {
					handle_command_mode_command();
				} else {
					char error_text[MAX_COMMAND_SIZE] = {0};
					sprintf(error_text, "Not and editor command: %s", command_mode_command.command);

					message_set(error_text);
				}

				command_mode_command_clear();
				mode_set_type(MODE_NORMAL);
			} else if (symbol_is_backspace(input_buf[i])) {
				command_mode_strip_tail();
			} else if (symbol_is_printable(input_buf[i])) {
				command_mode_add_char(input_buf[i]);
			}
		} else if (mode_type == MODE_INSERT) {
			insert_mode_command_add_char(input_buf[i]);
			handle_insert_mode_command(buffer_read_index, buffer_write_index);
			insert_mode_command_clear();
		}
	}

	return true;
}

void editor_command_add(
		int *read_index,
		int *write_index,
		EditorCommandType command_type,
		void *data,
		int size_of_data
) {
	if (*write_index >= MAX_COMMANDS_BUFFER_SIZE) {
		*write_index = 0;
		*read_index = 0;
	}

	memcpy(&editor_commands[*write_index].data, data, size_of_data);
	editor_commands[*write_index].type = command_type;

	*write_index = *write_index + 1;
}

void editor_command_add_move_cursor(int* read_index, int* write_index, EditorMoveCursorDirection direction, int count) {
	EditorCommandMoveCursorData data = {.direction = direction, .count = count};

	editor_command_add(read_index, write_index, EC_MOVE_CURSOR, &data, sizeof(data));
}

void editor_command_add_switch_window(int* read_index, int* write_index, EditorSwitchWindowDirection direction, int count) {
	EditorCommandSwitchWindowData data = {.direction = direction, .count = count};

	editor_command_add(read_index, write_index, EC_SWITCH_WINDOW, &data, sizeof(data));
}

void editor_command_add_normalize_cursor(int* read_index, int* write_index) {
	editor_command_add(read_index, write_index, EC_NORMALIZE_CURSOR, NULL, 0);
}

void editor_command_add_scroll(int* read_index, int* write_index, EditorScrollDirection direction, int count) {
	EditorCommandScrollData data = {.direction = direction, .scroll = count};

	editor_command_add(read_index, write_index, EC_SCROLL, &data, sizeof(data));
}

void editor_command_add_insert_symbol(int* read_index, int* write_index, char symbol, int count) {
	EditorCommandInsertSymbolData data = {.symbol = symbol, .count = count};

	editor_command_add(read_index, write_index, EC_INSERT, &data, sizeof(data));
}

void process_user_commands(
	int *user_read_index,
	int *user_write_index,
	int *editor_read_index,
	int *editor_write_index,
	NormalModeCommand* command_modifier
) {
	while (*user_read_index != *user_write_index) {
		UserCommand cmd = user_commands[*user_read_index];

		switch (cmd.type) {
			case UC_h: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_BACKWARD, cmd.count);
				break;
			}

			case UC_j: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_DOWN, cmd.count);
				break;
			}

			case UC_k: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_UP, cmd.count);
				break;
			}

			case UC_l: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_FORWARD, cmd.count);
				break;
			}

			case UC_caret: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_START_OF_LINE, cmd.count);
				break;
			}

			case UC_dollar: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_END_OF_LINE, cmd.count);
				break;
			}

			case UC_w: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_NEXT_WORD, cmd.count);
				break;
			}

			case UC_b: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_PREV_WORD, cmd.count);
				break;
			}

			case UC_e: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_END_OF_WORD, cmd.count);
				break;
			}

			case UC_H: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TOP, cmd.count);
				break;
			}

			case UC_M: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_MID, cmd.count);
				break;
			}

			case UC_L: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_BOTTOM, cmd.count);
				break;
			}

			case UC_gg: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_FIRST_LINE, cmd.count);
				break;
			}

			case UC_G: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_LAST_LINE, cmd.count);
				break;
			}

			case UC_CTRL_d: {
				editor_command_add_scroll(editor_read_index, editor_write_index, ED_SCROLL_DOWN, cmd.count);
				break;
			}

			case UC_CTRL_u: {
				editor_command_add_scroll(editor_read_index, editor_write_index, ED_SCROLL_UP, cmd.count);
				break;
			}

			case UC_esc: {
				normal_mode_command_clear();
				insert_mode_command_clear();
				command_mode_command_clear();
				editor_command_add_normalize_cursor(editor_read_index, editor_write_index);
				mode_set_type(MODE_NORMAL);
				break;
			}

			case UC_colon: {
				mode_set_type(MODE_COMMAND);
				normal_mode_command_clear();
				break;
			}

			case UC_i: {
				mode_set_type(MODE_INSERT);
				break;
			}

			case UC_I: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_START_OF_LINE, 1);
				mode_set_type(MODE_INSERT);
				break;
			}

			case UC_a: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_FORWARD, 1);
				mode_set_type(MODE_INSERT);
				break;
			}

			case UC_A: {
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_TO_END_OF_LINE, 1);
				mode_set_type(MODE_INSERT);
				break;
			}

			case UC_insert_symbol: {
				UserCommandDataSymbol* data = (UserCommandDataSymbol*)cmd.data;

				editor_command_add_insert_symbol(editor_read_index, editor_write_index, data->symbol, cmd.count);
				break;
			}

			case UC_CTRL_w_l: {
				editor_command_add_switch_window(editor_read_index, editor_write_index, ED_SWITCH_WINDOW_RIGHT, cmd.count);
				break;
			}

			case UC_CTRL_w_h: {
				editor_command_add_switch_window(editor_read_index, editor_write_index, ED_SWITCH_WINDOW_LEFT, cmd.count);
				break;
			}

			case UC_CTRL_w_j: {
				editor_command_add_switch_window(editor_read_index, editor_write_index, ED_SWITCH_WINDOW_DOWN, cmd.count);
				break;
			}

			case UC_CTRL_w_k: {
				editor_command_add_switch_window(editor_read_index, editor_write_index, ED_SWITCH_WINDOW_UP, cmd.count);
				break;
			}
		}

		*user_read_index = *user_read_index + 1;
	}
}

void process_editor_commands(
	int *editor_read_index,
	int *editor_write_index,
	EditorWindow* editor_window
) {
	LineT** cursor_line = &editor_window->cursor_line;
	LineItemT** cursor_line_item = &editor_window->cursor_line_item;
	Pos* cursor_pos = &editor_window->cursor_pos;
	int cols = view_cols(editor_window->source_view);
	int rows = view_rows(editor_window->source_view);
	int total_rows = line_count_from(editor_window->editor_buffer->head_line);

	while (*editor_read_index != *editor_write_index) {
		switch (editor_commands[*editor_read_index].type) {
			case EC_MOVE_CURSOR: {
				EditorCommandMoveCursorData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandMoveCursorData));

				int move_count = MAX(1, data.count);

				switch (data.direction) {
					case ED_CURSOR_BACKWARD: {
						nav_oneline_count(nav_backward, cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_FORWARD: {
						nav_oneline_count(nav_forward, cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_DOWN: {
						nav_down(cursor_line, cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_UP: {
						nav_up(cursor_line, cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_TO_START_OF_LINE: {
						nav_to_start_of_line(cursor_line_item, cursor_pos);

						break;
					}

					case ED_CURSOR_TO_END_OF_LINE: {
						nav_down(cursor_line, cursor_line_item, cursor_pos, move_count - 1);
						nav_to_end_of_line(cursor_line_item, cursor_pos);

						break;
					}

					case ED_CURSOR_TOP: {
						nav_up(cursor_line, cursor_line_item, cursor_pos, cursor_pos->y);

						break;
					}

					case ED_CURSOR_MID: {
						int current_row = cursor_pos->y;
						int lines_offset = (rows / 2) - current_row;

						nav_vertical(cursor_line, cursor_line_item, cursor_pos, lines_offset);

						break;
					}

					case ED_CURSOR_BOTTOM: {
						nav_down(cursor_line, cursor_line_item, cursor_pos, rows - cursor_pos->y - 1);

						break;
					}

					case ED_CURSOR_TO_NEXT_WORD: {
						for (int i = move_count; i > 0; i--) {
							nav_to_end_of_word(cursor_line_item, cursor_pos);
							nav_forward_or_next_line(cursor_line, cursor_line_item, cursor_pos);
							nav_to_next_word(cursor_line, cursor_line_item, cursor_pos);
						}

						break;
					}

					case ED_CURSOR_TO_END_OF_WORD: {
						for (int i = move_count; i > 0; i--) {
							if (nav_to_end_of_word(cursor_line_item, cursor_pos))
								continue;

							nav_forward_or_next_line(cursor_line, cursor_line_item, cursor_pos);
							nav_to_next_word(cursor_line, cursor_line_item, cursor_pos);
							nav_to_end_of_word(cursor_line_item, cursor_pos);
						}

						break;
					}

					case ED_CURSOR_TO_PREV_WORD: {
						for (int i = move_count; i > 0; i--) {
							if (nav_to_start_of_word(cursor_line_item, cursor_pos))
								continue;

							nav_backward_or_prev_line(cursor_line, cursor_line_item, cursor_pos);
							nav_to_prev_word(cursor_line, cursor_line_item, cursor_pos);
							nav_to_start_of_word(cursor_line_item, cursor_pos);
						}

						break;
					}

					case ED_CURSOR_TO_FIRST_LINE: {
						int current_row = editor_window->y_offset + cursor_pos->y;
						int lines_offset = data.count - current_row - 1;

						if (data.count == 0) {
							nav_vertical(cursor_line, cursor_line_item, cursor_pos, current_row * -1);
						} else {
							nav_vertical(cursor_line, cursor_line_item, cursor_pos, lines_offset);
						}

						break;
					}

					case ED_CURSOR_TO_LAST_LINE: {
						int current_row = editor_window->y_offset + cursor_pos->y;
						int lines_offset = data.count - current_row - 1;

						if (data.count == 0) {
							nav_vertical(cursor_line, cursor_line_item, cursor_pos, total_rows - current_row);
						} else {
							nav_vertical(cursor_line, cursor_line_item, cursor_pos, lines_offset);
						}

						break;
					}
				}

				offset_sync_with_cursor(&editor_window->y_offset, cursor_pos, rows, total_rows);

				break;
			}

			case EC_SWITCH_WINDOW: {
				EditorCommandSwitchWindowData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandSwitchWindowData));

				int move_count = MAX(1, data.count);

				switch (data.direction) {
					case ED_SWITCH_WINDOW_RIGHT: {
						if (current_editor_tab->tab_item_current->right != NULL) {
							current_editor_tab->tab_item_current = current_editor_tab->tab_item_current->right;
						}

						break;
					}

					case ED_SWITCH_WINDOW_LEFT: {
						if (current_editor_tab->tab_item_current->left != NULL) {
							current_editor_tab->tab_item_current = current_editor_tab->tab_item_current->left;
						}

						break;
					}

					case ED_SWITCH_WINDOW_UP: {
						if (current_editor_tab->tab_item_current->up != NULL) {
							current_editor_tab->tab_item_current = current_editor_tab->tab_item_current->up;
						}

						break;
					}

					case ED_SWITCH_WINDOW_DOWN: {
						if (current_editor_tab->tab_item_current->down != NULL) {
							current_editor_tab->tab_item_current = current_editor_tab->tab_item_current->down;
						}

						break;
					}

					break;
				}
			}

			case EC_NORMALIZE_CURSOR: {
				if (line_item_is_newline(*cursor_line_item))
					nav_backward(cursor_line_item, cursor_pos);

				break;
			}

			case EC_SCROLL: {
				EditorCommandScrollData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandScrollData));

				int move_count = MAX(1, data.scroll);

				if (data.scroll > 0)
					editor_config_set_scroll(data.scroll);

				int scroll = editor_config.scroll;

				switch (data.direction) {
					case ED_SCROLL_DOWN: {
						nav_down(cursor_line, cursor_line_item, cursor_pos, scroll);
						offset_down(&editor_window->y_offset, cursor_pos, rows, total_rows, scroll);

						break;
					}

					case ED_SCROLL_UP: {
						nav_up(cursor_line, cursor_line_item, cursor_pos, scroll);
						offset_up(&editor_window->y_offset, cursor_pos, rows, scroll);

						break;
					}
				}
			}

			case EC_INSERT: {
				EditorCommandInsertSymbolData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandInsertSymbolData));

				if (symbol_is_backspace(data.symbol)) {
					int shift = insert_delete_symbol(cursor_line, cursor_line_item);
					cursor_backward(cursor_pos, shift);

					if (shift == 0 && editor_window->cursor_line->prev != NULL) {
						if (line_item_is_newline(editor_window->cursor_line->prev->item_head)) {
							line_delete_before(*cursor_line);
							cursor_up(cursor_pos, 1);
						} else {
							nav_up(cursor_line, cursor_line_item, cursor_pos, 1);
							nav_to_end_of_line(cursor_line_item, cursor_pos);
							nav_backward(cursor_line_item, cursor_pos);
							line_concat_after(*cursor_line);
							cursor_forward(cursor_pos, 1);
							line_item_next(cursor_line_item);
						}
					}
				} else if (symbol_is_enter(data.symbol)) {
					LineT* current_line = *cursor_line;
					LineItemT* line_tail = *cursor_line_item;
					LineItemT* new_end_of_current_line = line_tail->prev;

					line_new_after(cursor_line);
					nav_down(cursor_line, cursor_line_item, cursor_pos, 1);

					LineItemT* new_line_terminator = line_find_next_symbol(*cursor_line, '\n');

					line_set_head(*cursor_line, line_tail);

					if (new_end_of_current_line != NULL) {
						new_end_of_current_line->next = new_line_terminator;
						new_line_terminator->prev = new_end_of_current_line;
					} else {
						current_line->item_head = new_line_terminator;
					}

					*cursor_line_item = (*cursor_line)->item_head;
				} else if (symbol_is_printable(data.symbol)) {
					int shift = insert_insert_symbol(editor_window->cursor_line, editor_window->cursor_line_item, data.symbol);
					cursor_forward(cursor_pos, shift);
				}
			}
		}

		*editor_read_index = *editor_read_index + 1;
	}
}

EditorTabItemT* init_editor_tab_item(
	char* filename,
	ViewT* parent_view
) {
	int parent_view_cols = view_cols(parent_view);
	int parent_view_rows = view_rows(parent_view);

	ViewT* source_view = view_new(STATUS_COLUMN_WIDTH, 0, parent_view_cols, parent_view_rows - INFO_LINE_HEIGHT, parent_view);
	ViewT* status_column_view = view_new(0, 0, STATUS_COLUMN_WIDTH, parent_view_rows - INFO_LINE_HEIGHT, parent_view);
	ViewT* info_line_view = view_new(0, parent_view_rows - INFO_LINE_HEIGHT, parent_view_cols, parent_view_rows, parent_view);

	EditorBufferT* editor_buffer = editor_buffer_find_by_filename(filename);

	if (editor_buffer == NULL) {
		editor_buffer = editor_buffer_new();

		if (strcmp("", filename)) {
			editor_buffer->head_line = read_and_parse_source_file(filename);
			editor_buffer->filename = filename;
		} else {
			LineItemT* head_line_item = line_item_new();
			head_line_item->symbol = '\n';

			editor_buffer->head_line = line_new();
			editor_buffer->head_line->item_head = head_line_item;
			editor_buffer->filename = "";
		}
	}

	EditorWindow* editor_window = editor_window_new();
	editor_window->editor_buffer = editor_buffer;
	editor_window->cursor_line = editor_buffer->head_line;
	editor_window->cursor_line_item = editor_buffer->head_line->item_head;
	editor_window->source_view = source_view;
	editor_window->status_column_view = status_column_view;
	editor_window->info_line_view = info_line_view;
	editor_window->cursor_pos.x = 0;
	editor_window->cursor_pos.y = 0;
	editor_window->x_offset = 0;
	editor_window->y_offset = 0;

	EditorTabItemT* editor_tab_item = editor_tab_item_new();
	editor_tab_item->tabno = tabno_counter;
	editor_tab_item->window = editor_window;

	tabno_counter++;

	return editor_tab_item;
}

EditorTabT* init_editor_tab(
	char* filename,
	ViewT* parent_view
) {
	EditorTabItemT* editor_tab_item = init_editor_tab_item(filename, parent_view);

	EditorTabT* editor_tab = editor_tab_new();
	editor_tab->tab_item_head = editor_tab_item;
	editor_tab->tab_item_current = editor_tab_item;

	return editor_tab;
}

int main(int argc, char * argv[]) {
	srand(time(NULL));

	s_configure_terminal();
	atexit(s_restore_terminal);
	signal(SIGINT, s_exit_editor);

	r_clear_screen();

	struct winsize w_winsize;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w_winsize);

	int rows = w_winsize.ws_row;
	int cols = w_winsize.ws_col;

	if (rows == 0)
		rows = 80;

	if (cols == 0)
		cols = 190;

	int user_command_read_index = 0;
	int user_command_write_index = 0;

	int editor_command_read_index = 0;
	int editor_command_write_index = 0;

	char user_input_buf[256];
	char* filename = "";

	if (argc > 1)
		filename = argv[1];

	ViewT* main_view = view_new(0, 0, cols, rows, NULL);
	ViewT* source_view = view_new(0, 0, cols, rows - COMMAND_LINE_HEIGHT, main_view);
	ViewT* current_editor_tab_view = view_new_embedded(source_view);

	current_editor_tab = init_editor_tab(filename, current_editor_tab_view);

	ViewT* command_line_view = view_new(0, rows - COMMAND_LINE_HEIGHT, cols, rows, main_view);
	ViewT* debug_info_view = view_new((cols / 3) * 2, 0, cols - 1, (rows / 3) * 2, main_view);

	DebugInformation debug_info = {
		.cols = &cols,
		.rows = &rows,
	};

	editor_config_set_scroll(rows / 2);

	draw_editor_tab(current_editor_tab);
	draw_command_line(command_line_view);
	draw_debug_information(debug_info_view, debug_info);

	render(rows, cols);
	switch_grids();

	while (!exit_loop) {
		if (!handle_user_input(user_input_buf, &user_command_read_index, &user_command_write_index))
			continue;

		process_user_commands(
			&user_command_read_index,
			&user_command_write_index,
			&editor_command_read_index,
			&editor_command_write_index,
			&normal_mode_command
		);

		process_editor_commands(
			&editor_command_read_index,
			&editor_command_write_index,
			current_editor_tab->tab_item_current->window
		);

		draw_editor_tab(current_editor_tab);
		draw_command_line(command_line_view);
		draw_debug_information(debug_info_view, debug_info);

		render(rows, cols);
		switch_grids();
	}

	r_move_cursor(0, rows);
}
