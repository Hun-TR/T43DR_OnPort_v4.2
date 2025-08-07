#include "ntp_handler.h"
#include "log_system.h"
#include <Preferences.h>
#include <HardwareSerial.h>

// Global değişkenler
ReceivedTimeData receivedTime;
NTPConfig ntpConfig;
bool ntpConfigured = false;

// Arka port ile iletişim için ikinci seri port
HardwareSerial backendSerial(2);

// Buffer overflow koruması için maksimum boyutlar
#define MAX_DATA_BUFFER 32
#define MAX_MESSAGE_LENGTH 128

bool loadNTPSettings() {
    Preferences preferences;
    preferences.begin("ntp-config", true);
    
    String server1 = preferences.getString("ntp_server1", "");
    if (server1.length() == 0) {
        preferences.end();
        return false;
    }

    // NTP sunucu adreslerinin validasyonu
    String server2 = preferences.getString("ntp_server2", "");
    
    // IP adresi formatı kontrolü (basit)
    if (!isValidIPOrDomain(server1) || (server2.length() > 0 && !isValidIPOrDomain(server2))) {
        addLog("Geçersiz NTP sunucu adresi tespit edildi.", ERROR, "NTP");
        preferences.end();
        return false;
    }
    
    server1.toCharArray(ntpConfig.ntpServer1, sizeof(ntpConfig.ntpServer1));
    server2.toCharArray(ntpConfig.ntpServer2, sizeof(ntpConfig.ntpServer2));
    
    ntpConfig.timezone = preferences.getInt("timezone", 3);
    ntpConfig.enabled = preferences.getBool("enabled", true);
    
    preferences.end();
    
    ntpConfigured = true;
    addLog("✅ NTP ayarları NVS'den yüklendi.", SUCCESS, "NTP");
    return true;
}

// IP adresi veya domain adı validasyonu
bool isValidIPOrDomain(const String& address) {
    if (address.length() < 7 || address.length() > 253) return false;
    
    // Basit IP adresi kontrolü (xxx.xxx.xxx.xxx formatı)
    int dots = 0;
    bool isIP = true;
    
    for (char c : address) {
        if (c == '.') {
            dots++;
        } else if (c < '0' || c > '9') {
            isIP = false;
            break;
        }
    }
    
    if (isIP && dots == 3) {
        // IP adresi formatında, sayısal değerleri kontrol et
        IPAddress testIP;
        return testIP.fromString(address);
    }
    
    // Domain adı kontrolü (basit)
    if (address.indexOf('.') > 0 && address.indexOf(' ') == -1) {
        return true;
    }
    
    return false;
}

void sendNTPConfigToBackend() {
    if (strlen(ntpConfig.ntpServer1) == 0) {
        addLog("NTP sunucu adresi boş, arka porta gönderilmiyor.", WARN, "NTP");
        return;
    }
    
    String message = "NTP_UPDATE;" + String(ntpConfig.ntpServer1) + ";" + String(ntpConfig.ntpServer2);
    
    // Mesaj uzunluğu kontrolü
    if (message.length() > MAX_MESSAGE_LENGTH) {
        addLog("NTP ayarı mesajı çok uzun, gönderilemedi.", ERROR, "NTP");
        return;
    }
    
    backendSerial.println(message);
    addLog("Arka porta NTP ayarları gönderildi: " + message, INFO, "NTP");

    // ACK bekleme (geliştirilmiş timeout)
    unsigned long startTime = millis();
    String responseBuffer = "";
    
    while (millis() - startTime < 3000) { // 3 saniye timeout
        if (backendSerial.available()) {
            char c = backendSerial.read();
            if (c == '\n' || c == '\r') {
                responseBuffer.trim();
                if (responseBuffer == "ACK") {
                    addLog("✅ Arka porttan NTP ayarları için ACK alındı.", SUCCESS, "NTP");
                    return;
                } else if (responseBuffer == "NACK") {
                    addLog("❌ Arka port NTP ayarlarını reddetti.", ERROR, "NTP");
                    return;
                }
                responseBuffer = "";
            } else {
                responseBuffer += c;
                if (responseBuffer.length() > 10) { // Buffer overflow koruması
                    responseBuffer = "";
                }
            }
        }
        delay(10);
    }
    addLog("⚠️ Arka porttan ACK alınamadı (timeout).", WARN, "NTP");
}

bool saveNTPSettings(const String& server1, const String& server2, int timezone) {
    // Input validation
    if (!isValidIPOrDomain(server1)) {
        addLog("Geçersiz birincil NTP sunucu adresi.", ERROR, "NTP");
        return false;
    }
    
    if (server2.length() > 0 && !isValidIPOrDomain(server2)) {
        addLog("Geçersiz ikincil NTP sunucu adresi.", ERROR, "NTP");
        return false;
    }
    
    if (timezone < -12 || timezone > 14) {
        addLog("Geçersiz zaman dilimi (-12 ile +14 arasında olmalı).", ERROR, "NTP");
        return false;
    }

    Preferences preferences;
    preferences.begin("ntp-config", false);
    
    preferences.putString("ntp_server1", server1);
    preferences.putString("ntp_server2", server2);
    preferences.putInt("timezone", timezone);
    preferences.putBool("enabled", true);
    
    preferences.end();

    // Global config yapısını güncelle
    server1.toCharArray(ntpConfig.ntpServer1, sizeof(ntpConfig.ntpServer1));
    server2.toCharArray(ntpConfig.ntpServer2, sizeof(ntpConfig.ntpServer2));
    ntpConfig.timezone = timezone;
    ntpConfig.enabled = true;

    ntpConfigured = true;
    addLog("✅ NTP ayarları kaydedildi: " + server1 + ", " + server2, SUCCESS, "NTP");
    
    // Değişikliği arka porta gönder
    sendNTPConfigToBackend();
    return true;
}

