
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <SDL.h>

#include "simulator/ethernet.h"
#include "gui/udp_transport.h"
#include "microcode/microcode.h"
#include "common/serdes.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Constants. */
#define UDP_PORT                       42424
#define UDP_BUFFER_SIZE                 8192
#define UDP_PACKET_SIZE                 1024

/* Data structures and types. */

/* Internal structure for the UDP transport. */
struct udp_transport_internal {
    int running;                  /* Transport running. */
    SDL_Thread *thread;           /* Receiving thread. */
    SDL_mutex *mutex;             /* Mutex for synchronization between
                                   * threads for receiving packets.
                                   */
};

/* Static function declarations. */
static void trp_reset(void *arg);
static void trp_drop(void *arg);
static int trp_append(void *arg, uint16_t data);
static int trp_send(void *arg);
static int trp_receive(void *arg, size_t *len);
static uint16_t trp_get_data(void *arg);
static size_t trp_has_data(void *arg);
static int receive_thread(void *arg);

/* Functions. */

void udp_transport_initvar(struct udp_transport *utrp)
{
    utrp->sockfd = -1;
    utrp->tx_buf = NULL;
    utrp->rx_buf = NULL;
    utrp->internal = NULL;
}

void udp_transport_destroy(struct udp_transport *utrp)
{
    struct udp_transport_internal *iutrp;

    if (utrp->sockfd >= 0) {
        close(utrp->sockfd);
        utrp->sockfd = -1;
    }

    if (utrp->tx_buf) free((void *) utrp->tx_buf);
    utrp->tx_buf = NULL;

    if (utrp->rx_buf) free((void *) utrp->rx_buf);
    utrp->rx_buf = NULL;

    if (utrp->rx_pbuf) free((void *) utrp->rx_pbuf);
    utrp->rx_pbuf = NULL;

    iutrp = (struct udp_transport_internal *) utrp->internal;
    if (!iutrp) return;

    if (iutrp->mutex) {
        SDL_LockMutex(iutrp->mutex);
        iutrp->running = FALSE;
        SDL_UnlockMutex(iutrp->mutex);
    } else {
        iutrp->running = FALSE;
    }

    if (iutrp->thread) {
        SDL_WaitThread(iutrp->thread, NULL);
    }
    iutrp->thread = NULL;

    if (iutrp->mutex) {
        SDL_DestroyMutex(iutrp->mutex);
    }
    iutrp->mutex = NULL;

    free((void *) iutrp);
    utrp->internal = NULL;
}

