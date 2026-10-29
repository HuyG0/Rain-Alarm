#define BLYNK_TEMPLATE_ID "TMPL607_Se2va"
#define BLYNK_TEMPLATE_NAME "RainAlarm"
#define BLYNK_AUTH_TOKEN "1-ITSaaL5ZTKaKoXTMwq-qmefb6BOLaQ"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// ================= WIFI =================
char ssid[] = "...";          // <-- điền SSID WiFi của bạn
char pass[] = "12345679";     // <-- điền mật khẩu WiFi

// ================= CHÂN ESP32 =================
#define RAIN_PIN   34
#define SERVO_PIN  18
#define BTN_MODE   32
#define BTN_SERVO  33
#define LED_RAIN   19
#define LED_AUTO   23
#define LED_MANUAL 27

// ================= CẤU HÌNH =================
// true: giá trị analog giảm khi mưa
// false: giá trị analog tăng khi mưa
#define RAIN_WHEN_LOW true

#define SERVO_LEFT        0
#define SERVO_RIGHT     180
#define SERVO_STOP_US  1500   // nếu servo vẫn nhích: thử 1480 / 1490 / 1510 / 1520

#define RAIN_THRESHOLD_DEFAULT 2000
#define RAIN_CONFIRM_COUNT 2
#define SERVO_RUN_TIME_DEFAULT 3000UL   // 3 giây mặc định

const unsigned long debounceDelay = 180;

// ================= ĐỐI TƯỢNG =================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo rainServo;
BlynkTimer timer;

// ================= BIẾN HỆ THỐNG =================
int rainValue = 0;
int rainThreshold = RAIN_THRESHOLD_DEFAULT;
unsigned long servoRunTime = SERVO_RUN_TIME_DEFAULT;   // thời gian quay (ms) – chỉnh trên Blynk

bool autoMode = true;
bool servoOpen = true;
bool isRaining = false;

// Lọc cảm biến: cần 2 lần đọc liên tiếp cùng trạng thái
bool candidateRain = false;
int candidateCount = 0;
bool rainStateInitialized = false;

// Điều khiển servo theo thời gian
bool servoMoving = false;
unsigned long servoMoveStart = 0;

// Nút nhấn
bool lastModeButton = HIGH;
bool lastServoButton = HIGH;
unsigned long lastModeTime = 0;
unsigned long lastServoTime = 0;

// ================= SERVO =================
void servoAttachIfNeeded() {
  if (!rainServo.attached()) {
    rainServo.setPeriodHertz(50);
    rainServo.attach(SERVO_PIN, 500, 2400);
  }
}

// Dừng hẳn servo: gửi xung trung tính rồi ngắt PWM
void servoHalt() {
  if (rainServo.attached()) {
    rainServo.writeMicroseconds(SERVO_STOP_US);  // gửi xung dừng
    delay(30);                                    // đảm bảo xung được phát
    rainServo.detach();                           // ngắt hẳn PWM
  }
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);                   // giữ chân ở mức thấp
}

// open = true  : quay phải để mở
// open = false : quay trái để đóng
void setServo(bool open) {
  // Nếu đã đúng trạng thái đích và servo không đang quay → bỏ qua
  if (servoOpen == open && !servoMoving) {
    return;
  }

  servoOpen = open;
  servoAttachIfNeeded();
  rainServo.write(open ? SERVO_RIGHT : SERVO_LEFT);
  servoMoving = true;
  servoMoveStart = millis();

  Serial.println(open ? "SERVO: MỞ - quay phải" : "SERVO: ĐÓNG - quay trái");
}

// Dừng servo sau thời gian đã cài (không dùng delay dài)
void updateServo() {
  if (servoMoving && (millis() - servoMoveStart >= servoRunTime)) {
    servoMoving = false;
    servoHalt();
    Serial.println("SERVO: DỪNG");
  }
}

// ================= PHÁT HIỆN MƯA =================
bool detectRain(int value) {
  if (RAIN_WHEN_LOW) {
    return value < rainThreshold;
  }
  return value > rainThreshold;
}

// Chỉ chấp nhận trạng thái mới sau 2 lần đọc liên tiếp
bool updateRainState(bool rawRain) {
  if (!rainStateInitialized) {
    rainStateInitialized = true;
    candidateRain = rawRain;
    candidateCount = 1;
    if (!rawRain) {
      isRaining = false;
    }
    return false;
  }

  if (rawRain == isRaining) {
    candidateRain = rawRain;
    candidateCount = 0;
    return false;
  }

  if (rawRain != candidateRain) {
    candidateRain = rawRain;
    candidateCount = 1;
  } else {
    candidateCount++;
  }

  if (candidateCount >= RAIN_CONFIRM_COUNT) {
    bool oldState = isRaining;
    isRaining = candidateRain;
    candidateCount = 0;
    return oldState != isRaining;
  }
  return false;
}

// ================= LED =================
void updateLEDs() {
  digitalWrite(LED_RAIN, isRaining ? HIGH : LOW);
  digitalWrite(LED_AUTO, autoMode ? HIGH : LOW);
  digitalWrite(LED_MANUAL, autoMode ? LOW : HIGH);
}

// ================= LCD =================
void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("Rain:");
  lcd.print(rainValue);
  lcd.print("       ");

  lcd.setCursor(0, 1);
  lcd.print(autoMode ? "AUTO " : "MAN  ");
  lcd.print(isRaining ? "RAIN " : "DRY  ");
  lcd.print(servoOpen ? "OPEN " : "CLOSE");
}

