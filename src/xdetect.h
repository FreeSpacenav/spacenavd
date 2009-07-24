/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2009 John Tsiombikas <nuclear@member.fsf.org>

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

#ifndef XDETECT_H_
#define XDETECT_H_

#include "config.h"

int xdet_start(void);
void xdet_stop(void);

int xdet_get_fd(void);

int handle_xdet_events(fd_set *rset);

#endif	/* XDETECT_H_ */
