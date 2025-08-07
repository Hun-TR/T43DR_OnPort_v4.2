#include <Arduino.h>
#include <SPIFFS.h>
#include "settings.h"
#include "log_system.h"
#include "uart_handler.h"
#include "ntp_handler.h"
#include "web_routes.h"

// Sistem durumu takibi için değişkenler
unsigned long lastHeapCheck = 0;
unsigned long lastStatusLog = 0;
size_t minFreeHeap = SIZE_MAX;

// Watchdog timer için
unsigned long lastWatchdogFeed = 0;
const unsigned long WATCHDOG_TIMEOUT = 30000; // 30 saniye

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Sistem başlangıç logu
  Serial.println("\n=== TEİAŞ EKLİM Cihazı Başlatılıyor ===");
  Serial.println("Chip Model: " + String(ESP.getChipModel()));
  Serial.println("CPU Freq: " + String(ESP.getCpuFreqMHz()) + " MHz");
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("Flash Size: " + String(ESP.getFlashChipSize()) + " bytes");
  
  // Dosya sistemini başlat
  Serial.print("SPIFFS başlatılıyor... ");
  if(!SPIFFS.begin(true)){
    Serial.println("BAŞARISIZ!");
    Serial.println("HATA: SPIFFS dosya sistemi bağlanamadı!");
    
    // Sistem kritik hata durumunda LED ile uyarı
    pinMode(2, OUTPUT); // Built-in LED
    for(int i = 0; i < 10; i++) {
      digitalWrite(2, HIGH);
      delay(200);
      digitalWrite(2, LOW);
      delay(200);
    }
    
    // Sistem yeniden başlatılsın
    ESP.restart();
    return;
  }
  Serial.println("BAŞARILI");
  
  // SPIFFS dosya sistemi bilgileri
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  Serial.println("SPIFFS Total: " + String(totalBytes) + " bytes");
  Serial.println("SPIFFS Used: " + String(usedBytes) + " bytes (" + 
                String((usedBytes * 100) / totalBytes) + "%)");

  // Modülleri sırasıyla başlat
  Serial.println("\n=== Modüller Başlatılıyor ===");
  
  // 1. Log sistemi
  Serial.print("Log sistemi... ");
  initLogSystem();
  Serial.println("BAŞARILI");
  
  // 2. Ayarları yükle
  Serial.print("Ayarlar yükleniyor... ");
  loadSettings();
  Serial.println("BAŞARILI");
  
  // 3. Ethernet başlat
  Serial.print("Ethernet başlatılıyor... ");
  initEthernet();
  Serial.println("BAŞARILI");
  
  // 4. UART başlat
  Serial.print("UART başlatılıyor... ");
  initUART();
  Serial.println("BAŞARILI");
  
  // 5. NTP handler başlat
  Serial.print("NTP Handler başlatılıyor... ");
  initNTPHandler();
  Serial.println("BAŞARULI");
  
  // 6. Web sunucusu ve rotalar
  Serial.print("Web sunucusu başlatılıyor... ");
  setupWebRoutes();
  Serial.println("BAŞARILI");
  
  // Başlangıç heap durumunu kaydet
  minFreeHeap = ESP.getFreeHeap();
  
  Serial.println("\n=== SİSTEM HAZIR ===");
  Serial.println("Web Arayüzü: http://" + settings.local_IP.toString());
  Serial.println("Varsayılan Giriş: admin / 1234");
  Serial.println("========================\n");
  
  addLog("🚀 Sistem başarıyla başlatıldı.", SUCCESS, "SYSTEM");
  addLog("Web arayüzü aktif: http://" + settings.local_IP.toString(), INFO, "SYSTEM");
}

// Heap durumunu kontrol eden fonksiyon
void checkSystemHealth() {
  size_t currentHeap = ESP.getFreeHeap();
  
  // Minimum heap değerini güncelle
  if (currentHeap < minFreeHeap) {
    minFreeHeap = currentHeap;
  }
  
  // Heap kullanımı kritik seviyeye düştüyse uyar
  if (currentHeap < 20000) { // 20KB altında
    addLog("⚠️ UYARI: Düşük bellek! Free Heap: " + String(currentHeap), WARN, "SYSTEM");
  }
  
  // Çok kritik durumda sistem yeniden başlat
  if (currentHeap < 10000) { // 10KB altında
    addLog("🔄 KRİTİK: Bellek tükendi! Sistem yeniden başlatılıyor...", ERROR, "SYSTEM");
    delay(1000);
    ESP.restart();
  }
}

// Periyodik sistem durumu logu
void logSystemStatus() {
  addLog("📊 Sistem Durumu - Heap: " + String(ESP.getFreeHeap()) + 
         "B, Min: " + String(minFreeHeap) + 
         "B, Uptime: " + String(millis() / 1000) + "s", DEBUG, "SYSTEM");
}

// Watchdog timer fonksiyonu
void feedWatchdog() {
  lastWatchdogFeed = millis();
}

// Sistem donma kontrolü
void checkWatchdog() {
  if (millis() - lastWatchdogFeed > WATCHDOG_TIMEOUT) {
    addLog("🔄 WATCHDOG: Sistem yanıt vermiyor! Yeniden başlatılıyor...", ERROR, "SYSTEM");
    delay(1000);
    ESP.restart();
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  // Ana işlemler
  server.handleClient();
  processReceivedData(); // NTP handler - arka porttan veri işleme
  
  // Watchdog besleme
  feedWatchdog();
  
  // Periyodik sistem kontrolleri
  if (currentTime - lastHeapCheck > 5000) { // Her 5 saniyede
    checkSystemHealth();
    lastHeapCheck = currentTime;
  }
  
  if (currentTime - lastStatusLog > 300000) { // Her 5 dakikada
    logSystemStatus();
    lastStatusLog = currentTime;
  }
  
  // Watchdog kontrolü
  checkWatchdog();
  
  // Ethernet bağlantı durumu kontrolü
  static bool lastEthStatus = false;
  bool currentEthStatus = ETH.linkUp();
  if (currentEthStatus != lastEthStatus) {
    if (currentEthStatus) {
      addLog("✅ Ethernet bağlantısı yeniden kuruldu.", SUCCESS, "ETH");
    } else {
      addLog("❌ Ethernet bağlantısı kesildi.", ERROR, "ETH");
    }
    lastEthStatus = currentEthStatus;
  }
  
  // Session timeout kontrolü - aktif oturum varsa
  if (settings.isLoggedIn) {
    if (millis() - settings.sessionStartTime > settings.SESSION_TIMEOUT) {
      settings.isLoggedIn = false;
      addLog("Oturum otomatik olarak sonlandırıldı (timeout).", INFO, "AUTH");
    }
  }
  
  // CPU'ya nefes aldır - loop delay
  delay(10);
}