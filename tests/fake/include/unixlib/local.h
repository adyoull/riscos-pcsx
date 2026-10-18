/* Stand-in for UnixLib's unixlib/local.h in the fake tests (the test supplies it). */
#include <stddef.h>
char *__unixify_std(const char *name, char *buffer, size_t buf_len, int filetype);
