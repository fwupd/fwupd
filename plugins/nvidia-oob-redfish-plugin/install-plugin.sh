#!/usr/bin/env bash
# Copyright 2026 NVIDIA Corporation
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# install-plugin.sh -- install + authenticate the nvidia-oob-redfish fwupd plugin
#
# run this script once after first deployment and again whenever the BMC
# password changes or the plugin binary is updated; it:
#   1. locates the fwupd plugin directory and service
#   2. discovers and verifies BMC connectivity
#   3. collects BMC credentials and creates an initial Redfish session
#   4. installs the plugin binary
#   5. writes BMC config (host, user, password) to /etc/fwupd/nvidia_oob.conf
#   6. restarts fwupd so the plugin loads with the fresh session token
#   7. verifies the plugin is loaded and OOB devices are visible
#
# when the plugin runs thereafter it will re-authenticate automatically using
# the stored BmcPass whenever the session expires
#
# Usage:
#   sudo ./install-plugin.sh <libfu_plugin_nvidia_oob_redfish.so> [OPTIONS]
#
# Options:
#   --host <ip>    BMC IP or hostname (auto-discovered via USB if omitted)
#   --user <name>  BMC username (default: root)
#   --pass <pass>  BMC password (prompted interactively if omitted)
#   --insecure     Skip TLS verification (auto-selected if no CA cert present)
#
# Environment overrides (higher priority than options):
#   NVIDIA_OOB_BMC_HOST, NVIDIA_OOB_BMC_USER, NVIDIA_OOB_BMC_PASS

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

# ── logging ───────────────────────────────────────────────────────────────────

_c()        { printf '\e[%sm' "$1"; }
info()      { printf '%s[INFO ]%s  %s\n'      "$(_c '1;34')" "$(_c 0)" "$*" >&2; }
detail()    { printf '%s[INFO ]%s    ↳ %s\n'  "$(_c '34')"   "$(_c 0)" "$*" >&2; }
ok()        { printf '%s[ OK  ]%s  %s\n'      "$(_c '1;32')" "$(_c 0)" "$*" >&2; }
chk()       { printf '%s[CHECK]%s  %s\n'      "$(_c '1;36')" "$(_c 0)" "$*" >&2; }
pass()      { printf '%s[ OK  ]%s    ✔ %s\n'  "$(_c '1;32')" "$(_c 0)" "$*" >&2; }
fail()      { printf '%s[FAIL ]%s    ✘ %s\n'  "$(_c '1;31')" "$(_c 0)" "$*" >&2; }
warn()      { printf '%s[WARN ]%s  %s\n'      "$(_c '1;33')" "$(_c 0)" "$*" >&2; }
die()       { printf '%s[FATAL]%s  %s\n'      "$(_c '1;31')" "$(_c 0)" "$*" >&2; exit 1; }
sep()       { printf '%s\n' "────────────────────────────────────────────────────────" >&2; }
phase()     { printf '\n%s══ %s %s%s\n' "$(_c '1;37')" "$*" "$(printf '═%.0s' {1..40})" "$(_c 0)" >&2; }
milestone() { printf '\n%s  ★  MILESTONE: %s  ★%s\n\n' "$(_c '1;32')" "$*" "$(_c 0)" >&2; }

require() { command -v "$1" >/dev/null 2>&1 || die "Required tool not found: $1"; }

# ── argument parsing ──────────────────────────────────────────────────────────

SO_FILE=""
ARG_HOST=""
ARG_USER=""
ARG_PASS=""
ARG_INSECURE=""

usage() {
    cat >&2 <<EOF
Usage: sudo $0 <libfu_plugin_nvidia_oob_redfish.so> [OPTIONS]

  --host <ip>    BMC IP or hostname (auto-discovered via USB if omitted)
  --user <name>  BMC username (default: root)
  --pass <pass>  BMC password (prompted interactively if omitted)
  --insecure     Skip TLS verification (auto-selected if no CA cert)
EOF
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)     [[ $# -ge 2 ]] || die "--host requires a value"; ARG_HOST="$2"; shift 2 ;;
        --user)     [[ $# -ge 2 ]] || die "--user requires a value"; ARG_USER="$2"; shift 2 ;;
        --pass)     [[ $# -ge 2 ]] || die "--pass requires a value"; ARG_PASS="$2"; shift 2 ;;
        --insecure) ARG_INSECURE=1; shift ;;
        --help|-h)  usage ;;
        -*)         die "Unknown option: $1" ;;
        *)          [[ -z "${SO_FILE}" ]] || die "Unexpected argument: $1"
                    SO_FILE="$1"; shift ;;
    esac
