/* WMBright -- a brightness control using randr.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/xpm.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>

#include "include/master.xpm"
#include "include/led-on.xpm"
#include "include/led-off.xpm"

#include "include/common.h"
#include "include/misc.h"
#include "include/brightness.h"
#include "include/ui_x.h"
#include "include/config.h"


#ifndef PI
#define PI M_PI
#endif

#define LED_POS_RADIUS 13
#define KNOB_CENTER_X 41
#define KNOB_CENTER_Y 39
#define LED_WIDTH 6
#define LED_HEIGHT 6

struct osd {
    RRCrtc crtc;
    Window win;
    GC gc;
    int width;
    int x;
    int y;
    bool mapped;
    bool on;
    int bar;
};

struct dockapp {
    int width;
    int height;
    Pixmap pixmap;
    Pixmap mask;
    GC gc;
    int ctlength;

    int osd_count;
    struct osd *osd;
};

static Pixmap led_on_pixmap;
static Pixmap led_on_mask;
static Pixmap led_off_pixmap;
static Pixmap led_off_mask;

#define copy_xpm_area(x, y, w, h, dx, dy) \
    XCopyArea(display, dockapp.pixmap, dockapp.pixmap, dockapp.gc, \
	    x, y, w, h, dx, dy)

/* local prototypes */
static Cursor create_null_cursor(Display *x_display);

/* ui stuff */
//static void draw_stereo_led(void);
static void draw_bl_led(void);
//static void draw_mute_led(void);
static void draw_percent(void);
static void draw_knob(float level);
//static void draw_slider(float offset);

/* global variables */
static struct dockapp dockapp;
static Display *display;
static Window win;
static Window iconwin;
static Cursor hand_cursor;
static Cursor null_cursor;
static Cursor norm_cursor;
static Cursor bar_cursor;

/* public methods */
void dockapp_init(Display *x_display)
{
    display = x_display;

    /* XRRScreenResources *screen = XRRGetScreenResources(display, DefaultRootWindow(display)); */
    /* dockapp.osd = (struct osd *)malloc(sizeof(struct osd)*screen->ncrtc); */
    /* dockapp.osd_count = screen->ncrtc; */
    /* for (int i = 0; i < screen->ncrtc; i++) { */
    /*     dockapp.osd[i].crtc = screen->crtcs[i]; */
    /*     dockapp.osd[i].gc = 0; */
    /*     dockapp.osd[i].win = 0; */
	/* } */
    /* XRRFreeScreenResources(screen); */
}

void redraw_window(void)
{
    XCopyArea(display, dockapp.pixmap, iconwin, dockapp.gc,
	      0, 0, dockapp.width, dockapp.height, 0, 0);
    XCopyArea(display, dockapp.pixmap, win, dockapp.gc,
	      0, 0, dockapp.width, dockapp.height, 0, 0);
}

void ui_update(void)
{
    //draw_stereo_led();
    draw_bl_led();
    //draw_mute_led();
    draw_knob(brightness_get_level(-1));
    /* draw_slider(brightness_get_balance()); */
    redraw_window();
}

void knob_turn(float delta)
{
    brightness_set_level_rel(delta);
    draw_knob(brightness_get_level(-1));
    redraw_window();
}

int blit_string(const char *text)
{
    register int i;
    register int c;
    register int k;

    k = 0;
    copy_xpm_area(0, 87, 256, 9, 0, 96);

    for (i = 0; text[i] || i > 31; i++) {
	c = toupper(text[i]);
	if (c == '-') {
	    copy_xpm_area(60, 67, 6, 8, k, 96);
	    k += 6;
	}
	if (c == ' ') {
	    copy_xpm_area(66, 67, 6, 8, k, 96);
	    k += 6;
	}
	if (c == '.') {
	    copy_xpm_area(72, 67, 6, 8, k, 96);
	    k += 6;
	}
	if (c >= 'A' && c <= 'Z') {	/* letter */
	    c = c - 'A';
	    copy_xpm_area(c * 6, 77, 6, 8, k, 96);
	    k += 6;
	} else if (c >= '0' && c <= '9') {	/* number */
	    c = c - '0';
	    copy_xpm_area(c * 6, 67, 6, 8, k, 96);
	    k += 6;
	}
    }
    dockapp.ctlength = k;
    return k;
}

