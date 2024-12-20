#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MAX_GRID_SIZE 65535
#define MAX_COMMANDS_BUFFER_SIZE 1
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
	CLEAR,
	CURSOR,
	INFO_LINE,
	HIGHLIGHT,
	WHITE,
} Color;

typedef enum {
	EC_NAVIGATE,
} EditorCommandType;

typedef enum {
	UC_h,
	UC_j,
	UC_k,
	UC_l,
	UC_w,
	UC_b,
	UC_H,
	UC_M,
	UC_L,
	UC_G,
	UC_CTRL_D,
	UC_CTRL_U,
} UserCommandType;

typedef struct {
	char symbol;
	Color color;
} Cell;

typedef Cell Grid[MAX_GRID_SIZE];

typedef struct {
	char *filename;
	int32_t line;
	int32_t column;
} BufferStatus;

typedef struct {
	UserCommandType type;
} UserCommand;

typedef enum {
	ED_CURSOR_UP,
	ED_CURSOR_RIGHT,
	ED_CURSOR_DOWN,
	ED_CURSOR_LEFT,
	ED_CURSOR_TOP,
	ED_CURSOR_MID,
	ED_CURSOR_BOTTOM,
	ED_CURSOR_TO_NEXT_WORD,
	ED_CURSOR_TO_PREV_WORD,
	ED_SCROLL_HALF_DOWN,
	ED_SCROLL_HALF_UP,
	ED_SCROLL_BOTTOM,
	ED_SCROLL_TOP,
} EditorDirection;

typedef struct {
	EditorDirection direction;
} EditorCommandMoveData;

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
} DebugInformation;

bool exit_loop = false;

Grid rendered_grid = {};
Grid current_grid = {};
Grid source_file_grid = {};

UserCommand user_commands[MAX_COMMANDS_BUFFER_SIZE] = {};
EditorCommand editor_commands[MAX_COMMANDS_BUFFER_SIZE] = {};

int get_grid_index(int x, int y, int cols) {
	return (y * cols) + x;
}

