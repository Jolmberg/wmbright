/* wmbright -- a brightness control using randr.
 * Copyright (C) 2000, 2001
 *     Daniel Richard G. <skunk@mit.edu>,
 *     timecop <timecop@japan.co.jp>
 * Copyright (C) 2019
 *     Johannes Holmberg <johannes@update.uu.se>
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
#include <assert.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#include "include/common.h"
#include "include/misc.h"
#include "include/brightness.h"


static bool get_brightness_state(void);

struct monitor_data {
    RROutput output;
    RRCrtc crtc;
    bool supported_methods[3];
    enum method current_method;
    Atom backlight_atom;
    uint32_t min[3];                /* Min backlight level */
    uint32_t max[3];                /* Max backlight level */
    uint32_t level[3];              /* Current backlight level */
    float normalised_level[3];      /* level, in [0, 1] */
    float actual_level;             /* normalised + global boost */
    float gamma_red, gamma_green, gamma_blue;
    int gamma_size;
    XRRCrtcGamma *gamma;
    XRRCrtcGamma **gamma_precalc;
    uint32_t last_set_brightness;
    struct dimensions dim;          /* Monitor position and size */
    pthread_mutex_t mutex;
    bool thread_active;
    bool thread_kill;
};

/* Multiple outputs may share the same controller.
   Retain the unique names but share the rest of the data between clones. */
struct monitor {
    char name[17];                  /* Output name */
    bool is_clone;
    struct monitor_data *data;
};

static char *methods[] = { "None", "Backlight", "Gamma" };
static struct monitor *monitors;
static int cur_monitor;
static int n_monitors;
static bool needs_update;
static Display *display;
static float global_offset;
static bool verbose;


/* static int elem_callback(__attribute__((unused)) snd_mixer_elem_t *elem, */
/*                          __attribute__((unused)) unsigned int mask) */
/* { */
/*     needs_update = true; */
/*     return 0; */
/* } */

/* static int mixer_callback(__attribute__((unused)) snd_mixer_t *ctl, */
/*                           unsigned int mask, */
/*            snd_mixer_elem_t *elem) */
/* { */
/*     if (mask & SND_CTL_EVENT_MASK_ADD) { */
/*         snd_mixer_elem_set_callback(elem, elem_callback); */
/*         needs_update = true; */
/*     } */
/*     return 0; */
/* } */

static bool get_backlight_property(struct monitor_data *m)
{
    int propcount;
    Atom *a = XRRListOutputProperties(display, m->output, &propcount);
    for (int j = 0; j < propcount; j++) {
        char *name = XGetAtomName(display, a[j]);
        bool found_backlight = !strcmp(name, "Backlight");
        Xfree(name);
        if (found_backlight) {
            XRRPropertyInfo *pi = XRRQueryOutputProperty(display, m->output, a[j]);
            if (pi->range != 1 || pi->num_values != 2) {
                printf("Output has backlight support but its settings were not understood.");
                Xfree(pi);
                Xfree(a);
                return false;
            }
            m->min[BACKLIGHT] = pi->values[0];
            m->max[BACKLIGHT] = pi->values[1];
            Xfree(pi);
            unsigned char *prop;
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            XRRGetOutputProperty(display, m->output, a[j], 0, 100, False, False,
                                 AnyPropertyType, &actual_type, &actual_format,
                                 &nitems, &bytes_after, &prop);
            if (verbose)
                printf("Items: %d, value_type: %d, value_format: %d\n", (int)nitems, (int)actual_type, actual_format);
            if (actual_type != XA_INTEGER) {
                printf("Output has backlight support but it's type is strange: %d\n",
                       (int)actual_type);
            }
            m->backlight_atom = a[j];
            m->level[BACKLIGHT] = *(uint32_t *)prop;
            Xfree(prop);
            if (verbose)
                printf("Output supports backlight, range: (%d, %d), current: %d\n",
                       m->min[BACKLIGHT], m->max[BACKLIGHT], m->level[BACKLIGHT]);
            m->supported_methods[BACKLIGHT] = true;
            Xfree(a);
            return true;
        }
    }
    if (propcount > 0) {
        Xfree(a);
    }
    return false;
}

