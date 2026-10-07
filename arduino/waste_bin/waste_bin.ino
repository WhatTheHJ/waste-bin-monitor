#include <SoftwareSerial.h>

#include <WiFiEsp.h>

#include <MsTimer2.h>



// ==================================================

// ESP-01

// ==================================================

#define WIFI_RX 8

#define WIFI_TX 9



SoftwareSerial espSerial(WIFI_RX, WIFI_TX);

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

// RGB LED

//

// 통마다 RGB LED 1개

// R, G만 사용

//

// R + G = 노랑

// B는 사용하지 않음

// ==================================================

#define LED_A_R A0

#define LED_A_G A1



#define LED_B_R A2

#define LED_B_G A3



#define LED_C_R A4

#define LED_C_G A5





// 공통 캐소드 = 0

// 공통 애노드  = 1

#define LED_COMMON_ANODE 0





// ==================================================

// LED 상태 기준

// ==================================================

//

// 0 ~ 40%   : 초록

// 41 ~ 75%  : 노랑

// 76 ~ 90%  : 빨강

// 91 ~ 100% : 빨강 깜빡임

// ==================================================

#define LEVEL_GREEN_MAX   40

#define LEVEL_YELLOW_MAX  75

#define LEVEL_RED_MAX     90



#define BLINK_INTERVAL    300





const uint8_t LED_R_PINS[3] =

{

  LED_A_R,

  LED_B_R,

  LED_C_R

};



const uint8_t LED_G_PINS[3] =

{

  LED_A_G,

  LED_B_G,

  LED_C_G

};





// ==================================================

// 통별 마지막 정상 적재량

//

// 초음파 측정 실패 시 -1을 보내지 않고

// 직전 정상값을 계속 사용

//

// collection_history에서

// 센서 실패를 수거로 오판하는 것도 방지

// ==================================================

int ledPercent[3] =

{

  0,

  0,

  0

};





// ==================================================

// LED 깜빡임

// ==================================================

volatile uint8_t blinkMask = 0;

volatile bool blinkOn = true;



volatile uint8_t *ledRPort[3];

uint8_t ledRBit[3];





// ==================================================

// 쓰레기통 깊이

//

// 실제 모형 완성 후 여기만 보정

// ==================================================

#define EMPTY_DISTANCE 18.0

#define FULL_DISTANCE   3.0





// ==================================================

// Wi-Fi

// ==================================================

char ssid[] = "kcci603";

char pass[] = "CHANGE_ME";  // 팀 환경 Wi-Fi 비밀번호로 변경





// ==================================================

// Raspberry Pi TCP Server

//

// ★ 네 Raspberry Pi 주소

// ==================================================

char server[] = "10.10.16.70";



int port = 5000;





// ==================================================

// 초음파 거리 측정

//

// 성공:

//   distance에 cm 저장

//   true 반환

//

// 실패:

//   false 반환

// ==================================================

bool getDistance(

  int trigPin,

  int echoPin,

  float &distance

)

{

  digitalWrite(trigPin, LOW);

  delayMicroseconds(2);



  digitalWrite(trigPin, HIGH);

  delayMicroseconds(10);



  digitalWrite(trigPin, LOW);





  long duration =

    pulseIn(

      echoPin,

      HIGH,

      30000

    );





  // Echo 없음

  if (duration == 0)

  {

    return false;

  }





  distance =

    duration * 0.0343 / 2.0;





  return true;

}





// ==================================================

// 거리 → 적재량 %

// ==================================================

int getPercent(float distance)

{

  float percent =

      (EMPTY_DISTANCE - distance)

      /

      (EMPTY_DISTANCE - FULL_DISTANCE)

      *

      100.0;





  if (percent < 0)

  {

    percent = 0;

  }





  if (percent > 100)

  {

    percent = 100;

  }





  return (int)percent;

}





// ==================================================

// 통 하나 측정

//

// 정상:

//   새로운 % 저장

//

// 실패:

//   직전 정상값 유지

// ==================================================

int measurePercent(

  int index,

  int trigPin,

  int echoPin

)

