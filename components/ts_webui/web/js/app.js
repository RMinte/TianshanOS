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
    
    // 注册路由（系统页面作为首页）
    router.register('/', loadSystemPage);
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
//                         系统页面（合并原首页+系统）
// =========================================================================

async function loadSystemPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-system">
            <h1>🖥️ 系统</h1>
            
            <!-- 系统概览卡片 -->
            <div class="cards">
                <div class="card">
                    <h3>📟 系统信息</h3>
                    <div class="card-content">
                        <p><strong>芯片:</strong> <span id="sys-chip">-</span></p>
                        <p><strong>固件:</strong> <span id="sys-version">-</span></p>
                        <p><strong>IDF:</strong> <span id="sys-idf">-</span></p>
                        <p><strong>编译:</strong> <span id="sys-compile">-</span></p>
                        <p><strong>运行时间:</strong> <span id="sys-uptime">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🕐 系统时间</h3>
                    <div class="card-content">
                        <p><strong>当前:</strong> <span id="sys-datetime">-</span></p>
                        <p><strong>状态:</strong> <span id="sys-time-status">-</span></p>
                        <p><strong>来源:</strong> <span id="sys-time-source">-</span></p>
                        <p><strong>时区:</strong> <span id="sys-timezone">-</span></p>
                    </div>
                    <div class="button-group" style="margin-top:10px">
                        <button class="btn btn-small" onclick="syncTimeFromBrowser()">🔄 浏览器同步</button>
                        <button class="btn btn-small" onclick="showTimezoneModal()">⚙️ 时区</button>
                    </div>
                </div>
                
                <div class="card">
                    <h3>💾 内存</h3>
                    <div class="card-content">
                        <p><strong>堆内存:</strong></p>
                        <div class="progress-bar"><div class="progress" id="heap-progress"></div></div>
                        <p style="font-size:0.9em" id="heap-text">-</p>
                        <p><strong>PSRAM:</strong></p>
                        <div class="progress-bar"><div class="progress" id="psram-progress"></div></div>
                        <p style="font-size:0.9em" id="psram-text">-</p>
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
                        <p><strong>电流:</strong> <span id="current">-</span></p>
                        <p><strong>功率:</strong> <span id="power-watts">-</span></p>
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
                    <h3>⚙️ 系统操作</h3>
                    <div class="card-content">
                        <p style="color:#888;font-size:0.9em">管理 ESP32 系统</p>
                    </div>
                    <div class="button-group" style="margin-top:10px">
                        <button class="btn btn-warning btn-small" onclick="confirmReboot()">🔄 重启系统</button>
                    </div>
                </div>
            </div>
            
            <!-- 风扇控制 -->
            <div class="section">
                <h2>🌀 风扇控制</h2>
                <div class="fans-grid" id="fans-grid">
                    <div class="loading">加载中...</div>
                </div>
            </div>
            
            <!-- 服务状态 -->
            <div class="section">
                <h2>📋 服务状态</h2>
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
        </div>
    `;
    
    await refreshSystemPage();
    
    // 定时刷新
    refreshInterval = setInterval(refreshSystemPage, 3000);
}

async function refreshSystemPage() {
    // 系统信息
    try {
        const info = await api.getSystemInfo();
        if (info.data) {
            document.getElementById('sys-chip').textContent = info.data.chip?.model || '-';
            document.getElementById('sys-version').textContent = info.data.app?.version || '-';
            document.getElementById('sys-idf').textContent = info.data.app?.idf_version || '-';
            document.getElementById('sys-compile').textContent = 
                (info.data.app?.compile_date || '') + ' ' + (info.data.app?.compile_time || '');
            document.getElementById('sys-uptime').textContent = formatUptime(info.data.uptime_ms);
        }
    } catch (e) { console.log('System info error:', e); }
    
    // 时间同步信息
    try {
        const time = await api.timeInfo();
        if (time.data) {
            // 检查时间是否早于 2025 年，自动同步浏览器时间
            const deviceYear = time.data.year || (time.data.datetime ? parseInt(time.data.datetime.substring(0, 4)) : 0);
            if (deviceYear < 2025) {
                console.log('Device time is before 2025, auto-syncing from browser...');
                await syncTimeFromBrowser(true);  // 静默同步
                return;  // 同步后会再次刷新
            }
            
            // 显示实时时间（基于服务器时间+本地偏移）
            const serverTime = time.data.timestamp_ms || Date.now();
            const now = new Date(serverTime);
            const timeStr = now.toLocaleString('zh-CN', { 
                year: 'numeric', month: '2-digit', day: '2-digit',
                hour: '2-digit', minute: '2-digit', second: '2-digit',
                hour12: false 
            });
            document.getElementById('sys-datetime').textContent = timeStr;
            
            const statusText = time.data.synced ? '✅ 已同步' : '⏳ 未同步';
            document.getElementById('sys-time-status').textContent = statusText;
            const sourceMap = { ntp: 'NTP', http: '浏览器', manual: '手动', none: '未同步' };
            document.getElementById('sys-time-source').textContent = sourceMap[time.data.source] || time.data.source;
            document.getElementById('sys-timezone').textContent = time.data.timezone || '-';
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
            } else {
                document.getElementById('psram-text').textContent = '不可用';
            }
        }
    } catch (e) { console.log('Memory info error:', e); }
    
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
            const voltage = powerStatus.data.power_chip?.voltage_v || 
                           powerStatus.data.voltage?.supply_v || 
                           powerStatus.data.stats?.avg_voltage_v;
            const current = powerStatus.data.power_chip?.current_a ||
                           powerStatus.data.current?.value_a;
            const power = powerStatus.data.power_chip?.power_w ||
                         powerStatus.data.power?.value_w;
            
            document.getElementById('voltage').textContent = 
                (typeof voltage === 'number' ? voltage.toFixed(1) + ' V' : '-');
            document.getElementById('current').textContent = 
                (typeof current === 'number' ? current.toFixed(2) + ' A' : '-');
            document.getElementById('power-watts').textContent = 
                (typeof power === 'number' ? power.toFixed(1) + ' W' : '-');
        }
        const protStatus = await api.powerProtectionStatus();
        if (protStatus.data) {
            const running = protStatus.data.running || protStatus.data.initialized;
            document.getElementById('protection-status').textContent = 
                running ? '✅ 已启用' : '⚠️ 已禁用';
        }
    } catch (e) { 
        document.getElementById('voltage').textContent = '-'; 
        document.getElementById('current').textContent = '-'; 
        document.getElementById('power-watts').textContent = '-'; 
    }
    
    // 设备状态
    try {
        const devStatus = await api.deviceStatus();
        if (devStatus.data?.devices) {
            const agx = devStatus.data.devices.find(d => d.name === 'agx');
            const lpmu = devStatus.data.devices.find(d => d.name === 'lpmu');
            document.getElementById('agx-status').textContent = agx?.powered ? '🟢 运行中' : '⚫ 关机';
            document.getElementById('lpmu-status').textContent = lpmu?.powered ? '🟢 运行中' : '⚫ 关机';
        }
    } catch (e) {
        document.getElementById('agx-status').textContent = '-';
        document.getElementById('lpmu-status').textContent = '-';
    }
    
    // 风扇
    try {
        const fans = await api.fanStatus();
        const container = document.getElementById('fans-grid');
        if (fans.data?.fans && fans.data.fans.length > 0) {
            container.innerHTML = fans.data.fans.map(fan => `
                <div class="fan-card">
                    <h4>🌀 风扇 ${fan.id}</h4>
                    <p><strong>模式:</strong> ${fan.mode || 'auto'}</p>
                    <p><strong>转速:</strong> ${fan.speed || fan.duty || 0}%</p>
                    <p><strong>RPM:</strong> ${fan.rpm || '-'}</p>
                    <div class="fan-slider">
                        <input type="range" min="0" max="100" value="${fan.speed || fan.duty || 0}" 
                               onchange="setFanSpeed(${fan.id}, this.value)"
                               oninput="this.nextElementSibling.textContent = this.value + '%'">
                        <span>${fan.speed || fan.duty || 0}%</span>
                    </div>
                </div>
            `).join('');
        } else {
            container.innerHTML = '<p class="text-muted">无可用风扇</p>';
        }
    } catch (e) { 
        document.getElementById('fans-grid').innerHTML = '<p class="text-muted">风扇状态不可用</p>';
    }
    
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

async function setFanSpeed(id, speed) {
    try {
        await api.fanSet(id, parseInt(speed));
        showToast(`风扇 ${id} 速度已设置为 ${speed}%`, 'success');
    } catch (e) { showToast('设置风扇失败: ' + e.message, 'error'); }
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
async function syncTimeFromBrowser(silent = false) {
    try {
        const now = Date.now();
        if (!silent) showToast('正在从浏览器同步时间...', 'info');
        const result = await api.timeSync(now);
        if (result.data?.synced) {
            if (!silent) showToast(`时间已同步: ${result.data.datetime}`, 'success');
            await refreshSystemPage();
        } else {
            if (!silent) showToast('时间同步失败', 'error');
        }
    } catch (e) {
        if (!silent) showToast('同步失败: ' + e.message, 'error');
    }
}

async function forceNtpSync() {
    try {
        showToast('正在强制NTP同步...', 'info');
        const result = await api.timeForceSync();
        if (result.data?.syncing) {
            showToast('NTP同步已启动，请稍候刷新查看结果', 'success');
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
            <div class="led-page-header">
                <h1>💡 LED 控制</h1>
                <div class="led-quick-actions">
                    <button class="btn btn-sm" onclick="refreshLedPage()">🔄 刷新</button>
                    <button class="btn btn-sm" onclick="allLedsOff()">⏹ 全部关闭</button>
                </div>
            </div>
            <div id="led-devices-grid" class="led-devices-grid">
                <div class="loading-inline">加载设备中...</div>
            </div>
        </div>
    `;
    
    await refreshLedPage();
}

async function refreshLedPage() {
    const container = document.getElementById('led-devices-grid');
    
    try {
        const result = await api.ledList();
        
        if (result.data && result.data.devices && result.data.devices.length > 0) {
            // 存储设备信息
            result.data.devices.forEach(dev => {
                ledDevices[dev.name] = dev;
                if (dev.current && dev.current.animation) {
                    selectedEffects[dev.name] = dev.current.animation;
                }
                // 初始化 LED 状态
                if (dev.current) {
                    ledStates[dev.name] = dev.current.on || false;
                }
            });
            
            window.ledDevicesCache = result.data.devices;
            
            // 渲染设备卡片
            container.innerHTML = result.data.devices.map(dev => generateLedDeviceCard(dev)).join('');
            
            // 加载字体列表
            if (result.data.devices.some(d => d.name === 'matrix' || d.layout === 'matrix')) {
                loadFontList();
            }
        } else {
            container.innerHTML = `
                <div class="led-empty-state">
                    <div class="empty-icon">⚠️</div>
                    <h3>未找到 LED 设备</h3>
                    <p>LED 设备可能尚未启动，请检查：</p>
                    <ul>
                        <li>LED 服务状态 (<code>service --status</code>)</li>
                        <li>GPIO 引脚配置</li>
                    </ul>
                </div>
            `;
        }
    } catch (e) {
        console.error('LED list error:', e);
        container.innerHTML = `<div class="error-state">加载失败: ${e.message}</div>`;
    }
}

