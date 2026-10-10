// ============================================================
// MeteoLink_mega.ino
// ============================================================
// Arduino Mega 2560 — основной контроллер метеостанции.
//
//  * Управляет дисплеем ILI9488 (480x320)
//  * Читает RTC DS1302 и DHT11
//  * Общается с ESP8266 по Serial1 (пины 18/19)
//  * Принимает точное время от ESP и обновляет RTC
//  * Отправляет показания датчиков в ESP для веб-интерфейса
//  * Сообщает свою версию прошивки (для OTA)
// ============================================================

#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include <Wire.h>
#include <RtcDS1302.h>
#include <DHT.h>
#include <RussianFont.h>
#include <ArduinoJson.h>

// ============================================================
// ВЕРСИЯ ПРОШИВКИ MEGA
// Меняйте при обновлении. ESP сравнивает эту версию с тегом
// релиза на GitHub и решает, надо ли прошивать Mega.
// ============================================================
#define MEGA_VERSION "1.0.0"

// ============================================================
// ДИСПЛЕЙ И ЦВЕТОВАЯ ПАЛИТРА (RGB565)
// ============================================================
LCDWIKI_KBV tft(ILI9488, A3, A2, A1, A0, A4);
RussianFont rus(&tft);

#define COLOR_BG           0x0842
#define COLOR_CARD_BG      0x10A4
#define COLOR_CARD_BORDER  0x2125
#define COLOR_ACCENT       0x139F
#define COLOR_WHITE        0xFFFF
#define COLOR_TEXT_WHITE   0xFFFF
#define COLOR_TEXT_GRAY    0x9CD3
#define COLOR_YELLOW       0xFDE0
#define COLOR_CYAN         0x07FF
#define COLOR_BLACK        0x0000
#define COLOR_BLUE_ICON    0x32DF
#define COLOR_ALICE_PURPLE 0x91F9

// ============================================================
// RTC DS1302
// ============================================================
#define DS1302_CLK_PIN 30
#define DS1302_DAT_PIN 31
#define DS1302_RST_PIN 32

ThreeWire myWire(DS1302_DAT_PIN, DS1302_CLK_PIN, DS1302_RST_PIN);
RtcDS1302<ThreeWire> Rtc(myWire);

// ============================================================
// DHT11
// ============================================================
#define DHTPIN  33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ============================================================
// ДАННЫЕ И ПЕРЕМЕННЫЕ
// ============================================================
float temp_in   = 0.0f;
float hum_in    = 0.0f;
float press_hpa = 750.0f;   // TODO: заменить на реальный датчик давления

int last_minute = -1;
int last_day    = -1;

uint32_t nextAliceAnimTime = 0;