static void get_backlight_level(struct monitor_data *m)
{
    unsigned char *prop;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    XRRGetOutputProperty(display, m->output, m->backlight_atom, 0, 100, False, False, AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
    m->level[BACKLIGHT] = *(uint32_t *)prop;
    Xfree(prop);
}

static void set_backlight_level(struct monitor_data *m)
{
    uint32_t min = m->min[BACKLIGHT], max = m->max[BACKLIGHT];
    m->actual_level = CLAMP(m->normalised_level[BACKLIGHT] + global_offset, 0.0, 1.0);
    m->level[BACKLIGHT] = CLAMP(min + (max - min) * m->actual_level, min, max);

    XRRChangeOutputProperty(display, m->output, m->backlight_atom, XA_INTEGER, 32,
                            PropModeReplace, (unsigned char *)(&m->level[BACKLIGHT]), 1);
}

static void brightness_to_gamma(struct monitor_data *m)
{
    int i, shift;
    float gammaRed;
    float gammaGreen;
    float gammaBlue;
    float brightness = m->last_set_brightness / 100.0;

    if (!m->gamma) {
        fprintf(stderr, "wmbright:error: Gamma struct was not allocated!\n");
        return;
    }

    /*
     * The hardware color lookup table has a number of significant
     * bits equal to ffs(size) - 1; compute all values so that
     * they are in the range [0,size) then shift the values so
     * that they occupy the MSBs of the 16-bit X Color.
     */
    shift = 16 - (ffs(m->gamma_size) - 1);

    if (m->gamma_red == 0.0)
        m->gamma_red = 1.0;
    if (m->gamma_green == 0.0)
        m->gamma_green = 1.0;
    if (m->gamma_blue == 0.0)
        m->gamma_blue = 1.0;

    gammaRed = 1.0 / m->gamma_red;
    gammaGreen = 1.0 / m->gamma_green;
    gammaBlue = 1.0 / m->gamma_blue;
    
    for (i = 0; i < m->gamma_size; i++) {
        if (gammaRed == 1.0 && brightness == 1.0)
            m->gamma->red[i] = i;
        else
            m->gamma->red[i] = fmin(pow((double)i/(double)(m->gamma_size - 1),
                                     gammaRed) * brightness,
                                 1.0) * (double)(m->gamma_size - 1);
        m->gamma->red[i] <<= shift;
        
        if (gammaGreen == 1.0 && brightness == 1.0)
            m->gamma->green[i] = i;
        else
            m->gamma->green[i] = fmin(pow((double)i/(double)(m->gamma_size - 1),
                                       gammaGreen) * brightness,
                                   1.0) * (double)(m->gamma_size - 1);
        m->gamma->green[i] <<= shift;
        
        if (gammaBlue == 1.0 && brightness == 1.0)
            m->gamma->blue[i] = i;
        else
            m->gamma->blue[i] = fmin(pow((double)i/(double)(m->gamma_size - 1),
                                      gammaBlue) * brightness,
                                  1.0) * (double)(m->gamma_size - 1);
        m->gamma->blue[i] <<= shift;
    }
}

static void *do_set_brightness_level(void *data)
{
    struct monitor_data *m = (struct monitor_data *)data;
    do {
        uint32_t min = m->min[GAMMA], max = m->max[GAMMA];
        m->actual_level = CLAMP(m->normalised_level[GAMMA] + global_offset, 0.0, 1.0);
        m->level[GAMMA] = CLAMP((max-min) * m->actual_level, min, max);

        pthread_mutex_lock(&m->mutex);
        if (m->thread_kill || m->last_set_brightness == m->level[GAMMA]) {
            m->thread_active = false;
            pthread_mutex_unlock(&m->mutex);
            return NULL;
        }
        pthread_mutex_unlock(&m->mutex);

        m->last_set_brightness = m->level[GAMMA];
        brightness_to_gamma(m);
        XRRSetCrtcGamma(display, m->crtc, m->gamma);
        XFlush(display);
        usleep(100000);
    } while (true);
}

static void set_brightness_level(struct monitor_data *m)
{
    pthread_t thread;
    uint32_t min = m->min[GAMMA], max = m->max[GAMMA];

    pthread_mutex_lock(&m->mutex);
    m->actual_level = CLAMP(m->normalised_level[GAMMA] + global_offset, 0.0, 1.0);

    m->level[GAMMA] = CLAMP((max - min) * m->actual_level, min, max);
    if (m->thread_active || (m->last_set_brightness == m->level[GAMMA])) {
        pthread_mutex_unlock(&m->mutex);
        return;
    }
    m->thread_active = true;
    pthread_mutex_unlock(&m->mutex);
    pthread_create(&thread, NULL, do_set_brightness_level, (void *)m);
}

/* Returns the index of the last value in an array < 0xffff */
static int find_last_non_clamped(CARD16 array[], int size)
{
    int i;
    for (i = size - 1; i > 0; i--) {
        if (array[i] < 0xffff)
            return i;
    }
    return 0;
}

/* Allocate the gamma struct and leave it for later */
static bool get_gamma_property(struct monitor_data *m)
{
    m->gamma_size = XRRGetCrtcGammaSize(display, m->crtc);
    if (verbose)
        printf("Gamma size: %d\n", m->gamma_size);
    if (!m->gamma_size) {
        fprintf(stderr, "wmbright:warning: Failed to get size of gamma for output %ld\n", m->output);
        return false;
    }

    m->gamma = XRRGetCrtcGamma(display, m->crtc);
    if (!m->gamma) {
        fprintf(stderr, "wmbright:warning: Failed to get gamma for output %ld\n", m->output);
        return false;
    }
    m->min[GAMMA] = 0;
    m->max[GAMMA] = 100;
    m->supported_methods[GAMMA] = true;
    return true;
}

/* Reallocate the gamma struct and calculate brightness from it */
/* Assumes that gamma_size has not changed, which would be really weird */
static void get_gamma_values(struct monitor_data *m)
{
    XRRFreeGamma(m->gamma);
    m->gamma = XRRGetCrtcGamma(display, m->crtc);
    if (!m->gamma) {
        fprintf(stderr, "wmbright:warning: Failed to get gamma for output %ld\n", m->output);
        return;
    }

    double i1, v1, i2, v2;
    int middle, last_best, last_red, last_green, last_blue;
    CARD16 *best_array;
    float brightness;

    /*
     * Here is a bit tricky because gamma is a whole curve for each
     * color.  So, typically, we need to represent 3 * 256 values as 3 + 1
     * values.  Therefore, we approximate the gamma curve (v) by supposing
     * it always follows the way we set it: a power function (i^g)
     * multiplied by a brightness (b).
     * v = i^g * b
     * so g = (ln(v) - ln(b))/ln(i)
     * and b can be found using two points (v1,i1) and (v2, i2):
     * b = e^((ln(v2)*ln(i1) - ln(v1)*ln(i2))/ln(i1/i2))
     * For the best resolution, we select i2 at the highest place not
     * clamped and i1 at i2/2. Note that if i2 = 1 (as in most normal
     * cases), then b = v2.
     */
    last_red = find_last_non_clamped(m->gamma->red, m->gamma_size);
    last_green = find_last_non_clamped(m->gamma->green, m->gamma_size);
    last_blue = find_last_non_clamped(m->gamma->blue, m->gamma_size);
    best_array = m->gamma->red;
    last_best = last_red;
    if (last_green > last_best) {
        last_best = last_green;
        best_array = m->gamma->green;
    }
    if (last_blue > last_best) {
        last_best = last_blue;
        best_array = m->gamma->blue;
    }
    if (last_best == 0)
        last_best = 1;

    middle = last_best / 2;
    i1 = (double)(middle + 1) / m->gamma_size;
    v1 = (double)(best_array[middle]) / 65535;
    i2 = (double)(last_best + 1) / m->gamma_size;
    v2 = (double)(best_array[last_best]) / 65535;
    if (v2 < 0.0001) { /* The screen is black */
        brightness = 0;
        m->gamma_red = 1;
        m->gamma_green = 1;
        m->gamma_blue = 1;
    } else {
        if ((last_best + 1) == m->gamma_size)
            brightness = v2;
        else
            brightness = exp((log(v2)*log(i1) - log(v1)*log(i2))/log(i1/i2));
        m->gamma_red = log((double)(m->gamma->red[last_red / 2]) / brightness
                  / 65535) / log((double)((last_red / 2) + 1) / m->gamma_size);
        m->gamma_green = log((double)(m->gamma->green[last_green / 2]) / brightness
                    / 65535) / log((double)((last_green / 2) + 1) / m->gamma_size);
        m->gamma_blue = log((double)(m->gamma->blue[last_blue / 2]) / brightness
                   / 65535) / log((double)((last_blue / 2) + 1) / m->gamma_size);
        if (verbose)
            printf("red: %f, green: %f, blue: %f, brightness: %f\n", m->gamma_red, m->gamma_green, m->gamma_blue, brightness);
    }
    
    m->level[GAMMA] = (100 * brightness) + 0.5;
}

void brightness_init(Display *x_display, bool set_verbose)
{
    needs_update = true;
    display = x_display;
    verbose = set_verbose;
    XRRScreenResources *screen = XRRGetScreenResources(display, DefaultRootWindow(display));

    /* Count the number of monitors that are actually in use. */
    n_monitors = 1;
    for (int i = 0; i < screen->noutput; i++) {
        XRROutputInfo *oi = XRRGetOutputInfo(display, screen, screen->outputs[i]);
        if (oi->crtc != 0)
            n_monitors++;
        XRRFreeOutputInfo(oi);
    }

    if (verbose)
        printf("Found %d active output(s)\n", n_monitors);
    monitors = (struct monitor *)malloc((n_monitors + 1) * sizeof(struct monitor));

    /* Use the first entry for the global controller */
    cur_monitor = 0;
    monitors[0].name[0] = 'A';
    monitors[0].name[1] = 'L';
    monitors[0].name[2] = 'L';
    monitors[0].name[3] = '\0';
    monitors[0].data = (struct monitor_data *)malloc(sizeof(struct monitor_data));
    monitors[0].data->normalised_level[NONE] = 0.5;
    monitors[0].data->actual_level = 0.5;
    monitors[0].data->supported_methods[0] = true;
    monitors[0].data->supported_methods[1] = false;
    monitors[0].data->supported_methods[2] = false;
    global_offset = 0.0;

    int i2 = 0;
    for (int i = 0; i < screen->noutput; i++) {
        XRROutputInfo *oi = XRRGetOutputInfo(display, screen, screen->outputs[i]);
        if (verbose)
            printf("Found monitor: %s, connection: %d, output: %d crtc: %d\n", oi->name, oi->connection, (int)screen->outputs[i], (int)oi->crtc);
        if (oi->crtc == 0) {
            XRRFreeOutputInfo(oi);
            continue;
        }
        i2++;
        struct monitor *m = monitors + i2;
        strncpy(m->name, oi->name, 16);
        m->name[16] = '\0';
        m->is_clone = false;
        for (int j = 0; j < oi->nclone; j++) {
            for (int k = 0; k < i2; k++) {
                if (oi->clones[j] == monitors[k].data->output) {
                    if (verbose)
                        printf("This is a clone of %s\n", monitors[k].name);
                    m->data = monitors[k].data;
                    m->is_clone = true;
                    break;
                }
            }
            if (m->is_clone)
                break;
        }
        if (!m->is_clone) {
            m->data = (struct monitor_data *)malloc(sizeof(struct monitor_data));
            struct monitor_data *d = m->data;
            d->supported_methods[0] = true;
            d->supported_methods[1] = false;
            d->supported_methods[2] = false;
            d->current_method = NONE;
            d->crtc = oi->crtc;
            d->output = screen->outputs[i];
            pthread_mutex_init(&d->mutex, NULL);
            d->thread_active = false;
            d->thread_kill = false;
            if (get_backlight_property(d))
                d->current_method = BACKLIGHT;
            if (get_gamma_property(d) && (d->current_method == NONE))
                d->current_method = GAMMA;

            XRRCrtcInfo *ci = XRRGetCrtcInfo(display, screen, d->crtc);
            d->dim = (struct dimensions){ ci->x, ci->y, ci->width, ci->height };
            XRRFreeCrtcInfo(ci);
        }
        XRRFreeOutputInfo(oi);
        if (verbose)
            printf("Stored monitor: %d, crtc: %ld\n", i2, m->data->crtc);
    }
    
    XRRFreeScreenResources(screen);

    get_brightness_state();
}

void brightness_reinit() {
    // Wait for threads to finish, free everything and start over
    for (int i = 0; i < n_monitors; i++) {
        if (i > 0) {
            struct monitor_data *m = monitors[i].data;
            m->thread_kill = true;
            while (m->thread_active) {
                usleep(10000);
            }
            pthread_mutex_lock(&m->mutex);
            pthread_mutex_unlock(&m->mutex);
            pthread_mutex_destroy(&m->mutex);
            XRRFreeGamma(monitors[i].data->gamma);
        }
        free(monitors[i].data);
    }
    free(monitors);

    brightness_init(display, verbose);
}

static bool get_brightness_state(void)
{
    if (!needs_update)
        return false;
    needs_update = false;
    XRRScreenResources *screen = XRRGetScreenResources(display, DefaultRootWindow(display));

    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone)
            continue;
        struct monitor_data *m = monitors[i].data;
        if (m->crtc == 0)
            continue;
        if (m->supported_methods[BACKLIGHT])
            get_backlight_level(m);
        if (m->supported_methods[GAMMA])
            get_gamma_values(m);
            
        for (int method = BACKLIGHT; method <= GAMMA; method++) {
            if (m->supported_methods[method]) {
                uint32_t min = m->min[method], max = m->max[method];
                m->normalised_level[method] = (float)(m->level[method] - min) / (max - min);
            }
        }
        m->actual_level = m->normalised_level[m->current_method];
    }
    XRRFreeScreenResources(screen);
    return true;
}

