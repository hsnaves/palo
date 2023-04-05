
#ifndef __GUI_UDP_TRANSPORT_H
#define __GUI_UDP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */

/* The ethernet controller for the simulator. */
struct udp_transport {
    int sockfd;                   /* UDP socket for transport. */
    uint8_t *tx_buf;              /* Buffer to transmit UDP packets. */
    uint8_t *rx_buf;              /* Buffer to receive UDP packets. */
    uint8_t *rx_pbuf;             /* Buffer for one packet. */
    size_t tx_pos;                /* Position in the UDP tx buffer. */
    size_t rx_pos;                /* Position in the UDP rx buffer. */
    size_t rx_len;                /* Length of the UDP rx buffer. */
    size_t rx_len_copy;           /* Thread-safe version of rx_len. */

    struct transport trp;         /* The populated transport structure. */
    void *internal;               /* Opaque internal structure. */
};

/* Functions. */

/* Initializes the utrp variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void udp_transport_initvar(struct udp_transport *utrp);

/* Destroys the utrp object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void udp_transport_destroy(struct udp_transport *utrp);

/* Creates a new utrp object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int udp_transport_create(struct udp_transport *utrp);


#endif /* __GUI_UDP_TRANSPORT_H */