done

[[ "${EUID}" -eq 0 ]] || die "Must run as root.  Try: sudo $0 $*"
[[ -n "${SO_FILE}" ]] || usage
[[ -f "${SO_FILE}" ]] || die "File not found: ${SO_FILE}"
SO_FILE="$(realpath "${SO_FILE}")"

require fwupdmgr
require curl
require systemctl
require ip
require netplan

sep
info "nvidia-oob-redfish fwupd plugin — install"
info "Plugin file : ${SO_FILE}"
sep

# ══ PHASE 1: SYSTEM DISCOVERY ═════════════════════════════════════════════════
phase "PHASE 1: System discovery"

# ── Plugin directory ───────────────────────────────────────────────────────────
info "Locating fwupd plugin directory..."
detail "Strategy 1: scanning /usr/lib for existing fwupd plugins..."
PLUGIN_DIR="$(
    find /usr/lib -name 'libfu_plugin_*.so' \
         ! -name 'libfu_plugin_nvidia_oob_redfish.so' \
         2>/dev/null | head -1 | xargs -r dirname
)"

if [[ -z "${PLUGIN_DIR}" ]]; then
    detail "Strategy 2: querying pkg-config for plugindir..."
    PLUGIN_DIR="$(pkg-config --variable=plugindir fwupdplugin 2>/dev/null)" || true
fi

if [[ -z "${PLUGIN_DIR}" ]]; then
    detail "Strategy 3: deriving from fwupd systemd ExecStart..."
    FWUPD_BIN="$(systemctl cat fwupd.service 2>/dev/null | grep -oP '(?<=ExecStart=)\S+' | head -1 || true)"
    if [[ -n "${FWUPD_BIN}" ]]; then
        PLUGIN_DIR="$(
            find "$(dirname "$(dirname "${FWUPD_BIN}")")/lib" -maxdepth 3 \
                 -name 'libfu_plugin_*.so' 2>/dev/null \
            | head -1 | xargs -r dirname
        )"
    fi
fi

[[ -n "${PLUGIN_DIR}" ]] || die "Cannot locate fwupd plugin directory.  Is fwupd installed?"
detail "Plugin directory: ${PLUGIN_DIR}"
ok "Plugin directory located."

# ── fwupd version ─────────────────────────────────────────────────────────────
info "Detecting fwupd version..."
FWUPD_RUNTIME_VER="$(basename "${PLUGIN_DIR}" | grep -oP '\d+\.\d+\.\d+' || true)"
[[ -n "${FWUPD_RUNTIME_VER}" ]] \
    || die "Cannot determine version from '$(basename "${PLUGIN_DIR}")'.  Is fwupd installed?"
detail "fwupd runtime: ${FWUPD_RUNTIME_VER}"
ok "fwupd version: ${FWUPD_RUNTIME_VER}"

# ── Existing installation ──────────────────────────────────────────────────────
INSTALLED_SO="${PLUGIN_DIR}/libfu_plugin_nvidia_oob_redfish.so"
info "Checking for existing plugin installation..."
if [[ -f "${INSTALLED_SO}" ]]; then
    ALREADY_INSTALLED=1
    detail "Found — will verify and update config if needed."
    ok "Plugin already installed.  Will verify."
else
    ALREADY_INSTALLED=0
    detail "Not found — will install."
    ok "Plugin not yet installed."
fi

# ── fwupd systemd service ─────────────────────────────────────────────────────
info "Detecting fwupd systemd service..."
FWUPD_SERVICE=""
if systemctl list-units --type=service --all --no-legend 2>/dev/null \
        | grep -qE '^fwupd\.service'; then
    FWUPD_SERVICE="fwupd.service"
