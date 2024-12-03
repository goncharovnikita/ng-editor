#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MAX_GRID_SIZE 65535

static struct termios old_termios, new_termios;

typedef struct {
	int x;
	int y;
} Pos;

typedef enum {
	CLEAR,
	CURSOR,
	BLUE,
	WHITE,
} Color;

typedef struct {
	char symbol;
	Color color;
} Cell;

typedef Cell Grid[MAX_GRID_SIZE];

bool exit_loop = false;

Grid rendered_grid = {};
Grid current_grid = {};
Grid source_file_grid = {};

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
			fprintf(stdout, "\033[30;47m");
			break;

		case BLUE:
			fprintf(stdout, "\033[34m");
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

void fill_source_file_grid(int rows, int cols, char *file_name) {
	int y = 0;
	char line[cols];
	FILE *source_file = fopen(file_name, "r");

	while (fgets(line, cols, source_file)) {
		bool line_ended = false;
		bool buf_size_exceeded = false;

		for (int i = 0; i < cols; i++) {
			if ((y * cols) + i >= MAX_GRID_SIZE) {
				buf_size_exceeded = true;
				break;
			}

			if (line[i] == '\n')
				line_ended = true;

			if (line_ended) {
				Cell cell = {.symbol = ' ', .color = CLEAR};
				source_file_grid[(y * cols) + i] = cell;

				continue;
			}

			Cell cell = {.symbol = line[i], .color = CLEAR};
			source_file_grid[(y * cols) + i] = cell;
		}

		if (buf_size_exceeded)
			break;

		y++;
	}
}

void draw_source_file(int rows, int cols, int y_offset) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int source_index = get_grid_index(x, y + y_offset, cols);
			int grid_index = get_grid_index(x, y, cols);

			memcpy(&current_grid[grid_index], &source_file_grid[source_index], sizeof(Cell));
		}
	}
}

void draw_cursor(int x, int y, int cols) {
	// current_grid[get_grid_index(x, y, cols)].symbol = '@';
	current_grid[get_grid_index(x, y, cols)].color = CURSOR;
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
	int y_offset = 0;
	Pos cursor_pos = { .x = 0, .y = 0 };
	char buf[128];

	if (argc > 1) {
		fill_source_file_grid(rows, cols, argv[1]);
	}

	draw_source_file(rows, cols, y_offset);
	render(rows, cols);
	switch_grids();

	while (!exit_loop) {
		int k = b_read_input(&buf);

		for (int i = 0; i < k; i++) {
			switch (buf[i]) {
				case 'j':
					cursor_pos.y = MIN(rows, cursor_pos.y + 1);
					break;

				case 'k':
					cursor_pos.y = MAX(0, cursor_pos.y - 1);
					break;

				case 'l':
					cursor_pos.x = MIN(cols, cursor_pos.x + 1);
					break;

				case 'h':
					cursor_pos.x = MAX(0, cursor_pos.x - 1);
					break;

				default:
					break;
			}
		}

		draw_source_file(rows, cols, y_offset);
		draw_cursor(cursor_pos.x, cursor_pos.y, cols);

		render(rows, cols);
		switch_grids();

		fflush(stdout);
	}

	r_move_cursor(0, rows);
}
