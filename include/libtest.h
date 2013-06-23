#ifndef _LIBTEST_H_
#define _LIBTEST_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif

#define RUNTESTS(t) \
	do { \
		fprint(2, "%s\n", "----\t" __FILE__ "\t " #t "\t----"); \
		t(); \
		test_teardown(); \
	} while (0) \

void start_tests(void);
void test_teardown(void);

void ok(char *srcfile, int srcline, int passed, char *err, ...);
void not_ok(char *srcfile, int srcline, int passed, char *err, ...);

void ok_long_eq(char *srcfile, int srcline, long, long, char *err, ...);
void ok_long_lt(char *srcfile, int srcline, long, long, char *err, ...);

void ok_ulong_eq(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);
void ok_ulong_ne(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);
void ok_ulong_ge(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);
void ok_ulong_le(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);

void ok_cstring_eq(char *srcfile, int srcline, char *s0, char *s1, char *err, ...);

void ok_ptr_ne(char *srcfile, int srcline, void *, void *, char *, ...);

#if defined(__cplusplus)
}
#endif
#endif /* _LIBTEST_H_ */

