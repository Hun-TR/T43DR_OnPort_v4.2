#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include <Arduino.h>

void initUART();
bool changeBaudRate(long newBaudRate); // Return type düzeltildi: void -> bool
bool requestFirstFault();
bool requestNextFault();
String getLastFaultResponse();

// Yeni eklenen fonksiyonlar
void checkUARTHealth();
String safeReadUARTResponse(unsigned long timeout);
void updateUARTStats(bool success);
String getUARTStatus();
bool sendCustomCommand(const String& command, String& response, unsigned long timeout = 0);
bool testUARTConnection();

#endif