else
    FWUPD_SERVICE="$(
        systemctl list-units --type=service --all --no-legend 2>/dev/null \
        | grep -oE 'fwupd[^[:space:]]*\.service' \
        | grep -v 'fwupd-refresh' | head -1 || true
    )"
fi
FWUPD_SERVICE="${FWUPD_SERVICE:-fwupd.service}"
detail "Service unit: ${FWUPD_SERVICE}"
ok "systemd service: ${FWUPD_SERVICE}"

# ══ PHASE 2: BMC CONNECTIVITY ═════════════════════════════════════════════════
phase "PHASE 2: BMC connectivity"

BMC_HOST="${ARG_HOST:-${NVIDIA_OOB_BMC_HOST:-}}"
if [[ -z "${BMC_HOST}" ]]; then
    BMC_HOST="$(grep -oP 'BmcHost\s*=\s*\K\S+' /etc/fwupd/nvidia_oob.conf 2>/dev/null || true)"
fi

# ── USB interface -- configure static IP via netplan (MAC-agnostic) ────────────
# the BMC is connected via USB CDC/RNDIS Ethernet (enx... interface); after a
# host reboot this interface comes UP with no IPv4, making the BMC unreachable;
# we write a netplan config that matches by name glob `enx*` rather than by
# specific MAC -- every BMC firmware update tends to roll the USB MAC, so a
# pinned-MAC netplan would silently stop matching after each firmware bump;
# the glob match also survives the case where the kernel renames the
# interface across reboots
info "Detecting USB BMC network interface..."
_USB_IFACE=""
for _p in /sys/class/net/*/; do
    _i="${_p%/}"; _i="${_i##*/}"
    [[ -e "/sys/class/net/${_i}/device" ]] || continue
    _dev="$(readlink -f "/sys/class/net/${_i}/device" 2>/dev/null)" || continue
    printf '%s' "${_dev}" | grep -qE '/usb[0-9]' || continue
    _USB_IFACE="${_i}"
    detail "USB network interface: ${_USB_IFACE}"
    break
done

if [[ -n "${_USB_IFACE}" ]]; then
    _CUR_IP="$(ip addr show dev "${_USB_IFACE}" 2>/dev/null \
        | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"

    # Host-side IP = BMC subnet with last octet 2  (BMC is always *.1)
    _BMC_HINT="${BMC_HOST:-10.0.1.1}"
    _HOST_IP="${_BMC_HINT%.*}.2"
    _NETPLAN="/etc/netplan/10-nvidia-oob-bmc.yaml"

    # idempotency: only rewrite when the file does NOT already contain the
    # MAC-agnostic glob match -- older installs of this script pinned to a
    # specific interface name and need to be replaced
    if [[ "${_CUR_IP}" == "${_HOST_IP}" ]] \
        && grep -qs 'name: "enx\*"' "${_NETPLAN}" 2>/dev/null \
        && grep -qs "${_HOST_IP}/24" "${_NETPLAN}" 2>/dev/null; then
        ok "USB interface ${_USB_IFACE}: ${_HOST_IP}/24 already configured (netplan glob match up to date)."
    else
        if grep -qs 'enx[0-9a-f]\{12\}:' "${_NETPLAN}" 2>/dev/null; then
            detail "Existing netplan pins to a specific MAC → replacing with glob match"
        elif [[ -n "${_CUR_IP}" ]]; then
            detail "Current IP ${_CUR_IP} → replacing with ${_HOST_IP}/24"
        else
            detail "No IPv4 on ${_USB_IFACE} → assigning ${_HOST_IP}/24"
        fi

        info "Writing ${_NETPLAN} (MAC-agnostic enx* match)..."
        cat > "${_NETPLAN}" <<NETPLAN_EOF
# managed by install-plugin.sh from nvidia-oob-redfish;
# matches by name glob "enx*" so it survives BMC firmware updates that roll
# the USB MAC; if you have multiple USB-Ethernet adapters on this host,
# replace the match with a stricter rule (driver: cdc_ether, or a specific
# MAC) -- see the plugin README's "AC (aux-rail) power cycle" section
network:
  version: 2
  ethernets:
    bmc-oob:
      match:
        name: "enx*"
      dhcp4: false
      addresses:
        - ${_HOST_IP}/24
      optional: true