// ================= BLYNK STATUS =================
void sendBlynkData() {
  if (!Blynk.connected()) return;

  Blynk.virtualWrite(V0, rainValue);
  Blynk.virtualWrite(V1, rainThreshold);
  Blynk.virtualWrite(V2, autoMode);
  Blynk.virtualWrite(V3, servoOpen);
  Blynk.virtualWrite(V6, servoRunTime / 1000);   // gửi về giây cho dễ nhìn

  String status = autoMode ? "AUTO " : "MANUAL ";
  status += isRaining ? "RAIN" : "DRY";
  status += servoOpen ? " | SERVO OPEN" : " | SERVO CLOSED";
  status += " | T=";
  status += String(servoRunTime / 1000);
  status += "s";
  Blynk.virtualWrite(V5, status);
}

// ================= ĐỌC CẢM BIẾN =================
void sendData() {
  rainValue = analogRead(RAIN_PIN);
  bool rawRain = detectRain(rainValue);
  bool stateChanged = updateRainState(rawRain);

  if (autoMode) {
    if (stateChanged) {
      if (isRaining) {
        // Khô → mưa: đóng (quay trái)
        setServo(false);
        Serial.println("CẢM BIẾN: BẮT ĐẦU MƯA");
      } else {
        // Mưa → khô: mở (quay phải)
        setServo(true);
        Serial.println("CẢM BIẾN: TRỜI TẠNH");
      }
    }
  }

  updateLEDs();
  updateLCD();
  sendBlynkData();
}

// ================= ÁP DỤNG AUTO =================
void applyAutomaticState() {
  // Khi chuyển sang Auto, đưa servo về trạng thái phù hợp với cảm biến hiện tại
  bool shouldBeOpen = !isRaining;
  if (servoOpen != shouldBeOpen) {
    setServo(shouldBeOpen);
  }
  updateLEDs();
  updateLCD();
  sendBlynkData();
}

// ================= KẾT NỐI LẠI BLYNK =================
void reconnectBlynk() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi chưa kết nối");
    return;
  }
  if (!Blynk.connected()) {
    Serial.println("Đang thử kết nối lại Blynk...");
    bool ok = Blynk.connect(500);
    Serial.println(ok ? "Blynk đã kết nối" : "Blynk chưa kết nối");
  }
}

// ================= BLYNK V1: NGƯỠNG MƯA =================
BLYNK_WRITE(V1) {
  rainThreshold = constrain(param.asInt(), 0, 4095);
  sendData();
}

// ================= BLYNK V2: AUTO / MANUAL =================
BLYNK_WRITE(V2) {
  bool requestedMode = param.asInt() == 1;
  if (requestedMode != autoMode) {
    autoMode = requestedMode;
    if (autoMode) {
      applyAutomaticState();
    } else {
      updateLEDs();
      updateLCD();
      sendBlynkData();
    }
  }
}

// ================= BLYNK V3: SERVO (chỉ Manual) =================
BLYNK_WRITE(V3) {
  if (!autoMode) {
    bool requestedOpen = param.asInt() == 1;
    if (requestedOpen != servoOpen) {
      setServo(requestedOpen);
      updateLCD();
      sendBlynkData();
    }
  }
}

// ================= BLYNK V6: THỜI GIAN QUAY SERVO (giây) =================
BLYNK_WRITE(V6) {
  int seconds = constrain(param.asInt(), 1, 30);   // giới hạn 1–30 giây
  servoRunTime = (unsigned long)seconds * 1000UL;
  Serial.print("Thời gian quay servo: ");
  Serial.print(seconds);
  Serial.println(" giây");
  sendBlynkData();
}

// ================= NÚT NHẤN =================
void handleButtons() {
  bool modeNow = digitalRead(BTN_MODE);
  bool servoNow = digitalRead(BTN_SERVO);
  unsigned long now = millis();

  // Nút MODE
  if (lastModeButton == HIGH && modeNow == LOW && now - lastModeTime >= debounceDelay) {
    lastModeTime = now;
    autoMode = !autoMode;
    Serial.println(autoMode ? "MODE: AUTO" : "MODE: MANUAL");
    if (autoMode) {
      applyAutomaticState();
    } else {
      updateLEDs();
      updateLCD();
      sendBlynkData();
    }
    if (Blynk.connected()) {
      Blynk.virtualWrite(V2, autoMode);
    }
  }

  // Nút SERVO chỉ hoạt động ở Manual
  if (lastServoButton == HIGH && servoNow == LOW && now - lastServoTime >= debounceDelay) {
    lastServoTime = now;
    if (!autoMode) {
      setServo(!servoOpen);
      updateLCD();
      sendBlynkData();
    } else {
      Serial.println("Chuyển sang MANUAL để điều khiển servo");
    }
  }

  lastModeButton = modeNow;
  lastServoButton = servoNow;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RAIN_PIN, INPUT);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_SERVO, INPUT_PULLUP);
  pinMode(LED_RAIN, OUTPUT);
  pinMode(LED_AUTO, OUTPUT);
  pinMode(LED_MANUAL, OUTPUT);

  analogReadResolution(12);

  // LCD I2C
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("RAIN SYSTEM");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  // Servo quay liên tục: khởi động ở trạng thái dừng hẳn
  servoMoving = false;
  servoOpen = true;
  servoAttachIfNeeded();
  servoHalt();

  // Wi-Fi và Blynk
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  // Đọc cảm biến mỗi giây
  timer.setInterval(1000L, sendData);
  // Thử kết nối Blynk mỗi 5 giây
  timer.setInterval(5000L, reconnectBlynk);

  Serial.println("=== RAIN ALARM STARTED ===");
  Serial.println("Servo sẽ dừng hẳn (detach) sau mỗi lần quay");
  sendData();
}

// ================= LOOP =================
void loop() {
  Blynk.run();
  timer.run();
  handleButtons();
  updateServo();
}