function generateLedDeviceCard(dev) {
    const icon = getDeviceIcon(dev.name);
    const description = getDeviceDescription(dev.name);
    const current = dev.current || {};
    const isOn = current.on || false;
    const currentAnimation = current.animation || '';
    const currentSpeed = current.speed || 50;
    const currentColor = current.color || {r: 255, g: 255, b: 255};
    const colorHex = rgbToHex(currentColor);
    const isMatrix = dev.name === 'matrix' || dev.layout === 'matrix';
    const deviceEffects = dev.effects || [];
    
    // 状态文本
    let statusText = '已关闭';
    let statusClass = 'off';
    if (isOn) {
        if (currentAnimation) {
            statusText = `▶ ${currentAnimation}`;
            statusClass = 'effect';
        } else {
            statusText = '常亮';
            statusClass = 'on';
        }
    }
    
    // 快捷特效按钮（显示前4个）
    const quickEffects = deviceEffects.slice(0, 4);
    const quickEffectsHtml = quickEffects.map(eff => 
        `<button class="led-quick-effect ${eff === currentAnimation ? 'active' : ''}" 
                 onclick="quickStartEffect('${dev.name}', '${eff}')" 
                 title="${eff}">${getEffectIcon(eff)}</button>`
    ).join('');
    
    // Matrix 设备额外按钮
    const matrixButtons = isMatrix ? `
        <button class="led-func-btn" onclick="openLedModal('${dev.name}', 'content')" title="图像/QR码">
            <span class="func-icon">📷</span>
        </button>
        <button class="led-func-btn" onclick="openLedModal('${dev.name}', 'text')" title="文本显示">
            <span class="func-icon">📝</span>
        </button>
        <button class="led-func-btn" onclick="openLedModal('${dev.name}', 'filter')" title="滤镜效果">
            <span class="func-icon">🎨</span>
        </button>
    ` : '';
    
    return `
        <div class="led-device-card ${isOn ? 'is-on' : ''}" data-device="${dev.name}">
            <!-- 设备头部 -->
            <div class="led-card-header">
                <div class="led-device-icon">${icon}</div>
                <div class="led-device-info">
                    <span class="led-device-name">${dev.name}</span>
                    <span class="led-device-desc">${description}</span>
                </div>
                <div class="led-device-status ${statusClass}">${statusText}</div>
            </div>
            
            <!-- 控制区域 -->
            <div class="led-card-controls">
                <!-- 亮度滑块 -->
                <div class="led-brightness-row">
                    <span class="brightness-label">☀️</span>
                    <input type="range" min="0" max="255" value="${dev.brightness}" 
                           class="led-brightness-slider"
                           oninput="updateBrightnessDisplay('${dev.name}', this.value)"
                           onchange="setBrightness('${dev.name}', this.value)"
                           id="brightness-${dev.name}">
                    <span class="brightness-value" id="brightness-val-${dev.name}">${dev.brightness}</span>
                </div>
                
                <!-- 颜色选择 -->
                <div class="led-color-row">
                    <input type="color" value="${colorHex}" id="color-picker-${dev.name}" 
                           onchange="fillColorFromPicker('${dev.name}', this.value)"
                           class="led-color-picker">
                    <div class="led-color-presets">
                        <button class="color-dot" style="background:#ff0000" onclick="quickFillColor('${dev.name}', '#ff0000')"></button>
                        <button class="color-dot" style="background:#ff6600" onclick="quickFillColor('${dev.name}', '#ff6600')"></button>
                        <button class="color-dot" style="background:#ffff00" onclick="quickFillColor('${dev.name}', '#ffff00')"></button>
                        <button class="color-dot" style="background:#00ff00" onclick="quickFillColor('${dev.name}', '#00ff00')"></button>
                        <button class="color-dot" style="background:#00ffff" onclick="quickFillColor('${dev.name}', '#00ffff')"></button>
                        <button class="color-dot" style="background:#0066ff" onclick="quickFillColor('${dev.name}', '#0066ff')"></button>
                        <button class="color-dot" style="background:#ff00ff" onclick="quickFillColor('${dev.name}', '#ff00ff')"></button>
                        <button class="color-dot" style="background:#ffffff" onclick="quickFillColor('${dev.name}', '#ffffff')"></button>
                    </div>
                </div>
                
                <!-- 快捷特效 -->
                <div class="led-effects-row">
                    <div class="led-quick-effects">
                        ${quickEffectsHtml}
                        ${deviceEffects.length > 4 ? `<button class="led-quick-effect more" onclick="openLedModal('${dev.name}', 'effect')" title="更多特效">+${deviceEffects.length - 4}</button>` : ''}
                    </div>
                    <button class="led-stop-btn" onclick="stopEffect('${dev.name}')" title="停止特效">⏹</button>
                </div>
            </div>
            
            <!-- 底部操作栏 -->
            <div class="led-card-footer">
                <button class="led-power-btn ${isOn ? 'on' : ''}" id="toggle-${dev.name}" onclick="toggleLed('${dev.name}')">
                    <span class="power-icon">${isOn ? '🔆' : '💡'}</span>
                    <span class="power-text">${isOn ? '关闭' : '开启'}</span>
                </button>
                ${matrixButtons}
                <button class="led-func-btn" onclick="openLedModal('${dev.name}', 'effect')" title="全部特效">
                    <span class="func-icon">🎬</span>
                </button>
                <button class="led-save-btn" onclick="saveLedConfig('${dev.name}')" title="保存配置">
                    💾
                </button>
            </div>
        </div>
    `;
}

// 辅助函数
function rgbToHex(color) {
    const r = (color.r || 0).toString(16).padStart(2, '0');
    const g = (color.g || 0).toString(16).padStart(2, '0');
    const b = (color.b || 0).toString(16).padStart(2, '0');
    return '#' + r + g + b;
}

function updateBrightnessDisplay(device, value) {
    const label = document.getElementById(`brightness-val-${device}`);
    if (label) label.textContent = value;
}

async function fillColorFromPicker(device, color) {
    try {
        await api.ledFill(device, color);
        ledStates[device] = true;
        updateLedCardState(device, true);
        showToast(`${device} 已填充 ${color}`, 'success');
    } catch (e) {
        showToast(`填充失败: ${e.message}`, 'error');
    }
}

async function quickFillColor(device, color) {
    const picker = document.getElementById(`color-picker-${device}`);
    if (picker) picker.value = color;
    try {
        await api.ledFill(device, color);
        ledStates[device] = true;
        updateLedCardState(device, true, null);
        showToast(`${device} → ${color}`, 'success');
    } catch (e) {
        showToast(`填充失败: ${e.message}`, 'error');
    }
}

async function quickStartEffect(device, effect) {
    try {
        await api.ledEffectStart(device, effect, { speed: 50 });
        selectedEffects[device] = effect;
        ledStates[device] = true;
        updateLedCardState(device, true, effect);
        showToast(`${device}: ${effect}`, 'success');
    } catch (e) {
        showToast(`启动失败: ${e.message}`, 'error');
    }
}

async function allLedsOff() {
    const devices = window.ledDevicesCache || [];
    for (const dev of devices) {
        try {
            await api.ledClear(dev.name);
            ledStates[dev.name] = false;
            updateLedCardState(dev.name, false);
        } catch (e) {
            console.error(`关闭 ${dev.name} 失败:`, e);
        }
    }
    showToast('全部 LED 已关闭', 'success');
}