void scroll_text(int x, int y, int width, bool reset)
{
    static int pos;
    static int first;
    static int stop;
    
    /* no text scrolling at all */
    if (!config.scrolltext) {
        if (!reset)
            return;
        copy_xpm_area(0, 96, 58, 9, x, y);
        redraw_window();
        return;
    }

    if (reset) {
        pos = 0;
        first = 0;
        stop = 0;
        copy_xpm_area(0, 87, width, 9, x, y);
    }

    if (stop) {
        return;
    }

    if ((first == 0) && pos == 0) {
        pos = width;
        first = 1;
    }

    if (pos < -(dockapp.ctlength)) {
        first = 1;
        pos = width;
        stop = 1;
        return;
    }
    pos -= 2;

    if (pos > 0) {
        copy_xpm_area(0, 87, pos, 9, x, y);	/* clear */
        copy_xpm_area(0, 96, width - pos, 9, x + pos, y);
    } else {			/* don't need to clear, already in text */
        copy_xpm_area(abs(pos), 96, width, 9, x, y);
    }
    redraw_window();
    return;
}

void new_window(char *name, int width, int height)
{
    XpmAttributes attr;
    Pixel fg, bg;
    XGCValues gcval;
    XSizeHints sizehints;
    XClassHint classhint;
    XWMHints wmhints;
    XTextProperty wname;

    dockapp.width = width;
    dockapp.height = height;

    sizehints.flags = USSize | USPosition;
    sizehints.x = 0;
    sizehints.y = 0;
    sizehints.width = width;
    sizehints.height = height;

    fg = BlackPixel(display, DefaultScreen(display));
    bg = WhitePixel(display, DefaultScreen(display));

    win = XCreateSimpleWindow(display, DefaultRootWindow(display),
			      sizehints.x, sizehints.y,
			      sizehints.width, sizehints.height, 1, fg,
			      bg);

    iconwin = XCreateSimpleWindow(display, win, sizehints.x, sizehints.y,
				  sizehints.width, sizehints.height, 1, fg,
				  bg);

    XSetWMNormalHints(display, win, &sizehints);
    classhint.res_name = name;
    classhint.res_class = name;
    XSetClassHint(display, win, &classhint);

#define INPUT_MASK \
    ButtonPressMask \
    | ExposureMask \
    | ButtonReleaseMask \
    | PointerMotionMask \
    | LeaveWindowMask \
    | StructureNotifyMask

    XSelectInput(display, win, INPUT_MASK);
    XSelectInput(display, iconwin, INPUT_MASK);

#undef INPUT_MASk

    XStringListToTextProperty(&name, 1, &wname);
    XSetWMName(display, win, &wname);

    gcval.foreground = fg;
    gcval.background = bg;
    gcval.graphics_exposures = 0;

    dockapp.gc =
	XCreateGC(display, win,
		  GCForeground | GCBackground | GCGraphicsExposures,
		  &gcval);

    attr.exactColors = 0;
    attr.alloc_close_colors = 1;
    attr.closeness = 30000;
    attr.valuemask = (XpmExactColors | XpmAllocCloseColors | XpmCloseness);
    if ((XpmCreatePixmapFromData(display, DefaultRootWindow(display),
				master_xpm, &dockapp.pixmap, &dockapp.mask,
				&attr) != XpmSuccess) ||
	    (XpmCreatePixmapFromData(display, DefaultRootWindow(display),
				led_on_xpm, &led_on_pixmap, &led_on_mask,
				&attr) != XpmSuccess) ||
	    (XpmCreatePixmapFromData(display, DefaultRootWindow(display),
				led_off_xpm, &led_off_pixmap, &led_off_mask,
				&attr) != XpmSuccess)) {
	fputs("Cannot allocate colors for the dockapp pixmaps!\n", stderr);
	exit(EXIT_FAILURE);
    }

    XShapeCombineMask(display, win, ShapeBounding, 0, 0, dockapp.mask,
		      ShapeSet);
    XShapeCombineMask(display, iconwin, ShapeBounding, 0, 0, dockapp.mask,
		      ShapeSet);

    wmhints.initial_state = WithdrawnState;
    wmhints.icon_window = iconwin;
    wmhints.icon_x = sizehints.x;
    wmhints.icon_y = sizehints.y;
    wmhints.window_group = win;
    wmhints.flags =
	StateHint | IconWindowHint | IconPositionHint | WindowGroupHint;
    XSetWMHints(display, win, &wmhints);

    hand_cursor = XCreateFontCursor(display, XC_hand2);
    norm_cursor = XCreateFontCursor(display, XC_left_ptr);
    bar_cursor = XCreateFontCursor(display, XC_sb_up_arrow);
    null_cursor = create_null_cursor(display);

    XMapWindow(display, win);
}

