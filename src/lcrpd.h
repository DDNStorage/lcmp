/*
 * Copyright (c) 2016, DDN Storage Corporation.
 */
/*
 * Author: Li Xi <lixi@ddn.com>
 */
#ifndef _LCRPD_H_
#define _LCRPD_H_

#define LCRP_MAXLEN 64
#define LCRPD_CONFIG "/etc/lcrpd.conf"
#define LCRP_STR_CHANGELOG_USER		"changelog_user"
#define LCRP_STR_LCRP_DIR		"lcrp_dir"
#define LCRP_STR_LCRP_MDT_DEVICE	"mdt_device"

#define LCRP_NAME_ACTIVE "active"
#define LCRP_NAME_FIDS "fids"
#define LCRP_NAME_INACTIVE "inactive"
#define LCRP_NAME_SECONDARY "secondary"

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
	/* Directory of current */
	char ls_dir_secondary[PATH_MAX + 1];
	/* Directory of current */
	char ls_dir_inactive[PATH_MAX + 1];
	/* Singal recieved so stopping */
	bool ls_stopping;
};

#endif /* _LCRPD_H_ */