const char* daysOfWeek[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
const char* monthsNames[] = {
  "", "Января", "Февраля", "Марта", "Апреля", "Мая", "Июня",
  "Июля", "Августа", "Сентября", "Октября", "Ноября", "Декабря"
};

// ============================================================
// НАСТРОЙКИ, ПРИХОДЯЩИЕ ОТ ESP
// ============================================================
bool  cfg_nightMode  = false;
int   cfg_nightStart = 23;
int   cfg_nightEnd   = 7;
bool  cfg_useF       = false;
bool  cfg_use12h     = false;
char  cfg_region[32] = "Europe/Moscow";

// ============================================================
// СВЯЗЬ С ESP8266 (Serial1 = пины 18/19)
// ============================================================
String  espBuffer = "";
unsigned long lastEspSend   = 0;   // когда в последний раз отправляли датчики
unsigned long lastHelloSend = 0;   // когда в последний раз отправляли hello
bool  espSeen = false;             // была ли хоть одна команда от ESP

// ============================================================
// ПРОТОТИПЫ
// ============================================================
void initRTC();
void initDHT();
void initLCD();
void splashAnimation();

void drawMainLayout();
void updateTime();
void updateSensors();
void runAliceAnimation();
void syncTimeFromUSB();

void drawWeatherIcon(int x, int y, int type);
void drawHourlyCards();
void drawCalendarWidget(int year, int month, int day);
void drawAstronomyWidget();
void drawWeeklyForecastWidget();

void sendHelloToEsp();
void sendSensorsToEsp();
void readFromEsp();

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);       // USB-отладка
  delay(500);
  Serial.println(F("=== MeteoLink Mega Starting ==="));
  Serial.print(F("Mega firmware version: "));
  Serial.println(MEGA_VERSION);

  Serial1.begin(9600);        // UART к ESP (пины 18/19)
  Serial.println(F("Serial1 (ESP link) initialized at 9600 baud"));

  randomSeed(analogRead(A5));

  initRTC();
  initDHT();
  initLCD();

  // Ждём 3 секунды возможной синхронизации от ПК (как было)
  Serial.println(F("Waiting for time sync from PC (3s)..."));
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    syncTimeFromUSB();
  }

  splashAnimation();
  drawMainLayout();

  nextAliceAnimTime = millis() + random(10, 31) * 1000L;

  // Сообщаем ESP свою версию — она поймёт, что Mega жива и какая у неё прошивка
  sendHelloToEsp();

  Serial.println(F("=== Ready ==="));
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // ----- Приём команд от ESP -----
  readFromEsp();

  // ----- Периодически отправляем hello (каждые 30 сек) -----
  // Это heartbeat: ESP знает, что Mega жива и видит её версию
  if (millis() - lastHelloSend >= 30000UL) {
    lastHelloSend = millis();
    sendHelloToEsp();
  }

  // ----- Раз в 5 секунд отправляем показания датчиков -----
  if (millis() - lastEspSend >= 5000UL) {
    lastEspSend = millis();
    sendSensorsToEsp();
  }

  // ----- Обработка USB Serial (синхронизация времени с ПК) -----
  syncTimeFromUSB();

  // ----- Опрос DHT11 каждые 2 секунды -----
  static uint32_t lastDHT = 0;
  uint32_t now = millis();
  if (now - lastDHT >= 2000UL) {
    lastDHT = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temp_in = t;
      hum_in  = h;
    }
  }

  // ----- Анимация Алисы -----
  if (now >= nextAliceAnimTime) {
    runAliceAnimation();
    nextAliceAnimTime = millis() + random(10, 31) * 1000L;
  }

  // ----- Обновление дисплея -----
  updateTime();
  updateSensors();

  delay(50);
}

// ============================================================
// СВЯЗЬ С ESP: ОТПРАВКА HELLO
// ============================================================
void sendHelloToEsp() {
  JsonDocument doc;
  doc["type"]    = "hello";
  doc["version"] = MEGA_VERSION;
  doc["uptime"]  = millis() / 1000UL;
  String out;
  serializeJson(doc, out);
  Serial1.println(out);
  Serial.print(F("[ESP] -> "));
  Serial.println(out);
}

// ============================================================
// СВЯЗЬ С ESP: ОТПРАВКА ДАТЧИКОВ
// ============================================================
void sendSensorsToEsp() {
  JsonDocument doc;
  doc["type"]    = "sensor";
  doc["temp"]    = temp_in;
  doc["hum"]     = hum_in;
  doc["press"]   = press_hpa;
  doc["version"] = MEGA_VERSION;
  String out;
  serializeJson(doc, out);
  Serial1.println(out);
  // Не логируем каждый пакет, чтобы не засорять USB-монитор
  // Serial.print(F("[ESP] -> ")); Serial.println(out);
}

