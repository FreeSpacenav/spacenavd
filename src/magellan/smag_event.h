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
#ifndef SMAG_EVENT_H_
#define SMAG_EVENT_H_

#include "event.h"

struct smag_event {
	struct dev_input data;
	struct smag_event *next;
};

struct smag_event *alloc_event(void);
void free_event(struct smag_event *ev);

#endif	/* SMAG_EVENT_H_ */
