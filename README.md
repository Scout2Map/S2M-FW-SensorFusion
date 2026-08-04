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

```bash
# pico-sdk 준비 (한 번만)
export PICO_SDK_PATH=/path/to/pico-sdk
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .

mkdir build && cd build
cmake ..
make -j4
# build/sensor_fusion.uf2 를 BOOTSEL 상태의 Pico2에 복사
```

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
