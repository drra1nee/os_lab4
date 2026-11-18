#include "channel.h"
#include "utils.h"
#include "vowels.h"

#include <stdio.h>
#include <stdlib.h>

static int write_all(HANDLE h, const char *buf, DWORD len) {
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile(h, buf, len, &written, NULL)) {
            print_error("WriteFile");
            return -1;
        }
        buf += written;
        len -= written;
    }
    return 0;
}

int main(int argc, char **argv) {
    static char line[SHM_DATA_SIZE];
    static char out[SHM_DATA_SIZE + 1];
    char tag[16];
    int rc = 0;

    if (argc != 3) {
        fprintf(stderr, "Использование: child.exe <PID родителя> <номер канала>\n");
        return EXIT_FAILURE;
    }

    char *end1, *end2;
    unsigned long ppid = strtoul(argv[1], &end1, 10);
    long idx = strtol(argv[2], &end2, 10);
    if (*end1 != '\0' || *end2 != '\0' || ppid == 0 || idx <= 0) {
        fprintf(stderr, "Неверные аргументы потомка\n");
        return EXIT_FAILURE;
    }
    snprintf(tag, sizeof(tag), "CHILD%ld", idx);

    HANDLE out_h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out_h == NULL || out_h == INVALID_HANDLE_VALUE) {
        print_error("GetStdHandle (stdout)");
        return EXIT_FAILURE;
    }

    // Ctrl+C игнорируем, выходим вслед за родителем
    if (!SetConsoleCtrlHandler(NULL, TRUE))
        print_error("SetConsoleCtrlHandler");

    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)ppid);
    if (parent == NULL) {
        print_error("OpenProcess (родитель)");
        return EXIT_FAILURE;
    }

    channel_t chan;
    if (channel_open(&chan, (DWORD)ppid, (int)idx) < 0) {
        CloseHandle(parent);
        return EXIT_FAILURE;
    }
    log_msg(tag, "запущен, канал %ld открыт, результат пишется в stdout", idx);

    DWORD count = 0;
    for (;;) {
        DWORD len = 0, seq = 0;
        int r = channel_recv(&chan, parent, line, &len, &seq);
        if (r == 0)
            break;
        if (r == 2) {
            log_msg(tag, "родитель завершился, не закончив передачу - выходим");
            rc = 2;
            break;
        }
        if (r < 0) {
            rc = 1;
            break;
        }

        int n = remove_vowels(line, out);
        log_msg(tag, "строка %lu: \"%s\" -> \"%s\"", seq, line, out);
        out[n++] = '\n';
        if (write_all(out_h, out, (DWORD)n) < 0) {
            rc = 1;
            break;
        }
        count++;
    }

    channel_close(&chan);
    CloseHandle(parent);

    log_msg(tag, "обработано строк: %lu, завершение", count);
    return rc;
}
