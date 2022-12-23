
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "gui/gui.h"
#include "simulator/display.h"
#include "simulator/keyboard.h"
#include "simulator/mouse.h"
#include "common/utils.h"

/* Data structures and types. */

/* Internal structure for the user interface. */
struct gui_internal {
    int initialized;              /* If this structure was initialized. */

    uint8_t *display_data;        /* The display pixels. */
    struct keyboard keyb;         /* The (fake) keyboard. */
    struct mouse mous;            /* The (fake) mouse. */
    SDL_mutex *mutex;             /* Mutex for synchronization between
                                   * threads of the keyboard, mouse
                                   * and the screen pixels.
                                   */
    SDL_cond *frame_cond;         /* A condition that signals
                                   * that a new frame is being drawn.
                                   */

    SDL_Window *window;           /* The interface window. */
    SDL_Renderer *renderer;       /* The renderer for the window. */
    SDL_Texture *texture;         /* The texture. */
    int mouse_captured;           /* Mouse is captured. */
    int skip_next_mouse_move;     /* To skip the next mouse move event. */
};

/* Global variables. */
static int gui_ref_count = 0;    /* A counter to keep track of the
                                  * number of alive gui objects.
                                  * Where there are no more such objects,
                                  * SDL_Quit() is invoked.
                                  */

/* Functions. */

/* To capture the mouse movements (and keyboard).
 * The `capture` indicates whether we should capture or release
 * the mouse movements.
 */
static
void gui_capture_mouse(struct gui *ui, int capture)
{
    struct gui_internal *iui;

    iui = (struct gui_internal *) ui->internal;
    if (capture) {
        SDL_ShowCursor(0);
        SDL_SetWindowGrab(iui->window, SDL_TRUE);
        SDL_SetWindowTitle(iui->window,
                           "PALOS - Mouse captured. Press 'Alt' to release.");
    } else {
        SDL_ShowCursor(1);
        SDL_SetWindowGrab(iui->window, SDL_FALSE);
        SDL_SetWindowTitle(iui->window, "PALOS");
    }

    iui->mouse_captured = capture;
}

