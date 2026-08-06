#!/usr/bin/env bash
# Copyright 2026 NVIDIA Corporation
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# fwupd-nvidia-oob-env.sh -- authenticate to the BMC and provision a Redfish
# session token so that fwupdmgr can use the nvidia-oob-redfish plugin
#
# source into your shell once before using fwupdmgr:
#
#   source /usr/local/bin/fwupd-nvidia-oob-env.sh
#   fwupdmgr get-devices
#   sudo fwupdmgr install firmware.cab --device <device-id>
#
# run again after every reboot or whenever the token expires
# can also be executed directly (sudo /usr/local/bin/fwupd-nvidia-oob-env.sh)
#
# Options:
#   --user <username>   BMC username (default: BmcUser from config, else "root")
#   --pass <password>   BMC password (prompted interactively if omitted)
#   --host <ip>         BMC IP / hostname (default: BmcHost from config)
#   --insecure          Skip TLS certificate verification
#
# Environment overrides (higher priority than config file):
#   NVIDIA_OOB_BMC_HOST, NVIDIA_OOB_BMC_USER, NVIDIA_OOB_BMC_PASS

# ── logging ────────────────────────────────────────────────────────────────────
_c()      { printf '\e[%sm' "$1"; }
_info()   { printf '%s[INFO ]%s  %s\n'     "$(_c '1;34')" "$(_c 0)" "$*" >&2; }
_detail() { printf '%s[INFO ]%s    ↳ %s\n' "$(_c '34')"   "$(_c 0)" "$*" >&2; }
_ok()     { printf '%s[ OK  ]%s  %s\n'     "$(_c '1;32')" "$(_c 0)" "$*" >&2; }
_warn()   { printf '%s[WARN ]%s  %s\n'     "$(_c '1;33')" "$(_c 0)" "$*" >&2; }
_sep()    { printf '%s\n' "────────────────────────────────────────────────────────" >&2; }
_phase()  { printf '\n%s══ %s %s%s\n' "$(_c '1;37')" "$*" "$(printf '═%.0s' {1..40})" "$(_c 0)" >&2; }

# ── self-heal the USB-OOB network (called on BMC-unreachable) ─────────────────
# mirrors the netplan logic in install-plugin.sh so users don't have to re-run
# the full installer after BMC firmware updates that roll the USB MAC; three
# paths to recovery, in order of speed:
#
#   1. already correct -> no-op (the fast/idempotent path)
#   2. USB iface present but missing IPv4 -> `ip addr add` for immediate effect
#   3. Netplan missing or pinned to a stale MAC -> rewrite with the MAC-agnostic
#      "enx*" glob match and `netplan apply` for permanence
#
# requires root for `ip addr add` and `tee /etc/netplan/`; logs and skips if
# not root (sourcing as a non-privileged user); the function is idempotent --
# safe to call on every source
_oob_repair_network() {
    local bmc_hint="$1"                # e.g. 10.0.1.1
    local host_ip="${bmc_hint%.*}.2"   # 10.0.1.2 (host side)
    local netplan_file=/etc/netplan/10-nvidia-oob-bmc.yaml

    if [[ "$(id -u)" -ne 0 ]]; then
        _warn "BMC not reachable but not running as root — cannot self-heal."
        _detail "Re-source with sudo, or run install-plugin.sh once."
        return 1
    fi

    # find the USB-CDC Ethernet interface (the BMC link)
    local iface=""
    local p i dev
    for p in /sys/class/net/*/; do
        i="${p%/}"; i="${i##*/}"
        [[ -e "/sys/class/net/${i}/device" ]] || continue
        dev="$(readlink -f "/sys/class/net/${i}/device" 2>/dev/null)" || continue
        printf '%s' "${dev}" | grep -qE '/usb[0-9]' || continue
        iface="${i}"
        break
    done
    if [[ -z "${iface}" ]]; then
        _warn "No USB network interface found — cannot self-heal."
        return 1
    fi
    _detail "USB interface: ${iface}"

    # Quick fix: assign the IP right now if it's not already there. Survives
    # only until reboot; the netplan rewrite below makes it persistent
    local cur_ip
    cur_ip="$(ip addr show dev "${iface}" 2>/dev/null \
                 | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"
    if [[ "${cur_ip}" != "${host_ip}" ]]; then
        _info "Assigning ${host_ip}/24 to ${iface} (runtime)..."
        ip addr add "${host_ip}/24" dev "${iface}" 2>/dev/null || true
        ip link set "${iface}" up 2>/dev/null || true
    fi

    # Persistent fix: if the netplan is missing or pinned to a specific MAC
    # (older install-plugin.sh versions), rewrite with the glob match. The
    # glob survives BMC firmware updates that roll the USB MAC
    local needs_rewrite=0
    if [[ ! -f "${netplan_file}" ]]; then
        needs_rewrite=1
        _detail "No netplan file → writing"
    elif ! grep -qs 'name: "enx\*"' "${netplan_file}"; then
        needs_rewrite=1
        _detail "Existing netplan pins a specific MAC → replacing with glob"
    elif ! grep -qs "${host_ip}/24" "${netplan_file}"; then
        needs_rewrite=1
        _detail "Netplan IP differs from ${host_ip}/24 → updating"
    fi
    if [[ ${needs_rewrite} -eq 1 ]]; then
        cat > "${netplan_file}" <<EOF
