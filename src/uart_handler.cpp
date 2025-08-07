#include "uart_handler.h"
#include "log_system.h"
#include "settings.h"
#include <Preferences.h>

#define UART_RX_PIN 4
#define UART_TX_PIN 2
#define UART_PORT   Serial2
#define UART_TIMEOUT 1000
#define MAX_RESPONSE_LENGTH 256

String lastResponse = "";
static unsigned long lastUARTActivity = 0;
static int uartErrorCount = 0;
static bool uartHealthy = true;

void initUART() {
    // UART pinlerini başlat
    pinMode(UART_RX_PIN, INPUT);
    pinMode(UART_TX_PIN, OUTPUT);
    
    // Ayarlardan baudrate'i oku ve UART'ı başlat
    UART_PORT.begin(settings.currentBaudRate, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    
    // Buffer'ı temizle
    while (UART_PORT.available()) {
        UART_PORT.read();
    }
    
    lastUARTActivity = millis();
    uartErrorCount = 0;
    uartHealthy = true;
    
    addLog("✅ UART başlatıldı. BaudRate: " + String(settings.currentBaudRate) + 
           ", RX: " + String(UART_RX_PIN) + ", TX: " + String(UART_TX_PIN), SUCCESS, "UART");
}

bool changeBaudRate(long newBaudRate) {
    // Geçerli baud rate kontrolü
    const long validBaudRates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    bool isValid = false;
    
    for (int i = 0; i < 8; i++) {
        if (newBaudRate == validBaudRates[i]) {
            isValid = true;
            break;
        }
    }
    
    if (!isValid) {
        addLog("❌ Geçersiz BaudRate: " + String(newBaudRate), ERROR, "UART");
        return false;
    }
    
    // Mevcut işlemleri bitir
    UART_PORT.flush();
    delay(100);
    
    // Yeni BaudRate'i ayarla
    long oldBaudRate = settings.currentBaudRate;
    settings.currentBaudRate = newBaudRate;
    
    // Ayarı kalıcı yap
    Preferences prefs;
    prefs.begin("app-settings", false);
    prefs.putLong("baudrate", newBaudRate);
    prefs.end();
    
    // UART'ı yeni hızda yeniden başlat
    UART_PORT.end();
    delay(100);
    initUART();
    
    addLog("🔄 BaudRate değiştirildi: " + String(oldBaudRate) + " -> " + String(newBaudRate), 
           SUCCESS, "UART");
    
    return true;
}

// UART sağlık durumunu kontrol et
void checkUARTHealth() {
    unsigned long now = millis();
    
    // 30 saniyedir aktivite yoksa uyarı ver
    if (now - lastUARTActivity > 30000) {
        if (uartHealthy) {
            addLog("⚠️ UART 30 saniyedir sessiz.", WARN, "UART");
            uartHealthy = false;
        }
    }
    
    // Çok fazla hata varsa UART'ı yeniden başlat
    if (uartErrorCount > 5) {
        addLog("🔄 Çok fazla UART hatası. Yeniden başlatılıyor...", WARN, "UART");
        initUART();
        uartErrorCount = 0;
    }
}

// Güvenli UART okuma fonksiyonu
String safeReadUARTResponse(unsigned long timeout) {
    String response = "";
    unsigned long startTime = millis();
    char buffer[MAX_RESPONSE_LENGTH];
    int bufferIndex = 0;
    
    while (millis() - startTime < timeout) {
        if (UART_PORT.available()) {
            char c = UART_PORT.read();
            lastUARTActivity = millis();
            uartHealthy = true;
            
            // Satır sonu karakterleri kontrolü
            if (c == '\n' || c == '\r') {
                if (bufferIndex > 0) {
                    buffer[bufferIndex] = '\0';
                    response = String(buffer);
                    break;
                }
            } else if (c >= 32 && c <= 126) { // Yazdırılabilir karakterler
                if (bufferIndex < MAX_RESPONSE_LENGTH - 1) {
                    buffer[bufferIndex++] = c;
                }
            }
            
            // Buffer overflow koruması
            if (bufferIndex >= MAX_RESPONSE_LENGTH - 1) {
                buffer[MAX_RESPONSE_LENGTH - 1] = '\0';
                response = String(buffer);
                addLog("⚠️ UART response buffer overflow koruması aktif.", WARN, "UART");
                break;
            }
        }
        
        delay(1); // CPU'ya nefes aldır
    }
    
    return response;
}

bool requestFirstFault() {
    // Buffer'ı temizle
    while (UART_PORT.available()) {
        UART_PORT.read();
    }
    
    // Komut gönder
    String command = "12345v";
    UART_PORT.println(command);
    UART_PORT.flush(); // Gönderimden emin ol
    
    addLog("UART komut gönderildi: " + command, DEBUG, "UART");
    
    // Yanıt bekle
    lastResponse = safeReadUARTResponse(UART_TIMEOUT);
    
    if (lastResponse.length() > 0) {
        addLog("UART yanıt alındı: " + lastResponse, DEBUG, "UART");
        return true;
    } else {
        uartErrorCount++;
        addLog("❌ İlk arıza bilgisi için yanıt alınamadı.", ERROR, "UART");
        return false;
    }
}

bool requestNextFault() {
    // Buffer'ı temizle
    while (UART_PORT.available()) {
        UART_PORT.read();
    }
    
    // Komut gönder
    String command = "n";
    UART_PORT.println(command);
    UART_PORT.flush(); // Gönderimden emin ol
    
    addLog("UART komut gönderildi: " + command, DEBUG, "UART");
    
    // Yanıt bekle
    lastResponse = safeReadUARTResponse(UART_TIMEOUT);
    
    if (lastResponse.length() > 0) {
        addLog("UART yanıt alındı: " + lastResponse, DEBUG, "UART");
        return true;
    } else {
        uartErrorCount++;
        addLog("❌ Sonraki arıza bilgisi için yanıt alınamadı.", ERROR, "UART");
        return false;
    }
}

String getLastFaultResponse() {
    return lastResponse;
}

// UART istatistikleri
struct UARTStats {
    unsigned long totalCommands;
    unsigned long successfulCommands;
    unsigned long failedCommands;
    unsigned long lastSuccessTime;
    unsigned long lastFailTime;
};

static UARTStats uartStats = {0, 0, 0, 0, 0};

// UART istatistiklerini güncelle
void updateUARTStats(bool success) {
    uartStats.totalCommands++;
    if (success) {
        uartStats.successfulCommands++;
        uartStats.lastSuccessTime = millis();
    } else {
        uartStats.failedCommands++;
        uartStats.lastFailTime = millis();
    }
}

// UART durumu ve istatistiklerini döndür
String getUARTStatus() {
    unsigned long now = millis();
    String status = "UART Durum Raporu:\n";
    status += "Baud Rate: " + String(settings.currentBaudRate) + "\n";
    status += "Sağlık Durumu: " + String(uartHealthy ? "Sağlıklı" : "Sorunlu") + "\n";
    status += "Hata Sayısı: " + String(uartErrorCount) + "\n";
    status += "Toplam Komut: " + String(uartStats.totalCommands) + "\n";
    status += "Başarılı: " + String(uartStats.successfulCommands) + "\n";
    status += "Başarısız: " + String(uartStats.failedCommands) + "\n";
    
    if (uartStats.totalCommands > 0) {
        float successRate = (float)uartStats.successfulCommands / uartStats.totalCommands * 100;
        status += "Başarı Oranı: %" + String(successRate, 1) + "\n";
    }
    
    if (uartStats.lastSuccessTime > 0) {
        status += "Son Başarılı: " + String((now - uartStats.lastSuccessTime) / 1000) + " sn önce\n";
    }
    
    return status;
}

// Özel komut gönderme fonksiyonu (gelişmiş kullanım için)
bool sendCustomCommand(const String& command, String& response, unsigned long timeout) {
    if (command.length() == 0 || command.length() > 50) {
        addLog("❌ Geçersiz komut uzunluğu.", ERROR, "UART");
        return false;
    }
    
    // Buffer'ı temizle
    while (UART_PORT.available()) {
        UART_PORT.read();
    }
    
    // Komut gönder
    UART_PORT.println(command);
    UART_PORT.flush();
    
    addLog("Özel UART komut: " + command, DEBUG, "UART");
    
    // Yanıt bekle
    response = safeReadUARTResponse(timeout == 0 ? UART_TIMEOUT : timeout);
    
    bool success = response.length() > 0;
    updateUARTStats(success);
    
    if (success) {
        addLog("Özel komut yanıtı: " + response, DEBUG, "UART");
    } else {
        addLog("❌ Özel komut için yanıt alınamadı: " + command, ERROR, "UART");
    }
    
    return success;
}

// UART test fonksiyonu
bool testUARTConnection() {
    addLog("UART bağlantı testi başlatıldı...", INFO, "UART");
    
    String testResponse;
    bool testResult = sendCustomCommand("test", testResponse, 2000);
    
    if (testResult) {
        addLog("✅ UART bağlantı testi başarılı.", SUCCESS, "UART");
    } else {
        addLog("❌ UART bağlantı testi başarısız.", ERROR, "UART");
    }
    
    return testResult;
}