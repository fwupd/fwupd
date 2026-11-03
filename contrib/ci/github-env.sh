#!/bin/sh

# Copyright 2026 Red Hat
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Source this script to emulate a GitHub Actions CI environment locally.
# This is useful for manually testing scripts that are usually run in the CI.
#
# Placeholder values are used where real values aren't applicable.
#   . contrib/ci/github-env.sh

export CI=true
export GITHUB_ACTIONS=true

# Workflow / run identity
export GITHUB_ACTION="__local_emulate"
export GITHUB_ACTION_PATH=""
export GITHUB_ACTION_REPOSITORY=""
export GITHUB_WORKFLOW="local-emulate"
export GITHUB_WORKFLOW_REF="owner/fwupd/.github/workflows/local-emulate.yml@refs/heads/main"
export GITHUB_WORKFLOW_SHA="0000000000000000000000000000000000000000"
export GITHUB_JOB="build"
export GITHUB_RUN_ID="1000000000"
export GITHUB_RUN_NUMBER="1"
export GITHUB_RUN_ATTEMPT="1"
export GITHUB_RETENTION_DAYS="90"

# Actor / triggering identity
export GITHUB_ACTOR="${USER:-octocat}"
export GITHUB_ACTOR_ID="1000000"
export GITHUB_TRIGGERING_ACTOR="${USER:-octocat}"

# Repository identity
export GITHUB_REPOSITORY="owner/fwupd"
export GITHUB_REPOSITORY_ID="123456789"
export GITHUB_REPOSITORY_OWNER="owner"
export GITHUB_REPOSITORY_OWNER_ID="987654321"

# Git ref / event
export GITHUB_EVENT_NAME="push"
export GITHUB_REF="refs/heads/main"
export GITHUB_REF_NAME="main"
export GITHUB_REF_TYPE="branch"
export GITHUB_REF_PROTECTED="false"
_sha=$(git rev-parse HEAD 2>/dev/null || echo '0000000000000000000000000000000000000000')
export GITHUB_SHA="$_sha"
export GITHUB_BASE_REF=""
export GITHUB_HEAD_REF=""

# URLs
export GITHUB_SERVER_URL="https://github.com"
export GITHUB_API_URL="https://api.github.com"
export GITHUB_GRAPHQL_URL="https://api.github.com/graphql"

# Workspace
export GITHUB_WORKSPACE="${PWD}"

# File-based workflow commands — backed by real temp files so writes don't fail
_gh_tmpdir="$(mktemp -d /tmp/github-env-XXXXXX)"
export GITHUB_ENV="${_gh_tmpdir}/env"
export GITHUB_OUTPUT="${_gh_tmpdir}/output"
export GITHUB_PATH="${_gh_tmpdir}/path"
export GITHUB_STEP_SUMMARY="${_gh_tmpdir}/step_summary"
export GITHUB_EVENT_PATH="${_gh_tmpdir}/event.json"
export GITHUB_ARTIFACTS="${_gh_tmpdir}/artifacts"
export GITHUB_ARTIFACTS_LIST="${_gh_tmpdir}/artifacts_list.json"
touch "${GITHUB_ENV}" "${GITHUB_OUTPUT}" "${GITHUB_PATH}" \
    "${GITHUB_STEP_SUMMARY}" "${GITHUB_ARTIFACTS}"
printf '{}' >"${GITHUB_EVENT_PATH}"
printf '[]' >"${GITHUB_ARTIFACTS_LIST}"
unset _gh_tmpdir

# Runner
export RUNNER_OS="Linux"
export RUNNER_ARCH="X64"
export RUNNER_NAME="local-runner"
export RUNNER_ENVIRONMENT="self-hosted"
_runner_tmpdir="$(mktemp -d /tmp/runner-temp-XXXXXX)"
export RUNNER_TEMP="$_runner_tmpdir"
export RUNNER_TOOL_CACHE="/opt/hostedtoolcache"
export RUNNER_DEBUG=""

# This one is ours but it's likely required whenever
# we source this script for debugging, so let's set it
# by default.
export CI_NETWORK=true

echo "GitHub env set up for:"
echo "    CI=${CI}"
echo "    CI_NETWORK={$CI_NETWORK}"
echo "    GITHUB_WORKSPACE=${GITHUB_WORKSPACE}"
echo "    GITHUB_SHA=${GITHUB_SHA}"
echo "    GITHUB_OUTPUT=${GITHUB_OUTPUT}"
echo "    GITHUB_EVENT_NAME=${GITHUB_EVENT_NAME}"
echo "    GITHUB_REF=${GITHUB_REF}"
echo "    GITHUB_REF_NAME=${GITHUB_REF_NAME}"
echo "    GITHUB_REF_TYPE=${GITHUB_REF_TYPE}"
echo ""
echo "unset CI_NETWORK if running without network"
