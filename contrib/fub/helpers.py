# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Shared utilities for fub

import logging
import os
import subprocess
import sys
import xml.etree.ElementTree as etree
from pathlib import Path

MARKERFILE = "fwupd.venv"

# Respect NO_COLOR (https://no-color.org) and non-tty output
_USE_COLOR = sys.stderr.isatty() and "NO_COLOR" not in os.environ

# Colors for banners (work on both dark and light terminals)
COLORS = {
    "green": "0;255;175",  # #00ffaf
    "blue": "0;215;255",  # #00d7ff
    "yellow": "255;215;0",  # #ffd700
    "pink": "239;177;246",  # #efb1f6
}

BOLD = "\033[1m" if _USE_COLOR else ""
ENDC = "\033[0m" if _USE_COLOR else ""
LEVEL_COLORS = (
    {
        logging.DEBUG: "\033[94m",  # blue
        logging.INFO: "\033[92m",  # green
        logging.WARNING: "\033[93m",  # yellow
        logging.ERROR: "\033[91m",  # red
        logging.CRITICAL: "\033[91m",  # red
    }
    if _USE_COLOR
    else {}
)


class ColorFormatter(logging.Formatter):
    def format(self, record):
        message = super().format(record)
        color = LEVEL_COLORS.get(record.levelno, "")
        if color:
            return f"{color}{record.levelname}:{ENDC} {message}"
        return f"{record.levelname}: {message}"


logger = logging.getLogger("fub")

# Minimum version of markdown required
MINIMUM_MARKDOWN = (3, 2, 0)

OS_PROFILES = [
    "fedora",
    "centos",
    "debian",
    "ubuntu",
    "arch",
    "darwin",
    "freebsd",
    "nixos",
]

# Translate debian architecture names to the naming in dependencies.xml
ARCH_TO_DEPS_MAP = {
    "amd64": "x86_64",
    "arm": "armhf",
    "arm64": "aarch64",
    "i386": "i386",
    "s390x": "s390x",
}


def ask(args, question: str) -> bool:
    yes = getattr(args, "yes", None)
    if yes is None:
        try:
            answer = input(f"{question} ")
        except EOFError:
            answer = "n"
    else:
        answer = "y" if yes else "n"

    return answer.lower() == "y"


def msg(args, message):
    if not args.quiet:
        message = message.replace("[bold]", BOLD).replace("[/bold]", ENDC)
        print(message)


def error(args, message):
    message = message.replace("[bold]", BOLD).replace("[/bold]", ENDC)
    print(message, file=sys.stderr)


def banner(args, colorname, text):
    """Print a colored section header if stdout is a tty."""
    if sys.stdout.isatty() and not os.environ.get("NO_COLOR"):
        rgb = COLORS.get(colorname, "255;255;255")
        reset = "\033[0m"
        fg = "\033[38;2;0;0;0m"
        pad = "               "
        # Nerd font arrows: \ue0b2 () and \ue0b0 ()
        larrow = "\ue0b2"
        rarrow = "\ue0b0"
        print(
            f"\033[38;2;{rgb}m{larrow}\033[48;2;{rgb}m{fg}"
            f"{pad}{text}{pad}"
            f"{reset}\033[38;2;{rgb}m{rarrow}{reset}\n"
        )
    else:
        msg(args, text)


def find_repo_root() -> Path:
    """Find the first parent directory containing meson_options.txt"""
    parent = Path(__file__).absolute().parent
    while parent != parent.parent:
        if (parent / "meson_options.txt").exists():
            return parent
        parent = parent.parent

    raise FileNotFoundError("Unable to find repository root")


def find_most_recent_venv() -> Path | None:
    """Find the most recently created venv by looking for the marker file.

    Returns the directory containing the most recent marker file, or
    None if no marker file is found.
    """
    root = find_repo_root()
    candidates = sorted(root.rglob(MARKERFILE), key=lambda p: p.stat().st_mtime)
    if candidates:
        return candidates[-1].parent
    return None


