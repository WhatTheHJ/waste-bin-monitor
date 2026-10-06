#include <SoftwareSerial.h>
#include <WiFiEsp.h>
#include <MsTimer2.h>   // 깜빡임용 타이머 인터럽트 (Timer2 사용)

// ==================================================
// ESP-01
// ==================================================
#define WIFI_RX 8
#define WIFI_TX 9

SoftwareSerial espSerial(WIFI_RX, WIFI_TX);   // RX, TX
WiFiEspClient client;


// ==================================================
// HC-SR04
// ==================================================

// 쓰레기통 A
#define TRIG_A 7
#define ECHO_A 6

// 쓰레기통 B
#define TRIG_B 5
#define ECHO_B 4

// 쓰레기통 C
#define TRIG_C 3
#define ECHO_C 2


// ==================================================
// 상태 표시 RGB LED (통마다 1개, R·G 두 핀만 연결)
// R+G = 노랑. B 다리는 연결하지 않음
// ==================================================
#define LED_A_R A0
#define LED_A_G A1

#define LED_B_R A2
#define LED_B_G A3

#define LED_C_R A4
#define LED_C_G A5

// 공통 다리가 GND면 0 (캐소드 공통), 5V면 1 (애노드 공통)
#define LED_COMMON_ANODE 0

// 색 기준 (%)
#define LEVEL_GREEN_MAX  40   //  0 ~ 40 : 초록
#define LEVEL_YELLOW_MAX 75   // 41 ~ 75 : 노랑
#define LEVEL_RED_MAX    90   // 76 ~ 90 : 빨강, 91 ~ 100 : 빨강 깜빡임

#define BLINK_INTERVAL 300    // 깜빡임 간격 (ms)

const uint8_t LED_R_PINS[3] = { LED_A_R, LED_B_R, LED_C_R };
const uint8_t LED_G_PINS[3] = { LED_A_G, LED_B_G, LED_C_G };

// 통별 마지막 정상 적재량 (0~100). 측정에 실패하면 이 값을 그대로 유지한다
int ledPercent[3] = { 0, 0, 0 };

// 깜빡임은 타이머 인터럽트가 맡는다.
// 측정·Wi-Fi·서버 전송으로 loop가 몇 초씩 막혀 있어도 깜빡임이 끊기지 않는다.
volatile uint8_t blinkMask = 0;      // 깜빡여야 하는 통 (bit0=A, bit1=B, bit2=C)
volatile bool blinkOn = true;        // 지금 깜빡임 켜진 차례인지
volatile uint8_t *ledRPort[3];       // R 핀 출력 레지스터 (인터럽트에서 빠르게 쓰기용)
uint8_t ledRBit[3];


// ==================================================
// 쓰레기통 깊이
// 실제 모형 완성 후 이 두 값만 보정
// ==================================================
#define EMPTY_DISTANCE 18.0
#define FULL_DISTANCE   3.0


// ==================================================
// Wi-Fi 설정
// ==================================================
char ssid[] = "kcci603";
char pass[] = "@kcci603!";


// ==================================================
// Raspberry Pi TCP Server
// ==================================================
char server[] = "10.10.16.71";
int port = 5000;


// ==================================================
// 초음파 거리 측정 함수
// 성공하면 distance에 cm 값을 넣고 true, Echo를 못 받으면 false
// ==================================================
bool getDistance(int trigPin, int echoPin, float &distance)
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);

  // Echo를 못 받았으면
  if (duration == 0)
  {
    return false;
  }

  distance = duration * 0.0343 / 2.0;

  return true;
}


// ==================================================
// 거리 → 적재량 % 변환 (항상 0 ~ 100)
// ==================================================
int getPercent(float distance)
{
  float percent =
      (EMPTY_DISTANCE - distance)
      / (EMPTY_DISTANCE - FULL_DISTANCE)
      * 100.0;

  if (percent < 0)
    percent = 0;

  if (percent > 100)
    percent = 100;

  return (int)percent;
}


// ==================================================
// 통 하나 측정
// 실패하면 직전 정상 값을 그대로 쓴다 (처음부터 실패면 0)
// ==================================================
int measurePercent(int index, int trigPin, int echoPin)
{
  float distance;

  if (getDistance(trigPin, echoPin, distance))
  {
    ledPercent[index] = getPercent(distance);
  }
  else
  {
    Serial.print("WARN : bin ");
    Serial.print((char)('A' + index));
    Serial.println(" no echo, keep last value");
  }

  return ledPercent[index];
}


// ==================================================
// RGB LED 표시
// ==================================================
void setLed(int index, bool red, bool green)
{
  digitalWrite(LED_R_PINS[index], (red != LED_COMMON_ANODE) ? HIGH : LOW);
  digitalWrite(LED_G_PINS[index], (green != LED_COMMON_ANODE) ? HIGH : LOW);
}

// ledPercent 값에 맞춰 세 LED 색을 정한다. 측정할 때마다 한 번 부른다.
// 91% 이상인 통은 다음 측정에서 90% 이하가 나올 때까지 계속 깜빡인다.
void updateLeds()
{
  for (int i = 0; i < 3; i++)
  {
    int p = ledPercent[i];
    uint8_t bit = (uint8_t)(1 << i);

    if (p > LEVEL_RED_MAX)
    {
      setLed(i, blinkOn, false);        // 빨강 깜빡임 (이후 토글은 인터럽트가 함)
      blinkMask |= bit;
      continue;
    }

    blinkMask &= (uint8_t)~bit;         // 먼저 깜빡임을 끊고 색을 칠한다

    if (p <= LEVEL_GREEN_MAX)
      setLed(i, false, true);           // 초록
    else if (p <= LEVEL_YELLOW_MAX)
      setLed(i, true, true);            // 노랑
    else
      setLed(i, true, false);           // 빨강
  }
}

