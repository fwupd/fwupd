#!/usr/bin/env bash
#
# fwupd test helper script.
#
usage() {
    if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
        B="\033[1m"
        R="\033[0m"
    else
        B=""
        R=""
    fi
    echo -e "${B}Usage: $(basename "$0") [OPTIONS]${R}"
    echo -e ""
    echo -e "Test this build of fwupd"
    echo -e ""
    echo -e "${B}OPTIONS:${R}"
    echo -e "    ${B}--tests=TESTS${R}"
    echo -e "        Select subset of tests to run:"
    echo -e "            ${B}all${R}           run all tests (default)"
    echo -e "            ${B}fwupd${R}         run the fwupd tests"
    echo -e "            ${B}fwupdtool${R}     run the fwupdtool tests"
    echo -e "            ${B}meson${R}         run the meson test suite"
    echo -e "            ${B}mtd${R}           run the mtd self-test"
    echo -e ""
    echo -e "    ${B}--meson-args=\"arg1[ ...]\"${R}"
    echo -e "    ${B}--meson-args arg1 [...]${R}"
    echo -e "        Pass arguments to the meson test command."
    echo -e "        Only applies if meson is in the selected tests."
    echo -e "        If given as --meson-args (without '=') all further arguments"
    echo -e "        are passed as-is to meson test."
}

# Print a message with a background color
print_msg() {
    colorname="$1"
    msg="$2"

    if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
        reset="\033[0m"
        pad="               "
        larrow="\xee\x82\xb2" # 
        rarrow="\xee\x82\xb0" # 
        case "$colorname" in
        pink)
            rgb="239;177;246" # #efb1f6
            ;;
        blue)
            rgb="00;215;255" # #00d7ff
            ;;
        green)
            rgb="00;255;175" # #00ffaf
            ;;
        yellow)
            rgb="255;215;00" # #ffd700
            ;;
        *)
            echo "Unsupported color '$colorname'" >&2
            exit 1
            ;;
        esac

        # Escape sequences:
        # \033[38;2;r;g;bm  - foreground truecolor r,g,b (decimal)
        # \033[48;2;r;g;bm  - background truecolor r,g,b (decimal)

        fg="\033[38;2;0;0;0m"
        # shellcheck disable=SC2059
        echo -e "\033[38;2;${rgb}m${larrow}\033[48;2;${rgb}m${fg}${pad}${msg}${pad}${reset}\033[38;2;${rgb}m${rarrow}${reset}\n"
    else
        echo "${msg}"
    fi
}

set -e

run_fwupd=false
run_fwupdtool=false
run_meson=false
run_mtd=false
run_all=true
meson_args=()

while [ $# -gt 0 ]; do
    case "$1" in
    --tests=*)
        tests="${1#--tests=}"
        run_all=false
        old_IFS="$IFS"
        IFS=','
        for t in $tests; do
            case "$t" in
            all)
                run_all=true
                ;;
            fwupd)
                run_fwupd=true
                ;;
            fwupdtool)
                run_fwupdtool=true
                ;;
            mtd)
                run_mtd=true
                ;;
            meson)
                run_meson=true
                ;;
            *)
                echo "Unknown test name: '$t'" >&2
                exit 1
                ;;
            esac
        done
        IFS="$old_IFS"
        shift
        ;;
    --meson-args=*)
        read -ra meson_args <<<"${1#--meson-args=}"
        shift
        ;;
    --meson-args)
        shift
        # Everything after --meson-args is passed through to meson test
        meson_args=("$@")
        shift $#
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown option: $1" >&2
        exit 1
        ;;
    esac
done

if [ "$run_all" = true ]; then
    run_fwupd=true
    run_fwupdtool=true
    run_meson=true
    run_mtd=true
fi

# shellcheck disable=SC1091
. "$(dirname "$(readlink -f "$0")")/nix.sh"

VENV="$(dirname "$0")/.."
BUILD="${VENV}/build"
INSTALLED_TESTS="${VENV}/dist/share/installed-tests/fwupd"
SUDO=$(command -v sudo 2>/dev/null)
export G_TEST_BUILDDIR="${INSTALLED_TESTS}"
export G_TEST_SRCDIR="${INSTALLED_TESTS}"
export GI_TYPELIB_PATH="${BUILD}/libfwupd"
export LD_LIBRARY_PATH="${BUILD}/libfwupd"
export DAEMON_BUILDDIR="${BUILD}/src"
export PATH="${VENV}/bin:$PATH"
export PYTHONWARNINGS="ignore::DeprecationWarning:gi.events"

if [ "$run_meson" = true ]; then
    print_msg "green" "Testing meson test suite"
    meson test -C "${BUILD}" "${meson_args[@]}"
fi

if [ "$run_mtd" = true ]; then
    print_msg "blue" "Testing mtd-self-test"
    "${SUDO}" modprobe mtdram
    "${SUDO}" \
        G_TEST_BUILDDIR="${G_TEST_BUILDDIR}" \
        LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
        G_TEST_SRCDIR="${G_TEST_SRCDIR}" \
        "${VENV}"/dist/libexec/installed-tests/fwupd/mtd-self-test
fi

if [ "$run_fwupdtool" = true ]; then
    print_msg "yellow" "Testing fwupdtool.sh"
    "${INSTALLED_TESTS}"/fwupdtool.sh

    # artifacts from the test run
    rm -f fwupdtool.txt
fi

if [ "$run_fwupd" = true ]; then
    print_msg "pink" "Starting daemon"
    G_DEBUG=fatal-criticals "${VENV}"/bin/fwupd --verbose --no-timestamp >fwupd.txt 2>&1 &

    print_msg "pink" "Testing fwupd.sh"
    "${INSTALLED_TESTS}"/fwupd.sh

    # artifacts from the test run
    rm -f fwupd.txt
fi
