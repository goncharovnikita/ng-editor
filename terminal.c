#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#define MAX_GRID_SIZE 1024

typedef char terminal_color[64];

typedef struct {
	char symbol;
	terminal_color* color;
} Cell;

typedef Cell Grid[MAX_GRID_SIZE][MAX_GRID_SIZE];

static struct termios old_termios, new_termios;

Grid rendered_grid = {};
Grid current_grid = {};

void t_configure_terminal() {
	tcgetattr(STDIN_FILENO, &old_termios);
	new_termios = old_termios;

	new_termios.c_lflag &= ~(ICANON | ECHO);
	new_termios.c_cc[VMIN] = 1;
	new_termios.c_cc[VTIME] = 0;

	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	printf("\e[?25l");
}

void t_restore_terminal() {
	printf("\e[?25h");
	printf("\e[m");
	fflush(stdout);

	tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

void t_clear_screen() {
	fprintf(stdout, "\e[1;1H\e[2J");
}

void t_move_cursor(int x, int y) {
	fprintf(stdout, "\e[%d;%dH", y + 1, x + 1);
}

void t_set_color(terminal_color color) {
	fprintf(stdout, "%s", color);
}

void t_render(int rows, int cols) {
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			Cell old_cell = rendered_grid[y][x];
			Cell new_cell = current_grid[y][x];

			if (old_cell.symbol != new_cell.symbol || old_cell.color != new_cell.color) {
				t_move_cursor(x, y);

				t_set_color(*new_cell.color);
				printf("%c", new_cell.symbol);
			}
		}
	}

	fflush(stdout);
}
