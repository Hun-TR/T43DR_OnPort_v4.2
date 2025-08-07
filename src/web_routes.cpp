#include "web_routes.h"
#include "auth_system.h"
#include "settings.h"
#include "ntp_handler.h"
#include "uart_handler.h"
#include "log_system.h"
#include <SPIFFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>

extern WebServer server;
extern Settings settings;
extern bool ntpConfigured;

// Güvenlik başlıkları ekleyen fonksiyon
void addSecurityHeaders() {
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("X-XSS-Protection", "1; mode=block");
    server.sendHeader("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
}

// Rate limiting için basit sistem
struct RateLimiter {
    unsigned long lastRequest;
    int requestCount;
    static const int MAX_REQUESTS = 10;
    static const unsigned long TIME_WINDOW = 60000; // 1 dakika
};

static RateLimiter apiLimiter = {0, 0};

bool checkRateLimit() {
    unsigned long now = millis();
    
    // Zaman penceresi sıfırlama
    if (now - apiLimiter.lastRequest > RateLimiter::TIME_WINDOW) {
        apiLimiter.requestCount = 0;
        apiLimiter.lastRequest = now;
    }
    
    apiLimiter.requestCount++;
    
    if (apiLimiter.requestCount > RateLimiter::MAX_REQUESTS) {
        addLog("API rate limit aşıldı.", WARN, "WEB");
        return false;
    }
    
    return true;
}

void serveStaticFile(const String& path, const String& contentType) {
    addSecurityHeaders();
    
    if (!SPIFFS.exists(path)) {
        addLog("Dosya bulunamadı: " + path, WARN, "WEB");
        server.send(404, "text/html", 
            "<!DOCTYPE html><html><head><title>404 - Sayfa Bulunamadı</title></head>"
            "<body><h1>404 - Sayfa Bulunamadı</h1><p>İstediğiniz sayfa bulunamadı.</p>"
            "<a href='/'>Ana Sayfaya Dön</a></body></html>");
        return;
    }
    
    File file = SPIFFS.open(path, "r");
    if (!file) {
        addLog("Dosya açılamadı: " + path, ERROR, "WEB");
        server.send(500, "text/html",
            "<!DOCTYPE html><html><head><title>500 - Sunucu Hatası</title></head>"
            "<body><h1>500 - Sunucu Hatası</h1><p>Dosya okunamadı.</p></body></html>");
        return;
    }
    
    // Dosya boyutu kontrolü (DoS koruması)
    size_t fileSize = file.size();
    if (fileSize > 1048576) { // 1MB limit
        addLog("Dosya çok büyük: " + path + " (" + String(fileSize) + " bytes)", WARN, "WEB");
        file.close();
        server.send(413, "text/plain", "413: Dosya çok büyük");
        return;
    }
    
    server.streamFile(file, contentType);
    file.close();
}

String getUptime() {
    unsigned long totalSeconds = millis() / 1000;
    unsigned long days = totalSeconds / 86400;
    unsigned long hours = (totalSeconds % 86400) / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;
    
    String uptime = "";
    if (days > 0) uptime += String(days) + " gün, ";
    if (hours > 0) uptime += String(hours) + " saat, ";
    if (minutes > 0) uptime += String(minutes) + " dk, ";
    uptime += String(seconds) + " sn";
    
    return uptime;
}

// Session refresh endpoint
void handleSessionRefresh() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    refreshSession();
    server.send(200, "application/json", "{\"success\":true}");
}

// --- API Handler Fonksiyonları ---

void handleStatusAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    if (!checkRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Çok fazla istek\"}");
        return;
    }
    
    addSecurityHeaders();
    
    JsonDocument doc;
    doc["datetime"] = getCurrentDateTime();
    doc["uptime"] = getUptime();
    doc["deviceName"] = settings.deviceName;
    doc["tmName"] = settings.transformerStation;
    doc["deviceIP"] = settings.local_IP.toString();
    doc["baudRate"] = settings.currentBaudRate;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["chipModel"] = ESP.getChipModel();
    doc["cpuFreq"] = ESP.getCpuFreqMHz();
    
    // Durum göstergeleri
    doc["ethernetStatus"] = ETH.linkUp() ? 
        "<span class='status-good'>✅ Bağlı</span>" : 
        "<span class='status-error'>❌ Bağlantı Yok</span>";
        
    doc["ntpConfigStatus"] = ntpConfigured ? 
        "<span class='status-good'>✅ Yapılandırıldı</span>" : 
        "<span class='status-warning'>⚠️ Varsayılan</span>";
        
    doc["backendStatus"] = isTimeDataValid() ? 
        "<span class='status-good'>✅ Aktif</span>" : 
        "<span class='status-error'>❌ Veri Alınamıyor</span>";
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

void handleGetSettingsAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    JsonDocument doc;
    doc["deviceName"] = settings.deviceName;
    doc["tmName"] = settings.transformerStation;
    doc["username"] = settings.username;
    doc["sessionTimeout"] = settings.SESSION_TIMEOUT / 60000; // dakika cinsinden
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

void handlePostSettingsAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    String newDevName = server.arg("deviceName");
    String newTmName = server.arg("tmName");
    String newUsername = server.arg("username");
    String newPassword = server.arg("password");
    
    // XSS koruması - basit HTML tag temizleme
    newDevName.replace("<", "&lt;");
    newDevName.replace(">", "&gt;");
    newTmName.replace("<", "&lt;");
    newTmName.replace(">", "&gt;");
    newUsername.replace("<", "&lt;");
    newUsername.replace(">", "&gt;");
    
    if (!saveSettings(newDevName, newTmName, newUsername, newPassword)) {
        server.send(400, "application/json", "{\"error\":\"Ayarlar kaydedilemedi. Girilen değerleri kontrol edin.\"}");
        return;
    }
    
    server.send(200, "application/json", "{\"success\":true}");
}

void handleFaultRequest(bool isFirst) {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    if (!checkRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Çok fazla istek\"}");
        return;
    }
    
    addSecurityHeaders();
    
    bool success = isFirst ? requestFirstFault() : requestNextFault();
    if (success) {
        String response = getLastFaultResponse();
        // Response'u JSON için escape et
        response.replace("\"", "\\\"");
        response.replace("\n", "\\n");
        response.replace("\r", "\\r");
        
        server.send(200, "application/json", "{\"response\":\"" + response + "\"}");
        // String concatenation hatası düzeltildi
        String logMessage = "Arıza bilgisi istendi: ";
        logMessage += isFirst ? "İlk" : "Sonraki";
        addLog(logMessage, INFO, "FAULT");
    } else {
        server.send(500, "application/json", "{\"error\":\"İşlemciden yanıt alınamadı.\"}");
        // String concatenation hatası düzeltildi
        String logMessage = "Arıza bilgisi alınamadı: ";
        logMessage += isFirst ? "İlk" : "Sonraki";
        addLog(logMessage, ERROR, "FAULT");
    }
}

void handleGetNtpAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    JsonDocument doc;
    doc["ntpServer1"] = ntpConfig.ntpServer1;
    doc["ntpServer2"] = ntpConfig.ntpServer2;
    doc["timezone"] = ntpConfig.timezone;
    doc["enabled"] = ntpConfig.enabled;
    doc["configured"] = ntpConfigured;
    doc["syncStatus"] = isNTPSynced();
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

void handlePostNtpAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    String ntp1 = server.arg("ntpServer1");
    String ntp2 = server.arg("ntpServer2");
    int timezone = server.arg("timezone").toInt();
    
    if (!saveNTPSettings(ntp1, ntp2, timezone)) {
        server.send(400, "application/json", "{\"error\":\"Geçersiz NTP ayarları.\"}");
        return;
    }
    
    server.send(200, "application/json", "{\"success\":true}");
}

void handleGetBaudRateAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    JsonDocument doc;
    doc["baudRate"] = settings.currentBaudRate;
    
    // ArduinoJson v7 syntax kullanımı - deprecated warning düzeltildi
    JsonArray supportedRates = doc["supportedRates"].to<JsonArray>();
    supportedRates.add(9600);
    supportedRates.add(19200);
    supportedRates.add(38400);
    supportedRates.add(57600);
    supportedRates.add(115200);
    supportedRates.add(230400);
    supportedRates.add(460800);
    supportedRates.add(921600);
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

void handlePostBaudRateAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    long newBaud = server.arg("baud").toInt();
    
    // Desteklenen baud rate kontrolü
    const long validRates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    bool valid = false;
    for (int i = 0; i < 8; i++) {
        if (newBaud == validRates[i]) {
            valid = true;
            break;
        }
    }
    
    if (!valid) {
        server.send(400, "application/json", "{\"error\":\"Desteklenmeyen BaudRate değeri.\"}");
        return;
    }
    
    if (!changeBaudRate(newBaud)) {
        server.send(500, "application/json", "{\"error\":\"BaudRate değiştirilemedi.\"}");
        return;
    }
    
    server.send(200, "application/json", "{\"success\":true}");
}

void handleGetLogsAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    JsonDocument doc;
    JsonArray logsArray = doc.to<JsonArray>();
    
    // Logları ters sırada ekle (en yeni önce)
    for (int i = totalLogs - 1; i >= 0; i--) {
        int idx = (logIndex - 1 - i + 50) % 50;
        if (logs[idx].message.length() > 0) {
            JsonObject logObj = logsArray.add<JsonObject>();
            logObj["timestamp"] = logs[idx].timestamp;
            logObj["message"] = logs[idx].message;
            logObj["level"] = logLevelToString(logs[idx].level);
            logObj["source"] = logs[idx].source;
            logObj["millis"] = logs[idx].millis_time;
        }
    }
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

void handleClearLogsAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    clearLogs();
    server.send(200, "application/json", "{\"success\":true}");
}

