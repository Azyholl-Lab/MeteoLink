// mega_protocol.h — обмен JSON-сообщениями с Arduino Mega по UART
#ifndef MEGA_PROTOCOL_H
#define MEGA_PROTOCOL_H

#include <Arduino.h>

void megaInit();
void megaLoop();                 // вызывать в loop()

// Отправка данных в Mega
void megaSendTime(int y, int mo, int d, int h, int mi, int s);
void megaSendSettings(const char* region, bool nightMode,
                      int nightStart, int nightEnd,
                      bool useF, bool use12h);

// Данные, полученные от Mega
float megaGetTemp();
float megaGetHum();
float megaGetPress();
bool  megaIsOnline();
unsigned long megaLastUpdateAge();   // сек с момента последнего пакета

#endif