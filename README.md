# Scout2Map — Sensor Fusion MCU (Raspberry Pi Pico 2)

RPi Pico 2(RP2350)에서 환경 센서를 읽어 USB CDC로 JSON 라인을 내보내는 펌웨어다.

## 디렉토리 구조

```
pico2_sensor_fusion/
├── CMakeLists.txt
├── README.md
├── main.c                      # 협조적 스케줄러, 엔트리 포인트
└── libs/
    ├── drivers/
    │   ├── aht21.c / aht21.h       # 온습도
    │   ├── ens160.c / ens160.h     # 가스/공기질
    │   ├── bh1750.c / bh1750.h     # 조도
    │   └── pms7003.c / pms7003.h   # 미세먼지
    ├── diag/
    │   └── i2c_scan.c / i2c_scan.h  # 부팅 시 I2C 버스 스캐너
    └── queue/
        └── line_queue.c / line_queue.h  # 단일 writer 출력 큐
```

헤더는 `libs`를 인클루드 루트로 삼아 `"drivers/aht21.h"`, `"queue/line_queue.h"` 형태로 참조한다.

## 배선

| 버스 | 핀 | 연결 | 비고 |
|---|---|---|---|
| I2C0 | SDA=GP4, SCL=GP5 | ENS160(0x53) + AHT21(0x38) | **3.3V 전용**, 5V 금지 |
| I2C1 | SDA=GP2, SCL=GP3 | BH1750(0x23) | 3.3V |
| UART0 | TX=GP0, RX=GP1 | PMS7003 (9600bps) | VBUS(5V) 급전 |

- Pico TX(GP0) → PMS7003 RX, Pico RX(GP1) → PMS7003 TX
- PMS7003의 SET/RESET은 미사용
- BH1750 ADDR 핀을 VCC에 붙였다면 `libs/drivers/bh1750.h`의 주소를 `0x5C`로 변경한다.

## 빌드

### pico-sdk 준비 (최초 1회)

SDK 위치를 모를 경우 다음으로 찾는다. `pico_sdk_init.cmake`가 있는 폴더가 SDK 루트다.

```bash
find / -name "pico_sdk_init.cmake" 2>/dev/null
echo $PICO_SDK_PATH
```

환경변수를 영구 등록한다.

```bash
echo 'export PICO_SDK_PATH=/path/to/pico-sdk' >> ~/.bashrc
source ~/.bashrc
```

**Pico 2(RP2350)는 SDK 2.0.0 이상이 필요하다.** 1.5.x 이하면 빌드되지 않는다.

```bash
grep -i version $PICO_SDK_PATH/pico_sdk_version.cmake

# 구버전일 경우 업데이트
cd $PICO_SDK_PATH && git fetch --all --tags && git checkout 2.1.1 && git submodule update --init
```

### 빌드 실행

```bash
cd pico2_sensor_fusion
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .

mkdir -p build && cd build
cmake ..
make -j4
ls -lh sensor_fusion.uf2
```

## 업로드

Pico2의 **BOOTSEL 버튼을 누른 채로 USB를 연결**하면 USB 대용량 저장장치로 인식된다.

```bash
lsblk                      # RPI-RP2 계열 장치가 보이면 정상
cp sensor_fusion.uf2 /media/$USER/RP2350/    # 라벨명은 lsblk로 확인
sync
```

자동 마운트가 되지 않으면 수동으로 마운트한다.

```bash
sudo mkdir -p /mnt/pico
sudo mount /dev/sdb1 /mnt/pico        # 장치명은 lsblk로 확인
sudo cp sensor_fusion.uf2 /mnt/pico/
sync && sudo umount /mnt/pico
```

복사 직후 Pico가 자동 재부팅되며 장치가 사라진다. 정상 동작이다.

### VMware 사용 시

가상머신 환경에서는 BOOTSEL 장치를 호스트가 선점하는 경우가 많다. 다음을 미리 설정한다.

