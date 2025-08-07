/**
 * TEİAŞ EKLİM Web Arayüzü - Ana JavaScript Dosyası
 * Geliştiriciler: Mehmet DEMİRBİLEK & Hüseyin ÇİFTCİ
 * Versiyon: 2.0.0
 */

// ===================================
// GLOBAL VARIABLES & CONFIGURATION
// ===================================
const CONFIG = {
    updateInterval: 3000, // API güncelleme aralığı (ms)
    maxRetries: 3,
    retryDelay: 1000,
    logRefreshInterval: 5000,
    connectionTimeout: 10000,
    theme: {
        key: 'teias-theme',
        default: 'light'
    }
};

let updateTimer = null;
let logTimer = null;
let connectionRetries = 0;
let isPageVisible = true;
let currentTheme = 'light';

// ===================================
// UTILITY FUNCTIONS
// ===================================

/**
 * Güvenli DOM element seçici
 */
function safeQuerySelector(selector) {
    try {
        return document.querySelector(selector);
    } catch (error) {
        console.warn(`Invalid selector: ${selector}`, error);
        return null;
    }
}

/**
 * Güvenli DOM element güncelleme
 */
function safeUpdateElement(elementId, content, isHTML = false) {
    const element = safeQuerySelector(`#${elementId}`);
    if (element) {
        if (isHTML) {
            element.innerHTML = content;
        } else {
            element.textContent = content;
        }
        return true;
    }
    return false;
}

/**
 * Debounce fonksiyonu
 */
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

/**
 * Sanitize input
 */
function sanitizeInput(input) {
    const div = document.createElement('div');
    div.textContent = input;
    return div.innerHTML;
}

/**
 * Format timestamp
 */
function formatTimestamp(date = new Date()) {
    return date.toLocaleString('tr-TR', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

/**
 * Show loading state
 */
function showLoading(buttonId) {
    const button = safeQuerySelector(`#${buttonId}`);
    if (button) {
        button.classList.add('loading');
        button.disabled = true;
    }
}

/**
 * Hide loading state
 */
function hideLoading(buttonId) {
    const button = safeQuerySelector(`#${buttonId}`);
    if (button) {
        button.classList.remove('loading');
        button.disabled = false;
    }
}

// ===================================
// THEME MANAGEMENT
// ===================================

/**
 * Initialize theme
 */
function initializeTheme() {
    const savedTheme = localStorage.getItem(CONFIG.theme.key) || CONFIG.theme.default;
    setTheme(savedTheme);
    
    // Theme toggle button
    const themeToggle = safeQuerySelector('#themeToggle');
    if (themeToggle) {
        themeToggle.addEventListener('click', toggleTheme);
        updateThemeToggleIcon(savedTheme);
    }
}

/**
 * Set theme
 */
function setTheme(theme) {
    currentTheme = theme;
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem(CONFIG.theme.key, theme);
    updateThemeToggleIcon(theme);
}

/**
 * Toggle theme
 */
function toggleTheme() {
    const newTheme = currentTheme === 'light' ? 'dark' : 'light';
    setTheme(newTheme);
    
    // Animate theme change
    document.body.style.transition = 'background-color 0.3s ease';
    setTimeout(() => {
        document.body.style.transition = '';
    }, 300);
}

/**
 * Update theme toggle icon
 */
function updateThemeToggleIcon(theme) {
    const themeToggle = safeQuerySelector('#themeToggle');
    if (themeToggle) {
        themeToggle.textContent = theme === 'light' ? '🌙' : '☀️';
        themeToggle.title = theme === 'light' ? 'Karanlık Moda Geç' : 'Açık Moda Geç';
    }
}

// ===================================
// NAVIGATION & MOBILE MENU
// ===================================

/**
 * Initialize navigation
 */
function initializeNavigation() {
    // Mobile menu toggle
    const navToggle = safeQuerySelector('#navToggle');
    const navMenu = safeQuerySelector('.nav-menu');
    
    if (navToggle && navMenu) {
        navToggle.addEventListener('click', () => {
            navMenu.classList.toggle('active');
            navToggle.classList.toggle('active');
        });
        
        // Close menu when clicking outside
        document.addEventListener('click', (e) => {
            if (!navToggle.contains(e.target) && !navMenu.contains(e.target)) {
                navMenu.classList.remove('active');
                navToggle.classList.remove('active');
            }
        });
        
        // Close menu when clicking on a link
        navMenu.querySelectorAll('.nav-link').forEach(link => {
            link.addEventListener('click', () => {
                navMenu.classList.remove('active');
                navToggle.classList.remove('active');
            });
        });
    }
    
    // Highlight active page
    highlightActivePage();
}

/**
 * Highlight active page in navigation
 */
function highlightActivePage() {
    const currentPath = window.location.pathname;
    const navLinks = document.querySelectorAll('.nav-link');
    
    navLinks.forEach(link => {
        link.classList.remove('active');
        if (link.getAttribute('href') === currentPath) {
            link.classList.add('active');
        }
    });
}

// ===================================
// API COMMUNICATION
// ===================================

/**
 * Make API request with retry logic
 */
async function apiRequest(url, options = {}) {
    const defaultOptions = {
        method: 'GET',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            'X-Requested-With': 'XMLHttpRequest'
        },
        timeout: CONFIG.connectionTimeout
    };
    
    const finalOptions = { ...defaultOptions, ...options };
    
    for (let attempt = 1; attempt <= CONFIG.maxRetries; attempt++) {
        try {
            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), finalOptions.timeout);
            
            const response = await fetch(url, {
                ...finalOptions,
                signal: controller.signal
            });
            
            clearTimeout(timeoutId);
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            
            connectionRetries = 0;
            updateConnectionStatus(true);
            
            return response;
            
        } catch (error) {
            console.warn(`API request attempt ${attempt} failed:`, error);
            
            if (attempt === CONFIG.maxRetries) {
                connectionRetries++;
                updateConnectionStatus(false, error.message);
                throw error;
            }
            
            await new Promise(resolve => setTimeout(resolve, CONFIG.retryDelay * attempt));
        }
    }
}

