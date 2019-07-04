/*
 * Copyright (c) 2019 DDN Storage, Inc
 *
 * Author: Li Xi lixi@ddn.com
 */

#include <yaml.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/limits.h>
#include "debug.h"
#include "lcrp_changelog.h"
#include "lcrpd.h"

static void lcrp_usage(void)
{
	LERROR("Usage: lcrp [config]\n");
}

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
		LERROR("failed to get cwd: %s\n", strerror(errno));
		rc = -errno;
		goto error;
	}
	return 0;
error:
	lcrp_fini();
	return rc;
}

enum lcrp_yaml_status {
	/* Next one should be YAML_KEY_TOKEN for key */
	LYS_INIT = 0,
	/* Next one should be YAML_SCALAR_TOKEN for key */
	LYS_KEY_INITED,
	/* Next one should be YAML_VALUE_TOKEN */
	LYS_KEY_FINISHED,
	/* Next one should be YAML_SCALAR_TOKEN for value */
	LYS_VALUE_INITED,
};

static int lcrp_set_key_value(const char *key, const char *value)
{
	if (strcmp(key, LCRP_STR_CHANGELOG_USER) == 0) {
		if (strlen(value) + 1 >
		    sizeof(lcrp_status->ls_changelog_user)) {
			LERROR("value of key/value [%s = %s] is too long\n",
			       key, value);
			return -EINVAL;
		}

		strcpy(lcrp_status->ls_changelog_user, value);
	} else if (strcmp(key, LCRP_STR_LCRP_DIR) == 0) {
		if (strlen(value) + 1 >
		    sizeof(lcrp_status->ls_dir_access_history)) {
			LERROR("value of key/value [%s = %s] is too long\n",
			       key, value);
			return -EINVAL;
		}

		strcpy(lcrp_status->ls_dir_access_history, value);
	} else if (strcmp(key, LCRP_STR_LCRP_MDT_DEVICE) == 0) {
		if (strlen(value) + 1 >
		    sizeof(lcrp_status->ls_mdt_device)) {
			LERROR("value of key/value [%s = %s] is too long\n",
			       key, value);
			return -EINVAL;
		}

		strcpy(lcrp_status->ls_mdt_device, value);
	} else {
		LERROR("unknown key %s\n", key);
		return -EINVAL;
	}
	return 0;
}