NETPLAN_EOF
        chmod 600 "${_NETPLAN}"
        ok "Written: ${_NETPLAN}"

        info "Applying netplan..."
        netplan apply 2>/dev/null || true

        # wait up to 10 s for the IP to appear on whatever enx* exists
        for _n in {1..10}; do
            _chk="$(ip addr show dev "${_USB_IFACE}" 2>/dev/null \
                | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"
            [[ "${_chk}" == "${_HOST_IP}" ]] && break
            detail "Waiting for ${_HOST_IP} on ${_USB_IFACE}... (${_n}/10)"
            sleep 1
        done
        _final="$(ip addr show dev "${_USB_IFACE}" 2>/dev/null \
            | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"
        [[ "${_final}" == "${_HOST_IP}" ]] \
            && ok "Interface ${_USB_IFACE}: ${_HOST_IP}/24 active." \
            || warn "IP not yet visible on ${_USB_IFACE} — continuing anyway."

        [[ -z "${BMC_HOST}" ]] && BMC_HOST="${_BMC_HINT}"
        milestone "USB BMC interface configured — survives reboots and BMC firmware MAC changes"
    fi
else
    warn "No USB network interface detected — skipping netplan setup."
fi

# ── BMC auto-discovery (if host still unknown) ────────────────────────────────
if [[ -z "${BMC_HOST}" ]]; then
    info "Auto-discovering BMC via USB network interfaces..."
    for iface_path in /sys/class/net/*/; do
        iface="${iface_path%/}"; iface="${iface##*/}"
        [[ -e "/sys/class/net/${iface}/device" ]] || continue
        sysdev="$(readlink -f "/sys/class/net/${iface}/device" 2>/dev/null)" || continue
        printf '%s' "${sysdev}" | grep -qE '/usb[0-9]' || continue
        addr="$(ip addr show dev "${iface}" 2>/dev/null \
            | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"
        [[ -n "${addr}" ]] || continue
        gateway="${addr%.*}.1"
        detail "Probing https://${gateway}/redfish/v1/ ..."
        if curl -sk --connect-timeout 3 "https://${gateway}/redfish/v1/" >/dev/null 2>&1; then
            BMC_HOST="${gateway}"
            detail "BMC found at ${BMC_HOST}"
            break
        fi
    done
fi

[[ -n "${BMC_HOST}" ]] \
    || die "Cannot reach BMC.  Provide --host <ip> or ensure the USB link is up."

# ── Verify reachability ────────────────────────────────────────────────────────
info "Verifying BMC reachability at ${BMC_HOST}..."
if ! curl -sk --connect-timeout 5 "https://${BMC_HOST}/redfish/v1/" >/dev/null 2>&1; then
    warn "BMC at ${BMC_HOST} is not responding."
    warn "  ip addr show                        # check USB interface has an IP"
    warn "  ip route show | grep ${BMC_HOST%.*} # check route to BMC subnet"
    die "BMC unreachable — check USB link, then retry."
fi
ok "BMC reachable at ${BMC_HOST}."

# ── TLS ───────────────────────────────────────────────────────────────────────
CA_PATH="/etc/fwupd/pki/nvidia-oob/ca.pem"
INSECURE="${ARG_INSECURE:-}"
if [[ -z "${INSECURE}" ]]; then
    [[ -f "${CA_PATH}" ]] && INSECURE=0 || INSECURE=1
fi
CURL_TLS=()
if [[ "${INSECURE}" == "1" ]]; then
    CURL_TLS=(-k)
    warn "TLS verification OFF — no CA cert at ${CA_PATH} (bringup mode)."
else
    CURL_TLS=(--cacert "${CA_PATH}")
    detail "TLS: verified against ${CA_PATH}"
fi