XRRCrtcInfo *crtc_info_by_output_name(char *monitor)
{
    XRRScreenResources *screen = XRRGetScreenResources(display, DefaultRootWindow(display));
    for (int i = 0; i < screen->noutput; i++) {
        XRROutputInfo *output_info = XRRGetOutputInfo(display, screen, screen->outputs[i]);
        if (!strcmp(monitor, output_info->name)) {
	    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen, output_info->crtc);
	    XRRFreeOutputInfo(output_info);
	    XRRFreeScreenResources(screen);
            return crtc_info;
	}
	XRRFreeOutputInfo(output_info);
    }
    XRRFreeScreenResources(screen);
    return NULL;
}

XRRCrtcInfo *crtc_info_by_output_number(int monitor)
{
    XRRScreenResources *screen = XRRGetScreenResources(display, DefaultRootWindow(display));
    if (monitor >= screen->ncrtc) {
	fprintf(stderr, "wmbright:warning: Requested osd monitor number is out of range, clamping\n");
	monitor = screen->ncrtc - 1;
    }
    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen, screen->crtcs[monitor]);
    XRRFreeScreenResources(screen);
    return crtc_info;
}

void new_osd(int height)
{
    Window osdwin;
    Pixel fg = WhitePixel(display, DefaultScreen(display));
    Pixel bg = BlackPixel(display, DefaultScreen(display));
    XGCValues gcval;
    GC gc;
    XSizeHints sizehints;
    XSetWindowAttributes xattributes;
    int win_layer = 6;
    XFontStruct *fs = NULL;
    int width;
    int x;
    int y;
    XRRScreenResources *screen = XRRGetScreenResources(display, DefaultRootWindow(display));
    dockapp.osd_count = brightness_get_monitor_count();
    dockapp.osd = (struct osd *)malloc(sizeof(struct osd) * dockapp.osd_count);
    for (int i = 0; i < dockapp.osd_count; i++) {
        dockapp.osd[i].crtc = screen->crtcs[i];
        dockapp.osd[i].gc = 0;
        dockapp.osd[i].win = 0;
    }

    /* dockapp.osd_count = screen->ncrtc; */
    /* for (int i = 0; i < screen->ncrtc; i++) { */
    /*     dockapp.osd[i].crtc = screen->crtcs[i]; */
    /*     dockapp.osd[i].gc = 0; */
    /*     dockapp.osd[i].win = 0; */
	/* } */
    /* XRRFreeScreenResources(screen); */

    /* -sony-fixed-medium-r-normal--24-170-100-100-c-120-iso8859-1
     * -misc-fixed-medium-r-normal--36-*-75-75-c-*-iso8859-* */

    /* try our cool scaled 36pt fixed font */
    fs = XLoadQueryFont(display,
	    "-misc-fixed-medium-r-normal--36-*-75-75-c-*-iso8859-*");

    if (fs == NULL) {
	/* they don't have it! */
	/* try our next preferred font (100dpi sony) */
        fprintf(stderr, "Trying alternate font\n");
        fs = XLoadQueryFont(display,
                            "-sony-fixed-medium-r-normal--24-*-100-100-c-*-iso8859-*");

        /* they don't have the sony font either */
        if (fs == NULL) {
            fprintf(stderr, "Trying \"fixed\" font\n");
            fs = XLoadQueryFont(display,
                                "fixed");
            /* if they don't have the fixed font, we've got different kind
             * of problems */
            if (fs == NULL) {
                fprintf(stderr, "Your X server is probably broken\n");
                exit(1);
            }
        }
    }
    
    sizehints.flags = USSize | USPosition;

    for (int i = 0; i < dockapp.osd_count; i++) {
        //XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen, dockapp.osd[i].crtc);
        dockapp.osd[i].mapped = false;
        struct dimensions dim = brightness_get_dimensions(i+1);
        
        if (dim.width == 0) {
            dockapp.osd[i].on = false;
            continue;
        }
        width = dim.width - 200;
        x = dim.x + 100;
        y = dim.y + dim.height - 120;

        // Rethink this?
        if (dockapp.osd[i].win &&
            width == dockapp.osd[i].width &&
            x == dockapp.osd[i].x &&
            y == dockapp.osd[i].y) {
            // Nothing important has changed.
            continue;
        }

        sizehints.x = x;
        sizehints.y = y;
        sizehints.width = width;
        sizehints.height = height;
        xattributes.save_under = True;
        xattributes.override_redirect = True;
        xattributes.cursor = None;

        if (dockapp.osd[i].win)
            XDestroyWindow(display, dockapp.osd[i].win);
        osdwin = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                     sizehints.x, sizehints.y, width, height,
                                     0, fg, bg);

        XSetWMNormalHints(display, osdwin, &sizehints);
        XChangeWindowAttributes(display, osdwin, CWSaveUnder | CWOverrideRedirect,
                                &xattributes);
        char name[5] = "osdx";
        name[3] = '0' + i;
        XStoreName(display, osdwin, name);
        XSelectInput(display, osdwin, ExposureMask);
        XChangeProperty(display, osdwin, XInternAtom(display, "_WIN_LAYER", False),
                        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&win_layer, 1);

        gcval.foreground = get_color(display, config.osd_color);
        gcval.background = bg;
        gcval.graphics_exposures = 0;
        if (dockapp.osd[i].gc)
            XFreeGC(display, dockapp.osd[i].gc);
        gc =
            XCreateGC(display, osdwin,
                      GCForeground | GCBackground | GCGraphicsExposures,
                      &gcval);
        XSetFont(display, gc, fs->fid);
        
        dockapp.osd[i].win = osdwin;
        dockapp.osd[i].gc = gc;
        dockapp.osd[i].width = width;
        dockapp.osd[i].x = x;
        dockapp.osd[i].y = y;
    }
}

