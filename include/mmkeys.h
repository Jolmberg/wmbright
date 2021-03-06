/* wmbright -- a brightness control using randr.
 * Copyright (C) 2014 Christophe CURIS for the WindowMaker Team
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
/* include/mmkeys.h: functions related to handling Multimedia keys */

#ifndef WMBRIGHT_MMKEYS_H
#define WMBRIGHT_MMKEYS_H


/* Global Configuration */
extern struct multimedia_keys {
    KeyCode brightness_up;
    KeyCode brightness_down;
} mmkeys;

/* Grab the multimedia keys */
void mmkey_install(Display *display);

#endif /* WMBRIGHT_MMKEYS_H */