# ── username ───────────────────────────────────────────────────────────────────
# resolve from --user / env for non-interactive use; otherwise prompt in Phase 3
BMC_USER="${ARG_USER:-${NVIDIA_OOB_BMC_USER:-}}"
_BMC_USER_DEFAULT="$(grep -oP 'BmcUser\s*=\s*\K\S+' /etc/fwupd/nvidia_oob.conf 2>/dev/null || echo 'root')"
[[ -n "${BMC_USER}" ]] && detail "BMC username: ${BMC_USER} (non-interactive)"

# ══ PHASE 3: BMC AUTHENTICATION ═══════════════════════════════════════════════
phase "PHASE 3: BMC authentication"

SESSION_FILE="/run/fwupd/nvidia_oob.session"
BMC_PASS="${ARG_PASS:-${NVIDIA_OOB_BMC_PASS:-}}"
TOKEN=""
MAX_ATTEMPTS=3

# always prompt for username unless supplied non-interactively via --user or env
if [[ -z "${BMC_USER}" ]]; then
    printf '  BMC username [%s]: ' "${_BMC_USER_DEFAULT}"
    read -r _INPUT_USER
    BMC_USER="${_INPUT_USER:-${_BMC_USER_DEFAULT}}"
    printf '\n'
    detail "BMC username: ${BMC_USER}"
fi

for attempt in $(seq 1 ${MAX_ATTEMPTS}); do
    if [[ -z "${BMC_PASS}" ]]; then
        printf '\n'
        printf '%s  BMC Authentication  %s\n' \
            "$(_c '1;33')══════════════════" "══════════════════$(_c 0)"
        printf '  Host     : %s\n' "${BMC_HOST}"
        printf '  Username : %s\n' "${BMC_USER}"
        printf '  Password : '
        read -rsp '' BMC_PASS
        printf '\n\n'
    fi

    info "Requesting Redfish session (attempt ${attempt}/${MAX_ATTEMPTS})..."
    detail "POST https://${BMC_HOST}/redfish/v1/SessionService/Sessions"

    HTTP_RESPONSE="$(
        curl -s "${CURL_TLS[@]}" --connect-timeout 10 \
             -X POST "https://${BMC_HOST}/redfish/v1/SessionService/Sessions" \
             -H 'Content-Type: application/json' \
             -d "{\"UserName\":\"${BMC_USER}\",\"Password\":\"${BMC_PASS}\"}" \
             -D - 2>/dev/null
    )" || true
    HTTP_STATUS="$(printf '%s' "${HTTP_RESPONSE}" | grep -m1 '^HTTP/' | awk '{print $2}' || true)"
    TOKEN="$(printf '%s' "${HTTP_RESPONSE}" | grep -i '^x-auth-token:' | awk '{print $2}' | tr -d '\r\n' || true)"

    detail "HTTP status : ${HTTP_STATUS:-no response}"
    detail "Token       : ${TOKEN:+received (${#TOKEN} chars)}${TOKEN:-not in response}"

    if [[ -n "${TOKEN}" ]]; then
        ok "Authenticated as '${BMC_USER}' — session token obtained."
        break
    fi

    case "${HTTP_STATUS}" in
        401) warn "Wrong password for user '${BMC_USER}' (HTTP 401)." ;;
        403) warn "User '${BMC_USER}' not authorised on this BMC (HTTP 403)." ;;
        400) die "Bad request (HTTP 400) — check username format." ;;
        000|"") die "No response from BMC at ${BMC_HOST} — check network connectivity." ;;
        *)   die "Unexpected HTTP ${HTTP_STATUS} from BMC." ;;
    esac

    if [[ "${attempt}" -lt "${MAX_ATTEMPTS}" ]]; then
        warn "Re-enter password (attempt $((attempt + 1))/${MAX_ATTEMPTS})."
        BMC_PASS=""
    else
        die "Authentication failed after ${MAX_ATTEMPTS} attempts."
    fi
done

mkdir -p "$(dirname "${SESSION_FILE}")"
printf '%s\n' "${TOKEN}" > "${SESSION_FILE}"
chmod 600 "${SESSION_FILE}"
ok "Session token written to ${SESSION_FILE}"

# ══ PHASE 4: PLUGIN INSTALLATION ══════════════════════════════════════════════
phase "PHASE 4: Plugin installation"

