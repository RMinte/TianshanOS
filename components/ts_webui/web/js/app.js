/**
 * TianShanOS Web App - Main Application
 */

// =========================================================================
//                         全局状态
// =========================================================================

let ws = null;
let refreshInterval = null;

// =========================================================================
//                         初始化
// =========================================================================

document.addEventListener('DOMContentLoaded', () => {
    // 初始化认证 UI
    updateAuthUI();
    
    // 注册路由
    router.register('/', loadDashboard);
    router.register('/system', loadSystemPage);
    router.register('/led', loadLedPage);
    router.register('/network', loadNetworkPage);
    router.register('/device', loadDevicePage);
    router.register('/files', loadFilesPage);
    router.register('/terminal', loadTerminalPage);
    router.register('/config', loadConfigPage);
    router.register('/security', loadSecurityPage);
    
    // 启动 WebSocket
    setupWebSocket();
});

// =========================================================================
//                         认证
// =========================================================================

function updateAuthUI() {
    const loginBtn = document.getElementById('login-btn');
    const userName = document.getElementById('user-name');
    
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
    document.getElementById('login-modal').classList.remove('hidden');
}

function closeLoginModal() {
    document.getElementById('login-modal').classList.add('hidden');
    document.getElementById('login-form').reset();
}

document.getElementById('login-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    
    try {
        await api.login(username, password);
        closeLoginModal();
        updateAuthUI();
        router.navigate();
    } catch (error) {
        showToast('登录失败: ' + error.message, 'error');
    }
});

async function logout() {
    try {
        await api.logout();
    } finally {
        updateAuthUI();
    }
}

// =========================================================================
//                         WebSocket
// =========================================================================

function setupWebSocket() {
    ws = new TianShanWS(
        (msg) => handleEvent(msg),
        () => document.getElementById('ws-status')?.classList.add('connected'),
        () => document.getElementById('ws-status')?.classList.remove('connected')
    );
    ws.connect();
}

function handleEvent(msg) {
    console.log('Event:', msg);
    
    if (msg.type === 'event') {
        // 刷新相关页面数据
        if (router.currentPage) {
            router.currentPage();
        }
    }
}

// =========================================================================
//                         仪表盘页面
// =========================================================================

async function loadDashboard() {
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="dashboard">
            <h1>仪表盘</h1>
            
            <div class="cards">
                <div class="card">
                    <h3>🖥️ 系统信息</h3>
                    <div class="card-content" id="sys-info-card">
                        <p><strong>芯片:</strong> <span id="chip-model">-</span></p>
                        <p><strong>固件:</strong> <span id="firmware-version">-</span></p>
                        <p><strong>运行时间:</strong> <span id="uptime">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>💾 内存</h3>
                    <div class="card-content">
                        <div class="progress-bar">
                            <div class="progress" id="mem-progress" style="width: 0%"></div>
                        </div>
                        <p><span id="mem-used">-</span> / <span id="mem-total">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🌐 网络</h3>
                    <div class="card-content">
                        <p><strong>以太网:</strong> <span id="eth-status">-</span></p>
                        <p><strong>WiFi:</strong> <span id="wifi-status">-</span></p>
                        <p><strong>IP:</strong> <span id="ip-addr">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>⚡ 电源</h3>
                    <div class="card-content">
                        <p><strong>电压:</strong> <span id="voltage">-</span></p>
                        <p><strong>保护:</strong> <span id="protection-status">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🖲️ 设备</h3>
                    <div class="card-content">
                        <p><strong>AGX:</strong> <span id="agx-status">-</span></p>
                        <p><strong>LPMU:</strong> <span id="lpmu-status">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🌡️ 温度 & 风扇</h3>
                    <div class="card-content">
                        <p><strong>温度:</strong> <span id="temperature">-</span></p>
                        <p><strong>风扇:</strong> <span id="fan-status">-</span></p>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    await refreshDashboard();
    
    // 定时刷新
    clearInterval(refreshInterval);
    refreshInterval = setInterval(refreshDashboard, 3000);
}

async function refreshDashboard() {
    // 系统信息
    try {
        const sysInfo = await api.getSystemInfo();
        if (sysInfo.data) {
            document.getElementById('chip-model').textContent = sysInfo.data.chip?.model || '-';
            document.getElementById('firmware-version').textContent = sysInfo.data.app?.version || '-';
            document.getElementById('uptime').textContent = formatUptime(sysInfo.data.uptime_ms);
        }
    } catch (e) { console.log('System info not available'); }
    
    // 内存
    try {
        const memInfo = await api.getMemoryInfo();
        if (memInfo.data) {
            const total = memInfo.data.internal?.total || 1;
            const free = memInfo.data.internal?.free || memInfo.data.free_heap || 0;
            const used = total - free;
            const percent = Math.round((used / total) * 100);
            
            document.getElementById('mem-progress').style.width = percent + '%';
            document.getElementById('mem-used').textContent = formatBytes(used);
            document.getElementById('mem-total').textContent = formatBytes(total);
        }
    } catch (e) { console.log('Memory info not available'); }
    
    // 网络
    try {
        const netStatus = await api.networkStatus();
        if (netStatus.data) {
            const eth = netStatus.data.ethernet || {};
            const wifi = netStatus.data.wifi || {};
            document.getElementById('eth-status').textContent = eth.status === 'connected' ? '已连接' : '未连接';
            document.getElementById('wifi-status').textContent = wifi.connected ? '已连接' : '未连接';
            document.getElementById('ip-addr').textContent = eth.ip || wifi.ip || '-';
        }
    } catch (e) {
        document.getElementById('eth-status').textContent = '-';
        document.getElementById('wifi-status').textContent = '-';
    }
    
    // 电源
    try {
        const powerStatus = await api.powerStatus();
        if (powerStatus.data) {
            // 优先使用 power_chip 数据，其次用 voltage 数据
            const voltage = powerStatus.data.power_chip?.voltage_v || 
                           powerStatus.data.voltage?.supply_v || 
                           powerStatus.data.stats?.avg_voltage_v || '-';
            document.getElementById('voltage').textContent = 
                (typeof voltage === 'number' ? voltage.toFixed(1) : voltage) + ' V';
        }
        const protStatus = await api.powerProtectionStatus();
        if (protStatus.data) {
            const running = protStatus.data.running || protStatus.data.initialized;
            document.getElementById('protection-status').textContent = 
                running ? '已启用' : '已禁用';
        }
    } catch (e) { document.getElementById('voltage').textContent = '-'; }
    
    // 设备
    try {
        const devStatus = await api.deviceStatus();
        if (devStatus.data) {
            const agx = devStatus.data.devices?.find(d => d.name === 'agx');
            const lpmu = devStatus.data.devices?.find(d => d.name === 'lpmu');
            document.getElementById('agx-status').textContent = agx?.powered ? '运行中' : '关机';
            document.getElementById('lpmu-status').textContent = lpmu?.powered ? '运行中' : '关机';
        }
    } catch (e) {
        document.getElementById('agx-status').textContent = '-';
        document.getElementById('lpmu-status').textContent = '-';
    }
    
    // 温度和风扇
    try {
        const tempStatus = await api.tempStatus();
        if (tempStatus.data) {
            document.getElementById('temperature').textContent = 
                (tempStatus.data.temperature || '-') + ' °C';
        }
        const fanStatus = await api.fanStatus();
        if (fanStatus.data) {
            const fans = fanStatus.data.fans || [];
            const running = fans.filter(f => f.enabled).length;
            document.getElementById('fan-status').textContent = `${running}/${fans.length} 运行`;
        }
    } catch (e) {
        document.getElementById('temperature').textContent = '-';
        document.getElementById('fan-status').textContent = '-';
    }
}

// =========================================================================
//                         系统页面
// =========================================================================