{

  float distance;





  if (

    getDistance(

      trigPin,

      echoPin,

      distance

    )

  )

  {

    ledPercent[index] =

      getPercent(distance);

  }

  else

  {

    Serial.print("WARN : bin ");



    Serial.print(

      (char)('A' + index)

    );



    Serial.println(

      " no echo, keep last value"

    );

  }





  return ledPercent[index];

}





// ==================================================

// RGB LED 출력

// ==================================================

void setLed(

  int index,

  bool red,

  bool green

)

{

  digitalWrite(

    LED_R_PINS[index],

    (red != LED_COMMON_ANODE)

      ? HIGH

      : LOW

  );





  digitalWrite(

    LED_G_PINS[index],

    (green != LED_COMMON_ANODE)

      ? HIGH

      : LOW

  );

}





// ==================================================

// 포화도에 맞춰 LED 색 결정

// ==================================================

void updateLeds()

{

  for (int i = 0; i < 3; i++)

  {

    int p =

      ledPercent[i];



    uint8_t bit =

      (uint8_t)(1 << i);





    // ------------------------------------------

    // 91 ~ 100%

    // 빨강 깜빡임

    // ------------------------------------------

    if (p > LEVEL_RED_MAX)

    {

      setLed(

        i,

        blinkOn,

        false

      );



      blinkMask |= bit;



      continue;

    }





    // 깜빡임 해제

    blinkMask &=

      (uint8_t)~bit;





    // ------------------------------------------

    // 0 ~ 40%

    // 초록

    // ------------------------------------------

    if (p <= LEVEL_GREEN_MAX)

    {

      setLed(

        i,

        false,

        true

      );

    }





    // ------------------------------------------

    // 41 ~ 75%

    // 노랑 = R + G

    // ------------------------------------------

    else if (

      p <= LEVEL_YELLOW_MAX

    )

    {

      setLed(

        i,

        true,

        true

      );

    }





    // ------------------------------------------

    // 76 ~ 90%

    // 빨강

    // ------------------------------------------

    else

    {

      setLed(

        i,

        true,

        false

      );

    }

  }

}





// ==================================================

// Timer2 인터럽트

//

// 91% 이상 빨강 LED 깜빡임

// ==================================================

void blinkTick()

{

  blinkOn =

    !blinkOn;





  bool level =

    (

      blinkOn

      !=

      LED_COMMON_ANODE

    );





  for (int i = 0; i < 3; i++)

  {

    if (

      blinkMask

      &

      (1 << i)

    )

    {

      if (level)

      {

        *ledRPort[i] |=

          ledRBit[i];

      }

      else

      {

        *ledRPort[i] &=

          (uint8_t)~ledRBit[i];

      }

    }

  }

}





// ==================================================

// Wi-Fi 연결

// ==================================================

void connectWiFi()

{

  Serial.print(

    "Connecting to "

  );



  Serial.println(ssid);





  while (

    WiFi.status()

    !=

    WL_CONNECTED

  )

  {

    WiFi.begin(

      ssid,

      pass

    );





    Serial.print(".");





    delay(3000);

  }





  Serial.println();



  Serial.println(

    "WIFI CONNECTED"

  );





  Serial.print(

    "Arduino IP : "

  );



  Serial.println(

    WiFi.localIP()

  );





  Serial.print(

    "RSSI : "

  );



  Serial.print(

    WiFi.RSSI()

  );



  Serial.println(

    " dBm"

  );

}





// ==================================================

// Raspberry Pi TCP 전송

// ==================================================

void sendToServer(

  int a,

  int b,

  int c

)