NEEDS_RESTART=0

if [[ "${ALREADY_INSTALLED}" -eq 1 ]]; then
    printf '\n'
    printf '%s  Plugin already installed  %s\n' \
        "$(_c '1;33')══════════════════" "══════════════════$(_c 0)"
    printf '  Path : %s\n\n' "${INSTALLED_SO}"
    printf '  [S] Skip    — keep the existing binary (default)\n'
    printf '  [R] Replace — remove old binary and install the new one\n\n'
    printf '  Choice [S/r]: '
    read -r _REINSTALL_CHOICE
    printf '\n'

    if [[ "${_REINSTALL_CHOICE,,}" == "r" ]]; then
        info "Removing existing plugin binary..."
        rm -f "${INSTALLED_SO}"
        info "Installing new plugin binary..."
        detail "Source : ${SO_FILE}"
        detail "Dest   : ${INSTALLED_SO}"
        install -m 755 "${SO_FILE}" "${INSTALLED_SO}"
        ok "Plugin reinstalled: ${INSTALLED_SO}"
        NEEDS_RESTART=1
    else
        info "Keeping existing plugin binary."
        ok "Plugin binary unchanged."
    fi
else
    milestone "Starting fresh install on fwupd ${FWUPD_RUNTIME_VER}"
    info "Installing plugin..."
    detail "Source : ${SO_FILE}"
    detail "Dest   : ${INSTALLED_SO}"
    install -m 755 "${SO_FILE}" "${INSTALLED_SO}"
    ok "Plugin installed: ${INSTALLED_SO}"
    milestone "Plugin binary installed"
    NEEDS_RESTART=1
fi

# ── Environment script ────────────────────────────────────────────────────────
info "Installing fwupd-nvidia-oob-env.sh to /usr/local/bin/..."
_env_src="${SCRIPT_DIR}/fwupd-nvidia-oob-env.sh"
if [[ -f "${_env_src}" ]]; then
    install -m 755 "${_env_src}" /usr/local/bin/fwupd-nvidia-oob-env.sh
    ok "Installed: /usr/local/bin/fwupd-nvidia-oob-env.sh"
else
    warn "fwupd-nvidia-oob-env.sh not found alongside installer — skipping"
fi

# ══ PHASE 5: CONFIGURATION ════════════════════════════════════════════════════
phase "PHASE 5: Configuration"

CONF_FILE="/etc/fwupd/nvidia_oob.conf"

info "Checking BMC config (${CONF_FILE})..."
CONF_CURRENT_HOST="$(grep -oP 'BmcHost\s*=\s*\K\S+' "${CONF_FILE}" 2>/dev/null || true)"
CONF_CURRENT_USER="$(grep -oP 'BmcUser\s*=\s*\K\S+' "${CONF_FILE}" 2>/dev/null || true)"

if [[ "${CONF_CURRENT_HOST}" == "${BMC_HOST}" && "${CONF_CURRENT_USER}" == "${BMC_USER}" ]]; then
    detail "Config already correct (BmcHost=${BMC_HOST}, BmcUser=${BMC_USER})."
    ok "BMC config up to date — no change."
else
    [[ -n "${CONF_CURRENT_HOST}" ]] \
        && detail "Updating config." \
        || detail "Creating ${CONF_FILE}"
    mkdir -p /etc/fwupd
    # password is intentionally NOT written -- credentials are never stored on disk
    printf '[OOB]\nBmcHost=%s\nBmcUser=%s\n' "${BMC_HOST}" "${BMC_USER}" > "${CONF_FILE}"
    chmod 600 "${CONF_FILE}"
    ok "Written: ${CONF_FILE}  (BmcHost=${BMC_HOST}, BmcUser=${BMC_USER})"
    [[ "${ALREADY_INSTALLED}" -eq 0 ]] && milestone "BMC config written → ${CONF_FILE}"
    NEEDS_RESTART=1
fi

# ── Systemd drop-in ───────────────────────────────────────────────────────────
DROPIN_DIR="/etc/systemd/system/${FWUPD_SERVICE}.d"
DROPIN_FILE="${DROPIN_DIR}/nvidia-oob.conf"
info "Checking systemd drop-in (${DROPIN_FILE})..."

