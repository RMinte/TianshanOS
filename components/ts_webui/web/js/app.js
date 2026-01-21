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
                <h2>🕐 系统时间</h2>
                <div class="info-grid">
                    <div class="info-item"><label>当前时间</label><span id="sys-datetime">-</span></div>
                    <div class="info-item"><label>同步状态</label><span id="sys-time-status">-</span></div>
                    <div class="info-item"><label>时间来源</label><span id="sys-time-source">-</span></div>
                    <div class="info-item"><label>NTP服务器</label><span id="sys-ntp-server">-</span></div>
                    <div class="info-item"><label>时区</label><span id="sys-timezone">-</span></div>
                    <div class="info-item"><label>同步次数</label><span id="sys-sync-count">-</span></div>
                </div>
                <div class="button-group" style="margin-top:15px">
                    <button class="btn" onclick="syncTimeFromBrowser()">🔄 从浏览器同步</button>
                    <button class="btn" onclick="forceNtpSync()">🌐 强制NTP同步</button>
                    <button class="btn" onclick="showTimezoneModal()">⚙️ 设置时区</button>
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
    
    // 时间同步信息
    try {
        const time = await api.timeInfo();
        if (time.data) {
            document.getElementById('sys-datetime').textContent = time.data.datetime || '-';
            const statusText = time.data.synced ? '✅ 已同步' : '⏳ 未同步';
            document.getElementById('sys-time-status').textContent = statusText;
            const sourceMap = { ntp: 'NTP服务器', http: '浏览器', manual: '手动设置', none: '未同步' };
            document.getElementById('sys-time-source').textContent = sourceMap[time.data.source] || time.data.source;
            document.getElementById('sys-ntp-server').textContent = time.data.ntp_server || '-';
            document.getElementById('sys-timezone').textContent = time.data.timezone || '-';
            document.getElementById('sys-sync-count').textContent = time.data.sync_count || '0';
        }
    } catch (e) { console.log('Time info error:', e); }
    
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

// 时间同步功能
async function syncTimeFromBrowser() {
    try {
        const now = Date.now();
        showToast('正在从浏览器同步时间...', 'info');
        const result = await api.timeSync(now);
        if (result.data?.synced) {
            showToast(`时间已同步: ${result.data.datetime}`, 'success');
            await refreshSystemPage();
        } else {
            showToast('时间同步失败', 'error');
        }
    } catch (e) {
        showToast('同步失败: ' + e.message, 'error');
    }
}

async function forceNtpSync() {
    try {
        showToast('正在强制NTP同步...', 'info');
        const result = await api.timeForceSync();
        if (result.data?.syncing) {
            showToast('NTP同步已启动，请稍候刷新查看结果', 'success');
            // 延迟刷新以等待同步完成
            setTimeout(refreshSystemPage, 3000);
        }
    } catch (e) {
        showToast('NTP同步失败: ' + e.message, 'error');
    }
}

