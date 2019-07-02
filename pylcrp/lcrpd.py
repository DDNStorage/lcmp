# Copyright (c) 2019 DataDirect Networks, Inc.
# All Rights Reserved.
# Author: lixi@ddn.com
"""
Run LCRP Deamon
"""
import os
import traceback
import socket
import yaml

from pylcrp import common
from pylcommon import watched_io
from pylcommon import utils
from pylcommon import cmd_general
from pylcommon import ssh_host

LCRPD_CONFIG = "/etc/lcrpd.conf"
LCRPD_LOG_DIR = "/var/log/lcrpd"

LCRPD_STR_LCRP_DIR = "lcrp_dir"
LCRPD_STR_FSNAME = "fsname"
LCRPD_STR_CHANGELOG_USER = "changelog_user"

LCRP_CMD_CHANGELOG = "lcrp_changelog"


def lcrp_changelog(log, fsname, lcrp_dir, changelog_user):
    """
    Run lcrp_changelog
    """
    hostname = socket.gethostname()
    host = ssh_host.SSHHost(hostname, local=True)
    command = common.c_command_path(LCRP_CMD_CHANGELOG)
    command += " -d " + lcrp_dir
    command += " -m %s-MDT0000" % fsname
    command += " -u " + changelog_user
    args = {}
    args[watched_io.WATCHEDIO_LOG] = log
    args[watched_io.WATCHEDIO_HOSTNAME] = host.sh_hostname
    stdout_fd = watched_io.watched_io_open("/dev/null",
                                           watched_io.log_watcher_info_simplified,
                                           args)
    stderr_fd = watched_io.watched_io_open("/dev/null",
                                           watched_io.log_watcher_error_simplified,
                                           args)
    log.cl_debug("start to run command [%s] on host [%s]",
                 command, host.sh_hostname)
    retval = host.sh_run(log, command, stdout_tee=stdout_fd,
                         stderr_tee=stderr_fd, return_stdout=False,
                         return_stderr=False, timeout=None, flush_tee=True)
    stdout_fd.close()
    stderr_fd.close()

    return retval.cr_exit_status


def lcrpd_do_loop(log, workspace, config, config_fpath):
    """
    Daemon routine
    """
    # pylint: disable=unused-argument
    fsname = utils.config_value(config, LCRPD_STR_FSNAME)
    if fsname is None:
        log.cl_error("no [%s] is configured, please correct config file [%s]",
                     LCRPD_STR_FSNAME, config_fpath)
        return -1

    lcrp_dir = utils.config_value(config, LCRPD_STR_LCRP_DIR)
    if lcrp_dir is None:
        log.cl_error("no [%s] is configured, please correct config file [%s]",
                     LCRPD_STR_LCRP_DIR, config_fpath)
        return -1

    changelog_user = utils.config_value(config, LCRPD_STR_CHANGELOG_USER)
    if changelog_user is None:
        log.cl_error("no [%s] is configured, please correct config file [%s]",
                     LCRPD_STR_CHANGELOG_USER, config_fpath)
        return -1

    if not os.path.isdir(lcrp_dir):
        log.cl_error("[%s] is not directory, please correct [%s] of config file [%s]",
                     lcrp_dir, LCRPD_STR_LCRP_DIR, config_fpath)
        return -1

    return lcrp_changelog(log, fsname, lcrp_dir, changelog_user)


def lcrpd_loop(log, workspace, config_fpath):
    """
    Start Clownfish holding the configure lock
    """
    # pylint: disable=bare-except
    config_fd = open(config_fpath)
    ret = 0
    try:
        config = yaml.load(config_fd)
    except:
        log.cl_error("not able to load [%s] as yaml file: %s", config_fpath,
                     traceback.format_exc())
        ret = -1
    config_fd.close()
    if ret:
        return -1

    try:
        ret = lcrpd_do_loop(log, workspace, config, config_fpath)
    except:
        ret = -1
        log.cl_error("exception: %s", traceback.format_exc())

    return ret


def main():
    """
    Run LCRP Deamon
    """
    cmd_general.main(LCRPD_CONFIG, LCRPD_LOG_DIR,
                     lcrpd_loop)