async function loadSystemPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-system">
            <h1>系统管理</h1>
            
            <div class="section">
                <h2>系统信息</h2>
                <div class="info-grid" id="system-info">
                    <div class="info-item"><label>芯片</label><span id="sys-chip">-</span></div>
                    <div class="info-item"><label>版本</label><span id="sys-version">-</span></div>
                    <div class="info-item"><label>编译时间</label><span id="sys-compile">-</span></div>
                    <div class="info-item"><label>运行时间</label><span id="sys-uptime">-</span></div>
                    <div class="info-item"><label>IDF版本</label><span id="sys-idf">-</span></div>
                    <div class="info-item"><label>Flash大小</label><span id="sys-flash">-</span></div>
                </div>
            </div>
            
            <div class="section">
                <h2>内存状态</h2>
                <div class="memory-bars">
                    <div class="memory-item">
                        <label>总堆内存</label>
                        <div class="progress-bar"><div class="progress" id="heap-progress"></div></div>
                        <span id="heap-text">-</span>
                    </div>
                    <div class="memory-item">
                        <label>PSRAM</label>
                        <div class="progress-bar"><div class="progress" id="psram-progress"></div></div>
                        <span id="psram-text">-</span>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <h2>服务状态</h2>
                <table class="data-table" id="services-table">
                    <thead>
                        <tr>
                            <th>服务名称</th>
                            <th>状态</th>
                            <th>阶段</th>
                            <th>健康</th>
                            <th>操作</th>
                        </tr>
                    </thead>
                    <tbody id="services-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>系统操作</h2>
                <div class="button-group">
                    <button class="btn btn-warning" onclick="confirmReboot()">🔄 重启系统</button>
                </div>
            </div>
        </div>
    `;
    
    await refreshSystemPage();
}

async function refreshSystemPage() {
    // 系统信息
    try {
        const info = await api.getSystemInfo();
        if (info.data) {
            document.getElementById('sys-chip').textContent = info.data.chip?.model || '-';
            document.getElementById('sys-version').textContent = info.data.app?.version || '-';
            document.getElementById('sys-compile').textContent = 
                (info.data.app?.compile_date || '') + ' ' + (info.data.app?.compile_time || '');
            document.getElementById('sys-uptime').textContent = formatUptime(info.data.uptime_ms);
            document.getElementById('sys-idf').textContent = info.data.app?.idf_version || '-';
            document.getElementById('sys-flash').textContent = formatBytes(info.data.flash_size || 0);
        }
    } catch (e) { console.log('System info error:', e); }
    
    // 内存
    try {
        const mem = await api.getMemoryInfo();
        if (mem.data) {
            const heapTotal = mem.data.internal?.total || 1;
            const heapFree = mem.data.internal?.free || mem.data.free_heap || 0;
            const heapUsed = heapTotal - heapFree;
            const heapPercent = Math.round((heapUsed / heapTotal) * 100);
            
            document.getElementById('heap-progress').style.width = heapPercent + '%';
            document.getElementById('heap-text').textContent = 
                `${formatBytes(heapUsed)} / ${formatBytes(heapTotal)} (${heapPercent}%)`;
            
            if (mem.data.psram?.total) {
                const psramTotal = mem.data.psram.total;
                const psramFree = mem.data.psram.free || 0;
                const psramUsed = psramTotal - psramFree;
                const psramPercent = Math.round((psramUsed / psramTotal) * 100);
                
                document.getElementById('psram-progress').style.width = psramPercent + '%';
                document.getElementById('psram-text').textContent = 
                    `${formatBytes(psramUsed)} / ${formatBytes(psramTotal)} (${psramPercent}%)`;
            }
        }
    } catch (e) { console.log('Memory info error:', e); }
    
    // 服务列表
    try {
        const services = await api.serviceList();
        const tbody = document.getElementById('services-body');
        tbody.innerHTML = '';
        
        if (services.data && services.data.services) {
            services.data.services.forEach(svc => {
                const tr = document.createElement('tr');
                const stateClass = svc.state === 'RUNNING' ? 'status-ok' : 
                                  svc.state === 'ERROR' ? 'status-error' : 'status-warn';
                tr.innerHTML = `
                    <td>${svc.name}</td>
                    <td><span class="status-badge ${stateClass}">${svc.state}</span></td>
                    <td>${svc.phase}</td>
                    <td>${svc.healthy ? '✅' : '❌'}</td>
                    <td>
                        <button class="btn btn-small" onclick="serviceAction('${svc.name}', 'restart')">重启</button>
                    </td>
                `;
                tbody.appendChild(tr);
            });
        }
    } catch (e) { console.log('Services error:', e); }
}

async function serviceAction(name, action) {
    try {
        if (action === 'restart') await api.serviceRestart(name);
        else if (action === 'start') await api.serviceStart(name);
        else if (action === 'stop') await api.serviceStop(name);
        showToast(`服务 ${name} ${action} 成功`, 'success');
        await refreshSystemPage();
    } catch (e) {
        showToast(`操作失败: ${e.message}`, 'error');
    }
}

function confirmReboot() {
    if (confirm('确定要重启系统吗？')) {
        showToast('正在发送重启命令...', 'info');
        api.reboot(500)
            .then((result) => {
                console.log('Reboot response:', result);
                showToast('系统正在重启，请稍候...', 'success');
            })
            .catch((err) => {
                console.error('Reboot failed:', err);
                showToast('重启失败: ' + err.message, 'error');
            });
    }
}

// =========================================================================
//                         LED 页面
// =========================================================================

// 存储设备信息和特效列表
let ledDevices = {};
let ledEffects = [];

async function loadLedPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-led">
            <h1>💡 LED 控制</h1>
            <div id="led-panels" class="led-panels">
                <p class="loading">加载设备中...</p>
            </div>
        </div>
    `;
    
    await refreshLedPage();
}

async function refreshLedPage() {
    const panelsContainer = document.getElementById('led-panels');
    
    // 加载设备列表并渲染每个设备的控制面板
    // 现在每个设备会带有自己适用的特效列表
    try {
        const result = await api.ledList();
        
        if (result.data && result.data.devices && result.data.devices.length > 0) {
            // 存储设备信息（包含特效列表）
            result.data.devices.forEach(dev => {
                ledDevices[dev.name] = dev;
                
                // 初始化 selectedEffects（如果设备有正在运行的动画）
                if (dev.current && dev.current.animation) {
                    selectedEffects[dev.name] = dev.current.animation;
                }
            });
            
            // 为每个设备生成独立的控制面板
            panelsContainer.innerHTML = result.data.devices.map(dev => generateDevicePanel(dev)).join('');
        } else {
            // 如果 API 返回空，显示提示信息
            panelsContainer.innerHTML = `
                <div class="empty-state">
                    <p>⚠️ 未找到已初始化的 LED 设备</p>
                    <p class="hint">LED 设备可能尚未启动。请检查：</p>
                    <ul>
                        <li>LED 服务是否已启动（<code>service --status</code>）</li>
                        <li>设备配置是否正确（GPIO 引脚）</li>
                    </ul>
                    <p>可用命令：<code>led --status</code></p>
                </div>
            `;
        }
    } catch (e) {
        console.error('LED list error:', e);
        panelsContainer.innerHTML = '<p class="error">加载设备失败: ' + e.message + '</p>';
    }
}

