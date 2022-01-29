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

#ifndef USERPRIV_H_
#define USERPRIV_H_

#include <sys/types.h>

/* struct for user id's */
struct userpriv {
	uid_t daemon_uid;         /* the uid for the daemon */
	gid_t daemon_gid;         /* the gid for the daemon */
	uid_t invoked_uid;        /* spnavd was started with this uid */
	gid_t invoked_gid;        /* spnavd was started with this gid */
	int can_restore_uid;    /* spnavd can restore the invoked uid */
	int can_restore_gid;    /* spnavd can restore the invoked gid */
	int runas_daemon;       /* flag for running in daemonmode */
	int has_cmd_user;       /* spnavd started with -u */
	int has_cmd_group;      /* spnavd started with -g */
};

struct userpriv userids;

void set_initial_user_privileges(void);
void test_initial_user_privileges(void);
int set_runas_uid(char *runas_lname);
int set_runas_gid(char *runas_gname);
void start_daemon_privileges(void);
void stop_daemon_privileges(void);
int user_set_by_cmdline(void);
int group_set_by_cmdline(void);

#endif	/* USERPRIV_H_ */
