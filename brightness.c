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
    bool has_backlight;
    bool backlight_selected;
    Atom backlight_atom;
    uint32_t min;                   /* Min backlight level */
    uint32_t max;                   /* Max backlight level */
    uint32_t level;                 /* Current backlight level */
    float normalised_level;	        /* level, in [0, 1] */
    float actual_level;             /* normalised + global boost */
    int output_number;
    /* unsigned short red[100]; */
    /* unsigned short green[100]; */
    /* unsigned short blue[100]; */
    float gamma_red, gamma_green, gamma_blue;
    int gamma_size;
    XRRCrtcGamma *gamma;
    XRRCrtcGamma **gamma_precalc;
    uint32_t brightness;            /* Wanted xrandr brightness */
    uint32_t last_set_brightness;
    pthread_mutex_t mutex;
    bool thread_active;
};

/* Multiple outputs may share the same controller.
   Retain the unique names but share the rest of the data between clones. */
struct monitor {
    char name[8];                /* Output name */
    bool is_clone;
    struct monitor_data *data;
};

static char *methods[] = { "Backlight", "Gamma" };
static struct monitor *monitors;
static int cur_monitor = 0;
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
/* 			  snd_mixer_elem_t *elem) */
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
            m->min = pi->values[0];
            m->max = pi->values[1];
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
            m->level = *(uint32_t *)prop;
            Xfree(prop);
            if (verbose)
                printf("Output supports backlight, range: (%d, %d), current: %d\n",
                       m->min, m->max, m->level);
            m->has_backlight = true;
            m->backlight_selected = true;
            Xfree(a);
            return true;
        }
    }
    if (propcount > 0) {
        Xfree(a);
    }
    return false;
}

