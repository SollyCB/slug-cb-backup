#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "defs.h"

enum {
    RB_NOTAIL = 0x01,
};

typedef struct ringbuffer {
    uchar *data;
    uint flags;
    int size;
    int head;
    int tail;
} ringbuffer;

bool init_ringbuffer(ringbuffer *buf, int size, uint flags);
void* rballoc(ringbuffer *rb, int size);
bool rbfree(ringbuffer *rb, int size);

#endif // include guard