def venv_path(args=None) -> Path:
    """Return the path to the venv directory.

    If args is given and args.directory is set, use that directory.
    Otherwise search for the most recently created venv by looking
    for the marker file. If no marker file is found, fall back to
    <repo_root>/venv.
    """
    directory = getattr(args, "directory", None) if args else None
    if directory is not None:
        return Path(directory)

    found = find_most_recent_venv()
    if found is not None:
        return found

    return find_repo_root() / "venv"


def build_path(args=None) -> Path:
    """Return the path to the meson build directory."""
    return venv_path(args) / "build"


def dist_path(args=None) -> Path:
    """Return the path to the meson install prefix."""
    return venv_path(args) / "dist"


class RunCmd:
    def __init__(
        self,
        args,
        *,
        check: bool = False,
        capture: bool = True,
        text: bool = True,
        **kwargs,
    ):
        args = [str(a) for a in args]
        logger.debug(f'Running: {" ".join(args)}')
        if capture:
            kwargs["stdout"] = subprocess.PIPE
            kwargs["stderr"] = subprocess.PIPE
        if "cwd" in kwargs and not isinstance(kwargs["cwd"], str):
            kwargs["cwd"] = str(kwargs["cwd"])

        self.p = subprocess.run(args, check=check, text=text, **kwargs)
        if capture:
            self.log_stdout()
            self.log_stderr()

    def log_stdout(self):
        if self.p.stdout:
            for line in self.p.stdout.split("\n"):
                logger.debug(line)

    def log_stderr(self):
        if self.p.stderr:
            if self.p.returncode != 0:
                logfunc = logger.error
            else:
                logfunc = logger.debug
            for line in self.p.stderr.split("\n"):
                logfunc(line)

    @property
    def stdout(self) -> str:
        return self.p.stdout

    @property
    def stderr(self) -> str:
        return self.p.stderr

    @property
    def returncode(self) -> int:
        return self.p.returncode

    @property
    def success(self) -> bool:
        return self.p.returncode == 0


def detect_os():
    """Detect the OS profile.

    Returns one of the OS_PROFILES strings, or empty string on failure.
    """
    # macOS
    if (Path("/") / "Library" / "Apple").exists():
        return "darwin"

    # Try the distro module first
    try:
        import distro

        target = distro.id()
        if target == "rhel":
            return "centos"
        if target not in OS_PROFILES:
            target = distro.like()
        return target
    except ModuleNotFoundError:
        pass

    # Fall back to /etc/os-release
    os_release = Path("/etc/os-release")
    try:
        with os_release.open() as f:
            for line in f:
                line = line.strip()
                if line.startswith("ID="):
                    target = line[3:].strip('"')
                    if target == "rhel":
                        return "centos"
                    return target
    except FileNotFoundError:
        pass

    return ""


def get_variant():
    """Get the machine variant (architecture)."""
    variant = os.uname().machine
    return ARCH_TO_DEPS_MAP.get(variant, variant)


def parse_dependencies(os_profile, variant):
    """Parse contrib/ci/dependencies.xml for build dependencies.

    Returns a list of package names for the given OS profile.
    """
    deps_file = find_repo_root() / "contrib" / "ci" / "dependencies.xml"
    tree = etree.parse(deps_file)
    root = tree.getroot()

    deps = []
    for child in root:
        if "id" not in child.attrib:
            continue
        for distro_elem in child:
            if "id" not in distro_elem.attrib:
                continue
            if distro_elem.attrib["id"] != os_profile:
                continue

            if len(distro_elem.findall("package")) == 0:
                dep = child.attrib["id"]
                if dep:
                    deps.append(dep)
            for package in distro_elem.findall("package"):
                if variant and "variant" in package.attrib:
                    if package.attrib["variant"] != variant:
                        continue
                dep = package.text if package.text else child.attrib["id"]
                if dep:
                    deps.append(dep)

    return deps