function generateDevicePanel(dev) {
    const icon = getDeviceIcon(dev.name);
    const description = getDeviceDescription(dev.name);
    
    // 获取当前状态
    const current = dev.current || {};
    const isOn = current.on || false;
    const currentAnimation = current.animation || '';
    const currentSpeed = current.speed || 50;
    const currentColor = current.color || {r: 255, g: 0, b: 0};
    
    // 将 RGB 转为 hex
    const colorHex = '#' + 
        currentColor.r.toString(16).padStart(2, '0') +
        currentColor.g.toString(16).padStart(2, '0') +
        currentColor.b.toString(16).padStart(2, '0');
    
    // 使用设备自带的特效列表（已按设备类型过滤）
    const deviceEffects = dev.effects || [];
    const effectsHtml = deviceEffects.length > 0 
        ? deviceEffects.map(eff => {
            const isActive = eff === currentAnimation;
            const activeClass = isActive ? ' active' : '';
            return `<button class="btn effect-btn${activeClass}" onclick="showEffectConfig('${dev.name}', '${eff}')" title="点击配置并启动">${getEffectIcon(eff)} ${eff}</button>`;
        }).join('')
        : '<span class="empty">暂无可用</span>';
    
    // 开关按钮状态
    const toggleClass = isOn ? ' on' : '';
    const toggleText = isOn ? '关灯' : '开灯';
    
    return `
        <div class="led-panel" data-device="${dev.name}">
            <div class="panel-header">
                <span class="device-icon">${icon}</span>
                <div class="device-title">
                    <h2>${dev.name}</h2>
                    <span class="device-desc">${description}</span>
                </div>
                <span class="device-layout">${dev.layout || 'strip'}</span>
                <span class="led-count">${dev.count} LEDs</span>
                <button class="btn btn-sm btn-header-save" onclick="saveLedConfig('${dev.name}')" title="保存当前状态为开机配置">💾</button>
            </div>
            
            <div class="panel-body two-columns">
                <!-- 左侧：基础控制 -->
                <div class="control-column basic-controls">
                    <label class="column-title">基础控制</label>
                    
                    <!-- 电源开关 -->
                    <div class="control-row">
                        <button class="btn btn-toggle${toggleClass}" id="toggle-${dev.name}" onclick="toggleLed('${dev.name}')">
                            <span class="toggle-icon">💡</span>
                            <span class="toggle-text">${toggleText}</span>
                        </button>
                    </div>
                    
                    <!-- 亮度控制 -->
                    <div class="control-row">
                        <label>亮度 <span id="brightness-val-${dev.name}">${dev.brightness}</span></label>
                        <input type="range" min="0" max="255" value="${dev.brightness}" 
                               oninput="updateBrightnessLabel('${dev.name}', this.value)"
                               onchange="setBrightness('${dev.name}', this.value)"
                               id="brightness-${dev.name}">
                    </div>
                    
                    <!-- 颜色填充 -->
                    <div class="control-row color-control">
                        <input type="color" id="color-${dev.name}" value="${colorHex}">
                        <button class="btn btn-sm btn-primary" onclick="fillColor('${dev.name}')">填充</button>
                    </div>
                    
                    <div class="preset-colors">
                        <button class="color-preset" style="background:#ff0000" onclick="quickFill('${dev.name}', '#ff0000')" title="红"></button>
                        <button class="color-preset" style="background:#00ff00" onclick="quickFill('${dev.name}', '#00ff00')" title="绿"></button>
                        <button class="color-preset" style="background:#0000ff" onclick="quickFill('${dev.name}', '#0000ff')" title="蓝"></button>
                        <button class="color-preset" style="background:#ffff00" onclick="quickFill('${dev.name}', '#ffff00')" title="黄"></button>
                        <button class="color-preset" style="background:#ff00ff" onclick="quickFill('${dev.name}', '#ff00ff')" title="品红"></button>
                        <button class="color-preset" style="background:#00ffff" onclick="quickFill('${dev.name}', '#00ffff')" title="青"></button>
                        <button class="color-preset" style="background:#ffffff" onclick="quickFill('${dev.name}', '#ffffff')" title="白"></button>
                        <button class="color-preset" style="background:#ff8000" onclick="quickFill('${dev.name}', '#ff8000')" title="橙"></button>
                    </div>
                </div>
                
                <!-- 右侧：程序动画 -->
                <div class="control-column effects-column">
                    <label class="column-title">程序动画 <span class="effect-count">(${deviceEffects.length})</span></label>
                    <div class="effects-grid">
                        ${effectsHtml}
                    </div>
                    <div class="effect-controls" id="effect-controls-${dev.name}" style="display:${currentAnimation ? 'block' : 'none'};">
                        <div class="effect-config">
                            <span class="current-effect" id="current-effect-${dev.name}">${currentAnimation || '-'}</span>
                            <div class="config-row">
                                <label>速度</label>
                                <input type="range" min="1" max="100" value="${currentSpeed}" id="effect-speed-${dev.name}">
                                <span id="speed-val-${dev.name}">${currentSpeed}</span>
                            </div>
                            <div class="config-row" id="color-row-${dev.name}" style="display:none;">
                                <label>颜色</label>
                                <input type="color" id="effect-color-${dev.name}" value="${colorHex}">
                            </div>
                            <div class="config-actions">
                                <button class="btn btn-sm btn-success" onclick="applyEffect('${dev.name}')">▶ 启动</button>
                                <button class="btn btn-sm btn-danger" onclick="stopEffect('${dev.name}')">⏹ 停止</button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    `;
}

function getDeviceIcon(name) {
    const icons = {
        'touch': '👆',
        'board': '🔲',
        'matrix': '🔢'
    };
    return icons[name.toLowerCase()] || '💡';
}

function getDeviceDescription(name) {
    const descriptions = {
        'touch': '触摸指示灯 (1颗 WS2812)',
        'board': '主板状态灯带 (28颗 WS2812)',
        'matrix': 'LED 矩阵屏 (16x16)'
    };
    return descriptions[name.toLowerCase()] || 'LED 设备';
}

function getEffectIcon(name) {
    const icons = {
        // 通用
        'rainbow': '🌈',
        'breathing': '💨',
        'solid': '⬛',
        'sparkle': '✨',
        // Touch 专属
        'pulse': '💓',
        'color_cycle': '🔄',
        'heartbeat': '❤️',
        // Board 专属
        'chase': '🏃',
        'comet': '☄️',
        'spin': '🔄',
        'breathe_wave': '🌊',
        // Matrix 专属
        'fire': '🔥',
        'rain': '🌧️',
        'coderain': '💻',
        'plasma': '🎆',
        'ripple': '💧',
        // 其他
        'wave': '🌊',
        'gradient': '🎨',
        'twinkle': '⭐'
    };
    return icons[name.toLowerCase()] || '🎯';
}

// 当前选中的特效
const selectedEffects = {};

// 支持颜色参数的特效
const colorSupportedEffects = ['breathing', 'solid', 'rain'];

function showEffectConfig(device, effect) {
    // 记录选中的特效
    selectedEffects[device] = effect;
    
    // 更新特效名显示
    const currentEffectEl = document.getElementById(`current-effect-${device}`);
    if (currentEffectEl) {
        currentEffectEl.textContent = `${getEffectIcon(effect)} ${effect}`;
    }
    
    // 显示/隐藏颜色配置（只有支持颜色的特效才显示）
    const colorRow = document.getElementById(`color-row-${device}`);
    if (colorRow) {
        colorRow.style.display = colorSupportedEffects.includes(effect) ? 'flex' : 'none';
    }
    
    // 显示配置面板
    const controlsEl = document.getElementById(`effect-controls-${device}`);
    if (controlsEl) {
        controlsEl.style.display = 'block';
    }
    
    // 绑定速度滑块的实时显示
    const speedSlider = document.getElementById(`effect-speed-${device}`);
    const speedVal = document.getElementById(`speed-val-${device}`);
    if (speedSlider && speedVal) {
        speedSlider.oninput = () => { speedVal.textContent = speedSlider.value; };
    }
}

async function applyEffect(device) {
    const effect = selectedEffects[device];
    if (!effect) {
        showToast('请先选择一个特效', 'warning');
        return;
    }
    
    const speed = parseInt(document.getElementById(`effect-speed-${device}`)?.value || '50');
    const color = document.getElementById(`effect-color-${device}`)?.value || '#ff0000';
    
    try {
        const params = { speed };
        // 只有支持颜色的特效才传递颜色参数
        if (colorSupportedEffects.includes(effect)) {
            params.color = color;
        }
        await api.ledEffectStart(device, effect, params);
        
        // 更新状态为开启
        ledStates[device] = true;
        const btn = document.getElementById(`toggle-${device}`);
        if (btn) {
            btn.classList.add('on');
            btn.querySelector('.toggle-icon').textContent = '⬛';
            btn.querySelector('.toggle-text').textContent = '关灯';
        }
        
        showToast(`${device}: ${effect} 已启动 (速度: ${speed})`, 'success');
    } catch (e) {
        showToast(`启动特效失败: ${e.message}`, 'error');
    }
}

function updateBrightnessLabel(device, value) {
    const label = document.getElementById(`brightness-val-${device}`);
    if (label) label.textContent = value;
}