// ============================================================
// СВЯЗЬ С ESP: ПРИЁМ КОМАНД
// ============================================================
void readFromEsp() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      if (espBuffer.length() > 0) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, espBuffer);
        if (err) {
          Serial.print(F("[ESP] JSON error: "));
          Serial.println(err.c_str());
        } else {
          const char* type = doc["type"] | "";
          espSeen = true;

          // ---- Команда "time": установить RTC ----
          if (strcmp(type, "time") == 0) {
            int y  = doc["y"]  | 2024;
            int mo = doc["mo"] | 1;
            int d  = doc["d"]  | 1;
            int h  = doc["h"]  | 0;
            int m  = doc["m"]  | 0;
            int s  = doc["s"]  | 0;

            if (y >= 2020 && y <= 2099 && mo >= 1 && mo <= 12 &&
                d >= 1 && d <= 31 && h >= 0 && h < 24 &&
                m >= 0 && m < 60 && s >= 0 && s < 60) {

              Rtc.SetDateTime(RtcDateTime(y, mo, d, h, m, s));
              Rtc.SetIsRunning(true);
              last_minute = -1;   // форсируем перерисовку
              last_day    = -1;
              Serial.println(F("[ESP] RTC synced from network time"));
            } else {
              Serial.println(F("[ESP] time: bad range, ignored"));
            }
          }

          // ---- Команда "settings": применить настройки ----
          else if (strcmp(type, "settings") == 0) {
            const char* region = doc["region"] | "Europe/Moscow";
            strncpy(cfg_region, region, sizeof(cfg_region) - 1);
            cfg_region[sizeof(cfg_region) - 1] = '\0';

            cfg_nightMode  = doc["nightMode"]  | false;
            cfg_nightStart = doc["nightStart"] | 23;
            cfg_nightEnd   = doc["nightEnd"]   | 7;
            cfg_useF       = doc["useF"]       | false;
            cfg_use12h     = doc["use12h"]     | false;

            Serial.print(F("[ESP] settings applied: region="));
            Serial.print(cfg_region);
            Serial.print(F(" night="));
            Serial.print(cfg_nightStart);
            Serial.print(F("-"));
            Serial.print(cfg_nightEnd);
            Serial.print(F(" useF="));
            Serial.println(cfg_useF);

            // TODO: применить настройки к интерфейсу дисплея
            // Например, перерисовать виджеты, если формат °F
            last_minute = -1;
            last_day    = -1;
          }

          // ---- Неизвестная команда ----
          else {
            Serial.print(F("[ESP] unknown cmd: "));
            Serial.println(type);
          }
        }
        espBuffer = "";
      }
    } else if (c != '\r') {
      espBuffer += c;
      if (espBuffer.length() > 256) {
        espBuffer = "";
        Serial.println(F("[ESP] buffer overflow, cleared"));
      }
    }
  }
}

// ============================================================
// ИНИЦИАЛИЗАЦИЯ
// ============================================================
void initRTC() {
  Serial.println(F("[RTC] Init DS1302"));
  Rtc.Begin();

  if (!Rtc.IsDateTimeValid() || !Rtc.GetIsRunning()) {
    Serial.println(F("[RTC] Invalid state — setting compile time"));
    Rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
    Rtc.SetIsRunning(true);
  } else {
    RtcDateTime now = Rtc.GetDateTime();
    Serial.print(F("[RTC] Current: "));
    Serial.print(now.Year()); Serial.print('-');
    Serial.print(now.Month()); Serial.print('-');
    Serial.print(now.Day()); Serial.print(' ');
    Serial.print(now.Hour()); Serial.print(':');
    Serial.print(now.Minute()); Serial.print(':');
    Serial.println(now.Second());
  }
}

void initDHT() {
  Serial.println(F("[DHT] Init DHT11"));
  dht.begin();
}

void initLCD() {
  Serial.println(F("[LCD] Init ILI9488"));
  tft.Init_LCD();
  tft.Set_Rotation(3);
  tft.Invert_Display(true);
  tft.Fill_Screen(COLOR_BG);

  tft.Set_Text_Mode(true);
  rus.setTransparent(true);
}