int udp_transport_create(struct udp_transport *utrp)
{
    struct udp_transport_internal *iutrp;
    struct sockaddr_in addr;
    struct timeval tv;
    int ret, val;

    udp_transport_initvar(utrp);

    utrp->tx_buf =
        (uint8_t *) malloc(UDP_BUFFER_SIZE * sizeof(uint8_t));
    utrp->rx_buf =
        (uint8_t *) malloc(UDP_BUFFER_SIZE * sizeof(uint8_t));
    utrp->rx_pbuf =
        (uint8_t *) malloc(UDP_PACKET_SIZE * sizeof(uint8_t));

    if (unlikely(!utrp->tx_buf || !utrp->rx_buf || !utrp->rx_pbuf)) {
        report_error("udp_transport: create: memory exhausted");
        udp_transport_destroy(utrp);
        return FALSE;
    }

    iutrp = (struct udp_transport_internal *) malloc(sizeof(*iutrp));
    if (unlikely(!iutrp)) {
        report_error("udp_transport: create: memory exhausted");
        udp_transport_destroy(utrp);
        return FALSE;
    }
    iutrp->running = FALSE;
    iutrp->thread = NULL;
    iutrp->mutex = NULL;

    utrp->internal = iutrp;

    iutrp->mutex = SDL_CreateMutex();
    if (unlikely(!iutrp->mutex)) {
        report_error("udp_transport: create: "
                     "could not create mutex (SDL_Error: %s)",
                     SDL_GetError());
        udp_transport_destroy(utrp);
        return FALSE;
    }

    utrp->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (unlikely(utrp->sockfd < 0)) {
        report_error("udp_transport: create: "
                     "could not create UDP socket: %s",
                     strerror(errno));
        udp_transport_destroy(utrp);
        return FALSE;
    }

    val = 1;
    ret = setsockopt(utrp->sockfd, SOL_SOCKET, SO_REUSEADDR,
                     &val, sizeof(val));
    if (unlikely(ret < 0)) {
        report_error("udp_transport: create: "
                     "could not set SO_REUSEADDR: %s",
                     strerror(errno));
        udp_transport_destroy(utrp);
        return FALSE;
    }

    val = 1;
    ret = setsockopt(utrp->sockfd, SOL_SOCKET, SO_BROADCAST,
                     &val, sizeof(val));
    if (unlikely(ret < 0)) {
        report_error("udp_transport: create: "
                     "could not set SO_BROADCAST: %s",
                     strerror(errno));
        udp_transport_destroy(utrp);
        return FALSE;
    }

    tv.tv_sec = 0;
    tv.tv_usec = 10;
    ret = setsockopt(utrp->sockfd, SOL_SOCKET, SO_RCVTIMEO,
                     &tv, sizeof(tv));
    if (unlikely(ret < 0)) {
        report_error("udp_transport: create: "
                     "could not set SO_RCVTIMEO: %s",
                     strerror(errno));
        udp_transport_destroy(utrp);
        return FALSE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);
    ret = bind(utrp->sockfd, (struct sockaddr *) &addr,
               sizeof(addr));
    if (unlikely(ret < 0)) {
        report_error("udp_transport: create: "
                     "could not bind socket to port %d: %s",
                     UDP_PORT, strerror(errno));
        udp_transport_destroy(utrp);
        return FALSE;
    }

    utrp->tx_pos = 0;
    utrp->rx_pos = 0;
    utrp->rx_len = 0;
    utrp->rx_len_copy = FALSE;

    iutrp->running = TRUE;
    iutrp->thread = SDL_CreateThread(&receive_thread,
                                     "udp_transport_thread", utrp);
    if (unlikely(!iutrp->thread)) {
        report_error("udp_transport: create: "
                     "could not create thread (SDL_Error: %s)",
                     SDL_GetError());
        udp_transport_destroy(utrp);
        return FALSE;
    }

    /* Set-up the inner transport object. */
    utrp->trp.reset = &trp_reset;
    utrp->trp.append = &trp_append;
    utrp->trp.send = &trp_send;
    utrp->trp.receive = &trp_receive;
    utrp->trp.drop = &trp_drop;
    utrp->trp.get_data = &trp_get_data;
    utrp->trp.has_data = &trp_has_data;
    utrp->trp.arg = utrp;

    return TRUE;
}

/* Resets the tranport. */
static
void trp_reset(void *arg)
{
    struct udp_transport *utrp;
    utrp = (struct udp_transport *) arg;
    utrp->tx_pos = 0;
}

/* To append a word to the current packet to be sent.
 * The parameter `data` contains the word to be appended.
 * Returns TRUE on success.
 */
static
int trp_append(void *arg, uint16_t data)
{
    struct udp_transport *utrp;
    utrp = (struct udp_transport *) arg;

    if (utrp->tx_pos == 0) {
        /* Reserve 2 bytes for the message length. */
        utrp->tx_pos = 2;
    }

    if (utrp->tx_pos >= UDP_BUFFER_SIZE) {
        report_error("udp_transport: append: "
                     "buffer overflow");
        return FALSE;
    }

    utrp->tx_buf[utrp->tx_pos++] = (uint8_t) (data >> 8);
    utrp->tx_buf[utrp->tx_pos++] = (uint8_t) data;
    return TRUE;
}

/* To send the current packet.
 * Returns TRUE on success.
 */
static
int trp_send(void *arg)
{
    struct udp_transport *utrp;
    struct sockaddr_in addr;
    uint16_t data;
    int ret;

    utrp = (struct udp_transport *) arg;

    /* Write the length. */
    data = (uint16_t) (utrp->tx_pos >> 1);
    utrp->tx_buf[0] = (uint8_t) (data >> 8);
    utrp->tx_buf[1] = (uint8_t) data;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, "255.255.255.255", &addr.sin_addr);

    ret = sendto(utrp->sockfd,
                 utrp->tx_buf,
                 utrp->tx_pos,
                 0,
                 (const struct sockaddr *) &addr,
                 sizeof(addr));

    if (unlikely(ret < 0)) {
        report_error("udp_transport: send: "
                     "could not send packet: %s",
                     strerror(errno));
        return FALSE;
    }
    utrp->tx_pos = 0;
    return TRUE;
}

/* Receives a packet.
 * The `len` parameter receives the length of the message.
 * Returns TRUE on success.
 */
