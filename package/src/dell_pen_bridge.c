/*
 * Dell PN7522W barrel-button bridge for the verified liuqin Novatek driver.
 *
 * Foreground process: the KernelSU service owns tracing setup and supervision.
 * It reads an existing private trace_pipe and creates a buttons-only uinput
 * device accepted by the unmodified XiaomiSmartPenPlus module. It does not set
 * up probes. An explicit --cleanup-instance option allows teardown of exactly
 * the fixed private instance/event below. It never writes kernel memory or
 * boot images. The normal supervisor must remain the process's parent.
 *
 * Build, for example:
 *   zig cc -target aarch64-linux-musl -static -O2 -std=c11 \
 *     -Wall -Wextra -Werror dell_pen_bridge.c -o dell_pen_bridge
 *
 * Live:
 *   dell_pen_bridge --trace /sys/kernel/tracing/instances/NAME/trace_pipe \
 *     --stop-file /dev/dell_pen_bridge_liuqin/stop \
 *     --status /dev/dell_pen_bridge_liuqin/status.json \
 *     --lock /dev/dell_pen_bridge_liuqin/native.lock [--max-seconds 120] \
 *     [--cleanup-instance /sys/kernel/tracing/instances/dell_pen_bridge_liuqin]
 * The default maximum, and an explicit 0, mean no lifetime limit.
 *
 * Offline (no uinput, status, lock, or other device access):
 *   dell_pen_bridge --replay upper.trace.txt
 * Replay uses recorded timestamps, reports deduplicated state transitions,
 * simulates the 500 ms release timeout between frames, and releases at EOF.
 *
 * Mask 2 (upper side button) -> KEY_PAGEUP   104 -> module button 2.
 * Mask 1 (lower side button) -> KEY_PAGEDOWN 109 -> module button 1.
 * No coordinate, pressure, tilt, or touch events are injected or intercepted.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif

#define PEN_NAME "Xiaomi Smart Pen Keyboard"
#define STALE_SECONDS 0.500
#define STATUS_SECONDS 5.0
#define LINE_CAPACITY 8192
#define CLEANUP_INSTANCE "/sys/kernel/tracing/instances/dell_pen_bridge_liuqin"
#define CLEANUP_EVENT "dell_pen_bridge_liuqin/raw"

_Static_assert(sizeof(struct input_id) == 8, "Linux input_id ABI");
_Static_assert(sizeof(struct uinput_user_dev) == 1116, "Linux legacy uinput ABI");
_Static_assert(KEY_PAGEUP == 104 && KEY_PAGEDOWN == 109, "Linux key codes");
_Static_assert(UI_DEV_CREATE == 0x5501 && UI_DEV_DESTROY == 0x5502, "uinput ioctls");
_Static_assert(UI_SET_EVBIT == 0x40045564 && UI_SET_KEYBIT == 0x40045565,
               "uinput capability ioctls");

struct options {
    const char *trace_path;
    const char *stop_path;
    const char *status_path;
    const char *lock_path;
    const char *replay_path;
    const char *cleanup_instance;
    unsigned max_seconds;
    bool max_given;
};

struct counters {
    uint64_t raw;
    uint64_t valid;
    uint64_t stale;
    uint64_t malformed;
    uint64_t ignored;
    uint64_t oversized;
    uint64_t transitions;
    uint64_t timeouts;
    uint64_t mask_frames[4];
};

struct frame {
    double stamp;
    unsigned mask;
};

struct bridge {
    const struct options *options;
    struct counters counts;
    int trace_fd;
    int uinput_fd;
    int lock_fd;
    bool uinput_created;
    unsigned mask;
    unsigned upper_code;
    unsigned lower_code;
    double started;
    double last_fresh;
    double last_stamp;
    double last_status;
    bool have_stamp;
    bool status_dirty;
    bool status_error_reported;
    const char *reason;
    const char *state;
};

enum parse_result { PARSE_IGNORE, PARSE_VALID, PARSE_INVALID };
static volatile sig_atomic_t requested_signal;

static void on_signal(int number) {
    requested_signal = number;
}

static double boot_time(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_BOOTTIME, &value) != 0) return -1.0;
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static void error_message(const char *stage) {
    int saved = errno;
    fprintf(stderr, "dell_pen_bridge: error %s (errno=%d)\n", stage, saved);
}

static bool whitespace(char value) {
    return isspace((unsigned char)value) != 0;
}

/* Only the named raw event is accepted. A decimal boot timestamp must be the
 * immediately preceding token, so incidental "raw:" in other events does not
 * become input. No addresses, coordinates, or complete raw lines are logged. */
