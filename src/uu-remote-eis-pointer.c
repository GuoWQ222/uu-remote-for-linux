/* SPDX-License-Identifier: MIT
 * Input sender for an already-authorized Portal ConnectToEIS fd.
 * stdin: A/R/B/S/K <x> <y>; stdout: READY <x> <y> <w> <h>, then OK/ERROR.
 * Deliberately restricted to one absolute region. No independent socket or
 * desktop authorization is created by this helper.
 */
#define _POSIX_C_SOURCE 200809L
#include <libei.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_DEVICES 16
struct device_slot {
    struct ei_device *device;
    bool active;
    uint32_t sequence;
    unsigned int pressed;
    bool keys[768];
};
static struct device_slot devices[MAX_DEVICES];
static struct ei_device *mapped_device;

static struct ei_device *find_device(enum ei_device_capability capability)
{
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active &&
            ei_device_has_capability(devices[i].device, capability))
            return devices[i].device;
    }
    return NULL;
}

static int dispatch_events(struct ei *context)
{
    struct ei_event *event;
    ei_dispatch(context);
    while ((event = ei_get_event(context))) {
        enum ei_event_type type = ei_event_get_type(event);
        if (type == EI_EVENT_DISCONNECT) {
            ei_event_unref(event);
            return -1;
        }
        if (type == EI_EVENT_SEAT_ADDED) {
            ei_seat_bind_capabilities(ei_event_get_seat(event),
                EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE,
                EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_SCROLL, EI_DEVICE_CAP_KEYBOARD, NULL);
        } else if (type == EI_EVENT_DEVICE_ADDED) {
            struct ei_device *added = ei_event_get_device(event);
            /* Without a mapping-id match, multiple absolute devices are ambiguous. */
            if (ei_device_has_capability(added, EI_DEVICE_CAP_POINTER_ABSOLUTE)) {
                for (size_t j = 0; j < MAX_DEVICES; j++) {
                    if (devices[j].device && ei_device_has_capability(
                            devices[j].device, EI_DEVICE_CAP_POINTER_ABSOLUTE)) {
                        ei_event_unref(event);
                        return -1;
                    }
                }
            }
            size_t i;
            for (i = 0; i < MAX_DEVICES; i++) {
                if (!devices[i].device) {
                    devices[i].device = ei_device_ref(ei_event_get_device(event));
                    break;
                }
            }
            if (i == MAX_DEVICES) {
                ei_event_unref(event);
                return -1;
            }
        } else if (type == EI_EVENT_DEVICE_RESUMED ||
                   type == EI_EVENT_DEVICE_PAUSED ||
                   type == EI_EVENT_DEVICE_REMOVED) {
            for (size_t i = 0; i < MAX_DEVICES; i++) {
                if (devices[i].device != ei_event_get_device(event))
                    continue;
                if (type == EI_EVENT_DEVICE_REMOVED && devices[i].device == mapped_device) {
                    ei_event_unref(event);
                    return -1;
                }
                if (type == EI_EVENT_DEVICE_RESUMED) {
                    devices[i].active = true;
                    ei_device_start_emulating(devices[i].device,
                                               ++devices[i].sequence);
                } else {
                    devices[i].active = false;
                    if (type == EI_EVENT_DEVICE_REMOVED) {
                        devices[i].device = ei_device_unref(devices[i].device);
                        devices[i].sequence = 0;
                    }
                }
            }
        }
        ei_event_unref(event);
    }
    return 0;
}

