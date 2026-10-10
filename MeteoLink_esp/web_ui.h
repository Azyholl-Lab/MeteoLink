// web_ui.h — генерация HTML и JSON-ответов для веб-интерфейса
#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

String webUiGetPage();           // HTML-страница
String webUiGetSensorsJson();    // /api/sensors
String webUiGetOtaStatusJson();  // /api/ota/status

#endif