async function setBrightness(device, value) {
    try {
        await api.ledBrightness(device, parseInt(value));
        showToast(`${device} 亮度: ${value}`, 'success');
    } catch (e) { 
        showToast(`设置 ${device} 亮度失败: ${e.message}`, 'error'); 
    }
}

// LED 开关状态记录
const ledStates = {};

async function toggleLed(device) {
    const btn = document.getElementById(`toggle-${device}`);
    const isOn = ledStates[device] || false;
    
    try {
        if (isOn) {
            // 当前是开启状态，关闭它
            await api.ledClear(device);
            ledStates[device] = false;
            btn.classList.remove('on');
            btn.querySelector('.toggle-icon').textContent = '💡';
            btn.querySelector('.toggle-text').textContent = '开灯';
            showToast(`${device} 已关闭`, 'success');
        } else {
            // 当前是关闭状态，开启它（白光）
            await api.ledFill(device, '#ffffff');
            ledStates[device] = true;
            btn.classList.add('on');
            btn.querySelector('.toggle-icon').textContent = '⬛';
            btn.querySelector('.toggle-text').textContent = '关灯';
            showToast(`${device} 已开启`, 'success');
        }
    } catch (e) {
        showToast(`操作失败: ${e.message}`, 'error');
    }
}

async function ledOn(device, color = '#ffffff') {
    try {
        await api.ledFill(device, color);
        // 更新状态
        ledStates[device] = true;
        const btn = document.getElementById(`toggle-${device}`);
        if (btn) {
            btn.classList.add('on');
            btn.querySelector('.toggle-icon').textContent = '⬛';
            btn.querySelector('.toggle-text').textContent = '关灯';
        }
        showToast(`${device} 已开启`, 'success');
    } catch (e) {
        showToast(`开启失败: ${e.message}`, 'error');
    }
}

async function fillColor(device) {
    const color = document.getElementById(`color-${device}`).value;
    try {
        await api.ledFill(device, color);
        // 更新状态为开启
        ledStates[device] = true;
        const btn = document.getElementById(`toggle-${device}`);
        if (btn) {
            btn.classList.add('on');
            btn.querySelector('.toggle-icon').textContent = '⬛';
            btn.querySelector('.toggle-text').textContent = '关灯';
        }
        showToast(`${device} 已填充 ${color}`, 'success');
    } catch (e) {
        showToast(`${device} 填充失败: ${e.message}`, 'error');
    }
}

async function quickFill(device, color) {
    document.getElementById(`color-${device}`).value = color;
    try {
        await api.ledFill(device, color);
        // 更新状态为开启
        ledStates[device] = true;
        const btn = document.getElementById(`toggle-${device}`);
        if (btn) {
            btn.classList.add('on');
            btn.querySelector('.toggle-icon').textContent = '⬛';
            btn.querySelector('.toggle-text').textContent = '关灯';
        }
        showToast(`${device} → ${color}`, 'success');
    } catch (e) {
        showToast(`填充失败: ${e.message}`, 'error');
    }
}

async function clearLed(device) {
    try {
        await api.ledClear(device);
        // 更新状态为关闭
        ledStates[device] = false;
        const btn = document.getElementById(`toggle-${device}`);
        if (btn) {
            btn.classList.remove('on');
            btn.querySelector('.toggle-icon').textContent = '💡';
            btn.querySelector('.toggle-text').textContent = '开灯';
        }
        showToast(`${device} 已关闭`, 'success');
    } catch (e) {
        showToast(`关闭失败: ${e.message}`, 'error');
    }
}

async function startEffect(device, effect) {
    try {
        await api.ledEffectStart(device, effect);
        // 更新状态为开启
        ledStates[device] = true;
        const btn = document.getElementById(`toggle-${device}`);
        if (btn) {
            btn.classList.add('on');
            btn.querySelector('.toggle-icon').textContent = '⬛';
            btn.querySelector('.toggle-text').textContent = '关灯';
        }
        showToast(`${device}: ${effect} 已启动`, 'success');
    } catch (e) {
        showToast(`启动特效失败: ${e.message}`, 'error');
    }
}

async function stopEffect(device) {
    try {
        await api.ledEffectStop(device);
        // 隐藏配置面板
        const controlsEl = document.getElementById(`effect-controls-${device}`);
        if (controlsEl) {
            controlsEl.style.display = 'none';
        }
        // 清除选中状态
        delete selectedEffects[device];
        showToast(`${device} 特效已停止`, 'success');
    } catch (e) {
        showToast(`停止特效失败: ${e.message}`, 'error');
    }
}

async function saveLedConfig(device) {
    try {
        const result = await api.call('led.save', { device });
        if (result.animation) {
            showToast(`${device} 配置已保存: ${result.animation}`, 'success');
        } else {
            showToast(`${device} 配置已保存`, 'success');
        }
    } catch (e) {
        showToast(`保存配置失败: ${e.message}`, 'error');
    }
}

// =========================================================================
//                         网络页面
// =========================================================================