// ============================================================
// СИНХРОНИЗАЦИЯ ВРЕМЕНИ С ПК ЧЕРЕЗ USB
// Формат: "SET YYYY MM DD hh mm ss" или "GET"
// ============================================================
void syncTimeFromUSB() {
  if (!Serial.available()) return;

  char buf[48];
  int idx = 0;
  uint32_t t0 = millis();

  while (millis() - t0 < 200 && idx < 47) {
    if (!Serial.available()) { delay(1); continue; }
    char c = Serial.read();
    if (c == '\n' || c == '\r') break;
    buf[idx++] = c;
  }
  buf[idx] = '\0';
  if (idx == 0) return;

  int y, mo, d, h, mi, s;
  if (sscanf(buf, "SET %d %d %d %d %d %d", &y, &mo, &d, &h, &mi, &s) == 6) {
    if (y >= 2000 && y <= 2099 && mo >= 1 && mo <= 12 &&
        d >= 1 && d <= 31 && h >= 0 && h < 24 &&
        mi >= 0 && mi < 60 && s >= 0 && s < 60) {

      Rtc.SetDateTime(RtcDateTime(y, mo, d, h, mi, s));
      Rtc.SetIsRunning(true);
      last_minute = -1;
      last_day    = -1;
      Serial.println(F("OK TIME SET"));
    } else {
      Serial.println(F("ERR BAD RANGE"));
    }
    return;
  }

  if (strcmp(buf, "GET") == 0) {
    RtcDateTime now = Rtc.GetDateTime();
    char out[48];
    sprintf(out, "TIME %04d %02d %02d %02d %02d %02d",
            now.Year(), now.Month(), now.Day(),
            now.Hour(), now.Minute(), now.Second());
    Serial.println(out);
    return;
  }

  Serial.println(F("ERR UNKNOWN CMD"));
}

// ============================================================
// ЗАСТАВКА (Алиса)
// ============================================================
void splashAnimation() {
  tft.Fill_Screen(COLOR_BLACK);

  const int cx_left  = 180;
  const int cx_right = 300;
  const int cy_eyes  = 160;
  const int eyeW     = 40;

  auto drawEye = [&](int cx, int cy, int h, uint16_t color) {
    tft.Set_Draw_color(color);
    tft.Set_Text_colour(color);
    if (h <= 3) {
      tft.Fill_Rect(cx - eyeW / 2, cy - 1, eyeW, 3, color);
    } else {
      int r = (h / 2 < 5) ? (h / 2) : 5;
      tft.Fill_Round_Rectangle(cx - eyeW / 2, cy - h / 2,
                               cx + eyeW / 2, cy + h / 2, r);
    }
  };

  auto shrinkEye = [&](int cx, int cy, int oldH, int newH, uint16_t color) {
    if (newH >= oldH) { drawEye(cx, cy, newH, color); return; }
    int x0 = cx - eyeW / 2 - 2;
    int y0 = cy - oldH / 2 - 2;
    tft.Fill_Rect(x0, y0, eyeW + 4, oldH + 4, COLOR_BLACK);
    drawEye(cx, cy, newH, color);
  };

  int prevL = 0, prevR = 0;

  drawEye(cx_left,  cy_eyes, 4, COLOR_ALICE_PURPLE);
  drawEye(cx_right, cy_eyes, 4, COLOR_ALICE_PURPLE);
  prevL = prevR = 4;
  delay(500);

  for (int h = 8; h <= 20; h += 4) {
    drawEye(cx_left,  cy_eyes, h, COLOR_ALICE_PURPLE);
    drawEye(cx_right, cy_eyes, h, COLOR_ALICE_PURPLE);
    prevL = prevR = h;
    delay(40);
  }
  delay(150);

  for (int h = 16; h >= 8; h -= 4) {
    shrinkEye(cx_left,  cy_eyes, prevL, h, COLOR_ALICE_PURPLE);
    shrinkEye(cx_right, cy_eyes, prevR, h, COLOR_ALICE_PURPLE);
    prevL = prevR = h;
    delay(30);
  }
  delay(150);

  for (int h = 14; h <= 56; h += 6) {
    drawEye(cx_left,  cy_eyes, h, COLOR_ALICE_PURPLE);
    drawEye(cx_right, cy_eyes, h, COLOR_ALICE_PURPLE);
    prevL = prevR = h;
    delay(30);
  }
  delay(300);

  for (int h = 48; h >= 4; h -= 8) {
    shrinkEye(cx_right, cy_eyes, prevR, h, COLOR_ALICE_PURPLE);
    prevR = h;
    delay(15);
  }
  delay(250);

  for (int h = 12; h <= 56; h += 8) {
    drawEye(cx_right, cy_eyes, h, COLOR_ALICE_PURPLE);
    prevR = h;
    delay(15);
  }
  delay(400);

  tft.Fill_Screen(COLOR_BG);
}

