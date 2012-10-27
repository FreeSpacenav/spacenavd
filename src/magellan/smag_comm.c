/*
serial magellan device support for spacenavd

Copyright (C) 2012 John Tsiombikas <nuclear@member.fsf.org>
Copyright (C) 2010 Thomas Anderson <ta@nextgenengineering.com>

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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "magellan/smag_comm.h"


int smag_open_device(const char *fname)
{
	return open(fname, O_RDWR | O_NOCTTY | O_NONBLOCK | O_NDELAY);
}

int smag_set_port_spaceball(int fd)
{
	int status;
	struct termios term;

	if(tcgetattr(fd, &term) == -1) {
		perror("error tcgetattr");
		return -1;
	}

	term.c_cflag = CREAD | CS8 | CLOCAL | HUPCL;
	term.c_iflag |= IGNBRK | IGNPAR;
	term.c_oflag = 0;
	term.c_lflag = 0;
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;

	cfsetispeed(&term, 9600);
	cfsetospeed(&term, 9600);
	if(tcsetattr(fd, TCSANOW, &term) == -1) {
		perror("error tcsetattr");
		return -1;
	}

	if(ioctl(fd, TIOCMGET, &status) == -1) {
		perror("error TIOMCGET");
		return -1;
	}
	status |= TIOCM_DTR;
	status |= TIOCM_RTS;
	if(ioctl(fd, TIOCMSET, &status) == -1) {
		perror("error TIOCMSET");
		return -1;
	}
	return 0;
}

int smag_set_port_magellan(int fd)
{
	int status;
	struct termios term;

	if(tcgetattr(fd, &term) == -1) {
		perror("error tcgetattr");
		return -1;
	}

	term.c_cflag = CS8 | CSTOPB | CRTSCTS | CREAD | HUPCL | CLOCAL;
	term.c_iflag |= IGNBRK | IGNPAR;
	term.c_oflag = 0;
	term.c_lflag = 0;
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;

	cfsetispeed(&term, 9600);
	cfsetospeed(&term, 9600);
	if(tcsetattr(fd, TCSANOW, &term) == -1) {
		perror("error tcsetattr");
		return -1;
	}

	if(ioctl(fd, TIOCMGET, &status) == -1) {
		perror("error TIOCMGET");
		return -1;
	}
	status |= TIOCM_DTR;
	status |= TIOCM_RTS;
	if(ioctl(fd, TIOCMSET, &status) == -1) {
		perror("error TIOCMSET");
		return -1;
	}
	return 0;
}

#define LONG_DELAY	150000

void smag_write(int fd, const char *buf, int sz)
{
	int i;

	for(i=0; i<sz; i++) {
		write(fd, buf + i, 1);
		usleep(SMAG_DELAY_USEC);
	}
	write(fd, "\r", 1);
	usleep(LONG_DELAY);
}

int smag_read(int fd, char *buf, int sz)
{
	int bytesrd = read(fd, buf, sz - 1);
	if(bytesrd < 1) {
		return 0;
	}
	buf[bytesrd] = 0;
	return bytesrd;
}

int smag_wait_read(int fd, char *buf, int sz, int wait_sec)
{
	int res;
	fd_set set;
	struct timeval tv;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	tv.tv_sec = wait_sec;
	tv.tv_usec = 0;

	do {
		res = select(fd + 1, &set, 0, 0, &tv);
	} while(res == -1 && errno == EINTR);

	return res == -1 ? -1 : smag_read(fd, buf, sz);
}
