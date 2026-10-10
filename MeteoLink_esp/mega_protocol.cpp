// mega_protocol.cpp
#include "mega_protocol.h"
#include "config.h"
#include "debug_log.h"
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

static SoftwareSerial megaSerial(MEGA_RX_PIN, MEGA_TX_PIN);
static String rxBuf = "";

static float sTemp  = NAN;
static float sHum   = NAN;
static float sPress = NAN;
static unsigned long sLastUpdate = 0;

void megaInit() {
    megaSerial.begin(MEGA_BAUD);
    pinMode(MEGA_RESET_PIN, OUTPUT);
    digitalWrite(MEGA_RESET_PIN, HIGH);   // RESET не активен
    debugPrintf("[MEGA] UART init (RX=D5, TX=D6, %d baud)", MEGA_BAUD);
}

static void megaSendJson(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    megaSerial.println(out);
    debugPrintf("[MEGA] -> %s", out.c_str());
}

void megaSendTime(int y, int mo, int d, int h, int mi, int s) {
    JsonDocument doc;
    doc["type"] = "time";
    doc["y"] = y;  doc["mo"] = mo; doc["d"] = d;
    doc["h"] = h;  doc["m"]  = mi; doc["s"] = s;
    megaSendJson(doc);
}

void megaSendSettings(const char* region, bool nightMode,
                      int nightStart, int nightEnd,
                      bool useF, bool use12h) {
    JsonDocument doc;
    doc["type"]       = "settings";
    doc["region"]     = region;
    doc["nightMode"]  = nightMode;
    doc["nightStart"] = nightStart;
    doc["nightEnd"]   = nightEnd;
    doc["useF"]       = useF;
    doc["use12h"]     = use12h;
    megaSendJson(doc);
}

// Читаем построчно, парсим JSON
void megaLoop() {
    while (megaSerial.available()) {
        char c = megaSerial.read();
        if (c == '\n') {
            if (rxBuf.length() > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, rxBuf);
                if (err) {
                    debugPrintf("[MEGA] JSON parse error: %s | raw=%s",
                                err.c_str(), rxBuf.c_str());
                } else {
                    const char* type = doc["type"] | "";
                    if (strcmp(type, "sensor") == 0) {
                        if (!doc["temp"].isNull())  sTemp  = doc["temp"].as<float>();
                        if (!doc["hum"].isNull())   sHum   = doc["hum"].as<float>();
                        if (!doc["press"].isNull()) sPress = doc["press"].as<float>();
                        sLastUpdate = millis();
                        debugPrintf("[MEGA] <- T=%.1f H=%.1f P=%.1f",
                                    sTemp, sHum, sPress);
                    } else if (strcmp(type, "log") == 0) {
                        debugPrintf("[MEGA-LOG] %s", (const char*)(doc["msg"] | ""));
                    } else if (strcmp(type, "status") == 0) {
                        debugPrintf("[MEGA-STATUS] %s", (const char*)(doc["msg"] | ""));
                    }
                }
                rxBuf = "";
            }
        } else if (c != '\r') {
            rxBuf += c;
            if (rxBuf.length() > 300) rxBuf = "";  // защита от мусора
        }
    }
}

float megaGetTemp()  { return sTemp; }
float megaGetHum()   { return sHum; }
float megaGetPress() { return sPress; }
bool  megaIsOnline() {
    return sLastUpdate > 0 && (millis() - sLastUpdate < 60000UL);
}
unsigned long megaLastUpdateAge() {
    return sLastUpdate ? (millis() - sLastUpdate) / 1000UL : 0;
}