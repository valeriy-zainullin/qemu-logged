/*
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "net/queue.h"
#include "qemu/queue.h"
#include "net/net.h"

/* The delivery handler may only return zero if it will call
 * qemu_net_queue_flush() when it determines that it is once again able
 * to deliver packets. It must also call qemu_net_queue_purge() in its
 * cleanup path.
 *
 * If a sent callback is provided to send(), the caller must handle a
 * zero return from the delivery handler by not sending any more packets
 * until we have invoked the callback. Only in that case will we queue
 * the packet.
 *
 * If a sent callback isn't provided, we just drop the packet to avoid
 * unbounded queueing.
 */

struct NetPacket {
    QTAILQ_ENTRY(NetPacket) entry;
    NetClientState *sender;
    unsigned flags;
    int size;
    NetPacketSent *sent_cb;
    uint8_t data[];
};

struct NetQueue {
    void *opaque;
    uint32_t nq_maxlen;
    uint32_t nq_count;
    NetQueueDeliverFunc *deliver;

    QTAILQ_HEAD(, NetPacket) packets;

    unsigned delivering : 1;
};

NetQueue *qemu_new_net_queue(NetQueueDeliverFunc *deliver, void *opaque)
{
    NetQueue *queue;

    queue = g_new0(NetQueue, 1);

    queue->opaque = opaque;
    queue->nq_maxlen = 10000;
    queue->nq_count = 0;
    queue->deliver = deliver;

    QTAILQ_INIT(&queue->packets);

    queue->delivering = 0;

    return queue;
}

void qemu_del_net_queue(NetQueue *queue)
{
    NetPacket *packet, *next;

    QTAILQ_FOREACH_SAFE(packet, &queue->packets, entry, next) {
        QTAILQ_REMOVE(&queue->packets, packet, entry);
        g_free(packet);
    }

    g_free(queue);
}

static void qemu_net_queue_append(NetQueue *queue,
                                  NetClientState *sender,
                                  unsigned flags,
                                  const uint8_t *buf,
                                  size_t size,
                                  NetPacketSent *sent_cb)
{
    NetPacket *packet;

    if (queue->nq_count >= queue->nq_maxlen && !sent_cb) {
        return; /* drop if queue full and no callback */
    }
    packet = g_malloc(sizeof(NetPacket) + size);
    packet->sender = sender;
    packet->flags = flags;
    packet->size = size;
    packet->sent_cb = sent_cb;
    memcpy(packet->data, buf, size);

    queue->nq_count++;
    QTAILQ_INSERT_TAIL(&queue->packets, packet, entry);
}

void qemu_net_queue_append_iov(NetQueue *queue,
                               NetClientState *sender,
                               unsigned flags,
                               const struct iovec *iov,
                               int iovcnt,
                               NetPacketSent *sent_cb)
{
    NetPacket *packet;
    size_t max_len = 0;
    int i;

    if (queue->nq_count >= queue->nq_maxlen && !sent_cb) {
        return; /* drop if queue full and no callback */
    }
    for (i = 0; i < iovcnt; i++) {
        max_len += iov[i].iov_len;
    }

    packet = g_malloc(sizeof(NetPacket) + max_len);
    packet->sender = sender;
    packet->sent_cb = sent_cb;
    packet->flags = flags;
    packet->size = 0;

    for (i = 0; i < iovcnt; i++) {
        size_t len = iov[i].iov_len;

        memcpy(packet->data + packet->size, iov[i].iov_base, len);
        packet->size += len;
    }

    queue->nq_count++;
    QTAILQ_INSERT_TAIL(&queue->packets, packet, entry);
}

static ssize_t qemu_net_queue_deliver(NetQueue *queue,
                                      NetClientState *sender,
                                      unsigned flags,
                                      const uint8_t *data,
                                      size_t size)
{
    printf("QEMU mod: qemu_net_queue_deliver called.\n");

    #if !defined(RETURN_ADDR_OFFSET_PRINT)
        #define RETURN_ADDR_OFFSET_PRINT(func) \
        { \
            int64_t value = (int64_t) (uint64_t) __builtin_return_address(0) - (int64_t) (uint64_t) func; \
            if (value < 0) { \
                printf("QEMU mod: %s, return_address - func_addr = -0x%llx.\n", __func__, (unsigned long long) (-value)); \
            } else { \
                printf("QEMU mod: %s, return_address - func_addr = 0x%llx.\n", __func__, (unsigned long long) value); \
            } \
        }
    #endif
    RETURN_ADDR_OFFSET_PRINT(qemu_net_queue_deliver);

    ssize_t ret = -1;
    struct iovec iov = {
        .iov_base = (void *)data,
        .iov_len = size
    };

    queue->delivering = 1;
    printf("QEMU mod: queue->deliver = %p, qemu_net_queue_deliver = %p.\n", queue->deliver, qemu_net_queue_deliver);
    ret = queue->deliver(sender, flags, &iov, 1, queue->opaque);
    queue->delivering = 0;

    return ret;
}

static ssize_t qemu_net_queue_deliver_iov(NetQueue *queue,
                                          NetClientState *sender,
                                          unsigned flags,
                                          const struct iovec *iov,
                                          int iovcnt)
{
    ssize_t ret = -1;

    queue->delivering = 1;
    ret = queue->deliver(sender, flags, iov, iovcnt, queue->opaque);
    queue->delivering = 0;

    return ret;
}

