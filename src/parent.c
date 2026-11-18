#include "channel.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHILDREN      2
#define WAIT_EXIT_MS  10000

typedef struct {
    const char         *exe;
    char                file[MAX_PATH];
    channel_t           chan;
    PROCESS_INFORMATION pi;
    int                 alive;
} child_t;

static int to_wide(const char *src, wchar_t *dst, int size) {
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, size) == 0) {
        print_error("MultiByteToWideChar");
        return -1;
    }
    return 0;
}

static void make_stderr_inheritable(void) {
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != NULL && h != INVALID_HANDLE_VALUE &&
        !SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        print_error("SetHandleInformation");
}

// файл открывает родитель и отдает потомку как stdout
static int spawn_child(child_t *c, int idx) {
    wchar_t wexe[MAX_PATH], wfile[MAX_PATH], cmd[2 * MAX_PATH];
    STARTUPINFOW si;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    if (to_wide(c->exe, wexe, MAX_PATH) < 0 || to_wide(c->file, wfile, MAX_PATH) < 0)
        return -1;
    swprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"%ls %lu %d",
             wexe, GetCurrentProcessId(), idx);

    HANDLE file = CreateFileW(wfile, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        print_error("CreateFile");
        return -1;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = file;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    ZeroMemory(&c->pi, sizeof(c->pi));

    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &c->pi);
    if (!ok)
        print_error("CreateProcess");

    if (!CloseHandle(file))
        print_error("CloseHandle (file)");
    if (!ok)
        return -1;
    c->alive = 1;
    return 0;
}

static int finish_children(child_t *children, int n) {
    HANDLE procs[CHILDREN];
    int cnt = 0, rc = 0;

    for (int i = 0; i < n; i++) {
        if (children[i].pi.hProcess == NULL)
            continue;
        if (children[i].alive) {
            int r = channel_send(&children[i].chan, children[i].pi.hProcess, "", 0, 0, 1);
            if (r == 1)
                log_msg("PARENT", "%s завершился раньше сигнала об окончании данных", children[i].exe);
            if (r != 0)
                rc = -1;
        }
        procs[cnt++] = children[i].pi.hProcess;
    }

    if (cnt > 0) {
        DWORD r = WaitForMultipleObjects((DWORD)cnt, procs, TRUE, WAIT_EXIT_MS);
        if (r == WAIT_TIMEOUT) {
            log_msg("PARENT", "потомки не завершились за %d мс, принудительное завершение", WAIT_EXIT_MS);
            for (int i = 0; i < cnt; i++)
                if (!TerminateProcess(procs[i], 3) && GetLastError() != ERROR_ACCESS_DENIED)
                    print_error("TerminateProcess");
            if (WaitForMultipleObjects((DWORD)cnt, procs, TRUE, INFINITE) == WAIT_FAILED)
                print_error("WaitForMultipleObjects");
            rc = -1;
        } else if (r == WAIT_FAILED) {
            print_error("WaitForMultipleObjects");
            rc = -1;
        }
    }

    for (int i = 0; i < n; i++) {
        if (children[i].pi.hProcess != NULL) {
            DWORD code = 0;
            if (!GetExitCodeProcess(children[i].pi.hProcess, &code)) {
                print_error("GetExitCodeProcess");
                rc = -1;
            } else {
                log_msg("PARENT", "%s (PID %lu) завершился с кодом %lu",
                        children[i].exe, children[i].pi.dwProcessId, code);
                if (code != 0)
                    rc = -1;
            }
            CloseHandle(children[i].pi.hThread);
            CloseHandle(children[i].pi.hProcess);
            children[i].pi.hThread = children[i].pi.hProcess = NULL;
        }
        channel_close(&children[i].chan);
    }
    return rc;
}

static int ask_file(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fflush(stdout);
    int n = read_line(buf, size);
    if (n == -1) {
        fprintf(stderr, "Ввод закончился раньше, чем было введено имя файла\n");
        return -1;
    }
    if (n == -2) {
        fprintf(stderr, "Слишком длинное имя файла\n");
        return -1;
    }
    if (n == 0) {
        fprintf(stderr, "Имя файла не может быть пустым\n");
        return -1;
    }
    if (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_CHAR) {
        printf("%s\n", buf);
        fflush(stdout);
    }
    return 0;
}

int main(void) {
    static char line[SHM_DATA_SIZE];
    child_t children[CHILDREN] = { { .exe = "child1.exe" }, { .exe = "child2.exe" } };
    int rc = 0;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    log_msg("PARENT", "лабораторная работа 4, вариант 18 (file mapping + события)");

    if (ask_file("Имя файла для child1: ", children[0].file, MAX_PATH) < 0 ||
        ask_file("Имя файла для child2: ", children[1].file, MAX_PATH) < 0)
        return EXIT_FAILURE;
    if (_stricmp(children[0].file, children[1].file) == 0) {
        fprintf(stderr, "Файлы для child1 и child2 должны различаться\n");
        return EXIT_FAILURE;
    }

    make_stderr_inheritable();

    for (int i = 0; i < CHILDREN; i++) {
        if (channel_create(&children[i].chan, GetCurrentProcessId(), i + 1) < 0) {
            finish_children(children, CHILDREN);
            return EXIT_FAILURE;
        }
    }
    log_msg("PARENT", "созданы 2 отображения по %zu байт и 4 события", sizeof(shm_block_t));

    for (int i = 0; i < CHILDREN; i++) {
        if (spawn_child(&children[i], i + 1) < 0) {
            finish_children(children, CHILDREN);
            return EXIT_FAILURE;
        }
        log_msg("PARENT", "запущен %s (PID %lu), файл '%s'",
                children[i].exe, children[i].pi.dwProcessId, children[i].file);
    }

    printf("Вводите строки (пустая строка или конец ввода - завершение):\n");
    fflush(stdout);

    DWORD seq = 0, sent = 0;
    for (;;) {
        int n = read_line(line, sizeof(line));
        if (n == -1 || n == 0)
            break;
        if (n == -2) {
            fprintf(stderr, "Строка длиннее %d байт пропущена\n", SHM_DATA_SIZE - 1);
            continue;
        }

        seq++;
        child_t *c = (seq % 2 != 0) ? &children[0] : &children[1];
        log_msg("PARENT", "строка %lu (%s) -> %s", seq,
                (seq % 2 != 0) ? "нечётная" : "чётная", c->exe);

        int r = channel_send(&c->chan, c->pi.hProcess, line, (DWORD)n, seq, 0);
        if (r == 1) {
            log_msg("PARENT", "ошибка: %s (PID %lu) завершился раньше времени, ввод прекращён",
                    c->exe, c->pi.dwProcessId);
            c->alive = 0;
            rc = -1;
            break;
        }
        if (r < 0) {
            rc = -1;
            break;
        }
        sent++;
    }

    log_msg("PARENT", "ввод завершён, передано строк: %lu", sent);
    if (finish_children(children, CHILDREN) < 0)
        rc = -1;
    log_msg("PARENT", rc == 0 ? "работа завершена успешно" : "работа завершена с ошибкой");
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