static int lcrp_init_dir(void)
{
	int rc;
	char *resolved_path;
	char path[PATH_MAX + 1];

	if (strlen(lcrp_status->ls_dir_access_history) < 1) {
		LERROR("unexpected zero length of access history directory\n");
		return -EINVAL;
	}

	if (lcrp_status->ls_dir_access_history[0] == '/')
		snprintf(path, sizeof(path), "%s",
			 lcrp_status->ls_dir_access_history);
	else
		snprintf(path, sizeof(path), "%s/%s", lcrp_status->ls_cwd,
			 lcrp_status->ls_dir_access_history);

	resolved_path = realpath(path, lcrp_status->ls_dir_access_history);
	if (resolved_path == NULL) {
		LERROR("failed to get real path of %s\n", path);
		return -errno;
	}

	snprintf(lcrp_status->ls_dir_fid, sizeof(lcrp_status->ls_dir_fid),
		 "%s/%s", lcrp_status->ls_dir_access_history,
		 LCRP_NAME_FIDS);
	rc = lcrp_find_or_mkdir(lcrp_status->ls_dir_fid);
	if (rc) {
		LERROR("failed to find or create directory %s\n",
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
	LINFO("received signal %d, stopping\n",
	       signum);
}

int main(int argc, char *argv[])
{
	int rc;
	FILE *config;
	//bool done = false;
	yaml_token_t token;
	yaml_parser_t parser;
	char key[PATH_MAX + 1];
	char value[PATH_MAX + 1];
	char *scalar;
	const char *config_fpath = LCRPD_CONFIG;
	enum lcrp_yaml_status yaml_status = LYS_INIT;

	if (argc > 2) {
		lcrp_usage();
		return -EINVAL;
	} else if (argc == 2) {
		config_fpath = argv[1];
	}

	config = fopen(config_fpath, "rb");
	if (config == NULL) {
		LERROR("failed to open [%s]: %s", config_fpath,
		       strerror(errno));
		return -errno;
	}

	rc = lcrp_init();
	if (rc) {
		LERROR("failed to init status\n");
		goto out_close;
	}

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, config);
	while (rc == 0) {
		yaml_parser_scan(&parser, &token);
		if (token.type == YAML_NO_TOKEN)
			break;
		switch (token.type) {
		case YAML_KEY_TOKEN:
			if (yaml_status != LYS_INIT) {
				LERROR("unexpected YAML status [%d], expected [%d]\n",
				       yaml_status, LYS_INIT);
				rc = -EINVAL;
				break;
			}
			yaml_status = LYS_KEY_INITED;
			break;
		case YAML_VALUE_TOKEN:
			if (yaml_status != LYS_KEY_FINISHED) {
				LERROR("unexpected YAML status [%d], expected [%d]\n",
				       yaml_status, LYS_KEY_FINISHED);
				rc = -EINVAL;
				break;
			}
			yaml_status = LYS_VALUE_INITED;
			break;
		case YAML_SCALAR_TOKEN:
			scalar = (char *)token.data.scalar.value;
			if (yaml_status != LYS_KEY_INITED &&
			    yaml_status != LYS_VALUE_INITED) {
				LERROR("unexpected YAML status [%d], expected [%d] or [%d]\n",
				       yaml_status, LYS_KEY_FINISHED,
				       LYS_VALUE_INITED);
				rc = -EINVAL;
			} else if (yaml_status == LYS_KEY_INITED) {
				if (strlen(scalar) + 1 > sizeof(key)) {
					LERROR("key [%s] is too long\n",
					       token.data.scalar.value);
					rc = -EINVAL;
					break;
				}
				strcpy(key, scalar);
				yaml_status = LYS_KEY_FINISHED;
			} else {
				if (strlen(scalar) + 1 > sizeof(value)) {
					LERROR("value [%s] is too long\n",
					       token.data.scalar.value);
					rc = -EINVAL;
					break;
				}
				strcpy(value, scalar);
				rc = lcrp_set_key_value(key, value);
				if (rc) {
					LERROR("failed to set key/value pair [%s = %s]\n",
					       key, value);
					rc = -EINVAL;
					break;
				}
				yaml_status = LYS_INIT;
			}
			break;
		default:
			break;
		}
		yaml_token_delete(&token);
	}
	yaml_parser_delete(&parser);

	if (rc)
		goto out;

	rc = lcrp_init_dir();
	if (rc) {
		LERROR("failed to init access history directory\n");
		goto out;
	}

	if (lcrp_status->ls_mdt_device[0] == '\0') {
		LERROR("[%s] is not configured in [%s]\n",
		       LCRP_STR_LCRP_MDT_DEVICE, config_fpath);
		rc = -EINVAL;
		goto out;
	}

	if (lcrp_status->ls_changelog_user[0] == '\0') {
		LERROR("[%s] is not configured in [%s]\n",
		       LCRP_STR_CHANGELOG_USER, config_fpath);
		rc = -EINVAL;
		goto out;
	}

	signal(SIGINT, lcrp_signal_handler);
	signal(SIGHUP, lcrp_signal_handler);
	signal(SIGTERM, lcrp_signal_handler);

	while (!lcrp_status->ls_stopping) {
		rc = lcrp_changelog_consume(lcrp_status->ls_dir_fid,
					    lcrp_status->ls_dir_active,
					    lcrp_status->ls_mdt_device,
					    lcrp_status->ls_changelog_user,
					    &lcrp_status->ls_stopping);
		if (rc < 0) {
			LERROR("failed to consume changelog\n");
			break;
		} else {
			rc = 0;
		}
	}
out:
	lcrp_fini();
out_close:
	fclose(config);
	return rc;
}
