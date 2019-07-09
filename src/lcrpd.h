/*
 * Copyright (c) 2016, DDN Storage Corporation.
 */
/*
 * Author: Li Xi <lixi@ddn.com>
 */
#ifndef _LCRPD_H_
#define _LCRPD_H_

#include "lcrp_changelog.h"

/* Minimal number of epoch interval */
#define LCRP_MIN_EPOCH_INTERVAL 10
/* Maximum number of epoch interval, 100 days */
#define LCRP_MAX_EPOCH_INTERVAL 8640000
#define LCRP_MAXLEN 64
#define LCRPD_CONFIG "/etc/lcrpd.conf"
#define LCRP_STR_CHANGELOG_USER	"changelog_user"
#define LCRP_STR_LCRP_DIR	"lcrp_dir"
#define LCRP_STR_MDT_DEVICE	"mdt_device"
#define LCRP_STR_EPOCH_INTERVAL	"epoch_interval"

#define LCRP_NAME_ACTIVE "active"
#define LCRP_NAME_FIDS "fids"
#define LCRP_NAME_INACTIVE "inactive"
#define LCRP_NAME_INACTIVE_ALL "all"
#define LCRP_NAME_SECONDARY "secondary"

struct lcrp_thread_info {
	/* ID returned by pthread_create() */
	pthread_t		 lti_thread_id;
	/* Thread is started */
	bool			 lti_started;
	/* Thread is asked to stop */
	bool			 lti_stopping;
	/* Thread is stopped */
	bool			 lti_stopped;
};

struct lcrp_inactive_thread_info {
	/* General thread info */
	struct lcrp_thread_info	liti_general;
};

struct lcrp_changelog_thread_info {
	/* MDT device to get Changelog from */
	char			lcti_mdt_device[LCRP_MAXLEN + 1];
	/* General thread info */
	struct lcrp_thread_info	lcti_general;
};

struct lcrp_status {
	/* Current working directory */
	char ls_cwd[PATH_MAX + 1];
	/* Changelog user */
	char ls_changelog_user[LCRP_MAXLEN + 1];
	/* MDT device to get Changelog from */
	char ls_mdt_device[LCRP_MAXLEN + 1];
	/* Root directory that saves the access history */
	char ls_dir_access_history[PATH_MAX + 1];
	/* Directory that saves all fids */
	char ls_dir_fid[PATH_MAX + 1];
	/* Directory of fids that are being actively accessed */
	char ls_dir_active[PATH_MAX + 1];
	/* Directory of secondary */
	char ls_dir_secondary[PATH_MAX + 1];
	/* Directory of inactive */
	char ls_dir_inactive[PATH_MAX + 1];
	/* Directory of all inactive FIDs, under inactive directory */
	char ls_dir_inactive_all[PATH_MAX + 1];
	/* Singal recieved so stopping */
	bool ls_stopping;
	/* Info of Changlog thread */
	struct lcrp_changelog_thread_info ls_changelog_info;
	/* Info of inactive thread */
	struct lcrp_inactive_thread_info ls_inactive_info;
	/* Epoch that could change from to time */
	struct lcrp_epoch ls_epoch;
};

#endif /* _LCRPD_H_ */