static void set_brightness_state(void)
{
    int start, stop;
    if (cur_monitor == 0) {
        start = 1;
        stop = n_monitors;
    } else {
        start = cur_monitor;
        stop = cur_monitor + 1;
    }
    for (int i = start; i < stop; i++) {
        if (monitors[i].is_clone || (monitors[i].data->crtc == 0))
            continue;
        struct monitor_data *m = monitors[i].data;
        if (m->current_method == BACKLIGHT) {
            set_backlight_level(m);
        } else if (m->current_method == GAMMA) {
            set_brightness_level(m);
        }
    }
}

bool brightness_is_changed(void)
{
    return get_brightness_state();
}

static float get_average_level(void)
{
    float total = 0;
    int count = 0;
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone || monitors[i].data->crtc == 0)
            continue;
        enum method method = monitors[i].data->current_method;
        total += CLAMP(monitors[i].data->normalised_level[method] + global_offset, 0.0, 1.0);
        count++;
    }
    return total / count;
}

float brightness_get_level(int monitor)
{
    if (monitor < 0) {
        monitor = cur_monitor;
    }
    if (monitor == 0) {
        return get_average_level();
    } else {
        // get_brightness_state();
        enum method method = monitors[monitor].data->current_method;
        return CLAMP(monitors[monitor].data->normalised_level[method] + global_offset, 0.0, 1.0);
    }
}

