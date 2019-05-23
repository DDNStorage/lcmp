/*
 * Copyright (c) 2019 DDN Storage, Inc
 *
 * Author: Li Xi lixi@ddn.com
 */
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>

#define LCRP_MAXLEN 64
#define LCRP_NAME_ACTIVE "active"
#define LCRP_NAME_FIDS "fids"
#define LCRP_NAME_INACTIVE "inactive"
#define LCRP_NAME_SECONDARY "secondary"

struct lcrp_status {
	/* Current working directory */
	char ls_cwd[PATH_MAX + 1];
	/* MDT device to get Changelog from */
	char ls_mdt_device[LCRP_MAXLEN + 1];
	/* Changelog user */
	char ls_changelog_user[LCRP_MAXLEN + 1];
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

/* Command line options */
struct option lcrp_long_opts[] = {
	{ .val = 'd',	.name = "dir",		.has_arg = required_argument },
	{ .val = 'm',	.name = "mdt",		.has_arg = required_argument },
	{ .val = 'u',	.name = "user",		.has_arg = required_argument },
	{ .name = NULL } };

struct lcrp_status *lcrp_status;

static void lcrp_fini(void)
{
	free(lcrp_status);
}

static int lcrp_init(void)
{
	int rc = 0;
	char *cwd;
	size_t size = sizeof(*lcrp_status);

	lcrp_status = calloc(size, 1);
	if (lcrp_status == NULL)
		return -ENOMEM;

	cwd = getcwd(lcrp_status->ls_cwd, sizeof(lcrp_status->ls_cwd));
	if (cwd == NULL) {
		fprintf(stderr, "failed to get cwd: %s\n", strerror(errno));
		rc = -errno;
		goto error;
	}
	return 0;
error:
	lcrp_fini();
	return rc;
}

static void lcrp_usage(void)
{
	fprintf(stderr,
		"Usage: lcrp -d <access_history_directory> -m <fsname-MDTnumber> -u user\n");
}

static int lcrp_get_record_fid(struct changelog_rec *rec,
			       struct lu_fid *fid)
{
	if (fid_is_zero(&rec->cr_tfid) &&
	    rec->cr_type == CL_RENAME && rec->cr_flags & CLF_RENAME) {
		struct changelog_ext_rename *rnm = changelog_rec_rename(rec);

		if (fid_is_zero(&rnm->cr_sfid)) {
			fprintf(stderr, "cannot find usable fid in rec %llu\n",
				rec->cr_index);
			return -EIO;
		}
		*fid = rnm->cr_sfid;
	} else {
		*fid = rec->cr_tfid;
	}

