/* SPDX-License-Identifier: MIT
 * Local libeis peer for tests; never connects to a desktop or Portal. */
#define _POSIX_C_SOURCE 200809L
#include <libeis.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static enum eis_device_capability caps[] = {EIS_DEVICE_CAP_POINTER,
    EIS_DEVICE_CAP_POINTER_ABSOLUTE, EIS_DEVICE_CAP_BUTTON, EIS_DEVICE_CAP_SCROLL, EIS_DEVICE_CAP_KEYBOARD};
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    struct eis *ctx = eis_new(NULL);
    struct eis_client *client = NULL;
    struct eis_seat *seat = NULL;
    struct eis_device *device = NULL;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (eis_setup_backend_socket(ctx, argv[1]) < 0) return 3;
    puts("LISTEN");
    while (1) {
        eis_dispatch(ctx);
        struct eis_event *event;
        while ((event = eis_get_event(ctx))) {
            switch (eis_event_get_type(event)) {
            case EIS_EVENT_CLIENT_CONNECT:
                client = eis_client_ref(eis_event_get_client(event));
                eis_client_connect(client);
                seat = eis_client_new_seat(client, "test-seat");
                for (size_t i=0;i<5;i++) eis_seat_configure_capability(seat,caps[i]);
                eis_seat_add(seat);
                break;
            case EIS_EVENT_SEAT_BIND:
                if (device) break;
                device = eis_seat_new_device(seat);
                eis_device_configure_type(device,EIS_DEVICE_TYPE_VIRTUAL);
                eis_device_configure_name(device,"test-pointer");
                for(size_t i=0;i<5;i++) eis_device_configure_capability(device,caps[i]);
                for(int i=0;i<atoi(argv[2]);i++) {
                    struct eis_region *region=eis_device_new_region(device);
                    eis_region_set_offset(region,10 + 1600*i,20);
                    eis_region_set_size(region,1600,1000);
                    eis_region_set_physical_scale(region,2.0);
                    eis_region_add(region);eis_region_unref(region);
                }
                eis_device_add(device);eis_device_resume(device);
                break;
            case EIS_EVENT_DEVICE_START_EMULATING:
                /* libeis 1.5 omits the sequence when queueing this event. */
                puts("START");break;
            case EIS_EVENT_POINTER_MOTION_ABSOLUTE:
                printf("A %.2f %.2f\n",eis_event_pointer_get_absolute_x(event),eis_event_pointer_get_absolute_y(event));break;
            case EIS_EVENT_POINTER_MOTION:
                printf("R %.2f %.2f\n",eis_event_pointer_get_dx(event),eis_event_pointer_get_dy(event));break;
            case EIS_EVENT_BUTTON_BUTTON:
                printf("B %u %d\n",eis_event_button_get_button(event),eis_event_button_get_is_press(event));break;
            case EIS_EVENT_KEYBOARD_KEY:
                printf("K %u %d\n",eis_event_keyboard_get_key(event),eis_event_keyboard_get_key_is_press(event));break;
            case EIS_EVENT_SCROLL_DISCRETE:
                printf("S %d %d\n",eis_event_scroll_get_discrete_dx(event),eis_event_scroll_get_discrete_dy(event));break;
            case EIS_EVENT_CLIENT_DISCONNECT:
                puts("DISCONNECTED");break;
            default:break;
            }
            eis_event_unref(event);
        }
        eis_dispatch(ctx);
        struct pollfd fds[]={{eis_get_fd(ctx),POLLIN,0},{STDIN_FILENO,POLLIN,0}};
        if(poll(fds,2,100)<0) break;
        if(fds[1].revents&POLLIN) {
            char c;
            if(read(STDIN_FILENO,&c,1)!=1) break;
            if(c=='p' && device) {eis_device_pause(device);puts("PAUSED");}
            if(c=='r' && device) {eis_device_resume(device);puts("RESUMED");}
            if(c=='d' && device) {eis_device_remove(device);puts("REMOVED");}
            if(c=='q') break;
        }
        eis_dispatch(ctx);
        if(fds[1].revents&POLLHUP) break;
    }
    if(device)eis_device_unref(device);
    if(seat)eis_seat_unref(seat);
    if(client)eis_client_unref(client);
    eis_unref(ctx);
    return 0;
}
