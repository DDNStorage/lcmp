# Copyright (c) 2019 DataDirect Networks, Inc.
# All Rights Reserved.
# Author: lixi@ddn.com
"""
Common functions
"""
import sys
import os
from pylcommon import utils


def c_command_path(cmd_name):
    """
    Return the path of the command
    """
    command = sys.argv[0]
    if "/" in command:
        directory = os.path.dirname(command)
        fpath = directory + "/src/" + cmd_name
        if utils.is_exe(fpath):
            return fpath

    return cmd_name
