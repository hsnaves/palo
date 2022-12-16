
#ifndef __GUI_GUI_H
#define __GUI_GUI_H

/* Data structures and types. */
/* A structure representing the user interface. */
struct gui {
    int running;                  /* User interface is running. */
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

/* Runs the gui event loop.
 * Returns TRUE on success.
 */
int gui_run(struct gui *ui);


#endif /* __GUI_GUI_H */