async function loadNetworkPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-network">
            <h1>网络配置</h1>
            
            <div class="cards">
                <div class="card">
                    <h3>🔌 以太网</h3>
                    <div class="card-content" id="eth-info">
                        <p><strong>状态:</strong> <span id="net-eth-status">-</span></p>
                        <p><strong>IP:</strong> <span id="net-eth-ip">-</span></p>
                        <p><strong>网关:</strong> <span id="net-eth-gw">-</span></p>
                        <p><strong>MAC:</strong> <span id="net-eth-mac">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>📶 WiFi</h3>
                    <div class="card-content" id="wifi-info">
                        <p><strong>状态:</strong> <span id="net-wifi-status">-</span></p>
                        <p><strong>SSID:</strong> <span id="net-wifi-ssid">-</span></p>
                        <p><strong>IP:</strong> <span id="net-wifi-ip">-</span></p>
                        <p><strong>信号:</strong> <span id="net-wifi-rssi">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn" onclick="showWifiScan()">扫描网络</button>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🔀 DHCP 服务器</h3>
                    <div class="card-content" id="dhcp-info">
                        <p><strong>状态:</strong> <span id="net-dhcp-status">-</span></p>
                        <p><strong>客户端:</strong> <span id="net-dhcp-clients">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🌍 NAT</h3>
                    <div class="card-content" id="nat-info">
                        <p><strong>状态:</strong> <span id="net-nat-status">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn" id="nat-toggle-btn" onclick="toggleNat()">启用</button>
                    </div>
                </div>
            </div>
            
            <div class="section hidden" id="wifi-scan-section">
                <h2>WiFi 网络列表</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>SSID</th><th>信号</th><th>加密</th><th>操作</th></tr>
                    </thead>
                    <tbody id="wifi-scan-results"></tbody>
                </table>
            </div>
        </div>
    `;
    
    await refreshNetworkPage();
}

async function refreshNetworkPage() {
    // 网络状态
    try {
        const status = await api.networkStatus();
        if (status.data) {
            const eth = status.data.ethernet || {};
            const wifi = status.data.wifi || {};
            
            document.getElementById('net-eth-status').textContent = eth.status || '-';
            document.getElementById('net-eth-ip').textContent = eth.ip || '-';
            document.getElementById('net-eth-gw').textContent = eth.gateway || '-';
            document.getElementById('net-eth-mac').textContent = eth.mac || '-';
            
            document.getElementById('net-wifi-status').textContent = wifi.connected ? '已连接' : '未连接';
            document.getElementById('net-wifi-ssid').textContent = wifi.ssid || '-';
            document.getElementById('net-wifi-ip').textContent = wifi.ip || '-';
            document.getElementById('net-wifi-rssi').textContent = wifi.rssi ? `${wifi.rssi} dBm` : '-';
        }
    } catch (e) { console.log('Network status error:', e); }
    
    // DHCP 状态
    try {
        const dhcp = await api.dhcpStatus();
        if (dhcp.data) {
            document.getElementById('net-dhcp-status').textContent = dhcp.data.enabled ? '运行中' : '已停止';
        }
        const clients = await api.dhcpClients();
        if (clients.data) {
            document.getElementById('net-dhcp-clients').textContent = 
                (clients.data.clients?.length || 0) + ' 个';
        }
    } catch (e) { console.log('DHCP error:', e); }
    
    // NAT 状态
    try {
        const nat = await api.natStatus();
        if (nat.data) {
            const enabled = nat.data.enabled;
            document.getElementById('net-nat-status').textContent = enabled ? '已启用' : '已禁用';
            document.getElementById('nat-toggle-btn').textContent = enabled ? '禁用' : '启用';
        }
    } catch (e) { console.log('NAT error:', e); }
}

async function showWifiScan() {
    const section = document.getElementById('wifi-scan-section');
    const tbody = document.getElementById('wifi-scan-results');
    
    section.classList.remove('hidden');
    tbody.innerHTML = '<tr><td colspan="4">扫描中...</td></tr>';
    
    try {
        const result = await api.wifiScan();
        if (result.data && result.data.networks) {
            tbody.innerHTML = result.data.networks.map(net => `
                <tr>
                    <td>${net.ssid}</td>
                    <td>${net.rssi} dBm</td>
                    <td>${net.auth || 'OPEN'}</td>
                    <td><button class="btn btn-small" onclick="connectWifi('${net.ssid}')">连接</button></td>
                </tr>
            `).join('');
        }
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4">扫描失败</td></tr>';
    }
}

function connectWifi(ssid) {
    const password = prompt(`输入 ${ssid} 的密码:`);
    if (password !== null) {
        api.wifiConnect(ssid, password)
            .then(() => showToast('正在连接...', 'info'))
            .catch(e => showToast('连接失败: ' + e.message, 'error'));
    }
}

async function toggleNat() {
    try {
        const status = await api.natStatus();
        if (status.data?.enabled) {
            await api.natDisable();
            showToast('NAT 已禁用', 'success');
        } else {
            await api.natEnable();
            showToast('NAT 已启用', 'success');
        }
        await refreshNetworkPage();
    } catch (e) { showToast('操作失败', 'error'); }
}

// =========================================================================
//                         设备页面
// =========================================================================

async function loadDevicePage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-device">
            <h1>设备控制</h1>
            
            <div class="cards">
                <div class="card">
                    <h3>🖥️ AGX</h3>
                    <div class="card-content">
                        <p><strong>电源:</strong> <span id="dev-agx-power">-</span></p>
                        <p><strong>CPU:</strong> <span id="dev-agx-cpu">-</span></p>
                        <p><strong>GPU:</strong> <span id="dev-agx-gpu">-</span></p>
                        <p><strong>温度:</strong> <span id="dev-agx-temp">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn btn-success" onclick="devicePower('agx', true)">开机</button>
                        <button class="btn btn-danger" onclick="devicePower('agx', false)">关机</button>
                        <button class="btn btn-warning" onclick="deviceReset('agx')">重启</button>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🔋 LPMU</h3>
                    <div class="card-content">
                        <p><strong>电源:</strong> <span id="dev-lpmu-power">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn btn-success" onclick="devicePower('lpmu', true)">开机</button>
                        <button class="btn btn-danger" onclick="devicePower('lpmu', false)">关机</button>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <h2>🌀 风扇控制</h2>
                <div class="fans-grid" id="fans-grid"></div>
            </div>
            
            <div class="section">
                <h2>⚡ 电源状态</h2>
                <div class="power-info" id="power-info"></div>
            </div>
        </div>
    `;
    
    await refreshDevicePage();
    refreshInterval = setInterval(refreshDevicePage, 2000);
}

async function refreshDevicePage() {
    // 设备状态
    try {
        const status = await api.deviceStatus();
        if (status.data?.devices) {
            const agx = status.data.devices.find(d => d.name === 'agx');
            const lpmu = status.data.devices.find(d => d.name === 'lpmu');
            
            document.getElementById('dev-agx-power').textContent = agx?.powered ? '运行中' : '关机';
            document.getElementById('dev-lpmu-power').textContent = lpmu?.powered ? '运行中' : '关机';
        }
    } catch (e) { console.log('Device status error:', e); }
    
    // AGX 监控数据
    try {
        const agxData = await api.agxData();
        if (agxData.data) {
            document.getElementById('dev-agx-cpu').textContent = 
                agxData.data.cpu_usage ? `${agxData.data.cpu_usage}%` : '-';
            document.getElementById('dev-agx-gpu').textContent = 
                agxData.data.gpu_usage ? `${agxData.data.gpu_usage}%` : '-';
            document.getElementById('dev-agx-temp').textContent = 
                agxData.data.temperature ? `${agxData.data.temperature}°C` : '-';
        }
    } catch (e) { /* AGX 可能未连接 */ }
    
    // 风扇
    try {
        const fans = await api.fanStatus();
        const container = document.getElementById('fans-grid');
        if (fans.data?.fans) {
            container.innerHTML = fans.data.fans.map(fan => `
                <div class="fan-card">
                    <h4>风扇 ${fan.id}</h4>
                    <p>模式: ${fan.mode}</p>
                    <p>转速: ${fan.speed}%</p>
                    <p>RPM: ${fan.rpm || '-'}</p>
                    <input type="range" min="0" max="100" value="${fan.speed}" 
                           onchange="setFanSpeed(${fan.id}, this.value)">
                </div>
            `).join('');
        }
    } catch (e) { console.log('Fan error:', e); }
    
    // 电源
    try {
        const power = await api.powerStatus();
        const container = document.getElementById('power-info');
        if (power.data) {
            container.innerHTML = `
                <div class="power-card">
                    <p><strong>电压:</strong> ${power.data.voltage || '-'} V</p>
                    <p><strong>电流:</strong> ${power.data.current || '-'} A</p>
                    <p><strong>功率:</strong> ${power.data.power || '-'} W</p>
                </div>
            `;
        }
    } catch (e) { console.log('Power error:', e); }
}

async function devicePower(name, on) {
    try {
        await api.devicePower(name, on);
        showToast(`${name} ${on ? '开机' : '关机'} 命令已发送`, 'success');
        await refreshDevicePage();
    } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
}

async function deviceReset(name) {
    if (confirm(`确定要重启 ${name} 吗？`)) {
        try {
            await api.deviceReset(name);
            showToast(`${name} 重启命令已发送`, 'success');
        } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
    }
}

async function setFanSpeed(id, speed) {
    try {
        await api.fanSet(id, parseInt(speed));
    } catch (e) { showToast('设置风扇失败', 'error'); }
}

// =========================================================================
//                         文件管理页面
// =========================================================================

let currentFilePath = '/sdcard';

