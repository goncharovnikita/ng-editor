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
	EC_MOVE_CURSOR,
} EditorCommandType;

typedef enum {
	UC_h,
	UC_j,
	UC_k,
	UC_l,
	UC_H,
	UC_M,
	UC_L,
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

typedef struct {
	int x;
	int y;
} EditorCommandMoveData;

typedef struct {
	EditorCommandType type;
	char data[256];
} EditorCommand;

bool exit_loop = false;

Grid rendered_grid = {};
Grid current_grid = {};
Grid source_file_grid = {};

UserCommand user_commands[MAX_COMMANDS_BUFFER_SIZE] = {};
EditorCommand editor_commands[MAX_COMMANDS_BUFFER_SIZE] = {};

int get_grid_index(int x, int y, int cols) {
	return (y * cols) + x;
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

void draw_status_column(View dest, int rows, int cols, int offset) {
	for (int y = dest.origin.y; y < dest.end.y; y++) {
		char column_text[256] = {0};
		sprintf(column_text, "%d", y + offset + 1);

		int column_text_size = strlen(column_text);

		for (int x = dest.origin.x; x < dest.end.x && (x - dest.origin.x < column_text_size); x++) {
			int grid_index = get_grid_index(x + (dest.origin.x * (y + 1)), y + dest.origin.y, cols);
			current_grid[grid_index].symbol = column_text[x - dest.origin.x];
		}
	}
}

void highlight_line(int row, int cols, int y_offset, int x_offset) {
	int y = row + y_offset;

	for (int x = x_offset; x < cols; x++) {
		current_grid[get_grid_index(x, y, cols)].color = HIGHLIGHT;
	}
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

			case 'M':
				add_user_command(buffer_read_index, buffer_write_index, UC_M);
				break;

			case 'H':
				add_user_command(buffer_read_index, buffer_write_index, UC_H);
				break;

			case 'L':
				add_user_command(buffer_read_index, buffer_write_index, UC_L);
				break;

			default:
				break;
		}
	}
}

void add_editor_command(int *read_index, int *write_index, void *data, int size_of_data) {
	if (*write_index >= MAX_COMMANDS_BUFFER_SIZE) {
		*write_index = 0;
		*read_index = 0;
	}

	memcpy(&editor_commands[*write_index].data, data, size_of_data);
	editor_commands[*write_index].type = EC_MOVE_CURSOR;

	*write_index = *write_index + 1;
}

void process_user_commands(
	int *user_read_index,
	int *user_write_index,
	int *editor_read_index,
	int *editor_write_index,
	Pos cursor_pos,
	int rows,
	int cols
) {
	while (*user_read_index != *user_write_index) {
		switch (user_commands[*user_read_index].type) {
			case UC_h: {
				EditorCommandMoveData data = {.x = MAX(0, cursor_pos.x - 1), .y = cursor_pos.y};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			case UC_j: {
				EditorCommandMoveData data = {.x = cursor_pos.x, .y = MIN(rows - 1, cursor_pos.y + 1)};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			case UC_k: {
				EditorCommandMoveData data = {.x = cursor_pos.x, .y = MAX(0, cursor_pos.y - 1)};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			case UC_l: {
				EditorCommandMoveData data = {.x = MIN(cols, cursor_pos.x + 1), .y = cursor_pos.y};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			case UC_H: {
				EditorCommandMoveData data = {.x = cursor_pos.x, .y = 0};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			case UC_M: {
				EditorCommandMoveData data = {.x = cursor_pos.x, .y = rows / 2};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			case UC_L: {
				EditorCommandMoveData data = {.x = cursor_pos.x, .y = rows - 1};

				add_editor_command(editor_read_index, editor_write_index, &data, sizeof(data));
				break;
			}

			default:
				break;
		}

		*user_read_index = *user_read_index + 1;
	}
}

void process_editor_commands(
	int *editor_read_index,
	int *editor_write_index,
	Pos *cursor_pos,
	int rows,
	int cols
) {
	while (*editor_read_index != *editor_write_index) {
		switch (editor_commands[*editor_read_index].type) {
			case EC_MOVE_CURSOR: {
				EditorCommandMoveData data;
				memcpy(&data, &editor_commands[*editor_read_index].data, sizeof(EditorCommandMoveData));

				cursor_pos->x = data.x;
				cursor_pos->y = data.y;

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

	if (argc > 1) {
		fill_source_file_grid(
			initial_buffer_view.end.x - initial_buffer_view.origin.x,
			argv[1]
		);
		buffer_status.filename = argv[1];
	}

	draw_source_file(initial_buffer_view, y_offset, x_offset);
	highlight_line(cursor_pos.y, cols, y_offset, STATUS_COLUMN_WIDTH);
	draw_cursor(initial_buffer_view, cursor_pos.x, cursor_pos.y, cols);
	draw_info_line(rows - INFO_LINE_HEIGHT, cols, buffer_status);
	draw_status_column(status_column_view, rows, cols, y_offset);

	render(rows, cols);
	switch_grids();

	while (!exit_loop) {
		handle_user_input(user_input_buf, &user_command_read_index, &user_command_write_index);
		process_user_commands(
			&user_command_read_index,
			&user_command_write_index,
			&editor_command_read_index,
			&editor_command_write_index,
			cursor_pos,
			initial_buffer_view.end.y - initial_buffer_view.origin.y,
			initial_buffer_view.end.x - initial_buffer_view.origin.x
		);

		process_editor_commands(
			&editor_command_read_index,
			&editor_command_write_index,
			&cursor_pos,
			initial_buffer_view.end.y - initial_buffer_view.origin.y,
			initial_buffer_view.end.x - initial_buffer_view.origin.x
		);

		buffer_status.column = cursor_pos.x;
		buffer_status.line = cursor_pos.y;

		draw_source_file(initial_buffer_view, x_offset, y_offset);
		highlight_line(cursor_pos.y, cols, y_offset, STATUS_COLUMN_WIDTH);
		draw_cursor(initial_buffer_view, cursor_pos.x, cursor_pos.y, cols);
		draw_info_line(rows - INFO_LINE_HEIGHT, cols, buffer_status);
		draw_status_column(status_column_view, rows, cols, y_offset);

		render(rows, cols);
		switch_grids();

		fflush(stdout);
	}

	r_move_cursor(0, rows);
}
