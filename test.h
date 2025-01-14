#include <stdarg.h>

bool run_tests(int n, ...) {
	va_list args;
	va_start(args, n);
	bool failed = false;

	for (int i = 0; i < n; i++) {
		if (va_arg(args, int(*)())() != 0)
			failed = true;
	}

	va_end(args);

	return failed;
}