function showTimezoneModal() {
    let modal = document.getElementById('timezone-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'timezone-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:400px">
            <h2>⚙️ 设置时区</h2>
            <div class="form-group">
                <label>时区</label>
                <select id="timezone-select" class="form-control">
                    <option value="CST-8">中国标准时间 (UTC+8)</option>
                    <option value="JST-9">日本标准时间 (UTC+9)</option>
                    <option value="KST-9">韩国标准时间 (UTC+9)</option>
                    <option value="UTC0">UTC (UTC+0)</option>
                    <option value="GMT0">GMT (UTC+0)</option>
                    <option value="EST5EDT">美国东部时间 (UTC-5)</option>
                    <option value="PST8PDT">美国太平洋时间 (UTC-8)</option>
                    <option value="CET-1CEST">中欧时间 (UTC+1)</option>
                </select>
            </div>
            <div class="form-group">
                <label>或自定义时区字符串</label>
                <input type="text" id="timezone-custom" class="form-control" placeholder="例如: CST-8">
            </div>
            <div class="form-actions">
                <button class="btn" onclick="hideTimezoneModal()">取消</button>
                <button class="btn btn-primary" onclick="applyTimezone()">应用</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function hideTimezoneModal() {
    const modal = document.getElementById('timezone-modal');
    if (modal) modal.classList.add('hidden');
}

async function applyTimezone() {
    const select = document.getElementById('timezone-select');
    const custom = document.getElementById('timezone-custom');
    const timezone = custom.value.trim() || select.value;
    
    try {
        const result = await api.timeSetTimezone(timezone);
        if (result.data?.success) {
            showToast(`时区已设置为 ${timezone}，本地时间: ${result.data.local_time}`, 'success');
            hideTimezoneModal();
            await refreshSystemPage();
        }
    } catch (e) {
        showToast('设置失败: ' + e.message, 'error');
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
            
            // 如果有 matrix 设备，加载字体列表
            if (result.data.devices.some(d => d.name === 'matrix' || d.layout === 'matrix')) {
                loadFontList();
            }
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
    
    // Matrix 专属功能区域
    const isMatrix = dev.name === 'matrix' || dev.layout === 'matrix';
    const matrixExtras = isMatrix ? generateMatrixExtras(dev) : '';
    
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
            ${matrixExtras}
        </div>
    `;
}

// 生成 Matrix 专属功能区域
function generateMatrixExtras(dev) {
    return `
        <div class="matrix-extras">
            <!-- 图像/动画 -->
            <div class="matrix-section">
                <label class="section-title">📷 图像/动画</label>
                <div class="matrix-controls">
                    <div class="control-row">
                        <input type="text" id="matrix-image-path" placeholder="/sdcard/images/..." class="input-full" value="/sdcard/images/">
                        <button class="btn btn-sm" onclick="browseImages()">📁</button>
                    </div>
                    <div class="control-row">
                        <label><input type="checkbox" id="matrix-image-center" checked> 居中</label>
                        <button class="btn btn-sm btn-primary" onclick="displayImage()">显示</button>
                    </div>
                </div>
            </div>
            
            <!-- QR 码 -->
            <div class="matrix-section">
                <label class="section-title">📱 QR 码</label>
                <div class="matrix-controls">
                    <div class="control-row">
                        <input type="text" id="matrix-qr-text" placeholder="输入文本或URL" class="input-full">
                    </div>
                    <div class="control-row">
                        <select id="matrix-qr-ecc" title="纠错级别">
                            <option value="L">低(L)</option>
                            <option value="M" selected>中(M)</option>
                            <option value="Q">较高(Q)</option>
                            <option value="H">高(H)</option>
                        </select>
                        <input type="color" id="matrix-qr-fg" value="#ffffff" title="前景色">
                        <button class="btn btn-sm btn-primary" onclick="generateQrCode()">生成</button>
                    </div>
                    <div class="control-row">
                        <label style="flex:1">背景图:</label>
                        <input type="text" id="matrix-qr-bg-image" placeholder="无背景图" readonly style="flex:2;cursor:pointer" onclick="openFilePickerFor('matrix-qr-bg-image', '/sdcard/images')">
                        <button class="btn btn-sm" onclick="clearQrBgImage()" title="清除背景图">✕</button>
                    </div>
                </div>
            </div>
            
            <!-- 文本滚动 -->
            <div class="matrix-section">
                <label class="section-title">📝 文本显示</label>
                <div class="matrix-controls">
                    <div class="control-row">
                        <input type="text" id="matrix-text-content" placeholder="输入显示文本" class="input-full">
                    </div>
                    <div class="control-row">
                        <select id="matrix-text-font" title="字体" style="flex:2">
                            <option value="default">默认字体</option>
                        </select>
                        <button class="btn btn-sm" onclick="loadFontList()" title="刷新字体列表">🔄</button>
                    </div>
                    <div class="control-row">
                        <select id="matrix-text-align" title="对齐">
                            <option value="left">左对齐</option>
                            <option value="center">居中</option>
                            <option value="right">右对齐</option>
                        </select>
                        <input type="color" id="matrix-text-color" value="#00ff00" title="文字颜色">
                    </div>
                    <div class="control-row">
                        <label>X: <input type="number" id="matrix-text-x" value="0" min="0" max="255" style="width:50px" title="X坐标"></label>
                        <label>Y: <input type="number" id="matrix-text-y" value="0" min="0" max="255" style="width:50px" title="Y坐标"></label>
                        <label><input type="checkbox" id="matrix-text-auto-pos"> 自动定位</label>
                    </div>
                    <div class="control-row">
                        <label>滚动 <select id="matrix-text-scroll">
                            <option value="none">无</option>
                            <option value="left" selected>向左</option>
                            <option value="right">向右</option>
                            <option value="up">向上</option>
                            <option value="down">向下</option>
                        </select></label>
                        <label>速度 <input type="number" id="matrix-text-speed" value="50" min="1" max="100" style="width:50px"></label>
                        <label><input type="checkbox" id="matrix-text-loop" checked> 循环</label>
                    </div>
                    <div class="control-row">
                        <button class="btn btn-sm btn-primary" onclick="displayText()">显示</button>
                        <button class="btn btn-sm btn-danger" onclick="stopText()">停止</button>
                    </div>
                </div>
            </div>
            
            <!-- 后处理滤镜 -->
            <div class="matrix-section">
                <label class="section-title">🎨 后处理滤镜</label>
                <div class="matrix-controls">
                    <!-- 滤镜分类：动态效果 -->
                    <div class="filter-category">
                        <span class="filter-label">动态效果</span>
                        <div class="control-row filters-grid">
                            <button class="btn btn-sm filter-btn" data-filter="pulse" onclick="selectFilter('pulse', this)">💓 脉冲</button>
                            <button class="btn btn-sm filter-btn" data-filter="breathing" onclick="selectFilter('breathing', this)">💨 呼吸</button>
                            <button class="btn btn-sm filter-btn" data-filter="blink" onclick="selectFilter('blink', this)">💡 闪烁</button>
                            <button class="btn btn-sm filter-btn" data-filter="wave" onclick="selectFilter('wave', this)">🌊 波浪</button>
                            <button class="btn btn-sm filter-btn" data-filter="scanline" onclick="selectFilter('scanline', this)">📺 扫描</button>
                            <button class="btn btn-sm filter-btn" data-filter="glitch" onclick="selectFilter('glitch', this)">⚡ 故障</button>
                        </div>
                    </div>
                    <!-- 滤镜分类：渐变效果 -->
                    <div class="filter-category">
                        <span class="filter-label">渐变效果</span>
                        <div class="control-row filters-grid">
                            <button class="btn btn-sm filter-btn" data-filter="fade-in" onclick="selectFilter('fade-in', this)">📈 淡入</button>
                            <button class="btn btn-sm filter-btn" data-filter="fade-out" onclick="selectFilter('fade-out', this)">📉 淡出</button>
                            <button class="btn btn-sm filter-btn" data-filter="color-shift" onclick="selectFilter('color-shift', this)">🌈 色移</button>
                        </div>
                    </div>
                    <!-- 滤镜分类：静态效果 -->
                    <div class="filter-category">
                        <span class="filter-label">静态效果</span>
                        <div class="control-row filters-grid">
                            <button class="btn btn-sm filter-btn" data-filter="invert" onclick="selectFilter('invert', this)">🔄 反色</button>
                            <button class="btn btn-sm filter-btn" data-filter="grayscale" onclick="selectFilter('grayscale', this)">⬜ 灰度</button>
                        </div>
                    </div>
                    <!-- 滤镜参数区域 -->
                    <div id="filter-params" class="filter-params" style="display:none;">
                        <div class="filter-param-row" id="filter-speed-row">
                            <label>速度</label>
                            <input type="range" id="matrix-filter-speed" min="1" max="100" value="50">
                            <span id="filter-speed-value">50</span>
                        </div>
                    </div>
                    <!-- 操作按钮 -->
                    <div class="control-row" style="margin-top:8px;">
                        <span id="selected-filter-name" style="color:#888;">未选择滤镜</span>
                        <button class="btn btn-sm btn-primary" id="apply-filter-btn" onclick="applySelectedFilter()" disabled>应用滤镜</button>
                        <button class="btn btn-sm btn-danger" onclick="stopFilter()">停止滤镜</button>
                    </div>
                </div>
            </div>
        </div>
        
        <!-- 文件选择器模态框 -->
        <div id="file-picker-modal" class="modal hidden">
            <div class="modal-content file-picker-modal">
                <h2>📁 选择文件</h2>
                <div class="file-picker-path">
                    <button class="btn btn-sm" onclick="filePickerGoUp()">⬆️ 上级</button>
                    <span id="file-picker-current-path">/sdcard/images</span>
                </div>
                <div class="file-picker-list" id="file-picker-list">
                    <div class="loading">加载中...</div>
                </div>
                <div class="file-picker-selected">
                    <span>已选择: </span><span id="file-picker-selected-name">-</span>
                </div>
                <div class="form-actions">
                    <button class="btn" onclick="closeFilePicker()">取消</button>
                    <button class="btn btn-primary" id="file-picker-confirm" onclick="confirmFilePicker()" disabled>确定</button>
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
//                     Matrix 专属功能
// =========================================================================

// 文件选择器状态
let filePickerCurrentPath = '/sdcard/images';
let filePickerSelectedFile = null;
let filePickerCallback = null;

// 通用文件选择器 - 为指定输入框打开文件选择器
async function openFilePickerFor(inputId, startPath = '/sdcard/images') {
    filePickerCurrentPath = startPath;
    filePickerSelectedFile = null;
    filePickerCallback = (path) => {
        document.getElementById(inputId).value = path;
    };
    document.getElementById('file-picker-modal').classList.remove('hidden');
    await loadFilePickerDirectory(filePickerCurrentPath);
}

// 浏览图像文件 - 打开文件选择器
async function browseImages() {
    filePickerCurrentPath = '/sdcard/images';
    filePickerSelectedFile = null;
    filePickerCallback = (path) => {
        document.getElementById('matrix-image-path').value = path;
    };
    document.getElementById('file-picker-modal').classList.remove('hidden');
    await loadFilePickerDirectory(filePickerCurrentPath);
}

// 加载文件选择器目录
async function loadFilePickerDirectory(path) {
    filePickerCurrentPath = path;
    document.getElementById('file-picker-current-path').textContent = path;
    const listContainer = document.getElementById('file-picker-list');
    listContainer.innerHTML = '<div class="loading">加载中...</div>';
    
    try {
        const result = await api.storageList(path);
        
        // 检查 API 返回的错误
        if (result.error) {
            // 目录不存在，尝试创建
            if (result.error.includes('not found') || result.error.includes('Directory')) {
                listContainer.innerHTML = `
                    <div class="empty-state">
                        <div>📂 目录不存在</div>
                        <button class="btn btn-sm btn-primary" onclick="createAndOpenDir('${path}')">创建目录</button>
                    </div>`;
                return;
            }
            throw new Error(result.error);
        }
        
        const files = result.data?.entries || [];
        
        // 筛选：只显示目录和图片文件
        const imageExts = ['.png', '.jpg', '.jpeg', '.gif', '.bmp'];
        const filtered = files.filter(f => {
            if (f.type === 'dir' || f.type === 'directory') return true;
            const ext = f.name.toLowerCase().substring(f.name.lastIndexOf('.'));
            return imageExts.includes(ext);
        });
        
        if (filtered.length === 0) {
            listContainer.innerHTML = '<div class="empty-state">📂 无图片文件</div>';
            return;
        }
        
        // 排序：目录在前，文件在后
        filtered.sort((a, b) => {
            const aIsDir = a.type === 'dir' || a.type === 'directory';
            const bIsDir = b.type === 'dir' || b.type === 'directory';
            if (aIsDir && !bIsDir) return -1;
            if (!aIsDir && bIsDir) return 1;
            return a.name.localeCompare(b.name);
        });
        
        listContainer.innerHTML = filtered.map(f => {
            const isDir = f.type === 'dir' || f.type === 'directory';
            const icon = isDir ? '📁' : getFileIcon(f.name);
            const fullPath = path + (path.endsWith('/') ? '' : '/') + f.name;
            return `
                <div class="file-picker-item ${isDir ? 'directory' : 'file'}" 
                     data-path="${fullPath}" data-type="${f.type}"
                     onclick="filePickerItemClick(this, '${fullPath}', ${isDir})"
                     ondblclick="filePickerItemDblClick('${fullPath}', ${isDir})">
                    <span class="icon">${icon}</span>
                    <span class="name">${f.name}</span>
                    ${!isDir ? `<span class="size">${formatFileSize(f.size)}</span>` : ''}
                </div>
            `;
        }).join('');
    } catch (e) {
        listContainer.innerHTML = `<div class="error">加载失败: ${e.message}</div>`;
    }
}

// 创建并打开目录
async function createAndOpenDir(path) {
    try {
        await api.storageMkdir(path);
        await loadFilePickerDirectory(path);
    } catch (e) {
        showToast('创建目录失败: ' + e.message, 'error');
    }
}

// 文件选择器项目单击
function filePickerItemClick(element, path, isDir) {
    // 移除其他选中状态
    document.querySelectorAll('.file-picker-item.selected').forEach(el => el.classList.remove('selected'));
    element.classList.add('selected');
    
    if (!isDir) {
        filePickerSelectedFile = path;
        document.getElementById('file-picker-selected-name').textContent = path.split('/').pop();
        document.getElementById('file-picker-confirm').disabled = false;
    } else {
        filePickerSelectedFile = null;
        document.getElementById('file-picker-selected-name').textContent = '-';
        document.getElementById('file-picker-confirm').disabled = true;
    }
}

// 文件选择器项目双击
async function filePickerItemDblClick(path, isDir) {
    if (isDir) {
        await loadFilePickerDirectory(path);
    } else {
        // 双击文件直接确认
        filePickerSelectedFile = path;
        confirmFilePicker();
    }
}

// 文件选择器上级目录
async function filePickerGoUp() {
    if (filePickerCurrentPath === '/sdcard' || filePickerCurrentPath === '/') {
        return;
    }
    const parentPath = filePickerCurrentPath.substring(0, filePickerCurrentPath.lastIndexOf('/')) || '/sdcard';
    await loadFilePickerDirectory(parentPath);
}

// 关闭文件选择器
function closeFilePicker() {
    document.getElementById('file-picker-modal').classList.add('hidden');
    filePickerSelectedFile = null;
    filePickerCallback = null;
}

// 确认文件选择
function confirmFilePicker() {
    if (filePickerSelectedFile && filePickerCallback) {
        filePickerCallback(filePickerSelectedFile);
    }
    closeFilePicker();
}

// 显示图像
async function displayImage() {
    const pathInput = document.getElementById('matrix-image-path');
    const centerCheckbox = document.getElementById('matrix-image-center');
    
    const path = pathInput.value.trim();
    if (!path) {
        showToast('请输入图像路径', 'error');
        return;
    }
    
    try {
        const result = await api.ledImage(path, 'matrix', centerCheckbox.checked);
        showToast(`图像显示成功`, 'success');
    } catch (e) {
        showToast(`显示图像失败: ${e.message}`, 'error');
    }
}

// 生成 QR 码
async function generateQrCode() {
    const textInput = document.getElementById('matrix-qr-text');
    const eccSelect = document.getElementById('matrix-qr-ecc');
    const fgColor = document.getElementById('matrix-qr-fg');
    const bgImageInput = document.getElementById('matrix-qr-bg-image');
    
    const text = textInput.value.trim();
    if (!text) {
        showToast('请输入 QR 码内容', 'error');
        return;
    }
    
    const params = {
        ecc: eccSelect.value,
        color: fgColor.value
    };
    
    // 添加背景图（如果有）
    const bgImage = bgImageInput.value.trim();
    if (bgImage) {
        params.bg_image = bgImage;
    }
    
    try {
        const result = await api.ledQrcode(text, params);
        showToast(`QR 码生成成功`, 'success');
    } catch (e) {
        showToast(`生成 QR 码失败: ${e.message}`, 'error');
    }
}

// 清除 QR 码背景图
function clearQrBgImage() {
    document.getElementById('matrix-qr-bg-image').value = '';
}

// 加载字体列表
async function loadFontList() {
    const fontSelect = document.getElementById('matrix-text-font');
    if (!fontSelect) return;
    
    // 保存当前选中的字体
    const currentFont = fontSelect.value;
    
    try {
        const result = await api.storageList('/sdcard/fonts');
        const files = result.data?.entries || [];
        
        // 筛选字体文件 (.fnt, .bdf, .pcf)
        const fontExts = ['.fnt', '.bdf', '.pcf'];
        const fonts = files.filter(f => {
            if (f.type === 'dir' || f.type === 'directory') return false;
            const ext = f.name.toLowerCase().substring(f.name.lastIndexOf('.'));
            return fontExts.includes(ext);
        });
        
        // 清空选项
        fontSelect.innerHTML = '';
        
        if (fonts.length === 0) {
            // 没有字体时添加占位选项
            fontSelect.innerHTML = '<option value="" disabled>无可用字体</option>';
            showToast('未找到字体文件，请上传到 /sdcard/fonts', 'info');
        } else {
            fonts.forEach(f => {
                const option = document.createElement('option');
                // 使用文件名（不含扩展名）作为值和显示名
                // 后端会自动添加路径前缀和扩展名
                const fontName = f.name.substring(0, f.name.lastIndexOf('.'));
                option.value = fontName;
                option.textContent = fontName;
                fontSelect.appendChild(option);
            });
            
            // 恢复之前选中的字体
            if (currentFont && fontSelect.querySelector(`option[value="${currentFont}"]`)) {
                fontSelect.value = currentFont;
            }
        }
    } catch (e) {
        console.log('加载字体列表失败:', e);
        // 目录不存在时不报错，保持默认选项
    }
}

// 显示文本
async function displayText() {
    const textInput = document.getElementById('matrix-text-content');
    const fontSelect = document.getElementById('matrix-text-font');
    const alignSelect = document.getElementById('matrix-text-align');
    const colorInput = document.getElementById('matrix-text-color');
    const xInput = document.getElementById('matrix-text-x');
    const yInput = document.getElementById('matrix-text-y');
    const autoPos = document.getElementById('matrix-text-auto-pos');
    const scrollSelect = document.getElementById('matrix-text-scroll');
    const speedInput = document.getElementById('matrix-text-speed');
    const loopCheckbox = document.getElementById('matrix-text-loop');
    
    const text = textInput.value.trim();
    if (!text) {
        showToast('请输入显示文本', 'error');
        return;
    }
    
    const params = {
        device: 'matrix',
        font: fontSelect.value,
        align: alignSelect.value,
        color: colorInput.value,
        scroll: scrollSelect.value,  // 滚动方向：none/left/right/up/down
        speed: parseInt(speedInput.value),
        loop: loopCheckbox.checked
    };
    
    // 添加坐标（如果不是自动定位）
    if (!autoPos.checked) {
        params.x = parseInt(xInput.value) || 0;
        params.y = parseInt(yInput.value) || 0;
    }
    
    try {
        const result = await api.ledText(text, params);
        showToast(`文本显示成功`, 'success');
    } catch (e) {
        showToast(`显示文本失败: ${e.message}`, 'error');
    }
}

// 停止文本
async function stopText() {
    try {
        await api.ledTextStop('matrix');
        showToast('文本已停止', 'success');
    } catch (e) {
        showToast(`停止失败: ${e.message}`, 'error');
    }
}

// 滤镜配置：哪些滤镜需要速度参数
const filterConfig = {
    // 动态效果 - 需要速度
    'pulse': { needsSpeed: true, defaultSpeed: 50 },
    'breathing': { needsSpeed: true, defaultSpeed: 30 },
    'blink': { needsSpeed: true, defaultSpeed: 50 },
    'wave': { needsSpeed: true, defaultSpeed: 40 },
    'scanline': { needsSpeed: true, defaultSpeed: 60 },
    'glitch': { needsSpeed: true, defaultSpeed: 70 },
    // 渐变效果 - 需要速度
    'fade-in': { needsSpeed: true, defaultSpeed: 30 },
    'fade-out': { needsSpeed: true, defaultSpeed: 30 },
    'color-shift': { needsSpeed: true, defaultSpeed: 20 },
    // 静态效果 - 不需要速度
    'invert': { needsSpeed: false },
    'grayscale': { needsSpeed: false }
};

let selectedFilter = null;

// 选择滤镜
function selectFilter(filterName, btnElement) {
    selectedFilter = filterName;
    
    // 高亮当前选中的按钮
    document.querySelectorAll('.filter-btn').forEach(btn => btn.classList.remove('selected'));
    if (btnElement) btnElement.classList.add('selected');
    
    // 更新显示的滤镜名称
    const nameSpan = document.getElementById('selected-filter-name');
    if (nameSpan) nameSpan.textContent = `已选择: ${filterName}`;
    
    // 启用应用按钮
    const applyBtn = document.getElementById('apply-filter-btn');
    if (applyBtn) applyBtn.disabled = false;
    
    // 根据滤镜类型显示/隐藏参数
    const paramsDiv = document.getElementById('filter-params');
    const speedRow = document.getElementById('filter-speed-row');
    const config = filterConfig[filterName];
    
    if (config && config.needsSpeed) {
        paramsDiv.style.display = 'block';
        speedRow.style.display = 'flex';
        // 设置默认速度
        const speedSlider = document.getElementById('matrix-filter-speed');
        if (speedSlider) {
            speedSlider.value = config.defaultSpeed;
            document.getElementById('filter-speed-value').textContent = config.defaultSpeed;
        }
    } else {
        paramsDiv.style.display = 'none';
    }
}

// 应用选中的滤镜
async function applySelectedFilter() {
    if (!selectedFilter) {
        showToast('请先选择滤镜', 'error');
        return;
    }
    
    const config = filterConfig[selectedFilter];
    let speed = 50;
    
    if (config && config.needsSpeed) {
        const speedSlider = document.getElementById('matrix-filter-speed');
        speed = parseInt(speedSlider.value) || config.defaultSpeed;
    }
    
    try {
        await api.ledFilterStart('matrix', selectedFilter, speed);
        showToast(`滤镜 ${selectedFilter} 已应用`, 'success');
        
        // 更新 active 状态
        document.querySelectorAll('.filter-btn').forEach(btn => {
            btn.classList.remove('active');
            if (btn.dataset.filter === selectedFilter) {
                btn.classList.add('active');
            }
        });
    } catch (e) {
        showToast(`应用滤镜失败: ${e.message}`, 'error');
    }
}

// 应用滤镜（兼容旧接口）
async function applyFilter(filterName, btnElement) {
    selectFilter(filterName, btnElement);
    await applySelectedFilter();
}

// 停止滤镜
async function stopFilter() {
    try {
        await api.ledFilterStop('matrix');
        showToast('滤镜已停止', 'success');
        
        // 移除滤镜按钮高亮和选中状态
        document.querySelectorAll('.filter-btn').forEach(btn => {
            btn.classList.remove('active');
            btn.classList.remove('selected');
        });
        selectedFilter = null;
        
        // 重置 UI
        const nameSpan = document.getElementById('selected-filter-name');
        if (nameSpan) nameSpan.textContent = '未选择滤镜';
        const applyBtn = document.getElementById('apply-filter-btn');
        if (applyBtn) applyBtn.disabled = true;
        const paramsDiv = document.getElementById('filter-params');
        if (paramsDiv) paramsDiv.style.display = 'none';
    } catch (e) {
        showToast(`停止滤镜失败: ${e.message}`, 'error');
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
                <!-- 以太网 -->
                <div class="card">
                    <h3>🔌 以太网 (W5500)</h3>
                    <div class="card-content" id="eth-info">
                        <p><strong>状态:</strong> <span id="net-eth-status" class="status-badge">-</span></p>
                        <p><strong>链路:</strong> <span id="net-eth-link">-</span></p>
                        <p><strong>IP:</strong> <span id="net-eth-ip">-</span></p>
                        <p><strong>子网:</strong> <span id="net-eth-netmask">-</span></p>
                        <p><strong>网关:</strong> <span id="net-eth-gw">-</span></p>
                        <p><strong>DNS:</strong> <span id="net-eth-dns">-</span></p>
                        <p><strong>MAC:</strong> <span id="net-eth-mac">-</span></p>
                    </div>
                </div>
                
                <!-- WiFi STA -->
                <div class="card">
                    <h3>📶 WiFi 站点</h3>
                    <div class="card-content" id="wifi-sta-info">
                        <p><strong>状态:</strong> <span id="net-wifi-sta-status" class="status-badge">-</span></p>
                        <p><strong>已连接:</strong> <span id="net-wifi-sta-connected">-</span></p>
                        <p><strong>IP:</strong> <span id="net-wifi-sta-ip">-</span></p>
                        <p><strong>信号:</strong> <span id="net-wifi-sta-rssi">-</span></p>
                        <p><strong>MAC:</strong> <span id="net-wifi-sta-mac">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn" id="wifi-scan-btn" onclick="showWifiScan()" disabled title="需要 STA 或 APSTA 模式">📡 扫描网络</button>
                        <button class="btn btn-danger hidden" id="wifi-disconnect-btn" onclick="disconnectWifi()">断开连接</button>
                    </div>
                </div>
                
                <!-- WiFi AP -->
                <div class="card">
                    <h3>📻 WiFi 热点</h3>
                    <div class="card-content" id="wifi-ap-info">
                        <p><strong>状态:</strong> <span id="net-wifi-ap-status" class="status-badge">-</span></p>
                        <p><strong>接入数:</strong> <span id="net-wifi-ap-sta-count">-</span></p>
                        <p><strong>IP:</strong> <span id="net-wifi-ap-ip">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn" id="ap-config-btn" onclick="showApConfig()" disabled title="需要 AP 或 APSTA 模式">⚙️ 配置热点</button>
                        <button class="btn" id="ap-stations-btn" onclick="showApStations()" disabled title="需要 AP 或 APSTA 模式">👥 查看接入设备</button>
                    </div>
                </div>
                
                <!-- 主机名 -->
                <div class="card">
                    <h3>🏷️ 主机名</h3>
                    <div class="card-content">
                        <p><strong>当前:</strong> <span id="net-hostname">-</span></p>
                        <div class="form-group" style="margin-top:10px">
                            <input type="text" id="hostname-input" placeholder="输入新主机名">
                            <button class="btn btn-small" onclick="setHostname()">设置</button>
                        </div>
                    </div>
                </div>
            </div>
            
            <div class="cards" style="margin-top:20px">
                <!-- DHCP 服务器 -->
                <div class="card">
                    <h3>🔀 DHCP 服务器</h3>
                    <div class="card-content" id="dhcp-info">
                        <div id="dhcp-interfaces-list"></div>
                    </div>
                    <div class="button-group">
                        <button class="btn" onclick="showDhcpClients()">👥 查看客户端</button>
                    </div>
                </div>
                
                <!-- NAT 网关 -->
                <div class="card">
                    <h3>🌍 NAT 网关</h3>
                    <div class="card-content" id="nat-info">
                        <p><strong>状态:</strong> <span id="net-nat-status" class="status-badge">-</span></p>
                        <p><strong>WiFi 连接:</strong> <span id="net-nat-wifi">-</span></p>
                        <p><strong>以太网:</strong> <span id="net-nat-eth">-</span></p>
                        <p class="hint" style="font-size:0.85rem;color:#888;margin-top:8px">
                            NAT 将 WiFi 网络共享给以太网设备
                        </p>
                    </div>
                    <div class="button-group">
                        <button class="btn" id="nat-toggle-btn" onclick="toggleNat()">启用</button>
                        <button class="btn" onclick="saveNatConfig()">💾 保存配置</button>
                    </div>
                </div>
                
                <!-- WiFi 模式 -->
                <div class="card">
                    <h3>📻 WiFi 模式</h3>
                    <div class="card-content">
                        <p><strong>当前模式:</strong> <span id="net-wifi-mode">-</span></p>
                        <div class="form-group" style="margin-top:10px">
                            <select id="wifi-mode-select">
                                <option value="off">关闭</option>
                                <option value="sta">仅站点 (STA)</option>
                                <option value="ap">仅热点 (AP)</option>
                                <option value="apsta">站点+热点 (AP+STA)</option>
                            </select>
                            <button class="btn btn-small" onclick="setWifiMode()">切换</button>
                        </div>
                    </div>
                </div>
            </div>
            
            <!-- WiFi 扫描结果 -->
            <div class="section hidden" id="wifi-scan-section">
                <h2>📡 WiFi 网络列表</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>SSID</th><th>信号</th><th>信道</th><th>加密</th><th>BSSID</th><th>操作</th></tr>
                    </thead>
                    <tbody id="wifi-scan-results"></tbody>
                </table>
                <div class="button-group" style="margin-top:10px">
                    <button class="btn" onclick="hideWifiScan()">关闭</button>
                    <button class="btn" onclick="showWifiScan()">🔄 重新扫描</button>
                </div>
            </div>
            
            <!-- AP 接入设备 -->
            <div class="section hidden" id="ap-stations-section">
                <h2>👥 热点接入设备</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>MAC 地址</th><th>信号强度</th></tr>
                    </thead>
                    <tbody id="ap-stations-results"></tbody>
                </table>
                <div class="button-group" style="margin-top:10px">
                    <button class="btn" onclick="hideApStations()">关闭</button>
                </div>
            </div>
            
            <!-- DHCP 客户端 -->
            <div class="section hidden" id="dhcp-clients-section">
                <h2>👥 DHCP 客户端</h2>
                <div class="form-group">
                    <select id="dhcp-iface-select" onchange="loadDhcpClients()">
                        <option value="ap">WiFi AP</option>
                        <option value="eth">Ethernet</option>
                    </select>
                </div>
                <table class="data-table">
                    <thead>
                        <tr><th>IP</th><th>MAC</th><th>主机名</th><th>类型</th></tr>
                    </thead>
                    <tbody id="dhcp-clients-results"></tbody>
                </table>
                <div class="button-group" style="margin-top:10px">
                    <button class="btn" onclick="hideDhcpClients()">关闭</button>
                    <button class="btn" onclick="loadDhcpClients()">🔄 刷新</button>
                </div>
            </div>
            
            <!-- AP 配置弹窗 -->
            <div class="modal hidden" id="ap-config-modal">
                <div class="modal-content">
                    <h2>⚙️ 配置 WiFi 热点</h2>
                    <div class="form-group">
                        <label>SSID (热点名称)</label>
                        <input type="text" id="ap-ssid-input" placeholder="TianShanOS">
                    </div>
                    <div class="form-group">
                        <label>密码 (留空为开放网络)</label>
                        <input type="password" id="ap-password-input" placeholder="至少 8 位">
                    </div>
                    <div class="form-group">
                        <label>信道</label>
                        <select id="ap-channel-input">
                            <option value="1">1</option>
                            <option value="6" selected>6</option>
                            <option value="11">11</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label><input type="checkbox" id="ap-hidden-input"> 隐藏 SSID</label>
                    </div>
                    <div class="form-actions">
                        <button class="btn" onclick="hideApConfig()">取消</button>
                        <button class="btn btn-primary" onclick="applyApConfig()">应用</button>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    await refreshNetworkPage();
}

async function refreshNetworkPage() {
    // 综合网络状态
    try {
        const status = await api.networkStatus();
        if (status.data) {
            const data = status.data;
            
            // 主机名
            document.getElementById('net-hostname').textContent = data.hostname || '-';
            
            // 以太网
            const eth = data.ethernet || {};
            updateStatusBadge('net-eth-status', eth.status, eth.status === 'connected');
            document.getElementById('net-eth-link').textContent = eth.link_up ? '已连接' : '未连接';
            document.getElementById('net-eth-ip').textContent = eth.ip || '-';
            document.getElementById('net-eth-netmask').textContent = eth.netmask || '-';
            document.getElementById('net-eth-gw').textContent = eth.gateway || '-';
            document.getElementById('net-eth-dns').textContent = eth.dns1 || '-';
            document.getElementById('net-eth-mac').textContent = eth.mac || '-';
            
            // WiFi STA
            const wifiSta = data.wifi_sta || {};
            updateStatusBadge('net-wifi-sta-status', wifiSta.status, wifiSta.status === 'connected');
            document.getElementById('net-wifi-sta-connected').textContent = wifiSta.connected ? '是' : '否';
            document.getElementById('net-wifi-sta-ip').textContent = wifiSta.ip || '-';
            document.getElementById('net-wifi-sta-rssi').textContent = wifiSta.rssi ? `${wifiSta.rssi} dBm` : '-';
            document.getElementById('net-wifi-sta-mac').textContent = wifiSta.mac || '-';
            
            // 根据连接状态显示/隐藏断开按钮
            const disconnectBtn = document.getElementById('wifi-disconnect-btn');
            if (wifiSta.connected) {
                disconnectBtn.classList.remove('hidden');
            } else {
                disconnectBtn.classList.add('hidden');
            }
            
            // WiFi AP
            const wifiAp = data.wifi_ap || {};
            updateStatusBadge('net-wifi-ap-status', wifiAp.status, wifiAp.status === 'connected');
            document.getElementById('net-wifi-ap-sta-count').textContent = 
                (wifiAp.sta_count !== undefined ? wifiAp.sta_count : 0) + ' 台设备';
            document.getElementById('net-wifi-ap-ip').textContent = wifiAp.ip || '-';
        }
    } catch (e) { console.log('Network status error:', e); }
    
    // WiFi 模式
    let currentWifiMode = 'off';
    try {
        const mode = await api.wifiMode();
        if (mode.data) {
            currentWifiMode = mode.data.mode || 'off';
            document.getElementById('net-wifi-mode').textContent = getWifiModeDisplay(currentWifiMode);
            document.getElementById('wifi-mode-select').value = currentWifiMode;
            
            // 根据 WiFi 模式启用/禁用扫描按钮（需要 STA 或 APSTA 模式）
            const scanBtn = document.getElementById('wifi-scan-btn');
            const canScan = (currentWifiMode === 'sta' || currentWifiMode === 'apsta');
            scanBtn.disabled = !canScan;
            scanBtn.title = canScan ? '扫描周围 WiFi 网络' : '需要先切换到 STA 或 APSTA 模式';
            
            // 根据 WiFi 模式启用/禁用 AP 按钮（需要 AP 或 APSTA 模式）
            const apConfigBtn = document.getElementById('ap-config-btn');
            const apStationsBtn = document.getElementById('ap-stations-btn');
            const canAp = (currentWifiMode === 'ap' || currentWifiMode === 'apsta');
            apConfigBtn.disabled = !canAp;
            apStationsBtn.disabled = !canAp;
            apConfigBtn.title = canAp ? '配置 WiFi 热点参数' : '需要先切换到 AP 或 APSTA 模式';
            apStationsBtn.title = canAp ? '查看已连接的设备' : '需要先切换到 AP 或 APSTA 模式';
        }
    } catch (e) { console.log('WiFi mode error:', e); }
    
    // DHCP 状态
    try {
        const dhcp = await api.dhcpStatus();
        if (dhcp.data) {
            const container = document.getElementById('dhcp-interfaces-list');
            if (dhcp.data.interfaces) {
                container.innerHTML = dhcp.data.interfaces.map(iface => `
                    <div style="margin-bottom:8px;padding:8px;background:var(--bg-color);border-radius:4px;">
                        <strong>${iface.display_name}</strong><br>
                        状态: <span class="status-badge ${iface.running ? 'status-ok' : 'status-warn'}">${iface.running ? '运行中' : '已停止'}</span><br>
                        活跃租约: ${iface.active_leases || 0}<br>
                        地址池: ${iface.pool_start} - ${iface.pool_end}
                    </div>
                `).join('');
            } else {
                // 单接口响应
                container.innerHTML = `
                    <p><strong>接口:</strong> ${dhcp.data.display_name || dhcp.data.interface}</p>
                    <p><strong>状态:</strong> <span class="status-badge ${dhcp.data.running ? 'status-ok' : 'status-warn'}">${dhcp.data.running ? '运行中' : '已停止'}</span></p>
                    <p><strong>活跃租约:</strong> ${dhcp.data.active_leases || 0}</p>
                    <p><strong>地址池:</strong> ${dhcp.data.pool?.start || '-'} - ${dhcp.data.pool?.end || '-'}</p>
                `;
            }
        }
    } catch (e) { console.log('DHCP error:', e); }
    
    // NAT 状态
    try {
        const nat = await api.natStatus();
        if (nat.data) {
            const enabled = nat.data.enabled;
            const wifiConnected = nat.data.wifi_connected;
            const ethUp = nat.data.eth_up;
            
            updateStatusBadge('net-nat-status', nat.data.state, enabled);
            document.getElementById('net-nat-wifi').textContent = wifiConnected ? '已连接 ✓' : '未连接 ✗';
            document.getElementById('net-nat-eth').textContent = ethUp ? '链路正常 ✓' : '链路断开 ✗';
            
            // NAT 启用/禁用按钮
            const natToggleBtn = document.getElementById('nat-toggle-btn');
            natToggleBtn.textContent = enabled ? '禁用' : '启用';
            natToggleBtn.className = enabled ? 'btn btn-danger' : 'btn btn-success';
            
            // 只有在 WiFi 已连接且以太网链路正常时才能启用 NAT
            // 如果 NAT 已启用，则始终允许禁用
            const canToggle = enabled || (wifiConnected && ethUp);
            natToggleBtn.disabled = !canToggle;
            if (!canToggle) {
                natToggleBtn.title = '需要 WiFi STA 已连接且以太网链路正常';
            } else {
                natToggleBtn.title = enabled ? '停止 NAT 网关' : '启动 NAT 网关';
            }
        }
    } catch (e) { console.log('NAT error:', e); }
}

// 更新状态徽章样式
function updateStatusBadge(elementId, text, isOk) {
    const el = document.getElementById(elementId);
    if (el) {
        el.textContent = text || '-';
        el.className = 'status-badge ' + (isOk ? 'status-ok' : 'status-warn');
    }
}

// WiFi 模式显示名
function getWifiModeDisplay(mode) {
    const modes = {
        'off': '关闭',
        'sta': '站点 (STA)',
        'ap': '热点 (AP)',
        'apsta': '站点+热点'
    };
    return modes[mode] || mode || '-';
}

// 设置 WiFi 模式
async function setWifiMode() {
    const mode = document.getElementById('wifi-mode-select').value;
    try {
        await api.wifiMode(mode);
        showToast(`WiFi 模式已切换为 ${getWifiModeDisplay(mode)}`, 'success');
        await refreshNetworkPage();
    } catch (e) {
        showToast('切换失败: ' + e.message, 'error');
    }
}

// 设置主机名
async function setHostname() {
    const name = document.getElementById('hostname-input').value.trim();
    if (!name) {
        showToast('请输入主机名', 'error');
        return;
    }
    try {
        await api.hostname(name);
        showToast('主机名已设置', 'success');
        document.getElementById('hostname-input').value = '';
        await refreshNetworkPage();
    } catch (e) {
        showToast('设置失败: ' + e.message, 'error');
    }
}

async function showWifiScan() {
    const section = document.getElementById('wifi-scan-section');
    const tbody = document.getElementById('wifi-scan-results');
    
    section.classList.remove('hidden');
    tbody.innerHTML = '<tr><td colspan="6">扫描中...</td></tr>';
    
    try {
        const result = await api.wifiScan();
        if (result.data && result.data.networks) {
            if (result.data.networks.length === 0) {
                tbody.innerHTML = '<tr><td colspan="6">未发现网络</td></tr>';
                return;
            }
            tbody.innerHTML = result.data.networks.map(net => `
                <tr>
                    <td>${escapeHtml(net.ssid) || '(隐藏)'}</td>
                    <td>${getSignalIcon(net.rssi)} ${net.rssi} dBm</td>
                    <td>${net.channel}</td>
                    <td>${net.auth || 'OPEN'}</td>
                    <td style="font-family:monospace;font-size:0.85rem">${net.bssid || '-'}</td>
                    <td><button class="btn btn-small btn-primary" onclick="connectWifi('${escapeHtml(net.ssid)}')">连接</button></td>
                </tr>
            `).join('');
        }
    } catch (e) {
        // 检查是否是模式错误
        const errorMsg = e.message || '';
        if (errorMsg.includes('STA') || errorMsg.includes('APSTA') || errorMsg.includes('mode')) {
            tbody.innerHTML = `
                <tr><td colspan="6" style="text-align:center;padding:20px">
                    <div style="color:#e74c3c;margin-bottom:10px">⚠️ WiFi 扫描需要 STA 或 APSTA 模式</div>
                    <div style="color:#666;font-size:0.9rem">请在下方"WiFi 模式"卡片中切换到"站点 (STA)"或"站点+热点"模式</div>
                </td></tr>`;
        } else {
            tbody.innerHTML = `<tr><td colspan="6" style="color:red">扫描失败: ${errorMsg}</td></tr>`;
        }
    }
}

function hideWifiScan() {
    document.getElementById('wifi-scan-section').classList.add('hidden');
}

function getSignalIcon(rssi) {
    if (rssi >= -50) return '📶';
    if (rssi >= -60) return '📶';
    if (rssi >= -70) return '📶';
    return '📶';
}

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/[&<>"']/g, m => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
}

function connectWifi(ssid) {
    const password = prompt(`输入 "${ssid}" 的密码 (开放网络留空):`);
    if (password !== null) {
        api.wifiConnect(ssid, password)
            .then(() => {
                showToast('正在连接...', 'info');
                setTimeout(refreshNetworkPage, 3000);
            })
            .catch(e => showToast('连接失败: ' + e.message, 'error'));
    }
}

async function disconnectWifi() {
    try {
        await api.wifiDisconnect();
        showToast('已断开 WiFi 连接', 'success');
        await refreshNetworkPage();
    } catch (e) {
        showToast('断开失败: ' + e.message, 'error');
    }
}

// AP 接入设备
async function showApStations() {
    const section = document.getElementById('ap-stations-section');
    const tbody = document.getElementById('ap-stations-results');
    
    section.classList.remove('hidden');
    tbody.innerHTML = '<tr><td colspan="2">加载中...</td></tr>';
    
    try {
        const result = await api.wifiApStations();
        if (result.data && result.data.stations) {
            if (result.data.stations.length === 0) {
                tbody.innerHTML = '<tr><td colspan="2">无接入设备</td></tr>';
                return;
            }
            tbody.innerHTML = result.data.stations.map(sta => `
                <tr>
                    <td style="font-family:monospace">${sta.mac}</td>
                    <td>${sta.rssi} dBm</td>
                </tr>
            `).join('');
        }
    } catch (e) {
        tbody.innerHTML = `<tr><td colspan="2" style="color:red">获取失败: ${e.message}</td></tr>`;
    }
}

function hideApStations() {
    document.getElementById('ap-stations-section').classList.add('hidden');
}

// AP 配置
function showApConfig() {
    document.getElementById('ap-config-modal').classList.remove('hidden');
}

function hideApConfig() {
    document.getElementById('ap-config-modal').classList.add('hidden');
}

async function applyApConfig() {
    const ssid = document.getElementById('ap-ssid-input').value.trim();
    const password = document.getElementById('ap-password-input').value;
    const channel = parseInt(document.getElementById('ap-channel-input').value);
    const hidden = document.getElementById('ap-hidden-input').checked;
    
    if (!ssid) {
        showToast('请输入 SSID', 'error');
        return;
    }
    
    if (password && password.length < 8) {
        showToast('密码至少 8 位', 'error');
        return;
    }
    
    try {
        await api.wifiApConfig(ssid, password, channel, hidden);
        showToast('热点配置已应用', 'success');
        hideApConfig();
        await refreshNetworkPage();
    } catch (e) {
        showToast('配置失败: ' + e.message, 'error');
    }
}

// DHCP 客户端
function showDhcpClients() {
    document.getElementById('dhcp-clients-section').classList.remove('hidden');
    loadDhcpClients();
}

function hideDhcpClients() {
    document.getElementById('dhcp-clients-section').classList.add('hidden');
}

async function loadDhcpClients() {
    const iface = document.getElementById('dhcp-iface-select').value;
    const tbody = document.getElementById('dhcp-clients-results');
    
    tbody.innerHTML = '<tr><td colspan="4">加载中...</td></tr>';
    
    try {
        const result = await api.dhcpClients(iface);
        if (result.data && result.data.clients) {
            if (result.data.clients.length === 0) {
                tbody.innerHTML = '<tr><td colspan="4">无客户端</td></tr>';
                return;
            }
            tbody.innerHTML = result.data.clients.map(client => `
                <tr>
                    <td>${client.ip}</td>
                    <td style="font-family:monospace;font-size:0.85rem">${client.mac}</td>
                    <td>${client.hostname || '-'}</td>
                    <td>${client.is_static ? '静态' : '动态'}</td>
                </tr>
            `).join('');
        }
    } catch (e) {
        tbody.innerHTML = `<tr><td colspan="4" style="color:red">获取失败: ${e.message}</td></tr>`;
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
    } catch (e) { 
        showToast('操作失败: ' + e.message, 'error'); 
    }
}

async function saveNatConfig() {
    try {
        await api.natSave();
        showToast('NAT 配置已保存', 'success');
    } catch (e) {
        showToast('保存失败: ' + e.message, 'error');
    }
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
            
            <!-- 批量操作工具栏 -->
            <div class="batch-toolbar hidden" id="batch-toolbar">
                <span id="selected-count">已选择 0 项</span>
                <button class="btn btn-sm" onclick="batchDownload()">📥 批量下载</button>
                <button class="btn btn-sm btn-danger" onclick="batchDelete()">🗑️ 批量删除</button>
                <button class="btn btn-sm" onclick="clearSelection()">✖️ 取消选择</button>
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
    
    // 初始化选择状态
    selectedFiles.clear();
    
    await refreshFilesPage();
}

// 批量选择相关
const selectedFiles = new Set();

function updateSelectionUI() {
    const toolbar = document.getElementById('batch-toolbar');
    const countSpan = document.getElementById('selected-count');
    
    if (selectedFiles.size > 0) {
        toolbar.classList.remove('hidden');
        countSpan.textContent = `已选择 ${selectedFiles.size} 项`;
    } else {
        toolbar.classList.add('hidden');
    }
    
    // 更新全选复选框状态
    const selectAllCb = document.getElementById('select-all-cb');
    const allCheckboxes = document.querySelectorAll('.file-checkbox');
    if (selectAllCb && allCheckboxes.length > 0) {
        const checkedCount = document.querySelectorAll('.file-checkbox:checked').length;
        selectAllCb.checked = checkedCount === allCheckboxes.length;
        selectAllCb.indeterminate = checkedCount > 0 && checkedCount < allCheckboxes.length;
    }
}

function toggleFileSelection(path, checkbox) {
    if (checkbox.checked) {
        selectedFiles.add(path);
    } else {
        selectedFiles.delete(path);
    }
    updateSelectionUI();
}

function toggleSelectAll(selectAllCb) {
    const checkboxes = document.querySelectorAll('.file-checkbox');
    checkboxes.forEach(cb => {
        cb.checked = selectAllCb.checked;
        const path = cb.dataset.path;
        if (selectAllCb.checked) {
            selectedFiles.add(path);
        } else {
            selectedFiles.delete(path);
        }
    });
    updateSelectionUI();
}

function clearSelection() {
    selectedFiles.clear();
    document.querySelectorAll('.file-checkbox').forEach(cb => cb.checked = false);
    const selectAllCb = document.getElementById('select-all-cb');
    if (selectAllCb) selectAllCb.checked = false;
    updateSelectionUI();
}

async function batchDelete() {
    if (selectedFiles.size === 0) {
        showToast('请先选择要删除的文件', 'warning');
        return;
    }
    
    const count = selectedFiles.size;
    if (!confirm(`确定要删除选中的 ${count} 个文件/文件夹吗？此操作不可撤销！`)) {
        return;
    }
    
    showToast(`正在删除 ${count} 个项目...`, 'info');
    
    let successCount = 0;
    let failCount = 0;
    
    for (const path of selectedFiles) {
        try {
            await api.storageDelete(path);
            successCount++;
        } catch (e) {
            console.error('Delete failed:', path, e);
            failCount++;
        }
    }
    
    selectedFiles.clear();
    
    if (failCount === 0) {
        showToast(`成功删除 ${successCount} 个项目`, 'success');
    } else {
        showToast(`删除完成: ${successCount} 成功, ${failCount} 失败`, 'warning');
    }
    
    await refreshFilesPage();
}

async function batchDownload() {
    if (selectedFiles.size === 0) {
        showToast('请先选择要下载的文件', 'warning');
        return;
    }
    
    // 过滤出文件（排除文件夹）
    const filesToDownload = [];
    for (const path of selectedFiles) {
        const row = document.querySelector(`.file-row[data-path="${CSS.escape(path)}"]`);
        if (row && row.dataset.type !== 'dir') {
            filesToDownload.push(path);
        }
    }
    
    if (filesToDownload.length === 0) {
        showToast('选中的项目中没有可下载的文件（文件夹不支持下载）', 'warning');
        return;
    }
    
    showToast(`正在下载 ${filesToDownload.length} 个文件...`, 'info');
    
    // 逐个下载文件
    for (const path of filesToDownload) {
        try {
            await downloadFile(path);
            // 添加短暂延迟，避免浏览器阻止多个下载
            await new Promise(resolve => setTimeout(resolve, 300));
        } catch (e) {
            console.error('Download failed:', path, e);
        }
    }
    
    showToast('批量下载完成', 'success');
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
                        <th style="width:30px"><input type="checkbox" id="select-all-cb" onchange="toggleSelectAll(this)" title="全选"></th>
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
                        const isSelected = selectedFiles.has(fullPath);
                        return `
                            <tr class="file-row" data-path="${escapedPath}" data-type="${entry.type}" data-name="${escapedName}">
                                <td class="file-checkbox-cell">
                                    <input type="checkbox" class="file-checkbox" data-path="${escapedPath}" 
                                           ${isSelected ? 'checked' : ''} 
                                           onchange="toggleFileSelection('${escapedPath}', this)">
                                </td>
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
                    <button class="btn" onclick="filterConfigList()">筛选</button>
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

async function filterConfigList() {
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
                        <div class="form-group" style="width:80px">
                            <label>端口</label>
                            <input type="number" id="ssh-port" value="22">
                        </div>
                        <div class="form-group">
                            <label>用户名</label>
                            <input type="text" id="ssh-user" required placeholder="root">
                        </div>
                    </div>
                    <div class="form-group">
                        <label>认证方式</label>
                        <select id="ssh-auth-type" onchange="toggleSshAuthType()">
                            <option value="password">密码</option>
                            <option value="keyid">密钥 (安全存储)</option>
                        </select>
                    </div>
                    <div class="form-group" id="ssh-password-group">
                        <label>密码</label>
                        <input type="password" id="ssh-password" placeholder="输入 SSH 密码">
                    </div>
                    <div class="form-group hidden" id="ssh-keyid-group">
                        <label>密钥</label>
                        <select id="ssh-keyid">
                            <option value="">-- 选择密钥 --</option>
                        </select>
                    </div>
                    <button type="submit" class="btn btn-primary">测试连接</button>
                </form>
                <div id="ssh-result" class="result-box hidden"></div>
            </div>
            
            <div class="section">
                <h2>🔐 密钥管理</h2>
                <div class="button-group" style="margin-bottom:15px">
                    <button class="btn btn-primary" onclick="showGenerateKeyModal()">➕ 生成新密钥</button>
                </div>
                <table class="data-table">
                    <thead>
                        <tr><th>ID</th><th>类型</th><th>备注</th><th>创建时间</th><th>可导出</th><th>操作</th></tr>
                    </thead>
                    <tbody id="keys-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>📡 已知主机</h2>
                <div class="button-group" style="margin-bottom:15px">
                    <button class="btn btn-danger" onclick="clearAllHosts()">🗑️ 清除所有</button>
                </div>
                <table class="data-table">
                    <thead>
                        <tr><th>主机</th><th>端口</th><th>密钥类型</th><th>指纹</th><th>操作</th></tr>
                    </thead>
                    <tbody id="hosts-table-body"></tbody>
                </table>
            </div>
            
            <!-- 生成密钥弹窗 -->
            <div class="modal hidden" id="keygen-modal">
                <div class="modal-content">
                    <h2>🔑 生成新密钥</h2>
                    <div class="form-group">
                        <label>密钥 ID</label>
                        <input type="text" id="keygen-id" placeholder="如: default, mykey" required>
                    </div>
                    <div class="form-group">
                        <label>密钥类型</label>
                        <select id="keygen-type">
                            <option value="rsa2048">RSA 2048-bit</option>
                            <option value="rsa4096">RSA 4096-bit</option>
                            <option value="ec256" selected>ECDSA P-256 (推荐)</option>
                            <option value="ec384">ECDSA P-384</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>备注 (可选)</label>
                        <input type="text" id="keygen-comment" placeholder="如: TianShanOS@device">
                    </div>
                    <div class="form-group">
                        <label><input type="checkbox" id="keygen-exportable"> 允许导出私钥</label>
                    </div>
                    <div class="form-actions">
                        <button class="btn" onclick="hideGenerateKeyModal()">取消</button>
                        <button class="btn btn-primary" onclick="generateKey()">生成</button>
                    </div>
                </div>
            </div>
            
            <!-- 部署密钥弹窗 -->
            <div class="modal hidden" id="deploy-key-modal">
                <div class="modal-content">
                    <h2>🚀 部署公钥到远程服务器</h2>
                    <p style="margin-bottom:15px;color:#666">将公钥 <code id="deploy-key-id"></code> 部署到远程服务器的 authorized_keys</p>
                    <div class="form-group">
                        <label>目标主机</label>
                        <input type="text" id="deploy-host" placeholder="192.168.55.100 或 hostname" required>
                    </div>
                    <div class="form-row">
                        <div class="form-group" style="flex:1">
                            <label>用户名</label>
                            <input type="text" id="deploy-user" placeholder="root" required>
                        </div>
                        <div class="form-group" style="width:100px">
                            <label>端口</label>
                            <input type="number" id="deploy-port" value="22" min="1" max="65535">
                        </div>
                    </div>
                    <div class="form-group">
                        <label>认证密码 (首次部署需要)</label>
                        <input type="password" id="deploy-password" placeholder="输入 SSH 登录密码" required>
                    </div>
                    <div style="background:#e3f2fd;border:1px solid #2196f3;border-radius:4px;padding:10px;margin:15px 0;font-size:0.9rem">
                        💡 部署后将可使用此密钥免密登录该服务器
                    </div>
                    <div id="deploy-result" class="result-box hidden" style="margin-bottom:15px"></div>
                    <div class="form-actions">
                        <button class="btn" onclick="hideDeployKeyModal()">取消</button>
                        <button class="btn btn-primary" id="deploy-btn" onclick="deployKey()">🚀 开始部署</button>
                    </div>
                </div>
            </div>
            
            <!-- 撤销密钥弹窗 -->
            <div class="modal hidden" id="revoke-key-modal">
                <div class="modal-content">
                    <h2>⚠️ 撤销公钥</h2>
                    <p style="margin-bottom:15px;color:#666">从远程服务器移除公钥 <code id="revoke-key-id"></code></p>
                    <div style="background:#fff3cd;border:1px solid #ffc107;border-radius:4px;padding:10px;margin-bottom:15px">
                        <strong>⚠️ 警告</strong>：撤销后将无法使用此密钥免密登录该服务器
                    </div>
                    <div class="form-group">
                        <label>目标主机</label>
                        <input type="text" id="revoke-host" placeholder="192.168.55.100 或 hostname" required>
                    </div>
                    <div class="form-row">
                        <div class="form-group" style="flex:1">
                            <label>用户名</label>
                            <input type="text" id="revoke-user" placeholder="root" required>
                        </div>
                        <div class="form-group" style="width:100px">
                            <label>端口</label>
                            <input type="number" id="revoke-port" value="22" min="1" max="65535">
                        </div>
                    </div>
                    <div class="form-group">
                        <label>认证密码</label>
                        <input type="password" id="revoke-password" placeholder="输入 SSH 登录密码" required>
                    </div>
                    <div id="revoke-result" class="result-box hidden" style="margin-bottom:15px"></div>
                    <div class="form-actions">
                        <button class="btn" onclick="hideRevokeKeyModal()">取消</button>
                        <button class="btn btn-danger" id="revoke-btn" onclick="revokeKey()">⚠️ 撤销公钥</button>
                    </div>
                </div>
            </div>
            
            <!-- 主机指纹不匹配警告弹窗 -->
            <div class="modal hidden" id="host-mismatch-modal">
                <div class="modal-content">
                    <h2 style="color:#dc3545">⚠️ 安全警告：主机指纹不匹配!</h2>
                    <div style="background:#f8d7da;border:1px solid #f5c6cb;border-radius:4px;padding:15px;margin:15px 0">
                        <p style="margin:0 0 10px;font-weight:bold">主机密钥已更改！这可能表明：</p>
                        <ul style="margin:0;padding-left:20px">
                            <li>中间人攻击（Man-in-the-Middle Attack）</li>
                            <li>服务器重新安装或密钥重新生成</li>
                            <li>IP 地址被分配给了不同的服务器</li>
                        </ul>
                    </div>
                    <div class="form-group">
                        <label>主机</label>
                        <input type="text" id="mismatch-host" readonly style="background:#f5f5f5">
                    </div>
                    <div class="form-group">
                        <label>存储的指纹</label>
                        <input type="text" id="mismatch-stored-fp" readonly style="background:#f5f5f5;font-family:monospace;font-size:12px">
                    </div>
                    <div class="form-group">
                        <label>当前指纹</label>
                        <input type="text" id="mismatch-current-fp" readonly style="background:#fff3cd;font-family:monospace;font-size:12px">
                    </div>
                    <p style="color:#856404;background:#fff3cd;padding:10px;border-radius:4px">
                        <strong>建议</strong>：如果您确认服务器已重装或密钥已更新，可以先删除旧的主机记录后重新连接。
                    </p>
                    <div class="form-actions">
                        <button class="btn" onclick="hideHostMismatchModal()">关闭</button>
                        <button class="btn btn-warning" onclick="removeAndRetry()">删除旧记录并重试</button>
                    </div>
                </div>
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
        const sshKeySelect = document.getElementById('ssh-keyid');
        
        // 更新 SSH 测试的密钥下拉列表
        if (sshKeySelect) {
            sshKeySelect.innerHTML = '<option value="">-- 选择密钥 --</option>';
            if (keys.data?.keys && keys.data.keys.length > 0) {
                keys.data.keys.forEach(key => {
                    const option = document.createElement('option');
                    option.value = key.id;
                    option.textContent = `${key.id} (${key.type_desc || key.type})`;
                    sshKeySelect.appendChild(option);
                });
            }
        }
        
        if (keys.data?.keys && keys.data.keys.length > 0) {
            tbody.innerHTML = keys.data.keys.map(key => `
                <tr>
                    <td><code>${escapeHtml(key.id)}</code></td>
                    <td>${escapeHtml(key.type_desc || key.type)}</td>
                    <td>${escapeHtml(key.comment) || '-'}</td>
                    <td>${formatTimestamp(key.created)}</td>
                    <td>${key.exportable ? '✅ 是' : '❌ 否'}</td>
                    <td>
                        <button class="btn btn-small" onclick="exportKey('${escapeHtml(key.id)}')" ${key.has_pubkey ? '' : 'disabled'}>📤 公钥</button>
                        <button class="btn btn-small" onclick="exportPrivateKey('${escapeHtml(key.id)}')" ${key.exportable ? '' : 'disabled'} title="${key.exportable ? '导出私钥' : '此密钥不可导出私钥'}">🔐 私钥</button>
                        <button class="btn btn-small btn-primary" onclick="showDeployKeyModal('${escapeHtml(key.id)}')" ${key.has_pubkey ? '' : 'disabled'} title="部署公钥到远程服务器">🚀 部署</button>
                        <button class="btn btn-small" onclick="showRevokeKeyModal('${escapeHtml(key.id)}')" ${key.has_pubkey ? '' : 'disabled'} title="从远程服务器撤销公钥" style="background:#ff9800;color:white">⚠️ 撤销</button>
                        <button class="btn btn-small btn-danger" onclick="deleteKey('${escapeHtml(key.id)}')">🗑️ 删除</button>
                    </td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:#888">暂无密钥，点击上方按钮生成新密钥</td></tr>';
        }
    } catch (e) {
        document.getElementById('keys-table-body').innerHTML = '<tr><td colspan="6" style="color:red">加载失败: ' + e.message + '</td></tr>';
    }
    
    // 已知主机
    try {
        const hosts = await api.hostsList();
        const tbody = document.getElementById('hosts-table-body');
        if (hosts.data?.hosts && hosts.data.hosts.length > 0) {
            tbody.innerHTML = hosts.data.hosts.map(host => `
                <tr>
                    <td><code>${escapeHtml(host.host)}</code></td>
                    <td>${host.port}</td>
                    <td>${escapeHtml(host.type) || '-'}</td>
                    <td><code title="${escapeHtml(host.fingerprint)}">${host.fingerprint ? host.fingerprint.substring(0, 24) + '...' : '-'}</code></td>
                    <td><button class="btn btn-small btn-danger" onclick="removeHost('${escapeHtml(host.host)}', ${host.port})">移除</button></td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:#888">暂无已知主机</td></tr>';
        }
    } catch (e) {
        document.getElementById('hosts-table-body').innerHTML = '<tr><td colspan="5" style="color:red">加载失败: ' + e.message + '</td></tr>';
    }
}

function toggleSshAuthType() {
    const authType = document.getElementById('ssh-auth-type').value;
    const passwordGroup = document.getElementById('ssh-password-group');
    const keyidGroup = document.getElementById('ssh-keyid-group');
    
    if (authType === 'password') {
        passwordGroup.classList.remove('hidden');
        keyidGroup.classList.add('hidden');
    } else {
        passwordGroup.classList.add('hidden');
        keyidGroup.classList.remove('hidden');
    }
}

async function testSsh(e) {
    e.preventDefault();
    
    const host = document.getElementById('ssh-host').value;
    const port = parseInt(document.getElementById('ssh-port').value);
    const user = document.getElementById('ssh-user').value;
    const authType = document.getElementById('ssh-auth-type').value;
    
    let auth;
    if (authType === 'password') {
        const password = document.getElementById('ssh-password').value;
        if (!password) {
            showToast('请输入密码', 'error');
            return;
        }
        auth = { password };
    } else {
        const keyid = document.getElementById('ssh-keyid').value;
        if (!keyid) {
            showToast('请选择密钥', 'error');
            return;
        }
        auth = { keyid };
    }
    
    const resultBox = document.getElementById('ssh-result');
    resultBox.classList.remove('hidden');
    resultBox.textContent = '测试中...';
    resultBox.className = 'result-box';
    
    try {
        const result = await api.sshTest(host, user, auth, port);
        if (result.data?.success) {
            // 显示指纹信息
            let msg = `✅ 连接成功! (${authType === 'password' ? '密码' : '密钥'}认证)`;
            if (result.data.fingerprint) {
                msg += `\n📝 指纹: ${result.data.fingerprint.substring(0, 32)}...`;
            }
            if (result.data.host_status === 'new_trusted') {
                msg += '\n🆕 新主机已添加到已知主机列表';
            }
            resultBox.textContent = msg;
            resultBox.classList.add('success');
        } else {
            resultBox.textContent = '❌ 连接失败: ' + (result.data?.error || '未知错误');
            resultBox.classList.add('error');
        }
    } catch (e) {
        // 检查是否是主机指纹问题
        if (e.code === 1001) {
            // 主机指纹不匹配 - 警告用户
            showHostMismatchModal(e.data || {
                host,
                port,
                current_fingerprint: e.data?.current_fingerprint || '未知',
                stored_fingerprint: e.data?.stored_fingerprint || '未知'
            });
            resultBox.textContent = '⚠️ 主机指纹不匹配! 可能存在中间人攻击风险';
            resultBox.classList.add('error');
        } else if (e.code === 1002) {
            // 新主机需要确认（trust_new=false 时）
            resultBox.textContent = '🆕 新主机: ' + (e.data?.fingerprint || '');
            resultBox.classList.add('warning');
        } else {
            resultBox.textContent = '❌ 连接失败: ' + e.message;
            resultBox.classList.add('error');
        }
    }
}

async function deleteKey(id) {
    if (confirm(`确定要删除密钥 "${id}" 吗？此操作不可撤销！`)) {
        try {
            await api.keyDelete(id);
            showToast('密钥已删除', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('删除失败: ' + e.message, 'error');
        }
    }
}

async function exportKey(id) {
    try {
        const result = await api.keyExport(id);
        if (result.data?.public_key) {
            // 显示公钥弹窗
            showPubkeyModal(id, result.data.public_key, result.data.type, result.data.comment);
        } else {
            showToast('无法获取公钥', 'error');
        }
    } catch (e) {
        showToast('导出失败: ' + e.message, 'error');
    }
}

async function exportPrivateKey(id) {
    // 安全确认
    if (!confirm(`⚠️ 安全警告\n\n您正在导出私钥 "${id}"。\n\n私钥是高度敏感的安全凭证，请确保：\n• 不要在公共网络传输\n• 不要分享给他人\n• 安全存储在本地\n\n确定要继续吗？`)) {
        return;
    }
    
    try {
        const result = await api.keyExportPrivate(id);
        if (result.data?.private_key) {
            showPrivkeyModal(id, result.data.private_key, result.data.type, result.data.comment);
        } else {
            showToast('无法获取私钥', 'error');
        }
    } catch (e) {
        showToast('导出失败: ' + e.message, 'error');
    }
}

function showPubkeyModal(id, pubkey, type, comment) {
    // 创建临时弹窗
    let modal = document.getElementById('pubkey-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'pubkey-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:700px">
            <h2>📤 公钥导出 - ${escapeHtml(id)}</h2>
            <p style="margin-bottom:10px;color:#666">类型: ${escapeHtml(type)}${comment ? ' | 备注: ' + escapeHtml(comment) : ''}</p>
            <textarea id="pubkey-content" readonly style="width:100%;height:150px;font-family:monospace;font-size:12px;resize:vertical">${escapeHtml(pubkey)}</textarea>
            <p style="margin-top:10px;font-size:0.85rem;color:#888">
                💡 将此公钥添加到远程服务器的 <code>~/.ssh/authorized_keys</code> 文件中即可实现免密登录
            </p>
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="closePubkeyModal()">关闭</button>
                <button class="btn btn-primary" onclick="copyPubkey()">📋 复制到剪贴板</button>
                <button class="btn" onclick="downloadPubkey('${escapeHtml(id)}')">💾 下载文件</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function closePubkeyModal() {
    const modal = document.getElementById('pubkey-modal');
    if (modal) modal.classList.add('hidden');
}

function showPrivkeyModal(id, privkey, type, comment) {
    // 创建临时弹窗
    let modal = document.getElementById('privkey-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'privkey-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:700px">
            <h2>🔐 私钥导出 - ${escapeHtml(id)}</h2>
            <div style="background:#fff3cd;border:1px solid #ffc107;border-radius:4px;padding:10px;margin-bottom:15px">
                <strong>⚠️ 安全警告</strong>：私钥是敏感信息，请妥善保管！
            </div>
            <p style="margin-bottom:10px;color:#666">类型: ${escapeHtml(type)}${comment ? ' | 备注: ' + escapeHtml(comment) : ''}</p>
            <textarea id="privkey-content" readonly style="width:100%;height:200px;font-family:monospace;font-size:11px;resize:vertical;background:#2d2d2d;color:#00ff00">${escapeHtml(privkey)}</textarea>
            <p style="margin-top:10px;font-size:0.85rem;color:#888">
                💡 保存为 <code>~/.ssh/${escapeHtml(id)}</code> 并设置权限 <code>chmod 600</code>
            </p>
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="closePrivkeyModal()">关闭</button>
                <button class="btn btn-primary" onclick="copyPrivkey()">📋 复制到剪贴板</button>
                <button class="btn" onclick="downloadPrivkey('${escapeHtml(id)}')">💾 下载文件</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function closePrivkeyModal() {
    const modal = document.getElementById('privkey-modal');
    if (modal) modal.classList.add('hidden');
}

async function copyPubkey() {
    const textarea = document.getElementById('pubkey-content');
    if (textarea) {
        try {
            await navigator.clipboard.writeText(textarea.value);
            showToast('已复制到剪贴板', 'success');
        } catch (e) {
            // Fallback for older browsers
            textarea.select();
            document.execCommand('copy');
            showToast('已复制到剪贴板', 'success');
        }
    }
}

function downloadPubkey(id) {
    const textarea = document.getElementById('pubkey-content');
    if (textarea) {
        // 使用 Data URL 避免 HTTP 安全警告
        const dataUrl = 'data:text/plain;charset=utf-8,' + encodeURIComponent(textarea.value);
        const a = document.createElement('a');
        a.href = dataUrl;
        a.download = `${id}.pub`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        showToast(`已下载 ${id}.pub`, 'success');
    }
}

async function copyPrivkey() {
    const textarea = document.getElementById('privkey-content');
    if (textarea) {
        try {
            await navigator.clipboard.writeText(textarea.value);
            showToast('已复制到剪贴板', 'success');
        } catch (e) {
            textarea.select();
            document.execCommand('copy');
            showToast('已复制到剪贴板', 'success');
        }
    }
}

function downloadPrivkey(id) {
    const textarea = document.getElementById('privkey-content');
    if (textarea) {
        // 使用 Data URL 避免 HTTP 安全警告
        const dataUrl = 'data:text/plain;charset=utf-8,' + encodeURIComponent(textarea.value);
        const a = document.createElement('a');
        a.href = dataUrl;
        a.download = id;  // 私钥文件不带扩展名
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        showToast(`已下载 ${id}`, 'success');
    }
}

// ====== 部署密钥功能 ======

let currentDeployKeyId = null;

function showDeployKeyModal(keyId) {
    currentDeployKeyId = keyId;
    document.getElementById('deploy-key-id').textContent = keyId;
    document.getElementById('deploy-host').value = '';
    document.getElementById('deploy-user').value = 'root';
    document.getElementById('deploy-port').value = '22';
    document.getElementById('deploy-password').value = '';
    const resultBox = document.getElementById('deploy-result');
    resultBox.classList.add('hidden');
    resultBox.textContent = '';
    document.getElementById('deploy-btn').disabled = false;
    document.getElementById('deploy-key-modal').classList.remove('hidden');
}

function hideDeployKeyModal() {
    document.getElementById('deploy-key-modal').classList.add('hidden');
    currentDeployKeyId = null;
}

async function deployKey() {
    if (!currentDeployKeyId) return;
    
    const host = document.getElementById('deploy-host').value.trim();
    const user = document.getElementById('deploy-user').value.trim();
    const port = parseInt(document.getElementById('deploy-port').value) || 22;
    const password = document.getElementById('deploy-password').value;
    
    if (!host || !user || !password) {
        showToast('请填写完整的服务器信息', 'error');
        return;
    }
    
    const resultBox = document.getElementById('deploy-result');
    const deployBtn = document.getElementById('deploy-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在部署密钥...';
    deployBtn.disabled = true;
    
    try {
        // 调用 ssh.copyid API（与 CLI 逻辑一致）
        const result = await api.sshCopyid(host, user, password, currentDeployKeyId, port, true);
        
        if (result.data?.deployed) {
            let msg = `✅ 部署成功！现在可以使用密钥 "${currentDeployKeyId}" 免密登录 ${user}@${host}`;
            if (result.data.verified) {
                msg += '\n✓ 公钥认证已验证';
            } else {
                msg += '\n⚠ 公钥认证验证跳过';
            }
            resultBox.textContent = msg;
            resultBox.classList.add('success');
            showToast('密钥部署成功', 'success');
        } else {
            throw new Error('部署失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 部署失败: ' + e.message;
        resultBox.classList.add('error');
    } finally {
        deployBtn.disabled = false;
    }
}

// ====== 撤销密钥功能 ======

let currentRevokeKeyId = null;

function showRevokeKeyModal(keyId) {
    currentRevokeKeyId = keyId;
    document.getElementById('revoke-key-id').textContent = keyId;
    document.getElementById('revoke-host').value = '';
    document.getElementById('revoke-user').value = 'root';
    document.getElementById('revoke-port').value = '22';
    document.getElementById('revoke-password').value = '';
    const resultBox = document.getElementById('revoke-result');
    resultBox.classList.add('hidden');
    resultBox.textContent = '';
    document.getElementById('revoke-btn').disabled = false;
    document.getElementById('revoke-key-modal').classList.remove('hidden');
}

function hideRevokeKeyModal() {
    document.getElementById('revoke-key-modal').classList.add('hidden');
    currentRevokeKeyId = null;
}

async function revokeKey() {
    if (!currentRevokeKeyId) return;
    
    const host = document.getElementById('revoke-host').value.trim();
    const user = document.getElementById('revoke-user').value.trim();
    const port = parseInt(document.getElementById('revoke-port').value) || 22;
    const password = document.getElementById('revoke-password').value;
    
    if (!host || !user || !password) {
        showToast('请填写完整的服务器信息', 'error');
        return;
    }
    
    const resultBox = document.getElementById('revoke-result');
    const revokeBtn = document.getElementById('revoke-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在撤销密钥...';
    revokeBtn.disabled = true;
    
    try {
        // 调用 ssh.revoke API（与 CLI 逻辑一致）
        const result = await api.sshRevoke(host, user, password, currentRevokeKeyId, port);
        
        if (result.data?.revoked) {
            resultBox.textContent = `✅ 撤销成功！已从 ${user}@${host} 移除 ${result.data.removed_count || 1} 个匹配的公钥`;
            resultBox.classList.add('success');
            showToast('密钥撤销成功', 'success');
        } else if (result.data?.found === false) {
            resultBox.textContent = `⚠️ 该公钥未在 ${user}@${host} 上找到`;
            resultBox.classList.add('warning');
            showToast('公钥未找到', 'warning');
        } else {
            throw new Error('撤销失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 撤销失败: ' + e.message;
        resultBox.classList.add('error');
    } finally {
        revokeBtn.disabled = false;
    }
}

// ====== 主机指纹不匹配警告 ======

let currentMismatchInfo = null;

function showHostMismatchModal(info) {
    currentMismatchInfo = info;
    document.getElementById('mismatch-host').value = `${info.host}:${info.port || 22}`;
    document.getElementById('mismatch-stored-fp').value = info.stored_fingerprint || '未知';
    document.getElementById('mismatch-current-fp').value = info.current_fingerprint || '未知';
    document.getElementById('host-mismatch-modal').classList.remove('hidden');
}

function hideHostMismatchModal() {
    document.getElementById('host-mismatch-modal').classList.add('hidden');
    currentMismatchInfo = null;
}

async function removeAndRetry() {
    if (!currentMismatchInfo) return;
    
    try {
        await api.hostsRemove(currentMismatchInfo.host, currentMismatchInfo.port || 22);
        showToast('已删除旧的主机记录，请重新连接', 'success');
        hideHostMismatchModal();
        await refreshSecurityPage();
    } catch (e) {
        showToast('删除失败: ' + e.message, 'error');
    }
}

async function removeHost(host, port) {
    if (confirm(`确定要移除主机 "${host}:${port}" 的记录吗？`)) {
        try {
            await api.hostsRemove(host, port);
            showToast('主机已移除', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('移除失败: ' + e.message, 'error');
        }
    }
}

async function clearAllHosts() {
    if (confirm('确定要清除所有已知主机记录吗？此操作不可撤销！')) {
        try {
            await api.hostsClear();
            showToast('已清除所有已知主机', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('清除失败: ' + e.message, 'error');
        }
    }
}

function showGenerateKeyModal() {
    document.getElementById('keygen-modal').classList.remove('hidden');
    document.getElementById('keygen-id').value = '';
    document.getElementById('keygen-type').value = 'ec256';
    document.getElementById('keygen-comment').value = '';
    document.getElementById('keygen-exportable').checked = false;
}

function hideGenerateKeyModal() {
    document.getElementById('keygen-modal').classList.add('hidden');
}

async function generateKey() {
    const id = document.getElementById('keygen-id').value.trim();
    const type = document.getElementById('keygen-type').value;
    const comment = document.getElementById('keygen-comment').value.trim();
    const exportable = document.getElementById('keygen-exportable').checked;
    
    if (!id) {
        showToast('请输入密钥 ID', 'error');
        return;
    }
    
    try {
        showToast('正在生成密钥...', 'info');
        await api.keyGenerate(id, type, comment, exportable);
        hideGenerateKeyModal();
        showToast(`密钥 "${id}" 生成成功`, 'success');
        await refreshSecurityPage();
    } catch (e) {
        showToast('生成失败: ' + e.message, 'error');
    }
}

function formatTimestamp(ts) {
    if (!ts) return '-';
    const date = new Date(ts * 1000);
    return date.toLocaleString('zh-CN');
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
window.syncTimeFromBrowser = syncTimeFromBrowser;
window.forceNtpSync = forceNtpSync;
window.showTimezoneModal = showTimezoneModal;
window.hideTimezoneModal = hideTimezoneModal;
window.applyTimezone = applyTimezone;
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
window.filterConfigList = filterConfigList;
window.loadAllConfig = loadAllConfig;
window.editConfig = editConfig;
window.saveConfig = saveConfig;
window.deleteConfig = deleteConfig;
window.toggleSshAuthType = toggleSshAuthType;
window.testSsh = testSsh;
window.deleteKey = deleteKey;
window.exportKey = exportKey;
window.exportPrivateKey = exportPrivateKey;
window.showPubkeyModal = showPubkeyModal;
window.closePubkeyModal = closePubkeyModal;
window.copyPubkey = copyPubkey;
window.downloadPubkey = downloadPubkey;
window.showPrivkeyModal = showPrivkeyModal;
window.closePrivkeyModal = closePrivkeyModal;
window.copyPrivkey = copyPrivkey;
window.downloadPrivkey = downloadPrivkey;
window.removeHost = removeHost;
window.clearAllHosts = clearAllHosts;
window.showGenerateKeyModal = showGenerateKeyModal;
window.hideGenerateKeyModal = hideGenerateKeyModal;
window.generateKey = generateKey;
window.showDeployKeyModal = showDeployKeyModal;
window.hideDeployKeyModal = hideDeployKeyModal;
window.deployKey = deployKey;
window.showRevokeKeyModal = showRevokeKeyModal;
window.hideRevokeKeyModal = hideRevokeKeyModal;
window.revokeKey = revokeKey;
window.showHostMismatchModal = showHostMismatchModal;
window.hideHostMismatchModal = hideHostMismatchModal;
window.removeAndRetry = removeAndRetry;
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
// 批量文件操作
window.toggleFileSelection = toggleFileSelection;
window.toggleSelectAll = toggleSelectAll;
window.clearSelection = clearSelection;
window.batchDelete = batchDelete;
window.batchDownload = batchDownload;
// Matrix 滤镜
window.selectFilter = selectFilter;
window.applySelectedFilter = applySelectedFilter;
window.applyFilter = applyFilter;
window.stopFilter = stopFilter;
// Matrix 功能
window.displayImage = displayImage;
window.generateQrCode = generateQrCode;
window.clearQrBgImage = clearQrBgImage;
window.displayText = displayText;
window.stopText = stopText;
window.saveLedConfig = saveLedConfig;
window.loadFontList = loadFontList;
// 文件选择器
window.openFilePickerFor = openFilePickerFor;
window.browseImages = browseImages;
window.filePickerItemClick = filePickerItemClick;
window.filePickerItemDblClick = filePickerItemDblClick;
window.filePickerGoUp = filePickerGoUp;
window.closeFilePicker = closeFilePicker;
window.confirmFilePicker = confirmFilePicker;
window.createAndOpenDir = createAndOpenDir;
// 网络配置
window.hideWifiScan = hideWifiScan;
window.disconnectWifi = disconnectWifi;
window.showApStations = showApStations;
window.hideApStations = hideApStations;
window.showApConfig = showApConfig;
window.hideApConfig = hideApConfig;
window.applyApConfig = applyApConfig;
window.showDhcpClients = showDhcpClients;
window.hideDhcpClients = hideDhcpClients;
window.loadDhcpClients = loadDhcpClients;
window.setWifiMode = setWifiMode;
window.setHostname = setHostname;
window.saveNatConfig = saveNatConfig;

// 初始化滑块事件
document.addEventListener('DOMContentLoaded', function() {
    // 滤镜速度滑块
    document.body.addEventListener('input', function(e) {
        if (e.target.id === 'matrix-filter-speed') {
            const valueSpan = document.getElementById('filter-speed-value');
            if (valueSpan) valueSpan.textContent = e.target.value;
        }
    });
});
