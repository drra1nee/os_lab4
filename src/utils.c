#include "utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void print_error(const char *msg)
{
    DWORD err = GetLastError();
    wchar_t wbuf[512];
    char buf[1024];

    // консоль в UTF-8, поэтому берем текст в UTF-16 и переводим
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, err, 0, wbuf, sizeof(wbuf) / sizeof(wbuf[0]), NULL);
    if (n == 0 ||
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), NULL, NULL) == 0) {
        fprintf(stderr, "[PID %lu TID %lu] %s: код ошибки %lu\n",
                GetCurrentProcessId(), GetCurrentThreadId(), msg, err);
        return;
    }

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' '))
        buf[--len] = '\0';

    fprintf(stderr, "[PID %lu TID %lu] %s: %s (код %lu)\n",
            GetCurrentProcessId(), GetCurrentThreadId(), msg, buf, err);
}

void log_msg(const char *tag, const char *fmt, ...)
{
    char text[10240];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    // в stderr, потому что stdout потомка - это файл
    fprintf(stderr, "[%-6s | PID %5lu TID %5lu] %s\n",
            tag, GetCurrentProcessId(), GetCurrentThreadId(), text);
    fflush(stderr);
}

int read_line(char *buf, int size)
{
    if (fgets(buf, size, stdin) == NULL)
        return -1;

    size_t len = strlen(buf);
    int too_long = 0;

    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    } else if (!feof(stdin)) {
        // выбрасываем остаток длинной строки
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
        too_long = 1;
    }

    while (len > 0 && buf[len - 1] == '\r')
        buf[--len] = '\0';

    return too_long ? -2 : (int)len;
}
