# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'check-abi' subcommand — check for ABI incompatibilities

from pathlib import Path

from .helpers import (
    find_repo_root,
    error,
    logger,
    RunCmd,
)


def register(subparsers):
    """Register the 'observe' subcommand."""

    parser = subparsers.add_parser(
        "observe",
        help="Observe the system while running other commands",
    )

    subparser = parser.add_subparsers(dest="observee", help="")

    cpu_parser = subparser.add_parser("cpu", help="Observe CPU usage")
    cpu_parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="CPU limit in Mcycles",
    )
    cpu_parser.add_argument(
        "command",
        nargs="+",
        help="the command to run and observe",
    )

    rss_parser = subparser.add_parser("rss", help="Observe RSS usage")
    rss_parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="RSS limit in kB",
    )
    rss_parser.add_argument(
        "command",
        nargs="+",
        help="the command to run and observe",
    )

    parser.set_defaults(func=run)


def run(args):
    cwd = find_repo_root()

    logger.debug("Priming the cache")
    cmd = RunCmd(args.command, cwd=cwd)
    if not cmd.success:
        error(args, f"Failed to run $ {' '.join(args.command)}")
        return 1

    valgrind = ["valgrind"]
    if args.observee == "cpu":
        valgrind += ["--tool=callgrind"]

    cmd = RunCmd(valgrind + args.command, cwd=cwd)
    if not cmd.success:
        error(args, f"Failed to run $ {' '.join(args.command)}")
        return 1

    match args.observee:
        case "cpu":
            value: int = 0
            for line in cmd.stderr.splitlines():
                if line.find("Collected : ") != -1:
                    value = int(line.split(" ")[3])
            if args.limit > 0 and value > args.limit * 1_000_000:
                print(
                    f"CPU usage was {value//1_000_000}Mcycles (limit of {args.limit}Mcycles)"
                )
                return 1

            print(f"CPU usage was {value//1_000_000}Mcycles")
        case "rss":
            value: int = 0
            for line in cmd.stderr.split("\n"):
                if line.find("in use at exit: ") != -1:
                    value = int(line.split(" ")[9].strip().replace(",", ""))
            if args.limit and value > args.limit * 1024:
                print(f"RSS usage was {value//1024}kB (limit of {args.limit}kB)")
                return 1
            print(f"RSS usage was {value//1024}kB")
    return 0
