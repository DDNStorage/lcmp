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
#include <sys/time.h>

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
	struct lcrp_epoch *epoch = &lcrp_status->ls_epoch;

	pthread_mutex_destroy(&epoch->le_mutex);
	free(lcrp_status);
}

static int lcrp_init(void)
{
	char *cwd;
	int rc = 0;
	struct lcrp_epoch *epoch;
	size_t size = sizeof(*lcrp_status);

	lcrp_status = calloc(size, 1);
	if (lcrp_status == NULL)
		return -ENOMEM;

	epoch = &lcrp_status->ls_epoch;

	cwd = getcwd(lcrp_status->ls_cwd, sizeof(lcrp_status->ls_cwd));
	if (cwd == NULL) {
		LERROR("failed to get cwd: %s\n", strerror(errno));
		rc = -errno;
		goto error;
	}
	epoch->le_seconds = 3600;
	pthread_mutex_init(&epoch->le_mutex, NULL);
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
	char *end;
	struct lcrp_epoch *epoch = &lcrp_status->ls_epoch;

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
	} else if (strcmp(key, LCRP_STR_MDT_DEVICE) == 0) {
		if (strlen(value) + 1 >
		    sizeof(lcrp_status->ls_mdt_device)) {
			LERROR("value of key/value [%s = %s] is too long\n",
			       key, value);
			return -EINVAL;
		}

		strcpy(lcrp_status->ls_mdt_device, value);
	}  else if (strcmp(key, LCRP_STR_EPOCH_INTERVAL) == 0) {
		epoch->le_seconds = strtoul(value, &end, 0);
		if (*end != '\0') {
			LERROR("invalid value of key/value [%s = %s], should be number\n",
			       key, value, LCRP_MIN_EPOCH_INTERVAL);
			return -EINVAL;
		}
		if (epoch->le_seconds < LCRP_MIN_EPOCH_INTERVAL) {
			LERROR("too small epoch interval in [%s = %s], should >= %d\n",
			       key, value, LCRP_MIN_EPOCH_INTERVAL);
			return -EINVAL;
		}
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
		LERROR("failed to get real path of [%s]\n", path);
		return -errno;
	}

	snprintf(lcrp_status->ls_dir_fid, sizeof(lcrp_status->ls_dir_fid),
		 "%s/%s", lcrp_status->ls_dir_access_history,
		 LCRP_NAME_FIDS);
	rc = lcrp_find_or_mkdir(lcrp_status->ls_dir_fid);
	if (rc) {
		LERROR("failed to find or create directory [%s]\n",
			lcrp_status->ls_dir_fid);
		return rc;
	}

	snprintf(lcrp_status->ls_dir_active,
		 sizeof(lcrp_status->ls_dir_active), "%s/%s",
		 lcrp_status->ls_dir_access_history, LCRP_NAME_ACTIVE);
	rc = lcrp_find_or_mkdir(lcrp_status->ls_dir_active);
	if (rc) {
		LERROR("failed to find or create directory [%s]\n",
			lcrp_status->ls_dir_active);
		return rc;
	}

	snprintf(lcrp_status->ls_dir_secondary,
		 sizeof(lcrp_status->ls_dir_secondary), "%s/%s",
		 lcrp_status->ls_dir_access_history, LCRP_NAME_SECONDARY);
	rc = lcrp_find_or_mkdir(lcrp_status->ls_dir_secondary);
	if (rc) {
		LERROR("failed to find or create directory [%s]\n",
			lcrp_status->ls_dir_secondary);
		return rc;
	}

	snprintf(lcrp_status->ls_dir_inactive,
		 sizeof(lcrp_status->ls_dir_inactive), "%s/%s",
		 lcrp_status->ls_dir_access_history, LCRP_NAME_INACTIVE);
	rc = lcrp_find_or_mkdir(lcrp_status->ls_dir_inactive);
	if (rc) {
		LERROR("failed to find or create directory [%s]\n",
			lcrp_status->ls_dir_inactive);
		return rc;
	}
	return 0;
}