// ============================================================
// АНИМАЦИЯ АЛИСЫ (короткая)
// ============================================================
void runAliceAnimation() {
  const int cx       = 113;
  const int cy       = 56;
  const int eyeDist  = 28;
  const int eyeHalfW = 10;

  tft.Fill_Rect(4, 26, 224, 60, COLOR_BG);

  const int clearX = 52;
  const int clearY = 34;
  const int clearW = 124;
  const int clearH = 44;

  auto drawEyes = [&](int offset, int h, uint16_t color) {
    tft.Set_Draw_color(color);
    tft.Set_Text_colour(color);
    if (h <= 3) {
      tft.Fill_Rect(cx - eyeDist - eyeHalfW + offset, cy - 1, 2 * eyeHalfW, 3, color);
      tft.Fill_Rect(cx + eyeDist - eyeHalfW + offset, cy - 1, 2 * eyeHalfW, 3, color);
    } else {
      int r = (h / 2 < 4) ? (h / 2) : 4;
      tft.Fill_Round_Rectangle(cx - eyeDist - eyeHalfW + offset, cy - h / 2,
                               cx - eyeDist + eyeHalfW + offset, cy + h / 2, r);
      tft.Fill_Round_Rectangle(cx + eyeDist - eyeHalfW + offset, cy - h / 2,
                               cx + eyeDist + eyeHalfW + offset, cy + h / 2, r);
    }
  };

  auto clearEyes = [&]() {
    tft.Fill_Rect(clearX, clearY, clearW, clearH, COLOR_BG);
  };

  for (int h = 2; h <= 24; h += 4) {
    drawEyes(0, h, COLOR_ALICE_PURPLE);
    delay(30);
  }
  delay(150);

  for (int offset = -2; offset >= -14; offset -= 2) {
    clearEyes(); drawEyes(offset, 24, COLOR_ALICE_PURPLE);
    delay(20);
  }
  delay(250);

  for (int offset = -12; offset <= 14; offset += 3) {
    clearEyes(); drawEyes(offset, 24, COLOR_ALICE_PURPLE);
    delay(20);
  }
  delay(250);

  for (int offset = 12; offset >= 0; offset -= 2) {
    clearEyes(); drawEyes(offset, 24, COLOR_ALICE_PURPLE);
    delay(20);
  }
  delay(150);

  for (int h = 24; h >= 2; h -= 6) {
    clearEyes(); drawEyes(0, h, COLOR_CYAN);
    delay(15);
  }
  for (int h = 2; h <= 24; h += 6) {
    clearEyes(); drawEyes(0, h, COLOR_CYAN);
    delay(15);
  }
  delay(350);

  for (int h = 20; h >= 4; h -= 4) {
    clearEyes(); drawEyes(0, h, COLOR_ALICE_PURPLE);
    delay(25);
  }

  tft.Fill_Rect(4, 26, 224, 60, COLOR_BG);
  last_minute = -1;
}

