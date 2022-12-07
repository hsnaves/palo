
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "gui/gui.h"
#include "common/utils.h"

/* Constants. */
#define SCREEN_WIDTH  608
#define SCREEN_HEIGHT 808

/* Internal structures. */
struct gui_internal {
    int sdl_initialized;
    int mouse_captured;
    int skip_next_mouse_move;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
};

/* Functions. */

void gui_initvar(struct gui *ui)
{
    ui->internal = NULL;
}

void gui_destroy(struct gui *ui)
{
    struct gui_internal *internal;

    internal = (struct gui_internal *) ui->internal;
    if (internal) {
        if (internal->texture) {
        }
        internal->texture = NULL;

        if (internal->renderer) {
            SDL_DestroyRenderer(internal->renderer);
        }
        internal->renderer = NULL;

        if (internal->window) {
            SDL_DestroyWindow(internal->window);
        }
        internal->window = NULL;

        if (internal->sdl_initialized) {
            SDL_Quit();
        }
        internal->sdl_initialized = FALSE;

        free((void *) internal);
    }
    ui->internal = NULL;
}

int gui_create(struct gui *ui)
{
    struct gui_internal *internal;

    gui_initvar(ui);

    internal = (struct gui_internal *) malloc(sizeof(*internal));
    if (unlikely(!internal)) {
        report_error("gui: create: could not allocate memory");
        return FALSE;
    }
    internal->sdl_initialized = FALSE;
    internal->mouse_captured = FALSE;
    internal->skip_next_mouse_move = FALSE;
    internal->window = NULL;
    internal->renderer = NULL;
    internal->texture = NULL;

    ui->internal = internal;
    return TRUE;
}

/* To capture the mouse movements (and keyboard).
 * The `capture` indicates whether we should capture or release
 * the mouse movements.
 */
static
void gui_capture_mouse(struct gui *ui, int capture)
{
    struct gui_internal *internal;

    internal = (struct gui_internal *) ui->internal;
    if (capture) {
        SDL_ShowCursor(0);
        SDL_SetWindowGrab(internal->window, SDL_TRUE);
        SDL_SetWindowTitle(internal->window,
                           "Galto - Mouse captured. Press 'Alt' to release.");
    } else {
        SDL_ShowCursor(1);
        SDL_SetWindowGrab(internal->window, SDL_FALSE);
        SDL_SetWindowTitle(internal->window, "Galto");
    }

    internal->mouse_captured = capture;
}

/* Processes the SDL events.
 * Returns TRUE on success.
 */
static
int gui_process_events(struct gui *ui)
{
    struct gui_internal *internal;
    SDL_Event e;
    int mx, my;

    internal = (struct gui_internal *) ui->internal;
    mx = SCREEN_WIDTH / 2;
    my = SCREEN_HEIGHT / 2;

    while (SDL_PollEvent(&e)) {
        switch(e.type) {
        case SDL_QUIT:
            ui->running = FALSE;
            break;
        case SDL_MOUSEMOTION:
            if (!internal->mouse_captured)
                break;

            if (internal->skip_next_mouse_move) {
                internal->skip_next_mouse_move = FALSE;
                break;
            }

            /*
            dx = e.motion.x - mx;
            dy = e.motion.y - my;
            */
            SDL_WarpMouseInWindow(internal->window, mx, my);
            internal->skip_next_mouse_move = TRUE;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (!internal->mouse_captured) {
                if (e.motion.x <= 0 || e.motion.y <= 0)
                    break;
                gui_capture_mouse(ui, TRUE);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (!internal->mouse_captured)
                break;

            break;
        case SDL_KEYDOWN:
            if (!internal->mouse_captured)
                break;

            break;
        case SDL_KEYUP:
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                ui->running = FALSE;
                break;
            }

            if (e.key.keysym.sym == SDLK_LALT
                || e.key.keysym.sym == SDLK_RALT) {
                gui_capture_mouse(ui, FALSE);
            }
            if (!internal->mouse_captured)
                break;

            break;
        }
    }

    return TRUE;
}

