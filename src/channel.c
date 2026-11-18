#include "channel.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

static void make_name(char *buf, size_t size, const char *kind, DWORD parent_pid, int idx)
{
    snprintf(buf, size, "Local\\lab4_%s_%lu_%d", kind, parent_pid, idx);
}

static void channel_reset(channel_t *chan)
{
    chan->map = NULL;
    chan->view = NULL;
    chan->ev_ready = NULL;
    chan->ev_free = NULL;
}

int channel_create(channel_t *chan, DWORD parent_pid, int idx)
{
    char name[128];
    channel_reset(chan);

    make_name(name, sizeof(name), "shm", parent_pid, idx);
    chan->map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, (DWORD)sizeof(shm_block_t), name);
    if (chan->map == NULL) {
        print_error("CreateFileMapping");
        return -1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Объект %s уже существует\n", name);
        channel_close(chan);
        return -1;
    }

    chan->view = (shm_block_t *)MapViewOfFile(chan->map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shm_block_t));
    if (chan->view == NULL) {
        print_error("MapViewOfFile");
        channel_close(chan);
        return -1;
    }
    memset(chan->view, 0, sizeof(shm_block_t));

    make_name(name, sizeof(name), "ready", parent_pid, idx);
    chan->ev_ready = CreateEventA(NULL, FALSE, FALSE, name);   // данных пока нет
    if (chan->ev_ready == NULL) {
        print_error("CreateEvent (ready)");
        channel_close(chan);
        return -1;
    }

    make_name(name, sizeof(name), "free", parent_pid, idx);
    chan->ev_free = CreateEventA(NULL, FALSE, TRUE, name);     // блок свободен
    if (chan->ev_free == NULL) {
        print_error("CreateEvent (free)");
        channel_close(chan);
        return -1;
    }

    return 0;
}

int channel_open(channel_t *chan, DWORD parent_pid, int idx)
{
    char name[128];
    channel_reset(chan);

    make_name(name, sizeof(name), "shm", parent_pid, idx);
    chan->map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (chan->map == NULL) {
        print_error("OpenFileMapping");
        return -1;
    }

    chan->view = (shm_block_t *)MapViewOfFile(chan->map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shm_block_t));
    if (chan->view == NULL) {
        print_error("MapViewOfFile");
        channel_close(chan);
        return -1;
    }

    make_name(name, sizeof(name), "ready", parent_pid, idx);
    chan->ev_ready = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (chan->ev_ready == NULL) {
        print_error("OpenEvent (ready)");
        channel_close(chan);
        return -1;
    }

    make_name(name, sizeof(name), "free", parent_pid, idx);
    chan->ev_free = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (chan->ev_free == NULL) {
        print_error("OpenEvent (free)");
        channel_close(chan);
        return -1;
    }

    return 0;
}

void channel_close(channel_t *chan)
{
    if (chan->view != NULL && !UnmapViewOfFile(chan->view))
        print_error("UnmapViewOfFile");
    if (chan->map != NULL && !CloseHandle(chan->map))
        print_error("CloseHandle (map)");
    if (chan->ev_ready != NULL && !CloseHandle(chan->ev_ready))
        print_error("CloseHandle (ready)");
    if (chan->ev_free != NULL && !CloseHandle(chan->ev_free))
        print_error("CloseHandle (free)");
    channel_reset(chan);
}

int channel_send(channel_t *chan, HANDLE peer, const char *data, DWORD len, DWORD seq, LONG done)
{
    if (len >= SHM_DATA_SIZE) {
        fprintf(stderr, "channel_send: строка длиной %lu не помещается в блок\n", len);
        return -1;
    }

    // потомок первым, чтобы не писать строку мертвому процессу
    HANDLE w[2] = { peer, chan->ev_free };
    DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
    if (r == WAIT_OBJECT_0)
        return 1;
    if (r != WAIT_OBJECT_0 + 1) {
        print_error("WaitForMultipleObjects (free)");
        return -1;
    }

    chan->view->done = done;
    chan->view->seq = seq;
    chan->view->len = len;
    if (len > 0)
        memcpy(chan->view->data, data, len);
    chan->view->data[len] = '\0';

    if (!SetEvent(chan->ev_ready)) {
        print_error("SetEvent (ready)");
        return -1;
    }
    return 0;
}

int channel_recv(channel_t *chan, HANDLE peer, char *out, DWORD *len, DWORD *seq)
{
    // событие первым, чтобы дочитать уже переданное
    HANDLE w[2] = { chan->ev_ready, peer };
    DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
    if (r == WAIT_OBJECT_0 + 1)
        return 2;
    if (r != WAIT_OBJECT_0) {
        print_error("WaitForMultipleObjects (ready)");
        return -1;
    }

    LONG done = chan->view->done;
    DWORD n = chan->view->len;
    if (n >= SHM_DATA_SIZE)
        n = SHM_DATA_SIZE - 1;

    *seq = chan->view->seq;
    *len = n;
    memcpy(out, chan->view->data, n);
    out[n] = '\0';

    if (!SetEvent(chan->ev_free)) {
        print_error("SetEvent (free)");
        return -1;
    }
    return done ? 0 : 1;
}