// Sistem bilgileri API
void handleSystemInfoAPI() {
    if (!checkSession()) {
        server.send(401, "application/json", "{\"error\":\"Oturum geçersiz\"}");
        return;
    }
    
    addSecurityHeaders();
    
    JsonDocument doc;
    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["totalHeap"] = ESP.getHeapSize();
    doc["flashSize"] = ESP.getFlashChipSize();
    doc["sketchSize"] = ESP.getSketchSize();
    doc["freeSketchSpace"] = ESP.getFreeSketchSpace();
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

// --- Rota Kurulumu ---
void setupWebRoutes() {
    // Ana sayfalar - session kontrolü ile
    server.on("/", HTTP_GET, []() {
        if (!checkSession()) { 
            server.sendHeader("Location", "/login", true); 
            server.send(302, "text/plain", "Yönlendiriliyor..."); 
            return; 
        }
        serveStaticFile("/index.html", "text/html");
    });
    
    server.on("/login", HTTP_GET, []() { 
        // Zaten giriş yapmışsa ana sayfaya yönlendir
        if (checkSession()) {
            server.sendHeader("Location", "/", true);
            server.send(302, "text/plain", "Yönlendiriliyor...");
            return;
        }
        serveStaticFile("/login.html", "text/html"); 
    });
    
    // Statik dosyalar
    server.on("/style.css", HTTP_GET, []() { serveStaticFile("/style.css", "text/css"); });
    server.on("/script.js", HTTP_GET, []() { 
        addSecurityHeaders();
        serveStaticFile("/script.js", "application/javascript"); 
    });
    
    // Diğer sayfalar - session kontrolü ile
    server.on("/account", HTTP_GET, []() { 
        if (!checkSession()) { 
            server.sendHeader("Location", "/login", true); 
            server.send(302, "text/plain", ""); 
            return; 
        } 
        serveStaticFile("/account.html", "text/html"); 
    });
    
    server.on("/fault", HTTP_GET, []() { 
        if (!checkSession()) { 
            server.sendHeader("Location", "/login", true); 
            server.send(302, "text/plain", ""); 
            return; 
        } 
        serveStaticFile("/fault.html", "text/html"); 
    });
    
    server.on("/ntp", HTTP_GET, []() { 
        if (!checkSession()) { 
            server.sendHeader("Location", "/login", true); 
            server.send(302, "text/plain", ""); 
            return; 
        } 
        serveStaticFile("/ntp.html", "text/html"); 
    });
    
    server.on("/baudrate", HTTP_GET, []() { 
        if (!checkSession()) { 
            server.sendHeader("Location", "/login", true); 
            server.send(302, "text/plain", ""); 
            return; 
        } 
        serveStaticFile("/baudrate.html", "text/html"); 
    });
    
    server.on("/log", HTTP_GET, []() { 
        if (!checkSession()) { 
            server.sendHeader("Location", "/login", true); 
            server.send(302, "text/plain", ""); 
            return; 
        } 
        serveStaticFile("/log.html", "text/html"); 
    });

    // Kimlik doğrulama
    server.on("/login", HTTP_POST, handleUserLogin);
    server.on("/logout", HTTP_GET, handleUserLogout);
    
    // API endpoints
    server.on("/api/session/refresh", HTTP_POST, handleSessionRefresh);
    server.on("/api/status", HTTP_GET, handleStatusAPI);
    server.on("/api/system", HTTP_GET, handleSystemInfoAPI);
    server.on("/api/settings", HTTP_GET, handleGetSettingsAPI);
    server.on("/api/settings", HTTP_POST, handlePostSettingsAPI);
    server.on("/api/faults/first", HTTP_POST, []() { handleFaultRequest(true); });
    server.on("/api/faults/next", HTTP_POST, []() { handleFaultRequest(false); });
    server.on("/api/ntp", HTTP_GET, handleGetNtpAPI);
    server.on("/api/ntp", HTTP_POST, handlePostNtpAPI);
    server.on("/api/baudrate", HTTP_GET, handleGetBaudRateAPI);
    server.on("/api/baudrate", HTTP_POST, handlePostBaudRateAPI);
    server.on("/api/logs", HTTP_GET, handleGetLogsAPI);
    server.on("/api/logs/clear", HTTP_POST, handleClearLogsAPI);

    // 404 handler
    server.onNotFound([]() {
        addSecurityHeaders();
        addLog("404 - Bilinmeyen sayfa: " + server.uri(), WARN, "WEB");
        server.send(404, "text/html", 
            "<!DOCTYPE html><html><head><title>404 - Sayfa Bulunamadı</title></head>"
            "<body><h1>404 - Sayfa Bulunamadı</h1><p>İstediğiniz sayfa bulunamadı: " + server.uri() + "</p>"
            "<a href='/'>Ana Sayfaya Dön</a></body></html>");
    });

    server.begin();
    addLog("✅ Web sunucusu ve rotalar başlatıldı.", SUCCESS, "WEB");
}