/* Processes one SDL event. */
static
void gui_process_event(struct gui *ui, SDL_Event *e)
{
    struct gui_internal *iui;
    enum alto_button btn;
    enum alto_key key;
    int mx, my;

    iui = (struct gui_internal *) ui->internal;
    if (SDL_LockMutex(iui->mutex) != 0) return;

    mx = DISPLAY_WIDTH / 2;
    my = DISPLAY_HEIGHT / 2;

    switch (e->type) {
    case SDL_MOUSEMOTION:
        mouse_move(&iui->mous,
                   e->motion.x - mx,
                   e->motion.y - my);
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            btn = AB_BTN_LEFT;
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            btn = AB_BTN_RIGHT;
        } else if (e->button.button == SDL_BUTTON_MIDDLE) {
            btn = AB_BTN_MIDDLE;
        } else {
            btn = AB_NONE;
        }

        if (e->type == SDL_MOUSEBUTTONDOWN) {
            mouse_press_button(&iui->mous, btn);
        } else {
            mouse_release_button(&iui->mous, btn);
        }
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP:

        btn = AB_NONE;

        switch (e->key.keysym.sym) {
        case SDLK_0: key = AK_0; break;
        case SDLK_1: key = AK_1; break;
        case SDLK_2: key = AK_2; break;
        case SDLK_3: key = AK_3; break;
        case SDLK_4: key = AK_4; break;
        case SDLK_5: key = AK_5; break;
        case SDLK_6: key = AK_6; break;
        case SDLK_7: key = AK_7; break;
        case SDLK_8: key = AK_8; break;
        case SDLK_9: key = AK_9; break;
        case SDLK_a: key = AK_A; break;
        case SDLK_b: key = AK_B; break;
        case SDLK_c: key = AK_C; break;
        case SDLK_d: key = AK_D; break;
        case SDLK_e: key = AK_E; break;
        case SDLK_f: key = AK_F; break;
        case SDLK_g: key = AK_G; break;
        case SDLK_h: key = AK_H; break;
        case SDLK_i: key = AK_I; break;
        case SDLK_j: key = AK_J; break;
        case SDLK_k: key = AK_K; break;
        case SDLK_l: key = AK_L; break;
        case SDLK_m: key = AK_M; break;
        case SDLK_n: key = AK_N; break;
        case SDLK_o: key = AK_O; break;
        case SDLK_p: key = AK_P; break;
        case SDLK_q: key = AK_Q; break;
        case SDLK_r: key = AK_R; break;
        case SDLK_s: key = AK_S; break;
        case SDLK_t: key = AK_T; break;
        case SDLK_u: key = AK_U; break;
        case SDLK_v: key = AK_V; break;
        case SDLK_w: key = AK_W; break;
        case SDLK_x: key = AK_X; break;
        case SDLK_y: key = AK_Y; break;
        case SDLK_z: key = AK_Z; break;

        case SDLK_SPACE: key = AK_SPACE; break;
        case SDLK_EQUALS: key = AK_PLUS; break;
        case SDLK_MINUS: key = AK_MINUS; break;
        case SDLK_COMMA: key = AK_COMMA; break;
        case SDLK_PERIOD: key = AK_PERIOD; break;
        case SDLK_SEMICOLON: key = AK_SEMICOLON; break;
        case SDLK_QUOTE: key = AK_QUOTE; break;
        case SDLK_LEFTBRACKET: key = AK_LBRACKET; break;
        case SDLK_RIGHTBRACKET: key = AK_RBRACKET; break;
        case SDLK_SLASH: key = AK_FSLASH; break;
        case SDLK_BACKSLASH: key = AK_BSLASH; break;
        case SDLK_LEFT: key = AK_ARROW; break;
        case SDLK_F4: key = AK_LOCK; break;
        case SDLK_LSHIFT: key = AK_LSHIFT; break;
        case SDLK_RSHIFT: key = AK_RSHIFT; break;
        case SDLK_DOWN: key = AK_LF; break;
        case SDLK_BACKSPACE: key = AK_BS; break;
        case SDLK_DELETE: key = AK_DEL; break;
        case SDLK_ESCAPE: key = AK_ESC; break;
        case SDLK_TAB: key = AK_TAB; break;
        case SDLK_LCTRL: key = AK_CTRL; break;
        case SDLK_RCTRL: key = AK_CTRL; break;
        case SDLK_RETURN: key = AK_RETURN; break;
        case SDLK_F1: key = AK_BLANKTOP; break;
        case SDLK_F2: key = AK_BLANKMIDDLE; break;
        case SDLK_F3: key = AK_BLANKBOTTOM; break;

        case SDLK_F5: btn = AB_KEYSET0; break;
        case SDLK_F6: btn = AB_KEYSET1; break;
        case SDLK_F7: btn = AB_KEYSET2; break;
        case SDLK_F8: btn = AB_KEYSET3; break;
        case SDLK_F9: btn = AB_KEYSET4; break;

        default:
            key = AK_NONE;
            break;
        }

        if (e->type == SDL_KEYDOWN) {
            keyboard_press_key(&iui->keyb, key);
            mouse_press_button(&iui->mous, btn);
        } else {
            keyboard_release_key(&iui->keyb, key);
            mouse_release_button(&iui->mous, btn);
        }

        break;
    }

    SDL_UnlockMutex(iui->mutex);
}

