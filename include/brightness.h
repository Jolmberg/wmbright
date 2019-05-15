/* WMix 3.0 -- a mixer using the OSS mixer API.
 * Copyright (C) 2000, 2001
 *	Daniel Richard G. <skunk@mit.edu>,
 *	timecop <timecop@japan.co.jp>
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

void brightness_init(Display *display, bool set_verbose);
void brightness_reinit(void);
bool brightness_is_changed(void);
float brightness_get_level(void);
void brightness_set_level(float level);
void brightness_set_level_rel(float delta_level);
void brightness_tick(void);
const char *brightness_get_monitor_name(void);
void brightness_set_monitor_rel(int delta_monitor);
float brightness_get_level_by_crtc(RRCrtc crtc);
int brightness_get_current_monitor(void);
RRCrtc brightness_get_crtc(void);
bool brightness_has_backlight(void);
bool brightness_backlight_selected(void);
void brightness_switch_backlight(void);
void brightness_ready(void);
void brightness_unready(void);
int brightness_get_percent(void);
char *brightness_get_method_by_crtc(RRCrtc crtc);
