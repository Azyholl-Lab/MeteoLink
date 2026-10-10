// github_ota.cpp — OTA через GitHub Releases (только чтение)
//
// Логика:
//   1. GET https://api.github.com/repos/{repo}/releases/latest
//   2. Читаем tag_name (например, "v1.2.0") и ищем ассет *.bin
//   3. Если тег больше текущего FIRMWARE_VERSION — скачиваем .bin
//      через ESP8266httpUpdate и перезагружаемся.
// Ничего на GitHub НЕ отправляется.
#include "github_ota.h"
#include "config.h"
#include "debug_log.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static unsigned long lastCheck = 0;

void githubOtaInit() {
    debugPrintf("[OTA] Repo: %s", GITHUB_REPO);
    debugPrintf("[OTA] Current version: %s", FIRMWARE_VERSION);
    lastCheck = millis();  // первая проверка — сразу
}

String githubOtaCurrentVersion() { return String(FIRMWARE_VERSION); }

// Сравнение версий: "1.0.0" < "1.2.3" → -1, == → 0, > → 1
static int compareVersions(const String& a, const String& b) {
    String va = a.startsWith("v") ? a.substring(1) : a;
    String vb = b.startsWith("v") ? b.substring(1) : b;
    int aM = 0, am = 0, ap = 0, bM = 0, bm = 0, bp = 0;
    sscanf(va.c_str(), "%d.%d.%d", &aM, &am, &ap);
    sscanf(vb.c_str(), "%d.%d.%d", &bM, &bm, &bp);
    if (aM != bM) return aM - bM;
    if (am != bm) return am - bm;
    return ap - bp;
}

// Ручная проверка. Возвращает текст для веб-интерфейса.
String githubOtaCheckNow() {
    if (WiFi.status() != WL_CONNECTED) return "Нет Wi-Fi";

    WiFiClientSecure client;
    client.setInsecure();       // без валидации сертификата — проще

    HTTPClient http;
    String url = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";
    http.begin(client, url);
    http.addHeader("User-Agent", "MeteoLink");
    http.setTimeout(15000);

    debugPrintf("[OTA] GET %s", url.c_str());
    int code = http.GET();
    if (code != 200) {
        String err = "HTTP " + String(code);
        debugPrintf("[OTA] %s", err.c_str());
        http.end();
        lastCheck = millis();
        return err;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        debugPrintf("[OTA] JSON error: %s", err.c_str());
        lastCheck = millis();
        return "Ошибка JSON";
    }

    const char* tag = doc["tag_name"] | "";
    debugPrintf("[OTA] Latest release: %s", tag);

    if (compareVersions(tag, FIRMWARE_VERSION) <= 0) {
        debugPrintln("[OTA] Уже новейшая версия");
        lastCheck = millis();
        return String("Уже новейшая: ") + FIRMWARE_VERSION;
    }

    // Ищем .bin-ассет
    String binUrl = "";
    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject a : assets) {
        const char* name = a["name"] | "";
        if (strstr(name, ".bin") != NULL) {
            binUrl = String((const char*)(a["browser_download_url"] | ""));
            break;
        }
    }

    if (binUrl.length() == 0) {
        debugPrintln("[OTA] .bin ассет не найден");
        lastCheck = millis();
        return "Нет .bin в релизе";
    }

    debugPrintf("[OTA] Downloading %s", binUrl.c_str());
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, binUrl);
    // Если успешно — плата перезагрузится до этой строки
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            debugPrintf("[OTA] FAILED: %s", ESPhttpUpdate.getLastErrorString().c_str());
            lastCheck = millis();
            return "Ошибка: " + ESPhttpUpdate.getLastErrorString();
        case HTTP_UPDATE_NO_UPDATES:
            debugPrintln("[OTA] No updates");
            lastCheck = millis();
            return "Нет обновлений";
        case HTTP_UPDATE_OK:
            return "OK";
    }
    lastCheck = millis();
    return "?";
}

void githubOtaLoop() {
    if (millis() - lastCheck < OTA_CHECK_INTERVAL) return;
    githubOtaCheckNow();
}