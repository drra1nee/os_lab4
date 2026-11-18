#include "channel.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

static void make_name(char *buf, size_t size, const char *kind, DWORD parent_pid, int idx)
{
    snprintf(buf, size, "Local\\lab4_%s_%lu_%d", kind, parent_pid, idx);
}

static void channel_reset(channel_t *ch)
{
    ch->map = NULL;
    ch->view = NULL;
    ch->ev_ready = NULL;
    ch->ev_free = NULL;
}

int channel_create(channel_t *ch, DWORD parent_pid, int idx)
{
    char name[128];
    channel_reset(ch);

    make_name(name, sizeof(name), "shm", parent_pid, idx);
    ch->map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, (DWORD)sizeof(shm_block_t), name);
    if (ch->map == NULL) {
        print_error("CreateFileMapping");
        return -1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Объект %s уже существует\n", name);
        channel_close(ch);
        return -1;
    }

    ch->view = (shm_block_t *)MapViewOfFile(ch->map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shm_block_t));
    if (ch->view == NULL) {
        print_error("MapViewOfFile");
        channel_close(ch);
        return -1;
    }
    memset(ch->view, 0, sizeof(shm_block_t));

    make_name(name, sizeof(name), "ready", parent_pid, idx);
    ch->ev_ready = CreateEventA(NULL, FALSE, FALSE, name);   // данных пока нет
    if (ch->ev_ready == NULL) {
        print_error("CreateEvent (ready)");
        channel_close(ch);
        return -1;
    }

    make_name(name, sizeof(name), "free", parent_pid, idx);
    ch->ev_free = CreateEventA(NULL, FALSE, TRUE, name);     // блок свободен
    if (ch->ev_free == NULL) {
        print_error("CreateEvent (free)");
        channel_close(ch);
        return -1;
    }

    return 0;
}

int channel_open(channel_t *ch, DWORD parent_pid, int idx)
{
    char name[128];
    channel_reset(ch);

    make_name(name, sizeof(name), "shm", parent_pid, idx);
    ch->map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (ch->map == NULL) {
        print_error("OpenFileMapping");
        return -1;
    }

    ch->view = (shm_block_t *)MapViewOfFile(ch->map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shm_block_t));
    if (ch->view == NULL) {
        print_error("MapViewOfFile");
        channel_close(ch);
        return -1;
    }

    make_name(name, sizeof(name), "ready", parent_pid, idx);
    ch->ev_ready = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (ch->ev_ready == NULL) {
        print_error("OpenEvent (ready)");
        channel_close(ch);
        return -1;
    }

    make_name(name, sizeof(name), "free", parent_pid, idx);
    ch->ev_free = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (ch->ev_free == NULL) {
        print_error("OpenEvent (free)");
        channel_close(ch);
        return -1;
    }

    return 0;
}

void channel_close(channel_t *ch)
{
    if (ch->view != NULL && !UnmapViewOfFile(ch->view))
        print_error("UnmapViewOfFile");
    if (ch->map != NULL && !CloseHandle(ch->map))
        print_error("CloseHandle (map)");
    if (ch->ev_ready != NULL && !CloseHandle(ch->ev_ready))
        print_error("CloseHandle (ready)");
    if (ch->ev_free != NULL && !CloseHandle(ch->ev_free))
        print_error("CloseHandle (free)");
    channel_reset(ch);
}

int channel_send(channel_t *ch, HANDLE peer, const char *data, DWORD len, DWORD seq, LONG done)
{
    if (len >= SHM_DATA_SIZE) {
        fprintf(stderr, "channel_send: строка длиной %lu не помещается в блок\n", len);
        return -1;
    }

    // потомок первым, чтобы не писать строку мертвому процессу
    HANDLE w[2] = { peer, ch->ev_free };
    DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
    if (r == WAIT_OBJECT_0)
        return 1;
    if (r != WAIT_OBJECT_0 + 1) {
        print_error("WaitForMultipleObjects (free)");
        return -1;
    }

    ch->view->done = done;
    ch->view->seq = seq;
    ch->view->len = len;
    if (len > 0)
        memcpy(ch->view->data, data, len);
    ch->view->data[len] = '\0';

    if (!SetEvent(ch->ev_ready)) {
        print_error("SetEvent (ready)");
        return -1;
    }
    return 0;
}

int channel_recv(channel_t *ch, HANDLE peer, char *out, DWORD *len, DWORD *seq)
{
    // событие первым, чтобы дочитать уже переданное
    HANDLE w[2] = { ch->ev_ready, peer };
    DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
    if (r == WAIT_OBJECT_0 + 1)
        return 2;
    if (r != WAIT_OBJECT_0) {
        print_error("WaitForMultipleObjects (ready)");
        return -1;
    }

    LONG done = ch->view->done;
    DWORD n = ch->view->len;
    if (n >= SHM_DATA_SIZE)
        n = SHM_DATA_SIZE - 1;

    *seq = ch->view->seq;
    *len = n;
    memcpy(out, ch->view->data, n);
    out[n] = '\0';

    if (!SetEvent(ch->ev_free)) {
        print_error("SetEvent (free)");
        return -1;
    }
    return done ? 0 : 1;
}