ssize_t qemu_net_queue_receive(NetQueue *queue,
                               const uint8_t *data,
                               size_t size)
{
    if (queue->delivering) {
        return 0;
    }

    return qemu_net_queue_deliver(queue, NULL, 0, data, size);
}

ssize_t qemu_net_queue_receive_iov(NetQueue *queue,
                                   const struct iovec *iov,
                                   int iovcnt)
{
    if (queue->delivering) {
        return 0;
    }

    return qemu_net_queue_deliver_iov(queue, NULL, 0, iov, iovcnt);
}

ssize_t qemu_net_queue_send(NetQueue *queue,
                            NetClientState *sender,
                            unsigned flags,
                            const uint8_t *data,
                            size_t size,
                            NetPacketSent *sent_cb)
{
    printf("QEMU mod: qemu_net_queue_send called.\n");

    #if !defined(RETURN_ADDR_OFFSET_PRINT)
        #define RETURN_ADDR_OFFSET_PRINT(func) \
        { \
            int64_t value = (int64_t) (uint64_t) __builtin_return_address(0) - (int64_t) (uint64_t) func; \
            if (value < 0) { \
                printf("QEMU mod: %s, return_address - func_addr = -0x%llx.\n", __func__, (unsigned long long) (-value)); \
            } else { \
                printf("QEMU mod: %s, return_address - func_addr = 0x%llx.\n", __func__, (unsigned long long) value); \
            } \
        }
    #endif
    RETURN_ADDR_OFFSET_PRINT(qemu_net_queue_send);

    {
        printf("QEMU mod: qemu_net_queue_send, content = \"");
        char* buffer = (char*) data;
        size_t buffer_size = size;
        for (size_t i = 0; i < buffer_size; ++i) {
            if (('a' <= buffer[i] && buffer[i] <= 'z') || ('A' <= buffer[i] && buffer[i] <= 'Z') || ('0' <= buffer[i] && buffer[i] <= '9')) {
                printf("%c", buffer[i]);
            } else {
                printf("\\%02x", (unsigned) (* (uint8_t*) &buffer[i]));                
            }
        }
        printf("\".\n");
    }


    ssize_t ret;

    if (queue->delivering || !qemu_can_send_packet(sender)) {
        printf("QEMU mod: qemu_net_queue_send #1 taken.\n");
        qemu_net_queue_append(queue, sender, flags, data, size, sent_cb);
        return 0;
    }

    printf("QEMU mod: qemu_net_queue_send #2 taken.\n");
    ret = qemu_net_queue_deliver(queue, sender, flags, data, size);
    if (ret == 0) {
        printf("QEMU mod: qemu_net_queue_send #3 taken.\n");
        qemu_net_queue_append(queue, sender, flags, data, size, sent_cb);
        return 0;
    }

    qemu_net_queue_flush(queue);

    return ret;
}

ssize_t qemu_net_queue_send_iov(NetQueue *queue,
                                NetClientState *sender,
                                unsigned flags,
                                const struct iovec *iov,
                                int iovcnt,
                                NetPacketSent *sent_cb)
{
    ssize_t ret;

    if (queue->delivering || !qemu_can_send_packet(sender)) {
        qemu_net_queue_append_iov(queue, sender, flags, iov, iovcnt, sent_cb);
        return 0;
    }

    ret = qemu_net_queue_deliver_iov(queue, sender, flags, iov, iovcnt);
    if (ret == 0) {
        qemu_net_queue_append_iov(queue, sender, flags, iov, iovcnt, sent_cb);
        return 0;
    }

    qemu_net_queue_flush(queue);

    return ret;
}

void qemu_net_queue_purge(NetQueue *queue, NetClientState *from)
{
    NetPacket *packet, *next;

    QTAILQ_FOREACH_SAFE(packet, &queue->packets, entry, next) {
        if (packet->sender == from) {
            QTAILQ_REMOVE(&queue->packets, packet, entry);
            queue->nq_count--;
            if (packet->sent_cb) {
                packet->sent_cb(packet->sender, 0);
            }
            g_free(packet);
        }
    }
}

bool qemu_net_queue_flush(NetQueue *queue)
{

    printf("QEMU mod: qemu_net_queue_flush called.\n");
    if (queue->delivering) {
        printf("QEMU mod: qemu_net_queue_flush #1 taken.\n");
        return false;
    }

    size_t index = 0;
    while (!QTAILQ_EMPTY(&queue->packets)) {
        NetPacket *packet;
        int ret;

        ++index;
        printf("QEMU mod: qemu_net_queue_flush iteration #%zu.\n", index);

        packet = QTAILQ_FIRST(&queue->packets);
        QTAILQ_REMOVE(&queue->packets, packet, entry);
        queue->nq_count--;

        ret = qemu_net_queue_deliver(queue,
                                     packet->sender,
                                     packet->flags,
                                     packet->data,
                                     packet->size);
        if (ret == 0) {
            queue->nq_count++;
            QTAILQ_INSERT_HEAD(&queue->packets, packet, entry);
            return false;
        }

        if (packet->sent_cb) {
            packet->sent_cb(packet->sender, ret);
        }

        g_free(packet);
    }
    return true;
}
