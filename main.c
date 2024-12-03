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

static struct termios old_termios, new_termios;

typedef struct {
	int x;
	int y;
} Pos;

typedef struct {
	char symbol;
} Cell;

typedef Cell Grid[65535];

bool exit_loop = false;

Grid rendered_grid = {};
Grid current_grid = {};

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

int b_read_input(void *buf) {
	return read(STDIN_FILENO, buf, sizeof(buf));
}

void render(int rows, int cols) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int index = get_grid_index(x, y, cols);

			Cell old_cell = rendered_grid[index];
			Cell new_cell = current_grid[index];

			if (old_cell.symbol != new_cell.symbol) {
				r_move_cursor(x, y);

				printf("%c", new_cell.symbol);
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

void fill_grid_with_source_file(int rows, int cols, char *file_name) {
	int y = 0;
	char line[cols];
	FILE *source_file = fopen(file_name, "r");

	while (y < rows && fgets(line, cols, source_file)) {
		bool line_ended = false;

		for (int i = 0; i < cols; i++) {
			if (line[i] == '\n')
				line_ended = true;

			if (line_ended) {
				Cell cell = {.symbol = ' '};
				current_grid[(y * cols) + i] = cell;

				continue;
			}

			Cell cell = {.symbol = line[i]};
			current_grid[(y * cols) + i] = cell;
		}

		y++;
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
	Pos cursor_pos = { .x = 0, .y = 0 };
	char buf[128];

	if (argc > 1) {
		fill_grid_with_source_file(rows, cols, argv[1]);
	}

	render(rows, cols);
	switch_grids();

	while (!exit_loop) {
		int k = b_read_input(&buf);

		/* for (int i = 0; i < k; i++) { */
		/* 	fprintf(stdout, "%c", buf[i]); */
		/* } */

		render(rows, cols);
		switch_grids();

		fflush(stdout);
	}
}