bool get_backlight_level(struct monitor_data *m)
{
    unsigned char *prop;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    XRRGetOutputProperty(display, m->output, m->backlight_atom, 0, 100, False, False, AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
    uint32_t level = *(uint32_t *)prop; 
    Xfree(prop);
    return level;    
}

void set_backlight_level(struct monitor_data *m)
{
    m->actual_level = CLAMP(m->normalised_level + global_offset, 0.0, 1.0);
    m->level = CLAMP(m->min + (m->max - m->min) * m->actual_level, m->min, m->max);

    XRRChangeOutputProperty(display, m->output, m->backlight_atom, XA_INTEGER, 32,
                            PropModeReplace, (unsigned char *)(&m->level), 1);
}

void *do_set_brightness_level(void *data)
{
    struct monitor_data *m = (struct monitor_data *)data;
    do {
        m->actual_level = CLAMP(m->normalised_level + global_offset, 0.0, 1.0);
        m->brightness = CLAMP(100 * m->actual_level, 0, 100);

        pthread_mutex_lock(&m->mutex);
        if (m->last_set_brightness == m->brightness) {
            m->thread_active = false;
            pthread_mutex_unlock(&m->mutex);
            return NULL;
        }
        pthread_mutex_unlock(&m->mutex);
        
        m->last_set_brightness = m->brightness;
        XRRCrtcGamma *source = m->gamma_precalc[m->last_set_brightness]; // +
        /* memcpy(m->gamma->red, source->red, sizeof(unsigned short) * m->gamma_size); */
        /* memcpy(m->gamma->green, source->green, sizeof(unsigned short) * m->gamma_size); */
        /* memcpy(m->gamma->blue, source->blue, sizeof(unsigned short) * m->gamma_size); */
        XRRSetCrtcGamma(display, m->crtc, source); //m->gamma);
        XFlush(display);
        usleep(200000);
    } while (true);
}

void set_brightness_level(struct monitor_data *m)
{
    pthread_t thread;

    pthread_mutex_lock(&m->mutex);
    if (m->thread_active) {
        pthread_mutex_unlock(&m->mutex);
        return;
    }
    m->thread_active = true;
    pthread_mutex_unlock(&m->mutex);
    pthread_create(&thread, NULL, do_set_brightness_level, (void *)m);
}

void brightness_to_gamma(struct monitor_data *m, float gamma, XRRCrtcGamma *target)
{
    if (!m->crtc) {
        return;
    }
    int i, shift;
	float gammaRed;
	float gammaGreen;
	float gammaBlue;
    float brightness = gamma; //brightness_in / 100.0;

    if (!m->gamma) {
        m->gamma_size = XRRGetCrtcGammaSize(display, m->crtc);

        if (!m->gamma_size) {
            fprintf(stderr, "Gamma size is 0.\n");
            return;
        }

        /*
         * The gamma-correction lookup table managed through XRR[GS]etCrtcGamma
         * is 2^n in size, where 'n' is the number of significant bits in
         * the X Color.  Because an X Color is 16 bits, size cannot be larger
         * than 2^16.
         */
        if (m->gamma_size > 65536) {
            fprintf(stderr, "Gamma correction table is impossibly large.\n");
            return;
        }

        m->gamma = XRRAllocGamma(m->gamma_size);
    }
	if (!m->gamma) {
	    fprintf(stderr, "Gamma allocation failed.\n");
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
            target->red[i] = i;
	    else
            target->red[i] = fmin(pow((double)i/(double)(m->gamma_size - 1),
                                     gammaRed) * brightness,
                                 1.0) * (double)(m->gamma_size - 1);
	    target->red[i] <<= shift;
        
	    if (gammaGreen == 1.0 && brightness == 1.0)
            target->green[i] = i;
	    else
            target->green[i] = fmin(pow((double)i/(double)(m->gamma_size - 1),
                                       gammaGreen) * brightness,
                                   1.0) * (double)(m->gamma_size - 1);
	    target->green[i] <<= shift;
        
	    if (gammaBlue == 1.0 && brightness == 1.0)
            target->blue[i] = i;
	    else
            target->blue[i] = fmin(pow((double)i/(double)(m->gamma_size - 1),
                                      gammaBlue) * brightness,
                                  1.0) * (double)(m->gamma_size - 1);
	    target->blue[i] <<= shift;
	}
    memcpy(m->gamma->red, target->red, sizeof(unsigned short) * m->gamma_size);
    memcpy(m->gamma->green, target->green, sizeof(unsigned short) * m->gamma_size);
    memcpy(m->gamma->blue, target->blue, sizeof(unsigned short) * m->gamma_size);
    /* for (int i = 0; i <= m->gamma_size; i++) { */
    /*     m->gamma->red[i] = target->red[i]; */
    /*     m->gamma->green[i] = target->green[i]; */
    /*     m->gamma->blue[i] = target->blue[i]; */
    /* } */
	//XRRSetCrtcGamma(display, m->crtc, m->gamma);

	//free(gamma);
}

/* Returns the index of the last value in an array < 0xffff */
int find_last_non_clamped(CARD16 array[], int size)
{
    int i;
    for (i = size - 1; i > 0; i--) {
        if (array[i] < 0xffff)
            return i;
    }
    return 0;
}

/* At this point we don't just get the current gamma values. We precalculate
   all 100 possible gamma structs. */
 
void get_gamma_values(struct monitor_data *m)
{
    //XRRCrtcGamma *gamma;
    double i1, v1, i2, v2;
    int middle, last_best, last_red, last_green, last_blue;
    CARD16 *best_array;
    float brightness;

    m->gamma_size = XRRGetCrtcGammaSize(display, m->crtc);
    if (verbose)
        printf("Gamma size: %d\n", m->gamma_size);
    if (!m->gamma_size) {
        fprintf(stderr, "Failed to get size of gamma for output %ld\n", m->output);
        return;
    }

    m->gamma = XRRGetCrtcGamma(display, m->crtc);
    if (!m->gamma) {
        fprintf(stderr, "Failed to get gamma for output %ld\n", m->output);
        return;
    }
            
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
    
    m->brightness = (100 * brightness) + 0.5;
    
    m->gamma_precalc = (XRRCrtcGamma **)malloc(sizeof(XRRCrtcGamma *) * 101);
    //unsigned short *data = malloc(sizeof(unsigned short) * m->gamma_size * 3 * 100);
    for (int i = 0; i <= 100; i++) {
        m->gamma_precalc[i] = XRRGetCrtcGamma(display, m->crtc);
        /* m->gamma_precalc[i].red = data + (i * m->gamma_size * 3); */
        /* m->gamma_precalc[i].green = data + (i * m->gamma_size * 3) + m->gamma_size; */
        /* m->gamma_precalc[i].blue = data + (i * m->gamma_size * 3) + m->gamma_size * 2; */
        /* m->gamma_precalc[i].red = (unsigned short *)malloc(sizeof(unsigned short) * m->gamma_size); */
        /* m->gamma_precalc[i].green = (unsigned short *)malloc(sizeof(unsigned short) * m->gamma_size); */
        /* m->gamma_precalc[i].blue = (unsigned short *)malloc(sizeof(unsigned short) * m->gamma_size); */
        brightness_to_gamma(m, i*0.01, m->gamma_precalc[i]); // + i
    }
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
    monitors[0].name[0] = 'A';
    monitors[0].name[1] = 'L';
    monitors[0].name[2] = 'L';
    monitors[0].name[3] = '\0';
    monitors[0].data = (struct monitor_data *)malloc(sizeof(struct monitor_data));
    monitors[0].data->normalised_level = 0.5;
    monitors[0].data->actual_level = 0.5;
    monitors[0].data->level = 0;
    monitors[0].data->min = 0;
    monitors[0].data->max = 100;
    monitors[0].data->has_backlight = false;
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
        strncpy(m->name, oi->name, 8);
        if (strlen(oi->name) >= 8) {
            m->name[7] = '\0';
        }
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
            m->data->has_backlight = false;
            m->data->backlight_selected = false;
            m->data->crtc = 0;
            pthread_mutex_init(&m->data->mutex, NULL);
            m->data->thread_active = false;
        }
        struct monitor_data *d = m->data;
        d->gamma = NULL;
        if (verbose)
            printf("Storing crtc, monitor: %d, crtc: %ld\n", i2, oi->crtc);
        if (oi->crtc != 0) {
            d->crtc = oi->crtc;
            d->output = screen->outputs[i];
            get_backlight_property(d);
        }
                
        XRRFreeOutputInfo(oi);
    }
    
    XRRFreeScreenResources(screen);

    get_brightness_state();
    if (verbose) {
        printf("Current level: %d (%f)\n", monitors[0].data->level, monitors[0].data->normalised_level);
    }
}

