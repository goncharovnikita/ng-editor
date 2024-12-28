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

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MAX_GRID_SIZE 65535
#define MAX_COMMANDS_BUFFER_SIZE 1
#define MAX_COMMAND_SIZE 4096
#define MAX_MESSAGE_SIZE 1024
#define STATUS_COLUMN_WIDTH 5
#define INFO_LINE_HEIGHT 2

static struct termios old_termios, new_termios;

typedef struct {
	int x;
	int y;
} Pos;

typedef struct {
	Pos origin;
	Pos end;
} View;

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
	EC_SCROLL,
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

typedef Cell Grid[MAX_GRID_SIZE];

typedef struct {
	char *filename;
	int32_t line;
	int32_t column;
} BufferStatus;

typedef struct {
	int scroll;
} EditorConfig;

typedef struct {
	UserCommandType type;
	int count;
} UserCommand;

typedef struct {
	int count;
	char command[MAX_COMMAND_SIZE];
} NormalModeCommand;

typedef struct {
	char command[MAX_COMMAND_SIZE];
} CommandModeCommand;

typedef struct {
	char message[MAX_MESSAGE_SIZE];
	NormalModeCommand* normal_mode_command;
} MessageLineData;

typedef enum {
	ED_CURSOR_UP,
	ED_CURSOR_RIGHT,
	ED_CURSOR_DOWN,
	ED_CURSOR_LEFT,
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

typedef struct {
	EditorMoveCursorDirection direction;
	int count;
} EditorCommandMoveCursorData;

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

typedef struct {
	int* rows;
	int* cols;
	int* total_rows;
	int* x_offset;
	int* y_offset;
	Pos* cursor_pos;
	LineItemT** cursor_head;
	LineT** cursor_line;
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
	"G", "g", "gg",
	"\x04", // CTRL-d
	"\x15", // CTRL-u
	"\x1b", // CTRL-[
	":",

	"-1",
};

const char* conf_command_mode_valid_commands[] = {
	"q", "quit",

	"-1",
};

Grid rendered_grid = {};
Grid current_grid = {};
LineT* source_file_first_line;
LineItemT* cursor_line_item;
LineT* cursor_line;
int source_file_items_count = 0;

ModeType mode_type;

UserCommand user_commands[MAX_COMMANDS_BUFFER_SIZE] = {};
EditorCommand editor_commands[MAX_COMMANDS_BUFFER_SIZE] = {};

EditorConfig editor_config = {.scroll = 1};

NormalModeCommand normal_mode_command = {.count = 0, .command = ""};
CommandModeCommand command_mode_command = {.command = ""};

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

int get_grid_index(int x, int y, int cols) {
	return (y * cols) + x;
}

int get_view_grid_index(View view, int x, int y, int cols) {
	return get_grid_index(x + view.origin.x, y + view.origin.y, cols);
}

void message_set(char* message) {
	sprintf(message_line_data.message, "%s", message);
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

void r_draw_line(int x, int y, int cols, int max_len, char* text) {
	int text_len = strlen(text);

	for (int i = 0; i < max_len; i++) {
		char symbol = ' ';
		if (i < text_len)
			symbol = text[i];

		current_grid[get_grid_index(x + i, y, cols)].symbol = symbol;
		current_grid[get_grid_index(x + i, y, cols)].color = WHITE;
	}
}

int b_read_input(void *buf) {
	return read(STDIN_FILENO, buf, sizeof(buf));
}

void render(int rows, int cols) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int index = get_grid_index(x, y, cols);

			Cell old_cell = rendered_grid[index];
			Cell new_cell = current_grid[index];

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

void read_source_file_into_list(char *file_name) {
	int y = 0;
	char line[256];
	FILE *source_file = fopen(file_name, "r");

	LineT* head_line = (LineT*)malloc(sizeof(LineT));
	LineT* current_line = head_line;
	LineT* prev_line = NULL;

	LineItemT* head_line_item = (LineItemT*)malloc(sizeof(LineItemT));
	LineItemT* current_line_item = head_line_item;
	LineItemT* prev_line_item = NULL;

	head_line->item_head = head_line_item;

	while (fgets(line, 100, source_file)) {
		for (int i = 0; i < strlen(line); i++) {
			assert(current_line != NULL);
			assert(current_line_item != NULL);

			if (line[i] == '\n') {
				if (prev_line != NULL) {
					prev_line->next = current_line;
					current_line->prev = prev_line;
				}

				if (prev_line_item == NULL) {
					current_line_item->symbol = '\n';
					current_line_item = (LineItemT*)(malloc(sizeof(LineItemT)));
				}

				prev_line = current_line;
				prev_line_item = NULL;

				current_line = (LineT*)malloc(sizeof(LineT));
				current_line->item_head = current_line_item;

				continue;
			}

			current_line_item->symbol = line[i];

			if (prev_line_item != NULL) {
				prev_line_item->next = current_line_item;
				current_line_item->prev = prev_line_item;
			}

			prev_line_item = current_line_item;

			current_line_item = (LineItemT*)(malloc(sizeof(LineItemT)));

			source_file_items_count++;
		}
	}

	source_file_first_line = head_line;
	cursor_line_item = head_line_item;
	cursor_line = head_line;

	fclose(source_file);
}

void draw_source_file(View dest, int cols, int x_offset, int y_offset) {
	int view_cols = dest.end.x - dest.origin.x;
	int view_rows = dest.end.y - dest.origin.y;

	int source_file_skip_lines = y_offset;

	LineT* source_file_line = source_file_first_line;

	while (source_file_skip_lines > 0) {
		assert(source_file_line != NULL);

		source_file_line = source_file_line->next;
		source_file_skip_lines--;
	}

	for (int y = 0; y < view_rows; y++) {
		bool line_ended = false;

		LineItemT* source_file_line_item;

		if (source_file_line != NULL) {
			 source_file_line_item = source_file_line->item_head;
		}

		for (int x = 0; x < view_cols; x++) {
			int grid_index = get_view_grid_index(dest, x, y, cols);

			if (source_file_line_item == NULL)
				line_ended = true;

			if (line_ended) {
				Cell cell = {.symbol = ' ', .color = CLEAR};

				memcpy(&current_grid[grid_index], &cell, sizeof(Cell));

				continue;
			}

			if (source_file_line_item->symbol == '\t') {
				for (int j = 0; j < 4 && x + j < view_cols; j++) {
					grid_index = get_view_grid_index(dest, x + j, y, cols);

					Cell cell = {.symbol = ' ', .color = CLEAR};

					if (j == 0)
						cell.symbol = '>';

					memcpy(&current_grid[grid_index], &cell, sizeof(Cell));
				}

				x += 3;

				source_file_line_item = source_file_line_item->next;
			} else {
				Cell cell = {.symbol = source_file_line_item->symbol, .color = CLEAR};

				if (source_file_line_item->symbol == '\n')
					cell.symbol = ' ';

				memcpy(&current_grid[grid_index], &cell, sizeof(Cell));

				source_file_line_item = source_file_line_item->next;
			}
		}

		if (source_file_line != NULL)
			source_file_line = source_file_line->next;
	}
}

void draw_cursor(View dest, int x, int y, int cols) {
	current_grid[get_grid_index(x + (dest.origin.x), y + dest.origin.y, cols)].color = CURSOR;
}

void draw_info_line(int y, int cols, BufferStatus status) {
	for (int i = 0; i < cols; i++) {
		current_grid[get_grid_index(i, y, cols)].symbol = ' ';
		current_grid[get_grid_index(i, y, cols)].color = INFO_LINE;
	}

	for (int i = 0; i < strlen(status.filename); i++) {
		current_grid[get_grid_index(i, y, cols)].symbol = status.filename[i];
	}

	char line_and_column_text[256];

	sprintf(line_and_column_text, "%d,%d", status.line, status.column);

	int line_and_column_text_len = strlen(line_and_column_text);

	for (int i = 0; i < strlen(line_and_column_text); i++) {
		current_grid[get_grid_index(cols - i - 1, y, cols)].symbol = line_and_column_text[line_and_column_text_len - 1 - i];
	}
}

void draw_message_line(int y, int cols) {
	for (int i = 0; i < cols; i++) {
		current_grid[get_grid_index(i, y, cols)].symbol = ' ';
		current_grid[get_grid_index(i, y, cols)].color = CLEAR;
	}

	if (mode_type == MODE_NORMAL) {
		for (int i = 0; i < strlen(message_line_data.message); i++) {
			current_grid[get_grid_index(i, y, cols)].symbol = message_line_data.message[i];
		}

		char user_modifier_text[256] = {0};

		if (normal_mode_command.count > 0)
			sprintf(user_modifier_text, "%d", normal_mode_command.count);

		if (strlen(normal_mode_command.command) > 0)
			sprintf(user_modifier_text, "%s%s", user_modifier_text, normal_mode_command.command);

		if (strlen(user_modifier_text) == 0)
			sprintf(user_modifier_text, " ");

		int line_and_column_text_len = strlen(user_modifier_text);

		for (int i = 0; i < strlen(user_modifier_text); i++) {
			current_grid[get_grid_index(cols - i - 1, y, cols)].symbol = user_modifier_text[line_and_column_text_len - 1 - i];
		}
	} else if (mode_type == MODE_COMMAND) {
		char command_text[MAX_COMMAND_SIZE] = {0};
		sprintf(command_text, ":%s", command_mode_command.command);

		for (int i = 0; i < strlen(command_text); i++) {
			current_grid[get_grid_index(i, y, cols)].symbol = command_text[i];
		}
	}
}

void draw_status_column(View dest, int rows, int cols, int total_rows, int offset) {
	for (int y = dest.origin.y; y < dest.end.y; y++) {
		char column_text[256] = {0};

		if (y + offset + 1 <= total_rows) {
			sprintf(column_text, "%d", y + offset + 1);
		} else {
			sprintf(column_text, "");
		}

		r_draw_line(dest.origin.x, y, cols, dest.end.x - dest.origin.x, column_text);
	}
}

void draw_debug_information(View dest, int cols, DebugInformation debug_info) {
	for (int y = dest.origin.y; y < dest.end.y; y++) {
		int lineno = y - dest.origin.y;
		int grid_index = get_view_grid_index(dest, dest.origin.x, y, cols);

		char text[4096];

		if (lineno == 0) {
			sprintf(text, "rows: %d, cols: %d, total_rows: %d", *debug_info.rows, *debug_info.cols, *debug_info.total_rows);
			r_draw_line(dest.origin.x, y, cols, dest.end.x - dest.origin.x, text);
		}

		if (lineno == 1) {
			sprintf(text, "x_offset: %d, y_offset: %d", *debug_info.x_offset, *debug_info.y_offset);
			r_draw_line(dest.origin.x, y, cols, dest.end.x - dest.origin.x, text);
		}

		if (lineno == 2) {
			sprintf(text, "cursor_pos_x: %d, cursor_pos_y: %d", debug_info.cursor_pos->x, debug_info.cursor_pos->y);
			r_draw_line(dest.origin.x, y, cols, dest.end.x - dest.origin.x, text);
		}

		if (lineno == 3) {
			LineItemT* ch = *debug_info.cursor_head;
			char symbol[256] = {ch->symbol};
			if (symbol[0] == '\n')
				strcpy(symbol, "newline");
			sprintf(text, "cursor source item (symbol: %s, addr: %p)", symbol, ch);
			r_draw_line(dest.origin.x, y, cols, dest.end.x - dest.origin.x, text);
		}

		if (lineno == 4) {
			LineT* line = *debug_info.cursor_line;
			LineItemT* ch = line->item_head;

			char line_text[256] = {};

			int i = 0;
			while (ch != NULL && ch->symbol != '\n') {
				line_text[i] = ch->symbol;
				ch = ch->next;
				i++;
			}

			sprintf(text, "%s", line_text);
			r_draw_line(dest.origin.x, y, cols, dest.end.x - dest.origin.x, text);
		}
	}
}

void highlight_line(View dest, int row, int cols) {
	int y = row;

	for (int x = dest.origin.x; x < dest.end.x; x++) {
		current_grid[get_grid_index(x, y, cols)].color = HIGHLIGHT;
	}
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

	if (cursor_head->symbol == '\n' || cursor_head->next == NULL || cursor_head->next->symbol == '\n')
		return 0;

	int shift = nav_move_count_by_source_symbol(cursor_head->symbol);
	cursor_pos->x += shift;
	cursor_head = cursor_head->next;

	*cursor_node = cursor_head;

	return shift;
}

int nav_backward(LineItemT** cursor_node, Pos* cursor_pos) {
	LineItemT* cursor_head = *cursor_node;

	if (cursor_head->symbol == '\n' || cursor_head->prev == NULL || cursor_head->prev->symbol == '\n')
		return 0;

	int shift = nav_move_count_by_source_symbol(cursor_head->prev->symbol);
	cursor_pos->x -= shift;
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

	cursor_pos->y = cursor_pos->y + 1;
	cursor_pos->x = 0;

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

	cursor_pos->y = cursor_pos->y - 1;
	cursor_pos->x = 0;

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

void normal_mode_command_clear() {
	normal_mode_command.count = 0;

	sprintf(normal_mode_command.command, "");
}

void normal_mode_command_add_count(int count) {
	normal_mode_command.count = normal_mode_command.count * 10 + count;
}

bool normal_mode_command_is_valid() {
	int i = 0;

	while (strcmp("-1", conf_normal_mode_valid_commands[i])) {
		if (!strcmp(conf_normal_mode_valid_commands[i], normal_mode_command.command))
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

bool command_mode_command_is_valid() {
	int i = 0;

	while (strcmp("-1", conf_command_mode_valid_commands[i])) {
		if (!strcmp(conf_command_mode_valid_commands[i], command_mode_command.command))
			return true;

		i++;
	}

	return false;
}

bool add_user_command(int *read_index, int *write_index, UserCommandType type, int count) {
	if (*write_index >= MAX_COMMANDS_BUFFER_SIZE) {
		*write_index = 0;
		*read_index = 0;
	}

	user_commands[*write_index].type = type;
	user_commands[*write_index].count = count;

	*write_index = *write_index + 1;

	normal_mode_command_clear();

	return true;
}

bool handle_normal_mode_command(int* read_index, int* write_index) {
	if (!strcmp(":", normal_mode_command.command)) {
		mode_set_type(MODE_COMMAND);
		normal_mode_command_clear();

		return true;
	}

	if (!strcmp("h", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_h, normal_mode_command.count);

	if (!strcmp("j", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_j, normal_mode_command.count);

	if (!strcmp("k", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_k, normal_mode_command.count);

	if (!strcmp("l", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_l, normal_mode_command.count);

	if (!strcmp("^", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_caret, normal_mode_command.count);

	if (!strcmp("$", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_dollar, normal_mode_command.count);

	if (!strcmp("w", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_w, normal_mode_command.count);

	if (!strcmp("e", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_e, normal_mode_command.count);

	if (!strcmp("b", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_b, normal_mode_command.count);

	if (!strcmp("H", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_H, normal_mode_command.count);

	if (!strcmp("M", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_M, normal_mode_command.count);

	if (!strcmp("L", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_L, normal_mode_command.count);

	if (!strcmp("gg", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_gg, normal_mode_command.count);

	if (!strcmp("G", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_G, normal_mode_command.count);

	if (!strcmp("\x04", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_CTRL_d, normal_mode_command.count);

	if (!strcmp("\x15", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_CTRL_u, normal_mode_command.count);

	if (!strcmp("\x1b", normal_mode_command.command))
		return add_user_command(read_index, write_index, UC_esc, normal_mode_command.count);

	return false;
}

void handle_command_mode_command() {
	if (!strcmp("q", command_mode_command.command) || !strcmp("quit", command_mode_command.command))
		return s_exit_editor();
}

bool handle_user_input(char *input_buf, int *buffer_read_index, int *buffer_write_index) {
	int k = b_read_input(input_buf);

	if (k == 0)
		return false;

	for (int i = 0; i < k; i++) {
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

			if (!normal_mode_command_is_valid())
				normal_mode_command_clear();

			handle_normal_mode_command(buffer_read_index, buffer_write_index);
		} else if (mode_type == MODE_COMMAND) {
			if (input_buf[i] == 10) { // Enter
				if (command_mode_command_is_valid()) {
					handle_command_mode_command();
				} else {
					char error_text[MAX_COMMAND_SIZE] = {0};
					sprintf(error_text, "Not and editor command: %s", command_mode_command.command);

					message_set(error_text);
				}

				command_mode_command_clear();
				mode_set_type(MODE_NORMAL);
			} else {
				command_mode_add_char(input_buf[i]);
			}
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
	normal_mode_command_clear();
}

void editor_command_add_scroll(int* read_index, int* write_index, EditorScrollDirection direction, int count) {
	EditorCommandScrollData data = {.direction = direction, .scroll = count};

	editor_command_add(read_index, write_index, EC_SCROLL, &data, sizeof(data));
	normal_mode_command_clear();
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
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_LEFT, cmd.count);
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
				editor_command_add_move_cursor(editor_read_index, editor_write_index, ED_CURSOR_RIGHT, cmd.count);
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
				break;
			}
		}

		*user_read_index = *user_read_index + 1;
	}
}

void process_editor_commands(
	int *editor_read_index,
	int *editor_write_index,
	Pos *cursor_pos,
	View active_view,
	int rows,
	int cols,
	int total_rows,
	int *x_offset,
	int *y_offset
) {
	while (*editor_read_index != *editor_write_index) {
		switch (editor_commands[*editor_read_index].type) {
			case EC_MOVE_CURSOR: {
				EditorCommandMoveCursorData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandMoveCursorData));

				int move_count = MAX(1, data.count);

				switch (data.direction) {
					case ED_CURSOR_LEFT: {
						nav_oneline_count(nav_backward, &cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_RIGHT: {
						nav_oneline_count(nav_forward, &cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_DOWN: {
						nav_down(&cursor_line, &cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_UP: {
						nav_up(&cursor_line, &cursor_line_item, cursor_pos, move_count);

						break;
					}

					case ED_CURSOR_TO_START_OF_LINE: {
						nav_to_start_of_line(&cursor_line_item, cursor_pos);

						break;
					}

					case ED_CURSOR_TO_END_OF_LINE: {
						nav_down(&cursor_line, &cursor_line_item, cursor_pos, move_count - 1);
						nav_to_end_of_line(&cursor_line_item, cursor_pos);

						break;
					}

					case ED_CURSOR_TOP: {
						nav_up(&cursor_line, &cursor_line_item, cursor_pos, cursor_pos->y);

						break;
					}

					case ED_CURSOR_MID: {
						int current_row = cursor_pos->y;
						int lines_offset = (rows / 2) - current_row;

						nav_vertical(&cursor_line, &cursor_line_item, cursor_pos, lines_offset);

						break;
					}

					case ED_CURSOR_BOTTOM: {
						nav_down(&cursor_line, &cursor_line_item, cursor_pos, rows - cursor_pos->y - 1);

						break;
					}

					case ED_CURSOR_TO_NEXT_WORD: {
						for (int i = move_count; i > 0; i--) {
							nav_to_end_of_word(&cursor_line_item, cursor_pos);
							nav_forward_or_next_line(&cursor_line, &cursor_line_item, cursor_pos);
							nav_to_next_word(&cursor_line, &cursor_line_item, cursor_pos);
						}

						break;
					}

					case ED_CURSOR_TO_END_OF_WORD: {
						for (int i = move_count; i > 0; i--) {
							if (nav_to_end_of_word(&cursor_line_item, cursor_pos))
								continue;

							nav_forward_or_next_line(&cursor_line, &cursor_line_item, cursor_pos);
							nav_to_next_word(&cursor_line, &cursor_line_item, cursor_pos);
							nav_to_end_of_word(&cursor_line_item, cursor_pos);
						}

						break;
					}

					case ED_CURSOR_TO_PREV_WORD: {
						for (int i = move_count; i > 0; i--) {
							if (nav_to_start_of_word(&cursor_line_item, cursor_pos))
								continue;

							nav_backward_or_prev_line(&cursor_line, &cursor_line_item, cursor_pos);
							nav_to_prev_word(&cursor_line, &cursor_line_item, cursor_pos);
							nav_to_start_of_word(&cursor_line_item, cursor_pos);
						}

						break;
					}

					case ED_CURSOR_TO_FIRST_LINE: {
						int current_row = *y_offset + cursor_pos->y;
						int lines_offset = data.count - current_row - 1;

						if (data.count == 0) {
							nav_vertical(&cursor_line, &cursor_line_item, cursor_pos, current_row * -1);
						} else {
							nav_vertical(&cursor_line, &cursor_line_item, cursor_pos, lines_offset);
						}

						break;
					}

					case ED_CURSOR_TO_LAST_LINE: {
						int current_row = *y_offset + cursor_pos->y;
						int lines_offset = data.count - current_row - 1;

						if (data.count == 0) {
							nav_vertical(&cursor_line, &cursor_line_item, cursor_pos, total_rows - current_row);
						} else {
							nav_vertical(&cursor_line, &cursor_line_item, cursor_pos, lines_offset);
						}

						break;
					}
				}

				offset_sync_with_cursor(y_offset, cursor_pos, rows, total_rows);

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
						nav_down(&cursor_line, &cursor_line_item, cursor_pos, scroll);
						offset_down(y_offset, cursor_pos, rows, total_rows, scroll);

						break;
					}

					case ED_SCROLL_UP: {
						nav_up(&cursor_line, &cursor_line_item, cursor_pos, scroll);
						offset_up(y_offset, cursor_pos, rows, scroll);

						break;
					}
				}
			}

		}

		*editor_read_index = *editor_read_index + 1;
	}
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

	int total_rows = 0;
	int x_offset = 0;
	int y_offset = 0;
	Pos cursor_pos = { .x = 0, .y = 0 };

	int user_command_read_index = 0;
	int user_command_write_index = 0;

	int editor_command_read_index = 0;
	int editor_command_write_index = 0;

	char user_input_buf[256];

	BufferStatus buffer_status = {};
	buffer_status.filename = "empty buffer";
	buffer_status.line = 0;
	buffer_status.column = 0;

	View initial_buffer_view;
	initial_buffer_view.origin.x = STATUS_COLUMN_WIDTH;
	initial_buffer_view.origin.y = 0;
	initial_buffer_view.end.x = cols;
	initial_buffer_view.end.y = rows - INFO_LINE_HEIGHT;

	View status_column_view = {
		.origin = {
			.x = 0,
			.y = 0,
		},
		.end = {
			.x = STATUS_COLUMN_WIDTH,
			.y = rows - INFO_LINE_HEIGHT,
		},
	};

	View debug_info_view = {
		.origin = {
			.x = (cols / 3) * 2,
			.y = 0,
		},
		.end = {
			.x = cols - 1,
			.y = (rows / 3) * 2,
		},
	};

	if (argc > 1) {
		read_source_file_into_list(argv[1]);

		FILE *source_file = fopen(argv[1], "r");

		char line[256];

		while (fgets(line, cols, source_file)) {
			total_rows++;
		}

		fclose(source_file);

		buffer_status.filename = argv[1];
	}

	DebugInformation debug_info = {
		.cols = &cols,
		.rows = &rows,
		.total_rows = &total_rows,
		.x_offset = &x_offset,
		.y_offset = &y_offset,
		.cursor_pos = &cursor_pos,
		.cursor_head = &cursor_line_item,
		.cursor_line = &cursor_line,
	};

	editor_config_set_scroll(rows / 2);

	draw_source_file(initial_buffer_view, cols, y_offset, x_offset);
	highlight_line(initial_buffer_view, cursor_pos.y, cols);
	draw_cursor(initial_buffer_view, cursor_pos.x, cursor_pos.y, cols);
	draw_info_line(rows - INFO_LINE_HEIGHT, cols, buffer_status);
	draw_message_line(rows - 1, cols);
	draw_status_column(status_column_view, rows, cols, total_rows, y_offset);
	draw_debug_information(debug_info_view, cols, debug_info);

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
			&cursor_pos,
			initial_buffer_view,
			initial_buffer_view.end.y - initial_buffer_view.origin.y,
			initial_buffer_view.end.x - initial_buffer_view.origin.x,
			total_rows,
			&x_offset,
			&y_offset
		);

		buffer_status.column = cursor_pos.x;
		buffer_status.line = cursor_pos.y;

		draw_source_file(initial_buffer_view, cols, x_offset, y_offset);
		highlight_line(initial_buffer_view, cursor_pos.y, cols);
		draw_cursor(initial_buffer_view, cursor_pos.x, cursor_pos.y, cols);
		draw_info_line(rows - INFO_LINE_HEIGHT, cols, buffer_status);
		draw_message_line(rows - 1, cols);
		draw_status_column(status_column_view, rows, cols, total_rows, y_offset);
		draw_debug_information(debug_info_view, cols, debug_info);

		render(rows, cols);
		switch_grids();
	}

	r_move_cursor(0, rows);
}