String formatDate(const String& dateStr) {
    if (dateStr.length() != 6) return "Geçersiz Tarih";
    
    // Tarih geçerliliği kontrolü
    int day = dateStr.substring(0, 2).toInt();
    int month = dateStr.substring(2, 4).toInt();
    int year = 2000 + dateStr.substring(4, 6).toInt();
    
    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2020 || year > 2099) {
        return "Geçersiz Tarih";
    }
    
    return String(day < 10 ? "0" : "") + String(day) + "." + 
           String(month < 10 ? "0" : "") + String(month) + "." + String(year);
}

String formatTime(const String& timeStr) {
    if (timeStr.length() != 6) return "Geçersiz Saat";
    
    // Saat geçerliliği kontrolü
    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(2, 4).toInt();
    int second = timeStr.substring(4, 6).toInt();
    
    if (hour > 23 || minute > 59 || second > 59) {
        return "Geçersiz Saat";
    }
    
    return String(hour < 10 ? "0" : "") + String(hour) + ":" +
           String(minute < 10 ? "0" : "") + String(minute) + ":" +
           String(second < 10 ? "0" : "") + String(second);
}

void parseTimeData(const String& data) {
    if (data.length() != 7) {
        addLog("Arka porttan geçersiz formatta veri: " + data, WARN, "NTP");
        return;
    }
    
    String dataOnly = data.substring(0, 6);
    char checksum = data.charAt(6);

    // Checksum kontrolü ve veri türü belirleme
    if (checksum >= 'A' && checksum <= 'Z') { // Tarih verisi
        String formattedDate = formatDate(dataOnly);
        if (formattedDate != "Geçersiz Tarih") {
            receivedTime.date = dataOnly;
            receivedTime.lastUpdate = millis();
        }
    } else if (checksum >= 'a' && checksum <= 'z') { // Saat verisi
        String formattedTime = formatTime(dataOnly);
        if (formattedTime != "Geçersiz Saat") {
            receivedTime.time = dataOnly;
            receivedTime.isValid = true;
            receivedTime.lastUpdate = millis();
        }
    } else {
        addLog("Bilinmeyen checksum karakteri: " + String(checksum), WARN, "NTP");
    }
}

void readBackendData() {
    static String dataBuffer = "";
    static unsigned long lastActivity = millis();
    
    while (backendSerial.available()) {
        char c = backendSerial.read();
        lastActivity = millis();
        
        if (c == '\n' || c == '\r') {
            if (dataBuffer.length() > 0) {
                parseTimeData(dataBuffer);
                dataBuffer = "";
            }
        } else {
            dataBuffer += c;
            // Buffer overflow koruması
            if (dataBuffer.length() > MAX_DATA_BUFFER) {
                addLog("Backend veri buffer'ı overflow, temizleniyor.", WARN, "NTP");
                dataBuffer = "";
            }
        }
    }
    
    // Uzun süre veri gelmediğinde buffer'ı temizle
    if (dataBuffer.length() > 0 && millis() - lastActivity > 5000) {
        dataBuffer = "";
    }
}

void processReceivedData() {
    readBackendData();
    
    // Veri timeout kontrolü
    if (receivedTime.isValid && (millis() - receivedTime.lastUpdate > 60000)) { // 60 saniye
        receivedTime.isValid = false;
        addLog("❌ Arka porttan 60 saniyedir veri alınamıyor (timeout).", ERROR, "NTP");
    }
}

void initNTPHandler() {
    // Seri port başlatma
    backendSerial.begin(115200, SERIAL_8N1, 4, 2); // RX: 4, TX: 2
    receivedTime.isValid = false;
    receivedTime.lastUpdate = 0;
    
    // NTP ayarları yükleme
    if (!loadNTPSettings()) {
        addLog("⚠️ Kayıtlı NTP ayarı bulunamadı. Varsayılanlar kullanılıyor.", WARN, "NTP");
        // Varsayılan ayarları yükle
        strcpy(ntpConfig.ntpServer1, "pool.ntp.org");
        strcpy(ntpConfig.ntpServer2, "time.google.com");
        ntpConfig.timezone = 3;
        ntpConfig.enabled = true;
        ntpConfigured = false;
    }
    
    // İlk konfigürasyonu gönder
    delay(1000); // Backend'in hazır olmasını bekle
    sendNTPConfigToBackend();
    
    addLog("✅ NTP Handler başlatıldı.", SUCCESS, "NTP");
}

// Yardımcı fonksiyonlar
bool isTimeDataValid() {
    return receivedTime.isValid && (millis() - receivedTime.lastUpdate < 60000);
}

String getCurrentDateTime() {
    if (!isTimeDataValid()) return "Zaman verisi bekleniyor...";
    return formatDate(receivedTime.date) + " " + formatTime(receivedTime.time);
}

String getCurrentDate() {
    if (!isTimeDataValid()) return "Bilinmiyor";
    return formatDate(receivedTime.date);
}

String getCurrentTime() {
    if (!isTimeDataValid()) return "Bilinmiyor";
    return formatTime(receivedTime.time);
}

// NTP senkronizasyon durumunu kontrol et
bool isNTPSynced() {
    return ntpConfigured && isTimeDataValid();
}

// NTP ayarlarını sıfırla
void resetNTPSettings() {
    Preferences preferences;
    preferences.begin("ntp-config", false);
    preferences.clear();
    preferences.end();
    
    ntpConfigured = false;
    receivedTime.isValid = false;
    
    addLog("NTP ayarları sıfırlandı.", INFO, "NTP");
}