NEW_DROPIN="$(
    printf '[Service]\n'
    [[ "${INSECURE}" == "1" ]] && printf 'Environment=NVIDIA_OOB_INSECURE=1\n' || true
)"
EXISTING_DROPIN="$(cat "${DROPIN_FILE}" 2>/dev/null || true)"

if [[ "${NEW_DROPIN}" == "${EXISTING_DROPIN}" && "${ALREADY_INSTALLED}" -eq 1 ]]; then
    detail "Drop-in unchanged."
    ok "systemd drop-in up to date."
else
    mkdir -p "${DROPIN_DIR}"
    printf '%s\n' "${NEW_DROPIN}" > "${DROPIN_FILE}"
    systemctl daemon-reload
    ok "Written: ${DROPIN_FILE}"
    [[ "${ALREADY_INSTALLED}" -eq 0 ]] && milestone "systemd drop-in configured"
    NEEDS_RESTART=1
fi

# ══ PHASE 6: SERVICE RESTART ══════════════════════════════════════════════════
phase "PHASE 6: Service restart"

RUNTIME_PRESERVED="$(
    systemctl show "${FWUPD_SERVICE}" --property=RuntimeDirectoryPreserve 2>/dev/null \
    | cut -d= -f2
)"
detail "RuntimeDirectoryPreserve: ${RUNTIME_PRESERVED}"

if [[ "${NEEDS_RESTART}" -eq 1 ]]; then
    info "Restarting ${FWUPD_SERVICE}..."
    systemctl restart "${FWUPD_SERVICE}"

    # if the runtime directory is cleared on restart, re-write the token
    if [[ "${RUNTIME_PRESERVED}" != "restart" && "${RUNTIME_PRESERVED}" != "yes" ]]; then
        detail "RuntimeDirectory was cleared on restart — re-writing session token."
        printf '%s\n' "${TOKEN}" > "${SESSION_FILE}"
        chmod 600 "${SESSION_FILE}"
    fi

    for i in {1..10}; do
        systemctl is-active --quiet "${FWUPD_SERVICE}" && break
        detail "Waiting for ${FWUPD_SERVICE}... (${i}/10)"
        sleep 1
    done
    systemctl is-active --quiet "${FWUPD_SERVICE}" \
        || die "${FWUPD_SERVICE} did not become active after restart."
    ok "${FWUPD_SERVICE} is active."
    [[ "${ALREADY_INSTALLED}" -eq 0 ]] && milestone "fwupd restarted with plugin"
else
    ok "No config change — no restart needed."
fi

# ══ PHASE 7: VERIFICATION ═════════════════════════════════════════════════════
phase "PHASE 7: Verification"

VERIFY_PASS=0
VERIFY_FAIL=0
check_pass() { pass "$*"; (( VERIFY_PASS++ )) || true; }
check_fail() { fail "$*"; (( VERIFY_FAIL++ )) || true; }

# check 1: plugin binary on disk
chk "Plugin binary installed..."
if [[ -f "${INSTALLED_SO}" ]]; then
    check_pass "Binary present: ${INSTALLED_SO}"
else
    check_fail "Binary missing: ${INSTALLED_SO}"
fi

# check 2: BmcHost in config
chk "BmcHost in config..."
ACTIVE_HOST="$(grep -oP 'BmcHost\s*=\s*\K\S+' "${CONF_FILE}" 2>/dev/null || true)"
if [[ "${ACTIVE_HOST}" == "${BMC_HOST}" ]]; then
    check_pass "BmcHost=${ACTIVE_HOST}"
else
    check_fail "BmcHost mismatch — expected '${BMC_HOST}', got '${ACTIVE_HOST:-<missing>}'"
fi

# check 3: BmcUser in config
chk "BmcUser in config..."
ACTIVE_USER="$(grep -oP 'BmcUser\s*=\s*\K\S+' "${CONF_FILE}" 2>/dev/null || true)"
if [[ -n "${ACTIVE_USER}" ]]; then
    check_pass "BmcUser=${ACTIVE_USER}"
