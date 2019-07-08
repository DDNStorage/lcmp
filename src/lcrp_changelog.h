/*
 * Copyright (c) 2016, DDN Storage Corporation.
 */
/*
 * Author: Li Xi <lixi@ddn.com>
 */

#ifndef _LCRP_CHANGELOG_H_
#define _LCRP_CHANGELOG_H_

#include <pthread.h>

struct lcrp_epoch {
	/* Lock to protect when epoch changes */
	pthread_mutex_t	le_mutex;
	/* Epoch interval in seconds */
	int le_seconds;
	/* Start epoch time, protected by ls_mutex */
	int le_start;
	/* End epoch time, protected by ls_mutex */
	int le_end;
	/* Directory of this epoch under active, protected by ls_mutex */
	char le_dir_active[PATH_MAX + 1];
};

int lcrp_find_or_mkdir(const char *path);
int lcrp_changelog_consume(const char *dir_fid, struct lcrp_epoch *epoch,
			   const char *mdt_device, const char *changelog_user,
			   bool *stopping);

#endif /* _LCRP_CHANGELOG_H_ */