- VM Settings → USB Controller → **USB 3.1** 선택
- "Show all USB input devices" 체크
- 장치 연결 시 팝업에서 **Virtual Machine** 선택

USB 전환이 번거로우면 빌드까지만 VM에서 하고, `.uf2` 파일을 공유폴더로 호스트에 꺼내 호스트에서 복사하는 방법도 무방하다.

## 동작 확인

```bash
ls /dev/ttyACM*
sudo usermod -aG dialout $USER    # 권한 오류 시. 재로그인 필요
```

시리얼 모니터로 접속한다.

```bash
# picocom 권장 (개행 처리가 자연스럽다)
picocom -b 115200 --imap lfcrlf /dev/ttyACM0
# 종료: Ctrl+A 다음 Ctrl+X

# screen 사용 시
screen /dev/ttyACM0 115200
# 종료: Ctrl+A 다음 K, 이어서 y
```

### 정상 출력 판별

**하트비트가 5초 간격으로 올라오면 펌웨어는 정상 동작 중이다.**

```json
{"src":"sys","uptime_ms":81554}
{"src":"sys","uptime_ms":86555}
{"src":"sys","uptime_ms":91555}
```

부팅 라인은 전원 인가 직후 한 번만 발행되므로, 시리얼 접속이 늦으면 놓칠 수 있다. Pico의 리셋 버튼을 누르면 다시 확인할 수 있다.

```json
{"src":"sys","event":"boot","aht21":true,"ens160":true,"bh1750":true}
```

각 필드가 `false`이면 해당 센서의 초기화가 실패한 것이며, 배선 또는 I2C 주소를 점검해야 한다. 센서를 아직 연결하지 않은 단계에서는 전부 `false`로 나오고 센서 데이터 라인도 발행되지 않는 것이 정상이다.

### 출력이 계단 모양으로 밀려 보이는 경우

`PICO_STDIO_ENABLE_CRLF_SUPPORT=0` 설정으로 `\r` 없이 `\n`만 전송하기 때문에 발생하는 터미널 표시 현상이다. 호스트 파서 동작에는 영향이 없으며, 오히려 JSON 라인 파싱에는 이 편이 깔끔하다. 화면상 정렬이 필요하면 `picocom --imap lfcrlf` 옵션을 사용한다.

### I2C 버스 스캔

`main.c`의 `ENABLE_I2C_SCAN`이 1이면 부팅 직후 두 버스의 `0x08`~`0x77` 구간을 훑어 응답하는 주소를 보고한다. 배선 문제와 주소 문제를 구분하는 가장 빠른 방법이다.

```json
{"src":"sys","event":"i2c_found","bus":0,"addr":"0x38","guess":"AHT21"}
{"src":"sys","event":"i2c_found","bus":0,"addr":"0x53","guess":"ENS160"}
{"src":"sys","event":"i2c_scan","bus":0,"count":2,"addrs":["0x38","0x53"]}
{"src":"sys","event":"i2c_scan","bus":1,"count":0,"addrs":[]}
```

`bus` 값은 0이 I2C0(GP4/GP5), 1이 I2C1(GP2/GP3)이다.

| 스캔 결과 | 원인 |
|---|---|
| 양쪽 버스 모두 `count:0` | 3.3V 전원 또는 GND 공통 연결 문제 |
| 한쪽 버스만 `count:0` | 해당 버스의 SDA/SCL 배선 또는 핀 번호 착오 |
| 주소는 잡히는데 예상과 다름 | 모듈의 ADDR 핀 설정 문제. `guess` 필드 참고 |
| 주소가 정상인데 센서는 `false` | 초기화 시퀀스 실패. 풀업 저항 또는 전원 안정성 점검 |

배선이 확정된 뒤에는 `ENABLE_I2C_SCAN`을 0으로 두어 부팅 시간을 줄인다.

### 문제 해결

