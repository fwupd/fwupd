import grp
import os

EXIT_CODES = {"ERROR": 1, "SUCCESS": 0, "NOTHING_TO_DO": 2}

WARNING_COLOR = "\033[93m"


def create_dirs(*args):
    """Method creates directories.

    Keyword arguments:
    *args -- paths to be created
    """
    qubes_gid = grp.getgrnam("qubes").gr_gid
    old_umask = os.umask(0o002)
    if args is None:
        raise Exception("Creating directories failed, no paths given.")
    for file_path in args:
        if not os.path.exists(file_path):
            os.makedirs(file_path)
            os.chown(file_path, -1, qubes_gid)
        elif os.stat(file_path).st_gid != qubes_gid:
            print(
                f"{WARNING_COLOR}Warning: You should move a personal files"
                f" from {file_path}. Cleaning cache will cause lose of "
                f"the personal data!!{WARNING_COLOR}"
            )
    os.umask(old_umask)