static bool send_command(struct ei *context, const char *line)
{
    char kind, trailing;
    double x, y;
    struct ei_device *device = NULL;
    if (sscanf(line, "%c %lf %lf %c", &kind, &x, &y, &trailing) != 3 ||
        !isfinite(x) || !isfinite(y))
        return false;
    switch (kind) {
    case 'A':
        device = find_device(EI_DEVICE_CAP_POINTER_ABSOLUTE);
        if (!device || !ei_device_get_region_at(device, x, y))
            return false;
        ei_device_pointer_motion_absolute(device, x, y);
        break;
    case 'R':
        if (fabs(x) > INT_MAX || fabs(y) > INT_MAX)
            return false;
        device = find_device(EI_DEVICE_CAP_POINTER);
        if (device)
            ei_device_pointer_motion(device, x, y);
        break;
    case 'B':
        /* Linux evdev BTN_LEFT through BTN_TASK; no keyboard codes. */
        if (x < 0x110 || x > 0x117 || x != floor(x) || (y != 0 && y != 1))
            return false;
        device = find_device(EI_DEVICE_CAP_BUTTON);
        if (device) {
            ei_device_button_button(device, (uint32_t)x, y != 0);
            for (size_t i = 0; i < MAX_DEVICES; i++) {
                if (devices[i].device != device)
                    continue;
                unsigned int mask = 1U << ((unsigned int)x - 0x110);
                if (y) devices[i].pressed |= mask;
                else devices[i].pressed &= ~mask;
            }
        }
        break;
    case 'K':
        if (x < 0 || x > 767 || x != floor(x) || (y != 0 && y != 1))
            return false;
        device = find_device(EI_DEVICE_CAP_KEYBOARD);
        if (device) {
            ei_device_keyboard_key(device, (uint32_t)x, y != 0);
            for (size_t i = 0; i < MAX_DEVICES; i++)
                if (devices[i].device == device)
                    devices[i].keys[(unsigned int)x] = y != 0;
        }
        break;
    case 'S':
        if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX ||
            x != floor(x) || y != floor(y))
            return false;
        device = find_device(EI_DEVICE_CAP_SCROLL);
        if (device)
            ei_device_scroll_discrete(device, (int32_t)x, (int32_t)y);
        break;
    default:
        return false;
    }
    if (!device)
        return false;
    ei_device_frame(device, ei_now(context));
    ei_dispatch(context);
    return true;
}

int main(int argc, char **argv)
{
    struct ei *context;
    char *end;
    long fd;
    bool ready = false;
    int status = EXIT_FAILURE;
    char input[128];
    size_t used = 0;
    if (argc != 2)
        return EXIT_FAILURE;
    errno = 0;
    fd = strtol(argv[1], &end, 10);
    if (errno || !argv[1][0] || *end || fd < 0 || fd > INT_MAX)
        return EXIT_FAILURE;
    context = ei_new_sender(NULL);
    if (!context)
        return EXIT_FAILURE;
    setvbuf(stdout, NULL, _IOLBF, 0);
    ei_configure_name(context, "UU authorized Portal pointer");
    if (ei_setup_backend_fd(context, (int)fd) < 0)
        goto out;
    while (true) {
        struct ei_device *absolute;
        struct pollfd fds[] = {
            {ei_get_fd(context), POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}
        };
        if (dispatch_events(context) < 0)
            break;
        absolute = find_device(EI_DEVICE_CAP_POINTER_ABSOLUTE);
        if (!ready && absolute && find_device(EI_DEVICE_CAP_BUTTON) &&
            find_device(EI_DEVICE_CAP_KEYBOARD)) {
            struct ei_region *region = ei_device_get_region(absolute, 0);
            if (!region || ei_device_get_region(absolute, 1) ||
                !ei_region_get_width(region) || !ei_region_get_height(region))
                break;
            printf("READY %u %u %u %u\n", ei_region_get_x(region),
                ei_region_get_y(region), ei_region_get_width(region),
                ei_region_get_height(region));
            mapped_device = absolute;
            ready = true;
        }
        /* A removed/replaced absolute device cannot silently change mapping. */
        if (ready && !absolute) {
            /* A paused device remains known and may resume. */
            bool known = false;
            for (size_t i = 0; i < MAX_DEVICES; i++)
                if (devices[i].device && ei_device_has_capability(
                        devices[i].device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
                    known = true;
            if (!known)
                break;
        }
        int result = poll(fds, 2, 5000);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[0].revents && dispatch_events(context) < 0)
            break;
        if (fds[1].revents & POLLIN) {
            char c;
            ssize_t count = read(STDIN_FILENO, &c, 1);
            if (count <= 0) {
                status = count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
                break;
            }
            if (c == '\n') {
                input[used] = '\0';
                puts(ready && send_command(context, input) ? "OK" : "ERROR invalid input or unavailable device");
                used = 0;
            } else if (used < sizeof(input) - 1) {
                input[used++] = c;
            } else {
                break;
            }
        } else if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            status = EXIT_SUCCESS;
            break;
        }
    }
out:
    /* Release held buttons on orderly EOF; disconnect invalidates the device. */
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].device) {
            if (status == EXIT_SUCCESS && devices[i].active) {
                for (unsigned int button = 0; button < 8; button++)
                    if (devices[i].pressed & (1U << button))
                        ei_device_button_button(devices[i].device, 0x110 + button, false);
                for (unsigned int key = 0; key < 768; key++)
                    if (devices[i].keys[key])
                        ei_device_keyboard_key(devices[i].device, key, false);
                ei_device_frame(devices[i].device, ei_now(context));
                ei_device_stop_emulating(devices[i].device);
            }
            ei_device_unref(devices[i].device);
        }
    }
    ei_dispatch(context);
    ei_unref(context);
    return status;
}
