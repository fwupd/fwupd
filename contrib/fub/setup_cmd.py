# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'setup' subcommand — deps, venv, hooks, vscode, git

import argparse
import ast
import os
import shutil
import stat
import sys

from .helpers import (
    ask,
    MINIMUM_MARKDOWN,
    OS_PROFILES,
    detect_os,
    find_repo_root,
    get_variant,
    install_packages,
    logger,
    parse_dependencies,
    pip_install_package,
    venv_path,
    msg,
    error,
    RunCmd,
)


def register(subparsers):
    """Register the 'setup' subcommand with its sub-subcommands."""
    setup_parser = subparsers.add_parser("setup", help="set up development environment")
    setup_sub = setup_parser.add_subparsers(dest="setup_command", help="setup steps")

    deps_parser = setup_sub.add_parser("deps", help="install build dependencies")
    deps_parser.add_argument(
        "--os",
        dest="os_profile",
        choices=OS_PROFILES,
        default=None,
        help="OS profile to use",
    )
    deps_parser.add_argument(
        "-y", "--yes", action="store_true", default=True, help=argparse.SUPPRESS
    )
    # We don't need --no here because why would you run a setup command if --no...
    deps_parser.set_defaults(func=run_deps)

    venv_parser = setup_sub.add_parser(
        "venv", help="create virtual environment and install Python dependencies"
    )
    venv_parser.set_defaults(func=run_venv)

    hooks_parser = setup_sub.add_parser("hooks", help="set up pre-commit hooks")
    hooks_parser.add_argument(
        "--pre-push-hooks",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="whether to install pre-push test hooks (default: no)",
    )
    hooks_parser.set_defaults(func=run_hooks)

    vscode_parser = setup_sub.add_parser("vscode", help="copy VS Code settings")
    vscode_parser.set_defaults(func=run_vscode)

    git_parser = setup_sub.add_parser("git", help="configure git environment")
    git_parser.set_defaults(func=run_git)

    setup_parser.set_defaults(func=lambda args: _setup_help(setup_parser, args))


def _setup_help(parser, args):
    """Print help when 'setup' is called without a sub-subcommand."""
    if args.setup_command is None:
        parser.print_help()
        return 1
    return 0


def run_deps(args):
    """Install build dependencies."""
    if os.environ.get("CI"):
        args.yes = True

    if not ask(args, "Install build dependencies? (y/N) "):
        return 0

    os_profile = args.os_profile
    if os_profile is None:
        os_profile = detect_os()
        if not os_profile:
            # Try installing distro module and retry
            pip_install_package(args, "distro")
            os_profile = detect_os()
        if os_profile:
            logger.info(f"Detected OS profile: {os_profile}")
        else:
            profiles = " ".join(OS_PROFILES)
            error(
                args,
                f"Could not detect OS profile. Use --os to specify one of '{profiles}'.",
            )
            return 1

    if os_profile == "nixos":
        logger.info("NixOS detected, using nix-shell for build dependencies")
        return 0

    if os_profile not in OS_PROFILES:
        plist = ", ".join(f"'{p}'" for p in OS_PROFILES)
        error(args, f"Unknown OS profile, use one of {plist}.")
        return 1

    variant = get_variant()
    packages = parse_dependencies(os_profile, variant)
    if not packages:
        logger.warning(f"No packages found for profile {os_profile}")
        return 0

    if args.yes:
        msg(args, f"Installing {len(packages)} packages for {os_profile}")
        install_packages(args, os_profile, packages)
    else:
        logger.info("Skipping package install")
    return 0


