/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2025 John Tsiombikas <nuclear@mutantstargoat.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef SPNAV_HOTPLUG_H_
#define SPNAV_HOTPLUG_H_

int init_hotplug(void);
void shutdown_hotplug(void);
int get_hotplug_fd(void);
int handle_hotplug(void);

#endif	/* SPNAV_HOTPLUG_H_ */
