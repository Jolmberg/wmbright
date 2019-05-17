/* WMBright -- a brightness control using randr.
 * Copyright (C) 2000, 2001 timecop@japan.co.jp
 * Mixer code in version 3.0 based on mixer api library by
 * Daniel Richard G. <skunk@mit.edu>, which in turn was based on
 * the mixer code in WMix 2.x releases.
 * Copyright (C) 2019 Johannes Holmberg, johannes@update.uu.se
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include "include/common.h"
#include "include/misc.h"
#include "include/ui_x.h"
#include "include/mmkeys.h"
#include "include/config.h"
#include "include/brightness.h"


static Display *display;
static bool button_pressed = false;
static bool slider_pressed = false;
static double prev_button_press_time = 0.0;

static float display_height;
static float display_width;
static int mouse_drag_home_x;
static int mouse_drag_home_y;
static int idle_loop;
static int msg_length;

/* local stuff */
static void signal_catch(int sig);
static void button_press_event(XButtonEvent *event);
static void button_release_event(XButtonEvent *event);
static int  key_press_event(XKeyEvent *event);
static void motion_event(XMotionEvent *event);


int main(int argc, char **argv)
{
    XEvent event;
    int rr_event_base, rr_error_base;
    bool need_reinit = false;

    config_init();
    parse_cli_options(argc, argv);
    config_read();
    config_set_defaults();
    XInitThreads();
    display = XOpenDisplay(config.display_name);
    if (display == NULL) {
        const char *name;

        if (config.display_name) {
            name = config.display_name;
        } else {
            name = getenv("DISPLAY");
            if (name == NULL) {
                fprintf(stderr, "wbright:error: Unable to open display, variable $DISPLAY not set\n");
                return EXIT_FAILURE;
            }
        }
        fprintf(stderr, "wmbright:error: Unable to open display \"%s\"\n", name);
        return EXIT_FAILURE;
    }

    if (!XRRQueryExtension(display, &rr_event_base, &rr_error_base)) {
        fprintf(stderr, "wmbright:error: randr extension not found\n");
        return EXIT_FAILURE;
    }
    int rr_mask = RROutputChangeNotifyMask; //RRScreenChangeNotifyMask;
    XRRSelectInput(display,
                   RootWindow(display, DefaultScreen(display)),
                   rr_mask);

    brightness_init(display, config.verbose);

    display_width = (float)DisplayWidth(display, DefaultScreen(display)) / 4.0;
    display_height = (float)DisplayHeight(display, DefaultScreen(display)) / 2.0;

    dockapp_init(display);
    new_window("wmbright", 64, 64);
    new_osd(60);

    if (config.mmkeys)
        mmkey_install(display);

    config_release();

    blit_string(brightness_get_monitor_name());
    msg_length = strlen(brightness_get_monitor_name());
    scroll_text(3, 4, 35, msg_length, true);
    ui_update();

    /* add click regions */
    add_region(1, 20, 18, 42, 42);    /* knob */
    add_region(2, 3, 41, 14, 9);      /* backlight indicator */
    add_region(3, 3, 32, 14, 9);      /* gamma indicator */

    add_region(8, 3, 50, 7, 10);      /* previous channel */
    add_region(9, 10, 50, 7, 10);     /* next channel */
    add_region(10, 3, 4, 58, 11);     /* re-scroll current channel name */

    /* setup up/down signal handler */
    create_pid_file();
    signal(SIGUSR1, (void *) signal_catch);
    signal(SIGUSR2, (void *) signal_catch);
    while (true) {
        if (button_pressed || slider_pressed || (XPending(display) > 0)) {
            XNextEvent(display, &event);
            switch (event.type) {
            case KeyPress:
                if (key_press_event(&event.xkey))
                    idle_loop = 0;
                break;
            case Expose:
                redraw_window();
                break;
            case ButtonPress:
                button_press_event(&event.xbutton);
                idle_loop = 0;
                break;
            case ButtonRelease:
                button_release_event(&event.xbutton);
                idle_loop = 0;
                break;
            case MotionNotify:
                /* process cursor change, or drag events */
                motion_event(&event.xmotion);
                idle_loop = 0;
                break;
            case LeaveNotify:
                /* go back to standard cursor */
                if ((!button_pressed) && (!slider_pressed))
                    set_cursor(NORMAL_CURSOR);
                break;
            case DestroyNotify:
                XCloseDisplay(display);
                return EXIT_SUCCESS;
            default:
                if (event.type == rr_event_base + RRNotify) {
                    XRRNotifyEvent *notify = (XRRNotifyEvent *)&event;
                    if (notify->subtype == RRNotify_OutputChange) {
                        if (config.verbose)
                            printf("Outputs changed, reconfiguring.\n");
                        need_reinit = true;
                    }
                    XRRUpdateConfiguration(&event);
                }
                break;
            }
        } else {
            if (need_reinit) {
                need_reinit = false;
                brightness_reinit();
                ui_rrnotify();
                continue;
            }
            usleep(100000);
            //brightness_tick();
            scroll_text(3, 4, 35, msg_length, false);
            /* get rid of OSD after a few seconds of idle */
            if ((idle_loop++ > 15) && osd_mapped() && !button_pressed) {
                unmap_osd();
                idle_loop = -10000;
            }
            if (brightness_is_changed())
                ui_update();
        }
    }
    return EXIT_SUCCESS;
}