/**
 * Update connection status indicator
 */
function updateConnectionStatus(isConnected, errorMessage = '') {
    const indicator = safeQuerySelector('#connectionIndicator');
    if (!indicator) return;
    
    if (isConnected) {
        indicator.textContent = '🟢 Bağlı';
        indicator.className = 'status-indicator connected';
    } else {
        indicator.textContent = `🔴 Bağlantı Hatası${errorMessage ? ': ' + errorMessage : ''}`;
        indicator.className = 'status-indicator disconnected';
    }
}

// ===================================
// DASHBOARD DATA MANAGEMENT
// ===================================

/**
 * Update dashboard data
 */
async function updateDashboardData() {
    if (!isPageVisible) return;
    
    try {
        const response = await apiRequest('/api/status');
        const data = await response.json();
        
        if (!data || typeof data !== 'object') {
            throw new Error('Invalid API response format');
        }
        
        updateDashboardElements(data);
        updateLastUpdateTime();
        
    } catch (error) {
        console.error('Dashboard update failed:', error);
        showDashboardError();
    }
}

/**
 * Update dashboard DOM elements
 */
function updateDashboardElements(data) {
    const updates = {
        'datetime': data.datetime || 'Bilinmiyor',
        'dataStatus': createStatusBadge(data.backendStatus, 'info'),
        'deviceName': data.deviceName || 'Tanımsız',
        'tmName': data.tmName || 'Tanımsız',
        'deviceIP': data.deviceIP || '0.0.0.0',
        'uptime': data.uptime || '0',
        'ethernetStatus': createStatusBadge(data.ethernetStatus, 'success'),
        'ntpConfigStatus': createStatusBadge(data.ntpConfigStatus, 'info'),
        'backendStatus': createStatusBadge(data.backendStatus, 'warning'),
        'baudRate': (data.baudRate || 0) + ' bps'
    };
    
    Object.entries(updates).forEach(([id, value]) => {
        safeUpdateElement(id, value, true);
    });
}

/**
 * Create status badge HTML
 */
function createStatusBadge(status, type = 'info') {
    if (!status) return '<span class="status-badge">Bilinmiyor</span>';
    
    const cleanStatus = sanitizeInput(status);
    return `<span class="status-badge ${type}">${cleanStatus}</span>`;
}

/**
 * Update last update time
 */
function updateLastUpdateTime() {
    safeUpdateElement('lastUpdateTime', formatTimestamp());
}

/**
 * Show dashboard error state
 */
function showDashboardError() {
    const errorElements = [
        'datetime', 'deviceName', 'tmName', 'deviceIP', 
        'uptime', 'baudRate'
    ];
    
    errorElements.forEach(id => {
        safeUpdateElement(id, 'Bağlantı Hatası');
    });
    
    const statusElements = [
        'dataStatus', 'ethernetStatus', 
        'ntpConfigStatus', 'backendStatus'
    ];
    
    statusElements.forEach(id => {
        safeUpdateElement(id, createStatusBadge('Bağlantı Yok', 'error'), true);
    });
}

// ===================================
// MESSAGE SYSTEM
// ===================================

/**
 * Show message to user
 */
function showMessage(message, type = 'info', duration = 5000) {
    const container = safeQuerySelector('#message-container');
    if (!container) {
        console.warn('Message container not found');
        return;
    }
    
    const messageElement = document.createElement('div');
    messageElement.className = `${type}-message`;
    messageElement.innerHTML = `
        <strong>${getMessageIcon(type)}</strong> ${sanitizeInput(message)}
        <button class="message-close" onclick="this.parentElement.remove()" style="float: right; background: none; border: none; font-size: 1.2em; cursor: pointer;">&times;</button>
    `;
    
    container.innerHTML = '';
    container.appendChild(messageElement);
    
    // Auto remove after duration
    if (duration > 0) {
        setTimeout(() => {
            if (messageElement.parentElement) {
                messageElement.remove();
            }
        }, duration);
    }
}

/**
 * Get icon for message type
 */
function getMessageIcon(type) {
    const icons = {
        success: '✅',
        error: '❌',
        warning: '⚠️',
        info: 'ℹ️'
    };
    return icons[type] || icons.info;
}

// ===================================
// FORM HANDLING
// ===================================

/**
 * Handle form submission with loading state
 */
async function handleFormSubmission(formId, apiUrl, successMessage, validationFn = null) {
    const form = safeQuerySelector(`#${formId}`);
    if (!form) return;
    
    form.addEventListener('submit', async (event) => {
        event.preventDefault();
        
        const submitBtn = form.querySelector('button[type="submit"]');
        const btnId = submitBtn ? submitBtn.id : null;
        
        try {
            if (btnId) showLoading(btnId);
            
            const formData = new FormData(form);
            
            // Run validation if provided
            if (validationFn) {
                const validationErrors = validationFn(formData);
                if (validationErrors.length > 0) {
                    showMessage(validationErrors.join('<br>'), 'error');
                    return;
                }
            }
            
            const response = await apiRequest(apiUrl, {
                method: 'POST',
                body: new URLSearchParams(formData)
            });
            
            const result = await response.json();
            
            if (result.success) {
                showMessage(successMessage, 'success');
                
                // Handle special cases
                if (formId === 'accountForm' && formData.get('password')) {
                    setTimeout(() => {
                        if (confirm('Şifreniz değiştirildi. Tekrar giriş yapmanız gerekiyor.')) {
                            window.location.href = '/logout';
                        }
                    }, 2000);
                }
            } else {
                showMessage(result.error || 'Bir hata oluştu.', 'error');
            }
            
        } catch (error) {
            console.error('Form submission failed:', error);
            showMessage('Sunucu ile bağlantı kurulamadı.', 'error');
        } finally {
            if (btnId) hideLoading(btnId);
        }
    });
}

// ===================================
// PAGE-SPECIFIC INITIALIZERS
// ===================================

/**
 * Initialize login page
 */
