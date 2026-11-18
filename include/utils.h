#ifndef UTILS_H
#define UTILS_H

#include <windows.h>

void print_error(const char *msg);

void log_msg(const char *tag, const char *fmt, ...);

// длина строки, -1 конец ввода, -2 строка не влезла в буфер
int read_line(char *buf, int size);

#endif