/* Processes the SDL events. */
static
void gui_process_events(struct gui *ui)
{
    struct gui_internal *iui;
    SDL_Event e;
    int mx, my;

    iui = (struct gui_internal *) ui->internal;

    mx = DISPLAY_WIDTH / 2;
    my = DISPLAY_HEIGHT / 2;

    mouse_clear_movement(&iui->mous);
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            ui->running = FALSE;
            break;

        case SDL_MOUSEMOTION:
            if (!iui->mouse_captured)
                break;

            if (iui->skip_next_mouse_move) {
                iui->skip_next_mouse_move = FALSE;
                break;
            }

            gui_process_event(ui, &e);

            SDL_WarpMouseInWindow(iui->window, mx, my);
            iui->skip_next_mouse_move = TRUE;
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (!iui->mouse_captured) {
                if (e.button.x <= 0 || e.button.y <= 0)
                    break;
                gui_capture_mouse(ui, TRUE);
            }

            gui_process_event(ui, &e);
            break;

        case SDL_MOUSEBUTTONUP:
            if (!iui->mouse_captured)
                break;

            gui_process_event(ui, &e);
            break;

        case SDL_KEYDOWN:
            if (!iui->mouse_captured)
                break;

            gui_process_event(ui, &e);
            break;

        case SDL_KEYUP:
            if (e.key.keysym.sym == SDLK_LALT
                || e.key.keysym.sym == SDLK_RALT) {
                gui_capture_mouse(ui, FALSE);
            }
            if (!iui->mouse_captured)
                break;

            gui_process_event(ui, &e);
            break;
        }
    }
}

/* Updates the gui state and screen.
 * Returns TRUE on success.
 */
static
int gui_update_screen(struct gui *ui)
{
    struct gui_internal *iui;
    void *pixels;
    int i, stride, ret;

    iui = (struct gui_internal *) ui->internal;
    ret = SDL_LockTexture(iui->texture, NULL, &pixels, &stride);
    if (unlikely(ret < 0)) {
        report_error("gui: update_screen: "
                     "could not lock texture (SDL_Error(%d): %s)",
                     ret, SDL_GetError());
    }

    if (SDL_LockMutex(iui->mutex) == 0) {
        uint8_t *pixels8;
        pixels8 = (uint8_t *) pixels;
        for (i = 0; i < DISPLAY_HEIGHT; i++) {
            memcpy(&pixels8[stride * i],
                   &iui->display_data[DISPLAY_STRIDE * i],
                   DISPLAY_WIDTH * sizeof(uint8_t));
        }

        /* Signal the condition for a new frame. */
        SDL_CondSignal(iui->frame_cond);

        SDL_UnlockMutex(iui->mutex);
    } else {
        memset(pixels, -1, ((size_t) stride) * DISPLAY_HEIGHT);
    }

    SDL_UnlockTexture(iui->texture);

    ret = SDL_RenderCopy(iui->renderer, iui->texture,
                         NULL, NULL);
    if (unlikely(ret < 0)) {
        report_error("gui: update_screen: "
                     "could not copy texture (SDL_Error(%d): %s)",
                     ret, SDL_GetError());
    }

    SDL_RenderPresent(iui->renderer);
    return TRUE;
}

/* Function of the other thread. */
static
int other_thread_main(void *arg)
{
    struct gui *ui;

    ui = (struct gui *) arg;
    if (ui->thread_cb) {
        if (unlikely(!(*ui->thread_cb)(ui)))
            return 1;
    }
    return 0;
}

