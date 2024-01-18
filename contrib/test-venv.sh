#!/bin/sh -e

VENV=$(dirname $0)/..
BUILD=${VENV}/build

ninja -C ${BUILD} test
