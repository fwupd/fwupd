# lxs-touch

## 개요

LX Semicon SW42101 터치 컨트롤러의 펌웨어 업데이트를 지원하는 fwupd 플러그인입니다.
HID RAW 인터페이스(hidraw)를 통해 디바이스와 통신하며, SWIP/DFUP 두 가지 프로토콜 모드를 처리합니다.

## 지원 디바이스

| 모드 | VID | PID | 설명 |
|------|-----|-----|------|
| SWIP (일반) | `0x1FD2` | `0xB011` | 정상 동작 모드 (SW42101) |
| DFUP (업데이트) | `0x29BD` | `0x5357` | 부트로더/업데이트 모드 |
| DFUP (업데이트, 대체) | `0x1FD2` | `0x5357` | 부트로더/업데이트 모드 (대체 VID) |

## 동작 방식

### 프로토콜 모드

- **SWIP (Software In-Protocol)**: 터치 정상 동작 모드. 버전 정보 및 패널 정보 조회가 가능합니다.
- **DFUP (Device Firmware Upgrade Protocol)**: 펌웨어 업데이트 전용 부트로더 모드.

### 펌웨어 업데이트 흐름

1. **Setup**: 디바이스 연결 시 프로토콜 모드를 확인하고 버전 및 패널 정보를 읽습니다.
2. **Detach**: SWIP 모드에서 DFUP 모드로 전환(`FU_LXSTOUCH_REG_CTRL_SETTER`, mode=`0x02`)하고 재연결을 대기합니다.
3. **Write**: DFUP 모드에서 청크 단위로 플래시에 펌웨어를 기록합니다.
4. **Attach**: 펌웨어 기록 완료 후 워치독 리셋(`CMD_WATCHDOG_RESET`)으로 디바이스를 재시작합니다.

### 플래시 쓰기 모드

| 모드 | 청크 크기 | 전송 단위 | CRC 검증 |
|------|----------|----------|---------|
| Normal mode | 128 bytes | 16 bytes | 없음 |
| 4K mode | 4096 bytes | 48 bytes | 있음 (실패 시 최대 5회 재시도) |

4K 모드 지원 여부는 디바이스 쿼리(`CMD_FLASH_4KB_UPDATE_MODE`)를 통해 자동 감지됩니다.

## 펌웨어 포맷

| 타입 | 크기 | 플래시 오프셋 | 설명 |
|------|------|------------|------|
| Application-only | 112 KB (`0x1C000`) | `0x4000` | 애플리케이션 영역만 포함 |
| Boot + Application | 128 KB (`0x20000`) | `0x0000` | 부트로더 + 애플리케이션 전체 |

## 버전 형식

버전은 4개 필드로 구성됩니다:

```
{boot_ver}.{core_ver}.{app_ver}.{param_ver}
```

예: `1.2.3.4`

## 통신 프로토콜

HID Feature Report(Report ID `0x09`, 버퍼 64바이트)를 사용합니다.

### 패킷 구조 (`FuStructLxstouchPacket`)

| 필드 | 크기 | 설명 |
|------|------|------|
| `report_id` | 1 byte | 항상 `0x09` |
| `flag` | 1 byte | `0x68` = Write, `0x69` = Read |
| `length_lo` | 1 byte | 데이터 길이 하위 바이트 |
| `length_hi` | 1 byte | 데이터 길이 상위 바이트 |
| `command_hi` | 1 byte | 레지스터 주소 상위 바이트 |
| `command_lo` | 1 byte | 레지스터 주소 하위 바이트 |
| `data` | N bytes | 페이로드 (C에서 `memcpy`로 처리) |

### 주요 레지스터 주소

| 레지스터 | 주소 | 설명 |
|----------|------|------|
| `REG_INFO_PANEL` | `0x0110` | 패널 해상도 및 노드 정보 |
| `REG_INFO_VERSION` | `0x0120` | 펌웨어 버전 정보 |
| `REG_INFO_INTEGRITY` | `0x0140` | 무결성 정보 |
| `REG_INFO_INTERFACE` | `0x0150` | 프로토콜 모드 식별 |
| `REG_CTRL_GETTER` | `0x0600` | Ready 상태 조회 |
| `REG_CTRL_SETTER` | `0x0610` | 동작 모드 설정 |
| `REG_CTRL_DFUP_FLAG` | `0x0623` | DFUP 플래그 |
| `REG_FLASH_IAP_CTRL_CMD` | `0x1400` | 플래시 IAP 명령 |
| `REG_PARAMETER_BUFFER` | `0x6000` | 데이터 전송 버퍼 |

## 파일 구성

| 파일 | 설명 |
|------|------|
| `fu-lxs-touch-plugin.c` | 플러그인 등록, hidraw 서브시스템 및 GType 등록 |
| `fu-lxs-touch-device.c` | 디바이스 동작 로직 (setup, detach, write, attach) |
| `fu-lxs-touch-firmware.c` | 펌웨어 파일 파싱 및 크기/오프셋 결정 |
| `fu-lxs-touch.rs` | 프로토콜 상수 및 패킷 구조체 정의 (Rust struct-generator) |
| `lxstouch.quirk` | 지원 디바이스 VID/PID 및 메타데이터 quirk 설정 |
| `meson.build` | 빌드 설정 |

## 빌드

이 플러그인은 Linux 전용(`host_machine.system() == 'linux'`)이며, fwupd 빌드 시스템에 자동 포함됩니다.

```bash
# 특정 플러그인만 테스트
fwupdtool --plugins lxstouch get-devices --verbose
```