// BLINK_INTERVAL마다 타이머 인터럽트로 불린다.
// SoftwareSerial 수신을 방해하지 않도록 레지스터에 직접 써서 짧게 끝낸다.
void blinkTick()
{
  blinkOn = !blinkOn;
  bool level = (blinkOn != LED_COMMON_ANODE);

  for (int i = 0; i < 3; i++)
  {
    if (blinkMask & (1 << i))
    {
      if (level)
        *ledRPort[i] |= ledRBit[i];
      else
        *ledRPort[i] &= (uint8_t)~ledRBit[i];
    }
  }
}


// ==================================================
// Wi-Fi 연결
// ==================================================
void connectWiFi()
{
  Serial.print("Connecting to ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(ssid, pass);

    Serial.print(".");
    delay(3000);
  }

  Serial.println();
  Serial.println("WIFI CONNECTED");

  Serial.print("Arduino IP : ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}


// ==================================================
// Raspberry Pi TCP 전송
// ==================================================
void sendToServer(int a, int b, int c)
{
  Serial.print("Connecting Server ");

  if (client.connect(server, port))
  {
    Serial.println("OK");

    // Raspberry Pi로 보낼 데이터
    // SENSOR@82@37@91
    client.print("SENSOR@");
    client.print(a);
    client.print("@");
    client.print(b);
    client.print("@");
    client.print(c);
    client.print("\n");


    Serial.print("SEND : SENSOR@");
    Serial.print(a);
    Serial.print("@");
    Serial.print(b);
    Serial.print("@");
    Serial.println(c);


    // 현재 Raspberry Pi 서버가
    // 1회 수신 후 close() 하는 구조이므로
    // Arduino도 전송 후 연결 종료
    client.stop();
  }
  else
  {
    Serial.println("FAIL");
  }
}


// ==================================================
// SETUP
// ==================================================
void setup()
{
  Serial.begin(9600);

  // ★ ESP-01 실제 확인된 baud
  espSerial.begin(38400);

  WiFi.init(&espSerial);


  // --------------------------
  // 초음파센서 GPIO
  // --------------------------
  pinMode(TRIG_A, OUTPUT);
  pinMode(ECHO_A, INPUT);

  pinMode(TRIG_B, OUTPUT);
  pinMode(ECHO_B, INPUT);

  pinMode(TRIG_C, OUTPUT);
  pinMode(ECHO_C, INPUT);


  // --------------------------
  // RGB LED GPIO (처음엔 모두 끔)
  // --------------------------
  for (int i = 0; i < 3; i++)
  {
    pinMode(LED_R_PINS[i], OUTPUT);
    pinMode(LED_G_PINS[i], OUTPUT);

    ledRPort[i] = portOutputRegister(digitalPinToPort(LED_R_PINS[i]));
    ledRBit[i] = digitalPinToBitMask(LED_R_PINS[i]);
  }
  for (int i = 0; i < 3; i++)
    setLed(i, false, false);            // 첫 측정 전까지는 꺼 둔다

  MsTimer2::set(BLINK_INTERVAL, blinkTick);
  MsTimer2::start();


  Serial.println();
  Serial.println("============================");
  Serial.println(" FOOD WASTE SYSTEM START");
  Serial.println("============================");


  // --------------------------
  // ESP-01 확인
  // --------------------------
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println("ESP-01 NOT FOUND");

    while (true);
  }

  Serial.println("ESP-01 FOUND");


  // --------------------------
  // Wi-Fi 접속
  // --------------------------
  connectWiFi();
}


// ==================================================
// LOOP
// ==================================================
void loop()
{
  // --------------------------
  // 쓰레기통 A 측정
  // --------------------------
  int percentA = measurePercent(0, TRIG_A, ECHO_A);

  // 센서끼리 초음파 간섭 방지
  delay(60);


  // --------------------------
  // 쓰레기통 B 측정
  // --------------------------
  int percentB = measurePercent(1, TRIG_B, ECHO_B);

  delay(60);


  // --------------------------
  // 쓰레기통 C 측정
  // --------------------------
  int percentC = measurePercent(2, TRIG_C, ECHO_C);


  // --------------------------
  // RGB LED 갱신
  // --------------------------
  updateLeds();


  // --------------------------
  // Serial Monitor 출력
  // --------------------------
  Serial.println("----------------------------");

  Serial.print("A : ");
  Serial.print(percentA);
  Serial.print("%  ");

  Serial.print("B : ");
  Serial.print(percentB);
  Serial.print("%  ");

  Serial.print("C : ");
  Serial.print(percentC);
  Serial.println("%");


  // --------------------------
  // Wi-Fi 끊겼으면 재접속
  // --------------------------
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected");

    connectWiFi();
  }


  // --------------------------
  // Raspberry Pi로 전송
  // --------------------------
  sendToServer(
      percentA,
      percentB,
      percentC
  );


  // 3초마다 측정/전송
  delay(3000);
}