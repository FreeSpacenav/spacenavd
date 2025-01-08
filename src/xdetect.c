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

/* this must be the inverse of all the other xdetect_*.c ifdefs */
#if !defined(__linux__) && !defined(__FreeBSD__) && !defined(__APPLE__)
#include <sys/select.h>
#include "xdetect.h"

int xdet_start(void)
{
	return -1;
}

void xdet_stop(void)
{
}

int xdet_get_fd(void)
{
	return -1;
}

int handle_xdet_events(fd_set *rset)
{
	return -1;
}
#else
int spacenav_xdetect_none_shut_up_empty_source_warning;
#endif