/* Updates the gui state and screen.
 * Returns TRUE on success.
 */
static
int gui_update(struct gui *ui)
{
    struct gui_internal *internal;
    void *pixels;
    int stride, ret;

    internal = (struct gui_internal *) ui->internal;

    ret = SDL_LockTexture(internal->texture, NULL, &pixels, &stride);
    if (unlikely(ret < 0)) {
        report_error("gui: update: "
                     "could not lock texture (SDL_Error(%d): %s)",
                     ret, SDL_GetError());
    }
    memset(pixels, -1, ((size_t) stride) * SCREEN_HEIGHT);

    SDL_UnlockTexture(internal->texture);

    ret = SDL_RenderCopy(internal->renderer, internal->texture,
                         NULL, NULL);
    if (unlikely(ret < 0)) {
        report_error("gui: update: "
                     "could not copy texture (SDL_Error(%d): %s)",
                     ret, SDL_GetError());
    }

    SDL_RenderPresent(internal->renderer);

    return TRUE;
}

int gui_run(struct gui *ui)
{
    struct gui_internal *internal;
    int ret;

    if (unlikely(ui->running)) {
        report_error("gui: run: already running");
        return FALSE;
    }

    internal = (struct gui_internal *) ui->internal;

    if (!internal->sdl_initialized) {
        ret = SDL_Init(SDL_INIT_VIDEO);
        if (unlikely(ret < 0)) {
            report_error("gui: run: "
                         "could not initialize SDL (SDL_Error(%d): %s)",
                         ret, SDL_GetError());
            return FALSE;
        }
        internal->sdl_initialized = TRUE;
    }

    ret = SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (ret == SDL_FALSE) {
        report_error("gui: run: "
                     "could not set render scale quality (SDL_Error(%d): %s)",
                     ret, SDL_GetError());
        return FALSE;
    }

    if (!internal->window) {
        internal->window = SDL_CreateWindow("Galto",
                                            SDL_WINDOWPOS_UNDEFINED,
                                            SDL_WINDOWPOS_UNDEFINED,
                                            SCREEN_WIDTH,
                                            SCREEN_HEIGHT,
                                            SDL_WINDOW_SHOWN);

        if (unlikely(!internal->window)) {
            report_error("gui: run: "
                         "could not create window (SDL_Error: %s)",
                         SDL_GetError());
            return FALSE;
        }

        internal->mouse_captured = FALSE;
        internal->skip_next_mouse_move = FALSE;
    }

    if (!internal->renderer) {
        internal->renderer = SDL_CreateRenderer(internal->window, -1,
                                                SDL_RENDERER_ACCELERATED);
        if (!internal->renderer) {
            internal->renderer = SDL_CreateRenderer(internal->window, -1,
                                                    SDL_RENDERER_SOFTWARE);
        }

        if (unlikely(!internal->renderer)) {
            report_error("gui: run: "
                         "could not create renderer (SDL_Error: %s)",
                         SDL_GetError());
            return FALSE;
        }
    }

    SDL_RenderSetLogicalSize(internal->renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_RenderSetIntegerScale(internal->renderer, 1);
    SDL_SetRenderDrawColor(internal->renderer, 0x00, 0x00, 0x00, 0x00);

    if (!internal->texture) {
        internal->texture = SDL_CreateTexture(internal->renderer,
                                              SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              SCREEN_WIDTH,
                                              SCREEN_HEIGHT);

        if (unlikely(!internal->texture)) {
            report_error("gui: run: "
                         "could not create texture (SDL_Error: %s)",
                         SDL_GetError());
            return FALSE;
        }
    }

    ui->running = TRUE;
    while (ui->running) {
        if (unlikely(!gui_process_events(ui)))
            goto error_exit;
        if (unlikely(!gui_update(ui)))
            goto error_exit;
    }

    return TRUE;

error_exit:
    report_error("gui: run: could not run");
    return FALSE;
}
