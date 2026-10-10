#ifndef NOTOS_ASSERT_H
#define NOTOS_ASSERT_H
void notos_tls_assert(const char *expression);
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression)?(void)0:notos_tls_assert(#expression))
#endif
#endif