void brightness_reinit() {
    // Wait for threads to finish

    // Free everything and start over
    for (int i = 0; i < n_monitors; i++) {
        if (i > 0) {
            XRRFreeGamma(monitors[i].data->gamma);
            for (int j = 0; j <= 100; j++) {
                XRRFreeGamma(monitors[i].data->gamma_precalc[j]);
            }
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
        if (m->has_backlight) {
            get_backlight_level(m);
        }
        get_gamma_values(m);
        if (m->has_backlight && m->backlight_selected) {
            m->normalised_level = ((float)(m->level - m->min)) / (m->max - m->min);
        } else {
            m->normalised_level = (float)m->brightness / 100.0;
        }
        m->actual_level = m->normalised_level;
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
        if (m->has_backlight && m->backlight_selected) {
            set_backlight_level(m);
        } else {
            set_brightness_level(m);
        }
    }
}

bool brightness_is_changed(void)
{
    return get_brightness_state();
}

float get_average_level(void)
{
    float total = 0;
    int count = 0;
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone || monitors[i].data->crtc == 0)
            continue;
        total += CLAMP(monitors[i].data->normalised_level + global_offset, 0.0, 1.0);
        count++;
    }
    return total / count;
}

float brightness_get_level(void)
{
    if (cur_monitor == 0) {
        return get_average_level();
    } else {
        // get_brightness_state();
        return CLAMP(monitors[cur_monitor].data->normalised_level + global_offset, 0.0, 1.0);
    }
}

