# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'setup' subcommand — deps, venv, hooks, vscode, git

import argparse
import os
import shutil
import stat
import sys
from pathlib import Path

from .dependencies import PIP_PACKAGES, Dependencies
from .directories import directories
from .logger import Yes, logger, printer
from .meson import Meson
from .osprofile import (
    OsArch,
    OsName,
    PipPackageManager,
    RunCmd,
    Shell,
    UnknownOsException,
)


def register(subparsers):
    """Register the 'setup' subcommand with its sub-subcommands."""
    setup_parser = subparsers.add_parser(
        "setup",
        help="set up development environment",
        description=(
            "For most use-cases it is not required to run this command, use the init command instead. "
        ),
    )
    setup_sub = setup_parser.add_subparsers(dest="setup_command", help="setup steps")

    deps_parser = setup_sub.add_parser("deps", help="install build dependencies")
    deps_parser.add_argument(
        "--os",
        dest="os_name",
        choices=list(OsName),
        default=None,
        help="OS profile to use",
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


def _setup_help(parser, args) -> int:
    """Print help when 'setup' is called without a sub-subcommand."""
    if args.setup_command is None:
        parser.print_help()
        return 1
    return 0


def generate_fwupbuild_completions(completion_dir: Path, python: Path) -> None:
    """Generate and save shell completions for fwupbuild."""
    repo_root = directories.repository_root()
    completion_env = os.environ.copy()
    completion_env["PYTHONPATH"] = str(repo_root / "contrib")

    completion_dir.mkdir(exist_ok=True)
    for shell in Shell:
        completion_file = completion_dir / f"fwupbuild.{shell}"
        cmd = RunCmd(
            [python, "-m", "fwupbuild", "--print-completion", str(shell)],
            capture=True,
            env=completion_env,
        )
        if cmd.success and cmd.stdout:
            completion_file.write_text(cmd.stdout, encoding="utf-8")
            logger.info(f"Generated {shell} completion: {completion_file}")
        else:
            logger.debug(f"Failed to generate {shell} completion")


def setup_system_deps(osname: OsName) -> int:
    """Install build dependencies."""

    if osname == OsName.NIXOS:
        printer.message("NixOS detected, using nix-shell for build dependencies")
        return 0

    dependencies = Dependencies.load()
    filter = OsArch.detect().value
    packages = dependencies.find_packages(osname, filter=filter)
    if not packages:
        logger.warning(f"No packages found for {osname}")
        return 0

    if not printer.ask_yn(
        f"Install {len(packages)} build dependencies for {osname}? (y/N) "
    ):
        logger.info("Skipping package install")
        return 0

    printer.message(f"Installing packages for {osname}")
    try:
        pm = osname.package_manager()
        cmd = pm.install([p.package_name for p in packages])
        if not cmd.success:
            printer.error("Failed to install packages")

        return cmd.returncode
    except UnknownOsException:
        osnames = " ".join(f"'{p}'" for p in OsName)
        printer.error(
            f"Could not detect OS profile. Use --os to specify one of '{osnames}'.",
        )
        sys.exit(1)


def setup_python_deps(venv_python: Path) -> int:
    """Install Python packages from PIP_PACKAGES into the venv."""
    pip = PipPackageManager.new(venv_python)
    for package, version in PIP_PACKAGES.items():
        cmd = pip.install_package(package, version)
        if cmd is None:
            continue
        logger.debug(f"Installing {package} via pip")
        if not cmd.success:
            printer.error(f"Failed to install {package} via pip")
            return cmd.returncode
    return 0


def run_deps(args) -> int:
    """Install build dependencies."""

    try:
        if args.os_name:
            osname = OsName.from_string(args.os_name)
        else:
            osname = OsName.detect()
    except UnknownOsException:
        osnames = " ".join(f"'{p}'" for p in OsName)
        printer.error(
            f"Could not detect OS name. Use --os to specify one of {osnames}."
        )
        return 1

    logger.info(f"Using OS name: '{osname}'")

    if osname == OsName.NIXOS:
        printer.message("NixOS detected, using nix-shell for build dependencies")
        return 0

    printer.default_yn_answer = Yes.YES
    return setup_system_deps(osname)


def setup_venv() -> int:
    """Create virtual environment and install Python dependencies."""

    build_root = directories.build_root()
    if build_root.is_dir():
        logger.info(f"Virtual environment already exists at {build_root}")
    else:
        printer.message(f"Setting up virtualenv in {build_root}")
        if shutil.which("virtualenv"):
            cmd = RunCmd(
                [
                    "virtualenv",
                    "--system-site-packages",
                    build_root,
                    "--prompt",
                    "fwupd",
                ]
            )
        else:
            logger.debug("virtualenv not found, using python3 -m venv")
            cmd = RunCmd(
                [
                    sys.executable,
                    "-m",
                    "venv",
                    build_root,
                    "--system-site-packages",
                    "--prompt",
                    "fwupd",
                ],
            )
        if not cmd.success:
            printer.error("Failed to set up virtualenv")
            return 1

    fwupbuild_wrapper = directories.repository_root() / "fwupbuild"
    fwupbuild_link = build_root / "bin" / "fwupbuild"
    if fwupbuild_link.exists() or fwupbuild_link.is_symlink():
        fwupbuild_link.unlink()
    fwupbuild_link.symlink_to(fwupbuild_wrapper)
    logger.info(f"Created symlink: {fwupbuild_link}")

    wrapper_dir = (build_root / "bin").resolve()

    def create_run_wrapper(binary):
        """Create a small wrapper script in venv/bin/ that calls fwupbuild run."""
        wrapper_path = wrapper_dir / binary
        content = (
            f"""#!/bin/bash\nexec "$(dirname "$0")/fwupbuild" run {binary} -- "$@" """
        )
        wrapper_path.write_text(content)
        wrapper_path.chmod(
            wrapper_path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH
        )
        logger.info(f"Created wrapper: {wrapper_path}")

    for binary in ("fwupdtool", "fwupdmgr", "fwupdtui", "fwupd"):
        create_run_wrapper(binary)

    activate = wrapper_dir / "activate"
    if not activate.exists():
        return 0

    marker = "# fwupbuild additions"

    # for each shell we have additions for, create a fwupbuild.<shellname>
    # script that we can source from the activate script

    shell_configs = {
        Shell.BASH: """
echo "To build or rebuild fwupd within development environment run:"
echo ""
echo "# fwupbuild build"
echo ""
echo "To run the test suite run:"
echo ""
echo "# fwupbuild test"
echo ""
echo "To run any tool under gdbserver add DEBUG=1 to env, for example:"
echo ""
echo "# DEBUG=1 fwupdtool get-devices"
echo ""
echo "To leave fwupd development environment run:"
echo ""
echo "# deactivate"

if [ -n "$BASH_VERSION" ]; then
    . "{completion_dir}/fwupbuild.bash" 2>/dev/null || true
    . data/bash-completion/fwupdtool 2>/dev/null || true
    . data/bash-completion/fwupdmgr 2>/dev/null || true
fi
if [ -n "$ZSH_VERSION" ]; then
    fwupbuild_zsh="{completion_dir}/fwupbuild.zsh"
    [[ -f "$fwupbuild_zsh" ]] && eval "$(<"$fwupbuild_zsh")" 2>/dev/null || true
fi
export MANPATH=${{VIRTUAL_ENV}}/dist/share/man:
""",
        Shell.FISH: """
set -gx MANPATH $VIRTUAL_ENV/dist/share/man: $MANPATH

. "{completion_dir}/fwupbuild.fish" 2>/dev/null || true
set -g fish_complete_path $VIRTUAL_ENV/share/fish/vendor_completions.d $fish_complete_path
""",
    }

    completion_dir = (build_root / "completion").resolve()

    for shell, content in shell_configs.items():
        activate_path = shell.venv_activate_script(venv_root=wrapper_dir)
        if not activate_path.exists():
            continue

        fwupbuild_script = wrapper_dir / f"fwupbuild.{shell}"
        activate_content = activate_path.read_text()
        if marker not in activate_content:
            # The venv activate script simply sources our fwupbuild-specific script that contains
            # the actual data.
            with activate_path.open("a", encoding="utf-8") as f:
                source_command = shell.source_command(
                    fwupbuild_script, ignore_errors=True
                )
                f.write(f"\n{marker}\n{source_command}\n")

        # We always overwrite our custom fwupbuild.<shell> script with newest data
        with fwupbuild_script.open("w") as f:
            content = content.format(completion_dir=completion_dir)
            f.write("# This file is generated and will be overwritten, do not edit\n")
            f.write(content)

        logger.info(f"Augmented {activate_path} with usage instructions and completion")

    # Install required Python packages using the venv's pip
    venv_python = build_root / "bin" / "python3"
    if venv_python.exists():
        rc = setup_python_deps(venv_python)
        if rc != 0:
            return rc

        generate_fwupbuild_completions(completion_dir, venv_python)

    # meson
    repo_dir = directories.repository_root()
    min_vers = Meson.get_minimum_meson_version(repo_dir / "meson.build")
    logger.debug(f"Verifying minimum meson min version {min_vers}")
    cmd = RunCmd(
        [
            venv_python,
            "-c",
            (
                "from fwupbuild.meson import Meson, MesonVersion; "
                f"minimum = MesonVersion.from_string('{min_vers}'); "
                "current = Meson.current_meson_version(); "
                "assert minimum <= current, f'Required meson version not met'"
            ),
        ],
        capture=True,
        env={
            **os.environ,
            "PYTHONPATH": str(directories.repository_root() / "contrib"),
        },
    )
    if not cmd.success:
        printer.error("Minimum version of meson not met")
        return 1

    return 0


def run_venv(args) -> int:
    return setup_venv()


def setup_hooks(pre_push_hooks: bool) -> int:
    """Set up pre-commit hooks."""
    if os.environ.get("CI"):
        logger.info("Skipping hook setup in CI")
        return 0

    root = directories.repository_root()

    if not shutil.which("pre-commit"):
        pip = PipPackageManager.new(python=directories.python())
        cmd = pip.install_package("pre-commit")
        if cmd is not None and not cmd.success:
            printer.error("Failed to install pre-commit via pip")
            return cmd.returncode

    logger.info("Configuring pre-commit hooks")
    cmd = RunCmd(["pre-commit", "install"], capture=True, cwd=root)
    if cmd is not None and not cmd.success:
        printer.error("Failed to install pre-commit hooks")
        return cmd.returncode

    if pre_push_hooks:
        logger.info("Installing pre-push test hooks")
        cmd = RunCmd(
            ["pre-commit", "install", "-t", "pre-push"], capture=True, cwd=root
        )
        if cmd is not None and not cmd.success:
            printer.error("Failed to install pre-commit pre-push hooks")
            return cmd.returncode

    return 0


def run_hooks(args) -> int:
    return setup_hooks(args.pre_push_hooks)


def setup_vscode() -> int:
    """Copy VS Code settings."""

    root = directories.repository_root()

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


def run_vscode(args) -> int:
    return setup_vscode()


def setup_git() -> int:
    """Configure git environment."""
    if os.environ.get("CI"):
        logger.info("Skipping git config in CI")
        return 0

    root = directories.repository_root()

    logger.info("Configuring git environment")
    cmd = RunCmd(
        ["git", "config", "blame.ignoreRevsFile", ".git-blame-ignore-revs"],
        check=False,
        capture=True,
        cwd=root,
    )
    if not cmd.success:
        printer.error("Failed to configure Git")
        return cmd.returncode

    return 0


def run_git(args) -> int:
    return setup_git()
