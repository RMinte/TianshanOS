/**
 * TianShanOS Web App
 */

// DOM Elements
const loginBtn = document.getElementById('login-btn');
const loginModal = document.getElementById('login-modal');
const loginForm = document.getElementById('login-form');
const userName = document.getElementById('user-name');

// State
let ws = null;

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    updateAuthUI();
    loadDashboard();
    setupWebSocket();
    
    // Auto-refresh dashboard
    setInterval(loadDashboard, 5000);
});

// Auth UI
function updateAuthUI() {
    if (api.isLoggedIn()) {
        loginBtn.textContent = '登出';
        userName.textContent = '已登录';
        loginBtn.onclick = logout;
    } else {
        loginBtn.textContent = '登录';
        userName.textContent = '未登录';
        loginBtn.onclick = showLoginModal;
    }
}

function showLoginModal() {
    loginModal.classList.remove('hidden');
}

function closeLoginModal() {
    loginModal.classList.add('hidden');
    loginForm.reset();
}

loginForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    
    try {
        await api.login(username, password);
        closeLoginModal();
        updateAuthUI();
        loadDashboard();
    } catch (error) {
        alert('登录失败: ' + error.message);
    }
});

async function logout() {
    try {
        await api.logout();
    } finally {
        updateAuthUI();
    }
}

// Dashboard
async function loadDashboard() {
    try {
        // Load system info
        const sysInfo = await api.getSystemInfo();
        if (sysInfo.data) {
            document.getElementById('chip-model').textContent = sysInfo.data.chip || '-';
            document.getElementById('firmware-version').textContent = sysInfo.data.version || '-';
            document.getElementById('uptime').textContent = formatUptime(sysInfo.data.uptime_ms);
        }
    } catch (e) {
        console.log('System info not available');
    }
    
    try {
        // Load memory info
        const memInfo = await api.getMemoryInfo();
        if (memInfo.data) {
            const total = memInfo.data.total_heap || 1;
            const free = memInfo.data.free_heap || 0;
            const used = total - free;
            const percent = Math.round((used / total) * 100);
            
            document.getElementById('mem-progress').style.width = percent + '%';
            document.getElementById('mem-used').textContent = formatBytes(used);
            document.getElementById('mem-total').textContent = formatBytes(total);
        }
    } catch (e) {
        console.log('Memory info not available');
    }
    
    try {
        // Load network status
        const netStatus = await api.getNetworkStatus();
        if (netStatus.data) {
            document.getElementById('eth-status').textContent = 
                netStatus.data.eth_connected ? '已连接' : '未连接';
            document.getElementById('wifi-status').textContent = 
                netStatus.data.wifi_connected ? '已连接' : '未连接';
            document.getElementById('ip-addr').textContent = 
                netStatus.data.ip || '-';
        }
    } catch (e) {
        document.getElementById('eth-status').textContent = '-';
        document.getElementById('wifi-status').textContent = '-';
        document.getElementById('ip-addr').textContent = '-';
    }
    
    try {
        // Load device status
        const devStatus = await api.getDeviceStatus();
        if (devStatus.data) {
            document.getElementById('agx-status').textContent = 
                devStatus.data.agx_powered ? '运行中' : '关机';
        }
    } catch (e) {
        document.getElementById('agx-status').textContent = '-';
    }
    
    try {
        // Load fan status
        const fanStatus = await api.getFanStatus();
        if (fanStatus.data) {
            const fans = fanStatus.data.fans || [];
            const running = fans.filter(f => f.running).length;
            document.getElementById('fan-status').textContent = 
                `${running}/${fans.length} 运行中`;
        }
    } catch (e) {
        document.getElementById('fan-status').textContent = '-';
    }
}

// WebSocket
function setupWebSocket() {
    ws = new TianShanWS((msg) => {
        if (msg.type === 'event') {
            handleEvent(msg.event, msg.data);
        }
    });
    ws.connect();
}

function handleEvent(event, data) {
    console.log('Event:', event, data);
    
    // Refresh dashboard on relevant events
    if (event.startsWith('system.') || event.startsWith('network.') || event.startsWith('device.')) {
        loadDashboard();
    }
}

// Helpers
function formatUptime(ms) {
    if (!ms) return '-';
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) return `${days}天 ${hours % 24}小时`;
    if (hours > 0) return `${hours}小时 ${minutes % 60}分钟`;
    if (minutes > 0) return `${minutes}分钟`;
    return `${seconds}秒`;
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

// Expose for HTML onclick
window.closeLoginModal = closeLoginModal;