	return 0;
}

static int lcrp_get_fid_path(const char *root, char *pbuffer, int psize,
			     char *buffer, int size, struct lu_fid *fid)
{
	int rc;

	rc = snprintf(pbuffer, psize, "%s/%04x/", root,
		      fid->f_oid & 0xFFFF);
	if (rc < 0) {
		fprintf(stderr, "failed to generate the parent path for fid "DFID": %s\n",
			PFID(fid), strerror(-rc));
		return rc;
	} else if (rc >= psize) {
		fprintf(stderr,
			"failed to generate the parent path for fid "DFID" because buffser size %d is not enough\n",
			PFID(fid), psize);
		return -E2BIG;
	}

	rc = snprintf(buffer, size, "%s/%04x/" DFID_NOBRACE,
		      root, fid->f_oid & 0xFFFF,
		      PFID(fid));
	if (rc < 0) {
		fprintf(stderr, "failed to generate the path for fid "DFID": %s\n",
			PFID(fid), strerror(-rc));
		return rc;
	} else if (rc >= size) {
		fprintf(stderr,
			"failed to generate the path for fid "DFID" because buffser size %d is not enough\n",
			PFID(fid), size);
		return -E2BIG;
	}
	return 0;
}

static int lcrp_find_or_mkdir(const char *path)
{
	int rc;
	struct stat stat_buf;

	rc = stat(path, &stat_buf);
	if (rc == 0) {
		if (!S_ISDIR(stat_buf.st_mode)) {
			fprintf(stderr, "%s is not direcotry\n", path);
			return -EIO;
		}
		return 0;
	} else if (rc) {
		if (errno == ENOENT) {
			rc = mkdir(path, 0644);
			if (rc) {
				fprintf(stderr, "failed to create %s: %s\n",
					path, strerror(errno));
				return -errno;
			}
		} else {
			fprintf(stderr, "failed to stat %s: %s\n", path,
				strerror(errno));
			return -errno;
		}
	}
	return 0;
}

static int lcrp_find_or_create(const char *path)
{
	int rc;
	struct stat stat_buf;

	rc = stat(path, &stat_buf);
	if (rc == 0) {
		if (!S_ISREG(stat_buf.st_mode)) {
			fprintf(stderr, "%s is not regular file\n", path);
			return -EIO;
		}
		return 0;
	} else if (rc) {
		if (errno == ENOENT) {
			rc = creat(path, 0644);
			if (rc < 0) {
				fprintf(stderr, "failed to create %s: %s\n",
					path, strerror(errno));
				return -errno;
			}
			close(rc);
			rc = 0;
		} else {
			fprintf(stderr, "failed to stat %s: %s\n", path,
				strerror(errno));
			return -errno;
		}
	}
	return 0;
}

static int lcrp_find_or_create_fid(char *buf, int buf_size, struct lu_fid *fid)
{
	int rc;
	char parent_path[PATH_MAX + 1];

	rc = lcrp_get_fid_path(lcrp_status->ls_dir_fid, parent_path,
			       sizeof(parent_path), buf, buf_size, fid);
	if (rc) {
		fprintf(stderr, "failed to get path of fid "DFID"\n",
			PFID(fid));
		return rc;
	}

	rc = lcrp_find_or_mkdir(parent_path);
	if (rc) {
		fprintf(stderr, "failed to find or create directory %s\n",
			parent_path);
		return rc;
	}

	rc = lcrp_find_or_create(buf);
	if (rc) {
		fprintf(stderr, "failed to find or create file %s\n", buf);
		return rc;
	}

	return 0;
}

static int lcrp_find_or_link(const char *old_path, const char *new_path)
{
	int rc;
	struct stat stat_buf;

	rc = stat(new_path, &stat_buf);
	if (rc == 0) {
		if (!S_ISREG(stat_buf.st_mode)) {
			fprintf(stderr, "%s is not regular file\n", new_path);
			return -EIO;
		}
		return 0;
	} else if (rc) {
		if (errno == ENOENT) {
			rc = link(old_path, new_path);
			if (rc < 0) {
				fprintf(stderr, "failed to link %s to %s: %s\n",
					new_path, old_path, strerror(errno));
				return -errno;
			}
			close(rc);
			rc = 0;
		} else {
			fprintf(stderr, "failed to stat %s: %s\n", new_path,
				strerror(errno));
			return -errno;
		}
	}
	return 0;
}

static int lcrp_find_or_link_fid(const char *root, struct lu_fid *fid,
				 const char *fid_path)
{
	int rc;
	char link_path[PATH_MAX + 1];
	char parent_path[PATH_MAX + 1];

	rc = lcrp_get_fid_path(root, parent_path, sizeof(parent_path),
			       link_path, sizeof(link_path), fid);
	if (rc) {
		fprintf(stderr, "failed to get path of fid "DFID"\n",
			PFID(fid));
		return rc;
	}

	rc = lcrp_find_or_mkdir(root);
	if (rc) {
		fprintf(stderr, "failed to find or create directory %s\n",
			root);
		return rc;
	}

	rc = lcrp_find_or_mkdir(parent_path);
	if (rc) {
		fprintf(stderr, "failed to find or create directory %s\n",
			parent_path);
		return rc;
	}

	rc = lcrp_find_or_link(fid_path, link_path);
	if (rc) {
		fprintf(stderr, "failed to find or create link %s to %s\n",
			link_path, fid_path);
		return rc;
	}

	return 0;
}

static int lcrp_update_fid(struct lu_fid *fid)
{
	int rc;
	char fid_path[PATH_MAX + 1];

	printf("handling fid "DFID"\n", PFID(fid));
	rc = lcrp_find_or_create_fid(fid_path, sizeof(fid_path), fid);
	if (rc) {
		fprintf(stderr, "failed to find path of FID "DFID"\n",
			PFID(fid));
		return rc;
	}

	rc = lcrp_find_or_link_fid(lcrp_status->ls_dir_active, fid, fid_path);
	if (rc) {
		fprintf(stderr,
			"failed to find or link FID "DFID" to active directory\n",
			PFID(fid));
		return rc;
	}
	return rc;
}

static int lcrp_changelog_parse_record(void *changelog_priv)
{
	int rc;
	struct changelog_rec *rec;
	struct lu_fid fid;

	rc = llapi_changelog_recv(changelog_priv, &rec);
	if (rc < 0) {
		fprintf(stderr, "failed to read changelog: %s\n",
			strerror(-rc));
		return rc;
	}

	/* no more to read */
	if (rc)
		return rc;

	rc = lcrp_get_record_fid(rec, &fid);
	if (rc) {
		fprintf(stderr, "failed to get fid of record\n");
		goto out;
	}

	rc = lcrp_update_fid(&fid);
	if (rc)  {
		fprintf(stderr, "failed to update access of fid "DFID"\n",
			PFID(&fid));
		goto out;
	}

	/* clear entry */
	rc = llapi_changelog_clear(lcrp_status->ls_mdt_device,
				   lcrp_status->ls_changelog_user,
				   rec->cr_index);
	if (rc) {
		fprintf(stderr, "failed to clear record %lld\n",
			rec->cr_index);
		goto out;
	}

out:
	llapi_changelog_free(&rec);
	return rc;
}

static int lcrp_main(void)
{
	void *changelog_priv;
	int rc = 0;

	rc = llapi_changelog_start(&changelog_priv,
				   CHANGELOG_FLAG_BLOCK |
				   CHANGELOG_FLAG_JOBID |
				   CHANGELOG_FLAG_EXTRA_FLAGS,
				   lcrp_status->ls_mdt_device, 0);
	if (rc < 0) {
		fprintf(stderr, "failed to open Changelog file for %s: %s\n",
			lcrp_status->ls_mdt_device, strerror(-rc));
		return rc;
	}

	rc = llapi_changelog_set_xflags(changelog_priv,
					CHANGELOG_EXTRA_FLAG_UIDGID |
					CHANGELOG_EXTRA_FLAG_NID |
					CHANGELOG_EXTRA_FLAG_OMODE |
					CHANGELOG_EXTRA_FLAG_XATTR);
	if (rc < 0) {
		fprintf(stderr, "failed to set xflags for Changelog: %s\n",
			strerror(-rc));
		goto out;
	}

	while (!lcrp_status->ls_stopping) {
		rc = lcrp_changelog_parse_record(changelog_priv);
		if (rc < 0) {
			fprintf(stderr,
				"failed to parse record of Changelog: %s\n",
				strerror(-rc));
			break;
		} else if (rc) {
			printf("all records of Changelog has been proceeded\n");
			rc = 0;
			break;
		}
	}

out:
	llapi_changelog_fini(&changelog_priv);
	return rc;
}

static int lcrp_init_dir(const char *dir_access_history)
{
	int rc;
	char *resolved_path;
	char path[PATH_MAX + 1];

	if (strlen(dir_access_history) < 1) {
		fprintf(stderr,
			"unexpected zero length of access history directory\n");
		return -EINVAL;
	}

	if (dir_access_history[0] == '/')
		snprintf(path, sizeof(path), "%s", dir_access_history);
	else
		snprintf(path, sizeof(path), "%s/%s", lcrp_status->ls_cwd,
			 dir_access_history);

	resolved_path = realpath(path, lcrp_status->ls_dir_access_history);
	if (resolved_path == NULL) {
		fprintf(stderr,
			"failed to get real path of %s\n", path);
		return -errno;
	}

	snprintf(lcrp_status->ls_dir_fid, sizeof(lcrp_status->ls_dir_fid),
		 "%s/%s", lcrp_status->ls_dir_access_history,
		 LCRP_NAME_FIDS);
	rc = lcrp_find_or_mkdir(lcrp_status->ls_dir_fid);
	if (rc) {
		fprintf(stderr, "failed to find or create directory %s\n",
			lcrp_status->ls_dir_fid);
		return rc;
	}

	snprintf(lcrp_status->ls_dir_active,
		 sizeof(lcrp_status->ls_dir_active), "%s/%s",
		 lcrp_status->ls_dir_access_history, LCRP_NAME_ACTIVE);
	snprintf(lcrp_status->ls_dir_secondary,
		 sizeof(lcrp_status->ls_dir_secondary), "%s/%s",
		 lcrp_status->ls_dir_access_history, LCRP_NAME_SECONDARY);
	snprintf(lcrp_status->ls_dir_inactive,
		 sizeof(lcrp_status->ls_dir_inactive), "%s/%s",
		 lcrp_status->ls_dir_access_history, LCRP_NAME_INACTIVE);

	return 0;
}

static void
lcrp_signal_handler(int signum)
{
	/* Set a flag for the replicator to gracefully shutdown */
	lcrp_status->ls_stopping = true;
	/* just indicate we have to stop */
	printf("received signal %d, stopping\n",
	       signum);
}


int main(int argc, char *argv[])
{
	int rc;
	const char *dir_access_history = NULL;

	rc = lcrp_init();
	if (rc) {
		fprintf(stderr, "failed to init status\n");
		return rc;
	}

	while ((rc = getopt_long(argc, argv, "d:m:u:",
				 lcrp_long_opts, NULL)) >= 0) {
		switch (rc) {
		case 'd':
			dir_access_history = optarg;
			break;
		case 'm':
			snprintf(lcrp_status->ls_mdt_device,
				 sizeof(lcrp_status->ls_mdt_device),
				 "%s", optarg);
			break;
		case 'u':
			snprintf(lcrp_status->ls_changelog_user,
				 sizeof(lcrp_status->ls_changelog_user),
				 "%s", optarg);
			break;
		default:
			fprintf(stderr, "option '%s' unrecognized\n",
				argv[optind - 1]);
			lcrp_usage();
			goto out;
		}
	}

	if (dir_access_history == NULL) {
		fprintf(stderr,
			"please specify the root directory for saving access history by using -d option\n");
		lcrp_usage();
		return -1;
	}

	rc = lcrp_init_dir(dir_access_history);
	if (rc) {
		fprintf(stderr,
			"failed to init access history directory %s\n",
			dir_access_history);
		return -1;
	}

	if (lcrp_status->ls_mdt_device[0] == '\0') {
		fprintf(stderr,
			"please specify the MDT device by using -m option\n");
		lcrp_usage();
		return -1;
	}

	if (lcrp_status->ls_changelog_user[0] == '\0') {
		fprintf(stderr,
			"please specify the Changelog user by using -u option\n");
		lcrp_usage();
		return -1;
	}

	signal(SIGINT, lcrp_signal_handler);
	signal(SIGHUP, lcrp_signal_handler);
	signal(SIGTERM, lcrp_signal_handler);

	rc = lcrp_main();
out:
	lcrp_fini();
	return rc;
}