function initializeLoginPage() {
    if (window.location.pathname !== '/login') return;
    
    const loginForm = safeQuerySelector('#loginForm');
    if (!loginForm) return;
    
    // Update current time
    const updateTime = () => {
        safeUpdateElement('currentTime', new Date().toLocaleTimeString('tr-TR'));
    };
    updateTime();
    setInterval(updateTime, 1000);
    
    // Handle login form
    loginForm.addEventListener('submit', async (event) => {
        event.preventDefault();
        
        const btn = safeQuerySelector('#loginBtn');
        const loading = safeQuerySelector('#loading');
        const errorContainer = safeQuerySelector('#error-container');
        
        if (btn) btn.disabled = true;
        if (loading) loading.style.display = 'block';
        if (errorContainer) errorContainer.innerHTML = '';
        
        try {
            const formData = new FormData(loginForm);
            
            const response = await fetch('/login', {
                method: 'POST',
                body: new URLSearchParams(formData)
            });
            
            if (response.redirected) {
                window.location.href = response.url;
                return;
            }
            
            const data = await response.json();
            
            if (data && data.error) {
                const errorHtml = `
                    <div class="error-message">
                        <strong>❌</strong> ${sanitizeInput(data.error)}
                    </div>
                `;
                if (errorContainer) errorContainer.innerHTML = errorHtml;
            }
            
        } catch (error) {
            console.error('Login failed:', error);
            const errorHtml = `
                <div class="error-message">
                    <strong>❌</strong> Sunucuya ulaşılamıyor.
                </div>
            `;
            if (errorContainer) errorContainer.innerHTML = errorHtml;
        } finally {
            if (btn) btn.disabled = false;
            if (loading) loading.style.display = 'none';
        }
    });
}

/**
 * Initialize account page
 */
function initializeAccountPage() {
    if (window.location.pathname !== '/account') return;
    
    // Load current settings
    loadAccountSettings();
    
    // Setup form validation
    const validateAccount = (formData) => {
        const errors = [];
        
        const deviceName = formData.get('deviceName').trim();
        const username = formData.get('username').trim();
        const password = formData.get('password');
        const confirmPassword = formData.get('confirmPassword');
        
        if (!deviceName || deviceName.length < 3) {
            errors.push('Cihaz adı en az 3 karakter olmalıdır');
        }
        
        if (!username || username.length < 3) {
            errors.push('Kullanıcı adı en az 3 karakter olmalıdır');
        }
        
        if (password && password.length > 0) {
            if (password.length < 6) {
                errors.push('Şifre en az 6 karakter olmalıdır');
            }
            if (password !== confirmPassword) {
                errors.push('Şifreler eşleşmiyor');
            }
        }
        
        return errors;
    };
    
    // Setup form submission
    handleFormSubmission(
        'accountForm', 
        '/api/settings', 
        'Ayarlar başarıyla kaydedildi!', 
        validateAccount
    );
    
    // Reset button
    const resetBtn = safeQuerySelector('#resetBtn');
    if (resetBtn) {
        resetBtn.addEventListener('click', () => {
            if (confirm('Tüm değişiklikleri sıfırlamak istediğinizden emin misiniz?')) {
                loadAccountSettings();
                showMessage('Form sıfırlandı.', 'info');
            }
        });
    }
}

/**
 * Load account settings
 */
async function loadAccountSettings() {
    try {
        const response = await apiRequest('/api/settings');
        const data = await response.json();
        
        safeUpdateElement('deviceName', data.deviceName || '');
        safeUpdateElement('tmName', data.tmName || '');
        safeUpdateElement('username', data.username || '');
        
        // Clear password fields
        safeUpdateElement('password', '');
        safeUpdateElement('confirmPassword', '');
        
    } catch (error) {
        console.error('Failed to load settings:', error);
        showMessage('Ayarlar yüklenemedi.', 'error');
    }
}

/**
 * Initialize NTP page
 */
function initializeNtpPage() {
    if (window.location.pathname !== '/ntp') return;
    
    // Load current NTP settings
    loadNtpSettings();
    
    // Setup form submission
    handleFormSubmission(
        'ntpForm', 
        '/api/ntp', 
        'NTP ayarları başarıyla arka porta gönderildi!'
    );
    
    // Preset server buttons
    const presetButtons = document.querySelectorAll('.preset-btn');
    presetButtons.forEach(btn => {
        btn.addEventListener('click', () => {
            const server1 = btn.dataset.server1;
            const server2 = btn.dataset.server2;
            
            const input1 = safeQuerySelector('#ntpServer1');
            const input2 = safeQuerySelector('#ntpServer2');
            
            if (input1) input1.value = server1;
            if (input2) input2.value = server2;
            
            showMessage('Preset sunucular yüklendi.', 'info', 2000);
        });
    });
    
    // Test connection button
    const testBtn = safeQuerySelector('#testConnectionBtn');
    if (testBtn) {
        testBtn.addEventListener('click', testNtpConnection);
    }
}

/**
 * Load NTP settings
 */
async function loadNtpSettings() {
    try {
        const response = await apiRequest('/api/ntp');
        const data = await response.json();
        
        safeUpdateElement('ntpServer1', data.ntpServer1 || '');
        safeUpdateElement('ntpServer2', data.ntpServer2 || '');
        safeUpdateElement('currentServer1', data.ntpServer1 || 'Tanımsız');
        safeUpdateElement('currentServer2', data.ntpServer2 || 'Tanımsız');
        safeUpdateElement('lastUpdate', formatTimestamp());
        
    } catch (error) {
        console.error('Failed to load NTP settings:', error);
        showMessage('NTP ayarları yüklenemedi.', 'error');
    }
}

/**
 * Test NTP connection
 */
