#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "config.h"

/* Common. */

static const char *app_name = "ksd";

static void cleanup(void);

static int stop = 0;
static void handle_interrupt() {
    stop = 1;
}

/**
 * Prints a failure message and exits with a failure status.
 */
static void fail(const char *format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);

    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    fprintf(stderr, "%s: %s", app_name, buffer);

    cleanup();
    exit(EXIT_FAILURE);
}

/* Devices. */

static char fname_video[255];
static char fname_kb[255];
static const int fname_n = 2;

/**
 * Returns true if the name of a directory file represents an event device.
 */
static int is_evdev(const struct dirent *dir) {
    return strncmp("event", dir->d_name, 5) == 0;
}

/**
 * Scans input event device files and stores supported device file names.
 */
static void scan_devices(void) {
    int res;
    struct dirent **fnames;

    const int n = scandir("/dev/input", &fnames, is_evdev, versionsort);
    if (n < 0) {
        fail("could not list /dev/input: %d\n", n);
    }

    const int devname_video_l = strlen(devname_video);
    const int devname_kb_l = strlen(devname_kb);

    int found = 0;

    for (int i = 0; i < n && found < fname_n; ++i) {
        char path[11 /* prefix */ + 256 /* d_name */];
        snprintf(path, sizeof(path), "/dev/input/%s", fnames[i]->d_name);

        const int fd = open(path, O_RDONLY);
        if (fd < 0) {
            fail("could not open %s for reading: %d\n", path, fd);
        }

        char devname[256] = {0};
        if ((res = ioctl(fd, EVIOCGNAME(sizeof(devname)), devname)) < 0) {
            close(fd);
            fail("could not read device name for %s: %d\n", path, res);
        }
        close(fd);

        if (strncmp(devname, devname_video, devname_video_l) == 0) {
            memcpy(fname_video, path, strlen(path));
            ++found;
        } else if (strncmp(devname, devname_kb, devname_kb_l) == 0) {
            memcpy(fname_kb, path, strlen(path));
            ++found;
        }
    }

    if (found < fname_n) {
        fail("could not find all devices");
    }
}

/* Virtual keyboard. */

static const char *vk_name = "Virtual Keyboard";
static int fd_vk;

/**
 * Specifies which events the virtual keyboard should support.
 */
static void set_vk_evbits(void) {
    if (ioctl(fd_vk, UI_SET_EVBIT, EV_KEY) < 0) {
        fail("could not set EV_KEY bit on virtual keyboard\n");
    }

    if (ioctl(fd_vk, UI_SET_EVBIT, EV_SYN) < 0) {
        fail("could not set EV_SYN bit on virtual keyboard\n");
    }
}

/**
 * Specifies which key codes the virtual keyboard should support.
 */
static void set_vk_keybits(void) {
    int res;

    for (int i = 0; i < KEY_MAX; ++i) {
        if ((res = ioctl(fd_vk, UI_SET_KEYBIT, i)) < 0) {
            fail("could not set key bit %d on virtual keyboard device: %d\n",
                    i, res);
        }
    }
}

/**
 * Creates a virtual keyboard device.
 */
static void create_vk(void) {
    int res;

    fd_vk = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    if (fd_vk < 0) {
        fail("could not initialize virtual keyboard\n");
    }

    set_vk_evbits();
    set_vk_keybits();

    struct uinput_user_dev dev;
    memset(&dev, 0, sizeof(dev));

    snprintf(dev.name, UINPUT_MAX_NAME_SIZE, vk_name);
    dev.id.bustype = BUS_USB;
    dev.id.vendor  = 1;
    dev.id.product = 1;
    dev.id.version = 1;

    if ((res = write(fd_vk, &dev, sizeof(dev)) < 0)) {
        fail("could not write virtual keyboard data: %d\n", res);
    }

    if ((res = ioctl(fd_vk, UI_DEV_CREATE)) < 0) {
        fail("could not create virtual keyboard: %d\n", res);
    }
}

/**
 * Destroys the virtual keyboard and closes the file descriptor.
 */
static void destroy_vk(void) {
    if (fd_vk <= 0) {
        return;
    }

    int res;

    if ((res = ioctl(fd_vk, UI_DEV_DESTROY)) < 0) {
        close(fd_vk);
        fail("could not destroy virtual keyboard: %d\n", res);
    }

    close(fd_vk);
}

/* Devices. */

static int fd_video;
static int fd_kb;

/**
 * Opens and captures devices.
 */
static void capture_devices(void) {
    int res;

    if ((fd_video = open(fname_video, O_RDONLY)) < 0) {
        fail("could not open video device %s for reading: %d\n",
                fname_video, fd_video);
    }

    if ((res = ioctl(fd_video, EVIOCGRAB, 1)) < 0) {
        fail("could not capture video device %s: %d\n", fname_video, res);
    }

    if ((fd_kb = open(fname_kb, O_RDONLY)) < 0) {
        fail("could not open keyboard device %s for reading: %d\n",
                fname_kb, fd_kb);
    }

    if ((res = ioctl(fd_kb, EVIOCGRAB, 1)) < 0) {
        fail("could not capture keyboard device %s: %d\n", fname_kb, res);
    }
}

/**
 * Releases captured devices.
 */
static void release_devices(void) {
    if (fd_video > 0) {
        ioctl(fd_video, EVIOCGRAB, 0);
        close(fd_video);
    }

    if (fd_kb > 0) {
        ioctl(fd_video, EVIOCGRAB, 0);
        close(fd_kb);
    }
}

/* Events. */

static struct input_event ev;
static const int ev_size = sizeof(struct input_event);

/* Screen brightness events. */

static int brightness_max;
static int brightness_step;

