/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2012 John Tsiombikas <nuclear@member.fsf.org>

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

#include "config.h"
#include "dev_serial.h"
#include "dev.h"
#include "event.h"
#include "serial/sball.h"

static void close_dev_serial(struct device *dev);
static int read_dev_serial(struct device *dev, struct dev_input *inp);

int open_dev_serial(struct device *dev)
{
	if(!(dev->data = sball_open(dev->path))) {
		return -1;
	}
	dev->fd = sball_get_fd(dev->data);

	dev->close = close_dev_serial;
	dev->read = read_dev_serial;
	return 0;
}

static void close_dev_serial(struct device *dev)
{
	if(dev->data) {
		sball_close(dev->data);
	}
	dev->data = 0;
}

static int read_dev_serial(struct device *dev, struct dev_input *inp)
{
	if(!dev->data || !sball_get_input(dev->data, inp)) {
		return -1;
	}
	return 0;
}