/* Runs the user interface. */
static
int gui_run(struct gui *ui)
{
    struct gui_internal *iui;
    SDL_Thread *thread;
    int ret;

    iui = (struct gui_internal *) ui->internal;
    thread = NULL;

    ret = TRUE;
    iui->window = SDL_CreateWindow("PALOS",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   DISPLAY_WIDTH,
                                   DISPLAY_HEIGHT,
                                   SDL_WINDOW_SHOWN);

    if (unlikely(!iui->window)) {
        report_error("gui: run: "
                     "could not create window (SDL_Error: %s)",
                     SDL_GetError());
        ret = FALSE;
        goto do_exit;
    }

    iui->renderer = SDL_CreateRenderer(iui->window, -1,
                                       SDL_RENDERER_ACCELERATED);
    if (!iui->renderer) {
        iui->renderer = SDL_CreateRenderer(iui->window, -1,
                                           SDL_RENDERER_SOFTWARE);
    }

    if (unlikely(!iui->renderer)) {
        report_error("gui: run: "
                     "could not create renderer (SDL_Error: %s)",
                     SDL_GetError());
        ret = FALSE;
        goto do_exit;
    }

    SDL_RenderSetLogicalSize(iui->renderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    SDL_RenderSetIntegerScale(iui->renderer, 1);
    SDL_SetRenderDrawColor(iui->renderer, 0x00, 0x00, 0x00, 0x00);

    iui->texture = SDL_CreateTexture(iui->renderer,
                                     SDL_PIXELFORMAT_RGB332,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     DISPLAY_WIDTH,
                                     DISPLAY_HEIGHT);

    if (unlikely(!iui->texture)) {
        report_error("gui: run: "
                     "could not create texture (SDL_Error: %s)",
                     SDL_GetError());
        ret = FALSE;
        goto do_exit;
    }

    thread = SDL_CreateThread(&other_thread_main,
                              "gui_extra_thread", ui);
    if (unlikely(!thread)) {
        report_error("gui: run: "
                     "could not create thread (SDL_Error: %s)",
                     SDL_GetError());
        ret = FALSE;
        goto do_exit;
    }

    ui->running = TRUE;
    iui->mouse_captured = FALSE;
    iui->skip_next_mouse_move = FALSE;

    while (ui->running) {
        gui_process_events(ui);

        if (unlikely(!gui_update_screen(ui))) {
            report_error("gui: internal: run: "
                         "could not update screen");
            ret = FALSE;
            ui->running = FALSE;
        }

        SDL_Delay(16); /* For 60 FPS. */
    }

do_exit:
    if (iui->texture) {
        SDL_DestroyTexture(iui->texture);
    }
    iui->texture = NULL;

    if (iui->renderer) {
        SDL_DestroyRenderer(iui->renderer);
    }
    iui->renderer = NULL;

    if (iui->window) {
        SDL_DestroyWindow(iui->window);
    }
    iui->window = NULL;

    if (thread) {
        SDL_WaitThread(thread, NULL);
    }

    return ret;
}

void gui_initvar(struct gui *ui)
{
    ui->internal = NULL;
}

void gui_destroy(struct gui *ui)
{
    struct gui_internal *iui;
    int initialized;

    iui = (struct gui_internal *) ui->internal;
    if (!iui) return;

    if (iui->texture) {
        SDL_DestroyTexture(iui->texture);
    }
    iui->texture = NULL;

    if (iui->renderer) {
        SDL_DestroyRenderer(iui->renderer);
    }
    iui->renderer = NULL;

    if (iui->window) {
        SDL_DestroyWindow(iui->window);
    }
    iui->window = NULL;

    ui->running = FALSE;

    if (iui->mutex) {
        SDL_DestroyMutex(iui->mutex);
    }
    iui->mutex = NULL;

    if (iui->frame_cond) {
        SDL_DestroyCond(iui->frame_cond);
    }
    iui->frame_cond = NULL;

    if (iui->display_data) {
        free((void *) iui->display_data);
    }
    iui->display_data = NULL;

    keyboard_destroy(&iui->keyb);
    mouse_destroy(&iui->mous);

    initialized = iui->initialized;
    free((void *) iui);
    ui->internal = NULL;

    if (initialized) {
        gui_ref_count--;
        if (gui_ref_count < 0) {
            /* Something went wrong. */
            gui_ref_count = 0;
            report_error("gui: destroy: "
                         "gui_ref_count became negative");
        } else if (gui_ref_count == 0) {
            SDL_Quit();
        }
    }
}

int gui_create(struct gui *ui, struct simulator *sim,
               gui_thread_cb thread_cb, void *arg)
{
    struct gui_internal *iui;
    int ret;

    gui_initvar(ui);

    iui = (struct gui_internal *) malloc(sizeof(*iui));
    if (unlikely(!iui)) {
        report_error("gui: create: could not allocate memory");
        gui_destroy(ui);
        return FALSE;
    }

    iui->initialized = FALSE;
    iui->display_data = NULL;
    keyboard_initvar(&iui->keyb);
    mouse_initvar(&iui->mous);
    iui->mutex = NULL;
    iui->frame_cond = NULL;
    iui->window = NULL;
    iui->renderer = NULL;
    iui->texture = NULL;

    iui->display_data = (uint8_t *)
        malloc(DISPLAY_DATA_SIZE * sizeof(uint8_t));

    if (unlikely(!iui->display_data)) {
        report_error("gui: create: "
                     "memory exhausted");
        gui_destroy(ui);
        return FALSE;
    }

    if (unlikely(!keyboard_create(&iui->keyb))) {
        report_error("gui: create: "
                     "could not create keyboard");
        gui_destroy(ui);
        return FALSE;
    }

    if (unlikely(!mouse_create(&iui->mous))) {
        report_error("gui: create: "
                     "could not create mouse");
        gui_destroy(ui);
        return FALSE;
    }

    ui->sim = sim;
    ui->thread_cb = thread_cb;
    ui->internal = iui;
    ui->arg = arg;

    if (gui_ref_count == 0) {
        ret = SDL_Init(SDL_INIT_VIDEO);
        if (unlikely(ret < 0)) {
            report_error("gui: create: "
                         "could not initialize SDL (SDL_Error(%d): %s)",
                         ret, SDL_GetError());
            gui_destroy(ui);
            return FALSE;
        }
    }

    iui->initialized = TRUE;
    gui_ref_count++;

    ret = SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (unlikely(ret == SDL_FALSE)) {
        report_error("gui: create: "
                     "could not set render scale quality "
                     "(SDL_Error(%d): %s)",
                     ret, SDL_GetError());
        gui_destroy(ui);
        return FALSE;
    }

    iui->mutex = SDL_CreateMutex();
    if (unlikely(!iui->mutex)) {
        report_error("gui: create: "
                     "could not create mutex (SDL_Error: %s)",
                     SDL_GetError());
        gui_destroy(ui);
        return FALSE;
    }

    iui->frame_cond = SDL_CreateCond();
    if (unlikely(!iui->frame_cond)) {
        report_error("gui: create: "
                     "could not create condition (SDL_Error: %s)",
                     SDL_GetError());
        gui_destroy(ui);
        return FALSE;
    }

    return TRUE;
}

int gui_start(struct gui *ui)
{
    struct gui_internal *iui;

    iui = (struct gui_internal *) ui->internal;
    if (unlikely(iui->window)) {
        report_error("gui: start: already started");
        return FALSE;
    }

    if (unlikely(!gui_run(ui))) {
        report_error("gui: start: could not start");
        return FALSE;
    }

    return TRUE;
}

void gui_stop(struct gui *ui)
{
    ui->running = FALSE;
}

int gui_running(struct gui *ui)
{
    return ui->running;
}

int gui_update(struct gui *ui)
{
    struct gui_internal *iui;
    int ret;

    if (unlikely(!ui->sim)) {
        report_error("gui: update: no simulator object");
        return FALSE;
    }

    iui = (struct gui_internal *) ui->internal;
    ret = SDL_LockMutex(iui->mutex);
    if (unlikely(ret != 0)) {
        report_error("gui: update: could no acquire lock"
                     "(SDLError(%d): %s)", ret, SDL_GetError());
        return FALSE;
    }

    ret = simulator_update(ui->sim, &iui->keyb, &iui->mous,
                           iui->display_data);
    if (unlikely(!ret)) {
        report_error("gui: update: could not update state");
        SDL_UnlockMutex(iui->mutex);
        return FALSE;
    }

    SDL_UnlockMutex(iui->mutex);
    return TRUE;
}

int gui_wait_frame(struct gui *ui)
{
    struct gui_internal *iui;
    int ret;

    iui = (struct gui_internal *) ui->internal;
    ret = SDL_LockMutex(iui->mutex);
    if (unlikely(ret != 0)) {
        report_error("gui: wait_next_frame: could no acquire lock"
                     "(SDLError(%d): %s)", ret, SDL_GetError());
        return FALSE;
    }

    ret = SDL_CondWait(iui->frame_cond, iui->mutex);
    if (unlikely(ret != 0)) {
        report_error("gui: wait_next_frame: could wait condition"
                     "(SDLError(%d): %s)", ret, SDL_GetError());
        SDL_UnlockMutex(iui->mutex);
        return FALSE;
    }

    SDL_UnlockMutex(iui->mutex);
    return TRUE;
}