static enum parse_result parse_frame(const char *line, struct frame *out,
                                     bool *is_raw) {
    *is_raw = false;
    const char *event = line;
    for (;;) {
        event = strstr(event, "raw:");
        if (!event) return PARSE_IGNORE;
        if ((event == line || whitespace(event[-1])) &&
            (event[4] == '\0' || whitespace(event[4]))) break;
        event += 4;
    }

    const char *colon = event;
    while (colon > line && whitespace(colon[-1])) --colon;
    if (colon == line || colon[-1] != ':') return PARSE_IGNORE;
    --colon;
    const char *stamp = colon;
    while (stamp > line && (isdigit((unsigned char)stamp[-1]) || stamp[-1] == '.'))
        --stamp;
    if (stamp == colon || (stamp > line && !whitespace(stamp[-1]))) return PARSE_IGNORE;
    *is_raw = true;
    bool dot = false;
    unsigned before = 0, after = 0;
    for (const char *p = stamp; p < colon; ++p) {
        if (*p == '.') {
            if (dot) return PARSE_INVALID;
            dot = true;
        } else if (isdigit((unsigned char)*p)) {
            if (dot) ++after;
            else ++before;
        } else {
            return PARSE_INVALID;
        }
    }
    if (!dot || !before || !after) return PARSE_INVALID;
    errno = 0;
    char *end = NULL;
    double timestamp = strtod(stamp, &end);
    if (errno != 0 || end != colon || !isfinite(timestamp) || timestamp < 0.0)
        return PARSE_INVALID;

    /* Byte names are matched as complete whitespace-separated tokens. */
    const unsigned needed[6] = {66, 71, 72, 75, 76, 77};
    unsigned values[6] = {0};
    unsigned seen = 0;
    const char *p = event + 4;
    while (*p) {
        while (whitespace(*p)) ++p;
        if (!*p) break;
        const char *token = p;
        while (*p && !whitespace(*p)) ++p;
        const char *token_end = p;
        if (token[0] != 'b' || token + 1 >= token_end ||
            !isdigit((unsigned char)token[1])) continue;
        char *index_end;
        errno = 0;
        unsigned long index = strtoul(token + 1, &index_end, 10);
        if (errno || index_end >= token_end || *index_end != '=') continue;
        int slot = -1;
        for (unsigned i = 0; i < 6; ++i) {
            if (index == needed[i]) slot = (int)i;
        }
        if (slot < 0) continue;
        if ((seen & (1U << slot)) || index_end + 1 >= token_end ||
            !isdigit((unsigned char)index_end[1])) return PARSE_INVALID;
        char *value_end;
        errno = 0;
        unsigned long value = strtoul(index_end + 1, &value_end, 0);
        if (errno || value_end != token_end || value > 255) return PARSE_INVALID;
        values[slot] = (unsigned)value;
        seen |= 1U << slot;
    }
    if (!(seen & 1U)) return PARSE_INVALID;
    if (values[0] == 0xff) {
        out->stamp = timestamp;
        out->mask = 0;
        return PARSE_VALID;
    }
    if (values[0] != 1) return PARSE_IGNORE;
    if (seen != 0x3fU) return PARSE_INVALID;
    out->stamp = timestamp;
    out->mask = (values[1] | values[2] | values[3] | values[4]) == 0
        ? 0 : values[5] & 3U;
    return PARSE_VALID;
}

static int write_complete(int fd, const void *data, size_t length) {
    const unsigned char *cursor = data;
    while (length) {
        ssize_t count = write(fd, cursor, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            if (count == 0) errno = EIO;
            return -1;
        }
        cursor += (size_t)count;
        length -= (size_t)count;
    }
    return 0;
}