int get_view_grid_index(View view, int x, int y, int cols) {
	return get_grid_index(x + (view.origin.x * (y + 1)), y + view.origin.y, cols);
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
	new_termios.c_cc[VMIN] = 0;
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

void s_handle_sigint() {
	exit_loop = true;
}

void r_move_cursor(int x, int y) {
	fprintf(stdout, "\e[%d;%dH", y + 1, x + 1);
}

void r_clear_screen() {
	fprintf(stdout, "\e[1;1H\e[2J");
}

void r_draw_line(int x, int y, int max_len, char* text) {
	int text_len = strlen(text);

	for (int i = 0; i < max_len; i++) {
		r_move_cursor(x + i, y);
		if (i < text_len) {
			fprintf(stdout, "%c", text[i]);
		} else {
			fprintf(stdout, " ");
		}
	}

	fflush(stdout);
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

void fill_source_file_grid(int cols, char *file_name) {
	int y = 0;
	char line[cols];
	FILE *source_file = fopen(file_name, "r");

	while (fgets(line, cols, source_file)) {
		bool line_ended = false;
		bool buf_size_exceeded = false;
		int source_file_index = 0;
		int grid_index = 0;

		while (grid_index < cols) {
			if ((y * cols) + grid_index >= MAX_GRID_SIZE) {
				buf_size_exceeded = true;
				break;
			}

			if (line[source_file_index] == '\n')
				line_ended = true;

			if (line_ended) {
				Cell cell = {.symbol = ' ', .color = CLEAR};
				source_file_grid[(y * cols) + grid_index] = cell;
				source_file_index++;
				grid_index++;

				continue;
			}

			if (line[source_file_index] == '\t') {
				int j = 0;

				while (grid_index < cols && j < 4) {
					Cell cell = {.symbol = ' ', .color = CLEAR};
					source_file_grid[(y * cols) + grid_index] = cell;
					grid_index++;
					j++;
				}

				source_file_index++;

				continue;
			}

			Cell cell = {.symbol = line[source_file_index], .color = CLEAR};
			source_file_grid[(y * cols) + grid_index] = cell;
			source_file_index++;
			grid_index++;
		}

		if (buf_size_exceeded)
			break;

		y++;
	}

	int x = 0;

	while ((y * cols) + x < MAX_GRID_SIZE) {
		Cell cell = {.symbol = ' ', .color = CLEAR};
		source_file_grid[(y * cols) + x] = cell;

		x++;

		if (x == cols) {
			x = 0;
			y++;
		}
	}

	fclose(source_file);
}

void draw_source_file(View dest, int x_offset, int y_offset) {
	int cols = dest.end.x - dest.origin.x;
	int rows = dest.end.y - dest.origin.y;

	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int source_index = get_grid_index(x + x_offset, y + y_offset, cols);
			int grid_index = get_grid_index(x + (dest.origin.x * (y + 1)), y + dest.origin.y, cols);

			memcpy(&current_grid[grid_index], &source_file_grid[source_index], sizeof(Cell));
		}
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

void draw_status_column(View dest, int rows, int cols, int total_rows, int offset) {
	for (int y = dest.origin.y; y < dest.end.y; y++) {
		char column_text[256] = {0};

		if (y + offset + 1 <= total_rows) {
			sprintf(column_text, "%d", y + offset + 1);
		} else {
			sprintf(column_text, "");
		}

		r_draw_line(dest.origin.x, y, dest.end.x - dest.origin.x, column_text);
	}
}

void draw_debug_information(View dest, int cols, DebugInformation debug_info) {
	for (int y = dest.origin.y; y < dest.end.y; y++) {
		int lineno = y - dest.origin.y;
		int grid_index = get_grid_index(dest.origin.x + (dest.origin.x * (y + 1)), y + dest.origin.y, cols);

		char text[dest.end.x - dest.origin.x];

		if (lineno == 0) {
			sprintf(text, "rows: %d, cols: %d, total_rows: %d", *debug_info.rows, *debug_info.cols, *debug_info.total_rows);
			r_draw_line(dest.origin.x, y, dest.end.x - dest.origin.x, text);
		}

		if (lineno == 1) {
			sprintf(text, "x_offset: %d, y_offset: %d", *debug_info.x_offset, *debug_info.y_offset);
			r_draw_line(dest.origin.x, y, dest.end.x - dest.origin.x, text);
		}

		if (lineno == 2) {
			sprintf(text, "cursor_pos_x: %d, cursor_pos_y: %d", debug_info.cursor_pos->x, debug_info.cursor_pos->y);
			r_draw_line(dest.origin.x, y, dest.end.x - dest.origin.x, text);
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
	return sym != ' ';
}

bool nav_cursor_forward_position(View view, Pos cursor_pos, Pos* dest_pos) {
	int target_x = cursor_pos.x;
	int target_y = cursor_pos.y;

	target_x++;

	if (target_x >= view.end.x - view.origin.x) {
		target_x = 0;
		target_y++;
	}

	if (target_y >= view.end.y - view.origin.y) {
		return false;
	}

	dest_pos->x = target_x;
	dest_pos->y = target_y;

	return true;
}

bool nav_cursor_back_position(View view, Pos cursor_pos, Pos* dest_pos) {
	int target_x = cursor_pos.x;
	int target_y = cursor_pos.y;

	target_x--;

	if (target_x < 0) {
		target_x = view.end.x - view.origin.x - 1;
		target_y--;
	}

	if (target_y < 0) {
		return false;
	}

	dest_pos->x = target_x;
	dest_pos->y = target_y;

	return true;
}

bool nav_find_next_word(View view, int cols, Pos cursor_pos, Pos* target_pos) {
	Pos new_cursor = cursor_pos;

	while (!nav_is_word_symbol(current_grid[get_view_grid_index(view, new_cursor.x, new_cursor.y, cols)].symbol)) {
		if (!nav_cursor_forward_position(view, new_cursor, &new_cursor))
			return false;
	}

	target_pos->x = new_cursor.x;
	target_pos->y = new_cursor.y;

	return true;
}

bool nav_find_prev_word(View view, int cols, Pos cursor_pos, Pos* target_pos) {
	Pos new_cursor = cursor_pos;

	while (!nav_is_word_symbol(current_grid[get_view_grid_index(view, new_cursor.x, new_cursor.y, cols)].symbol)) {
		if (!nav_cursor_back_position(view, new_cursor, &new_cursor))
			return false;
	}

	target_pos->x = new_cursor.x;
	target_pos->y = new_cursor.y;

	return true;
}

bool nav_find_end_of_word(View view, int cols, Pos cursor_pos, Pos* target_pos) {
	Pos next_pos = cursor_pos;

	while (nav_is_word_symbol(current_grid[get_view_grid_index(view, next_pos.x, next_pos.y, cols)].symbol)) {
		pos_copy(target_pos, &next_pos);

		if (!nav_cursor_forward_position(view, next_pos, &next_pos))
			break;
	}

	return !pos_is_equal(cursor_pos, *target_pos);
}

bool nav_find_start_of_word(View view, int cols, Pos cursor_pos, Pos* target_pos) {
	Pos next_pos = cursor_pos;

	while (nav_is_word_symbol(current_grid[get_view_grid_index(view, next_pos.x, next_pos.y, cols)].symbol)) {
		pos_copy(target_pos, &next_pos);

		if (!nav_cursor_back_position(view, next_pos, &next_pos))
			break;
	}

	return !pos_is_equal(cursor_pos, *target_pos);
}

void add_user_command(int *read_index, int *write_index, UserCommandType type) {
	if (*write_index >= MAX_COMMANDS_BUFFER_SIZE) {
		*write_index = 0;
		*read_index = 0;
	}

	user_commands[*write_index].type = type;

	*write_index = *write_index + 1;
}

void handle_user_input(char *input_buf, int *buffer_read_index, int *buffer_write_index) {
	int k = b_read_input(input_buf);

	for (int i = 0; i < k; i++) {
		switch (input_buf[i]) {
			case 'j':
				add_user_command(buffer_read_index, buffer_write_index, UC_j);
				break;

			case 'k':
				add_user_command(buffer_read_index, buffer_write_index, UC_k);
				break;

			case 'l':
				add_user_command(buffer_read_index, buffer_write_index, UC_l);
				break;

			case 'h':
				add_user_command(buffer_read_index, buffer_write_index, UC_h);
				break;

			case 'w':
				add_user_command(buffer_read_index, buffer_write_index, UC_w);
				break;

			case 'b':
				add_user_command(buffer_read_index, buffer_write_index, UC_b);
				break;

			case 'M':
				add_user_command(buffer_read_index, buffer_write_index, UC_M);
				break;

			case 'H':
				add_user_command(buffer_read_index, buffer_write_index, UC_H);
				break;

			case 'L':
				add_user_command(buffer_read_index, buffer_write_index, UC_L);
				break;

			case 'G':
				add_user_command(buffer_read_index, buffer_write_index, UC_G);
				break;

			case 4:
				add_user_command(buffer_read_index, buffer_write_index, UC_CTRL_D);
				break;

			case 21:
				add_user_command(buffer_read_index, buffer_write_index, UC_CTRL_U);
				break;

			default:
				break;
		}
	}
}

void add_editor_command(
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

void process_user_commands(
	int *user_read_index,
	int *user_write_index,
	int *editor_read_index,
	int *editor_write_index
) {
	while (*user_read_index != *user_write_index) {
		switch (user_commands[*user_read_index].type) {
			case UC_h: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_LEFT};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_j: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_DOWN};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_k: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_UP};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_l: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_RIGHT};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_w: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_TO_NEXT_WORD};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_b: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_TO_PREV_WORD};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_H: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_TOP};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_M: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_MID};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_L: {
				EditorCommandMoveData data = {.direction = ED_CURSOR_BOTTOM};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_G: {
				EditorCommandMoveData data = {.direction = ED_SCROLL_BOTTOM};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_CTRL_D: {
				EditorCommandMoveData data = {.direction = ED_SCROLL_HALF_DOWN};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
				break;
			}

			case UC_CTRL_U: {
				EditorCommandMoveData data = {.direction = ED_SCROLL_HALF_UP};

				add_editor_command(editor_read_index, editor_write_index, EC_NAVIGATE, &data, sizeof(data));
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
			case EC_NAVIGATE: {
				EditorCommandMoveData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandMoveData));

				switch (data.direction) {
					case ED_CURSOR_LEFT: {
						cursor_pos->x = MAX(0, cursor_pos->x - 1);

						break;
					}

					case ED_CURSOR_DOWN: {
						int target_y = cursor_pos->y + 1;

						if (target_y >= rows - 1) {
							if ((*y_offset + rows) < total_rows)
								*y_offset = *y_offset + 1;

							target_y = rows - 1;
						}

						cursor_pos->y = MIN(rows - 1, target_y);

						break;
					}

					case ED_CURSOR_UP: {
						int target_y = cursor_pos->y - 1;

						if (target_y < 0) {
							if (*y_offset > 0)
								*y_offset = *y_offset - 1;

							target_y = 0;
						}

						cursor_pos->y = target_y;

						break;
					}

					case ED_CURSOR_RIGHT: {
						cursor_pos->x = MIN(cols - 1, cursor_pos->x + 1);

						break;
					}

					case ED_CURSOR_TOP: {
						cursor_pos->y = 0;

						break;
					}

					case ED_CURSOR_MID: {
						cursor_pos->y = rows / 2;

						break;
					}

					case ED_CURSOR_BOTTOM: {
						cursor_pos->y = rows - 1;

						break;
					}

					case ED_CURSOR_TO_NEXT_WORD: {
						nav_find_end_of_word(active_view, cols, *cursor_pos, cursor_pos);
						nav_cursor_forward_position(active_view, *cursor_pos, cursor_pos);
						nav_find_next_word(active_view, cols, *cursor_pos, cursor_pos);

						break;
					}

					case ED_CURSOR_TO_PREV_WORD: {
						nav_find_start_of_word(active_view, cols, *cursor_pos, cursor_pos);
						nav_cursor_back_position(active_view, *cursor_pos, cursor_pos);
						nav_find_prev_word(active_view, cols, *cursor_pos, cursor_pos);
						nav_find_start_of_word(active_view, cols, *cursor_pos, cursor_pos);

						break;
					}

					case ED_SCROLL_HALF_DOWN: {
						int target_y_offset = *y_offset + (rows / 2);

						*y_offset = MIN(total_rows, target_y_offset);

						break;
					}

					case ED_SCROLL_HALF_UP: {
						int target_y_offset = *y_offset - (rows / 2);

						*y_offset = MAX(0, target_y_offset);

						break;
					}

					case ED_SCROLL_TOP: {
						*y_offset = 0;
						cursor_pos->y = 0;

						break;
					}

					case ED_SCROLL_BOTTOM: {
						*y_offset = total_rows - rows;
						cursor_pos->y = rows - 1;

						break;
					}
				}

				break;
			}

			default:
				break;
		}

		*editor_read_index = *editor_read_index + 1;
	}
}