def get_installer_cmd(args, profile):
    """Get the package manager install command for the given profile."""
    match profile:
        case "darwin":
            return ["brew", "install"]
        case "debian" | "ubuntu":
            installer = ["apt-get", "install", "-q"]
        case "fedora" | "centos":
            installer = ["dnf", "install"]
        case "arch":
            installer = ["pacman", "-Syu", "--noconfirm", "--needed"]
        case "freebsd":
            installer = ["pkg", "install"]
        case _:
            profiles = " ".join(OS_PROFILES)
            error(
                args,
                f"Could not detect OS profile. Use --os to specify one of '{profiles}'.",
            )
            sys.exit(1)

    if os.geteuid() != 0:
        logger.info("Using sudo to install packages")
        installer.insert(0, "sudo")
    installer.append("-y")
    return installer


def install_packages(args, profile, packages):
    """Install packages using the OS package manager.

    Args:
        profile: OS profile string.
        packages: List of package names.
    """
    if profile == "nixos":
        return

    installer = get_installer_cmd(args, profile)
    cmd = installer + list(packages)
    cmd = RunCmd(cmd)
    if not cmd.success:
        error(args, "Failed to install packages")
        sys.exit(1)


def ensure_pip_package(args, name, min_version=None):
    """Install a pip package if it's not importable or too old.

    Args:
        name: The pip package name (also used as the import name).
        min_version: Optional minimum version tuple, e.g. (3, 2, 0).
    """
    try:
        mod = __import__(name)
        if min_version is not None:
            version_info = getattr(mod, "__version_info__", None)
            if version_info is not None and version_info >= min_version:
                return
            # version too old, fall through to install
        else:
            return
    except ModuleNotFoundError:
        pass

    logger.info(f"Installing/upgrading {name} via pip")
    pip_install_package(args, name)


def pip_install_package(args, name):
    """Install a pip package."""
    env = os.environ.copy()
    env["PIP_BREAK_SYSTEM_PACKAGES"] = "1"
    result = RunCmd(["python3", "-m", "pip", "install", "--upgrade", name], env=env)
    if not result.success:
        error(args, f"Failed to install {name}")
        sys.exit(1)


def cc_machine():
    """Run cc -dumpmachine and return the result."""
    result = RunCmd(["cc", "-dumpmachine"])
    if result.success:
        return result.stdout.strip()
    return ""


def get_minimum_meson_version():
    """Parse the minimum meson version from the top-level meson.build."""
    import re

    meson_build = find_repo_root() / "meson.build"
    with meson_build.open() as f:
        for line in f:
            if "meson_version" in line:
                m = re.search(r"(\d+\.\d+\.\d+)", line)
                if m:
                    return m.group(1)
    return None


def _meson_version_tuple(ver):
    """Convert a meson version string to a comparable tuple.

    Handles release candidates: x.y.z.rcN or x.y.zrcN (PEP440).
    """
    import re

    m = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:[.]?rc(\d+))?$", ver)
    if not m:
        raise ValueError(f"Unknown meson version format: '{ver}'")
    major, minor, micro = int(m[1]), int(m[2]), int(m[3])
    if m[4] is not None:
        return (major, minor, micro, -1, int(m[4]))
    return (major, minor, micro, 0, 0)


def ensure_meson():
    """Check that meson is installed and new enough, install via pip if not."""
    from importlib.metadata import version, PackageNotFoundError

    minimum = get_minimum_meson_version()
    if minimum is None:
        return

    try:
        new_enough = _meson_version_tuple(version("meson")) >= _meson_version_tuple(
            minimum
        )
    except PackageNotFoundError:
        result = RunCmd(["meson", "--version"])
        if result.success:
            ver = result.stdout.strip()
            new_enough = _meson_version_tuple(ver) >= _meson_version_tuple(minimum)
        else:
            new_enough = False

    if not new_enough:
        logger.info("meson must be installed/upgraded")
        pip_install_package(None, "meson")


def require_venv(args):
    """Check that the venv exists, exit with an error if not."""
    venv = venv_path(args)
    if not venv.is_dir():
        error(args, "Development environment not set up yet.")
        error(args, "Run: fub init")
        sys.exit(1)


def require_build(args):
    """Check that the project has been built, exit with an error if not."""
    require_venv(args)
    build = build_path(args)
    if not (build / "build.ninja").exists():
        error(args, "Project not yet built.")
        error(args, "Run: fub build")
        sys.exit(1)
