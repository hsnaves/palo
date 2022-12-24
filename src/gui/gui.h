
#ifndef __GUI_GUI_H
#define __GUI_GUI_H

#include "simulator/simulator.h"

/* Data structures and types. */

/* A structure representing the user interface. */

/* Callback to run as a separate thread in gui_start(). */
struct gui;
typedef int (*gui_thread_cb)(struct gui *ui);

struct gui {
    struct simulator *sim;        /* Reference to the simulator. */
    void *internal;               /* Opaque internal structure. */
    gui_thread_cb thread_cb;      /* The callback for the thread. */
    void *arg;                    /* Extra argument passed to used
                                   * by the thread.
                                   */
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
 * The parameter `sim` is a reference to the simulator.
 * The parameter `thread_cb` is a callback to be run in a separate thread,
 * and the argument `arg` is an extra argument to be used by this thread
 * (via ui->arg). If `thread_cb` is NULL, no separate thread is created.
 * Returns TRUE on success.
 */
int gui_create(struct gui *ui, struct simulator *sim,
               gui_thread_cb thread_cb, void *arg);

/* Starts the user interface.
 * Returns TRUE on success.
 */
int gui_start(struct gui *ui);

/* Stops the user interface (destroys the main window).
 * Returns TRUE on success.
 */
int gui_stop(struct gui *ui);

/* Checks if the user interface is running.
 * The parameter `running` returns TRUE if the interface is running.
 * Returns TRUE on success.
 */
int gui_running(struct gui *ui, int *running);

/* Updates the user interface.
 * Returns TRUE on success.
 */
int gui_update(struct gui *ui);

/* Waits until the next frame is drawn.
 * Returns TRUE on success.
 */
int gui_wait_frame(struct gui *ui);


#endif /* __GUI_GUI_H */