async function loadFilesPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-files">
            <h1>📂 文件管理</h1>
            
            <div class="file-toolbar">
                <div class="breadcrumb" id="breadcrumb"></div>
                <div class="file-actions">
                    <button class="btn btn-primary" onclick="showUploadDialog()">📤 上传文件</button>
                    <button class="btn" onclick="showNewFolderDialog()">📁 新建文件夹</button>
                    <button class="btn" onclick="refreshFilesPage()">🔄 刷新</button>
                </div>
            </div>
            
            <div class="storage-tabs">
                <button class="tab-btn active" onclick="navigateToPath('/sdcard')">💾 SD 卡</button>
                <button class="tab-btn" onclick="navigateToPath('/spiffs')">💿 SPIFFS</button>
            </div>
            
            <div class="file-list" id="file-list">
                <div class="loading">加载中...</div>
            </div>
            
            <!-- 存储状态 -->
            <div class="storage-status" id="storage-status"></div>
        </div>
        
        <!-- 上传对话框 -->
        <div id="upload-modal" class="modal hidden">
            <div class="modal-content">
                <h2>上传文件</h2>
                <div class="upload-area" id="upload-area">
                    <p>点击选择文件或拖拽文件到此处</p>
                    <input type="file" id="file-input" multiple style="display:none" onchange="handleFileSelect(event)">
                </div>
                <div id="upload-list"></div>
                <div class="form-actions">
                    <button class="btn" onclick="closeUploadDialog()">取消</button>
                    <button class="btn btn-primary" onclick="uploadFiles()">上传</button>
                </div>
            </div>
        </div>
        
        <!-- 新建文件夹对话框 -->
        <div id="newfolder-modal" class="modal hidden">
            <div class="modal-content">
                <h2>新建文件夹</h2>
                <div class="form-group">
                    <label>文件夹名称</label>
                    <input type="text" id="new-folder-name" placeholder="输入文件夹名称">
                </div>
                <div class="form-actions">
                    <button class="btn" onclick="closeNewFolderDialog()">取消</button>
                    <button class="btn btn-primary" onclick="createNewFolder()">创建</button>
                </div>
            </div>
        </div>
        
        <!-- 重命名对话框 -->
        <div id="rename-modal" class="modal hidden">
            <div class="modal-content">
                <h2>重命名</h2>
                <div class="form-group">
                    <label>新名称</label>
                    <input type="text" id="rename-input" placeholder="输入新名称">
                </div>
                <input type="hidden" id="rename-original-path">
                <div class="form-actions">
                    <button class="btn" onclick="closeRenameDialog()">取消</button>
                    <button class="btn btn-primary" onclick="doRename()">确定</button>
                </div>
            </div>
        </div>
    `;
    
    // 设置拖拽上传
    setupDragAndDrop();
    
    await refreshFilesPage();
}

async function refreshFilesPage() {
    await loadDirectory(currentFilePath);
    await loadStorageStatus();
}

async function loadDirectory(path) {
    currentFilePath = path;
    const listContainer = document.getElementById('file-list');
    
    // 移除旧的事件监听器
    listContainer.removeEventListener('click', handleFileListClick);
    
    console.log('Loading directory:', path);
    
    try {
        const result = await api.storageList(path);
        console.log('storageList result:', result);
        const entries = result.data?.entries || [];
        
        // 更新面包屑
        updateBreadcrumb(path);
        
        // 更新存储标签页
        document.querySelectorAll('.storage-tabs .tab-btn').forEach(btn => {
            btn.classList.remove('active');
            if (path.startsWith('/sdcard') && btn.textContent.includes('SD')) {
                btn.classList.add('active');
            } else if (path.startsWith('/spiffs') && btn.textContent.includes('SPIFFS')) {
                btn.classList.add('active');
            }
        });
        
        if (entries.length === 0) {
            listContainer.innerHTML = '<div class="empty-folder">📂 空文件夹</div>';
            // 仍然添加事件监听器（虽然没有文件）
            listContainer.addEventListener('click', handleFileListClick);
            return;
        }
        
        // 排序：目录在前，文件在后，按名称排序
        entries.sort((a, b) => {
            if (a.type === 'dir' && b.type !== 'dir') return -1;
            if (a.type !== 'dir' && b.type === 'dir') return 1;
            return a.name.localeCompare(b.name);
        });
        
        listContainer.innerHTML = `
            <table class="file-table">
                <thead>
                    <tr>
                        <th>名称</th>
                        <th>大小</th>
                        <th>操作</th>
                    </tr>
                </thead>
                <tbody>
                    ${entries.map(entry => {
                        const fullPath = path + '/' + entry.name;
                        const icon = entry.type === 'dir' ? '📁' : getFileIcon(entry.name);
                        const size = entry.type === 'dir' ? '-' : formatFileSize(entry.size);
                        const escapedPath = fullPath.replace(/'/g, "\\'").replace(/"/g, '&quot;');
                        const escapedName = entry.name.replace(/'/g, "\\'").replace(/"/g, '&quot;');
                        return `
                            <tr class="file-row" data-path="${escapedPath}" data-type="${entry.type}" data-name="${escapedName}">
                                <td class="file-name ${entry.type === 'dir' ? 'clickable' : ''}">
                                    <span class="file-icon">${icon}</span>
                                    <span>${entry.name}</span>
                                </td>
                                <td class="file-size">${size}</td>
                                <td class="file-actions-cell">
                                    ${entry.type !== 'dir' ? 
                                        `<button class="btn btn-sm btn-download" title="下载">📥</button>` : ''}
                                    <button class="btn btn-sm btn-rename" title="重命名">✏️</button>
                                    <button class="btn btn-sm btn-danger btn-delete" title="删除">🗑️</button>
                                </td>
                            </tr>
                        `;
                    }).join('')}
                </tbody>
            </table>
        `;
        
        // 使用事件委托处理点击
        listContainer.addEventListener('click', handleFileListClick);
    } catch (e) {
        console.error('loadDirectory error:', e);
        listContainer.innerHTML = `<div class="error">加载失败: ${e.message}</div>`;
    }
}

// 事件委托处理文件列表点击
function handleFileListClick(e) {
    const row = e.target.closest('.file-row');
    if (!row) return;
    
    const path = row.dataset.path;
    const type = row.dataset.type;
    const name = row.dataset.name;
    
    // 点击文件夹名称 - 进入目录
    if (e.target.closest('.file-name.clickable')) {
        navigateToPath(path);
        return;
    }
    
    // 点击下载按钮
    if (e.target.closest('.btn-download')) {
        downloadFile(path);
        return;
    }
    
    // 点击重命名按钮
    if (e.target.closest('.btn-rename')) {
        showRenameDialog(path, name);
        return;
    }
    
    // 点击删除按钮
    if (e.target.closest('.btn-delete')) {
        deleteFile(path);
        return;
    }
}

async function loadStorageStatus() {
    try {
        const status = await api.storageStatus();
        const container = document.getElementById('storage-status');
        
        const formatStorage = (type, data) => {
            if (!data?.mounted) return `<span class="unmounted">未挂载</span>`;
            return `<span class="mounted">已挂载</span>`;
        };
        
        container.innerHTML = `
            <div class="storage-info">
                <span>💾 SD: ${formatStorage('sd', status.data?.sd)}</span>
                <span>💿 SPIFFS: ${formatStorage('spiffs', status.data?.spiffs)}</span>
            </div>
        `;
    } catch (e) {
        console.log('Storage status error:', e);
    }
}

function updateBreadcrumb(path) {
    const container = document.getElementById('breadcrumb');
    const parts = path.split('/').filter(p => p);
    
    let html = '<span class="breadcrumb-item" onclick="navigateToPath(\'/\')">🏠</span>';
    let currentPath = '';
    
    parts.forEach((part, i) => {
        currentPath += '/' + part;
        const isLast = i === parts.length - 1;
        html += ` / <span class="breadcrumb-item${isLast ? ' current' : ''}" 
                        onclick="navigateToPath('${currentPath}')">${part}</span>`;
    });
    
    container.innerHTML = html;
}

function navigateToPath(path) {
    loadDirectory(path);
}

function getFileIcon(name) {
    const ext = name.split('.').pop().toLowerCase();
    const icons = {
        'txt': '📄', 'json': '📋', 'xml': '📋', 'csv': '📊',
        'jpg': '🖼️', 'jpeg': '🖼️', 'png': '🖼️', 'gif': '🖼️', 'bmp': '🖼️',
        'mp3': '🎵', 'wav': '🎵', 'ogg': '🎵',
        'mp4': '🎬', 'avi': '🎬', 'mkv': '🎬',
        'zip': '📦', 'rar': '📦', 'tar': '📦', 'gz': '📦',
        'bin': '💾', 'hex': '💾', 'elf': '💾',
        'c': '📝', 'h': '📝', 'cpp': '📝', 'py': '📝', 'js': '📝',
        'fnt': '🔤', 'ttf': '🔤'
    };
    return icons[ext] || '📄';
}

function formatFileSize(bytes) {
    if (bytes === 0) return '0 B';
    if (bytes === undefined) return '-';
    const units = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(1024));
    return (bytes / Math.pow(1024, i)).toFixed(i > 0 ? 1 : 0) + ' ' + units[i];
}

// 上传相关
let filesToUpload = [];

function showUploadDialog() {
    filesToUpload = [];
    document.getElementById('upload-list').innerHTML = '';
    document.getElementById('upload-modal').classList.remove('hidden');
}

function closeUploadDialog() {
    document.getElementById('upload-modal').classList.add('hidden');
}

function setupDragAndDrop() {
    const uploadArea = document.getElementById('upload-area');
    if (!uploadArea) return;
    
    uploadArea.onclick = () => document.getElementById('file-input').click();
    
    uploadArea.ondragover = (e) => {
        e.preventDefault();
        uploadArea.classList.add('drag-over');
    };
    
    uploadArea.ondragleave = () => {
        uploadArea.classList.remove('drag-over');
    };
    
    uploadArea.ondrop = (e) => {
        e.preventDefault();
        uploadArea.classList.remove('drag-over');
        handleFileSelect({ target: { files: e.dataTransfer.files } });
    };
}

function handleFileSelect(event) {
    const files = Array.from(event.target.files);
    filesToUpload = filesToUpload.concat(files);
    
    const listContainer = document.getElementById('upload-list');
    listContainer.innerHTML = filesToUpload.map((f, i) => `
        <div class="upload-item">
            <span>${f.name}</span>
            <span class="file-size">${formatFileSize(f.size)}</span>
            <button class="btn btn-sm" onclick="removeUploadFile(${i})">✕</button>
        </div>
    `).join('');
}

function removeUploadFile(index) {
    filesToUpload.splice(index, 1);
    handleFileSelect({ target: { files: [] } });
}

async function uploadFiles() {
    if (filesToUpload.length === 0) {
        showToast('请选择要上传的文件', 'warning');
        return;
    }
    
    const listContainer = document.getElementById('upload-list');
    
    for (let i = 0; i < filesToUpload.length; i++) {
        const file = filesToUpload[i];
        const targetPath = currentFilePath + '/' + file.name;
        
        // 更新状态
        const items = listContainer.querySelectorAll('.upload-item');
        if (items[i]) {
            items[i].innerHTML = `<span>${file.name}</span><span class="uploading">上传中...</span>`;
        }
        
        try {
            console.log('Uploading file:', targetPath);
            const result = await api.fileUpload(targetPath, file);
            console.log('Upload result:', result);
            if (items[i]) {
                items[i].innerHTML = `<span>${file.name}</span><span class="success">✓ 完成</span>`;
            }
        } catch (e) {
            console.error('Upload error:', e);
            if (items[i]) {
                items[i].innerHTML = `<span>${file.name}</span><span class="error">✕ 失败: ${e.message}</span>`;
            }
        }
    }
    
    showToast('上传完成', 'success');
    setTimeout(() => {
        closeUploadDialog();
        refreshFilesPage();
    }, 1000);
}

// 新建文件夹
function showNewFolderDialog() {
    document.getElementById('new-folder-name').value = '';
    document.getElementById('newfolder-modal').classList.remove('hidden');
}

function closeNewFolderDialog() {
    document.getElementById('newfolder-modal').classList.add('hidden');
}

async function createNewFolder() {
    const name = document.getElementById('new-folder-name').value.trim();
    if (!name) {
        showToast('请输入文件夹名称', 'warning');
        return;
    }
    
    const path = currentFilePath + '/' + name;
    try {
        await api.storageMkdir(path);
        showToast('文件夹创建成功', 'success');
        closeNewFolderDialog();
        refreshFilesPage();
    } catch (e) {
        showToast('创建失败: ' + e.message, 'error');
    }
}

// 重命名
function showRenameDialog(path, currentName) {
    document.getElementById('rename-input').value = currentName;
    document.getElementById('rename-original-path').value = path;
    document.getElementById('rename-modal').classList.remove('hidden');
}

function closeRenameDialog() {
    document.getElementById('rename-modal').classList.add('hidden');
}

async function doRename() {
    const newName = document.getElementById('rename-input').value.trim();
    const originalPath = document.getElementById('rename-original-path').value;
    
    if (!newName) {
        showToast('请输入新名称', 'warning');
        return;
    }
    
    // 构建新路径
    const pathParts = originalPath.split('/');
    pathParts.pop();
    const newPath = pathParts.join('/') + '/' + newName;
    
    try {
        await api.storageRename(originalPath, newPath);
        showToast('重命名成功', 'success');
        closeRenameDialog();
        refreshFilesPage();
    } catch (e) {
        showToast('重命名失败: ' + e.message, 'error');
    }
}

// 下载文件
async function downloadFile(path) {
    console.log('Downloading file:', path);
    try {
        const blob = await api.fileDownload(path);
        console.log('Download blob:', blob);
        const filename = path.split('/').pop();
        
        // 创建下载链接
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        showToast('下载开始', 'success');
    } catch (e) {
        console.error('Download error:', e);
        showToast('下载失败: ' + e.message, 'error');
    }
}

// 删除文件
async function deleteFile(path) {
    const name = path.split('/').pop();
    if (!confirm(`确定要删除 "${name}" 吗？`)) {
        return;
    }
    
    try {
        await api.storageDelete(path);
        showToast('删除成功', 'success');
        refreshFilesPage();
    } catch (e) {
        showToast('删除失败: ' + e.message, 'error');
    }
}

// =========================================================================
//                         配置页面
// =========================================================================

async function loadConfigPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-config">
            <h1>系统配置</h1>
            
            <div class="section">
                <h2>配置列表</h2>
                <div class="config-filter">
                    <input type="text" id="config-prefix" placeholder="输入前缀过滤 (如 network.)">
                    <button class="btn" onclick="filterConfig()">筛选</button>
                    <button class="btn" onclick="loadAllConfig()">显示全部</button>
                </div>
                <table class="data-table">
                    <thead>
                        <tr><th>键</th><th>值</th><th>类型</th><th>操作</th></tr>
                    </thead>
                    <tbody id="config-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>添加/修改配置</h2>
                <form id="config-form" class="config-form" onsubmit="saveConfig(event)">
                    <div class="form-row">
                        <div class="form-group">
                            <label>键名</label>
                            <input type="text" id="cfg-key" required placeholder="network.hostname">
                        </div>
                        <div class="form-group">
                            <label>值</label>
                            <input type="text" id="cfg-value" required>
                        </div>
                        <div class="form-group">
                            <label>持久化</label>
                            <input type="checkbox" id="cfg-persist">
                        </div>
                    </div>
                    <button type="submit" class="btn btn-primary">保存</button>
                </form>
            </div>
        </div>
    `;
    
    await loadAllConfig();
}

