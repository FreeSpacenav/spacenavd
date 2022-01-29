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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "userpriv.h"

void set_initial_user_privileges(void)
{
	/* get the effective uid and effective gid for the initial user
	 * who started spnavd */
	userids.invoked_uid = geteuid();
	userids.invoked_gid = getegid();

	/* set the "runas" effective uid and gid to an invalid startvalue */
	userids.daemon_uid = -1;
	userids.daemon_gid = -1;

	/* default assumption: we can not change effective uid / gid */
	userids.can_restore_uid = 0;
	userids.can_restore_gid = 0;

	userids.has_cmd_user = 0;
	userids.has_cmd_group = 0;
}

void test_initial_user_privileges(void)
{
	/* default assumption: we can not change effective uid / gid */
	userids.can_restore_uid = 0;
	userids.can_restore_gid = 0;

	/* check the effective uid change */
	if(userids.daemon_uid != -1) {
		if (userids.daemon_uid != userids.invoked_uid) {
			/* only run, if daemon uid differ from invoked uid */
			if(seteuid(userids.daemon_uid) == 0)
			{
				/* succeded to get lower privileges
				 * -> restore uid */
				if(seteuid(userids.invoked_uid) == 0)
				{
					userids.can_restore_uid = 1;
				}
			}
		}
	}

	/* check the effective gid change */
	if(userids.daemon_gid != -1)
	{
		if (userids.daemon_gid != userids.invoked_gid) {
			/* only run, if daemon gid differ from invoked gid */
			if(seteuid(userids.daemon_gid) == 0)
			{
				/* succeded to get lower privileges
				 * -> restore uid */
				if(seteuid(userids.invoked_gid) == 0)
				{
					userids.can_restore_gid = 1;
				}
			}
		}
	}
}

int set_runas_uid(char *runas_lname)
{
	struct passwd *userinfo;

	if(!(userinfo = getpwnam(runas_lname))) {
		/* error - but no distinction */
		return 0;
	}
	/* set the uid */
	userids.daemon_uid = userinfo->pw_uid;

	return 1;
}

int set_runas_gid(char *runas_gname)
{
	struct group *groupinfo;

	if(!(groupinfo = getgrnam(runas_gname))) {
		/* error - but no distinction */
		return 0;
	}
	/* set the gid */
	userids.daemon_gid = groupinfo->gr_gid;

	return 1;
}

void start_daemon_privileges(void)
{
	if(userids.runas_daemon == 1) {
		if(userids.can_restore_uid) {
			seteuid(userids.daemon_uid);
		}
		if(userids.can_restore_gid) {
			setegid(userids.daemon_gid);
		}
	}
}

void stop_daemon_privileges(void)
{
	if(userids.runas_daemon == 1) {
		if(userids.can_restore_uid) {
			seteuid(userids.invoked_uid);
		}
		if(userids.can_restore_gid) {
			setegid(userids.invoked_gid);
		}
	}
}

int user_set_by_cmdline(void)
{
	return userids.has_cmd_user;
}

int group_set_by_cmdline(void)
{
	return userids.has_cmd_group;
}
