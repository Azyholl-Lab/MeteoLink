// debug_log.h — логирование в USB Serial + кольцевой буфер для веб-UI
#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>

void debugInit();
void debugPrintln(const String& msg);
void debugPrintf(const char* format, ...);

// Для веб-интерфейса
String debugGetLogsJson();
void   debugClearLogs();

#endif