static float get_max_from_max(void)
{
    float max = 0;
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone || monitors[i].data->crtc == 0)
            continue;
        enum method method = monitors[i].data->current_method;
        float m = 1.0 - CLAMP(monitors[i].data->normalised_level[method] + global_offset, 0.0, 1.0);
        if (m > max)
            max = m;
    }
    return max;
}

static float get_max_from_min(void)
{
    float max = 0.0;
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone || monitors[i].data->crtc == 0)
            continue;
        enum method method = monitors[i].data->current_method;
        float m = CLAMP(monitors[i].data->normalised_level[method] + global_offset, 0.0, 1.0);
        if (m > max)
            max = m;
    }
    return max;
}

int brightness_get_percent(void)
{
    if (cur_monitor == 0) {
        return 100 * get_average_level();
    } else {
        return 100 * brightness_get_level(-1);
    }
}

void brightness_set_level(float level)
{
    struct monitor_data *m = monitors[cur_monitor].data;
    if (cur_monitor > 0) {
        assert((level >= 0.0) && (level <= 1.0));
        m->normalised_level[m->current_method] = level;
        set_brightness_state();
    }
}

void brightness_set_level_rel(float delta_level)
{
    struct monitor_data *m = monitors[cur_monitor].data;
    if (cur_monitor > 0) {
        m->normalised_level[m->current_method] = CLAMP(m->normalised_level[m->current_method] + delta_level, 0.0, 1.0);
    } else {
        if (delta_level > 0) {
            float max = get_max_from_max();
            if (delta_level > max)
                delta_level = max;
        } else if (delta_level < 0) {
            float max = -get_max_from_min();
            if (delta_level < max)
                delta_level = max;
        }
        global_offset += delta_level;
        m->normalised_level[NONE] += delta_level;
        m->actual_level = m->normalised_level[NONE];
    }
    set_brightness_state();
}