#define BRIGHTNESS_VAL_MAX 10

/**
 * Reads the brightness value from a file.
 */
static int read_brightness(const char *fname) {
    const int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        fail("could not open brightness device %s: %d", fname, fd);
    }

    char value[BRIGHTNESS_VAL_MAX];
    const ssize_t bytes = read(fd, &value, sizeof(value));
    close(fd);

    if (bytes == 0) {
        fail("could not read brightness device %s", fname);
    }

    const int value_parsed = atoi(value);
    if (value_parsed == 0) {
        fail("could not parse brightness value \"%s\" from %s", value, fname);
    }

    return value_parsed;
}

/**
 * Returns the maximum screen brightness.
 */
static int get_brightness_max(void) {
    if (brightness_max == 0) {
        brightness_max = read_brightness(fname_brightness_max);
    }

    return brightness_max;
}

/**
 * Returns the current screen brightness.
 */
static int get_brightness_now(void) {
    return read_brightness(fname_brightness_now);
}

/**
 * Returns the amount with which the brightness needs to be increased or
 * decreased.
 */
static int get_brightness_step(void) {
    if (brightness_step == 0) {
        brightness_step = percent_brightness / 100.0 * get_brightness_max();
    }

    return brightness_step;
}

/**
 * Sets the specified screen brightness.
 */
static void write_brightness(int value) {
    const int fd = open(fname_brightness_now, O_WRONLY);
    if (fd < 0) {
        fail("could not open brightness file %s for writing: %d",
                fname_brightness_now, fd);
    }

    char value_s[BRIGHTNESS_VAL_MAX];
    snprintf(value_s, sizeof(value_s), "%d", value);
    write(fd, &value_s, strlen(value_s));
    close(fd);
}

/**
 * Tries to handle a screen brightness event and returns true if the event was
 * handled.
 */
static bool handle_brightness_event(void) {
    const int now = get_brightness_now();
    const int step = get_brightness_step();

    int value;

    switch (ev.code) {
        case KEY_BRIGHTNESSDOWN:
            value = now - step;
            if (value < 10) {
                value = 10;
            }
            break;

        case KEY_BRIGHTNESSUP:
            value = now + step;
            const int max = get_brightness_max();
            if (value > max) {
                value = max;
            }
            break;

        default:
            return false;
    }

    if (value == now) {
        return true;
    }

    write_brightness(value);
    return true;
}

/* Main event handling. */

static bool is_ctrl_down = false;

/**
 * Returns true if the current event is a Control key event.
 */
static bool is_ctrl_key(void) {
    return (
        ev.type == EV_KEY &&
        (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL)
       );
}

/**
 * Tries to handle a keyboard event and returns true if the event was handled.
 */
static bool handle_kb_event(void) {
    if (is_ctrl_key()) {
        is_ctrl_down = ev.value > 0;
    }

    if (is_ctrl_down && ev.code == KEY_C && ev.value > 0) {
        stop = 1;
    }

    return false;
}

/**
 * Tries to handle a video event and returns true if the event was handled.
 */
static bool handle_video_event(void) {
    if (ev.value == 0) {
        return true;
    }

    switch (ev.code) {
        case KEY_BRIGHTNESSDOWN:
        case KEY_BRIGHTNESSUP:
            return handle_brightness_event();
    }

    return false;
}

/**
 * Reads then tries to handle the next event from the supported devices, and
 * returns true if the event was handled.
 */
static bool handle_event(void) {
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(fd_video, &fds);
    FD_SET(fd_kb, &fds);

    int fd_max = fd_video;
    if (fd_kb > fd_max) {
        fd_max = fd_kb;
    }

    select(fd_max + 1, &fds, NULL, NULL, NULL);
    if (stop) {
        cleanup();
        exit(EXIT_SUCCESS);
    }

    ssize_t bytes;

#define CHECK_BYTES(b) \
    if ((b) < 0) { \
        fail("expected to read %d bytes, got %ld\n", ev_size, (long) bytes); \
    }

    if (FD_ISSET(fd_video, &fds)) {
        CHECK_BYTES(bytes = read(fd_video, &ev, ev_size));
        return handle_video_event();
    } else if (FD_ISSET(fd_kb, &fds)) {
        CHECK_BYTES(bytes = read(fd_kb, &ev, ev_size));
        return handle_kb_event();
    } else {
        fail("expected file descriptor to be set\n");
    }

    return false;
}

/**
 * Forwards the current event to the virtual keyboard.
 */
static void forward_event(void) {
    int res;

    if ((res = write(fd_vk, &ev, ev_size)) < 0) {
        fail("could not forward event to virtual keyboard: %d\n", res);
    }
}

/* Cleanup. */

static void cleanup(void) {
    destroy_vk();
    release_devices();
}

/* Cache. */

static void cache(void) {
    get_brightness_max();
    get_brightness_step();
}

/* Arguments. */

static bool background_mode = false;

static void show_usage(void) {
    printf("usage: %s [-b]\n", app_name);
}

static void parse_args(int argc, char *argv[]) {
    if (argc == 1) {
        return;
    } else if (argc == 2 && strncmp(argv[1], "-b", 2) == 0) {
        background_mode = true;
    } else {
        show_usage();
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    if (background_mode && daemon(0, 0) == -1) {
        fail("could not enter background mode: %d\n", errno);
    }

    scan_devices();
    create_vk();
    capture_devices();
    cache();

    signal(SIGINT, handle_interrupt);
    signal(SIGTERM, handle_interrupt);

    while (!stop) {
        if (!handle_event()) {
            forward_event();
        }
    }

    cleanup();
    return EXIT_SUCCESS;
}