static void input_event_set(struct input_event *event, unsigned code, int value) {
    memset(event, 0, sizeof(*event));
    event->type = EV_KEY;
    event->code = (uint16_t)code;
    event->value = value;
}

static int emit_events(int fd, struct input_event *events, size_t count) {
    size_t remaining = count * sizeof(*events);
    unsigned char *cursor = (unsigned char *)events;
    while (remaining) {
        ssize_t amount = write(fd, cursor, remaining);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0 || (size_t)amount % sizeof(*events) != 0) {
            if (amount >= 0) errno = EIO;
            return -1;
        }
        cursor += amount;
        remaining -= (size_t)amount;
    }
    return 0;
}

#ifndef SIDE_CONFIG_PATH
#define SIDE_CONFIG_PATH "/data/adb/dell_pen_bridge_liuqin/side.conf"
#endif

static int set_mask(struct bridge *bridge, unsigned mask, const char *reason) {
    if (bridge->mask == mask) return 0;
    if (bridge->mask == 0) {
        bridge->upper_code = KEY_PAGEUP;
        bridge->lower_code = KEY_PAGEDOWN;
        FILE *config = fopen(SIDE_CONFIG_PATH, "r");
        if (config) {
            unsigned upper, lower; char extra;
            if (fscanf(config, "%u %u %c", &upper, &lower, &extra) == 2 &&
                ((upper == KEY_PAGEUP && lower == KEY_PAGEDOWN) ||
                 (upper == KEY_PAGEDOWN && lower == KEY_PAGEUP))) {
                bridge->upper_code = upper; bridge->lower_code = lower;
            }
            fclose(config);
        }
    }
    struct input_event events[3];
    size_t count = 0;
    unsigned released = bridge->mask & ~mask;
    unsigned pressed = mask & ~bridge->mask;
    if (released & 2) input_event_set(&events[count++], bridge->upper_code, 0);
    if (released & 1) input_event_set(&events[count++], bridge->lower_code, 0);
    if (pressed & 2) input_event_set(&events[count++], bridge->upper_code, 1);
    if (pressed & 1) input_event_set(&events[count++], bridge->lower_code, 1);
    memset(&events[count], 0, sizeof(events[count]));
    events[count++].type = EV_SYN;
    if (emit_events(bridge->uinput_fd, events, count) < 0) return -1;
    bridge->mask = mask;
    ++bridge->counts.transitions;
    bridge->reason = reason;
    bridge->status_dirty = true;
    return 0;
}

static int release_both(struct bridge *bridge) {
    if (!bridge->uinput_created) return 0;
    struct input_event events[3];
    input_event_set(&events[0], KEY_PAGEDOWN, 0);
    input_event_set(&events[1], KEY_PAGEUP, 0);
    memset(&events[2], 0, sizeof(events[2]));
    events[2].type = EV_SYN;
    int result = emit_events(bridge->uinput_fd, events, 3);
    if (result == 0 && bridge->mask != 0) ++bridge->counts.transitions;
    bridge->mask = 0;
    bridge->status_dirty = true;
    return result;
}

