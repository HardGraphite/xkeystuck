#include <X11/X.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

static int dry_run = 0;
static int verbose = 0;

struct text_area_contex {
    Display *display;
    Window window;
    unsigned int start_x, start_y;
    unsigned int width, height;
    unsigned int current_y;
};

static void print_line(struct text_area_contex *ctx, const char *fmt, ...) {
    char buffer[128];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    puts(buffer);

    const unsigned int line_height = 20;
    const GC gc = DefaultGC(ctx->display, DefaultScreen(ctx->display));
    if (ctx->current_y < ctx->height - line_height) {
        ctx->current_y += line_height;
    } else {
        XCopyArea(
            ctx->display, ctx->window, ctx->window, gc,
            0, line_height, ctx->width, ctx->current_y - line_height, 0, 0
        );
        XSetForeground(
            ctx->display, gc,
            WhitePixel(ctx->display, DefaultScreen(ctx->display))
        );
        XFillRectangle(
            ctx->display, ctx->window, gc,
            0, ctx->current_y - line_height, ctx->width, line_height * 2
        );
        XSetForeground(
            ctx->display, gc,
            BlackPixel(ctx->display, DefaultScreen(ctx->display))
        );
    }
    XDrawString(
        ctx->display, ctx->window, gc,
        (int)ctx->start_x, (int)ctx->current_y,
        buffer, n
    );
    XFlush(ctx->display);
}

static void *scan_keys(void *_ctx) {
    struct text_area_contex *const tac = _ctx;
    sleep(1);

    print_line(tac, "dry-run = %i, verbose = %i", dry_run, verbose);

    int keycode_min, keycode_max;
    char keymap[32];
    XDisplayKeycodes(tac->display, &keycode_min, &keycode_max);
    XQueryKeymap(tac->display, keymap);

    print_line(tac, "scanning key map from %i to %i", keycode_min, keycode_max);
    unsigned int down_count = 0;
    for (int kc = keycode_min; kc <= keycode_max; kc++) {
        const int byte_index = kc / 8;
        const int bit_index  = kc % 8;
        const int key_set    = (keymap[byte_index] & (1 << bit_index)) != 0;
        const char *const key_name =
            XKeysymToString(XkbKeycodeToKeysym(tac->display, kc, 0, 0));
        if (key_set || verbose) {
            print_line(
                tac, "* KEY %#04x \"%s\" -- %s",
                kc, key_name, key_set ? "DOWN" : "UP"
            );
        }
        if (key_set) {
            down_count++;
            if (!dry_run) {
                print_line(tac, "ungrab key %#04h", kc);
                XUngrabKey(tac->display, kc, AnyModifier, tac->window);
            }
            if (verbose)
                sleep(2);
        } else {
            if (verbose)
                usleep(5 * 1000);
        }
    }
    print_line(tac, "scanning done, %u keys are down", down_count);

    return NULL;
}

static void disp_key_event(struct text_area_contex *const tac, XKeyEvent *event) {
    const char *const key_name =
        XKeysymToString(XkbKeycodeToKeysym(tac->display, event->keycode, 0, 0));
    print_line(
        tac, "!! XKeyEvent: %#04x \"%s\" -- %s",
        event->keycode, key_name,
        event->type == KeyPress ? "Press" :
        event->type == KeyRelease ? "Release" : "?"
    );
}

static void run_app(void) {
    Display *const display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "cannot open display\n");
        exit(EXIT_FAILURE);
    }

    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);

    const Window window = XCreateSimpleWindow(
        display, RootWindow(display, DefaultScreen(display)), // display, parent
        0, 0, 800, 640, // x, y, width, height
        1, BlackPixel(display, DefaultScreen(display)), // border width & color
        WhitePixel(display, DefaultScreen(display)) // background color
    );

    XSelectInput(display, window, ExposureMask | KeyPressMask | KeyReleaseMask);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XStoreName(display, window, "X Key Stuck Handler");
    XMapWindow(display, window);

    struct text_area_contex ta_context = {
        .display = display,
        .window  = window,
        .start_x = 20,
        .start_y = 20,
        .width   = 800,
        .height  = 640,
        .current_y = 0,
    };

    pthread_t thread_worker;
    pthread_create(&thread_worker, NULL, scan_keys, &ta_context);

    while (1) {
        XEvent event;
        XNextEvent(display, &event);

        switch (event.type) {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm_delete_window)
                goto quit_event_loop;
            break;

        case Expose:
            break;

        case KeyPress:
        case KeyRelease:
            if (verbose)
                disp_key_event(&ta_context, &event.xkey);
            break;

        default:
            break;
        }
    }
quit_event_loop:

    pthread_cancel(thread_worker);

    XDestroyWindow(display, window);
    XCloseDisplay(display);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        const char *const arg_str = argv[i];
        if (strcmp(arg_str, "--dry-run") == 0) {
            dry_run = 1;
        } else if (strcmp(arg_str, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(arg_str, "--help") == 0) {
            printf("Usage: %s [--dry-run] [--verbose]\n", argv[0]);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "unknown command line option: %s\n", arg_str);
            return EXIT_FAILURE;
        }
    }

    run_app();
}
