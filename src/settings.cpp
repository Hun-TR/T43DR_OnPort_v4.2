#include "settings.h"
#include "log_system.h"
#include "crypto_utils.h"
#include <Preferences.h>

WebServer server(80);
Settings settings;

void loadSettings() {
    Preferences prefs;
    prefs.begin("app-settings", false);

    // Network ayarları - validation eklenmiş
    String ipStr = prefs.getString("local_ip", "192.168.1.160");
    String gwStr = prefs.getString("gateway", "192.168.1.1");
    String snStr = prefs.getString("subnet", "255.255.255.0");
    String dnsStr = prefs.getString("dns", "8.8.8.8");

    // IP adreslerinin validasyonu
    if (!settings.local_IP.fromString(ipStr)) {
        addLog("Geçersiz IP adresi, varsayılan kullanılıyor: 192.168.1.160", WARN, "SETTINGS");
        settings.local_IP.fromString("192.168.1.160");
    }
    if (!settings.gateway.fromString(gwStr)) {
        addLog("Geçersiz gateway adresi, varsayılan kullanılıyor", WARN, "SETTINGS");
        settings.gateway.fromString("192.168.1.1");
    }
    if (!settings.subnet.fromString(snStr)) {
        settings.subnet.fromString("255.255.255.0");
    }
    if (!settings.primaryDNS.fromString(dnsStr)) {
        settings.primaryDNS.fromString("8.8.8.8");
    }

    // String ayarları - uzunluk kontrolü eklenmiş
    settings.deviceName = prefs.getString("dev_name", "TEİAŞ EKLİM Cihazı");
    if (settings.deviceName.length() > 50) {
        settings.deviceName = settings.deviceName.substring(0, 50);
        addLog("Cihaz adı çok uzun, kısaltıldı.", WARN, "SETTINGS");
    }

    settings.transformerStation = prefs.getString("tm_name", "Belirtilmemiş");
    if (settings.transformerStation.length() > 50) {
        settings.transformerStation = settings.transformerStation.substring(0, 50);
    }

    settings.username = prefs.getString("username", "admin");
    if (settings.username.length() > 30) {
        settings.username = settings.username.substring(0, 30);
    }

    // BaudRate validasyonu
    long baudRate = prefs.getLong("baudrate", 115200);
    const long validBaudRates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    bool validBaud = false;
    for (int i = 0; i < 8; i++) {
        if (baudRate == validBaudRates[i]) {
            validBaud = true;
            break;
        }
    }
    
    if (!validBaud) {
        addLog("Geçersiz BaudRate, varsayılan 115200 kullanılıyor.", WARN, "SETTINGS");
        settings.currentBaudRate = 115200;
    } else {
        settings.currentBaudRate = baudRate;
    }

    // Güvenlik ayarları
    settings.passwordSalt = prefs.getString("p_salt", "");
    settings.passwordHash = prefs.getString("p_hash", "");

    // İlk kurulum kontrolü
    if (settings.passwordSalt.length() == 0 || settings.passwordHash.length() == 0) {
        addLog("İlk kurulum tespit edildi. Varsayılan parola '1234' ayarlanıyor.", WARN, "SETTINGS");
        
        // Güvenli rastgele tuz oluştur
        settings.passwordSalt = generateSalt(16);
        settings.passwordHash = sha256("1234", settings.passwordSalt);

        // Kaydet
        prefs.putString("p_salt", settings.passwordSalt);
        prefs.putString("p_hash", settings.passwordHash);
        prefs.putString("username", settings.username);
        
        addLog("Varsayılan ayarlar kaydedildi. Lütfen parolanızı değiştirin!", WARN, "SETTINGS");
    }

    prefs.end();

    // Session ayarları
    settings.isLoggedIn = false;
    settings.sessionStartTime = 0;
    settings.SESSION_TIMEOUT = 1800000; // 30 dakika

    addLog("Ayarlar başarıyla yüklendi.", SUCCESS, "SETTINGS");
}

bool saveSettings(const String& newDevName, const String& newTmName, const String& newUsername, const String& newPassword) {
    // Input validation
    if (newDevName.length() < 3 || newDevName.length() > 50) {
        addLog("Geçersiz cihaz adı uzunluğu (3-50 karakter).", ERROR, "SETTINGS");
        return false;
    }
    
    if (newUsername.length() < 3 || newUsername.length() > 30) {
        addLog("Geçersiz kullanıcı adı uzunluğu (3-30 karakter).", ERROR, "SETTINGS");
        return false;
    }
    
    if (newTmName.length() > 50) {
        addLog("Trafo merkezi adı çok uzun (max 50 karakter).", ERROR, "SETTINGS");
        return false;
    }

    // Parola kontrolü
    if (newPassword.length() > 0) {
        if (newPassword.length() < 4 || newPassword.length() > 50) {
            addLog("Parola 4-50 karakter arasında olmalıdır.", ERROR, "SETTINGS");
            return false;
        }
        
        if (!isPasswordStrong(newPassword)) {
            addLog("Parola yeterince güçlü değil. En az 2 farklı karakter türü kullanın.", WARN, "SETTINGS");
        }
    }

    Preferences prefs;
    prefs.begin("app-settings", false);

    // Ayarları kaydet
    settings.deviceName = newDevName;
    prefs.putString("dev_name", newDevName);

    settings.transformerStation = newTmName;
    prefs.putString("tm_name", newTmName);

    settings.username = newUsername;
    prefs.putString("username", newUsername);

    // Parola değişikliği
    if (newPassword.length() > 0) {
        settings.passwordSalt = generateSalt(16);
        settings.passwordHash = sha256(newPassword, settings.passwordSalt);

        prefs.putString("p_salt", settings.passwordSalt);
        prefs.putString("p_hash", settings.passwordHash);
        
        addLog("Parola güncellendi.", SUCCESS, "SETTINGS");
        
        // Güvenlik için mevcut oturumu sonlandır
        settings.isLoggedIn = false;
    }

    prefs.end();
    addLog("Cihaz ayarları güncellendi ve kaydedildi.", SUCCESS, "SETTINGS");
    return true;
}

void initEthernet() {
    addLog("Ethernet başlatılıyor...", INFO, "ETH");
    
    ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);
    
    if (!ETH.config(settings.local_IP, settings.gateway, settings.subnet, settings.primaryDNS)) {
        addLog("❌ Statik IP atanamadı!", ERROR, "ETH");
    } else {
        addLog("✅ Statik IP atandı: " + settings.local_IP.toString(), SUCCESS, "ETH");
    }

    // Ethernet bağlantı durumunu kontrol et
    unsigned long startTime = millis();
    while (!ETH.linkUp() && millis() - startTime < 10000) { // 10 saniye bekle
        delay(100);
    }
    
    if (ETH.linkUp()) {
        addLog("✅ Ethernet bağlantısı aktif.", SUCCESS, "ETH");
    } else {
        addLog("⚠️ Ethernet kablosu bağlı değil.", WARN, "ETH");
    }
}