{

  Serial.print(

    "Connecting Server "

  );





  if (

    client.connect(

      server,

      port

    )

  )

  {

    Serial.println("OK");





    // ==========================================

    // Raspberry Pi 전송

    //

    // SENSOR@82@37@91

    // ==========================================

    client.print(

      "SENSOR@"

    );



    client.print(a);



    client.print("@");



    client.print(b);



    client.print("@");



    client.print(c);



    client.print("\n");





    // Serial Monitor 확인

    Serial.print(

      "SEND : SENSOR@"

    );



    Serial.print(a);



    Serial.print("@");



    Serial.print(b);



    Serial.print("@");



    Serial.println(c);





    // 서버가 1회 수신 후

    // 연결을 닫는 구조

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

  // USB Serial Monitor

  Serial.begin(9600);





  // ==========================================

  // ESP-01

  //

  // 실제 확인된 AT baud = 38400

  // ==========================================

  espSerial.begin(38400);



  WiFi.init(

    &espSerial

  );





  // ==========================================

  // HC-SR04 GPIO

  // ==========================================

  pinMode(

    TRIG_A,

    OUTPUT

  );



  pinMode(

    ECHO_A,

    INPUT

  );





  pinMode(

    TRIG_B,

    OUTPUT

  );



  pinMode(

    ECHO_B,

    INPUT

  );





  pinMode(

    TRIG_C,

    OUTPUT

  );



  pinMode(

    ECHO_C,

    INPUT

  );





  // ==========================================

  // RGB LED GPIO

  // ==========================================

  for (int i = 0; i < 3; i++)

  {

    pinMode(

      LED_R_PINS[i],

      OUTPUT

    );



    pinMode(

      LED_G_PINS[i],

      OUTPUT

    );





    // Timer ISR에서 빠르게 쓰기 위해

    // 포트 레지스터 주소 저장

    ledRPort[i] =

      portOutputRegister(

        digitalPinToPort(

          LED_R_PINS[i]

        )

      );





    ledRBit[i] =

      digitalPinToBitMask(

        LED_R_PINS[i]

      );

  }





  // 첫 측정 전에는 LED OFF

  for (int i = 0; i < 3; i++)

  {

    setLed(

      i,

      false,

      false

    );

  }





  // ==========================================

  // 빨강 깜빡임 Timer

  // ==========================================

  MsTimer2::set(

    BLINK_INTERVAL,

    blinkTick

  );



  MsTimer2::start();





  Serial.println();



  Serial.println(

    "============================"

  );



  Serial.println(

    " FOOD WASTE SYSTEM START"

  );



  Serial.println(

    "============================"

  );





  // ==========================================

  // ESP-01 확인

  // ==========================================

  if (

    WiFi.status()

    ==

    WL_NO_SHIELD

  )

  {

    Serial.println(

      "ESP-01 NOT FOUND"

    );



    while (true);

  }





  Serial.println(

    "ESP-01 FOUND"

  );





  // ==========================================

  // Wi-Fi 접속

  // ==========================================

  connectWiFi();

}





// ==================================================

// LOOP

// ==================================================

void loop()

{

  // ==========================================

  // A 측정

  // ==========================================

  int percentA =

    measurePercent(

      0,

      TRIG_A,

      ECHO_A

    );





  // 센서 간 초음파 간섭 방지

  delay(60);





  // ==========================================

  // B 측정

  // ==========================================

  int percentB =

    measurePercent(

      1,

      TRIG_B,

      ECHO_B

    );





  delay(60);





  // ==========================================

  // C 측정

  // ==========================================

  int percentC =

    measurePercent(

      2,

      TRIG_C,

      ECHO_C

    );





  // ==========================================

  // RGB LED 갱신

  // ==========================================

  updateLeds();





  // ==========================================

  // Serial Monitor

  // ==========================================

  Serial.println(

    "----------------------------"

  );





  Serial.print(

    "A : "

  );



  Serial.print(

    percentA

  );



  Serial.print(

    "%  "

  );





  Serial.print(

    "B : "

  );



  Serial.print(

    percentB

  );



  Serial.print(

    "%  "

  );





  Serial.print(

    "C : "

  );



  Serial.print(

    percentC

  );



  Serial.println("%");





  // ==========================================

  // Wi-Fi 끊겼으면 재접속

  // ==========================================

  if (

    WiFi.status()

    !=

    WL_CONNECTED

  )

  {

    Serial.println(

      "WiFi disconnected"

    );



    connectWiFi();

  }





  // ==========================================

  // Raspberry Pi 전송

  // ==========================================

  sendToServer(

    percentA,

    percentB,

    percentC

  );





  // 3초마다 측정/전송

  delay(3000);

}