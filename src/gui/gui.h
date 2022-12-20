
#ifndef __GUI_GUI_H
#define __GUI_GUI_H

#include "simulator/keyboard.h"
#include "simulator/mouse.h"

/* Data structures and types. */
/* A structure representing the user interface. */
struct gui {
    void *internal;               /* Opaque internal structure. */
};

/* Functions. */

/* Initializes the gui variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void gui_initvar(struct gui *ui);

/* Destroys the gui object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void gui_destroy(struct gui *ui);

/* Creates a new gui object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int gui_create(struct gui *ui);

/* Starts the user interface.
 * Returns TRUE on success.
 */
int gui_start(struct gui *ui);

/* Stops the user interface (destroy the main window). */
void gui_stop(struct gui *ui);

/* Updates the user interface.
 * The new pixels are given by the `display_data` array, that should
 * come directly from the simulator display. The `keyb` and `mous`
 * are the keyboard and mouse to updated with the keyboard events and
 * mouse events from the GUI.
 * Returns TRUE on success.
 */
int gui_update(struct gui *ui, const uint8_t *display_data,
               struct keyboard *keyb, struct mouse *mous);


#endif /* __GUI_GUI_H */