void update_osd_by_number(int osd, bool up)
{
    int foo;
    if (dockapp.osd[osd].on) {
        float level = brightness_get_level(osd + 1);
        int bar = dockapp.osd[osd].bar;
        foo = (dockapp.osd[osd].width - 20) * level / 20.0;

        if (up) {
            for (int j = 1; j <= foo; j++)
                XFillRectangle(display, dockapp.osd[osd].win, dockapp.osd[osd].gc,
                               j * 20, 30, 5, 25);
        } else if (foo < bar) {
            XClearArea(display, dockapp.osd[osd].win, ((foo+1) * 20), 30,
                       ((bar-foo) * 20), 25, 1);
        } else if (foo > bar) {
            for (int j = (bar > 0 ? bar : 1); j <= foo; j++)
                XFillRectangle(display, dockapp.osd[osd].win, dockapp.osd[osd].gc,
                               j * 20, 30, 5, 25);
        }
        dockapp.osd[osd].bar = foo;
    }
}

void update_osd(bool up)
{
    if (config.osd) {
        if (brightness_get_current_monitor() == 0) { // Update all of them!
            for (int i = 0; i < dockapp.osd_count; i++) {
                if (dockapp.osd[i].on) {
                    update_osd_by_number(i, up);
                }
            }
        } else {
            update_osd_by_number(brightness_get_current_monitor()-1, up);
        }
    }
}


void unmap_osd(void)
{
    if (config.osd) {
        for (int i = 0; i < dockapp.osd_count; i++) {
            if (dockapp.osd[i].mapped) {
                XUnmapWindow(display, dockapp.osd[i].win);
                XFlush(display);
                dockapp.osd[i].mapped = false;
            }
        }
    }
}

void map_osd_by_number(int osd) {
    XMapRaised(display, dockapp.osd[osd].win);
    XDrawString(display, dockapp.osd[osd].win, dockapp.osd[osd].gc, 1, 25,
                brightness_get_method(osd+1), strlen(brightness_get_method(osd+1)));
    update_osd(true);
    XFlush(display);
    dockapp.osd[osd].mapped = true;
}

void map_osd(void)
{
    if (config.osd) {
        if (brightness_get_current_monitor() == 0) { // Map all of them!
            for (int i = 0; i < dockapp.osd_count; i++) {
                if (dockapp.osd[i].on) {
                    map_osd_by_number(i);
                }
            }
        } else {
            map_osd_by_number(brightness_get_current_monitor()-1);
        }
    }
}

/* bool osd_mapped_by_crtc(RRCrtc crtc) */
/* { */
/*     for (int i = 0; i < dockapp.osd_count; i++) { */
/*         if (dockapp.osd[i].crtc == crtc) { */
/*             return dockapp.osd[i].mapped; */
/*         } */
/*     } */
/*     return false; */
/* } */

bool osd_mapped(void)
{
    if (brightness_get_current_monitor() == 0) { 
        for (int i = 0; i < dockapp.osd_count; i++) {
            if (dockapp.osd[i].mapped) {
                return true;
            }
        }
        return false;
    } else {
        return dockapp.osd[brightness_get_current_monitor()-1].mapped;
    }
}

