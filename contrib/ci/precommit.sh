#!/bin/sh -e
# disable the safe directory feature
git config --global safe.directory "*"
SKIP=no-commit-to-branch pre-commit run -v --show-diff-on-failure --hook-stage commit --all-files