| 증상 | 확인 사항 |
|---|---|
| `/dev/ttyACM*`이 없음 | VMware USB 연결 대상, `dmesg \| tail` 확인 |
| 아무 출력도 없음 | 보레이트 확인, Pico 리셋 후 재접속 |
| 하트비트만 나옴 | 정상. 센서 미연결 상태 |
| 특정 센서만 `false` | 해당 센서 배선, 풀업, I2C 주소 점검 |
| PMS7003 값이 안 올라옴 | 5V 급전 여부, TX/RX 교차 결선 확인 |
| `{"src":"sys","dropped":N}` 반복 | 발행 주기가 너무 빠르거나 호스트가 읽지 않는 상태 |

## 출력 프로토콜

한 줄 = 하나의 JSON 오브젝트, `\n` 종료. 센서별 독립 주기로 발행한다.

```json
{"src":"sys","event":"boot","aht21":true,"ens160":true,"bh1750":true}
{"src":"bh1750","lux":320.5}
{"src":"aht21","temp":24.53,"hum":55.21}
{"src":"ens160","eco2":450,"tvoc":12,"aqi":1,"valid":0}
{"src":"pms7003","pm1":5,"pm25":8,"pm10":10}
{"src":"sys","uptime_ms":5002}
{"src":"sys","dropped":3}
```

### 필드 설명

| src | 필드 | 단위 / 의미 |
|---|---|---|
| `bh1750` | `lux` | 조도 (lx) |
| `aht21` | `temp` / `hum` | °C / %RH |
| `ens160` | `eco2` | ppm |
| | `tvoc` | ppb |
| | `aqi` | 1~5 (낮을수록 좋음) |
| | `valid` | 0=정상, 1=워밍업, 2=초기시동, 3=무효 |
| `pms7003` | `pm1`/`pm25`/`pm10` | µg/m³ (대기환경 기준) |
| `sys` | `uptime_ms` | 부팅 후 경과 ms (5초마다) |
| | `dropped` | 큐 오버플로로 유실된 줄 수 |

### 발행 주기

| 센서 | 주기 | 근거 |
|---|---|---|
| BH1750 | 200ms (5Hz) | 변환시간 120ms |
| AHT21 | 1s | |
| ENS160 | 1s | 내부 알고리즘 갱신 주기 |
| PMS7003 | 최소 1s 간격, 프레임 도착 시 | 팬 방식 물리 한계 |

## 설계 노트

- **줄 깨짐 방지**: 모든 출력은 `lq_push()`로 링버퍼에 들어가고, 메인 루프의 `lq_flush()` 한 곳에서만 USB에 쓴다. 나중에 core1이나 IRQ에서 센서를 돌려도 줄이 섞이지 않는다.
- **ENS160 보상**: AHT21 실측 온습도를 매 측정마다 ENS160의 `TEMP_IN`/`RH_IN` 레지스터에 주입한다. 이걸 빼면 eCO2/TVOC 정확도가 크게 떨어진다.
- **타임스탬프 없음**: RPi5 수신 시각을 기준으로 삼는 설계라 Pico 쪽 타임스탬프는 넣지 않았다. USB CDC 레이턴시(수 ms)는 차체 속도 0.228m/s에서 mm 단위 오차이므로 무시할 수 있다.
- **`valid` 활용**: ENS160은 전원 인가 후 워밍업(1) → 초기시동(2) 구간을 거친다. RPi5 쪽 캐시에서 `valid >= 2`인 값은 마커에 반영하지 않는 편이 안전하다.
- **stdio_uart 비활성화**: pico-sdk 기본값은 `printf()` 출력을 GP0/GP1로도 내보내는데, 이 핀은 PMS7003이 점유한다. 끄지 않으면 디버그 바이트가 먼지 센서로 흘러들어가고 보레이트까지 덮어써 프레임이 전부 깨진다.

## 다음 단계

RPi5 쪽 수신 노드: 시리얼 → 최신값 캐시 → 이동거리 10cm 트리거로 스냅샷 마커 publish.