static int create_device(struct bridge *bridge) {
    bridge->uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (bridge->uinput_fd < 0)
        bridge->uinput_fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (bridge->uinput_fd < 0) return -1;
    if (ioctl(bridge->uinput_fd, UI_SET_EVBIT, EV_SYN) < 0 ||
        ioctl(bridge->uinput_fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(bridge->uinput_fd, UI_SET_KEYBIT, KEY_PAGEUP) < 0 ||
        ioctl(bridge->uinput_fd, UI_SET_KEYBIT, KEY_PAGEDOWN) < 0) return -1;
    struct uinput_user_dev description;
    memset(&description, 0, sizeof(description));
    memcpy(description.name, PEN_NAME, sizeof(PEN_NAME));
    description.id.bustype = BUS_VIRTUAL;
    description.id.version = 1;
    /* No Xiaomi vendor/product ID claim; the module checks the name only. */
    ssize_t amount;
    do {
        amount = write(bridge->uinput_fd, &description, sizeof(description));
    } while (amount < 0 && errno == EINTR);
    if (amount != (ssize_t)sizeof(description)) {
        if (amount >= 0) errno = EIO;
        return -1;
    }
    if (ioctl(bridge->uinput_fd, UI_DEV_CREATE) < 0) return -1;
    bridge->uinput_created = true;
    return 0;
}

static int status_write(struct bridge *bridge, double now) {
    char temporary[PATH_MAX];
    int size = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld",
                        bridge->options->status_path, (long)getpid());
    if (size < 0 || (size_t)size >= sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char text[1536];
    const struct counters *c = &bridge->counts;
    size = snprintf(text, sizeof(text),
        "{\"version\":1,\"status\":\"%s\",\"reason\":\"%s\","
        "\"pid\":%ld,\"mask\":%u,\"uptime_seconds\":%.3f,"
        "\"raw_frames\":%" PRIu64 ",\"valid_frames\":%" PRIu64 ","
        "\"stale_frames\":%" PRIu64 ",\"malformed_frames\":%" PRIu64 ","
        "\"ignored_lines\":%" PRIu64 ",\"oversized_lines\":%" PRIu64 ","
        "\"transitions\":%" PRIu64 ",\"timeouts\":%" PRIu64 ","
        "\"mask_frames\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]}\n",
        bridge->state, bridge->reason, (long)getpid(), bridge->mask,
        now >= bridge->started ? now - bridge->started : 0.0,
        c->raw, c->valid, c->stale, c->malformed, c->ignored, c->oversized,
        c->transitions, c->timeouts,
        c->mask_frames[0], c->mask_frames[1], c->mask_frames[2], c->mask_frames[3]);
    if (size < 0 || (size_t)size >= sizeof(text)) {
        errno = EOVERFLOW;
        return -1;
    }
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    int result = write_complete(fd, text, (size_t)size);
    int saved = errno;
    if (close(fd) < 0 && result == 0) {
        result = -1;
        saved = errno;
    }
    if (result == 0 && rename(temporary, bridge->options->status_path) < 0) {
        result = -1;
        saved = errno;
    }
    if (result != 0) {
        (void)unlink(temporary);
        errno = saved;
        return -1;
    }
    bridge->last_status = now;
    bridge->status_dirty = false;
    return 0;
}

/* Any existing stop marker ends the run. Errors other than ENOENT also stop
 * the daemon rather than silently losing its externally controlled stop path. */
static int stop_requested(const char *path) {
    struct stat status;
    if (lstat(path, &status) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}

static int acquire_lock(struct bridge *bridge) {
    if (!bridge->options->lock_path) return 0;
    bridge->lock_fd = open(bridge->options->lock_path,
                          O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (bridge->lock_fd < 0 || flock(bridge->lock_fd, LOCK_EX | LOCK_NB) < 0) return -1;
    char pid[40];
    int length = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
    if (ftruncate(bridge->lock_fd, 0) < 0 || lseek(bridge->lock_fd, 0, SEEK_SET) < 0 ||
        write_complete(bridge->lock_fd, pid, (size_t)length) < 0) return -1;
    /* Never unlink a flock file: replacing its inode would defeat exclusivity. */
    return 0;
}

static int accept_live_line(struct bridge *bridge, const char *line) {
    struct frame frame;
    bool raw;
    enum parse_result parsed = parse_frame(line, &frame, &raw);
    if (raw) ++bridge->counts.raw;
    if (parsed == PARSE_IGNORE) {
        ++bridge->counts.ignored;
        return 0;
    }
    if (parsed == PARSE_INVALID) {
        ++bridge->counts.malformed;
        return set_mask(bridge, 0, "malformed_frame");
    }
    double now = boot_time();
    if (now < 0.0) return -1;
    double age = now - frame.stamp;
    if (age > STALE_SECONDS || age < -STALE_SECONDS ||
        (bridge->have_stamp && frame.stamp < bridge->last_stamp)) {
        ++bridge->counts.stale;
        return set_mask(bridge, 0, "stale_frame");
    }
    bridge->last_stamp = frame.stamp;
    bridge->have_stamp = true;
    /* Base expiry on the originating frame, not a delayed read receipt. */
    bridge->last_fresh = frame.stamp < now ? frame.stamp : now;
    ++bridge->counts.valid;
    ++bridge->counts.mask_frames[frame.mask];
    return set_mask(bridge, frame.mask, "frame");
}

static int install_signals(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    if (sigemptyset(&action.sa_mask) < 0) return -1;
    if (sigaction(SIGTERM, &action, NULL) < 0 ||
        sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGHUP, &action, NULL) < 0) return -1;
    action.sa_handler = SIG_IGN;
    return sigaction(SIGPIPE, &action, NULL);
}

static int cleanup_control(const char *path) {
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    int result = write_complete(fd, "0\n", 2);
    int saved = errno;
    if (close(fd) < 0 && result == 0) {
        result = -1;
        saved = errno;
    }
    errno = saved;
    return result;
}

/* This opt-in teardown is deliberately not parameterized beyond the single
 * literal instance accepted by the CLI. It never clears kprobe_events or
 * disables another instance. The supervisor performs the same idempotent
 * cleanup as a second line of defense. */
static int cleanup_private_trace(void) {
    int result = 0;
    if (cleanup_control(CLEANUP_INSTANCE "/tracing_on") < 0) {
        error_message("trace_cleanup_off");
        result = -1;
    }
    if (cleanup_control(CLEANUP_INSTANCE "/events/" CLEANUP_EVENT "/enable") < 0) {
        error_message("trace_cleanup_disable");
        result = -1;
    }
    if (rmdir(CLEANUP_INSTANCE) < 0 && errno != ENOENT) {
        error_message("trace_cleanup_instance");
        result = -1;
    }
    bool exists = false;
    FILE *events = fopen("/sys/kernel/tracing/kprobe_events", "re");
    if (!events) {
        error_message("trace_cleanup_inventory");
        return -1;
    }
    char line[LINE_CAPACITY];
    while (fgets(line, sizeof(line), events)) {
        const char *name = strchr(line, ':');
        if (!name) continue;
        ++name;
        size_t length = sizeof(CLEANUP_EVENT) - 1;
        if (!strncmp(name, CLEANUP_EVENT, length) && whitespace(name[length])) {
            exists = true;
            break;
        }
    }
    if (ferror(events)) {
        error_message("trace_cleanup_inventory_read");
        result = -1;
    }
    fclose(events);
    if (!exists) return result;
    int fd = open("/sys/kernel/tracing/kprobe_events", O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0) {
        error_message("trace_cleanup_event_open");
        return -1;
    }
    const char command[] = "-:" CLEANUP_EVENT "\n";
    if (write_complete(fd, command, sizeof(command) - 1) < 0 && errno != ENOENT) {
        error_message("trace_cleanup_event");
        result = -1;
    }
    if (close(fd) < 0) result = -1;
    return result;
}

static int run_live(const struct options *options) {
    struct bridge bridge;
    memset(&bridge, 0, sizeof(bridge));
    bridge.options = options;
    bridge.trace_fd = bridge.uinput_fd = bridge.lock_fd = -1;
    bridge.state = "starting";
    bridge.reason = "startup";
    bridge.started = boot_time();
    int result = 1;
    bool own_lock = false;
    if (bridge.started < 0.0) {
        error_message("clock");
        return 1;
    }
    if (install_signals() < 0) {
        error_message("signals");
        return 1;
    }
    pid_t supervisor = getppid();
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0) {
        error_message("parent_watch");
        return 1;
    }
    /* Parent may exit before or between getppid() and prctl(). Acquire our
     * lock before cleaning its trace instance; never create uinput in that case. */
    bool parent_missing = supervisor <= 1 || getppid() != supervisor;
    if (acquire_lock(&bridge) < 0) {
        error_message("lock");
        /* Do not overwrite the running instance's status on a lock conflict. */
        if (bridge.lock_fd >= 0) close(bridge.lock_fd);
        return 1;
    }
    own_lock = true;
    if (parent_missing || requested_signal) {
        bridge.reason = "parent_exit";
        result = 0;
        goto cleanup;
    }
    int marker = stop_requested(options->stop_path);
    if (marker != 0) {
        bridge.reason = marker > 0 ? "stop_file" : "stop_file_error";
        result = marker > 0 ? 0 : 1;
        if (marker < 0) error_message("stop_file");
        goto cleanup;
    }
    bridge.trace_fd = open(options->trace_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (bridge.trace_fd < 0) {
        bridge.reason = "trace_open_error";
        error_message("trace_open");
        goto cleanup;
    }
    if (create_device(&bridge) < 0) {
        bridge.reason = "uinput_error";
        error_message("uinput_create");
        goto cleanup;
    }
    bridge.state = "running";
    bridge.status_dirty = true;
    if (status_write(&bridge, boot_time()) < 0) {
        bridge.reason = "status_error";
        bridge.status_error_reported = true;
        error_message("status");
        goto cleanup;
    }
    fprintf(stdout, "dell_pen_bridge: started pid=%ld\n", (long)getpid());
    fflush(stdout);

    /* Creation and InputReader discovery are asynchronous. Suppress startup
     * presses for the first second; stale queued input remains rejected. */
    double accept_after = boot_time() + 1.0;
    char line[LINE_CAPACITY];
    size_t line_length = 0;
    bool dropping = false;
    for (;;) {
        double now = boot_time();
        if (now < 0.0) {
            bridge.reason = "clock_error";
            error_message("clock");
            break;
        }
        if (requested_signal) {
            bridge.reason = "signal";
            result = 0;
            break;
        }
        marker = stop_requested(options->stop_path);
        if (marker != 0) {
            bridge.reason = marker > 0 ? "stop_file" : "stop_file_error";
            result = marker > 0 ? 0 : 1;
            if (marker < 0) error_message("stop_file");
            break;
        }
        if (options->max_seconds && now - bridge.started >= options->max_seconds) {
            bridge.reason = "max_seconds";
            result = 0;
            break;
        }
        if (bridge.mask && now - bridge.last_fresh >= STALE_SECONDS) {
            ++bridge.counts.timeouts;
            if (set_mask(&bridge, 0, "timeout") < 0) {
                bridge.reason = "uinput_error";
                error_message("uinput_release");
                break;
            }
        }
        if (bridge.status_dirty || now - bridge.last_status >= STATUS_SECONDS) {
            if (status_write(&bridge, now) < 0) {
                bridge.reason = "status_error";
                bridge.status_error_reported = true;
                error_message("status");
                break;
            }
        }

        int timeout = bridge.mask ? 200 : 1000;
        if (bridge.mask) {
            double left = (STALE_SECONDS - (now - bridge.last_fresh)) * 1000.0;
            if (left < timeout) timeout = left > 0.0 ? (int)left : 0;
        }
        if (options->max_seconds) {
            double left = (options->max_seconds - (now - bridge.started)) * 1000.0;
            if (left < timeout) timeout = left > 0.0 ? (int)left : 0;
        }
        struct pollfd poll_fd = {.fd = bridge.trace_fd, .events = POLLIN};
        int ready = poll(&poll_fd, 1, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) {
            bridge.reason = "trace_poll_error";
            error_message("trace_poll");
            break;
        }
        if (ready == 0) continue;
        if (poll_fd.revents & (POLLERR | POLLNVAL)) {
            bridge.reason = "trace_poll_error";
            break;
        }
        if (!(poll_fd.revents & (POLLIN | POLLHUP))) continue;
        char buffer[4096];
        ssize_t amount = read(bridge.trace_fd, buffer, sizeof(buffer));
        if (amount < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (amount <= 0) {
            bridge.reason = amount == 0 ? "trace_eof" : "trace_read_error";
            if (amount < 0) error_message("trace_read");
            break;
        }
        for (ssize_t i = 0; i < amount; ++i) {
            if (requested_signal) break;
            if (buffer[i] != '\n') {
                if (dropping) continue;
                if (line_length + 1 >= sizeof(line)) {
                    dropping = true;
                    line_length = 0;
                    ++bridge.counts.oversized;
                    if (set_mask(&bridge, 0, "oversized_line") < 0) {
                        bridge.reason = "uinput_error";
                        goto cleanup;
                    }
                } else {
                    line[line_length++] = buffer[i];
                }
                continue;
            }
            if (dropping) {
                dropping = false;
                continue;
            }
            line[line_length] = '\0';
            line_length = 0;
            if (boot_time() < accept_after) continue;
            if (accept_live_line(&bridge, line) < 0) {
                bridge.reason = "input_processing_error";
                error_message("input_processing");
                goto cleanup;
            }
            /* Publish each actual state change, never every pen packet. */
            if (bridge.status_dirty && status_write(&bridge, boot_time()) < 0) {
                bridge.reason = "status_error";
                bridge.status_error_reported = true;
                error_message("status");
                goto cleanup;
            }
        }
    }

cleanup:
    if (bridge.uinput_created) {
        if (release_both(&bridge) < 0) {
            error_message("final_release");
            result = 1;
        }
        /* Give InputReader time to observe UP before the device disappears. */
        struct timespec grace = {.tv_sec = 0, .tv_nsec = 100000000};
        (void)nanosleep(&grace, NULL);
        if (ioctl(bridge.uinput_fd, UI_DEV_DESTROY) < 0) {
            error_message("uinput_destroy");
            result = 1;
        }
    }
    if (bridge.uinput_fd >= 0) close(bridge.uinput_fd);
    if (bridge.trace_fd >= 0) close(bridge.trace_fd);
    if (own_lock && options->cleanup_instance && cleanup_private_trace() < 0) result = 1;
    bridge.mask = 0;
    bridge.state = result == 0 ? "stopped" : "error";
    if (own_lock && status_write(&bridge, boot_time()) < 0 && !bridge.status_error_reported)
        error_message("final_status");
    fprintf(stdout, "dell_pen_bridge: stopped reason=%s frames=%" PRIu64
            " transitions=%" PRIu64 "\n", bridge.reason, bridge.counts.valid,
            bridge.counts.transitions);
    fflush(stdout);
    if (bridge.lock_fd >= 0) close(bridge.lock_fd);
    return result;
}

static void replay_transition(unsigned *mask, unsigned next, double at,
                              const char *reason, struct counters *counts) {
    if (*mask == next) return;
    printf("REPLAY transition at=%.6f from=%u to=%u reason=%s\n", at, *mask, next, reason);
    *mask = next;
    ++counts->transitions;
}

static int run_replay(const char *path) {
    FILE *input = fopen(path, "rb");
    if (!input) {
        error_message("replay_open");
        return 1;
    }
    struct stat file_status;
    if (fstat(fileno(input), &file_status) < 0 || !S_ISREG(file_status.st_mode)) {
        fprintf(stderr, "dell_pen_bridge: replay requires a regular input file\n");
        fclose(input);
        return 1;
    }
    struct counters counts = {0};
    char line[LINE_CAPACITY];
    unsigned mask = 0;
    double last = 0.0;
    bool have_last = false;
    while (fgets(line, sizeof(line), input)) {
        if (!strchr(line, '\n') && !feof(input)) {
            ++counts.oversized;
            int value;
            do { value = fgetc(input); } while (value != '\n' && value != EOF);
            replay_transition(&mask, 0, last, "oversized_line", &counts);
            continue;
        }
        struct frame frame;
        bool raw;
        enum parse_result parsed = parse_frame(line, &frame, &raw);
        if (raw) ++counts.raw;
        if (parsed == PARSE_IGNORE) {
            ++counts.ignored;
            continue;
        }
        if (parsed == PARSE_INVALID) {
            ++counts.malformed;
            replay_transition(&mask, 0, last, "malformed_frame", &counts);
            continue;
        }
        if (have_last && frame.stamp < last) {
            ++counts.stale;
            replay_transition(&mask, 0, last, "out_of_order", &counts);
            continue;
        }
        if (mask && have_last && frame.stamp - last >= STALE_SECONDS) {
            ++counts.timeouts;
            replay_transition(&mask, 0, last + STALE_SECONDS, "timeout", &counts);
        }
        ++counts.valid;
        ++counts.mask_frames[frame.mask];
        replay_transition(&mask, frame.mask, frame.stamp, "frame", &counts);
        last = frame.stamp;
        have_last = true;
    }
    int result = ferror(input) ? 1 : 0;
    if (result) error_message("replay_read");
    fclose(input);
    replay_transition(&mask, 0, last, "eof", &counts);
    printf("REPLAY summary raw=%" PRIu64 " valid=%" PRIu64 " stale=%" PRIu64
           " malformed=%" PRIu64 " ignored=%" PRIu64 " oversized=%" PRIu64
           " transitions=%" PRIu64 " timeouts=%" PRIu64
           " mask0=%" PRIu64 " mask1=%" PRIu64 " mask2=%" PRIu64 " mask3=%" PRIu64 "\n",
           counts.raw, counts.valid, counts.stale, counts.malformed, counts.ignored,
           counts.oversized, counts.transitions, counts.timeouts, counts.mask_frames[0],
           counts.mask_frames[1], counts.mask_frames[2], counts.mask_frames[3]);
    return result;
}

static void usage(FILE *output) {
    fprintf(output,
        "Usage: dell_pen_bridge --trace PATH --stop-file PATH --status PATH\n"
        "                       [--lock PATH] [--max-seconds N]\n"
        "                       [--cleanup-instance " CLEANUP_INSTANCE "]\n"
        "       dell_pen_bridge --replay FILE\n"
        "       dell_pen_bridge --help\n"
        "N=0 (default) runs until stopped. Live paths must be absolute.\n"
        "Replay is read-only and cannot be combined with live options.\n");
}

static int parse_options(int argc, char **argv, struct options *options) {
    memset(options, 0, sizeof(*options));
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            if (argc != 2) return -1;
            return 1;
        }
        if (i + 1 >= argc) return -1;
        const char *name = argv[i];
        const char *value = argv[++i];
        if (!*value) return -1;
        const char **slot = NULL;
        if (!strcmp(name, "--trace")) slot = &options->trace_path;
        else if (!strcmp(name, "--stop-file")) slot = &options->stop_path;
        else if (!strcmp(name, "--status")) slot = &options->status_path;
        else if (!strcmp(name, "--lock")) slot = &options->lock_path;
        else if (!strcmp(name, "--replay")) slot = &options->replay_path;
        else if (!strcmp(name, "--cleanup-instance")) slot = &options->cleanup_instance;
        else if (!strcmp(name, "--max-seconds")) {
            if (options->max_given || !isdigit((unsigned char)value[0])) return -1;
            char *end;
            errno = 0;
            unsigned long number = strtoul(value, &end, 10);
            if (errno || *end || number > UINT_MAX) return -1;
            options->max_seconds = (unsigned)number;
            options->max_given = true;
        } else return -1;
        if (slot) {
            if (*slot) return -1;
            *slot = value;
        }
    }
    if (options->replay_path) {
        return options->trace_path || options->stop_path || options->status_path ||
               options->lock_path || options->max_given || options->cleanup_instance ? -1 : 0;
    }
    if (!options->trace_path || !options->stop_path || !options->status_path) return -1;
    if (options->cleanup_instance &&
        (!options->lock_path || strcmp(options->cleanup_instance, CLEANUP_INSTANCE) ||
         strcmp(options->trace_path, CLEANUP_INSTANCE "/trace_pipe"))) return -1;
    const char *paths[4] = {options->trace_path, options->stop_path,
                            options->status_path, options->lock_path};
    for (unsigned i = 0; i < 4; ++i) {
        if (!paths[i]) continue;
        if (paths[i][0] != '/' || strlen(paths[i]) > PATH_MAX - 48) return -1;
        for (unsigned j = 0; j < i; ++j) {
            if (paths[j] && !strcmp(paths[i], paths[j])) return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    struct options options;
    int parsed = parse_options(argc, argv, &options);
    if (parsed != 0) {
        usage(parsed > 0 ? stdout : stderr);
        return parsed > 0 ? 0 : 2;
    }
    if (options.replay_path) return run_replay(options.replay_path);
    return run_live(&options);
}