void *lcrp_changelog_thread(void *arg)
{
	int rc;
	struct lcrp_epoch *epoch = &lcrp_status->ls_epoch;
	struct lcrp_changelog_thread_info *info = arg;

	while ((!lcrp_status->ls_stopping) && !(info->lcti_stopping)) {
		rc = lcrp_changelog_consume(lcrp_status->ls_dir_fid,
					    epoch,
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
	info->lcti_stopped = true;
	return NULL;
}

static int lcrp_changelog_thread_stop(struct lcrp_changelog_thread_info *info)
{
	int rc;

	if (!info->lcti_started)
		return 0;

	info->lcti_stopping = true;

	rc = pthread_join(info->lcti_thread_id, NULL);
	if (rc) {
		LERROR("failed to join thread [%d]\n",
		       info->lcti_thread_id);
	}
	return rc;
}

static int lcrp_changelog_thread_start(struct lcrp_changelog_thread_info *info)
{
	int rc;
	int ret;
	pthread_attr_t attr;

	rc = pthread_attr_init(&attr);
	if (rc) {
		LERROR("failed to set pthread attribute\n");
		return rc;
	}

	rc = pthread_create(&info->lcti_thread_id, &attr,
			    &lcrp_changelog_thread, info);
	if (rc == 0)
		info->lcti_started = true;

	ret = pthread_attr_destroy(&attr);
	if (ret) {
		LERROR("failed to destroy thread attribute\n");
		if (rc == 0)
			rc = ret;
	}

	if (rc) {
		ret = lcrp_changelog_thread_stop(info);
		if (ret)
			LERROR("failed to stop thread\n");
	}
	return rc;
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

static int _lcrp_epoch_update(struct lcrp_epoch *epoch, int current_second)
{
	int rc;
	int interval = epoch->le_seconds;

	pthread_mutex_lock(&epoch->le_mutex);
	epoch->le_start = current_second;
	epoch->le_end = current_second + interval;
	snprintf(epoch->le_dir_active, sizeof(epoch->le_dir_active),
		 "%s/%d-%d", lcrp_status->ls_dir_active,
		 epoch->le_start, epoch->le_end);
	rc = lcrp_find_or_mkdir(epoch->le_dir_active);
	if (rc) {
		LERROR("failed to find or create directory [%s]\n",
		       epoch->le_dir_active);
		goto out;
	}
out:
	pthread_mutex_unlock(&epoch->le_mutex);
	LERROR("updated epoch to [%d-%d]", epoch->le_start, epoch->le_end);
	return rc;
}

static int lcrp_epoch_update(void)
{
	int rc;
	int seconds;
	struct timeval this_time;
	struct lcrp_epoch *epoch = &lcrp_status->ls_epoch;
	int interval = epoch->le_seconds;

	gettimeofday(&this_time, NULL);
	seconds = this_time.tv_sec;
	seconds = seconds / interval * interval;
	if (seconds != epoch->le_start) {
		rc = _lcrp_epoch_update(epoch, seconds);
		if (rc) {
			LERROR("failed to update epoch\n");
			return rc;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int rc;
	char *scalar;
	FILE *config;
	yaml_token_t token;
	yaml_parser_t parser;
	char key[PATH_MAX + 1];
	char value[PATH_MAX + 1];
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
		       LCRP_STR_MDT_DEVICE, config_fpath);
		rc = -EINVAL;
		goto out;
	}

	if (lcrp_status->ls_changelog_user[0] == '\0') {
		LERROR("[%s] is not configured in [%s]\n",
		       LCRP_STR_CHANGELOG_USER, config_fpath);
		rc = -EINVAL;
		goto out;
	}

	rc = lcrp_epoch_update();
	if (rc) {
		LERROR("failed to init epoch\n");
		goto out;
	}

	signal(SIGINT, lcrp_signal_handler);
	signal(SIGHUP, lcrp_signal_handler);
	signal(SIGTERM, lcrp_signal_handler);

	rc = lcrp_changelog_thread_start(&lcrp_status->ls_info);
	if (rc) {
		LERROR("failed to start Changelog thread\n");
		goto out;
	}

	while (!lcrp_status->ls_stopping) {
		sleep(1);
		rc = lcrp_epoch_update();
		if (rc) {
			LERROR("failed to init epoch\n");
			goto out;
		}
	}

	rc = lcrp_changelog_thread_stop(&lcrp_status->ls_info);
	if (rc) {
		LERROR("failed to stop Changelog thread\n");
		goto out;
	}
out:
	lcrp_fini();
out_close:
	fclose(config);
	return rc;
}