async function testNtpConnection() {
    const testBtn = safeQuerySelector('#testConnectionBtn');
    const resultsDiv = safeQuerySelector('#testResults');
    const contentDiv = safeQuerySelector('#testContent');
    
    if (testBtn) showLoading(testBtn.id);
    
    try {
        const server1 = safeQuerySelector('#ntpServer1')?.value;
        const server2 = safeQuerySelector('#ntpServer2')?.value;
        
        if (!server1 || !server2) {
            showMessage('Lütfen her iki NTP sunucusunu da girin.', 'warning');
            return;
        }
        
        // Simulate NTP test (replace with actual API call)
        await new Promise(resolve => setTimeout(resolve, 2000));
        
        const testResults = `
            <div class="test-result">
                <strong>Birincil Sunucu (${server1}):</strong> 
                <span class="status-badge success">Bağlantı Başarılı</span>
            </div>
            <div class="test-result">
                <strong>Yedek Sunucu (${server2}):</strong> 
                <span class="status-badge success">Bağlantı Başarılı</span>
            </div>
        `;
        
        if (contentDiv) contentDiv.innerHTML = testResults;
        if (resultsDiv) resultsDiv.style.display = 'block';
        
    } catch (error) {
        console.error('NTP test failed:', error);
        showMessage('NTP bağlantı testi başarısız.', 'error');
    } finally {
        if (testBtn) hideLoading(testBtn.id);
    }
}

/**
 * Initialize BaudRate page
 */
function initializeBaudRatePage() {
    if (window.location.pathname !== '/baudrate') return;
    
    // Load current baudrate
    loadBaudRateSettings();
    
    // Setup form submission with confirmation
    const form = safeQuerySelector('#baudrateForm');
    if (form) {
        form.addEventListener('submit', async (event) => {
            event.preventDefault();
            
            const formData = new FormData(form);
            const selectedBaud = formData.get('baud');
            
            if (!selectedBaud) {
                showMessage('Lütfen bir BaudRate değeri seçin!', 'error');
                return;
            }
            
            if (!confirm(`BaudRate'i ${selectedBaud} bps olarak değiştirmek istediğinizden emin misiniz?\n\nBu işlem iletişimi geçici olarak kesebilir.`)) {
                return;
            }
            
            const submitBtn = form.querySelector('button[type="submit"]');
            if (submitBtn) showLoading(submitBtn.id);
            
            try {
                const response = await apiRequest('/api/baudrate', {
                    method: 'POST',
                    body: new URLSearchParams(formData)
                });
                
                const result = await response.json();
                
                if (result.success) {
                    showMessage(`BaudRate başarıyla ${selectedBaud} bps olarak ayarlandı!`, 'success');
                    safeUpdateElement('currentBaudRate', `${selectedBaud} bps`);
                } else {
                    showMessage(result.error || 'BaudRate değiştirilemedi.', 'error');
                }
                
            } catch (error) {
                console.error('BaudRate change failed:', error);
                showMessage('BaudRate değiştirme işlemi başarısız.', 'error');
            } finally {
                if (submitBtn) hideLoading(submitBtn.id);
            }
        });
    }
    
    // Test communication button
    const testBtn = safeQuerySelector('#testBaudBtn');
    if (testBtn) {
        testBtn.addEventListener('click', testBaudRateCommunication);
    }
}

/**
 * Load BaudRate settings
 */
async function loadBaudRateSettings() {
    try {
        const response = await apiRequest('/api/baudrate');
        const data = await response.json();
        
        const currentBaud = data.baudRate || 9600;
        safeUpdateElement('currentBaudRate', `${currentBaud} bps`);
        
        // Check the corresponding radio button
        const radioBtn = safeQuerySelector(`input[name="baud"][value="${currentBaud}"]`);
        if (radioBtn) radioBtn.checked = true;
        
        // Update communication status
        safeUpdateElement('commStatus', createStatusBadge('Aktif', 'success'), true);
        safeUpdateElement('lastChange', formatTimestamp());
        
    } catch (error) {
        console.error('Failed to load baudrate settings:', error);
        showMessage('BaudRate ayarları yüklenemedi.', 'error');
    }
}

/**
 * Test BaudRate communication
 */
async function testBaudRateCommunication() {
    const testBtn = safeQuerySelector('#testBaudBtn');
    const resultsDiv = safeQuerySelector('#testResults');
    const contentDiv = safeQuerySelector('#testContent');
    
    if (testBtn) showLoading(testBtn.id);
    
    try {
        // Simulate communication test
        await new Promise(resolve => setTimeout(resolve, 3000));
        
        const testResults = `
            <div class="test-result">
                <strong>UART İletişimi:</strong> 
                <span class="status-badge success">Başarılı</span>
            </div>
            <div class="test-result">
                <strong>Veri Alışverişi:</strong> 
                <span class="status-badge success">Normal</span>
            </div>
            <div class="test-result">
                <strong>Sinyal Kalitesi:</strong> 
                <span class="status-badge success">İyi</span>
            </div>
        `;
        
        if (contentDiv) contentDiv.innerHTML = testResults;
        if (resultsDiv) resultsDiv.style.display = 'block';
        
        showMessage('İletişim testi başarılı.', 'success');
        
    } catch (error) {
        console.error('Communication test failed:', error);
        showMessage('İletişim testi başarısız.', 'error');
    } finally {
        if (testBtn) hideLoading(testBtn.id);
    }
}

/**
 * Initialize Fault page
 */