float get_max_from_max(void)
{
    float max = 0;
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone || monitors[i].data->crtc == 0)
            continue;
        float m = 1.0 - CLAMP(monitors[i].data->normalised_level + global_offset, 0.0, 1.0);
        if (m > max)
            max = m;
    }
    return max;
}

float get_max_from_min(void)
{
    float max = 0.0;
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].is_clone || monitors[i].data->crtc == 0)
            continue;
        float m = CLAMP(monitors[i].data->normalised_level + global_offset, 0.0, 1.0);
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
        return 100 * brightness_get_level();
    }
}

float brightness_get_level_by_crtc(RRCrtc crtc)
{
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].data->crtc == crtc) {
            return CLAMP(monitors[i].data->normalised_level + global_offset, 0.0, 1.0);
        }
    }
    return -10;
}

void brightness_set_level(float level)
{
    struct monitor_data *m = monitors[cur_monitor].data;
    if (cur_monitor > 0) {
        assert((level >= 0.0) && (level <= 1.0));
        m->normalised_level = level;
        if (m->has_backlight && m->backlight_selected) {
            m->level = CLAMP(m->min + (m->max - m->min) * m->normalised_level, m->min, m->max);
        } else {
            m->brightness = CLAMP(100 * level, 0, 100);
        }
    } else {
        m->normalised_level = level;
    }
    set_brightness_state();
}

void brightness_set_level_rel(float delta_level)
{
    struct monitor_data *m = monitors[cur_monitor].data;
    if (cur_monitor > 0) {
        m->normalised_level += delta_level;
        m->normalised_level = CLAMP(m->normalised_level, 0.0, 1.0);
        if (m->has_backlight && m->backlight_selected) {
            m->level = CLAMP(m->min + (m->max - m->min) * m->normalised_level, m->min, m->max);
        } else {
            m->brightness = 100 * m->normalised_level;
        }
    } else {
        if (delta_level > 0) {
            float max = get_max_from_max();
            if (delta_level > max)
                delta_level = max;
        } else if (delta_level < 0) {
            float max = get_max_from_min();
            if (delta_level > max)
                delta_level = max;
        }
        global_offset += delta_level;
        m->normalised_level += delta_level;
        m->actual_level = m->normalised_level;
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
    
    //printf("set_monitor_rel: get_state\n");
    //get_brightness_state();
}

int brightness_get_current_monitor(void)
{
    return cur_monitor;
}

RRCrtc brightness_get_crtc(void)
{
    return monitors[cur_monitor].data->crtc;
}

bool brightness_has_backlight(void)
{
    return monitors[cur_monitor].data->has_backlight;
}

bool brightness_backlight_selected(void)
{
    return monitors[cur_monitor].data->backlight_selected;
}

void brightness_switch_backlight(void)
{
    struct monitor_data *m = monitors[cur_monitor].data;
    if (m->has_backlight) {
        m->backlight_selected = !m->backlight_selected;
        if (m->backlight_selected) {
            m->normalised_level = ((float)(m->level - m->min)) / (m->max - m->min);
        } else {
            m->normalised_level = (float)m->brightness / 100.0;
        }
        m->actual_level = m->normalised_level;
    }
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
            monitors[i].data->normalised_level = CLAMP(monitors[i].data->normalised_level + global_offset, 0.0, 1.0);
        }        
        global_offset = 0;
    }
}

char *brightness_get_method_by_crtc(RRCrtc crtc)
{
    for (int i = 1; i < n_monitors; i++) {
        if (monitors[i].data->crtc == crtc) {
            struct monitor_data *m = monitors[i].data;
            if (m->has_backlight && m->backlight_selected) {
                return methods[0];
            } else {
                return methods[1];
            }
        }
    }
    printf("This is bad!\n");
    return NULL;
}
