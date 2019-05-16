/* WMBright -- a brightness control using randr.
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

void dockapp_init(Display *x_display);

void new_window(char *name, int width, int height);

void new_osd(int height);
void update_osd(bool up);
void map_osd(void);
void unmap_osd(void);
bool osd_mapped(void);

void ui_update(void);
void redraw_window(void);

int blit_string(const char *text);
void scroll_text(int x, int y, int width, bool reset);
void set_cursor(int type);
void knob_turn(float delta);
//void slider_move(float delta);

unsigned long get_color(Display *display, char *color_name);

void ui_rrnotify(void);
