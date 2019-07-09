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
	/* Epoch interval in seconds, should >= LCRP_MIN_EPOCH_INTERVAL */
	int le_seconds;
	/**
	 * Start epoch time of active directory
	 * Should be N * le_seconds, N = current_time / le_seconds * le_seconds
	 *
	 * The active time is (N * le_seconds, (N + 1) * le_seconds]
	 * The secondary time is ((N - 1) * le_seconds, N * le_seconds]
	 * The inactive time is (0, (N - 1) * le_seconds]
	 */
	int le_start;
	/* Directory of this epoch under active, protected by ls_mutex */
	char le_dir_active[PATH_MAX + 1];
};

int lcrp_find_or_mkdir(const char *path);
int lcrp_changelog_consume(const char *dir_fid, struct lcrp_epoch *epoch,
			   const char *mdt_device, const char *changelog_user,
			   bool *stopping);
int lcrp_find_or_create_fid(const char *dir_fid, char *buf, int buf_size,
			    struct lu_fid *fid);
int lcrp_find_or_link_fid(const char *root, struct lu_fid *fid,
			  const char *fid_path);
#endif /* _LCRP_CHANGELOG_H_ */
