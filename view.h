typedef struct {
	int x;
	int y;
} Pos;

typedef struct View {
	Pos origin;
	Pos end;
	struct View* parent;
} ViewT;

ViewT* view_new(int origin_x, int origin_y, int end_x, int end_y, ViewT* parent);

int view_x(ViewT* view, int x);
int view_y(ViewT* view, int x);
int view_cols(ViewT* view);
int view_rows(ViewT* view);
