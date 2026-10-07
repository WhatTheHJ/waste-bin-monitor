# 음식물 쓰레기 관제 시스템

HC-SR04 적재량 센서, Arduino UNO, ESP-01, Raspberry Pi, MariaDB, Apache/PHP, Bluetooth HC-06, STM32 NUCLEO-F411RE를 연결한 음식물 쓰레기 관제 프로젝트입니다.

## 1. 최종 기능

- 쓰레기통 A/B/C 적재량을 HC-SR04로 측정
- Arduino RGB LED 상태 표시
  - 0~40%: 초록
  - 41~75%: 노랑
  - 76~90%: 빨강
  - 91~100%: 빨강 깜빡임
- Arduino → Raspberry Pi TCP 센서 데이터 전송
- MariaDB `food_bin`에 센서값 저장
- Apache/PHP 관리자 관제화면에서 현재 적재량, 상태, 최근 수거 기록 표시
- 일반 사용자 모바일 페이지에서 A/B/C 수거 요청 가능
- 적재량이 기준값보다 20%p 이상 급감한 상태가 2회 연속 확인되면 `collection_history`에 자동 수거 기록
- 사용자 수거 요청은 `request_history`에 저장
- Raspberry Pi → HC-06 → STM32로 상태/사용자 요청 전달
- STM32 출력
  - LCD1602: A/B/C 적재량 및 사용자 요청 표시
  - 8x8 Matrix: 75% 초과 위치 자동 깜빡임
  - Buzzer: 90% 이상 자동 경보
  - 사용자 요청 시: 부저 약 2초 + 요청 위치 약 5초 Matrix 알림 + LCD 요청 표시

## 2. 전체 구조

```text
HC-SR04 x3
   ↓
Arduino UNO ── RGB LED
   ↓ ESP-01 / Wi-Fi
Raspberry Pi TCP Server :5000
   ├─ MariaDB
   │   ├─ food_bin
   │   ├─ collection_history
   │   └─ request_history
   ├─ Apache/PHP
   │   ├─ index.php   관리자 관제
   │   └─ request.php 사용자 수거 요청
   └─ Bluetooth RFCOMM
          ↓
        HC-06
          ↓ USART1 9600
        STM32 NUCLEO-F411RE
          ├─ LCD1602
          ├─ 8x8 Matrix + 74HC595 x2
          └─ Buzzer
```

## 3. 폴더 구조

```text
food-waste-monitor/
├─ README.md
├─ FINAL_CHECKLIST.md
├─ .gitignore
├─ arduino/
│  └─ waste_bin.ino
├─ raspberrypi/
│  └─ food_server.c
├─ web/
│  ├─ index.php
│  └─ request.php
├─ db/
│  └─ schema.sql
├─ stm32/
│  ├─ waste_bin_monitor.ioc
│  ├─ README.md
│  ├─ Core/Src/
│  │  ├─ main.c
│  │  ├─ buzzer.c
│  │  ├─ dot_map.c
│  │  └─ lcd1602.c
│  └─ Core/Inc/
│     ├─ buzzer.h
│     ├─ dot_map.h
│     └─ lcd1602.h
└─ docs/
   └─ pinmap.md
```

## 4. 통신 프로토콜

```text
Arduino → Raspberry Pi
SENSOR@78@20@55\n
Raspberry Pi → STM32
STATUS@78@20@55\n
사용자 수거요청 → Raspberry Pi → STM32
REQUEST@A\n
REQUEST@B\n
REQUEST@C\n
```

문자열은 `@` 기준으로 파싱합니다.

## 5. Raspberry Pi

필요 패키지 예시:

```bash
sudo apt install mariadb-server libmariadb-dev apache2 php php-mysql
```

DB 스키마 적용:

```bash
mariadb -u root -p < db/schema.sql
```

`raspberrypi/food_server.c`에서 DB 비밀번호 `CHANGE_ME`를 팀 환경에 맞게 바꾼 뒤:

```bash
gcc food_server.c -o food_server $(mariadb_config --cflags --libs)
./food_server 5000
```

서버 확인:

```bash
ss -ltnp | grep 5000
```

HC-06 RFCOMM:

```bash
sudo rfcomm bind 0 <HC-06-MAC> 1
ls -l /dev/rfcomm0
```

Pi 재부팅 후 `/dev/rfcomm0`이 사라지면 RFCOMM bind를 다시 해야 할 수 있습니다.

## 6. Web

