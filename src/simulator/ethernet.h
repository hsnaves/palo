
#ifndef __SIMULATOR_ETHERNET_H
#define __SIMULATOR_ETHERNET_H

#include <stdint.h>

/* Data structures and types. */
struct ethernet {
    uint16_t *fifo_buffer;
};

/* Functions. */

/* Initializes the ethernet variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void ethernet_initvar(struct ethernet *e);

/* Destroys the ethernet object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void ethernet_destroy(struct ethernet *e);

/* Creates a new ethernet object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int ethernet_create(struct ethernet *e);


#endif /* __SIMULATOR_ETHERNET_H */