static void signal_catch(int sig)
{
    switch (sig) {
    case SIGUSR1:
        printf("sigusr1\n");
        brightness_set_level_rel(config.scrollstep);
        if (!osd_mapped())
            map_osd();
        if (osd_mapped())
            update_osd(false);
        ui_update();
        idle_loop = 0;
        break;
    case SIGUSR2:
        printf("sigusr2\n");
        brightness_set_level_rel(-config.scrollstep);
        if (!osd_mapped())
            map_osd();
        if (osd_mapped())
            update_osd(false);
        ui_update();
        idle_loop = 0;
        break;
    }
}

static void button_press_event(XButtonEvent *event)
{
    double button_press_time = get_current_time();
    int x = event->x;
    int y = event->y;
    //bool double_click = false;

    /* handle wheel scrolling to adjust level */
    if (config.mousewheel) {
        if (event->button == config.wheel_button_up) {
            brightness_ready();
            brightness_set_level_rel(config.scrollstep);
            brightness_unready();
            if (!osd_mapped())
                map_osd();
            if (osd_mapped())
                update_osd(false);
            ui_update();
            idle_loop = 0;
            return;
        }
        if (event->button == config.wheel_button_down) {
            brightness_ready();
            brightness_set_level_rel(-config.scrollstep);
            brightness_unready();
            if (!osd_mapped())
                map_osd();
            if (osd_mapped())
                update_osd(false);
            ui_update();
            idle_loop = 0;
            return;
        }
    }

    if ((button_press_time - prev_button_press_time) <= 0.5) {
        //double_click = true;
        prev_button_press_time = 0.0;
    } else
        prev_button_press_time = button_press_time;

    switch (check_region(x, y)) {
    case 1:            /* on knob */
        brightness_ready();
        button_pressed = true;
        slider_pressed = false;
        mouse_drag_home_x = x;
        mouse_drag_home_y = y;
        break;
    case 2:            /* backlight indicator */
        if (brightness_set_method(BACKLIGHT)) {
            unmap_osd();
            map_osd();
            ui_update();
            idle_loop = 0;
        }
        break;
    case 3:            /* gamma indicator */
        if (brightness_set_method(GAMMA)) {
            unmap_osd();
            map_osd();
            ui_update();
            idle_loop = 0;
        }
        break;
   case 8:            /* previous monitor */
        brightness_set_monitor_rel(-1); 
        blit_string(brightness_get_monitor_name());
        msg_length = strlen(brightness_get_monitor_name());
        scroll_text(3, 4, 35, msg_length, true);
        unmap_osd();
        map_osd();
        ui_update();
        idle_loop = 0;
        break;
    case 9:            /* next monitor */
        brightness_set_monitor_rel(1);
        blit_string(brightness_get_monitor_name());
        msg_length = strlen(brightness_get_monitor_name());
        scroll_text(3, 4, 35, msg_length, true);
        unmap_osd();
        map_osd();
        ui_update();
        break;
    case 10:
        scroll_text(3, 4, 35, msg_length, true);
        break;
    default:
        //printf("unknown region pressed\n");
        break;
    }
}

static int key_press_event(XKeyEvent *event)
{
    if (event->keycode == mmkeys.brightness_up) {
        brightness_set_level_rel(config.scrollstep);
        if (!osd_mapped())
            map_osd();
        if (osd_mapped())
            update_osd(false);
        ui_update();
        idle_loop = 0;
        return 1;
    }
    if (event->keycode == mmkeys.brightness_down) {
        brightness_set_level_rel(-config.scrollstep);
        if (!osd_mapped())
            map_osd();
        if (osd_mapped())
            update_osd(false);
        ui_update();
        idle_loop = 0;
        return 1;
    }

    /* Ignore other keys */
    return 0;
}

static void button_release_event(XButtonEvent *event)
{
    int x = event->x;
    int y = event->y;
    int region;

    region = check_region(x, y);

    if (region == 1)
        set_cursor(HAND_CURSOR);

    if (button_pressed) {
        brightness_unready();
    }
    button_pressed = false;
    slider_pressed = false;
}

static void motion_event(XMotionEvent *event)
{
    int x = event->x;
    int y = event->y;
    int region;

    if ((x == mouse_drag_home_x) && (y == mouse_drag_home_y))
        return;

    region = check_region(x, y);

    if (button_pressed) {
        if (y != mouse_drag_home_y) {
            float delta;

            set_cursor(NULL_CURSOR);

            delta = (float)(mouse_drag_home_y - y) / display_height;

            knob_turn(delta);

            if (!osd_mapped())
                map_osd();
            if (osd_mapped())
                update_osd(false);
            idle_loop = 0;
        }
        XWarpPointer(display, None, event->window, x, y, 0, 0,
                     mouse_drag_home_x, mouse_drag_home_y);
        return;
    }

    if (region == 1)
        set_cursor(HAND_CURSOR);
    else
        set_cursor(NORMAL_CURSOR);
}
