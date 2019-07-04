/*
 * Copyright (c) 2019 DDN Storage, Inc
 *
 * Author: Li Xi lixi@ddn.com
 */
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <lustre/lustreapi.h>

#include "debug.h"

#define LCRP_INTERVAL_EOF 3
#define LCRP_INTERVAL_RETRY 1

enum changelog_record_status {
	LRS_OK = 0, /* Got a record*/
	LRS_RETRY = 1, /* Try to get the record again */
	LRS_EOF = 2, /* No more record  */
	LRS_RESTART = 3, /* Call llapi_changelog_start again  */
};

static int lcrp_get_record_fid(struct changelog_rec *rec,
			       struct lu_fid *fid)
{
	if (fid_is_zero(&rec->cr_tfid) &&
	    rec->cr_type == CL_RENAME && rec->cr_flags & CLF_RENAME) {
		struct changelog_ext_rename *rnm = changelog_rec_rename(rec);

		if (fid_is_zero(&rnm->cr_sfid)) {
			LERROR("cannot find usable fid in rec %llu\n",
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
		LERROR("failed to generate the parent path for fid "DFID": %s\n",
			PFID(fid), strerror(-rc));
		return rc;
	} else if (rc >= psize) {
		LERROR("failed to generate the parent path for fid "DFID" because buffser size %d is not enough\n",
			PFID(fid), psize);
		return -E2BIG;
	}

	rc = snprintf(buffer, size, "%s/%04x/" DFID_NOBRACE,
		      root, fid->f_oid & 0xFFFF,
		      PFID(fid));
	if (rc < 0) {
		LERROR("failed to generate the path for fid "DFID": %s\n",
			PFID(fid), strerror(-rc));
		return rc;
	} else if (rc >= size) {
		LERROR("failed to generate the path for fid "DFID" because buffser size %d is not enough\n",
			PFID(fid), size);
		return -E2BIG;
	}
	return 0;
}

int lcrp_find_or_mkdir(const char *path)
{
	int rc;
	struct stat stat_buf;

	rc = stat(path, &stat_buf);
	if (rc == 0) {
		if (!S_ISDIR(stat_buf.st_mode)) {
			LERROR("%s is not direcotry\n", path);
			return -EIO;
		}
		return 0;
	} else if (rc) {
		if (errno == ENOENT) {
			rc = mkdir(path, 0644);
			if (rc) {
				LERROR("failed to create %s: %s\n",
					path, strerror(errno));
				return -errno;
			}
		} else {
			LERROR("failed to stat %s: %s\n", path,
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
			LERROR("%s is not regular file\n", path);
			return -EIO;
		}
		return 0;
	} else if (rc) {
		if (errno == ENOENT) {
			rc = creat(path, 0644);
			if (rc < 0) {
				LERROR("failed to create %s: %s\n",
					path, strerror(errno));
				return -errno;
			}
			close(rc);
			rc = 0;
		} else {
			LERROR("failed to stat %s: %s\n", path,
				strerror(errno));
			return -errno;
		}
	}
	return 0;
}

static int lcrp_find_or_create_fid(const char *dir_fid, char *buf,
				   int buf_size, struct lu_fid *fid)
{
	int rc;
	char parent_path[PATH_MAX + 1];

	rc = lcrp_get_fid_path(dir_fid, parent_path,
			       sizeof(parent_path), buf, buf_size, fid);
	if (rc) {
		LERROR("failed to get path of fid "DFID"\n",
			PFID(fid));
		return rc;
	}

	rc = lcrp_find_or_mkdir(parent_path);
	if (rc) {
		LERROR("failed to find or create directory %s\n",
			parent_path);
		return rc;
	}

	rc = lcrp_find_or_create(buf);
	if (rc) {
		LERROR("failed to find or create file %s\n", buf);
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
			LERROR("%s is not regular file\n", new_path);
			return -EIO;
		}
		return 0;
	} else if (rc) {
		if (errno == ENOENT) {
			rc = link(old_path, new_path);
			if (rc < 0) {
				LERROR("failed to link %s to %s: %s\n",
					new_path, old_path, strerror(errno));
				return -errno;
			}
			close(rc);
			rc = 0;
		} else {
			LERROR("failed to stat %s: %s\n", new_path,
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
		LERROR("failed to get path of fid "DFID"\n",
			PFID(fid));
		return rc;
	}

	rc = lcrp_find_or_mkdir(root);
	if (rc) {
		LERROR("failed to find or create directory %s\n",
			root);
		return rc;
	}

	rc = lcrp_find_or_mkdir(parent_path);
	if (rc) {
		LERROR("failed to find or create directory %s\n",
			parent_path);
		return rc;
	}

	rc = lcrp_find_or_link(fid_path, link_path);
	if (rc) {
		LERROR("failed to find or create link %s to %s\n",
			link_path, fid_path);
		return rc;
	}

	return 0;
}

static int lcrp_update_fid(const char *dir_fid, const char *dir_active,
			   struct lu_fid *fid)
{
	int rc;
	char fid_path[PATH_MAX + 1];

	LINFO("handling fid "DFID"\n", PFID(fid));
	rc = lcrp_find_or_create_fid(dir_fid, fid_path, sizeof(fid_path), fid);
	if (rc) {
		LERROR("failed to find path of FID "DFID"\n",
			PFID(fid));
		return rc;
	}

	rc = lcrp_find_or_link_fid(dir_active, fid, fid_path);
	if (rc) {
		LERROR(
			"failed to find or link FID "DFID" to active directory\n",
			PFID(fid));
		return rc;
	}
	return rc;
}

/*
 * return "enum changelog_record_status" if no error, or the error is
 * recoverable; return negative error if unrecoverable failure.
 */
static int lcrp_changelog_recv(void *changelog_priv,
			       struct changelog_rec **rec)
{
	int rc;

	rc = llapi_changelog_recv(changelog_priv, rec);
	switch (rc) {
	case 0:
		return LRS_OK;
	case 1:	/* EOF */
		return LRS_EOF;
	case -EINVAL:  /* FS unmounted */
	case -EPROTO:  /* error in KUC channel */
		return LRS_RESTART;
	case -EINTR: /* Interruption */
		return LRS_RETRY;
	default:
		return rc;
	}
}

/*
 * return "enum changelog_record_status" if no error, or the error is
 * recoverable; return negative error if unrecoverable failure.
 */
static int lcrp_changelog_parse_record(void *changelog_priv,
				       const char *dir_fid,
				       const char *dir_active,
				       const char *mdt_device,
				       const char *changelog_user)
{
	int rc;
	struct changelog_rec *rec;
	struct lu_fid fid;

	rc = lcrp_changelog_recv(changelog_priv, &rec);
	if (rc < 0) {
		LERROR("failed to read changelog: %s\n",
			strerror(-rc));
		return rc;
	} else if (rc == LRS_EOF || rc == LRS_RETRY || rc == LRS_RESTART) {
		return rc;
	}

	LASSERT(rc == LRS_OK);
	rc = lcrp_get_record_fid(rec, &fid);
	if (rc) {
		LERROR("failed to get fid of record\n");
		goto out;
	}

	rc = lcrp_update_fid(dir_fid, dir_active, &fid);
	if (rc)  {
		LERROR("failed to update access of fid "DFID"\n",
			PFID(&fid));
		goto out;
	}

	/* clear entry */
	rc = llapi_changelog_clear(mdt_device,
				   changelog_user,
				   rec->cr_index);
	if (rc) {
		LERROR("failed to clear record %lld\n", rec->cr_index);
		goto out;
	}

out:
	llapi_changelog_free(&rec);
	return rc;
}

/*
 * return "enum changelog_record_status" if no error, or the error is
 * recoverable; return negative error if unrecoverable failure.
 */
static int lcrp_changelog_parse_records(void *changelog_priv,
					const char *dir_fid,
					const char *dir_active,
					const char *mdt_device,
					const char *changelog_user,
					bool *stopping)
{
	int rc = 0;

	while (!*stopping) {
		rc = lcrp_changelog_parse_record(changelog_priv, dir_fid,
						 dir_active, mdt_device,
						 changelog_user);
		if (rc < 0) {
			LERROR("failed to parse record of Changelog: %s\n",
			       strerror(-rc));
			break;
		} else if (rc == LRS_EOF) {
			LINFO("no record to parse, sleep for [%d] seconds waiting for new ones\n",
			      LCRP_INTERVAL_EOF);
			sleep(LCRP_INTERVAL_EOF);
			break;
		} else if (rc == LRS_RESTART) {
			LINFO("need to restart for failure, sleep for [%d] seconds before restarting\n",
			      LCRP_INTERVAL_RETRY);
			sleep(LCRP_INTERVAL_RETRY);
			break;
		} else if (rc == LRS_RETRY) {
			LINFO("temporary failure of getting record, sleep for [%d] seconds before retry\n",
			      LCRP_INTERVAL_RETRY);
			sleep(LCRP_INTERVAL_RETRY);
		} else {
			LASSERT(rc == 0);
		}
	}
	return rc;
}

/*
 * return "enum changelog_record_status" if no error, or the error is
 * recoverable; return negative error if unrecoverable failure.
 */
int lcrp_changelog_consume(const char *dir_fid, const char *dir_active,
			   const char *mdt_device, const char *changelog_user,
			   bool *stopping)
{
	int rc = 0;
	void *changelog_priv;
	enum changelog_send_flag flags =  (CHANGELOG_FLAG_BLOCK |
					   CHANGELOG_FLAG_JOBID |
					   CHANGELOG_FLAG_EXTRA_FLAGS);

	rc = llapi_changelog_start(&changelog_priv, flags,
				   mdt_device, 0);
	if (rc < 0) {
		LERROR("failed to open Changelog file for %s: %s\n",
		       mdt_device, strerror(-rc));
		return rc;
	}

	rc = llapi_changelog_set_xflags(changelog_priv,
					CHANGELOG_EXTRA_FLAG_UIDGID |
					CHANGELOG_EXTRA_FLAG_NID |
					CHANGELOG_EXTRA_FLAG_OMODE |
					CHANGELOG_EXTRA_FLAG_XATTR);
	if (rc < 0) {
		LERROR("failed to set xflags for Changelog: %s\n",
		       strerror(-rc));
		goto out;
	}

	rc = lcrp_changelog_parse_records(changelog_priv, dir_fid, dir_active,
					  mdt_device, changelog_user, stopping);
	if (rc < 0) {
		LERROR("failed to parse Changelog records\n");
		goto out;
	}

out:
	llapi_changelog_fini(&changelog_priv);
	return rc;
}