void set_cursor(int type)
{
    static int oldtype;

    if (oldtype == type)
	return;

    switch (type) {
	case NULL_CURSOR:
	    XDefineCursor(display, win, null_cursor);
	    XDefineCursor(display, iconwin, null_cursor);
	    break;
	case NORMAL_CURSOR:
	    XDefineCursor(display, win, norm_cursor);
	    XDefineCursor(display, iconwin, norm_cursor);
	    break;
	case HAND_CURSOR:
	    XDefineCursor(display, win, hand_cursor);
	    XDefineCursor(display, iconwin, hand_cursor);
	    break;
	case BAR_CURSOR:
	    XDefineCursor(display, win, bar_cursor);
	    XDefineCursor(display, iconwin, bar_cursor);
	    break;
    }
    oldtype = type;
}

static void draw_bl_led(void)
{
    if (brightness_has_backlight())	/* backlight exists */
        if (brightness_backlight_selected()) 
            copy_xpm_area(65, 0, 12, 7, 4, 42);	/* BL lit */
        else
            copy_xpm_area(65, 7, 12, 7, 4, 42); /* BL not lit */
    else /* backlight not available */
        copy_xpm_area(65, 14, 12, 7, 4, 42);	/* BL dark */
}

static void draw_percent(void)
{
    int level = brightness_get_percent();

    copy_xpm_area(0, 87, 18, 9, 3, 14);	/* clear percentage */
    
    if (level < 100) {
        if (level >= 10)
            copy_xpm_area((level / 10) * 6, 67, 6, 9, 6, 14);
        copy_xpm_area((level % 10) * 6, 67, 6, 9, 12, 14);
    } else {
        copy_xpm_area(9, 67, 3, 9, 3, 14);
        copy_xpm_area(0, 67, 6, 9, 6, 14);
        copy_xpm_area(0, 67, 6, 9, 12, 14);
    }
}

static void draw_knob(float level)
{
    float bearing, led_x, led_y;
    int led_topleft_x, led_topleft_y;
    Pixmap led_pixmap;

    bearing = (1.25 * PI) - (1.5 * PI) * level;

    led_x = KNOB_CENTER_X + LED_POS_RADIUS * cos(bearing);
    led_y = KNOB_CENTER_Y - LED_POS_RADIUS * sin(bearing);

    led_topleft_x = (int)(led_x - (LED_WIDTH / 2.0) + 0.5);
    led_topleft_y = (int)(led_y - (LED_HEIGHT / 2.0) + 0.5);

    /* clear previous knob picture */
    copy_xpm_area(87, 0, 42, 42, 20, 18);

    /* if (brightness_is_muted()) */
	/* led_pixmap = led_off_pixmap; */
    /* else */
	led_pixmap = led_on_pixmap;

    XCopyArea(display, led_pixmap, dockapp.pixmap, dockapp.gc,
	    0, 0, LED_WIDTH, LED_HEIGHT, led_topleft_x, led_topleft_y);
    draw_percent();
}

static Cursor create_null_cursor(Display *x_display)
{
    Pixmap cursor_mask;
    XGCValues gcval;
    GC gc;
    XColor dummy_color;
    Cursor cursor;

    cursor_mask = XCreatePixmap(x_display, DefaultRootWindow(x_display), 1, 1, 1);
    gcval.function = GXclear;
    gc = XCreateGC(x_display, cursor_mask, GCFunction, &gcval);
    XFillRectangle(x_display, cursor_mask, gc, 0, 0, 1, 1);
    dummy_color.pixel = 0;
    dummy_color.red = 0;
    dummy_color.flags = 04;
    cursor = XCreatePixmapCursor(x_display, cursor_mask, cursor_mask,
		&dummy_color, &dummy_color, 0, 0);
    XFreePixmap(x_display, cursor_mask);
    XFreeGC(x_display, gc);

    return cursor;
}

unsigned long get_color(Display *display, char *color_name)
{
    XColor color;
    XWindowAttributes winattr;
    Status status;

    XGetWindowAttributes(display,
	    RootWindow(display, DefaultScreen(display)), &winattr);

    status = XParseColor(display, winattr.colormap, color_name, &color);
    if (status == 0) {
	fprintf(stderr, "wmbright:warning: Could not get color \"%s\" for OSD, falling back to default\n", color_name);

	if (color_name != default_osd_color)
		status = XParseColor(display, winattr.colormap, default_osd_color, &color);
	if (status == 0)
		return WhitePixel(display, DefaultScreen(display));
    }

    color.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(display, winattr.colormap, &color);

    return color.pixel;
}

void ui_rrnotify()
{
    new_osd(60);
}