# auto-managed by fwupd-nvidia-oob-env.sh / install-plugin.sh
# matches by name glob "enx*" so it survives BMC firmware updates that
# roll the USB MAC
network:
  version: 2
  ethernets:
    bmc-oob:
      match:
        name: "enx*"
      dhcp4: false
      addresses:
        - ${host_ip}/24
      optional: true
EOF
        chmod 600 "${netplan_file}"
        _ok "Updated ${netplan_file}"
        _info "Applying netplan..."
        netplan apply 2>/dev/null || true
    fi

    # wait briefly for the IP to settle on the interface
    local n
    for n in {1..5}; do
        cur_ip="$(ip addr show dev "${iface}" 2>/dev/null \
                     | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"
        [[ "${cur_ip}" == "${host_ip}" ]] && break
        sleep 1
    done
    [[ "${cur_ip}" == "${host_ip}" ]] || {
        _warn "Interface ${iface} still missing ${host_ip}/24 after repair."
        return 1
    }
    return 0
}

# ── main (wrapped so 'return' works correctly when sourced) ────────────────────
_oob_auth_main() {
    local ARG_USER="" ARG_PASS="" ARG_HOST="" ARG_INSECURE=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --user)     [[ $# -ge 2 ]] || { printf '[FATAL] --user requires a value\n' >&2; return 1; }
                        ARG_USER="$2"; shift 2 ;;
            --pass)     [[ $# -ge 2 ]] || { printf '[FATAL] --pass requires a value\n' >&2; return 1; }
                        ARG_PASS="$2"; shift 2 ;;
            --host)     [[ $# -ge 2 ]] || { printf '[FATAL] --host requires a value\n' >&2; return 1; }
                        ARG_HOST="$2"; shift 2 ;;
            --insecure) ARG_INSECURE=1; shift ;;
            --help|-h)
                cat >&2 <<'EOF'
Usage: source /usr/local/bin/fwupd-nvidia-oob-env.sh [OPTIONS]
   or: sudo /usr/local/bin/fwupd-nvidia-oob-env.sh  [OPTIONS]

  --user <username>   BMC username (default: BmcUser from config or "root")
  --pass <password>   BMC password (prompted interactively if omitted)
  --host <ip>         BMC host     (default: BmcHost from config)
  --insecure          Skip TLS verification
EOF
                return 0 ;;
            -*) printf '%s[FATAL]%s  Unknown option: %s\n' "$(_c '1;31')" "$(_c 0)" "$1" >&2; return 1 ;;
            *)  printf '%s[FATAL]%s  Unexpected argument: %s\n' "$(_c '1;31')" "$(_c 0)" "$1" >&2; return 1 ;;
        esac
    done

    if [[ "${EUID}" -ne 0 ]]; then
        printf '%s[FATAL]%s  Must run as root.\n' "$(_c '1;31')" "$(_c 0)" >&2
        if [[ "${_OOB_SOURCED:-0}" -eq 1 ]]; then
            printf '         Become root first: sudo -s\n' >&2
            printf '         Then source:  source %s\n' "${BASH_SOURCE[1]}" >&2
        else
            printf '         Try: sudo %s\n' "${BASH_SOURCE[0]}" >&2
        fi
        return 1
    fi

    command -v curl      >/dev/null 2>&1 || { printf '%s[FATAL]%s  curl is required.\n' "$(_c '1;31')" "$(_c 0)" >&2; return 1; }
    command -v systemctl >/dev/null 2>&1 || { printf '%s[FATAL]%s  systemctl is required.\n' "$(_c '1;31')" "$(_c 0)" >&2; return 1; }

    local CONF_FILE="/etc/fwupd/nvidia_oob.conf"
    local SESSION_FILE="/run/fwupd/nvidia_oob.session"
    local CA_PATH="/etc/fwupd/pki/nvidia-oob/ca.pem"

    _sep
    _info "nvidia-oob-redfish — BMC authentication"
    _sep

    # ══ STEP 1: BMC host ══════════════════════════════════════════════════════
    _phase "STEP 1: BMC host"

    local BMC_HOST="${ARG_HOST:-${NVIDIA_OOB_BMC_HOST:-}}"

    if [[ -z "${BMC_HOST}" ]]; then
        BMC_HOST="$(grep -oP 'BmcHost\s*=\s*\K\S+' "${CONF_FILE}" 2>/dev/null || true)"
    fi

    if [[ -z "${BMC_HOST}" ]]; then
        _info "Auto-discovering BMC via USB network interfaces..."
        local iface_path iface sysdev addr gateway
        for iface_path in /sys/class/net/*/; do
            iface="${iface_path%/}"; iface="${iface##*/}"
            [[ -e "/sys/class/net/${iface}/device" ]] || continue
            sysdev="$(readlink -f "/sys/class/net/${iface}/device" 2>/dev/null)" || continue
            printf '%s' "${sysdev}" | grep -qE '/usb[0-9]' || continue
            addr="$(ip addr show dev "${iface}" 2>/dev/null \
                    | grep -oP '(?<=inet )\d+\.\d+\.\d+\.\d+' | head -1 || true)"
            [[ -n "${addr}" ]] || continue
            gateway="${addr%.*}.1"
            _detail "Probing ${gateway} via ${iface}..."
            if curl -sk --connect-timeout 3 "https://${gateway}/redfish/v1/" >/dev/null 2>&1; then
                BMC_HOST="${gateway}"
                _detail "BMC found at ${BMC_HOST}"
                break
            fi
        done
    fi

    if [[ -z "${BMC_HOST}" ]]; then
        printf '%s[FATAL]%s  Cannot find BMC.  Set --host <ip> or add BmcHost to %s\n' \
            "$(_c '1;31')" "$(_c 0)" "${CONF_FILE}" >&2
        return 1
    fi
    _ok "BMC host: ${BMC_HOST}"

    _info "Verifying BMC reachability..."
    if ! curl -sk --connect-timeout 5 "https://${BMC_HOST}/redfish/v1/" >/dev/null 2>&1; then
        # try to self-heal -- most common cause is a stale MAC-pinned netplan
        # after a BMC firmware update changed the USB MAC, or a fresh boot
        # where the interface came up without an IP; _oob_repair_network
        # writes/refreshes the netplan with a MAC-agnostic glob match, runs
        # `netplan apply`, and assigns the IP at runtime; idempotent
        _warn "BMC at ${BMC_HOST} not responding — attempting network self-heal..."
        if _oob_repair_network "${BMC_HOST}" \
            && curl -sk --connect-timeout 5 "https://${BMC_HOST}/redfish/v1/" >/dev/null 2>&1; then
            _ok "BMC reachable after self-heal."
        else
            printf '%s[FATAL]%s  BMC at %s is not responding.\n' \
                "$(_c '1;31')" "$(_c 0)" "${BMC_HOST}" >&2
            printf '         Self-heal attempted but BMC still unreachable.\n' >&2
            printf '         Diagnose with:\n' >&2
            printf '           ip addr show\n' >&2
            printf '           ip route show | grep %s\n' "${BMC_HOST%.*}" >&2
            printf '         If no USB interface has a %s.x address, wait for\n' "${BMC_HOST%.*}" >&2
            printf '         the device to enumerate, then retry.\n' >&2
            return 1
        fi
    else
        _ok "BMC reachable."
    fi

    # ══ STEP 2: Credentials ═══════════════════════════════════════════════════
    _phase "STEP 2: Credentials"

    local BMC_USER="${ARG_USER:-${NVIDIA_OOB_BMC_USER:-}}"
    if [[ -z "${BMC_USER}" ]]; then
        BMC_USER="$(grep -oP 'BmcUser\s*=\s*\K\S+' "${CONF_FILE}" 2>/dev/null || true)"
    fi
    BMC_USER="${BMC_USER:-root}"

    # Prompt interactively unless --user or NVIDIA_OOB_BMC_USER was explicitly set
    if [[ -z "${ARG_USER}" && -z "${NVIDIA_OOB_BMC_USER:-}" ]]; then
        printf '  BMC username [%s]: ' "${BMC_USER}"
        local _input_user
        read -r _input_user
        [[ -n "${_input_user}" ]] && BMC_USER="${_input_user}"
    fi
    _detail "BMC username: ${BMC_USER}"

    local BMC_PASS="${ARG_PASS:-${NVIDIA_OOB_BMC_PASS:-}}"

    # ══ STEP 3: Authenticate ══════════════════════════════════════════════════
    _phase "STEP 3: Authentication"

    local INSECURE="${ARG_INSECURE:-}"
    if [[ -z "${INSECURE}" ]]; then
        [[ -f "${CA_PATH}" ]] && INSECURE=0 || INSECURE=1
    fi

    local -a CURL_TLS=()
    if [[ "${INSECURE}" == "1" ]]; then
        CURL_TLS=(-k)
        _warn "TLS verification OFF (no CA cert at ${CA_PATH})"
    else
        CURL_TLS=(--cacert "${CA_PATH}")
        _detail "TLS: verified against ${CA_PATH}"
    fi

    local TOKEN="" HTTP_RESPONSE="" HTTP_STATUS=""
    local MAX_ATTEMPTS=3

    local attempt
    for attempt in $(seq 1 ${MAX_ATTEMPTS}); do
        if [[ -z "${BMC_PASS}" ]]; then
            printf '\n'
            printf '%s  BMC Authentication  %s\n' \
                "$(_c '1;33')══════════════════" "══════════════════$(_c 0)"
            printf '  Host     : %s\n' "${BMC_HOST}"
            printf '  Username : %s\n  Password : ' "${BMC_USER}"
            read -rsp '' BMC_PASS
            printf '\n\n'
        fi

        _info "Requesting Redfish session (attempt ${attempt}/${MAX_ATTEMPTS})..."
        _detail "POST https://${BMC_HOST}/redfish/v1/SessionService/Sessions"

        HTTP_RESPONSE="$(
            curl -s "${CURL_TLS[@]}" --connect-timeout 10 \
                 -X POST "https://${BMC_HOST}/redfish/v1/SessionService/Sessions" \
                 -H 'Content-Type: application/json' \
                 -d "{\"UserName\":\"${BMC_USER}\",\"Password\":\"${BMC_PASS}\"}" \
                 -D - 2>/dev/null
        )" || true
        HTTP_STATUS="$(printf '%s' "${HTTP_RESPONSE}" \
            | grep -m1 '^HTTP/' | awk '{print $2}' || true)"
        TOKEN="$(printf '%s' "${HTTP_RESPONSE}" \
            | grep -i '^x-auth-token:' | awk '{print $2}' | tr -d '\r\n' || true)"

        _detail "HTTP status : ${HTTP_STATUS:-no response}"
        # never echo the token itself -- only its presence + length; the
        # `:+` / `:-` pair previously used here looked symmetric but the
        # `:-` falls back to the variable's *value* when set, not to
        # nothing, so a live session token landed on stdout; use a
        # straight if/else
        if [[ -n "${TOKEN}" ]]; then
            _detail "Token       : received (${#TOKEN} chars)"
        else
            _detail "Token       : not in response"
        fi

        if [[ -n "${TOKEN}" ]]; then
            _ok "Authenticated as '${BMC_USER}' — session token obtained."
            break
        fi

        case "${HTTP_STATUS}" in
            401) _warn "Wrong password for user '${BMC_USER}' (HTTP 401)." ;;
            403) _warn "User '${BMC_USER}' not authorised on this BMC (HTTP 403)." ;;
            400) printf '%s[FATAL]%s  Bad request (HTTP 400) — check username format.\n' \
                     "$(_c '1;31')" "$(_c 0)" >&2; return 1 ;;
            000|"") printf '%s[FATAL]%s  No response from BMC at %s — check network.\n' \
                        "$(_c '1;31')" "$(_c 0)" "${BMC_HOST}" >&2; return 1 ;;
            *) printf '%s[FATAL]%s  Unexpected HTTP %s from BMC.\n' \
                   "$(_c '1;31')" "$(_c 0)" "${HTTP_STATUS}" >&2; return 1 ;;
        esac

        if [[ "${attempt}" -lt "${MAX_ATTEMPTS}" ]]; then
            _warn "Re-enter password (attempt $((attempt + 1))/${MAX_ATTEMPTS})."
            BMC_PASS=""
        else
            printf '%s[FATAL]%s  Authentication failed after %d attempts.\n' \
                "$(_c '1;31')" "$(_c 0)" "${MAX_ATTEMPTS}" >&2
            return 1
        fi
    done

    # ══ STEP 4: Write session token ═══════════════════════════════════════════
    _phase "STEP 4: Write session token"

    mkdir -p "$(dirname "${SESSION_FILE}")"
    printf '%s\n' "${TOKEN}" > "${SESSION_FILE}"
    chmod 600 "${SESSION_FILE}"
    _ok "Token written to ${SESSION_FILE}"

    # ══ STEP 5: Restart fwupd ═════════════════════════════════════════════════
    _phase "STEP 5: Restart fwupd"

    local FWUPD_SERVICE=""
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
    _detail "Service: ${FWUPD_SERVICE}"

    local RUNTIME_PRESERVED
    RUNTIME_PRESERVED="$(
        systemctl show "${FWUPD_SERVICE}" --property=RuntimeDirectoryPreserve 2>/dev/null \
        | cut -d= -f2
    )"
    _detail "RuntimeDirectoryPreserve: ${RUNTIME_PRESERVED}"

    _info "Restarting ${FWUPD_SERVICE} so the plugin reads the new token..."
    systemctl restart "${FWUPD_SERVICE}"

    if [[ "${RUNTIME_PRESERVED}" != "restart" && "${RUNTIME_PRESERVED}" != "yes" ]]; then
        _detail "RuntimeDirectory was cleared on restart — re-writing token."
        printf '%s\n' "${TOKEN}" > "${SESSION_FILE}"
        chmod 600 "${SESSION_FILE}"
    fi

    local i
    for i in {1..10}; do
        systemctl is-active --quiet "${FWUPD_SERVICE}" && break
        _detail "Waiting for ${FWUPD_SERVICE}... (${i}/10)"
        sleep 1
    done
    systemctl is-active --quiet "${FWUPD_SERVICE}" || {
        printf '%s[FATAL]%s  %s did not become active after restart.\n' \
            "$(_c '1;31')" "$(_c 0)" "${FWUPD_SERVICE}" >&2
        return 1
    }
    _ok "${FWUPD_SERVICE} is active."

    # ══ STEP 6: Verify ════════════════════════════════════════════════════════
    _phase "STEP 6: Verify"

    if fwupdmgr get-plugins 2>/dev/null | grep -q 'nvidia_oob_redfish'; then
        _ok "nvidia_oob_redfish plugin is loaded."
    else
        _warn "Plugin not found — check: journalctl -u ${FWUPD_SERVICE} -n 50 --no-pager"
    fi

    local OOB_COUNT
    OOB_COUNT="$(fwupdmgr get-devices 2>/dev/null | grep -c 'OOB-managed' || true)"
    if [[ "${OOB_COUNT}" -gt 0 ]]; then
        _ok "${OOB_COUNT} OOB-managed firmware component(s) discovered."
    else
        _warn "No OOB devices found — check: journalctl -u ${FWUPD_SERVICE} -n 80 --no-pager"
    fi

    _sep
    printf '   %-20s %s\n' "BMC host:"    "${BMC_HOST}"
    printf '   %-20s %s\n' "Username:"    "${BMC_USER}"
    printf '   %-20s %s\n' "TLS:"         "$([[ "${INSECURE}" == "1" ]] && echo "insecure (bringup)" || echo "verified")"
    printf '   %-20s %s\n' "OOB devices:" "${OOB_COUNT}"
    _sep
    _ok "Authentication complete.  Run fwupdmgr as normal."
    _sep
}

# ── entry point ────────────────────────────────────────────────────────────────
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # executed directly (sudo ./fwupd-nvidia-oob-env.sh)
    set -euo pipefail
    _OOB_SOURCED=0
    _oob_auth_main "$@"
else
    # sourced into current shell (source fwupd-nvidia-oob-env.sh)
    _OOB_SOURCED=1
    _oob_auth_main "$@"
    _oob_rc=$?
    unset -f _oob_auth_main _c _info _detail _ok _warn _sep _phase
    unset _OOB_SOURCED
    return ${_oob_rc}
fi