void brightness_tick(void)
{
    /* brightness_handle_events(brightness); */
}

const char *brightness_get_monitor_name(void)
{
    return monitors[cur_monitor].name;
}

void brightness_set_monitor_rel(int delta_monitor)
{
    cur_monitor = (cur_monitor + delta_monitor) % n_monitors;
    if (cur_monitor < 0)
        cur_monitor += n_monitors;
    
    get_brightness_state();
}

int brightness_get_current_monitor(void)
{
    return cur_monitor;
}

RRCrtc brightness_get_crtc(void)
{
    return monitors[cur_monitor].data->crtc;
}

bool brightness_has_method(enum method method)
{
    if (cur_monitor == 0) {
        for (int i = 1; i < n_monitors; i++) {
            if (monitors[i].data->supported_methods[method])
                return true;
        }
        return false;
    } else {
        return monitors[cur_monitor].data->supported_methods[method];
    }
}

enum method brightness_get_method(void)
{
    if (cur_monitor == 0) {
        if (n_monitors == 1) {
            return NONE;
        } else if (n_monitors == 2) {
            return monitors[1].data->current_method;
        } else {
            enum method method = monitors[1].data->current_method;
            for (int i = 2; i < n_monitors; i++) {
                if (monitors[i].data->current_method != method) {
                    return NONE;
                }
            }
            return method;
        }
    }
    return monitors[cur_monitor].data->current_method;
}

