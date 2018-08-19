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
#include <stdnoreturn.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "config.h"

/* Forward declarations. */

static void cleanup(void);
static void forward_event(void);

/* Common. */

static const char *app_name = "evd";

static int stop = 0;
static void handle_interrupt() {
    stop = 1;
}

static const ssize_t error_buffer_l = 4096 * sizeof(char);

/**
 * Prints a failure message and exits with a failure status.
 */
static _Noreturn void fail(const char *format, ...) {
    char * const buffer = malloc(error_buffer_l);
    if (buffer == NULL) {
        perror("could not allocate memory for error message");
        goto exit;
    }

    const ssize_t app_name_l = strlen(app_name);
    const ssize_t prefix_l = app_name_l + 2;

    memcpy(buffer, app_name, app_name_l);
    buffer[app_name_l]= ':';
    buffer[app_name_l + 1] = ' ';

    va_list args;
    va_start(args, format);
    vsnprintf(buffer + prefix_l, error_buffer_l - prefix_l - 1, format, args);
    va_end(args);

    if (errno == 0) {
        fprintf(stderr, buffer);
        fprintf(stderr, "\n");
    } else {
        perror(buffer);
    }

    free(buffer);

exit:
    cleanup();
    exit(EXIT_FAILURE);
}

/**
 * Writes a value to a file.
 */
static void write_file(const char *fname, const char *value) {
    const int fd = open(fname, O_WRONLY);
    if (fd < 0) {
        fail("could not open file %s for writing", fname);
    }

    write(fd, value, strlen(value));
    close(fd);
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
        fail("could not list /dev/input");
    }

    const int devname_video_l = strlen(devname_video);
    const int devname_kb_l = strlen(devname_kb);

    int found = 0;

    for (int i = 0; i < n && found < fname_n; ++i) {
        const char *prefix = "/dev/input/%s";
        char path[sizeof(prefix) + NAME_MAX + 1];
        res = snprintf(path, sizeof(path), prefix, fnames[i]->d_name);
        if (res < 0 || res > (long) sizeof(path)) {
            fail("could not store path \"%s\"", path);
        }

        const int fd = open(path, O_RDONLY);
        if (fd < 0) {
            fail("could not open %s for reading", path);
        }

        char devname[NAME_MAX + 1] = {0};
        if ((res = ioctl(fd, EVIOCGNAME(sizeof(devname)), devname)) < 0) {
            close(fd);
            fail("could not read device name for %s", path);
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
        fail("could not set EV_KEY bit on virtual keyboard");
    }

    if (ioctl(fd_vk, UI_SET_EVBIT, EV_SYN) < 0) {
        fail("could not set EV_SYN bit on virtual keyboard");
    }
}

/**
 * Specifies which key codes the virtual keyboard should support.
 */
static void set_vk_keybits(void) {
    int res;

    for (int i = 0; i < KEY_MAX; ++i) {
        if ((res = ioctl(fd_vk, UI_SET_KEYBIT, i)) < 0) {
            fail("could not set key bit %d on virtual keyboard device", i);
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
        fail("could not initialize virtual keyboard");
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
        fail("could not write virtual keyboard data");
    }

    if ((res = ioctl(fd_vk, UI_DEV_CREATE)) < 0) {
        fail("could not create virtual keyboard");
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
        fail("could not destroy virtual keyboard");
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
        fail("could not open video device %s for reading", fname_video);
    }

    if ((res = ioctl(fd_video, EVIOCGRAB, 1)) < 0) {
        fail("could not capture video device %s", fname_video);
    }

    if ((fd_kb = open(fname_kb, O_RDONLY)) < 0) {
        fail("could not open keyboard device %s for reading", fname_kb);
    }

    if ((res = ioctl(fd_kb, EVIOCGRAB, 1)) < 0) {
        fail("could not capture keyboard device %s", fname_kb);
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
        ioctl(fd_kb, EVIOCGRAB, 0);
        close(fd_kb);
    }
}

/* Events. */

static struct input_event ev;
static const int ev_size = sizeof(struct input_event);

/* Control keys. */

static bool is_shift_down = false;
static bool is_ctrl_down = false;

/* Screen brightness events. */

static const int brightness_min = 10;
static int brightness_max;

#define BRIGHTNESS_VAL_LEN 10

/**
 * Reads the brightness value from a file.
 */
static int read_brightness(const char *fname) {
    const int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        fail("could not open brightness device %s", fname);
    }

    char value[BRIGHTNESS_VAL_LEN];
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
static int get_brightness_step(double percent) {
    return percent / 100.0 * get_brightness_max();
}

/**
 * Sets the specified screen brightness.
 */
static void write_brightness(int value) {
    char value_s[BRIGHTNESS_VAL_LEN];
    snprintf(value_s, sizeof(value_s), "%d", value);
    write_file(fname_brightness_now, value_s);
}

/**
 * Tries to handle a screen brightness event and returns true if the event was
 * handled.
 */
static bool handle_brightness_event(void) {
    int value = 0;
    if (is_ctrl_down) {
        switch (ev.code) {
            case KEY_BRIGHTNESSUP:
                value = get_brightness_max();
                break;

            case KEY_BRIGHTNESSDOWN:
                value =
                    brightness_min +
                    get_brightness_step(percent_brightness);
                break;

            default: return false;
        }
    }

    const int now = get_brightness_now();

    if (value == 0) {
        const float percent = is_shift_down
            ? percent_brightness_alt
            : percent_brightness;

        const int step = get_brightness_step(percent);

        switch (ev.code) {
            case KEY_BRIGHTNESSDOWN:
                value = now - step;
                if (value < brightness_min) {
                    value = brightness_min;
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
    }

    if (value == now) {
        return true;
    }

    write_brightness(value);
    return true;
}

/* Main event handling. */

/**
 * Tries to handle a keyboard event and returns true if the event was handled.
 */
static bool handle_kb_event(void) {
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
 * Returns true if the current key is pressed.
 */
static bool is_key_down(void) {
    return ev.value > 0;
}

/**
 * Reads an event from a device file and returns true if the event was handled.
 */
static bool read_event(int fd) {
    ssize_t bytes;

    if ((bytes = read(fd, &ev, ev_size)) < 0) {
        fail("expected to read %d bytes, got %ld", ev_size, (long) bytes);
    }

    if (fd == fd_kb && ev.type == EV_KEY) {
        switch (ev.code) {
            case KEY_LEFTSHIFT:
            case KEY_RIGHTSHIFT:
                is_shift_down = is_key_down();
                break;

            case KEY_LEFTCTRL:
            case KEY_RIGHTCTRL:
                is_ctrl_down = is_key_down();
                break;

            default:
                return false;
        }

        forward_event();
        return true;
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

    if (FD_ISSET(fd_video, &fds)) {
        return read_event(fd_video) || handle_video_event();
    } else if (FD_ISSET(fd_kb, &fds)) {
        return read_event(fd_kb) || handle_kb_event();
    } else {
        fail("expected file descriptor to be set");
    }

    return false;
}

/**
 * Forwards the current event to the virtual keyboard.
 */
static void forward_event(void) {
    int res;

    if ((res = write(fd_vk, &ev, ev_size)) < 0) {
        fail("could not forward event to virtual keyboard");
    }
}

/* Cleanup. */

static void cleanup(void) {
    destroy_vk();
    release_devices();
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
        fail("could not enter background mode");
    }

    scan_devices();
    create_vk();
    capture_devices();

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