async function loadAllConfig() {
    const tbody = document.getElementById('config-table-body');
    tbody.innerHTML = '<tr><td colspan="4">加载中...</td></tr>';
    
    try {
        const result = await api.configList();
        if (result.data?.items) {
            tbody.innerHTML = result.data.items.map(item => `
                <tr>
                    <td><code>${item.key}</code></td>
                    <td>${item.value}</td>
                    <td>${item.type || '-'}</td>
                    <td>
                        <button class="btn btn-small" onclick="editConfig('${item.key}', '${item.value}')">编辑</button>
                        <button class="btn btn-small btn-danger" onclick="deleteConfig('${item.key}')">删除</button>
                    </td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="4">暂无配置</td></tr>';
        }
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4">加载失败</td></tr>';
    }
}

async function filterConfig() {
    const prefix = document.getElementById('config-prefix').value;
    const tbody = document.getElementById('config-table-body');
    
    try {
        const result = await api.configList(prefix);
        if (result.data?.items) {
            tbody.innerHTML = result.data.items.map(item => `
                <tr>
                    <td><code>${item.key}</code></td>
                    <td>${item.value}</td>
                    <td>${item.type || '-'}</td>
                    <td>
                        <button class="btn btn-small" onclick="editConfig('${item.key}', '${item.value}')">编辑</button>
                        <button class="btn btn-small btn-danger" onclick="deleteConfig('${item.key}')">删除</button>
                    </td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="4">暂无匹配配置</td></tr>';
        }
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4">加载失败</td></tr>';
    }
}

function editConfig(key, value) {
    document.getElementById('cfg-key').value = key;
    document.getElementById('cfg-value').value = value;
}

async function saveConfig(e) {
    e.preventDefault();
    
    const key = document.getElementById('cfg-key').value;
    const value = document.getElementById('cfg-value').value;
    const persist = document.getElementById('cfg-persist').checked;
    
    try {
        await api.configSet(key, value, persist);
        showToast('配置已保存', 'success');
        await loadAllConfig();
        document.getElementById('config-form').reset();
    } catch (e) {
        showToast('保存失败: ' + e.message, 'error');
    }
}

async function deleteConfig(key) {
    if (confirm(`确定要删除配置 "${key}" 吗？`)) {
        try {
            await api.configDelete(key);
            showToast('配置已删除', 'success');
            await loadAllConfig();
        } catch (e) {
            showToast('删除失败: ' + e.message, 'error');
        }
    }
}

// =========================================================================
//                         安全页面
// =========================================================================

async function loadSecurityPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-security">
            <h1>安全与连接</h1>
            
            <div class="section">
                <h2>🔑 SSH 连接测试</h2>
                <form id="ssh-test-form" class="ssh-form" onsubmit="testSsh(event)">
                    <div class="form-row">
                        <div class="form-group">
                            <label>主机</label>
                            <input type="text" id="ssh-host" required placeholder="192.168.1.100">
                        </div>
                        <div class="form-group">
                            <label>端口</label>
                            <input type="number" id="ssh-port" value="22">
                        </div>
                        <div class="form-group">
                            <label>用户名</label>
                            <input type="text" id="ssh-user" required placeholder="root">
                        </div>
                        <div class="form-group">
                            <label>密码</label>
                            <input type="password" id="ssh-password" required>
                        </div>
                    </div>
                    <button type="submit" class="btn btn-primary">测试连接</button>
                </form>
                <div id="ssh-result" class="result-box hidden"></div>
            </div>
            
            <div class="section">
                <h2>🔐 密钥管理</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>ID</th><th>类型</th><th>备注</th><th>创建时间</th><th>操作</th></tr>
                    </thead>
                    <tbody id="keys-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>📡 已知主机</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>主机</th><th>端口</th><th>指纹</th><th>操作</th></tr>
                    </thead>
                    <tbody id="hosts-table-body"></tbody>
                </table>
            </div>
        </div>
    `;
    
    await refreshSecurityPage();
}

async function refreshSecurityPage() {
    // 密钥列表
    try {
        const keys = await api.keyList();
        const tbody = document.getElementById('keys-table-body');
        if (keys.data?.keys) {
            tbody.innerHTML = keys.data.keys.map(key => `
                <tr>
                    <td>${key.id}</td>
                    <td>${key.type}</td>
                    <td>${key.comment || '-'}</td>
                    <td>${key.created || '-'}</td>
                    <td><button class="btn btn-small btn-danger" onclick="deleteKey('${key.id}')">删除</button></td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="5">暂无密钥</td></tr>';
        }
    } catch (e) {
        document.getElementById('keys-table-body').innerHTML = '<tr><td colspan="5">加载失败</td></tr>';
    }
    
    // 已知主机
    try {
        const hosts = await api.hostsList();
        const tbody = document.getElementById('hosts-table-body');
        if (hosts.data?.hosts) {
            tbody.innerHTML = hosts.data.hosts.map(host => `
                <tr>
                    <td>${host.host}</td>
                    <td>${host.port}</td>
                    <td><code>${host.fingerprint?.substring(0, 20)}...</code></td>
                    <td><button class="btn btn-small btn-danger" onclick="removeHost('${host.id}')">移除</button></td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="4">暂无已知主机</td></tr>';
        }
    } catch (e) {
        document.getElementById('hosts-table-body').innerHTML = '<tr><td colspan="4">加载失败</td></tr>';
    }
}

async function testSsh(e) {
    e.preventDefault();
    
    const host = document.getElementById('ssh-host').value;
    const port = parseInt(document.getElementById('ssh-port').value);
    const user = document.getElementById('ssh-user').value;
    const password = document.getElementById('ssh-password').value;
    
    const resultBox = document.getElementById('ssh-result');
    resultBox.classList.remove('hidden');
    resultBox.textContent = '测试中...';
    resultBox.className = 'result-box';
    
    try {
        const result = await api.sshTest(host, user, password, port);
        resultBox.textContent = '✅ 连接成功!';
        resultBox.classList.add('success');
    } catch (e) {
        resultBox.textContent = '❌ 连接失败: ' + e.message;
        resultBox.classList.add('error');
    }
}

async function deleteKey(id) {
    if (confirm('确定要删除此密钥吗？')) {
        try {
            await api.call('key.delete', { id }, 'POST');
            showToast('密钥已删除', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('删除失败', 'error');
        }
    }
}

async function removeHost(id) {
    if (confirm('确定要移除此主机记录吗？')) {
        try {
            await api.call('hosts.remove', { id }, 'POST');
            showToast('主机已移除', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('移除失败', 'error');
        }
    }
}

// =========================================================================
//                         工具函数
// =========================================================================

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
    if (!bytes) return '-';
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function showToast(message, type = 'info') {
    // 创建 toast 元素
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        document.body.appendChild(toast);
    }
    
    toast.textContent = message;
    toast.className = `toast toast-${type} show`;
    
    setTimeout(() => {
        toast.classList.remove('show');
    }, 3000);
}

// =========================================================================
//                         终端页面
// =========================================================================

async function loadTerminalPage() {
    // 清理之前的终端实例
    if (webTerminal) {
        webTerminal.destroy();
        webTerminal = null;
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="terminal-page">
            <div class="terminal-header">
                <h1>🖥️ Web 终端</h1>
                <div class="terminal-actions">
                    <button class="btn btn-sm" onclick="terminalClear()">清屏</button>
                    <button class="btn btn-sm btn-danger" onclick="terminalDisconnect()">断开</button>
                </div>
            </div>
            <div class="terminal-container" id="terminal-container"></div>
            <div class="terminal-help">
                <span>💡 提示: 输入 <code>help</code> 查看命令 | <code>Ctrl+C</code> 中断 | <code>Ctrl+L</code> 清屏 | <code>↑↓</code> 历史</span>
            </div>
        </div>
    `;
    
    // 初始化终端
    webTerminal = new WebTerminal('terminal-container');
    const ok = await webTerminal.init();
    if (ok) {
        webTerminal.connect();
    }
}

function terminalClear() {
    if (webTerminal && webTerminal.terminal) {
        webTerminal.terminal.clear();
        webTerminal.writePrompt();
    }
}

function terminalDisconnect() {
    if (webTerminal) {
        webTerminal.disconnect();
        showToast('终端已断开', 'info');
    }
}

// 暴露给 HTML onclick
window.closeLoginModal = closeLoginModal;
window.confirmReboot = confirmReboot;
window.serviceAction = serviceAction;
window.setBrightness = setBrightness;
window.toggleLed = toggleLed;
window.clearLed = clearLed;
window.fillColor = fillColor;
window.quickFill = quickFill;
window.startEffect = startEffect;
window.stopEffect = stopEffect;
window.showEffectConfig = showEffectConfig;
window.applyEffect = applyEffect;
window.updateBrightnessLabel = updateBrightnessLabel;
window.showWifiScan = showWifiScan;
window.connectWifi = connectWifi;
window.toggleNat = toggleNat;
window.devicePower = devicePower;
window.deviceReset = deviceReset;
window.setFanSpeed = setFanSpeed;
window.filterConfig = filterConfig;
window.loadAllConfig = loadAllConfig;
window.editConfig = editConfig;
window.saveConfig = saveConfig;
window.deleteConfig = deleteConfig;
window.testSsh = testSsh;
window.deleteKey = deleteKey;
window.removeHost = removeHost;
window.terminalClear = terminalClear;
window.terminalDisconnect = terminalDisconnect;
// 文件管理
window.navigateToPath = navigateToPath;
window.showUploadDialog = showUploadDialog;
window.closeUploadDialog = closeUploadDialog;
window.showNewFolderDialog = showNewFolderDialog;
window.closeNewFolderDialog = closeNewFolderDialog;
window.createNewFolder = createNewFolder;
window.showRenameDialog = showRenameDialog;
window.closeRenameDialog = closeRenameDialog;
window.doRename = doRename;
window.downloadFile = downloadFile;
window.deleteFile = deleteFile;
window.uploadFiles = uploadFiles;
window.handleFileSelect = handleFileSelect;
window.removeUploadFile = removeUploadFile;
window.refreshFilesPage = refreshFilesPage;