// ============================================================
// ВЕКТОРНЫЕ ИКОНКИ
// ============================================================
void drawWeatherIcon(int x, int y, int type) {
  if (type == 0) {
    tft.Set_Draw_color(COLOR_YELLOW);
    tft.Fill_Circle(x - 3, y - 3, 7);
    tft.Set_Draw_color(COLOR_TEXT_WHITE);
    tft.Fill_Circle(x + 2, y + 3, 5);
    tft.Fill_Circle(x + 8, y + 1, 7);
    tft.Fill_Circle(x + 14, y + 4, 5);
    tft.Fill_Rect(x + 2, y + 5, 13, 4, COLOR_TEXT_WHITE);
  }
  else if (type == 1) {
    tft.Set_Draw_color(COLOR_TEXT_GRAY);
    tft.Fill_Circle(x + 2, y + 2, 5);
    tft.Fill_Circle(x + 8, y, 7);
    tft.Fill_Circle(x + 14, y + 3, 5);
    tft.Fill_Rect(x + 2, y + 4, 13, 4, COLOR_TEXT_GRAY);
  }
  else if (type == 2) {
    drawWeatherIcon(x, y - 2, 1);
    tft.Set_Draw_color(COLOR_BLUE_ICON);
    tft.Draw_Fast_VLine(x + 4, y + 9, 4);
    tft.Draw_Fast_VLine(x + 9, y + 9, 4);
    tft.Draw_Fast_VLine(x + 14, y + 9, 4);
  }
}

// ============================================================
// ОСНОВНОЙ МАКЕТ
// ============================================================
void drawMainLayout() {
  tft.Fill_Screen(COLOR_BG);

  tft.Set_Draw_color(COLOR_CARD_BORDER);
  tft.Draw_Round_Rectangle(225, 8, 472, 112, 8);
  tft.Draw_Fast_HLine(235, 78, 227);

  drawWeatherIcon(245, 25, 0);

  rus.setSize(1);
  rus.setColor(COLOR_TEXT_GRAY);
  rus.print("Переменная", 235, 48);
  rus.print("облачность", 235, 60);

  tft.Set_Draw_color(COLOR_CYAN);
  tft.Fill_Circle(242, 94, 3);
  tft.Draw_Triangle(239, 94, 245, 94, 242, 88);

  tft.Set_Draw_color(COLOR_CYAN);
  tft.Draw_Fast_HLine(318, 91, 10);
  tft.Draw_Fast_HLine(315, 95, 10);

  tft.Draw_Circle(390, 93, 4);
  tft.Draw_Line(390, 93, 392, 90);

  rus.setColor(COLOR_TEXT_GRAY);
  rus.print("м/с", 350, 88);
  rus.print("мм", 435, 88);

  drawHourlyCards();

  tft.Set_Draw_color(COLOR_CARD_BORDER);
  tft.Draw_Round_Rectangle(8, 202, 155, 312, 8);
  tft.Draw_Round_Rectangle(163, 202, 317, 312, 8);
  drawAstronomyWidget();

  tft.Draw_Round_Rectangle(325, 202, 472, 312, 8);
  drawWeeklyForecastWidget();
}

// ============================================================
// ПОЧАСОВОЙ ПРОГНОЗ
// ============================================================
void drawHourlyCards() {
  int cardW = 58;
  int cardH = 75;
  int startX = 8;
  int startY = 118;
  int gap = 8;

  const char* hours[7] = { "18:00", "19:00", "20:00", "21:00", "22:00", "23:00", "00:00" };
  const char* temps[7] = { "12", "11", "10", "9", "8", "7", "7" };
  int types[7]         = { 0, 1, 1, 1, 2, 2, 1 };

  for (int i = 0; i < 7; i++) {
    int x = startX + i * (cardW + gap);

    if (i == 0) {
      tft.Set_Draw_color(COLOR_ACCENT);
      tft.Draw_Round_Rectangle(x, startY, x + cardW, startY + cardH, 6);
      tft.Draw_Round_Rectangle(x + 1, startY + 1, x + cardW - 1, startY + cardH - 1, 5);
    } else {
      tft.Set_Draw_color(COLOR_CARD_BORDER);
      tft.Draw_Round_Rectangle(x, startY, x + cardW, startY + cardH, 6);
    }

    tft.Set_Text_Mode(true);
    tft.Set_Text_colour(COLOR_TEXT_GRAY);
    tft.Set_Text_Size(1);
    tft.Print((char*)hours[i], x + 13, startY + 8);

    drawWeatherIcon(x + 18, startY + 30, types[i]);

    tft.Set_Text_colour(COLOR_TEXT_WHITE);
    tft.Set_Text_Size(2);
    tft.Print((char*)temps[i], x + 18, startY + 52);
  }
}