`web/index.php`, `web/request.php`의 DB 비밀번호 `CHANGE_ME`를 팀 환경에 맞게 바꾼 뒤 Apache DocumentRoot에 복사합니다.

```bash
sudo cp web/index.php /var/www/html/index.php
sudo cp web/request.php /var/www/html/request.php
```

접속:

```text
관리자: http://<PI_IP>/index.php
사용자: http://<PI_IP>/request.php
```

같은 네트워크의 PC, 휴대폰, LDPlayer Chrome에서 접근 가능합니다.

## 7. Arduino

`arduino/waste_bin.ino`에서 확인/수정:

- Wi-Fi SSID
- Wi-Fi 비밀번호 (`CHANGE_ME`)
- Raspberry Pi IP
- TCP 포트 5000
- ESP-01 baud 38400
- 실제 모형에 맞는 `EMPTY_DISTANCE`, `FULL_DISTANCE`

현재 핀맵은 `docs/pinmap.md` 참고.

## 8. STM32

공유본에는 이제 아래 파일이 모두 포함되어 있습니다.

```text
stm32/waste_bin_monitor.ioc
stm32/Core/Src/main.c
stm32/Core/Src/buzzer.c
stm32/Core/Src/dot_map.c
stm32/Core/Src/lcd1602.c
stm32/Core/Inc/buzzer.h
stm32/Core/Inc/dot_map.h
stm32/Core/Inc/lcd1602.h
```

핵심 설정:

- USART1: **9600 8N1**, USART1 Global Interrupt Enable (HC-06)
- USART2: **115200 8N1** (ST-LINK Virtual COM debug)
- I2C1: **100 kHz**
- HC-06 TXD → PA10/D2 / USART1_RX
- HC-06 RXD ← PA9/D8 / USART1_TX
- HC-06 VCC → 5V, GND → GND
- LCD1602 SCL → PB8/D15
- LCD1602 SDA → PB9/D14
- Buzzer → PA0 / TIM2_CH1
- Matrix DATA → PA7/D11
- Matrix CLK → PA6/D12
- Matrix LATCH → PB6/D10

`waste_bin_monitor.ioc` 공유본에는 USART1=9600, USART2=115200, I2C1=100kHz를 명시적으로 넣었습니다. 그래도 Generate Code 후에는 USART1이 9600인지 한 번 확인하는 것이 안전합니다.

CubeIDE에서는 반드시 **`waste_bin_monitor` 프로젝트를 선택해서 Build Project** 하세요. 다른 `stm-project`를 잘못 빌드하면 FreeRTOS/cmsis_os.h 관련 에러가 날 수 있습니다.

## 9. DB 테이블

- `food_bin`: 실시간 A/B/C 적재량
- `collection_history`: 자동 수거 판정 기록
- `request_history`: 사용자 수거 요청 기록

전체 생성문은 `db/schema.sql` 참고.

## 10. 발표/시연 권장 순서

1. Pi에서 RFCOMM 확인 후 `food_server` 실행
2. Arduino 전원 연결 및 Wi-Fi/TCP 연결 확인
3. 센서 A에 손을 가까이/멀리 움직여 적재량 변화 확인
4. 관리자 웹에서 값과 색상이 갱신되는지 확인
5. STM32 LCD/Matrix/Buzzer가 동일한 값에 반응하는지 확인
6. 90% 이상으로 만들어 자동 부저 경보 확인
7. 사용자 `request.php`에서 A 수거 요청
8. `request_history` 저장 + Pi `REQUEST@A` 로그 확인
9. STM32에서 부저/LCD/Matrix 사용자 요청 알림 확인
10. 고적재량 상태에서 20%p 이상 급감을 2회 연속 만들어 `collection_history` 확인

## 11. 자주 발생한 문제

### `bind: Address already in use`

```bash
sudo fuser -k 5000/tcp
./food_server 5000
```

### `Bluetooth open: No such file or directory`

`/dev/rfcomm0`이 없는 상태이므로 RFCOMM bind를 다시 수행합니다.

### STM32 Bluetooth 수신 안 됨

USART1이 **9600**인지 가장 먼저 확인합니다.

### CubeIDE에서 `cmsis_os.h` 오류

`waste_bin_monitor`가 아니라 다른 FreeRTOS 프로젝트를 빌드하고 있는지 확인합니다.

## 12. GitHub 업로드 주의

공유본은 비밀번호를 `CHANGE_ME`로 둔 상태입니다. 실제 Wi-Fi/DB 비밀번호를 공개 저장소에 커밋하지 마세요.
