#ifndef CHANNEL_H
#define CHANNEL_H

#include <windows.h>

#define SHM_DATA_SIZE 4096

typedef struct {
    LONG  done;   // 1 - данных больше не будет
    DWORD seq;
    DWORD len;
    char  data[SHM_DATA_SIZE];
} shm_block_t;

typedef struct {
    HANDLE       map;
    shm_block_t *view;
    HANDLE       ev_ready;
    HANDLE       ev_free;
} channel_t;

int channel_create(channel_t *chan, DWORD parent_pid, int idx);
int channel_open(channel_t *chan, DWORD parent_pid, int idx);
void channel_close(channel_t *chan);

// 0 если все ок, -1 ошибка, 1 если потомок peer уже завершился
int channel_send(channel_t *chan, HANDLE peer, const char *data, DWORD len, DWORD seq, LONG done);

// 1 строка, 0 конец данных, -1 ошибка, 2 если родитель peer завершился
int channel_recv(channel_t *chan, HANDLE peer, char *out, DWORD *len, DWORD *seq);

#endif