else
    check_fail "BmcUser missing from ${CONF_FILE}"
fi

# check 4: BMC Redfish reachable
chk "BMC Redfish endpoint reachable..."
PING_STATUS="$(
    curl -s "${CURL_TLS[@]}" --connect-timeout 5 -o /dev/null -w '%{http_code}' \
         "https://${BMC_HOST}/redfish/v1/" 2>/dev/null || true
)"
if [[ "${PING_STATUS}" == "200" ]]; then
    check_pass "Redfish root returned HTTP 200."
else
    check_fail "Redfish root returned HTTP ${PING_STATUS:-?} — check network."
fi

# check 5: plugin loaded in fwupdmgr
chk "nvidia_oob_redfish plugin loaded..."
if fwupdmgr get-plugins 2>/dev/null | grep -q 'nvidia_oob_redfish'; then
    check_pass "Plugin loaded in fwupdmgr."
else
    check_fail "Plugin not visible — check: journalctl -u ${FWUPD_SERVICE} -n 50 --no-pager"
fi

# check 6: OOB devices visible
chk "OOB-managed devices visible..."
OOB_COUNT="$(fwupdmgr get-devices 2>/dev/null | grep -c 'OOB-managed' || true)"
if [[ "${OOB_COUNT}" -gt 0 ]]; then
    check_pass "${OOB_COUNT} OOB-managed firmware component(s) discovered."
else
    check_fail "No OOB devices — check: journalctl -u ${FWUPD_SERVICE} -n 80 --no-pager"
fi

# check 7: environment script installed
chk "fwupd-nvidia-oob-env.sh installed..."
if [[ -x /usr/local/bin/fwupd-nvidia-oob-env.sh ]]; then
    check_pass "fwupd-nvidia-oob-env.sh at /usr/local/bin/fwupd-nvidia-oob-env.sh"
else
    check_fail "fwupd-nvidia-oob-env.sh not installed — transfer it alongside install-plugin.sh"
fi

# ══ SUMMARY ═══════════════════════════════════════════════════════════════════
phase "SUMMARY"

# pull the running plugin version from the journal -- the plugin g_debug()s
# its version on every startup, so the most recent entry reflects what's
# actually loaded right now (not what was on disk before the restart)
PLUGIN_VERSION="$(journalctl -u "${FWUPD_SERVICE}" --since '5 minutes ago' --no-pager 2>/dev/null \
    | grep -oP 'nvidia_oob_redfish plugin v\K[0-9]+\.[0-9]+\.[0-9]+' \
    | tail -1)"
PLUGIN_VERSION="${PLUGIN_VERSION:-(not reported — plugin may pre-date version logging)}"

sep
printf '   %-24s %s\n' "Mode:"            "$( [[ "${ALREADY_INSTALLED}" -eq 1 ]] && echo "Re-verify/reconfigure" || echo "Fresh install" )"
printf '   %-24s %s\n' "fwupd version:"   "${FWUPD_RUNTIME_VER}"
printf '   %-24s %s\n' "Plugin version:"  "${PLUGIN_VERSION}"
printf '   %-24s %s\n' "Plugin path:"     "${INSTALLED_SO}"
printf '   %-24s %s\n' "BMC host:"        "${BMC_HOST}"
printf '   %-24s %s\n' "BMC user:"        "${BMC_USER}"
printf '   %-24s %s\n' "TLS:"             "$( [[ "${INSECURE}" == "1" ]] && echo "insecure (bringup)" || echo "verified" )"
printf '   %-24s %s\n' "OOB devices:"     "${OOB_COUNT}"
printf '   %-24s %s\n' "Env script:"      "/usr/local/bin/fwupd-nvidia-oob-env.sh"
printf '   %-24s %s/%s\n' "Checks:"       "${VERIFY_PASS}" "$((VERIFY_PASS + VERIFY_FAIL))"
sep

if [[ "${VERIFY_FAIL}" -eq 0 ]]; then
    ok "Installation and authentication complete."
    ok "Before each fwupdmgr session, run:  source fwupd-nvidia-oob-env.sh"
else
    warn "${VERIFY_FAIL} check(s) failed.  See details above."
fi
sep
