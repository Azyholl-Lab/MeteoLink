// github_ota.h — проверка и установка обновлений прошивки ESP с GitHub Releases
#ifndef GITHUB_OTA_H
#define GITHUB_OTA_H

#include <Arduino.h>

void   githubOtaInit();
void   githubOtaLoop();               // вызывать в loop()
String githubOtaCheckNow();           // ручная проверка: возвращает текст результата
String githubOtaCurrentVersion();

#endif