int main(int argc, char * argv[]) {
	srand(time(NULL));

	s_configure_terminal();
	atexit(s_restore_terminal);
	signal(SIGINT, s_handle_sigint);

	r_clear_screen();

	struct winsize w_winsize;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w_winsize);

	int rows = w_winsize.ws_row;
	int cols = w_winsize.ws_col;
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
		fill_source_file_grid(
			initial_buffer_view.end.x - initial_buffer_view.origin.x,
			argv[1]
		);

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
	};

	draw_source_file(initial_buffer_view, y_offset, x_offset);
	highlight_line(initial_buffer_view, cursor_pos.y, cols);
	draw_cursor(initial_buffer_view, cursor_pos.x, cursor_pos.y, cols);
	draw_info_line(rows - INFO_LINE_HEIGHT, cols, buffer_status);
	draw_status_column(status_column_view, rows, cols, total_rows, y_offset);
	draw_debug_information(debug_info_view, cols, debug_info);

	render(rows, cols);
	switch_grids();

	while (!exit_loop) {
		handle_user_input(user_input_buf, &user_command_read_index, &user_command_write_index);
		process_user_commands(
			&user_command_read_index,
			&user_command_write_index,
			&editor_command_read_index,
			&editor_command_write_index
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

		draw_source_file(initial_buffer_view, x_offset, y_offset);
		highlight_line(initial_buffer_view, cursor_pos.y, cols);
		draw_cursor(initial_buffer_view, cursor_pos.x, cursor_pos.y, cols);
		draw_info_line(rows - INFO_LINE_HEIGHT, cols, buffer_status);
		draw_status_column(status_column_view, rows, cols, total_rows, y_offset);
		draw_debug_information(debug_info_view, cols, debug_info);

		render(rows, cols);
		switch_grids();

		fflush(stdout);
	}

	r_move_cursor(0, rows);
}