def run_venv(args):
    """Create virtual environment and install Python dependencies."""
    root = find_repo_root()
    venv = venv_path(args)

    if venv.is_dir():
        logger.info(f"Virtual environment already exists at {venv}")
    else:
        msg(args, f"Setting up virtualenv in {venv}")
        if shutil.which("virtualenv"):
            cmd = RunCmd(
                ["virtualenv", "--system-site-packages", venv, "--prompt", "fwupd"]
            )
        else:
            logger.debug("virtualenv not found, using python3 -m venv")
            cmd = RunCmd(
                [
                    sys.executable,
                    "-m",
                    "venv",
                    venv,
                    "--system-site-packages",
                    "--prompt",
                    "fwupd",
                ],
            )
        if not cmd.success:
            error(args, "Failed to set up virtualenv")
            return 1

    fub_wrapper = root / "fub"
    fub_link = venv / "bin" / "fub"
    if fub_link.exists():
        fub_link.unlink()
    fub_link.symlink_to(fub_wrapper)
    logger.info(f"Created symlink: {fub_link}")

    wrapper_dir = venv / "bin"

    def create_run_wrapper(binary):
        """Create a small wrapper script in venv/bin/ that calls fub run."""
        wrapper_path = wrapper_dir / binary
        content = f"""#!/bin/bash\nexec "$(dirname "$0")/fub" run {binary} -- "$@" """
        wrapper_path.write_text(content)
        wrapper_path.chmod(
            wrapper_path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH
        )
        logger.info(f"Created wrapper: {wrapper_path}")

    for binary in ("fwupdtool", "fwupdmgr", "fwupd"):
        create_run_wrapper(binary)

    def create_alias_wrapper(name, subcommand):
        """Create a small wrapper script in venv/bin/ that calls a fub subcommand."""
        wrapper_path = wrapper_dir / name
        content = f"""#!/bin/sh\nexec "$(dirname "$0")/fub" {subcommand} "$@" """
        wrapper_path.write_text(content)
        wrapper_path.chmod(
            wrapper_path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH
        )
        logger.info(f"Created wrapper: {wrapper_path}")

    create_alias_wrapper("build-fwupd", "build")
    create_alias_wrapper("test-fwupd", "test")

    activate = wrapper_dir / "activate"
    if not activate.exists():
        return 0

    marker = "# fub additions"
    if marker in activate.read_text():
        return 0

    additions = f"""\n{marker}
echo "To build or rebuild fwupd within development environment run:"
echo ""
echo "# fub build"
echo ""
echo "To run the test suite run:"
echo ""
echo "# fub test"
echo ""
echo "To run any tool under gdbserver add DEBUG=1 to env, for example:"
echo ""
echo "# DEBUG=1 fwupdtool get-devices"
echo ""
echo "To leave fwupd development environment run:"
echo ""
echo "# deactivate"

if [ -n "$BASH_VERSION" ]; then
    . data/bash-completion/fwupdtool 2>/dev/null || true
    . data/bash-completion/fwupdmgr 2>/dev/null || true
fi
export MANPATH=${{VIRTUAL_ENV}}/dist/share/man:
"""
    with activate.open("a") as f:
        f.write(additions)
    logger.info(f"Augmented {activate} with usage instructions")

    # Install required Python packages using the venv's pip
    venv_python = venv / "bin" / "python3"
    if venv_python.exists():
        _ensure_venv_pip_packages(venv_python)

    return 0


def _ensure_venv_pip_packages(venv_python):
    """Check and install required Python packages in the venv."""
    try:
        cmd = RunCmd(
            [venv_python, "-c", "import markdown; print(markdown.__version_info__)"],
        )
        if not cmd.success:
            raise ModuleNotFoundError()
        version_str = cmd.stdout.strip()
        if ast.literal_eval(version_str) < MINIMUM_MARKDOWN:
            raise ModuleNotFoundError()
    except (ModuleNotFoundError, ValueError):
        logger.info("Installing/upgrading markdown via pip")
        cmd = RunCmd(
            [venv_python, "-m", "pip", "install", "--upgrade", "markdown"],
        )

    cmd = RunCmd(
        [venv_python, "-c", "import jinja2"],
        capture=True,
    )
    if not cmd.success:
        logger.info("Installing jinja2 via pip")
        RunCmd(
            [venv_python, "-m", "pip", "install", "jinja2"],
            capture=True,
        )

    RunCmd(
        [
            venv_python,
            "-c",
            "from fub.helpers import ensure_meson; ensure_meson()",
        ],
        capture=True,
        env={
            **os.environ,
            "PYTHONPATH": str(find_repo_root() / "contrib"),
        },
    )


def run_hooks(args):
    """Set up pre-commit hooks."""
    if os.environ.get("CI"):
        logger.info("Skipping hook setup in CI")
        return 0

    if not shutil.which("pre-commit"):
        pip_install_package(args, "pre-commit")

    logger.info("Configuring pre-commit hooks")
    RunCmd(["pre-commit", "install"], capture=True)

    if args.pre_push_hooks:
        logger.info("Installing pre-push test hooks")
        RunCmd(["pre-commit", "install", "-t", "pre-push"], capture=True)

    return 0


def run_vscode(args):
    """Copy VS Code settings."""
    root = find_repo_root()
    source_dir = root / "contrib" / "vscode"
    target_dir = root / ".vscode"

    target_dir.mkdir(parents=True, exist_ok=True)

    for filename in ("settings.json", "launch.json", "tasks.json"):
        src = source_dir / filename
        dst = target_dir / filename
        if src.exists():
            shutil.copy2(src, dst)
            logger.info(f"Copied {src} to {dst}")
        else:
            logger.warning(f"Source file not found: {src}")

    return 0


def run_git(args):
    """Configure git environment."""
    if os.environ.get("CI"):
        logger.info("Skipping git config in CI")
        return 0

    logger.info("Configuring git environment")
    RunCmd(
        ["git", "config", "include.path", "../.gitconfig"], check=False, capture=True
    )

    return 0
