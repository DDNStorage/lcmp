/*
 * Copyright (c) 2016, DDN Storage Corporation.
 */
/*
 * Author: Li Xi <lixi@ddn.com>
 */

#ifndef _LCRP_CHANGELOG_H_
#define _LCRP_CHANGELOG_H_

int lcrp_find_or_mkdir(const char *path);
int lcrp_changelog_consume(const char *dir_fid, const char *dir_active,
			   const char *mdt_device, const char *changelog_user,
			   bool *stopping);

#endif /* _LCRP_CHANGELOG_H_ */
