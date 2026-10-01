## 핀 할당

| 칩 핀 | 보드 표기 | 연결 대상 | 핀 기능 | GPIO mode | Pull | 초기 출력 | 속도 | User Label |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| PA0 | A0 | A통 HC-SR04 Trig | GPIO_Output | Output Push Pull | No pull | Low | Low | TRIG_A |
| PA1 | A1 | B통 HC-SR04 Trig | GPIO_Output | Output Push Pull | No pull | Low | Low | TRIG_B |
| PA4 | A2 | C통 HC-SR04 Trig | GPIO_Output | Output Push Pull | No pull | Low | Low | TRIG_C |
| PB10 | D6 | A통 HC-SR04 Echo | GPIO_Input | Input mode | No pull | - | - | ECHO_A |
| PC7 | D9 | B통 HC-SR04 Echo | GPIO_Input | Input mode | No pull | - | - | ECHO_B |
| PB6 | D10 | C통 HC-SR04 Echo | GPIO_Input | Input mode | No pull | - | - | ECHO_C |
| PA9 | D8 | HC-06 RXD | USART1_TX | Alternate Function Push Pull | No pull | - | Very High | - |
| PA10 | D2 | HC-06 TXD | USART1_RX | Alternate Function Push Pull | Pull-up | - | Very High | - |


## 모듈별 배선 요약

### HC-SR04 (×3)

| HC-SR04 핀 | A통 | B통 | C통 |
| --- | --- | --- | --- |
| VCC | 5V | 5V | 5V |
| Trig | A0 (PA0) | A1 (PA1) | A2 (PA4) |
| Echo | D6 (PB10) | D9 (PC7) | D10 (PB6) |
| GND | GND | GND | GND |

### HC-06 (×1)

| HC-06 핀 | 연결 |
| --- | --- |
| VCC | 5V |
| GND | GND |
| TXD | D2 (PA10, USART1_RX) |
| RXD | D8 (PA9, USART1_TX) |
