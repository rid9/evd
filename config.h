/* Keyboard. */

// Name of the event device providing other keyboard events.
static const char *devname_kb = "AT Translated Set 2 keyboard";

/* Brightness. */

// Name of the event device providing display related keyboard events.
static const char *devname_video = "Video Bus";

// Percentage with which to increase or decrease the display brightness when
// the brightness keys are pressed.
static const double percent_brightness = 5.0;

// Percentage with which to increase or decrease the display brightness when
// the brightness keys are pressed while holding down the Alt key.
static const double percent_brightness_alt = 1.0;

// Device file containing the maximum display brightness.
static const char *fname_brightness_max = "/sys/class/backlight/intel_backlight/max_brightness";

// Device file containing the current display brightness.
static const char *fname_brightness_now = "/sys/class/backlight/intel_backlight/brightness";
