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

struct dimensions {
    int x, y, width, height;
};

enum method {
    NONE = 0,
    BACKLIGHT = 1,
    GAMMA = 2
};

void brightness_init(Display *display, bool set_verbose);
void brightness_reinit(void);
bool brightness_is_changed(void);
float brightness_get_level(int monitor);
void brightness_set_level(float level);
void brightness_set_level_rel(float delta_level);
void brightness_tick(void);
const char *brightness_get_monitor_name(void);
void brightness_set_monitor_rel(int delta_monitor);
int brightness_get_current_monitor(void);
RRCrtc brightness_get_crtc(void);
void brightness_ready(void);
void brightness_unready(void);
int brightness_get_percent(void);
char *brightness_get_method_name(int monitor);
int brightness_get_monitor_count(void);
struct dimensions brightness_get_dimensions(int monitor);
bool brightness_set_method(enum method method);
enum method brightness_get_method(void);
bool brightness_has_method(enum method method);