// ============================================================
// КАЛЕНДАРЬ
// ============================================================
void drawCalendarWidget(int year, int month, int currentDay) {
  tft.Fill_Rect(10, 204, 143, 106, COLOR_BG);

  rus.setSize(1);
  rus.setColor(COLOR_TEXT_WHITE);
  rus.print((char*)monthsNames[month], 14, 208);

  tft.Set_Text_colour(COLOR_TEXT_GRAY);
  tft.Set_Text_Size(1);
  tft.Print("<", 122, 208);
  tft.Print(">", 138, 208);

  int xStart = 14;
  int yStart = 240;
  int cellW  = 18;
  int cellH  = 13;

  const char* dayHeaders[7] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс" };
  for (int col = 0; col < 7; col++) {
    if (col >= 5) rus.setColor(0xF800);
    else           rus.setColor(COLOR_TEXT_GRAY);
    rus.print((char*)dayHeaders[col], xStart + col * cellW, 224);
  }

  int dayNum = 1;
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 7; col++) {
      if (dayNum > 31) break;

      int x = xStart + col * cellW;
      int y = yStart + row * cellH;

      if (dayNum == currentDay) {
        tft.Set_Draw_color(COLOR_ACCENT);
        tft.Fill_Round_Rectangle(x - 2, y - 2, x + 14, y + cellH - 2, 3);
        tft.Set_Text_colour(COLOR_TEXT_WHITE);
      } else if (col >= 5) {
        tft.Set_Text_colour(0xF800);
      } else {
        tft.Set_Text_colour(COLOR_TEXT_GRAY);
      }

      char numStr[4];
      sprintf(numStr, "%2d", dayNum);
      tft.Set_Text_Size(1);
      tft.Print(numStr, x, y);

      dayNum++;
    }
  }
}

// ============================================================
// АСТРОНОМИЯ
// ============================================================
void drawAstronomyWidget() {
  tft.Set_Draw_color(COLOR_YELLOW);
  tft.Draw_Circle(186, 220, 5);
  tft.Draw_Fast_HLine(178, 226, 16);

  rus.setSize(1);
  rus.setColor(COLOR_TEXT_GRAY);
  rus.print("Восход", 196, 208);

  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Set_Text_Size(1);
  tft.Print("06:42", 196, 222);

  tft.Set_Draw_color(COLOR_YELLOW);
  tft.Draw_Circle(262, 220, 5);
  tft.Draw_Fast_HLine(254, 226, 16);

  rus.setColor(COLOR_TEXT_GRAY);
  rus.print("Закат", 272, 208);

  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Set_Text_Size(1);
  tft.Print("18:03", 272, 222);

  tft.Set_Draw_color(COLOR_CARD_BORDER);
  tft.Draw_Fast_HLine(170, 244, 140);

  tft.Set_Draw_color(COLOR_TEXT_WHITE);
  tft.Fill_Circle(185, 275, 12);
  tft.Set_Draw_color(COLOR_BG);
  tft.Fill_Circle(180, 275, 10);

  rus.setColor(COLOR_TEXT_GRAY);
  rus.print("Фаза луны", 205, 250);
  rus.setColor(COLOR_TEXT_WHITE);
  rus.print("Растущая луна", 205, 264);

  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Set_Text_Size(1);
  tft.Print("72%", 205, 282);
}