bool brightness_set_method(enum method method)
{
    if (cur_monitor == 0) {
        bool success = false;
        for (int i = 1; i < n_monitors; i++) {
            if (monitors[i].data->supported_methods[method]) {
                monitors[i].data->current_method = method;
                success = true;
            }
        }
        return success;
    }
    struct monitor_data *m = monitors[cur_monitor].data;
    if (m->supported_methods[method]) {
        m->current_method = method;
        return true;
    }
    return false;
}

void brightness_ready(void)
{
    if (cur_monitor == 0) {
        global_offset = 0;
    }
}

void brightness_unready(void)
{
    if (cur_monitor == 0) {
        for (int i = 1; i < n_monitors; i++) {
            if (monitors[i].is_clone || monitors[i].data->crtc == 0)
                continue;
            enum method method = monitors[i].data->current_method;
            monitors[i].data->normalised_level[method] = CLAMP(monitors[i].data->normalised_level[method] + global_offset, 0.0, 1.0);
        }        
        global_offset = 0;
    }
}

char *brightness_get_method_name(int monitor)
{
    if (monitor < 0)
        monitor = cur_monitor;
    struct monitor_data *m = monitors[monitor].data;
    return methods[m->current_method];
}

int brightness_get_monitor_count(void)
{
    return n_monitors - 1;
}

struct dimensions brightness_get_dimensions(int monitor)
{
    if (monitor < 0)
        monitor = cur_monitor;
    return monitors[monitor].data->dim;
}
