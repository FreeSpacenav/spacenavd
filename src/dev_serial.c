/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2010 John Tsiombikas <nuclear@member.fsf.org>

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

#include "dev_serial.h"
#include "serial/sball.h"

static void *dev;

int open_dev_serial(const char *devfile)
{
	if(!(dev = sball_open(devfile))) {
		return -1;
	}
	return sball_get_fd(dev);
}

void close_dev_serial(void)
{
	sball_close(dev);
}

int read_dev_serial(struct dev_input *inp)
{
	if(!sball_get_input(dev, inp)) {
		return -1;
	}
	return 0;
}
