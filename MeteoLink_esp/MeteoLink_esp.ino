// MeteoLink_esp.ino
// Шлюз между Arduino Mega и интернетом: Wi-Fi + NTP + Web + OTA.
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <time.h>
#include <ArduinoJson.h>

#include "config.h"
#include "debug_log.h"
#include "mega_protocol.h"
#include "github_ota.h"
#include "web_ui.h"

ESP8266WebServer server(80);

unsigned long lastTimePush = 0;
unsigned long lastHeartbeat = 0;

// ============================================================
// HTTP-ОБРАБОТЧИКИ
// ============================================================
void handleRoot() {
    server.send(200, "text/html; charset=utf-8", webUiGetPage());
}

void handleSensors() {
    server.send(200, "application/json", webUiGetSensorsJson());
}

void handleOtaStatus() {
    server.send(200, "application/json", webUiGetOtaStatusJson());
}

void handleOtaCheck() {
    debugPrintln("[HTTP] Manual OTA check");
    String msg = githubOtaCheckNow();
    String out = "{\"ok\":true,\"message\":\"" + msg + "\"}";
    server.send(200, "application/json", out);
}

void handleSaveSettings() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }
    const char* region = doc["region"] | "Europe/Moscow";
    bool  nightMode  = doc["nightMode"]  | false;
    int   nightStart = doc["nightStart"] | 23;
    int   nightEnd   = doc["nightEnd"]   | 7;
    bool  useF       = doc["useF"]       | false;
    bool  use12h     = doc["use12h"]     | false;

    debugPrintf("[HTTP] Settings: region=%s night=%d-%d F=%d 12h=%d",
                region, nightStart, nightEnd, useF, use12h);

    // Устанавливаем часовой пояс локально
    setenv("TZ", region, 1);
    tzset();

    // И отправляем в Mega
    megaSendSettings(region, nightMode, nightStart, nightEnd, useF, use12h);

    server.send(200, "application/json", "{\"ok\":true}");
}

void handleDebugLogs() {
    server.send(200, "application/json", debugGetLogsJson());
}

void handleDebugClear() {
    debugClearLogs();
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    debugInit();
    debugPrintln("=== MeteoLink ESP8266 starting ===");
    debugPrintf("Version: %s | Device: %s", FIRMWARE_VERSION, DEVICE_NAME);
    debugPrintf("Free heap: %d", ESP.getFreeHeap());

    // 1. UART к Mega
    megaInit();

    // 2. Wi-Fi через WiFiManager
    WiFiManager wm;
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
    wm.setAPCallback([](WiFiManager*) {
        debugPrintln("[WiFi] Config portal opened");
    });
    if (!wm.autoConnect(WIFI_AP_NAME)) {
        debugPrintln("[WiFi] Failed, restarting...");
        delay(2000);
        ESP.restart();
    }
    debugPrintf("[WiFi] Connected: %s | IP: %s",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    // 3. mDNS — доступ по http://MeteoLink.local
    if (MDNS.begin(DEVICE_NAME)) {
        MDNS.addService("http", "tcp", 80);
        debugPrintf("[mDNS] http://%s.local", DEVICE_NAME);
    }

    // 4. NTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "Europe/Moscow", 1);   // временно, потом придёт из настроек
    tzset();
    debugPrintln("[NTP] Requested time");

    // 5. HTTP-сервер
    server.on("/",                 HTTP_GET,  handleRoot);
    server.on("/api/sensors",      HTTP_GET,  handleSensors);
    server.on("/api/ota/status",   HTTP_GET,  handleOtaStatus);
    server.on("/api/ota/check",    HTTP_POST, handleOtaCheck);
    server.on("/api/settings",     HTTP_POST, handleSaveSettings);
    server.on("/api/debug/logs",   HTTP_GET,  handleDebugLogs);
    server.on("/api/debug/clear",  HTTP_POST, handleDebugClear);
    server.onNotFound(handleNotFound);
    server.begin();
    debugPrintf("[HTTP] http://%s/", WiFi.localIP().toString().c_str());

    // 6. OTA
    githubOtaInit();

    debugPrintln("=== Ready ===");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    server.handleClient();
    MDNS.update();
    megaLoop();
    githubOtaLoop();

    unsigned long now = millis();

    // Раз в минуту — точное время в Mega
    if (now - lastTimePush >= 60000UL) {
        lastTimePush = now;
        time_t t = time(nullptr);
        if (t > 1600000000) {              // NTP уже синхронизирован
            struct tm tm;
            localtime_r(&t, &tm);
            megaSendTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
        } else {
            debugPrintln("[NTP] waiting for sync...");
        }
    }

    // Раз в 30 секунд — диагностика в лог
    if (now - lastHeartbeat >= 30000UL) {
        lastHeartbeat = now;
        debugPrintf("[SYS] Heap=%d RSSI=%d Mega=%s",
                    ESP.getFreeHeap(), WiFi.RSSI(),
                    megaIsOnline() ? "ON" : "OFF");
    }

    delay(2);
}