// ============================================================
// ПРОГНОЗ НА НЕДЕЛЮ
// ============================================================
void drawWeeklyForecastWidget() {
  rus.setSize(1);
  rus.setColor(COLOR_TEXT_WHITE);
  rus.print("Ближайшие дни", 335, 208);

  const char* days[4]  = { "Ср", "Чт", "Пт", "Сб" };
  const int dates[4]   = { 8, 9, 10, 11 };
  const char* tMax[4]  = { "10", "12", "11", "9" };
  const char* tMin[4]  = { "4",  "5",  "6",  "3" };
  int icons[4]         = { 1, 0, 1, 2 };

  for (int i = 0; i < 4; i++) {
    int y = 226 + (i * 20);

    rus.setColor(COLOR_TEXT_GRAY);
    rus.print((char*)days[i], 332, y + 2);

    tft.Set_Text_colour(COLOR_TEXT_GRAY);
    tft.Set_Text_Size(1);
    char dBuf[5];
    sprintf(dBuf, "%d", dates[i]);
    tft.Print(dBuf, 355, y + 2);

    drawWeatherIcon(385, y + 2, icons[i]);

    tft.Set_Text_colour(COLOR_TEXT_WHITE);
    tft.Print((char*)tMax[i], 418, y + 2);

    tft.Set_Text_colour(COLOR_TEXT_GRAY);
    tft.Print((char*)tMin[i], 448, y + 2);
  }
}

// ============================================================
// ОБНОВЛЕНИЕ ВРЕМЕНИ
// ============================================================
void updateTime() {
  RtcDateTime now = Rtc.GetDateTime();

  if (now.Minute() == last_minute && now.Day() == last_day) return;
  last_minute = now.Minute();

  if (now.Day() != last_day) {
    last_day = now.Day();

    tft.Fill_Rect(10, 10, 210, 20, COLOR_BG);

    rus.setSize(1);
    rus.setColor(COLOR_TEXT_WHITE);
    rus.print((char*)daysOfWeek[now.DayOfWeek()], 10, 12);

    tft.Set_Text_colour(COLOR_TEXT_WHITE);
    tft.Set_Text_Size(1);
    char dStr[5];
    sprintf(dStr, ", %d ", now.Day());
    tft.Print(dStr, 26, 12);

    rus.print((char*)monthsNames[now.Month()], 55, 12);

    char yStr[8];
    sprintf(yStr, " %d", now.Year());
    tft.Print(yStr, 125, 12);

    rus.setColor(COLOR_TEXT_GRAY);
    rus.print("Орел", 25, 90);

    tft.Set_Text_colour(COLOR_TEXT_GRAY);
    tft.Print(" INDOOR", 60, 90);

    tft.Set_Draw_color(COLOR_ACCENT);
    tft.Fill_Circle(15, 94, 3);

    drawCalendarWidget(now.Year(), now.Month(), now.Day());
  }

  tft.Fill_Rect(8, 30, 210, 52, COLOR_BG);

  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", now.Hour(), now.Minute());

  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Set_Text_Size(6);
  tft.Set_Text_Mode(true);
  tft.Print(timeStr, 8, 32);
}

// ============================================================
// ОБНОВЛЕНИЕ ДАТЧИКОВ
// ============================================================
void updateSensors() {
  static float last_t = -999.0f;
  static float last_h = -999.0f;

  if (temp_in == last_t && hum_in == last_h) return;
  last_t = temp_in;
  last_h = hum_in;

  char buf[10];

  tft.Fill_Rect(315, 18, 90, 35, COLOR_BG);
  dtostrf(temp_in, 2, 0, buf);

  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Set_Text_Size(5);
  tft.Print(buf, 318, 18);
  tft.Set_Text_Size(3);
  tft.Print("C", 378, 18);

  tft.Set_Text_Size(1);
  tft.Set_Text_colour(0xF800);
  tft.Print("^", 412, 18);
  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Print("24", 420, 18);

  tft.Set_Text_colour(COLOR_CYAN);
  tft.Print("v", 412, 32);
  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Print("18", 420, 32);

  tft.Fill_Rect(252, 86, 30, 18, COLOR_BG);
  dtostrf(hum_in, 2, 0, buf);

  tft.Set_Text_colour(COLOR_TEXT_WHITE);
  tft.Set_Text_Size(2);
  tft.Print(buf, 252, 88);

  tft.Print("4", 332, 88);
  tft.Print("751", 398, 88);
}