function updateLedCardState(device, isOn, effect = undefined) {
    const card = document.querySelector(`.led-device-card[data-device="${device}"]`);
    if (!card) return;
    
    // 更新卡片状态
    if (isOn) {
        card.classList.add('is-on');
    } else {
        card.classList.remove('is-on');
    }
    
    // 更新状态显示
    const statusEl = card.querySelector('.led-device-status');
    if (statusEl) {
        if (!isOn) {
            statusEl.textContent = '已关闭';
            statusEl.className = 'led-device-status off';
        } else if (effect) {
            statusEl.textContent = `▶ ${effect}`;
            statusEl.className = 'led-device-status effect';
        } else {
            statusEl.textContent = '常亮';
            statusEl.className = 'led-device-status on';
        }
    }
    
    // 更新电源按钮
    const powerBtn = card.querySelector('.led-power-btn');
    if (powerBtn) {
        if (isOn) {
            powerBtn.classList.add('on');
            powerBtn.querySelector('.power-icon').textContent = '🔆';
            powerBtn.querySelector('.power-text').textContent = '关闭';
        } else {
            powerBtn.classList.remove('on');
            powerBtn.querySelector('.power-icon').textContent = '💡';
            powerBtn.querySelector('.power-text').textContent = '开启';
        }
    }
    
    // 更新快捷特效按钮状态
    card.querySelectorAll('.led-quick-effect').forEach(btn => {
        const btnEffect = btn.getAttribute('title');
        if (effect && btnEffect === effect) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
}

// 颜色选择模态框
function openColorModal(device) {
    const deviceData = window.ledDevicesCache?.find(d => d.name === device);
    const current = deviceData?.current || {};
    const currentColor = current.color || {r: 255, g: 0, b: 0};
    const colorHex = '#' + 
        currentColor.r.toString(16).padStart(2, '0') +
        currentColor.g.toString(16).padStart(2, '0') +
        currentColor.b.toString(16).padStart(2, '0');
    
    const modal = document.getElementById('led-modal');
    const title = document.getElementById('led-modal-title');
    const body = document.getElementById('led-modal-body');
    
    title.textContent = `🎨 ${device} - 颜色设置`;
    body.innerHTML = `
        <div class="modal-section">
            <h3>颜色选择</h3>
            <div class="config-row">
                <input type="color" id="modal-color-picker-${device}" value="${colorHex}" style="width:60px;height:40px;">
                <button class="btn btn-primary" onclick="applyColorFromModal('${device}')">填充颜色</button>
            </div>
            <h3 style="margin-top:16px;">快捷颜色</h3>
            <div class="preset-colors-grid">
                <button class="color-preset" style="background:#ff0000" onclick="quickFillFromModal('${device}', '#ff0000')"></button>
                <button class="color-preset" style="background:#ff6600" onclick="quickFillFromModal('${device}', '#ff6600')"></button>
                <button class="color-preset" style="background:#ffff00" onclick="quickFillFromModal('${device}', '#ffff00')"></button>
                <button class="color-preset" style="background:#00ff00" onclick="quickFillFromModal('${device}', '#00ff00')"></button>
                <button class="color-preset" style="background:#00ffff" onclick="quickFillFromModal('${device}', '#00ffff')"></button>
                <button class="color-preset" style="background:#0000ff" onclick="quickFillFromModal('${device}', '#0000ff')"></button>
                <button class="color-preset" style="background:#ff00ff" onclick="quickFillFromModal('${device}', '#ff00ff')"></button>
                <button class="color-preset" style="background:#ffffff" onclick="quickFillFromModal('${device}', '#ffffff')"></button>
                <button class="color-preset" style="background:#ffcccc" onclick="quickFillFromModal('${device}', '#ffcccc')"></button>
                <button class="color-preset" style="background:#ccffcc" onclick="quickFillFromModal('${device}', '#ccffcc')"></button>
                <button class="color-preset" style="background:#ccccff" onclick="quickFillFromModal('${device}', '#ccccff')"></button>
                <button class="color-preset" style="background:#000000" onclick="quickFillFromModal('${device}', '#000000')"></button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

async function applyColorFromModal(device) {
    const color = document.getElementById(`modal-color-picker-${device}`)?.value || '#ffffff';
    try {
        await api.ledFill(device, color);
        ledStates[device] = true;
        updateToggleButton(device, true);
        showToast(`${device} 已填充 ${color}`, 'success');
    } catch (e) {
        showToast(`填充失败: ${e.message}`, 'error');
    }
}

async function quickFillFromModal(device, color) {
    try {
        await api.ledFill(device, color);
        ledStates[device] = true;
        updateToggleButton(device, true);
        showToast(`${device} → ${color}`, 'success');
    } catch (e) {
        showToast(`填充失败: ${e.message}`, 'error');
    }
}

// 更新开关按钮状态
function updateToggleButton(device, isOn) {
    // 更新新版卡片
    updateLedCardState(device, isOn);
    
    // 旧版兼容
    const btn = document.getElementById(`toggle-${device}`);
    if (btn && !btn.classList.contains('led-power-btn')) {
        if (isOn) {
            btn.classList.add('on');
            btn.innerHTML = '🔆 已开启';
        } else {
            btn.classList.remove('on');
            btn.innerHTML = '💡 已关闭';
        }
    }
}

// 生成 LED 模态框内容
function generateLedModalContent(device, type) {
    const deviceData = window.ledDevicesCache?.find(d => d.name === device);
    const current = deviceData?.current || {};
    const currentAnimation = current.animation || '';
    const currentSpeed = current.speed || 50;
    const currentColor = current.color || {r: 255, g: 0, b: 0};
    const colorHex = '#' + 
        currentColor.r.toString(16).padStart(2, '0') +
        currentColor.g.toString(16).padStart(2, '0') +
        currentColor.b.toString(16).padStart(2, '0');
    const deviceEffects = deviceData?.effects || [];
    
    if (type === 'effect') {
        // 普通设备的动画模态框
        const effectsHtml = deviceEffects.length > 0 
            ? deviceEffects.map(eff => {
                const isActive = eff === currentAnimation;
                const activeClass = isActive ? ' active' : '';
                return `<button class="btn effect-btn${activeClass}" onclick="selectEffectInModal('${device}', '${eff}', this)">${getEffectIcon(eff)} ${eff}</button>`;
            }).join('')
            : '<span class="empty">暂无可用特效</span>';
        
        return `
            <div class="modal-section">
                <h3>🎬 程序动画</h3>
                <div class="effects-grid">${effectsHtml}</div>
                <div class="effect-config-modal" id="modal-effect-config-${device}" style="display:${currentAnimation ? 'flex' : 'none'};">
                    <span class="effect-name" id="modal-effect-name-${device}">${currentAnimation || '未选择'}</span>
                    <div class="config-row">
                        <label>速度</label>
                        <input type="range" min="1" max="100" value="${currentSpeed}" id="modal-effect-speed-${device}" 
                               oninput="document.getElementById('modal-speed-val-${device}').textContent=this.value">
                        <span id="modal-speed-val-${device}">${currentSpeed}</span>
                    </div>
                    <div class="config-row" id="modal-color-row-${device}" style="display:${colorSupportedEffects.includes(currentAnimation) ? 'flex' : 'none'};">
                        <label>颜色</label>
                        <input type="color" id="modal-effect-color-${device}" value="${colorHex}">
                    </div>
                    <div class="config-actions">
                        <button class="btn btn-primary" onclick="applyEffectFromModal('${device}')">▶ 启动</button>
                        <button class="btn btn-danger" onclick="stopEffectFromModal('${device}')">⏹ 停止</button>
                    </div>
                </div>
            </div>
        `;
    } else if (type === 'content') {
        // Matrix 内容模态框 (动画 + 图像 + QR码)
        const effectsHtml = deviceEffects.length > 0 
            ? deviceEffects.map(eff => {
                const isActive = eff === currentAnimation;
                const activeClass = isActive ? ' active' : '';
                return `<button class="btn effect-btn${activeClass}" onclick="selectEffectInModal('${device}', '${eff}', this)">${getEffectIcon(eff)} ${eff}</button>`;
            }).join('')
            : '<span class="empty">暂无可用特效</span>';
        
        return `
            <div class="modal-tabs">
                <button class="modal-tab active" onclick="switchModalTab(this, 'modal-tab-effect')">🎬 动画</button>
                <button class="modal-tab" onclick="switchModalTab(this, 'modal-tab-image')">📷 图像</button>
                <button class="modal-tab" onclick="switchModalTab(this, 'modal-tab-qr')">📱 QR码</button>
            </div>
            
            <!-- 动画 Tab -->
            <div class="modal-tab-content active" id="modal-tab-effect">
                <div class="effects-grid">${effectsHtml}</div>
                <div class="effect-config-modal" id="modal-effect-config-${device}" style="display:${currentAnimation ? 'flex' : 'none'};">
                    <span class="effect-name" id="modal-effect-name-${device}">${currentAnimation || '未选择'}</span>
                    <div class="config-row">
                        <label>速度</label>
                        <input type="range" min="1" max="100" value="${currentSpeed}" id="modal-effect-speed-${device}" 
                               oninput="document.getElementById('modal-speed-val-${device}').textContent=this.value">
                        <span id="modal-speed-val-${device}">${currentSpeed}</span>
                    </div>
                    <div class="config-row" id="modal-color-row-${device}" style="display:${colorSupportedEffects.includes(currentAnimation) ? 'flex' : 'none'};">
                        <label>颜色</label>
                        <input type="color" id="modal-effect-color-${device}" value="${colorHex}">
                    </div>
                    <div class="config-actions">
                        <button class="btn btn-primary" onclick="applyEffectFromModal('${device}')">▶ 启动</button>
                        <button class="btn btn-danger" onclick="stopEffectFromModal('${device}')">⏹ 停止</button>
                    </div>
                </div>
            </div>
            
            <!-- 图像 Tab -->
            <div class="modal-tab-content" id="modal-tab-image" style="display:none;">
                <div class="modal-section">
                    <div class="config-row">
                        <input type="text" id="modal-image-path" placeholder="/sdcard/images/..." class="input-flex" value="/sdcard/images/">
                        <button class="btn btn-sm" onclick="browseImages()">📁 浏览</button>
                    </div>
                    <div class="config-row">
                        <label><input type="checkbox" id="modal-image-center" checked> 居中显示</label>
                        <button class="btn btn-primary" onclick="displayImageFromModal()">显示图像</button>
                    </div>
                </div>
            </div>
            
            <!-- QR码 Tab -->
            <div class="modal-tab-content" id="modal-tab-qr" style="display:none;">
                <div class="modal-section">
                    <div class="config-row">
                        <input type="text" id="modal-qr-text" placeholder="输入文本或URL" class="input-flex">
                    </div>
                    <div class="config-row">
                        <label>纠错</label>
                        <select id="modal-qr-ecc">
                            <option value="L">L - 7%</option>
                            <option value="M" selected>M - 15%</option>
                            <option value="Q">Q - 25%</option>
                            <option value="H">H - 30%</option>
                        </select>
                        <label>前景色</label>
                        <input type="color" id="modal-qr-fg" value="#ffffff">
                    </div>
                    <div class="config-row">
                        <label>背景图</label>
                        <input type="text" id="modal-qr-bg-image" placeholder="无" readonly style="flex:1;cursor:pointer" onclick="openFilePickerFor('modal-qr-bg-image', '/sdcard/images')">
                        <button class="btn btn-sm" onclick="document.getElementById('modal-qr-bg-image').value=''" title="清除">✕</button>
                    </div>
                    <div class="config-row">
                        <button class="btn btn-primary" onclick="generateQrCodeFromModal()">生成 QR 码</button>
                    </div>
                </div>
            </div>
        `;
    } else if (type === 'text') {
        // Matrix 文本模态框
        return `
            <div class="modal-section">
                <h3>📝 文本显示</h3>
                <div class="config-row">
                    <input type="text" id="modal-text-content" placeholder="输入要显示的文本" class="input-flex">
                </div>
                <div class="config-row">
                    <label>字体</label>
                    <select id="modal-text-font">
                        <option value="default">默认</option>
                    </select>
                    <button class="btn btn-sm" onclick="loadFontListForModal()" title="刷新字体">🔄</button>
                </div>
                <div class="config-row">
                    <label>对齐</label>
                    <select id="modal-text-align">
                        <option value="left">左对齐</option>
                        <option value="center" selected>居中</option>
                        <option value="right">右对齐</option>
                    </select>
                    <label>颜色</label>
                    <input type="color" id="modal-text-color" value="#00ff00">
                </div>
                <div class="config-row">
                    <label>X</label>
                    <input type="number" id="modal-text-x" value="0" min="0" max="255" style="width:50px">
                    <label>Y</label>
                    <input type="number" id="modal-text-y" value="0" min="0" max="255" style="width:50px">
                    <label><input type="checkbox" id="modal-text-auto-pos" checked> 自动位置</label>
                </div>
                <div class="config-row">
                    <label>滚动</label>
                    <select id="modal-text-scroll">
                        <option value="none">无滚动</option>
                        <option value="left" selected>← 向左</option>
                        <option value="right">→ 向右</option>
                        <option value="up">↑ 向上</option>
                        <option value="down">↓ 向下</option>
                    </select>
                    <label>速度</label>
                    <input type="number" id="modal-text-speed" value="50" min="1" max="100" style="width:55px">
                </div>
                <div class="config-row">
                    <label><input type="checkbox" id="modal-text-loop" checked> 循环滚动</label>
                </div>
                <div class="config-actions">
                    <button class="btn btn-primary" onclick="displayTextFromModal()">▶ 显示</button>
                    <button class="btn btn-danger" onclick="stopTextFromModal()">⏹ 停止</button>
                </div>
            </div>
        `;
    } else if (type === 'filter') {
        // Matrix 滤镜模态框
        return `
            <div class="modal-section">
                <h3>🎨 后处理滤镜</h3>
                <div class="filters-grid">
                    <button class="btn filter-btn" data-filter="pulse" onclick="selectFilterInModal('pulse', this)">💓 脉冲</button>
                    <button class="btn filter-btn" data-filter="breathing" onclick="selectFilterInModal('breathing', this)">💨 呼吸</button>
                    <button class="btn filter-btn" data-filter="blink" onclick="selectFilterInModal('blink', this)">💡 闪烁</button>
                    <button class="btn filter-btn" data-filter="wave" onclick="selectFilterInModal('wave', this)">🌊 波浪</button>
                    <button class="btn filter-btn" data-filter="scanline" onclick="selectFilterInModal('scanline', this)">📺 扫描线</button>
                    <button class="btn filter-btn" data-filter="glitch" onclick="selectFilterInModal('glitch', this)">⚡ 故障艺术</button>
                    <button class="btn filter-btn" data-filter="invert" onclick="selectFilterInModal('invert', this)">🔄 反色</button>
                    <button class="btn filter-btn" data-filter="grayscale" onclick="selectFilterInModal('grayscale', this)">⬜ 灰度</button>
                </div>
                <div class="filter-config-modal" id="modal-filter-config" style="display:none;">
                    <span class="filter-name" id="modal-filter-name">未选择</span>
                    <div class="config-row">
                        <label>速度</label>
                        <input type="range" id="modal-filter-speed" min="1" max="100" value="50" style="flex:1"
                               oninput="document.getElementById('modal-filter-speed-val').textContent=this.value">
                        <span id="modal-filter-speed-val">50</span>
                    </div>
                </div>
                <div class="config-actions">
                    <button class="btn btn-primary" id="modal-apply-filter-btn" onclick="applyFilterFromModal()" disabled>▶ 应用</button>
                    <button class="btn btn-danger" onclick="stopFilterFromModal()">⏹ 停止</button>
                </div>
            </div>
        `;
    }
    return '<p>未知类型</p>';
}

// LED 模态框存储
let currentLedModal = { device: null, type: null };
let selectedModalFilter = null;

// 打开 LED 模态框
function openLedModal(device, type) {
    currentLedModal = { device, type };
    
    const titleMap = {
        'effect': `🎬 ${device} - 程序动画`,
        'content': `🎬 ${device} - 内容`,
        'text': `📝 ${device} - 文本显示`,
        'filter': `🎨 ${device} - 后处理滤镜`
    };
    
    const modal = document.getElementById('led-modal');
    const title = document.getElementById('led-modal-title');
    const body = document.getElementById('led-modal-body');
    
    title.textContent = titleMap[type] || `${device} - 设置`;
    body.innerHTML = generateLedModalContent(device, type);
    
    modal.classList.remove('hidden');
    
    // 加载字体列表（如果是文本模态框）
    if (type === 'text') {
        loadFontListForModal();
    }
}

// 关闭 LED 模态框
function closeLedModal() {
    const modal = document.getElementById('led-modal');
    modal.classList.add('hidden');
    currentLedModal = { device: null, type: null };
    selectedModalFilter = null;
}

// 模态框内 Tab 切换
function switchModalTab(btn, tabId) {
    btn.parentElement.querySelectorAll('.modal-tab').forEach(t => t.classList.remove('active'));
    btn.classList.add('active');
    
    const modal = btn.closest('.modal-content');
    modal.querySelectorAll('.modal-tab-content').forEach(c => {
        c.style.display = 'none';
        c.classList.remove('active');
    });
    
    const tab = document.getElementById(tabId);
    if (tab) {
        tab.style.display = 'block';
        tab.classList.add('active');
    }
}

// 模态框内选择特效
function selectEffectInModal(device, effect, btn) {
    selectedEffects[device] = effect;
    
    // 更新按钮状态
    btn.closest('.effects-grid, .modal-tab-content').querySelectorAll('.effect-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    
    // 显示特效名
    const effectName = document.getElementById(`modal-effect-name-${device}`);
    if (effectName) effectName.textContent = `${getEffectIcon(effect)} ${effect}`;
    
    // 显示/隐藏颜色选择器
    const colorRow = document.getElementById(`modal-color-row-${device}`);
    if (colorRow) {
        colorRow.style.display = colorSupportedEffects.includes(effect) ? 'flex' : 'none';
    }
    
    // 显示配置区
    const configEl = document.getElementById(`modal-effect-config-${device}`);
    if (configEl) configEl.style.display = 'flex';
}

// 模态框内应用特效
async function applyEffectFromModal(device) {
    const effect = selectedEffects[device];
    if (!effect) {
        showToast('请先选择一个特效', 'warning');
        return;
    }
    
    const speed = parseInt(document.getElementById(`modal-effect-speed-${device}`)?.value || '50');
    const color = document.getElementById(`modal-effect-color-${device}`)?.value || '#ff0000';
    
    try {
        const params = { speed };
        if (colorSupportedEffects.includes(effect)) {
            params.color = color;
        }
        await api.ledEffectStart(device, effect, params);
        
        ledStates[device] = true;
        updateLedCardState(device, true, effect);
        
        showToast(`${device}: ${effect} 已启动`, 'success');
    } catch (e) {
        showToast(`启动特效失败: ${e.message}`, 'error');
    }
}

// 模态框内停止特效
async function stopEffectFromModal(device) {
    try {
        await api.ledEffectStop(device);
        delete selectedEffects[device];
        updateLedCardState(device, ledStates[device], null);
        showToast(`${device} 特效已停止`, 'success');
    } catch (e) {
        showToast(`停止特效失败: ${e.message}`, 'error');
    }
}

// 模态框内显示图像
async function displayImageFromModal() {
    const path = document.getElementById('modal-image-path')?.value;
    const center = document.getElementById('modal-image-center')?.checked;
    
    if (!path) {
        showToast('请输入图像路径', 'warning');
        return;
    }
    
    try {
        await api.call('led.image', { device: 'matrix', path, center });
        showToast('图像已显示', 'success');
    } catch (e) {
        showToast(`显示图像失败: ${e.message}`, 'error');
    }
}

// 模态框内生成 QR 码
async function generateQrCodeFromModal() {
    const text = document.getElementById('modal-qr-text')?.value;
    const ecc = document.getElementById('modal-qr-ecc')?.value || 'M';
    const fg = document.getElementById('modal-qr-fg')?.value || '#ffffff';
    const bgImage = document.getElementById('modal-qr-bg-image')?.value || '';
    
    if (!text) {
        showToast('请输入要编码的文本', 'warning');
        return;
    }
    
    try {
        await api.call('led.qr', { device: 'matrix', text, ecc, fg_color: fg, bg_image: bgImage || undefined });
        showToast('QR 码已生成', 'success');
    } catch (e) {
        showToast(`生成 QR 码失败: ${e.message}`, 'error');
    }
}

// 加载字体列表（模态框版本）
async function loadFontListForModal() {
    try {
        const result = await api.call('led.fonts', {});
        const fonts = result.fonts || [];
        const select = document.getElementById('modal-text-font');
        if (select) {
            select.innerHTML = '<option value="default">默认</option>' + 
                fonts.map(f => `<option value="${f}">${f}</option>`).join('');
        }
    } catch (e) {
        console.error('加载字体失败:', e);
    }
}

// 模态框内显示文本
async function displayTextFromModal() {
    const text = document.getElementById('modal-text-content')?.value;
    const font = document.getElementById('modal-text-font')?.value || 'default';
    const align = document.getElementById('modal-text-align')?.value || 'center';
    const color = document.getElementById('modal-text-color')?.value || '#00ff00';
    const x = parseInt(document.getElementById('modal-text-x')?.value || '0');
    const y = parseInt(document.getElementById('modal-text-y')?.value || '0');
    const autoPos = document.getElementById('modal-text-auto-pos')?.checked;
    const scroll = document.getElementById('modal-text-scroll')?.value || 'none';
    const speed = parseInt(document.getElementById('modal-text-speed')?.value || '50');
    const loop = document.getElementById('modal-text-loop')?.checked;
    
    if (!text) {
        showToast('请输入要显示的文本', 'warning');
        return;
    }
    
    try {
        const params = {
            device: 'matrix',
            text,
            font: font !== 'default' ? font : undefined,
            align,
            color,
            scroll: scroll !== 'none' ? scroll : undefined,
            speed,
            loop
        };
        if (!autoPos) {
            params.x = x;
            params.y = y;
        }
        await api.call('led.text', params);
        showToast('文本已显示', 'success');
    } catch (e) {
        showToast(`显示文本失败: ${e.message}`, 'error');
    }
}

// 模态框内停止文本
async function stopTextFromModal() {
    try {
        await api.call('led.text_stop', { device: 'matrix' });
        showToast('文本滚动已停止', 'success');
    } catch (e) {
        showToast(`停止文本失败: ${e.message}`, 'error');
    }
}

// 模态框内选择滤镜
function selectFilterInModal(filter, btn) {
    selectedModalFilter = filter;
    
    btn.closest('.filters-grid').querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    
    const filterName = document.getElementById('modal-filter-name');
    if (filterName) filterName.textContent = filter;
    
    const filterConfig = document.getElementById('modal-filter-config');
    if (filterConfig) filterConfig.style.display = 'flex';
    
    const applyBtn = document.getElementById('modal-apply-filter-btn');
    if (applyBtn) applyBtn.disabled = false;
}

// 模态框内应用滤镜
async function applyFilterFromModal() {
    if (!selectedModalFilter) {
        showToast('请先选择一个滤镜', 'warning');
        return;
    }
    
    const speed = parseInt(document.getElementById('modal-filter-speed')?.value || '50');
    
    try {
        await api.call('led.filter', { device: 'matrix', filter: selectedModalFilter, speed });
        showToast(`滤镜 ${selectedModalFilter} 已应用`, 'success');
    } catch (e) {
        showToast(`应用滤镜失败: ${e.message}`, 'error');
    }
}

// 模态框内停止滤镜
async function stopFilterFromModal() {
    try {
        await api.call('led.filter_stop', { device: 'matrix' });
        showToast('滤镜已停止', 'success');
    } catch (e) {
        showToast(`停止滤镜失败: ${e.message}`, 'error');
    }
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

// 选择特效（旧版兼容，保留）
function selectEffect(device, effect, btn) {
    selectedEffects[device] = effect;
    
    // 更新按钮状态
    const panel = btn.closest('.led-panel');
    panel.querySelectorAll('.effect-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
}

// 旧版 showEffectConfig 保持兼容
function showEffectConfig(device, effect) {
    selectedEffects[device] = effect;
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
            const icon = btn.querySelector('.power-icon');
            if (icon) icon.textContent = '🔆';
        }
        
        // 更新顶部当前动画显示
        const currentAnim = document.getElementById(`current-anim-${device}`);
        if (currentAnim) currentAnim.textContent = `▶ ${effect}`;
        
        showToast(`${device}: ${effect} 已启动`, 'success');
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
    const isOn = ledStates[device] || false;
    
    try {
        if (isOn) {
            // 当前是开启状态，关闭它
            await api.ledClear(device);
            ledStates[device] = false;
            updateLedCardState(device, false);
            showToast(`${device} 已关闭`, 'success');
        } else {
            // 当前是关闭状态，开启它（白光）
            await api.ledFill(device, '#ffffff');
            ledStates[device] = true;
            updateLedCardState(device, true, null);
            showToast(`${device} 已开启`, 'success');
        }
    } catch (e) {
        showToast(`操作失败: ${e.message}`, 'error');
    }
}

async function ledOn(device, color = '#ffffff') {
    try {
        await api.ledFill(device, color);
        ledStates[device] = true;
        updateToggleButton(device, true);
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
        // 优先填充模态框中的路径，否则填充旧版元素
        const modalInput = document.getElementById('modal-image-path');
        const oldInput = document.getElementById('matrix-image-path');
        if (modalInput) {
            modalInput.value = path;
        } else if (oldInput) {
            oldInput.value = path;
        }
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
            <h1>🌐 网络配置</h1>
            
            <!-- 网络状态概览 -->
            <div class="net-overview">
                <div class="net-status-row">
                    <div class="net-iface" id="net-iface-eth">
                        <div class="iface-icon">🔌</div>
                        <div class="iface-info">
                            <div class="iface-name">以太网</div>
                            <div class="iface-status" id="eth-quick-status">-</div>
                        </div>
                        <div class="iface-ip" id="eth-quick-ip">-</div>
                    </div>
                    <div class="net-iface" id="net-iface-wifi">
                        <div class="iface-icon">📶</div>
                        <div class="iface-info">
                            <div class="iface-name">WiFi STA</div>
                            <div class="iface-status" id="wifi-quick-status">-</div>
                        </div>
                        <div class="iface-ip" id="wifi-quick-ip">-</div>
                    </div>
                    <div class="net-iface" id="net-iface-ap">
                        <div class="iface-icon">📻</div>
                        <div class="iface-info">
                            <div class="iface-name">WiFi AP</div>
                            <div class="iface-status" id="ap-quick-status">-</div>
                        </div>
                        <div class="iface-clients" id="ap-quick-clients">-</div>
                    </div>
                </div>
            </div>
            
            <!-- 主要配置区域 -->
            <div class="net-config-grid">
                <!-- 左侧：接口配置 -->
                <div class="net-panel">
                    <div class="panel-header">
                        <h3>🔧 接口配置</h3>
                        <div class="panel-tabs">
                            <button class="panel-tab active" onclick="switchNetTab('eth')">以太网</button>
                            <button class="panel-tab" onclick="switchNetTab('wifi')">WiFi</button>
                        </div>
                    </div>
                    
                    <!-- 以太网配置面板 -->
                    <div class="panel-content" id="net-tab-eth">
                        <div class="config-section">
                            <div class="config-row">
                                <span class="config-label">链路状态</span>
                                <span class="config-value" id="net-eth-link">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">IP 地址</span>
                                <span class="config-value mono" id="net-eth-ip">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">子网掩码</span>
                                <span class="config-value mono" id="net-eth-netmask">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">网关</span>
                                <span class="config-value mono" id="net-eth-gw">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">DNS</span>
                                <span class="config-value mono" id="net-eth-dns">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">MAC</span>
                                <span class="config-value mono small" id="net-eth-mac">-</span>
                            </div>
                        </div>
                    </div>
                    
                    <!-- WiFi 配置面板 -->
                    <div class="panel-content hidden" id="net-tab-wifi">
                        <div class="wifi-mode-selector">
                            <label>模式:</label>
                            <select id="wifi-mode-select" onchange="setWifiMode()">
                                <option value="off">关闭</option>
                                <option value="sta">站点 (STA)</option>
                                <option value="ap">热点 (AP)</option>
                                <option value="apsta">STA+AP</option>
                            </select>
                        </div>
                        
                        <!-- STA 信息 -->
                        <div class="config-section" id="wifi-sta-section">
                            <h4>📶 站点连接</h4>
                            <div class="config-row">
                                <span class="config-label">状态</span>
                                <span class="config-value" id="net-wifi-sta-status">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">SSID</span>
                                <span class="config-value" id="net-wifi-sta-ssid">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">IP</span>
                                <span class="config-value mono" id="net-wifi-sta-ip">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">信号</span>
                                <span class="config-value" id="net-wifi-sta-rssi">-</span>
                            </div>
                            <div class="wifi-sta-actions">
                                <button class="btn btn-sm" id="wifi-scan-btn" onclick="showWifiScan()">📡 扫描</button>
                                <button class="btn btn-sm btn-danger hidden" id="wifi-disconnect-btn" onclick="disconnectWifi()">断开</button>
                            </div>
                        </div>
                        
                        <!-- AP 信息 -->
                        <div class="config-section" id="wifi-ap-section">
                            <h4>📻 热点</h4>
                            <div class="config-row">
                                <span class="config-label">状态</span>
                                <span class="config-value" id="net-wifi-ap-status">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">SSID</span>
                                <span class="config-value" id="net-wifi-ap-ssid">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">IP</span>
                                <span class="config-value mono" id="net-wifi-ap-ip">-</span>
                            </div>
                            <div class="config-row">
                                <span class="config-label">接入数</span>
                                <span class="config-value" id="net-wifi-ap-sta-count">0</span>
                            </div>
                            <div class="wifi-ap-actions">
                                <button class="btn btn-sm" id="ap-config-btn" onclick="showApConfig()">⚙️ 配置</button>
                                <button class="btn btn-sm" id="ap-stations-btn" onclick="showApStations()">👥 设备</button>
                            </div>
                        </div>
                    </div>
                </div>
                
                <!-- 右侧：服务配置 -->
                <div class="net-panel">
                    <div class="panel-header">
                        <h3>🔀 网络服务</h3>
                    </div>
                    <div class="panel-content">
                        <!-- 主机名 -->
                        <div class="service-block">
                            <div class="service-header">
                                <span class="service-icon">🏷️</span>
                                <span class="service-name">主机名</span>
                                <span class="service-value" id="net-hostname">-</span>
                            </div>
                            <div class="service-config">
                                <input type="text" id="hostname-input" placeholder="新主机名" class="input-sm">
                                <button class="btn btn-sm" onclick="setHostname()">设置</button>
                            </div>
                        </div>
                        
                        <!-- DHCP 服务 -->
                        <div class="service-block">
                            <div class="service-header">
                                <span class="service-icon">🔄</span>
                                <span class="service-name">DHCP 服务器</span>
                                <span class="service-badge" id="dhcp-badge">-</span>
                            </div>
                            <div class="service-detail" id="dhcp-interfaces-list"></div>
                            <div class="service-actions">
                                <button class="btn btn-sm" onclick="showDhcpClients()">👥 客户端</button>
                            </div>
                        </div>
                        
                        <!-- NAT 网关 -->
                        <div class="service-block">
                            <div class="service-header">
                                <span class="service-icon">🌍</span>
                                <span class="service-name">NAT 网关</span>
                                <span class="service-badge" id="nat-badge">-</span>
                            </div>
                            <div class="service-detail">
                                <div class="nat-status-row">
                                    <span>WiFi:</span>
                                    <span id="net-nat-wifi">-</span>
                                    <span>ETH:</span>
                                    <span id="net-nat-eth">-</span>
                                </div>
                            </div>
                            <div class="service-actions">
                                <button class="btn btn-sm" id="nat-toggle-btn" onclick="toggleNat()">启用</button>
                                <button class="btn btn-sm" onclick="saveNatConfig()">💾 保存</button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            
            <!-- WiFi 扫描结果面板 -->
            <div class="net-section hidden" id="wifi-scan-section">
                <div class="section-header">
                    <h3>📡 WiFi 网络</h3>
                    <div class="section-actions">
                        <button class="btn btn-sm" onclick="showWifiScan()">🔄 刷新</button>
                        <button class="btn btn-sm" onclick="hideWifiScan()">✕ 关闭</button>
                    </div>
                </div>
                <div class="wifi-networks" id="wifi-scan-results"></div>
            </div>
            
            <!-- AP 接入设备面板 -->
            <div class="net-section hidden" id="ap-stations-section">
                <div class="section-header">
                    <h3>👥 热点接入设备</h3>
                    <button class="btn btn-sm" onclick="hideApStations()">✕ 关闭</button>
                </div>
                <div class="ap-stations-list" id="ap-stations-results"></div>
            </div>
            
            <!-- DHCP 客户端面板 -->
            <div class="net-section hidden" id="dhcp-clients-section">
                <div class="section-header">
                    <h3>👥 DHCP 客户端</h3>
                    <div class="section-actions">
                        <select id="dhcp-iface-select" class="select-sm" onchange="loadDhcpClients()">
                            <option value="ap">WiFi AP</option>
                            <option value="eth">Ethernet</option>
                        </select>
                        <button class="btn btn-sm" onclick="loadDhcpClients()">🔄</button>
                        <button class="btn btn-sm" onclick="hideDhcpClients()">✕</button>
                    </div>
                </div>
                <div class="dhcp-clients-list" id="dhcp-clients-results"></div>
            </div>
            
            <!-- AP 配置弹窗 -->
            <div class="modal hidden" id="ap-config-modal">
                <div class="modal-content modal-sm">
                    <div class="modal-header">
                        <h2>⚙️ WiFi 热点配置</h2>
                        <button class="modal-close" onclick="hideApConfig()">✕</button>
                    </div>
                    <div class="form-group">
                        <label>SSID</label>
                        <input type="text" id="ap-ssid-input" placeholder="TianShanOS">
                    </div>
                    <div class="form-group">
                        <label>密码 (留空=开放)</label>
                        <input type="password" id="ap-password-input" placeholder="至少 8 位">
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label>信道</label>
                            <select id="ap-channel-input">
                                <option value="1">1</option>
                                <option value="6" selected>6</option>
                                <option value="11">11</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label class="checkbox-label">
                                <input type="checkbox" id="ap-hidden-input">
                                隐藏 SSID
                            </label>
                        </div>
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

// 网络页面 Tab 切换
function switchNetTab(tab) {
    document.querySelectorAll('.panel-tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.panel-content').forEach(p => p.classList.add('hidden'));
    
    event.target.classList.add('active');
    document.getElementById('net-tab-' + tab).classList.remove('hidden');
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
            const ethConnected = eth.status === 'connected' || eth.link_up;
            
            // 概览区
            updateIfaceStatus('net-iface-eth', ethConnected);
            document.getElementById('eth-quick-status').textContent = ethConnected ? '已连接' : '未连接';
            document.getElementById('eth-quick-ip').textContent = eth.ip || '-';
            
            // 详细配置
            document.getElementById('net-eth-link').innerHTML = ethConnected ? 
                '<span class="status-dot green"></span>已连接' : '<span class="status-dot red"></span>未连接';
            document.getElementById('net-eth-ip').textContent = eth.ip || '-';
            document.getElementById('net-eth-netmask').textContent = eth.netmask || '-';
            document.getElementById('net-eth-gw').textContent = eth.gateway || '-';
            document.getElementById('net-eth-dns').textContent = eth.dns1 || '-';
            document.getElementById('net-eth-mac').textContent = eth.mac || '-';
            
            // WiFi STA
            const wifiSta = data.wifi_sta || {};
            const staConnected = wifiSta.connected || wifiSta.status === 'connected';
            
            updateIfaceStatus('net-iface-wifi', staConnected);
            document.getElementById('wifi-quick-status').textContent = staConnected ? '已连接' : '未连接';
            document.getElementById('wifi-quick-ip').textContent = wifiSta.ip || '-';
            
            document.getElementById('net-wifi-sta-status').innerHTML = staConnected ? 
                '<span class="status-dot green"></span>已连接' : '<span class="status-dot red"></span>未连接';
            document.getElementById('net-wifi-sta-ssid').textContent = wifiSta.ssid || '-';
            document.getElementById('net-wifi-sta-ip').textContent = wifiSta.ip || '-';
            document.getElementById('net-wifi-sta-rssi').textContent = wifiSta.rssi ? `${wifiSta.rssi} dBm ${getSignalBars(wifiSta.rssi)}` : '-';
            
            // 根据连接状态显示/隐藏断开按钮
            const disconnectBtn = document.getElementById('wifi-disconnect-btn');
            if (staConnected) {
                disconnectBtn.classList.remove('hidden');
            } else {
                disconnectBtn.classList.add('hidden');
            }
            
            // WiFi AP
            const wifiAp = data.wifi_ap || {};
            const apActive = wifiAp.status === 'connected' || wifiAp.active;
            const apClients = wifiAp.sta_count || 0;
            
            updateIfaceStatus('net-iface-ap', apActive);
            document.getElementById('ap-quick-status').textContent = apActive ? '运行中' : '未启用';
            document.getElementById('ap-quick-clients').textContent = apActive ? `${apClients} 设备` : '-';
            
            document.getElementById('net-wifi-ap-status').innerHTML = apActive ? 
                '<span class="status-dot green"></span>运行中' : '<span class="status-dot gray"></span>未启用';
            document.getElementById('net-wifi-ap-ssid').textContent = wifiAp.ssid || '-';
            document.getElementById('net-wifi-ap-ip').textContent = wifiAp.ip || '-';
            document.getElementById('net-wifi-ap-sta-count').textContent = apClients;
        }
    } catch (e) { console.log('Network status error:', e); }
    
    // WiFi 模式
    let currentWifiMode = 'off';
    try {
        const mode = await api.wifiMode();
        if (mode.data) {
            currentWifiMode = mode.data.mode || 'off';
            document.getElementById('wifi-mode-select').value = currentWifiMode;
            
            // 根据模式显示/隐藏相关区域
            const staSection = document.getElementById('wifi-sta-section');
            const apSection = document.getElementById('wifi-ap-section');
            const scanBtn = document.getElementById('wifi-scan-btn');
            const apConfigBtn = document.getElementById('ap-config-btn');
            const apStationsBtn = document.getElementById('ap-stations-btn');
            
            const canSta = (currentWifiMode === 'sta' || currentWifiMode === 'apsta');
            const canAp = (currentWifiMode === 'ap' || currentWifiMode === 'apsta');
            
            staSection.style.display = canSta ? 'block' : 'none';
            apSection.style.display = canAp ? 'block' : 'none';
            
            scanBtn.disabled = !canSta;
            apConfigBtn.disabled = !canAp;
            apStationsBtn.disabled = !canAp;
        }
    } catch (e) { console.log('WiFi mode error:', e); }
    
    // DHCP 状态
    try {
        const dhcp = await api.dhcpStatus();
        if (dhcp.data) {
            const container = document.getElementById('dhcp-interfaces-list');
            const badge = document.getElementById('dhcp-badge');
            
            if (dhcp.data.interfaces) {
                const runningCount = dhcp.data.interfaces.filter(i => i.running).length;
                badge.textContent = `${runningCount}/${dhcp.data.interfaces.length}`;
                badge.className = 'service-badge ' + (runningCount > 0 ? 'badge-ok' : 'badge-warn');
                
                container.innerHTML = dhcp.data.interfaces.map(iface => `
                    <div class="dhcp-iface-row">
                        <span class="status-dot ${iface.running ? 'green' : 'gray'}"></span>
                        <span class="iface-name">${iface.display_name || iface.interface}</span>
                        <span class="iface-detail">${iface.active_leases || 0} 租约</span>
                    </div>
                `).join('');
            } else {
                badge.textContent = dhcp.data.running ? '运行' : '停止';
                badge.className = 'service-badge ' + (dhcp.data.running ? 'badge-ok' : 'badge-warn');
                container.innerHTML = `<div class="dhcp-iface-row">
                    <span class="status-dot ${dhcp.data.running ? 'green' : 'gray'}"></span>
                    <span>${dhcp.data.active_leases || 0} 活跃租约</span>
                </div>`;
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
            
            const badge = document.getElementById('nat-badge');
            badge.textContent = enabled ? '运行' : '停止';
            badge.className = 'service-badge ' + (enabled ? 'badge-ok' : 'badge-warn');
            
            document.getElementById('net-nat-wifi').innerHTML = wifiConnected ? 
                '<span class="status-dot green"></span>✓' : '<span class="status-dot red"></span>✗';
            document.getElementById('net-nat-eth').innerHTML = ethUp ? 
                '<span class="status-dot green"></span>✓' : '<span class="status-dot red"></span>✗';
            
            // NAT 按钮
            const natToggleBtn = document.getElementById('nat-toggle-btn');
            natToggleBtn.textContent = enabled ? '禁用' : '启用';
            natToggleBtn.className = enabled ? 'btn btn-sm btn-danger' : 'btn btn-sm btn-success';
            
            const canToggle = enabled || (wifiConnected && ethUp);
            natToggleBtn.disabled = !canToggle;
        }
    } catch (e) { console.log('NAT error:', e); }
}

// 更新接口状态样式
function updateIfaceStatus(elementId, isActive) {
    const el = document.getElementById(elementId);
    if (el) {
        el.className = 'net-iface ' + (isActive ? 'active' : 'inactive');
    }
}

// 信号强度条
function getSignalBars(rssi) {
    if (rssi >= -50) return '████';
    if (rssi >= -60) return '███░';
    if (rssi >= -70) return '██░░';
    if (rssi >= -80) return '█░░░';
    return '░░░░';
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
    const container = document.getElementById('wifi-scan-results');
    
    section.classList.remove('hidden');
    container.innerHTML = '<div class="loading-inline">扫描中...</div>';
    
    try {
        const result = await api.wifiScan();
        if (result.data && result.data.networks) {
            if (result.data.networks.length === 0) {
                container.innerHTML = '<div class="empty-state">未发现网络</div>';
                return;
            }
            // 按信号强度排序
            const networks = result.data.networks.sort((a, b) => b.rssi - a.rssi);
            container.innerHTML = networks.map(net => `
                <div class="wifi-network-card" onclick="connectWifi('${escapeHtml(net.ssid)}')">
                    <div class="wifi-signal">${getSignalIcon(net.rssi)}</div>
                    <div class="wifi-info">
                        <div class="wifi-ssid">${escapeHtml(net.ssid) || '(隐藏网络)'}</div>
                        <div class="wifi-meta">
                            <span>${net.rssi} dBm</span>
                            <span>CH ${net.channel}</span>
                            <span>${net.auth || 'OPEN'}</span>
                        </div>
                    </div>
                    <button class="btn btn-sm btn-primary">连接</button>
                </div>
            `).join('');
        }
    } catch (e) {
        const errorMsg = e.message || '';
        if (errorMsg.includes('STA') || errorMsg.includes('APSTA') || errorMsg.includes('mode')) {
            container.innerHTML = `<div class="error-state">
                <div class="error-icon">⚠️</div>
                <div class="error-text">需要切换到 STA 或 APSTA 模式</div>
            </div>`;
        } else {
            container.innerHTML = `<div class="error-state">扫描失败: ${errorMsg}</div>`;
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
    const container = document.getElementById('ap-stations-results');
    
    section.classList.remove('hidden');
    container.innerHTML = '<div class="loading-inline">加载中...</div>';
    
    try {
        const result = await api.wifiApStations();
        if (result.data && result.data.stations) {
            if (result.data.stations.length === 0) {
                container.innerHTML = '<div class="empty-state">无接入设备</div>';
                return;
            }
            container.innerHTML = result.data.stations.map(sta => `
                <div class="device-card">
                    <div class="device-icon">📱</div>
                    <div class="device-info">
                        <div class="device-mac">${sta.mac}</div>
                        <div class="device-rssi">${sta.rssi} dBm</div>
                    </div>
                </div>
            `).join('');
        }
    } catch (e) {
        container.innerHTML = `<div class="error-state">获取失败: ${e.message}</div>`;
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
    const container = document.getElementById('dhcp-clients-results');
    
    container.innerHTML = '<div class="loading-inline">加载中...</div>';
    
    try {
        const result = await api.dhcpClients(iface);
        if (result.data && result.data.clients) {
            if (result.data.clients.length === 0) {
                container.innerHTML = '<div class="empty-state">无客户端</div>';
                return;
            }
            container.innerHTML = result.data.clients.map(client => `
                <div class="device-card">
                    <div class="device-icon">${client.is_static ? '📌' : '💻'}</div>
                    <div class="device-info">
                        <div class="device-ip">${client.ip}</div>
                        <div class="device-mac">${client.mac}</div>
                        ${client.hostname ? `<div class="device-hostname">${client.hostname}</div>` : ''}
                    </div>
                    <div class="device-badge">${client.is_static ? '静态' : '动态'}</div>
                </div>
            `).join('');
        }
    } catch (e) {
        container.innerHTML = `<div class="error-state">获取失败: ${e.message}</div>`;
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
            <h1>🖲️ 设备控制</h1>
            
            <div class="cards">
                <div class="card card-large">
                    <h3>🖥️ AGX</h3>
                    <div class="card-content">
                        <div class="device-status-grid">
                            <p><strong>电源状态:</strong> <span id="dev-agx-power" class="status-value">-</span></p>
                            <p><strong>CPU 使用率:</strong> <span id="dev-agx-cpu">-</span></p>
                            <p><strong>GPU 使用率:</strong> <span id="dev-agx-gpu">-</span></p>
                            <p><strong>温度:</strong> <span id="dev-agx-temp">-</span></p>
                        </div>
                    </div>
                    <div class="button-group">
                        <button class="btn btn-success" onclick="devicePower('agx', true)">⏻ 开机</button>
                        <button class="btn btn-danger" onclick="devicePower('agx', false)">⏼ 关机</button>
                        <button class="btn btn-warning" onclick="deviceReset('agx')">🔄 重启</button>
                        <button class="btn" onclick="deviceForceOff('agx')">⚡ 强制关机</button>
                    </div>
                </div>
                
                <div class="card card-large">
                    <h3>🔋 LPMU</h3>
                    <div class="card-content">
                        <div class="device-status-grid">
                            <p><strong>电源状态:</strong> <span id="dev-lpmu-power" class="status-value">-</span></p>
                        </div>
                    </div>
                    <div class="button-group">
                        <button class="btn btn-success" onclick="devicePower('lpmu', true)">⏻ 开机</button>
                        <button class="btn btn-danger" onclick="devicePower('lpmu', false)">⏼ 关机</button>
                    </div>
                </div>
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
            
            const agxPowerEl = document.getElementById('dev-agx-power');
            const lpmuPowerEl = document.getElementById('dev-lpmu-power');
            
            if (agxPowerEl) {
                agxPowerEl.textContent = agx?.powered ? '🟢 运行中' : '⚫ 关机';
                agxPowerEl.className = agx?.powered ? 'status-value status-on' : 'status-value status-off';
            }
            if (lpmuPowerEl) {
                lpmuPowerEl.textContent = lpmu?.powered ? '🟢 运行中' : '⚫ 关机';
                lpmuPowerEl.className = lpmu?.powered ? 'status-value status-on' : 'status-value status-off';
            }
        }
    } catch (e) { console.log('Device status error:', e); }
    
    // AGX 监控数据
    try {
        const agxData = await api.agxData();
        if (agxData.data) {
            const cpuEl = document.getElementById('dev-agx-cpu');
            const gpuEl = document.getElementById('dev-agx-gpu');
            const tempEl = document.getElementById('dev-agx-temp');
            
            if (cpuEl) cpuEl.textContent = agxData.data.cpu_usage ? `${agxData.data.cpu_usage}%` : '-';
            if (gpuEl) gpuEl.textContent = agxData.data.gpu_usage ? `${agxData.data.gpu_usage}%` : '-';
            if (tempEl) tempEl.textContent = agxData.data.temperature ? `${agxData.data.temperature}°C` : '-';
        }
    } catch (e) { /* AGX 可能未连接 */ }
}

async function devicePower(name, on) {
    try {
        await api.devicePower(name, on);
        showToast(`${name.toUpperCase()} ${on ? '开机' : '关机'} 命令已发送`, 'success');
        await refreshDevicePage();
    } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
}

async function deviceReset(name) {
    if (confirm(`确定要重启 ${name.toUpperCase()} 吗？`)) {
        try {
            await api.deviceReset(name);
            showToast(`${name.toUpperCase()} 重启命令已发送`, 'success');
        } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
    }
}

async function deviceForceOff(name) {
    if (confirm(`确定要强制关闭 ${name.toUpperCase()} 吗？这可能导致数据丢失！`)) {
        try {
            await api.deviceForceOff(name);
            showToast(`${name.toUpperCase()} 强制关机命令已发送`, 'success');
            await refreshDevicePage();
        } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
    }
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
                <div class="storage-controls" id="storage-controls">
                    <!-- 动态显示挂载/卸载按钮 -->
                </div>
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

// SD 卡挂载/卸载
async function mountSdCard() {
    try {
        showToast('正在挂载 SD 卡...', 'info');
        await api.storageMount();
        showToast('SD 卡挂载成功', 'success');
        await refreshFilesPage();
    } catch (e) {
        showToast('挂载失败: ' + e.message, 'error');
    }
}

async function unmountSdCard() {
    if (!confirm('确定要卸载 SD 卡吗？\n\n卸载后将无法访问 SD 卡上的文件。')) {
        return;
    }
    
    try {
        showToast('正在卸载 SD 卡...', 'info');
        await api.storageUnmount();
        showToast('SD 卡已卸载', 'success');
        // 如果当前在 SD 卡目录，切换到 SPIFFS
        if (currentFilePath.startsWith('/sdcard')) {
            currentFilePath = '/spiffs';
        }
        await refreshFilesPage();
    } catch (e) {
        showToast('卸载失败: ' + e.message, 'error');
    }
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
    
    // 如果是 SD 卡路径，先检查挂载状态，避免不必要的错误请求
    if (path.startsWith('/sdcard')) {
        try {
            const status = await api.storageStatus();
            if (!status.data?.sd?.mounted) {
                console.log('SD card not mounted, showing mount prompt');
                listContainer.innerHTML = `
                    <div class="unmounted-notice">
                        <div class="unmounted-icon">💾</div>
                        <div class="unmounted-text">SD 卡未挂载</div>
                        <button class="btn btn-success" onclick="mountSdCard()">挂载 SD 卡</button>
                    </div>
                `;
                updateBreadcrumb(path);
                return;
            }
        } catch (e) {
            console.warn('Failed to check storage status:', e.message);
            // 继续尝试加载目录，让后续逻辑处理错误
        }
    }
    
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
        
        // 检查是否是 SD 卡未挂载（后端返回 'SD card not mounted' 或 'Directory not found'）
        const isUnmounted = path.startsWith('/sdcard') && 
            (e.message.includes('not mounted') || e.message.includes('未挂载') || e.message.includes('Directory not found'));
        
        if (isUnmounted) {
            listContainer.innerHTML = `
                <div class="unmounted-notice">
                    <div class="unmounted-icon">💾</div>
                    <div class="unmounted-text">SD 卡未挂载</div>
                    <button class="btn btn-success" onclick="mountSdCard()">挂载 SD 卡</button>
                </div>
            `;
        } else {
            listContainer.innerHTML = `<div class="error">加载失败: ${e.message}</div>`;
        }
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
        const controlsContainer = document.getElementById('storage-controls');
        
        const sdMounted = status.data?.sd?.mounted;
        const spiffsMounted = status.data?.spiffs?.mounted;
        
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
        
        // 更新挂载/卸载按钮
        if (controlsContainer) {
            if (sdMounted) {
                controlsContainer.innerHTML = `
                    <button class="btn btn-sm btn-warning" onclick="unmountSdCard()" title="卸载 SD 卡">
                        ⏏️ 卸载 SD
                    </button>
                `;
            } else {
                controlsContainer.innerHTML = `
                    <button class="btn btn-sm btn-success" onclick="mountSdCard()" title="挂载 SD 卡">
                        💾 挂载 SD
                    </button>
                `;
            }
        }
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

// 模块描述信息
const CONFIG_MODULE_INFO = {
    net: { name: '网络', icon: '🌐', description: '以太网和主机名配置' },
    dhcp: { name: 'DHCP', icon: '📡', description: 'DHCP 服务器配置' },
    wifi: { name: 'WiFi', icon: '📶', description: 'WiFi AP 配置' },
    led: { name: 'LED', icon: '💡', description: 'LED 亮度和效果配置' },
    fan: { name: '风扇', icon: '🌀', description: '风扇控制配置' },
    device: { name: '设备', icon: '🖥️', description: 'AGX 设备控制配置' },
    system: { name: '系统', icon: '⚙️', description: '系统和控制台配置' }
};

// 配置项的用户友好描述
const CONFIG_KEY_LABELS = {
    // net
    'eth.enabled': { label: '以太网启用', type: 'bool' },
    'eth.dhcp': { label: 'DHCP 客户端', type: 'bool' },
    'eth.ip': { label: 'IP 地址', type: 'ip' },
    'eth.netmask': { label: '子网掩码', type: 'ip' },
    'eth.gateway': { label: '网关', type: 'ip' },
    'hostname': { label: '主机名', type: 'string' },
    // dhcp
    'enabled': { label: '启用', type: 'bool' },
    'start_ip': { label: '起始 IP', type: 'ip' },
    'end_ip': { label: '结束 IP', type: 'ip' },
    'lease_time': { label: '租约时间 (秒)', type: 'number' },
    // wifi
    'mode': { label: '模式', type: 'select', options: ['off', 'ap', 'sta', 'apsta'] },
    'ap.ssid': { label: 'AP SSID', type: 'string' },
    'ap.password': { label: 'AP 密码', type: 'password' },
    'ap.channel': { label: 'AP 信道', type: 'number', min: 1, max: 13 },
    'ap.max_conn': { label: '最大连接数', type: 'number', min: 1, max: 10 },
    'ap.hidden': { label: '隐藏 SSID', type: 'bool' },
    // led
    'brightness': { label: '亮度', type: 'number', min: 0, max: 255 },
    'effect_speed': { label: '效果速度', type: 'number', min: 1, max: 100 },
    'power_on_effect': { label: '开机效果', type: 'string' },
    'idle_effect': { label: '待机效果', type: 'string' },
    // fan
    'min_duty': { label: '最小占空比 (%)', type: 'number', min: 0, max: 100 },
    'max_duty': { label: '最大占空比 (%)', type: 'number', min: 0, max: 100 },
    'target_temp': { label: '目标温度 (°C)', type: 'number', min: 20, max: 80 },
    // device
    'agx.auto_power_on': { label: 'AGX 自动开机', type: 'bool' },
    'agx.power_on_delay': { label: '开机延迟 (ms)', type: 'number' },
    'agx.force_off_timeout': { label: '强制关机超时 (ms)', type: 'number' },
    'monitor.enabled': { label: '监控启用', type: 'bool' },
    'monitor.interval': { label: '监控间隔 (ms)', type: 'number' },
    // system
    'timezone': { label: '时区', type: 'string' },
    'log_level': { label: '日志级别', type: 'select', options: ['none', 'error', 'warn', 'info', 'debug', 'verbose'] },
    'console.enabled': { label: '控制台启用', type: 'bool' },
    'console.baudrate': { label: '波特率', type: 'select', options: [9600, 115200, 460800, 921600] },
    'webui.enabled': { label: 'WebUI 启用', type: 'bool' },
    'webui.port': { label: 'WebUI 端口', type: 'number', min: 1, max: 65535 }
};

async function loadConfigPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-config">
            <h1>⚙️ 系统配置</h1>
            
            <!-- 模块概览 -->
            <div class="section">
                <div class="section-header">
                    <h2>配置模块</h2>
                    <div class="section-actions">
                        <button class="btn btn-small" onclick="saveAllModules()">💾 保存全部</button>
                        <button class="btn btn-small" onclick="syncConfigToSd()">📤 同步到 SD 卡</button>
                    </div>
                </div>
                <div id="module-cards" class="module-cards">
                    <div class="loading">加载中...</div>
                </div>
            </div>
            
            <!-- 模块详情 -->
            <div class="section" id="module-detail-section" style="display:none">
                <div class="section-header">
                    <h2 id="module-detail-title">模块配置</h2>
                    <div class="section-actions">
                        <button class="btn btn-small" id="btn-save-module" onclick="saveCurrentModule()">💾 保存</button>
                        <button class="btn btn-small btn-danger" id="btn-reset-module" onclick="resetCurrentModule()">🔄 重置</button>
                    </div>
                </div>
                <div id="module-detail-content"></div>
            </div>
        </div>
    `;
    
    await loadModuleCards();
}

async function loadModuleCards() {
    const container = document.getElementById('module-cards');
    
    try {
        const result = await api.configModuleList();
        const modules = result.data?.modules || result.modules || [];
        
        if (modules.length === 0) {
            container.innerHTML = '<div class="empty">没有注册的配置模块</div>';
            return;
        }
        
        container.innerHTML = modules.map(mod => {
            const info = CONFIG_MODULE_INFO[mod.name] || { name: mod.name, icon: '📦', description: '' };
            const statusClass = mod.registered ? (mod.dirty ? 'dirty' : 'clean') : 'disabled';
            const statusText = !mod.registered ? '未注册' : (mod.dirty ? '有修改' : '已同步');
            const pendingBadge = mod.pending_sync ? '<span class="badge badge-warning">待同步</span>' : '';
            
            return `
                <div class="module-card ${statusClass}" onclick="showModuleDetail('${mod.name}')" ${!mod.registered ? 'style="opacity:0.5;pointer-events:none"' : ''}>
                    <div class="module-icon">${info.icon}</div>
                    <div class="module-info">
                        <div class="module-name">${info.name}</div>
                        <div class="module-desc">${info.description}</div>
                        <div class="module-status">
                            <span class="status-dot ${statusClass}"></span>
                            <span>${statusText}</span>
                            ${pendingBadge}
                        </div>
                    </div>
                </div>
            `;
        }).join('');
        
    } catch (e) {
        container.innerHTML = `<div class="error">加载失败: ${e.message}</div>`;
    }
}

// 当前选中的模块
let currentConfigModule = null;

async function showModuleDetail(moduleName) {
    currentConfigModule = moduleName;
    const info = CONFIG_MODULE_INFO[moduleName] || { name: moduleName, icon: '📦' };
    
    document.getElementById('module-detail-title').textContent = `${info.icon} ${info.name} 配置`;
    document.getElementById('module-detail-section').style.display = 'block';
    
    const contentDiv = document.getElementById('module-detail-content');
    contentDiv.innerHTML = '<div class="loading">加载中...</div>';
    
    try {
        const result = await api.configModuleShow(moduleName);
        const config = result.data?.config || result.config || {};
        const dirty = result.data?.dirty || result.dirty || false;
        
        // 生成配置表单
        const keys = Object.keys(config);
        if (keys.length === 0) {
            contentDiv.innerHTML = '<div class="empty">此模块暂无配置项</div>';
            return;
        }
        
        contentDiv.innerHTML = `
            <form id="module-config-form" class="config-form" onsubmit="return false;">
                <div class="config-grid">
                    ${keys.map(key => generateConfigInput(moduleName, key, config[key])).join('')}
                </div>
                ${dirty ? '<div class="form-note">⚠️ 有未保存的修改</div>' : ''}
            </form>
        `;
        
        // 滚动到详情区域
        document.getElementById('module-detail-section').scrollIntoView({ behavior: 'smooth' });
        
    } catch (e) {
        contentDiv.innerHTML = `<div class="error">加载失败: ${e.message}</div>`;
    }
}

function generateConfigInput(module, key, value) {
    const meta = CONFIG_KEY_LABELS[key] || { label: key, type: 'string' };
    const inputId = `cfg-${module}-${key.replace(/\./g, '-')}`;
    
    let inputHtml = '';
    
    switch (meta.type) {
        case 'bool':
            inputHtml = `
                <label class="toggle-switch">
                    <input type="checkbox" id="${inputId}" data-module="${module}" data-key="${key}" 
                           ${value ? 'checked' : ''} onchange="markModuleConfigChanged('${module}', '${key}', this.checked)">
                    <span class="toggle-slider"></span>
                </label>`;
            break;
            
        case 'select':
            inputHtml = `
                <select id="${inputId}" data-module="${module}" data-key="${key}" 
                        onchange="markModuleConfigChanged('${module}', '${key}', this.value)">
                    ${meta.options.map(opt => `<option value="${opt}" ${value == opt ? 'selected' : ''}>${opt}</option>`).join('')}
                </select>`;
            break;
            
        case 'number':
            const min = meta.min !== undefined ? `min="${meta.min}"` : '';
            const max = meta.max !== undefined ? `max="${meta.max}"` : '';
            inputHtml = `
                <input type="number" id="${inputId}" data-module="${module}" data-key="${key}" 
                       value="${value}" ${min} ${max}
                       onchange="markModuleConfigChanged('${module}', '${key}', parseInt(this.value))">`;
            break;
            
        case 'password':
            inputHtml = `
                <input type="password" id="${inputId}" data-module="${module}" data-key="${key}" 
                       value="${value}" autocomplete="new-password"
                       onchange="markModuleConfigChanged('${module}', '${key}', this.value)">`;
            break;
            
        case 'ip':
            inputHtml = `
                <input type="text" id="${inputId}" data-module="${module}" data-key="${key}" 
                       value="${value}" pattern="^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}$" 
                       placeholder="192.168.1.1"
                       onchange="markModuleConfigChanged('${module}', '${key}', this.value)">`;
            break;
            
        default: // string
            inputHtml = `
                <input type="text" id="${inputId}" data-module="${module}" data-key="${key}" 
                       value="${value}"
                       onchange="markModuleConfigChanged('${module}', '${key}', this.value)">`;
    }
    
    return `
        <div class="config-item">
            <label for="${inputId}">${meta.label}</label>
            ${inputHtml}
        </div>
    `;
}

// 待保存的修改
const pendingConfigChanges = {};

function markModuleConfigChanged(module, key, value) {
    if (!pendingConfigChanges[module]) {
        pendingConfigChanges[module] = {};
    }
    pendingConfigChanges[module][key] = value;
    
    // 更新保存按钮状态
    const saveBtn = document.getElementById('btn-save-module');
    if (saveBtn) {
        saveBtn.classList.add('btn-primary');
        saveBtn.textContent = '💾 保存 *';
    }
}

async function saveCurrentModule() {
    if (!currentConfigModule) return;
    
    const changes = pendingConfigChanges[currentConfigModule];
    if (!changes || Object.keys(changes).length === 0) {
        showToast('没有需要保存的修改', 'info');
        return;
    }
    
    try {
        // 先设置所有修改
        for (const [key, value] of Object.entries(changes)) {
            await api.configModuleSet(currentConfigModule, key, value);
        }
        
        // 然后保存到 NVS
        await api.configModuleSave(currentConfigModule);
        
        // 清除待保存的修改
        delete pendingConfigChanges[currentConfigModule];
        
        showToast(`${CONFIG_MODULE_INFO[currentConfigModule]?.name || currentConfigModule} 配置已保存`, 'success');
        
        // 刷新
        await loadModuleCards();
        await showModuleDetail(currentConfigModule);
        
    } catch (e) {
        showToast('保存失败: ' + e.message, 'error');
    }
}

async function resetCurrentModule() {
    if (!currentConfigModule) return;
    
    const info = CONFIG_MODULE_INFO[currentConfigModule] || { name: currentConfigModule };
    if (!confirm(`确定要重置 ${info.name} 模块的所有配置为默认值吗？`)) {
        return;
    }
    
    try {
        await api.configModuleReset(currentConfigModule, true);
        delete pendingConfigChanges[currentConfigModule];
        
        showToast(`${info.name} 配置已重置`, 'success');
        
        await loadModuleCards();
        await showModuleDetail(currentConfigModule);
        
    } catch (e) {
        showToast('重置失败: ' + e.message, 'error');
    }
}

async function saveAllModules() {
    try {
        const result = await api.configModuleSave();
        const data = result.data || result;
        
        if (data.fail_count > 0) {
            showToast(`保存完成，${data.success_count} 成功，${data.fail_count} 失败`, 'warning');
        } else {
            showToast(`已保存 ${data.success_count} 个模块`, 'success');
        }
        
        // 清除所有待保存修改
        Object.keys(pendingConfigChanges).forEach(k => delete pendingConfigChanges[k]);
        
        await loadModuleCards();
        
    } catch (e) {
        showToast('保存失败: ' + e.message, 'error');
    }
}

async function syncConfigToSd() {
    try {
        const result = await api.configSync();
        const data = result.data || result;
        
        if (data.synced) {
            showToast('配置已同步到 SD 卡', 'success');
        } else {
            showToast(data.message || '无需同步', 'info');
        }
        
        await loadModuleCards();
        
    } catch (e) {
        showToast('同步失败: ' + e.message, 'error');
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
// Config module functions
window.showModuleDetail = showModuleDetail;
window.saveCurrentModule = saveCurrentModule;
window.resetCurrentModule = resetCurrentModule;
window.saveAllModules = saveAllModules;
window.syncConfigToSd = syncConfigToSd;
window.markModuleConfigChanged = markModuleConfigChanged;
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
