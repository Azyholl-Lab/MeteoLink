// debug_log.cpp
#include "debug_log.h"
#include "config.h"

// Кольцевой буфер в RAM на 30 последних строк.
// При перезагрузке теряется — это нормально для отладочных логов.
#define LOG_LINES 30
static String logBuf[LOG_LINES];
static int logPos = 0;

static void logWrite(const String& line) {
    Serial.println(line);           // USB Serial
    logBuf[logPos] = line;
    logPos = (logPos + 1) % LOG_LINES;
}

void debugInit() {
    Serial.begin(DEBUG_BAUD);
    delay(200);
    logWrite("[BOOT] MeteoLink ESP starting...");
}

void debugPrintln(const String& msg) {
    logWrite(msg);
}

void debugPrintf(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    logWrite(String(buf));
}

// Простой JSON-массив строк (без внешних зависимостей)
String debugGetLogsJson() {
    String out = "{\"logs\":[";
    bool first = true;
    for (int i = 0; i < LOG_LINES; i++) {
        int idx = (logPos + i) % LOG_LINES;
        if (logBuf[idx].length() == 0) continue;
        if (!first) out += ",";
        String s = logBuf[idx];
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        out += "\"" + s + "\"";
        first = false;
    }
    out += "]}";
    return out;
}

void debugClearLogs() {
    for (int i = 0; i < LOG_LINES; i++) logBuf[i] = "";
    logPos = 0;
}