static
int trp_receive(void *arg, size_t *len)
{
    struct udp_transport *utrp;
    struct udp_transport_internal *iutrp;
    int ret;

    utrp = (struct udp_transport *) arg;
    iutrp = (struct udp_transport_internal *) utrp->internal;

    if (utrp->rx_len_copy == 0) {
        ret = SDL_LockMutex(iutrp->mutex);
        if (unlikely(ret != 0)) {
            report_error("udp_transport: receive: "
                         "could no acquire lock (SDLError(%d): %s)",
                         ret, SDL_GetError());
            return 1;
        }
        utrp->rx_len_copy = utrp->rx_len;
        SDL_UnlockMutex(iutrp->mutex);
    }

    if (len) {
        *len = utrp->rx_len_copy;
    }

    return TRUE;
}

/* When done receiving a packet. */
static
void trp_drop(void *arg)
{
    struct udp_transport *utrp;
    struct udp_transport_internal *iutrp;
    int ret;

    utrp = (struct udp_transport *) arg;
    iutrp = (struct udp_transport_internal *) utrp->internal;

    utrp->rx_pos = 0;
    utrp->rx_len_copy = 0;

    ret = SDL_LockMutex(iutrp->mutex);
    if (unlikely(ret != 0)) {
        report_error("udp_transport: drop: "
                     "could no acquire lock (SDLError(%d): %s)",
                     ret, SDL_GetError());
        return;
    }
    utrp->rx_len = 0;
    SDL_UnlockMutex(iutrp->mutex);
}

/* Gets the data of the current packet.
 * Returns the current data (or zero if no data).
 */
static
uint16_t trp_get_data(void *arg)
{
    struct udp_transport *utrp;
    uint16_t data;

    utrp = (struct udp_transport *) arg;
    if (utrp->rx_pos >= utrp->rx_len_copy) return 0;

    data = (uint16_t) utrp->rx_buf[utrp->rx_pos++];
    data <<= 8;
    data |= (uint16_t) utrp->rx_buf[utrp->rx_pos++];
    return data;
}

/* Checks if there is still remaining data on the current received
 * packet. Returns the number of remaining bytes.
 */
static
size_t trp_has_data(void *arg)
{
    struct udp_transport *utrp;

    utrp = (struct udp_transport *) arg;
    if (utrp->rx_pos >= utrp->rx_len_copy) return 0;
    return (utrp->rx_len_copy - utrp->rx_pos);
}

/* Thread to receive packets. */
static
int receive_thread(void *arg)
{
    struct udp_transport *utrp;
    struct udp_transport_internal *iutrp;
    size_t rx_len, len, packet_len;
    int ret, running;
    ssize_t s;

    utrp = (struct udp_transport *) arg;
    iutrp = (struct udp_transport_internal *) utrp->internal;

    while (TRUE) {
        ret = SDL_LockMutex(iutrp->mutex);
        if (unlikely(ret != 0)) {
            report_error("udp_transport: receive_thread: "
                         "could no acquire lock (SDLError(%d): %s)",
                         ret, SDL_GetError());
            return 1;
        }
        running = iutrp->running;
        rx_len = utrp->rx_len;
        SDL_UnlockMutex(iutrp->mutex);

        if (!running) break;

        if (rx_len != 0) {
            SDL_Delay(1);
            continue;
        }

        s = recvfrom(utrp->sockfd,
                     utrp->rx_pbuf,
                     /* Two extra bytes for the fake checksum,
                      * which is not sent.
                      */
                     UDP_PACKET_SIZE,
                     0, NULL, NULL);

        if (s < 0) {
            if (errno != EAGAIN) {
                report_error("udp_transport: receive_thread: "
                             "could not receive packet: %s",
                             strerror(errno));
                return 1;
            }
        }
        if (s <= 0) {
            SDL_Delay(1);
            continue;
        }

        len = (size_t) s;

        packet_len = (size_t) utrp->rx_pbuf[0];
        packet_len <<= 8;
        packet_len |= (size_t) utrp->rx_pbuf[1];

        packet_len *= 2; /* Convert to bytes. */
        if ((packet_len > len) || ((packet_len % 2) != 0)) {
            report_error("udp_transport: receive: "
                         "invalid packet length: %zu (%zu)",
                         packet_len, len);
            return 1;
        }

        if (packet_len < len) {
            /* Discard the extra remaining of the packet. */
            len = packet_len;
        }

        ret = SDL_LockMutex(iutrp->mutex);
        if (unlikely(ret != 0)) {
            report_error("udp_transport: receive_thread: "
                         "could no acquire lock (SDLError(%d): %s)",
                         ret, SDL_GetError());
            return 1;
        }
        utrp->rx_len = len + 2; /* For the fake checksum. */
        memcpy(utrp->rx_buf, utrp->rx_pbuf, len);
        SDL_UnlockMutex(iutrp->mutex);
    }

    return 0;
}