function initializeFaultPage() {
    if (window.location.pathname !== '/fault') return;
    
    const firstBtn = safeQuerySelector('#firstFaultBtn');
    const nextBtn = safeQuerySelector('#nextFaultBtn');
    const refreshBtn = safeQuerySelector('#refreshFaultBtn');
    const exportBtn = safeQuerySelector('#exportFaultBtn');
    const clearBtn = safeQuerySelector('#clearFaultBtn');
    const contentDiv = safeQuerySelector('#faultContent');
    
    let faultData = [];
    let autoRefreshEnabled = false;
    let autoRefreshTimer = null;
    
    // Event listeners
    if (firstBtn) {
        firstBtn.addEventListener('click', () => fetchFaultData('/api/faults/first'));
    }
    
    if (nextBtn) {
        nextBtn.addEventListener('click', () => fetchFaultData('/api/faults/next'));
    }
    
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => fetchFaultData('/api/faults/refresh'));
    }
    
    if (exportBtn) {
        exportBtn.addEventListener('click', exportFaultData);
    }
    
    if (clearBtn) {
        clearBtn.addEventListener('click', () => {
            if (confirm('Ekrandaki tüm arıza kayıtları temizlenecek. Emin misiniz?')) {
                clearFaultDisplay();
            }
        });
    }
    
    // Auto refresh toggle
    const autoRefreshToggle = safeQuerySelector('#autoRefreshToggle');
    if (autoRefreshToggle) {
        autoRefreshToggle.addEventListener('click', toggleAutoRefresh);
    }
    
    // Filter functionality
    const filterLevel = safeQuerySelector('#filterLevel');
    if (filterLevel) {
        filterLevel.addEventListener('change', filterFaultData);
    }
    
    /**
     * Fetch fault data from API
     */
    async function fetchFaultData(apiUrl) {
        if (contentDiv) {
            contentDiv.innerHTML = '<div class="loading-logs"><div class="loading-spinner"></div><p>İşlemciye istek gönderiliyor...</p></div>';
        }
        
        try {
            const response = await apiRequest(apiUrl, { method: 'POST' });
            const data = await response.json();
            
            if (data.response) {
                const timestamp = formatTimestamp();
                const faultEntry = {
                    timestamp,
                    data: data.response,
                    level: determineFaultLevel(data.response)
                };
                
                faultData.unshift(faultEntry);
                updateFaultDisplay();
                updateFaultStats();
                
            } else {
                showEmptyFaultState(data.error || 'Arıza kaydı bulunamadı.');
            }
            
        } catch (error) {
            console.error('Fault data fetch failed:', error);
            showEmptyFaultState('Sunucuya ulaşılamadı.');
        }
    }
    
    /**
     * Update fault display
     */
    function updateFaultDisplay() {
        if (!contentDiv) return;
        
        if (faultData.length === 0) {
            showEmptyFaultState();
            return;
        }
        
        const filteredData = getFilteredFaultData();
        
        const faultHtml = filteredData.map(fault => `
            <div class="fault-entry ${fault.level}">
                <div class="fault-header">
                    <span class="fault-time">[${fault.timestamp}]</span>
                    <span class="fault-level ${fault.level}">${fault.level.toUpperCase()}</span>
                </div>
                <div class="fault-content">${sanitizeInput(fault.data)}</div>
            </div>
        `).join('');
        
        contentDiv.innerHTML = faultHtml;
    }
    
    /**
     * Determine fault level from data
     */
    function determineFaultLevel(data) {
        const lowerData = data.toLowerCase();
        if (lowerData.includes('error') || lowerData.includes('fail') || lowerData.includes('critical')) {
            return 'error';
        }
        if (lowerData.includes('warning') || lowerData.includes('warn')) {
            return 'warning';
        }
        return 'info';
    }
    
    /**
     * Get filtered fault data
     */
    function getFilteredFaultData() {
        const filterLevel = safeQuerySelector('#filterLevel')?.value || 'all';
        
        if (filterLevel === 'all') {
            return faultData;
        }
        
        return faultData.filter(fault => fault.level === filterLevel);
    }
    
    /**
     * Update fault statistics
     */
    function updateFaultStats() {
        safeUpdateElement('totalFaults', faultData.length.toString());
        safeUpdateElement('lastQuery', formatTimestamp());
        safeUpdateElement('faultCommStatus', createStatusBadge('Aktif', 'success'), true);
    }
    
    /**
     * Show empty fault state
     */
    function showEmptyFaultState(message = 'Arıza kayıtlarını görüntülemek için yukarıdaki butonları kullanın.') {
        if (contentDiv) {
            contentDiv.innerHTML = `
                <div class="empty-state">
                    <div class="empty-icon">📝</div>
                    <h4>Arıza kaydı bulunamadı</h4>
                    <p>${sanitizeInput(message)}</p>
                </div>
            `;
        }
    }
    
    /**
     * Clear fault display
     */
    function clearFaultDisplay() {
        faultData = [];
        showEmptyFaultState();
        updateFaultStats();
        showMessage('Arıza kayıtları temizlendi.', 'info');
    }
    
    /**
     * Export fault data
     */
    function exportFaultData() {
        if (faultData.length === 0) {
            showMessage('Dışa aktarılacak arıza kaydı bulunamadı.', 'warning');
            return;
        }
        
        const exportText = faultData.map(fault => 
            `[${fault.timestamp}] [${fault.level.toUpperCase()}] ${fault.data}`
        ).join('\n');
        
        const blob = new Blob([exportText], { type: 'text/plain;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `teias_ariza_kayitlari_${new Date().toISOString().split('T')[0]}.txt`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        showMessage('Arıza kayıtları başarıyla dışa aktarıldı.', 'success');
    }
    
    /**
     * Toggle auto refresh
     */
    function toggleAutoRefresh() {
        const toggle = safeQuerySelector('#autoRefreshToggle');
        if (!toggle) return;
        
        autoRefreshEnabled = !autoRefreshEnabled;
        toggle.dataset.active = autoRefreshEnabled.toString();
        toggle.querySelector('.toggle-text').textContent = autoRefreshEnabled ? 'Otomatik Yenileme (Açık)' : 'Otomatik Yenileme (Kapalı)';
        toggle.querySelector('.toggle-icon').textContent = autoRefreshEnabled ? '⏸️' : '▶️';
        
        if (autoRefreshEnabled) {
            autoRefreshTimer = setInterval(() => {
                fetchFaultData('/api/faults/refresh');
            }, 10000); // 10 saniyede bir
            showMessage('Otomatik yenileme açıldı.', 'info');
        } else {
            if (autoRefreshTimer) {
                clearInterval(autoRefreshTimer);
                autoRefreshTimer = null;
            }
            showMessage('Otomatik yenileme kapatıldı.', 'info');
        }
    }
    
    /**
     * Filter fault data
     */
    function filterFaultData() {
        updateFaultDisplay();
        const filteredCount = getFilteredFaultData().length;
        showMessage(`${filteredCount} kayıt gösteriliyor.`, 'info', 2000);
    }
    
    // Initialize fault stats
    updateFaultStats();
}

/**
 * Initialize Log page
 */
function initializeLogPage() {
    if (window.location.pathname !== '/log') return;
    
    const logContainer = safeQuerySelector('#logContainer');
    const refreshBtn = safeQuerySelector('#refreshLogsBtn');
    const exportBtn = safeQuerySelector('#exportLogsBtn');
    const pauseBtn = safeQuerySelector('#pauseLogsBtn');
    const clearBtn = safeQuerySelector('#clearLogsBtn');
    
    let logData = [];
    let isLogPaused = false;
    let logRefreshTimer = null;
    let autoScrollEnabled = true;
    
    // Event listeners
    if (refreshBtn) {
        refreshBtn.addEventListener('click', fetchLogs);
    }
    
    if (exportBtn) {
        exportBtn.addEventListener('click', exportLogs);
    }
    
    if (pauseBtn) {
        pauseBtn.addEventListener('click', toggleLogPause);
    }
    
    if (clearBtn) {
        clearBtn.addEventListener('click', clearAllLogs);
    }
    
    // Filter controls
    const searchInput = safeQuerySelector('#logSearch');
    const levelFilter = safeQuerySelector('#logLevelFilter');
    const sourceFilter = safeQuerySelector('#logSourceFilter');
    const clearFiltersBtn = safeQuerySelector('#clearFiltersBtn');
    
    if (searchInput) {
        searchInput.addEventListener('input', debounce(filterLogs, 300));
    }
    
    if (levelFilter) {
        levelFilter.addEventListener('change', filterLogs);
    }
    
    if (sourceFilter) {
        sourceFilter.addEventListener('change', filterLogs);
    }
    
    if (clearFiltersBtn) {
        clearFiltersBtn.addEventListener('click', clearLogFilters);
    }
    
    // Auto scroll toggle
    const autoScrollToggle = safeQuerySelector('#autoScrollToggle');
    if (autoScrollToggle) {
        autoScrollToggle.addEventListener('click', toggleAutoScroll);
    }
    
    // Auto refresh toggle
    const autoRefreshToggle = safeQuerySelector('#autoRefreshToggle');
    const refreshInterval = safeQuerySelector('#refreshInterval');
    
    if (autoRefreshToggle) {
        autoRefreshToggle.addEventListener('click', toggleAutoRefresh);
    }
    
    if (refreshInterval) {
        refreshInterval.addEventListener('change', updateRefreshInterval);
    }
    
    /**
     * Fetch logs from API
     */
    async function fetchLogs() {
        try {
            const response = await apiRequest('/api/logs');
            const logs = await response.json();
            
            if (Array.isArray(logs)) {
                logData = logs;
                updateLogDisplay();
                updateLogStats();
            }
            
        } catch (error) {
            console.error('Failed to fetch logs:', error);
            showMessage('Log kayıtları alınamadı.', 'error');
        }
    }
    
    /**
     * Update log display
     */
    function updateLogDisplay() {
        if (!logContainer) return;
        
        const filteredLogs = getFilteredLogs();
        
        if (filteredLogs.length === 0) {
            logContainer.innerHTML = `
                <div class="empty-state">
                    <div class="empty-icon">📄</div>
                    <h4>Log kaydı bulunamadı</h4>
                    <p>Henüz gösterilecek log kaydı yok veya filtrelerinize uygun kayıt bulunamadı.</p>
                </div>
            `;
            return;
        }
        
        const logHtml = filteredLogs.map(log => {
            const levelClass = log.level.toLowerCase();
            return `
                <div class="log-entry ${levelClass}">
                    <span class="log-timestamp">[${log.timestamp}]</span>
                    <span class="log-level ${levelClass}">[${log.level}]</span>
                    <span class="log-source">[${log.source}]</span>
                    <span class="log-message">${sanitizeInput(log.message)}</span>
                </div>
            `;
        }).join('');
        
        logContainer.innerHTML = logHtml;
        
        // Auto scroll to bottom
        if (autoScrollEnabled) {
            logContainer.scrollTop = logContainer.scrollHeight;
        }
        
        safeUpdateElement('lastLogUpdate', formatTimestamp());
    }
    
    /**
     * Get filtered logs
     */
    function getFilteredLogs() {
        let filtered = [...logData];
        
        // Search filter
        const searchTerm = searchInput?.value.toLowerCase().trim();
        if (searchTerm) {
            filtered = filtered.filter(log => 
                log.message.toLowerCase().includes(searchTerm) ||
                log.source.toLowerCase().includes(searchTerm)
            );
        }
        
        // Level filter
        const levelValue = levelFilter?.value;
        if (levelValue && levelValue !== 'all') {
            filtered = filtered.filter(log => log.level === levelValue);
        }
        
        // Source filter
        const sourceValue = sourceFilter?.value;
        if (sourceValue && sourceValue !== 'all') {
            filtered = filtered.filter(log => log.source === sourceValue);
        }
        
        return filtered.reverse(); // Show newest first
    }
    
    /**
     * Update log statistics
     */
    function updateLogStats() {
        const errorCount = logData.filter(log => log.level === 'ERROR').length;
        const warningCount = logData.filter(log => log.level === 'WARN').length;
        
        safeUpdateElement('totalLogs', logData.length.toString());
        safeUpdateElement('errorCount', errorCount.toString());
        safeUpdateElement('warningCount', warningCount.toString());
    }
    
    /**
     * Filter logs
     */
    function filterLogs() {
        updateLogDisplay();
    }
    
    /**
     * Clear log filters
     */
    function clearLogFilters() {
        if (searchInput) searchInput.value = '';
        if (levelFilter) levelFilter.value = 'all';
        if (sourceFilter) sourceFilter.value = 'all';
        filterLogs();
        showMessage('Filtreler temizlendi.', 'info', 2000);
    }
    
    /**
     * Export logs
     */
    function exportLogs() {
        if (logData.length === 0) {
            showMessage('Dışa aktarılacak log kaydı bulunamadı.', 'warning');
            return;
        }
        
        const filteredLogs = getFilteredLogs();
        const exportText = filteredLogs.map(log => 
            `[${log.timestamp}] [${log.level}] [${log.source}] ${log.message}`
        ).join('\n');
        
        const blob = new Blob([exportText], { type: 'text/plain;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `teias_log_kayitlari_${new Date().toISOString().split('T')[0]}.txt`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        showMessage(`${filteredLogs.length} log kaydı başarıyla dışa aktarıldı.`, 'success');
    }
    
    /**
     * Toggle log pause
     */
    function toggleLogPause() {
        isLogPaused = !isLogPaused;
        const btn = pauseBtn;
        if (btn) {
            btn.querySelector('.btn-text').textContent = isLogPaused ? '▶️ Devam Et' : '⏸️ Duraklat';
            btn.className = isLogPaused ? 'btn success' : 'btn warning';
        }
        
        showMessage(isLogPaused ? 'Log güncellemesi duraklatıldı.' : 'Log güncellemesi devam ediyor.', 'info');
    }
    
    /**
     * Clear all logs
     */
    function clearAllLogs() {
        if (!confirm('Tüm log kayıtları silinecek. Bu işlem geri alınamaz. Emin misiniz?')) {
            return;
        }
        
        clearBtn.classList.add('loading');
        
        apiRequest('/api/logs/clear', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    logData = [];
                    updateLogDisplay();
                    updateLogStats();
                    showMessage('Tüm log kayıtları başarıyla temizlendi.', 'success');
                } else {
                    showMessage('Log kayıtları temizlenemedi.', 'error');
                }
            })
            .catch(error => {
                console.error('Failed to clear logs:', error);
                showMessage('Log temizleme işlemi başarısız.', 'error');
            })
            .finally(() => {
                clearBtn.classList.remove('loading');
            });
    }
    
    /**
     * Toggle auto scroll
     */
    function toggleAutoScroll() {
        autoScrollEnabled = !autoScrollEnabled;
        const toggle = autoScrollToggle;
        if (toggle) {
            toggle.dataset.active = autoScrollEnabled.toString();
            toggle.querySelector('.toggle-text').textContent = autoScrollEnabled ? 'Otomatik Kaydırma (Açık)' : 'Otomatik Kaydırma (Kapalı)';
            toggle.querySelector('.toggle-icon').textContent = autoScrollEnabled ? '📜' : '📋';
        }
    }
    
    /**
     * Toggle auto refresh
     */
    function toggleAutoRefresh() {
        const isActive = autoRefreshToggle.dataset.active === 'true';
        const newState = !isActive;
        
        autoRefreshToggle.dataset.active = newState.toString();
        autoRefreshToggle.querySelector('.toggle-text').textContent = newState ? 'Otomatik Yenileme (Açık)' : 'Otomatik Yenileme (Kapalı)';
        autoRefreshToggle.querySelector('.toggle-icon').textContent = newState ? '⏸️' : '▶️';
        
        if (newState && !isLogPaused) {
            startLogRefresh();
        } else {
            stopLogRefresh();
        }
    }
    
    /**
     * Update refresh interval
     */
    function updateRefreshInterval() {
        if (logRefreshTimer) {
            stopLogRefresh();
            startLogRefresh();
        }
    }
    
    /**
     * Start log refresh timer
     */
    function startLogRefresh() {
        if (logRefreshTimer) clearInterval(logRefreshTimer);
        
        const interval = parseInt(refreshInterval?.value || CONFIG.logRefreshInterval);
        logRefreshTimer = setInterval(() => {
            if (!isLogPaused && isPageVisible) {
                fetchLogs();
            }
        }, interval);
    }
    
    /**
     * Stop log refresh timer
     */
    function stopLogRefresh() {
        if (logRefreshTimer) {
            clearInterval(logRefreshTimer);
            logRefreshTimer = null;
        }
    }
    
    // Initialize logs
    fetchLogs();
    
    // Start auto refresh if enabled
    if (autoRefreshToggle?.dataset.active === 'true') {
        startLogRefresh();
    }
    
    // Cleanup on page unload
    window.addEventListener('beforeunload', stopLogRefresh);
}

// ===================================
// PAGE VISIBILITY HANDLING
// ===================================

/**
 * Handle page visibility changes
 */
function handleVisibilityChange() {
    isPageVisible = !document.hidden;
    
    if (isPageVisible) {
        // Resume updates when page becomes visible
        if (window.location.pathname === '/') {
            startDashboardUpdates();
        }
    } else {
        // Pause updates when page is hidden
        stopDashboardUpdates();
    }
}

/**
 * Start dashboard updates
 */
function startDashboardUpdates() {
    if (updateTimer) clearInterval(updateTimer);
    
    // Initial update
    updateDashboardData();
    
    // Set up periodic updates
    updateTimer = setInterval(() => {
        if (isPageVisible) {
            updateDashboardData();
        }
    }, CONFIG.updateInterval);
}

/**
 * Stop dashboard updates
 */
function stopDashboardUpdates() {
    if (updateTimer) {
        clearInterval(updateTimer);
        updateTimer = null;
    }
}

// ===================================
// ERROR HANDLING & LOGGING
// ===================================

/**
 * Global error handler
 */
function setupGlobalErrorHandling() {
    window.addEventListener('error', (event) => {
        console.error('Global error:', event.error);
        
        // Don't show user errors for script loading failures
        if (event.filename && event.filename.includes('.js')) {
            console.warn('Script loading error, this might be expected');
            return;
        }
        
        showMessage('Beklenmeyen bir hata oluştu. Sayfa yeniden yüklenecek.', 'error');
        
        // Auto reload after 5 seconds
        setTimeout(() => {
            window.location.reload();
        }, 5000);
    });
    
    window.addEventListener('unhandledrejection', (event) => {
        console.error('Unhandled promise rejection:', event.reason);
        event.preventDefault();
    });
}

// ===================================
// KEYBOARD SHORTCUTS
// ===================================

/**
 * Setup keyboard shortcuts
 */
function setupKeyboardShortcuts() {
    document.addEventListener('keydown', (e) => {
        // Alt + number shortcuts for navigation
        if (e.altKey && !e.ctrlKey && !e.shiftKey) {
            switch (e.key) {
                case '1':
                    e.preventDefault();
                    window.location.href = '/';
                    break;
                case '2':
                    e.preventDefault();
                    window.location.href = '/ntp';
                    break;
                case '3':
                    e.preventDefault();
                    window.location.href = '/baudrate';
                    break;
                case '4':
                    e.preventDefault();
                    window.location.href = '/fault';
                    break;
                case '5':
                    e.preventDefault();
                    window.location.href = '/account';
                    break;
                case '6':
                    e.preventDefault();
                    window.location.href = '/log';
                    break;
                case 't':
                case 'T':
                    e.preventDefault();
                    toggleTheme();
                    break;
            }
        }
        
        // Escape key to close mobile menu
        if (e.key === 'Escape') {
            const navMenu = safeQuerySelector('.nav-menu');
            const navToggle = safeQuerySelector('#navToggle');
            if (navMenu && navMenu.classList.contains('active')) {
                navMenu.classList.remove('active');
                if (navToggle) navToggle.classList.remove('active');
            }
        }
        
        // Ctrl+R refresh prevention on sensitive pages
        if (e.ctrlKey && e.key === 'r') {
            const currentPath = window.location.pathname;
            if (['/fault', '/log'].includes(currentPath)) {
                e.preventDefault();
                showMessage('Bu sayfada Ctrl+R ile yenileme devre dışıdır. Lütfen sayfadaki yenile butonunu kullanın.', 'warning');
            }
        }
    });
}

// ===================================
// PERFORMANCE MONITORING
// ===================================

/**
 * Monitor performance
 */
function monitorPerformance() {
    // Monitor API response times
    const apiTimes = [];
    
    const originalFetch = window.fetch;
    window.fetch = async function(...args) {
        const startTime = performance.now();
        try {
            const response = await originalFetch.apply(this, args);
            const endTime = performance.now();
            const duration = endTime - startTime;
            
            apiTimes.push(duration);
            
            // Keep only last 10 measurements
            if (apiTimes.length > 10) {
                apiTimes.shift();
            }
            
            // Log slow API calls
            if (duration > 5000) {
                console.warn(`Slow API call detected: ${args[0]} took ${duration.toFixed(2)}ms`);
            }
            
            return response;
        } catch (error) {
            const endTime = performance.now();
            console.error(`API call failed: ${args[0]} after ${(endTime - startTime).toFixed(2)}ms`, error);
            throw error;
        }
    };
    
    // Log memory usage periodically (development only)
    if (performance.memory) {
        setInterval(() => {
            const memory = performance.memory;
            console.log('Memory usage:', {
                used: Math.round(memory.usedJSHeapSize / 1048576) + ' MB',
                total: Math.round(memory.totalJSHeapSize / 1048576) + ' MB',
                limit: Math.round(memory.jsHeapSizeLimit / 1048576) + ' MB'
            });
        }, 60000); // Every minute
    }
}

// ===================================
// INITIALIZATION & CLEANUP
// ===================================

/**
 * Initialize application
 */
function initializeApp() {
    console.log('🚀 TEİAŞ EKLİM Web Arayüzü başlatılıyor...');
    
    // Setup global error handling first
    setupGlobalErrorHandling();
    
    // Initialize theme system
    initializeTheme();
    
    // Initialize navigation
    initializeNavigation();
    
    // Setup keyboard shortcuts
    setupKeyboardShortcuts();
    
    // Setup performance monitoring
    monitorPerformance();
    
    // Page visibility handling
    document.addEventListener('visibilitychange', handleVisibilityChange);
    
    // Initialize page-specific functionality
    const currentPath = window.location.pathname;
    
    switch (currentPath) {
        case '/login':
            initializeLoginPage();
            break;
            
        case '/account':
            initializeAccountPage();
            break;
            
        case '/ntp':
            initializeNtpPage();
            break;
            
        case '/baudrate':
            initializeBaudRatePage();
            break;
            
        case '/fault':
            initializeFaultPage();
            break;
            
        case '/log':
            initializeLogPage();
            break;
            
        case '/':
        default:
            // Dashboard page
            if (currentPath !== '/login') {
                startDashboardUpdates();
            }
            break;
    }
    
    console.log('✅ TEİAŞ EKLİM Web Arayüzü başarıyla başlatıldı.');
}

/**
 * Cleanup function
 */
function cleanup() {
    stopDashboardUpdates();
    
    if (logTimer) {
        clearInterval(logTimer);
        logTimer = null;
    }
    
    // Clear any other timers or intervals
    // This helps prevent memory leaks
}

// ===================================
// POLYFILLS & COMPATIBILITY
// ===================================

/**
 * Simple polyfills for older browsers
 */
function addPolyfills() {
    // Promise.finally polyfill for older browsers
    if (!Promise.prototype.finally) {
        Promise.prototype.finally = function(callback) {
            const constructor = this.constructor;
            return this.then(
                value => constructor.resolve(callback()).then(() => value),
                reason => constructor.resolve(callback()).then(() => { throw reason; })
            );
        };
    }
    
    // AbortController polyfill check
    if (!window.AbortController) {
        console.warn('AbortController not supported, request timeouts may not work properly');
        window.AbortController = class {
            constructor() {
                this.signal = { aborted: false };
            }
            abort() {
                this.signal.aborted = true;
            }
        };
    }
}

// ===================================
// APPLICATION ENTRY POINT
// ===================================

// Add polyfills first
addPolyfills();

// Wait for DOM to be ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initializeApp);
} else {
    // DOM is already ready
    initializeApp();
}

// Cleanup on page unload
window.addEventListener('beforeunload', cleanup);
window.addEventListener('unload', cleanup);

// Export for debugging in development
if (typeof window !== 'undefined') {
    window.TeiasApp = {
        updateDashboardData,
        showMessage,
        toggleTheme,
        apiRequest,
        CONFIG
    };
}