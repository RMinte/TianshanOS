/**
 * TianshanOS Web App - Main Application
 */

// =========================================================================
//                         全局状态
// =========================================================================

let ws = null;
let refreshInterval = null;
let subscriptionManager = null;  // WebSocket 订阅管理器

// =========================================================================
//                         WebSocket 订阅管理器
// =========================================================================

class SubscriptionManager {
    constructor(ws) {
        this.ws = ws;
        this.subscriptions = new Map(); // topic -> Set(callbacks)
        this.activeSubs = new Set();    // 已激活的 topic
    }
    
    /**
     * 订阅主题
     * @param {string} topic - 主题名称 (system.info, device.status, ota.progress)
     * @param {function} callback - 数据回调函数
     * @param {object} params - 订阅参数 (interval 等)
     */
    subscribe(topic, callback, params = {}) {
        // 添加回调
        if (!this.subscriptions.has(topic)) {
            this.subscriptions.set(topic, new Set());
        }
        this.subscriptions.get(topic).add(callback);
        
        // 发送订阅消息（只在首次订阅时）
        if (!this.activeSubs.has(topic)) {
            this.ws.send({
                type: 'subscribe',
                topic: topic,
                params: params
            });
            this.activeSubs.add(topic);
            console.log(`[SubscriptionMgr] Subscribed to: ${topic}`, params);
        }
    }
    
    /**
     * 取消订阅
     * @param {string} topic - 主题名称
     * @param {function} callback - 回调函数（不传则移除所有）
     */
    unsubscribe(topic, callback = null) {
        if (!this.subscriptions.has(topic)) return;
        
        if (callback) {
            // 移除特定回调
            this.subscriptions.get(topic).delete(callback);
        } else {
            // 移除所有回调
            this.subscriptions.get(topic).clear();
        }
        
        // 如果没有回调了，发送取消订阅消息
        if (this.subscriptions.get(topic).size === 0) {
            this.subscriptions.delete(topic);
            if (this.activeSubs.has(topic)) {
                this.ws.send({
                    type: 'unsubscribe',
                    topic: topic
                });
                this.activeSubs.delete(topic);
                console.log(`[SubscriptionMgr] Unsubscribed from: ${topic}`);
            }
        }
    }
    
    /**
     * 处理 WebSocket 消息
     * @param {object} msg - WebSocket 消息
     */
    handleMessage(msg) {
        // 处理订阅确认
        if (msg.type === 'subscribed' || msg.type === 'unsubscribed') {
            const status = msg.success ? '✅' : '❌';
            console.log(`[SubscriptionMgr] ${msg.type}: ${msg.topic} ${status}`);
            if (!msg.success && msg.error) {
                console.error(`[SubscriptionMgr] Error: ${msg.error}`);
            }
            return;
        }
        
        // 分发数据到订阅回调
        if (msg.type === 'data' && msg.topic) {
            const callbacks = this.subscriptions.get(msg.topic);
            if (callbacks && callbacks.size > 0) {
                callbacks.forEach(cb => {
                    try {
                        cb(msg, msg.timestamp);
                    } catch (e) {
                        console.error(`[SubscriptionMgr] Callback error for ${msg.topic}:`, e);
                    }
                });
            }
            // 没有回调时静默丢弃（页面切换时的正常行为）
        }
    }
    
    /**
     * 清理所有订阅
     */
    clear() {
        this.activeSubs.forEach(topic => {
            this.ws.send({ type: 'unsubscribe', topic });
        });
        this.subscriptions.clear();
        this.activeSubs.clear();
        console.log('[SubscriptionMgr] Cleared all subscriptions');
    }
}

// =========================================================================
//                         初始化
// =========================================================================

document.addEventListener('DOMContentLoaded', () => {
    // 初始化认证 UI
    updateAuthUI();
    
    // 更新 Footer 版本号
    updateFooterVersion();
    
    // 注册路由（系统页面作为首页）
    router.register('/', loadSystemPage);
    router.register('/system', loadSystemPage);
    router.register('/network', loadNetworkPage);
    router.register('/ota', loadOtaPage);
    router.register('/files', loadFilesPage);
    // 日志页面已整合到终端页面模态框（重定向到终端并打开模态框）
    router.register('/logs', () => {
        window.location.hash = '#/terminal';
        setTimeout(() => {
            if (typeof showTerminalLogsModal === 'function') {
                showTerminalLogsModal();
            }
        }, 100);
    });
    router.register('/terminal', loadTerminalPage);
    router.register('/commands', loadCommandsPage);
    router.register('/security', loadSecurityPage);
    router.register('/automation', loadAutomationPage);
    
    // 启动 WebSocket
    setupWebSocket();
    
    // 全局键盘快捷键
    document.addEventListener('keydown', (e) => {
        // Esc 键取消 SSH 命令执行
        if (e.key === 'Escape' && typeof currentExecSessionId !== 'undefined' && currentExecSessionId) {
            e.preventDefault();
            cancelExecution();
        }
    });
});

// =========================================================================
//                         认证
// =========================================================================

function updateAuthUI() {
    const loginBtn = document.getElementById('login-btn');
    const userName = document.getElementById('user-name');
    
    if (api.isLoggedIn()) {
        const username = api.getUsername();
        const level = api.getLevel();
        const levelIcon = 'ri-user-line'; // 统一使用人形图标
        
        loginBtn.textContent = '登出';
        loginBtn.classList.add('btn-service-style');
        userName.innerHTML = `<i class="${levelIcon}"></i> ${username}`;
        userName.title = `权限级别: ${level}`;
        loginBtn.onclick = logout;
        
        // 更新导航菜单可见性
        router.updateNavVisibility();
    } else {
        loginBtn.textContent = '登录';
        loginBtn.classList.remove('btn-service-style');
        userName.textContent = '未登录';
        userName.title = '';
        loginBtn.onclick = showLoginModal;
        
        // 隐藏需要权限的导航项
        router.updateNavVisibility();
    }
}

function showLoginModal() {
    document.getElementById('login-modal').classList.remove('hidden');
    // 聚焦用户名输入框
    setTimeout(() => document.getElementById('username')?.focus(), 100);
}

function closeLoginModal() {
    document.getElementById('login-modal').classList.add('hidden');
    document.getElementById('login-form').reset();
    document.getElementById('login-error')?.classList.add('hidden');
}

document.getElementById('login-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    const errorEl = document.getElementById('login-error');
    const submitBtn = e.target.querySelector('button[type="submit"]');
    
    // 显示加载状态
    submitBtn.disabled = true;
    submitBtn.textContent = '登录中...';
    errorEl?.classList.add('hidden');
    
    try {
        const result = await api.login(username, password);
        
        if (result.code === 0) {
            closeLoginModal();
            updateAuthUI();
            
            // 检查是否需要修改密码
            if (!result.data.password_changed) {
                showPasswordChangeReminder();
            }
            
            router.navigate();
            showToast(`欢迎, ${username}!`, 'success');
        } else {
            // 显示错误信息
            if (errorEl) {
                errorEl.textContent = result.message || '登录失败';
                errorEl.classList.remove('hidden');
            }
            showToast(result.message || '登录失败', 'error');
        }
    } catch (error) {
        if (errorEl) {
            errorEl.textContent = error.message || '网络错误';
            errorEl.classList.remove('hidden');
        }
        showToast('登录失败: ' + error.message, 'error');
    } finally {
        submitBtn.disabled = false;
        submitBtn.textContent = '登录';
    }
});

async function logout() {
    try {
        await api.logout();
        showToast('已登出', 'info');
    } finally {
        updateAuthUI();
        window.location.hash = '/';  // 重定向到首页
        router.navigate();
    }
}

/**
 * 显示修改密码提醒
 */
function showPasswordChangeReminder() {
    const modal = document.createElement('div');
    modal.id = 'password-change-modal';
    modal.className = 'modal show';
    modal.innerHTML = `
        <div class="modal-content" style="max-width:400px;">
            <div class="modal-header">
                <h3>安全提醒</h3>
            </div>
            <div class="modal-body">
                <p style="margin-bottom:16px;">您正在使用默认密码，建议立即修改以确保系统安全。</p>
                <form id="change-password-form">
                    <div class="form-group">
                        <label>当前密码</label>
                        <input type="password" id="change-old-pwd" class="input" required>
                    </div>
                    <div class="form-group">
                        <label>新密码 (4-64字符)</label>
                        <input type="password" id="change-new-pwd" class="input" minlength="4" maxlength="64" required>
                    </div>
                    <div class="form-group">
                        <label>确认新密码</label>
                        <input type="password" id="change-confirm-pwd" class="input" minlength="4" maxlength="64" required>
                    </div>
                    <div id="change-pwd-error" class="form-error hidden"></div>
                </form>
            </div>
            <div class="modal-footer">
                <button class="btn" onclick="closePasswordChangeModal()">稍后修改</button>
                <button class="btn btn-primary" onclick="submitPasswordChange()">立即修改</button>
            </div>
        </div>
    `;
    document.body.appendChild(modal);
}

function closePasswordChangeModal() {
    const modal = document.getElementById('password-change-modal');
    if (modal) modal.remove();
}

async function submitPasswordChange() {
    const oldPwd = document.getElementById('change-old-pwd').value;
    const newPwd = document.getElementById('change-new-pwd').value;
    const confirmPwd = document.getElementById('change-confirm-pwd').value;
    const errorEl = document.getElementById('change-pwd-error');
    
    // 验证
    if (newPwd !== confirmPwd) {
        errorEl.textContent = '两次输入的新密码不一致';
        errorEl.classList.remove('hidden');
        return;
    }
    
    if (newPwd.length < 4) {
        errorEl.textContent = '新密码至少4个字符';
        errorEl.classList.remove('hidden');
        return;
    }
    
    try {
        const result = await api.changePassword(oldPwd, newPwd);
        if (result.code === 0) {
            closePasswordChangeModal();
            showToast('密码修改成功！', 'success');
        } else {
            errorEl.textContent = result.message || '修改失败';
            errorEl.classList.remove('hidden');
        }
    } catch (error) {
        errorEl.textContent = error.message || '网络错误';
        errorEl.classList.remove('hidden');
    }
}

// =========================================================================
//                         Footer 版本号更新
// =========================================================================

async function updateFooterVersion() {
    try {
        const versionData = await api.call('ota.version');
        if (versionData?.data?.version) {
            const versionEl = document.getElementById('footer-version');
            if (versionEl) {
                versionEl.textContent = 'v' + versionData.data.version;
            }
        }
    } catch (error) {
        console.log('Failed to fetch version:', error);
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
    
    // 初始化订阅管理器
    subscriptionManager = new SubscriptionManager(ws);
    
    // 暴露给全局，供日志页面使用
    window.ws = ws;
    window.subscriptionManager = subscriptionManager;
}

function handleEvent(msg) {
    // console.log('Event:', msg);
    
    // 处理订阅管理器消息 (subscribed/unsubscribed/data)
    if (subscriptionManager && (msg.type === 'subscribed' || msg.type === 'unsubscribed' || msg.type === 'data')) {
        subscriptionManager.handleMessage(msg);
        return;
    }
    
    // 处理日志消息
    if (msg.type === 'log') {
        console.log('[Debug] Received log message, type:', msg.type);
        
        // 日志页面处理
        if (typeof window.handleLogMessage === 'function') {
            window.handleLogMessage(msg);
        }
        
        // 模态框实时日志处理
        const modal = document.getElementById('terminal-logs-modal');
        console.log('[Debug] Modal check - exists:', !!modal, 'display:', modal?.style.display);
        
        if (modal && modal.style.display === 'flex') {
            console.log('[Debug] Modal is visible, calling handleModalLogMessage');
            if (typeof window.handleModalLogMessage === 'function') {
                window.handleModalLogMessage(msg);
            } else {
                console.error('[Debug] handleModalLogMessage function not found!');
            }
        } else {
            console.log('[Debug] Modal not visible or not found');
        }
        return;
    }
    
    // 处理日志订阅确认
    if (msg.type === 'log_subscribed') {
        if (typeof window.updateWsStatus === 'function') {
            window.updateWsStatus(true);
        }
        return;
    }
    
    // 处理历史日志响应
    if (msg.type === 'log_history') {
        const logs = msg.logs || [];
        
        // 日志页面
        if (typeof window.logEntries !== 'undefined') {
            window.logEntries = logs;
            if (typeof window.renderFilteredLogs === 'function') {
                window.renderFilteredLogs();
            }
            showToast(`加载了 ${logs.length} 条历史日志`, 'success');
        }
        
        // 终端页面的日志模态框
        const modal = document.getElementById('terminal-logs-modal');
        console.log('[Modal] log_history received, modal:', modal, 'display:', modal?.style.display);
        console.log('[Modal] logs count:', logs.length);
        
        if (modal && modal.style.display === 'flex') {
            console.log('[Modal] Updating modalLogEntries...');
            modalLogEntries.length = 0;  // 清空数组但保持引用
            modalLogEntries.push(...logs.map(log => ({
                level: log.level || 3,
                levelName: getLevelName(log.level || 3),
                tag: log.tag || 'unknown',
                message: log.message || '',
                timestamp: log.timestamp || Date.now(),
                task: log.task || ''
            })));
            console.log('[Modal] After push, modalLogEntries length:', modalLogEntries.length);
            renderModalLogs();
        } else {
            console.log('[Modal] Modal not visible, skipping update');
        }
        
        return;
    }
    
    if (msg.type === 'event') {
        // 刷新相关页面数据
        if (router.currentPage) {
            router.currentPage();
        }
    }
    
    // 处理电压保护事件
    if (msg.type === 'power_event') {
        handlePowerEvent(msg);
    }
    
    // 处理 SSH Exec 流式输出消息
    if (msg.type && msg.type.startsWith('ssh_exec_')) {
        handleSshExecMessage(msg);
    }
}

// 处理电压保护事件
function handlePowerEvent(msg) {
    const state = msg.state;
    const voltage = msg.voltage?.toFixed(2) || '?';
    const countdown = msg.countdown || 0;
    
    // 显示警告
    if (state === 'LOW_VOLTAGE' || state === 'SHUTDOWN') {
        showToast(`⚠️ 低电压警告: ${voltage}V (${countdown}s)`, 'warning', 5000);
    } else if (state === 'PROTECTED') {
        showToast(`🛡️ 电压保护已触发`, 'error', 10000);
    } else if (state === 'RECOVERY') {
        showToast(`🔄 电压恢复中: ${voltage}V`, 'info', 3000);
    }
}

// =========================================================================
//                         系统页面（合并原首页+系统）
// =========================================================================

async function loadSystemPage() {
    clearInterval(refreshInterval);
    
    // 取消之前的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');  // 取消聚合订阅
        subscriptionManager.unsubscribe('system.memory');
        subscriptionManager.unsubscribe('system.cpu');
        subscriptionManager.unsubscribe('network.status');
        subscriptionManager.unsubscribe('power.status');
        subscriptionManager.unsubscribe('fan.status');
        subscriptionManager.unsubscribe('service.list');
    }
    
    // 停止 uptime 计算
    if (window.systemUptimeInterval) {
        clearInterval(window.systemUptimeInterval);
        window.systemUptimeInterval = null;
    }
    
    // 停止服务状态刷新（切换页面时会重新启动）
    stopServiceStatusRefresh();
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-system">
            <!-- 紧凑式系统概览 -->
            <div class="cards">
                <!-- 资源监控 (标题栏含服务状态) - 放首位，高频被动观察 -->
                <div class="card">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
                        <h3 style="margin:0">资源监控</h3>
                        <div onclick="showServicesModal()" style="cursor:pointer;font-size:0.9em;color:#666;padding:4px 12px;border-radius:4px;background:#f0f8ff">
                            <i class="ri-service-line"></i> 服务 <span id="services-running" style="color:#666;font-weight:bold">-</span>/<span id="services-total" style="color:#666">-</span>
                        </div>
                    </div>
                    <div class="card-content" style="display:flex;gap:20px">
                        <div style="flex:1">
                            <p><strong>CPU</strong></p>
                            <div id="cpu-cores" style="margin-top:5px">
                                <div class="loading-small">加载中...</div>
                            </div>
                        </div>
                        <div style="flex:1;border-left:1px solid #e0e0e0;padding-left:20px">
                            <div style="display:flex;justify-content:space-between;align-items:center">
                                <p><strong>内存</strong></p>
                                <button class="btn btn-sm" onclick="showMemoryDetailModal()" style="font-size:0.75em;padding:2px 8px" title="查看详细内存分析">详情</button>
                            </div>
                            <div style="margin-top:5px">
                                <p style="font-size:0.85em;margin:3px 0">DRAM:</p>
                                <div class="progress-bar" style="height:12px"><div class="progress" id="heap-progress"></div></div>
                                <p style="font-size:0.8em;margin:2px 0" id="heap-text">-</p>
                                <p style="font-size:0.85em;margin:8px 0 3px">PSRAM:</p>
                                <div class="progress-bar" style="height:12px"><div class="progress" id="psram-progress"></div></div>
                                <p style="font-size:0.8em;margin:2px 0" id="psram-text">-</p>
                            </div>
                        </div>
                    </div>
                </div>
                
                <!-- 系统总览 (包含电源) - 第二位，操作按钮在右手热区 -->
                <div class="card">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
                        <h3 style="margin:0">系统总览</h3>
                        <div style="display:flex;gap:8px">
                            <button class="btn btn-small" onclick="showShutdownSettingsModal()" style="font-size:0.85em;color:#666" title="电压保护设置"><i class="ri-flashlight-line"></i> 关机设置</button>
                            <button id="usb-mux-btn" class="btn btn-small" onclick="toggleUsbMux()" style="font-size:0.85em;color:#666"><i class="ri-usb-line"></i> USB: <span id="usb-mux-target">-</span></button>
                            <button class="btn btn-small btn-service-style" onclick="confirmReboot()" style="font-size:0.85em"><i class="ri-restart-line"></i> 重启</button>
                        </div>
                    </div>
                    <div class="card-content" style="display:flex;gap:20px">
                        <div style="flex:1">
                            <p style="font-size:0.9em;color:#888;margin-bottom:5px">系统信息</p>
                            <p><strong>芯片:</strong> <span id="sys-chip">-</span></p>
                            <p><strong>固件:</strong> <span id="sys-version">-</span> / <span id="sys-idf" style="font-size:0.85em;color:#888">-</span></p>
                            <p><strong>运行:</strong> <span id="sys-uptime">-</span></p>
                            <p style="font-size:0.8em;color:#888;margin-top:5px" id="sys-compile">-</p>
                        </div>
                        <div style="flex:1;border-left:1px solid #e0e0e0;padding-left:20px">
                            <p style="font-size:0.9em;color:#888;margin-bottom:5px">电源状态</p>
                            <p><strong>输入:</strong> <span id="voltage">-</span> <span style="font-size:0.85em;color:#888">/ 内部 <span id="internal-voltage">-</span></span></p>
                            <p><strong>电流:</strong> <span id="current">-</span></p>
                            <p><strong>功率:</strong> <span id="power-watts">-</span></p>
                            <p><strong>保护:</strong> <span id="protection-status">-</span></p>
                        </div>
                    </div>
                </div>
                
                <!-- 网络 & 时间 -->
                <div class="card">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
                        <h3 style="margin:0">网络 & 时间</h3>
                        <button class="btn btn-small btn-service-style" onclick="router.navigate('/ota')" style="font-size:0.85em"><i class="ri-download-cloud-line"></i> OTA</button>
                    </div>
                    <div class="card-content" style="display:flex;gap:20px">
                        <div style="flex:1">
                            <p style="font-size:0.9em;color:#888;margin-bottom:5px">网络连接</p>
                            <p><strong>以太网:</strong> <span id="eth-status">-</span></p>
                            <p><strong>WiFi:</strong> <span id="wifi-status">-</span></p>
                            <p><strong>IP:</strong> <span id="ip-addr" style="font-size:0.9em">-</span></p>
                        </div>
                        <div style="flex:1;border-left:1px solid #e0e0e0;padding-left:20px">
                            <p style="font-size:0.9em;color:#888;margin-bottom:5px">时间同步</p>
                            <p><strong>当前:</strong> <span id="sys-datetime" style="font-size:0.9em">-</span></p>
                            <p><strong>状态:</strong> <span id="sys-time-status">-</span> <span style="font-size:0.85em;color:#888">(<span id="sys-time-source">-</span>)</span></p>
                            <p><strong>时区:</strong> <span id="sys-timezone">-</span></p>
                            <div style="margin-top:8px;display:flex;gap:5px">
                                <button class="btn btn-small" onclick="syncTimeFromBrowser()" style="font-size:0.85em;padding:4px 8px;color:#666"><i class="ri-refresh-line"></i> 同步</button>
                                <button class="btn btn-small" onclick="showTimezoneModal()" style="font-size:0.85em;padding:4px 8px;color:#666"><i class="ri-time-line"></i> 时区</button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            
            <!-- 设备面板 + 风扇控制 并排 -->
            <div class="panel-row">
                <!-- 设备面板 -->
                <div class="section device-panel-section">
                    <div class="section-header">
                        <h2>设备面板</h2>
                        <div class="section-actions">
                            <button id="agx-power-btn" class="btn btn-sm btn-danger" onclick="toggleAgxPower()">🔴 AGX 已关闭</button>
                            <button id="lpmu-power-btn" class="btn btn-sm btn-warning" onclick="toggleLpmuPower()">⚠️ LPMU 检测中</button>
                            <button class="btn btn-sm btn-service-style" onclick="showWidgetManager()"><i class="ri-apps-line"></i> 组件管理</button>
                        </div>
                    </div>
                    <!-- 快捷操作区域 -->
                    <div id="quick-actions-grid" class="quick-actions-grid">
                        <div class="loading-inline">加载中...</div>
                    </div>
                    <!-- 分隔线 -->
                    <div class="device-panel-divider"></div>
                    <!-- 数据监控区域 -->
                    <div id="data-widgets-grid" class="data-widgets-grid">
                        <!-- 动态生成的组件 -->
                    </div>
                    <div id="data-widgets-empty" class="data-widgets-empty" style="display:none;">
                        <div class="empty-icon"><i class="ri-box-3-line"></i></div>
                        <p>还没有添加数据组件</p>
                        <button class="btn btn-service-style" onclick="showWidgetManager()">打开管理面板</button>
                    </div>
                </div>
                
                <!-- 风扇控制 -->
                <div class="section fan-control-section">
                    <div class="section-header">
                        <h2>风扇控制</h2>
                        <div class="section-actions">
                            <button class="btn btn-sm" onclick="refreshFans()" style="color:#666"><i class="ri-refresh-line"></i></button>
                            <button class="btn btn-sm" onclick="showFanCurveModal()" style="color:#666"><i class="ri-line-chart-line"></i> 曲线</button>
                        </div>
                    </div>
                    <!-- 温度状态栏 -->
                    <div class="fan-temp-status-bar" id="fan-temp-status-bar">
                        <div class="temp-status-item">
                            <span class="temp-label"><i class="ri-temp-hot-line"></i> 有效温度</span>
                            <span class="temp-value" id="fan-global-temp">--°C</span>
                        </div>
                        <div class="temp-status-item">
                            <span class="temp-label"><i class="ri-dashboard-3-line"></i> 目标转速</span>
                            <span class="temp-value" id="fan-global-duty">--%</span>
                        </div>
                        <div class="temp-status-item test-temp-control">
                            <span class="temp-label"><i class="ri-scan-line"></i> 测试温度</span>
                            <div class="test-temp-input-wrap">
                                <input type="number" id="fan-test-temp" class="input input-sm" 
                                       placeholder="--" min="0" max="100" step="1" style="width:60px;">
                                <button class="btn btn-sm btn-warning" onclick="applyTestTemp()">测试</button>
                                <button class="btn btn-sm" onclick="clearTestTemp()">清除</button>
                            </div>
                        </div>
                    </div>
                    <div class="fans-grid" id="fans-grid">
                        <div class="loading">加载中...</div>
                    </div>
                </div>
            </div>
            
            <!-- LED 控制 -->
            <div class="section">
                <div class="led-page-header">
                    <h2>LED 控制</h2>
                    <div class="led-quick-actions">
                        <button class="btn btn-sm" onclick="refreshSystemLeds()">🔄 刷新</button>
                        <button class="btn btn-sm" onclick="allLedsOff()">⏹ 全部关闭</button>
                    </div>
                </div>
                <div id="system-led-devices-grid" class="led-devices-grid">
                    <div class="loading-inline">加载设备中...</div>
                </div>
            </div>
        </div>
        
        <!-- 服务详情模态框 -->
        <div id="services-modal" class="modal hidden">
            <div class="modal-content" style="max-width:900px">
                <div class="modal-header">
                    <h2>服务状态</h2>
                    <button class="modal-close" onclick="hideServicesModal()">&times;</button>
                </div>
                <div class="modal-body">
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
        </div>
    `;
    
    // 初始加载
    await refreshSystemPageOnce();
    
    // 加载数据监控面板
    await initDataWidgets();
    
    // 订阅 WebSocket 实时更新 - 使用聚合订阅（system.dashboard）
    if (subscriptionManager) {
        subscriptionManager.subscribe('system.dashboard', (msg) => {
            console.log('[System Page] Received dashboard:', msg);
            if (!msg.data) return;
            
            const data = msg.data;
            
            // 分发到各个更新函数
            if (data.info) updateSystemInfo(data.info);
            if (data.memory) updateMemoryInfo(data.memory);
            if (data.cpu) updateCpuInfo(data.cpu);
            if (data.network) updateNetworkInfo(data.network);
            if (data.power) updatePowerInfo(data.power);
            if (data.fan) updateFanInfo(data.fan);
            if (data.services) updateServiceList(data.services);
        }, { interval: 1000 });  // 1秒更新所有数据
    }
    
    // 启动浏览器本地时间更新定时器
    startLocalTimeUpdate();
    
    // 启动设备状态实时监控
    startDeviceStateMonitor();
}

// 单次刷新（初始加载）
async function refreshSystemPageOnce() {
    // 系统信息
    try {
        const info = await api.getSystemInfo();
        if (info.data) {
            updateSystemInfo(info.data);
        }
    } catch (e) { console.log('System info error:', e); }
    
    // 时间信息
    try {
        const time = await api.timeInfo();
        if (time.data) {
            updateTimeInfo(time.data);
        }
    } catch (e) { console.log('Time info error:', e); }
    
    // 内存
    try {
        const mem = await api.getMemoryInfo();
        if (mem.data) {
            updateMemoryInfo(mem.data);
        }
    } catch (e) { console.log('Memory info error:', e); }
    
    // 网络
    try {
        const netStatus = await api.networkStatus();
        if (netStatus.data) {
            updateNetworkInfo(netStatus.data);
        }
    } catch (e) {
        document.getElementById('eth-status').textContent = '-';
        document.getElementById('wifi-status').textContent = '-';
    }
    
    // 电源
    try {
        const powerStatus = await api.powerStatus();
        if (powerStatus.data) {
            updatePowerInfo(powerStatus.data);
        }
        const protStatus = await api.powerProtectionStatus();
        if (protStatus.data) {
            const running = protStatus.data.running || protStatus.data.initialized;
            document.getElementById('protection-status').textContent = 
                running ? '已启用' : '已禁用';
        }
    } catch (e) { 
        document.getElementById('voltage').textContent = '-'; 
        document.getElementById('current').textContent = '-'; 
        document.getElementById('power-watts').textContent = '-'; 
    }
    
    // 风扇
    try {
        const fans = await api.fanStatus();
        updateFanInfo(fans.data);
    } catch (e) { 
        document.getElementById('fans-grid').innerHTML = '<p class="text-muted">风扇状态不可用</p>';
    }
    
    // 服务列表
    try {
        const services = await api.serviceList();
        updateServiceList(services.data);
    } catch (e) {
        console.log('Services error:', e);
    }
    
    // LED 设备
    await refreshSystemLeds();
    
    // 快捷操作
    await refreshQuickActions();
    
    // USB Mux 状态
    await refreshUsbMuxStatus();
    
    // AGX 电源状态
    await refreshAgxPowerState();
    
    // LPMU 状态检测
    await refreshLpmuState();
}

// =========================================================================
// USB Mux 状态和切换 (支持 ESP32 / AGX / LPMU 三设备循环)
// =========================================================================
let usbMuxTarget = 'esp32';
let usbMuxConfigured = false;

const USB_MUX_TARGETS = ['esp32', 'agx', 'lpmu'];
const USB_MUX_DISPLAY = { 'esp32': 'ESP', 'agx': 'AGX', 'lpmu': 'LPMU' };
const USB_MUX_COLORS = { 'esp32': '', 'agx': 'btn-primary', 'lpmu': 'btn-success' };

async function refreshUsbMuxStatus() {
    try {
        const result = await api.call('device.usb.status');
        if (result.code === 0 && result.data) {
            usbMuxConfigured = result.data.configured !== false;
            usbMuxTarget = result.data.target || 'esp32';
            updateUsbMuxButton();
        }
    } catch (e) {
        console.warn('USB Mux status unavailable:', e.message);
        usbMuxConfigured = false;
        updateUsbMuxButton();
    }
}

function updateUsbMuxButton() {
    const targetEl = document.getElementById('usb-mux-target');
    const btn = document.getElementById('usb-mux-btn');

    if (!usbMuxConfigured) {
        if (targetEl) targetEl.textContent = '未配置';
        if (btn) {
            btn.className = 'btn btn-small';
            btn.disabled = true;
        }
        return;
    }

    const displayName = USB_MUX_DISPLAY[usbMuxTarget] || usbMuxTarget.toUpperCase();
    if (targetEl) {
        targetEl.textContent = displayName;
    }
    if (btn) {
        btn.disabled = false;
        const colorClass = USB_MUX_COLORS[usbMuxTarget] || '';
        btn.className = 'btn btn-small ' + colorClass;
    }
}

async function toggleUsbMux() {
    if (!usbMuxConfigured) {
        showToast('USB MUX 未配置', 'warning');
        return;
    }

    // 循环切换: esp32 → agx → lpmu → esp32
    const currentIdx = USB_MUX_TARGETS.indexOf(usbMuxTarget);
    const nextIdx = (currentIdx + 1) % USB_MUX_TARGETS.length;
    const newTarget = USB_MUX_TARGETS[nextIdx];
    const displayName = USB_MUX_DISPLAY[newTarget];

    try {
        showToast(`切换 USB 到 ${displayName}...`, 'info');
        const result = await api.call('device.usb.set', { target: newTarget }, 'POST');

        if (result.code === 0) {
            usbMuxTarget = newTarget;
            updateUsbMuxButton();
            showToast(`USB 已切换到 ${displayName}`, 'success');
        } else {
            showToast(`切换失败: ${result.message || '未知错误'}`, 'error');
        }
    } catch (e) {
        showToast(`切换失败: ${e.message}`, 'error');
    }
}

// AGX 电源控制（持续电平：LOW=上电，HIGH=断电）
let agxPowerState = false; // false=断电(HIGH), true=上电(LOW)

async function refreshAgxPowerState() {
    try {
        const result = await api.call('device.status', { device: 'agx' });
        if (result.code === 0 && result.data) {
            agxPowerState = result.data.state === 'on' || result.data.state === 'booting';
            updateAgxPowerButton();
        }
    } catch (e) {
        console.warn('AGX status unavailable:', e.message);
    }
}

function updateAgxPowerButton() {
    const btn = document.getElementById('agx-power-btn');
    if (!btn) return;

    if (agxPowerState) {
        btn.innerHTML = '🟢 AGX 运行中';
        btn.className = 'btn btn-sm btn-success';
        btn.title = '点击关闭 AGX 电源';
    } else {
        btn.innerHTML = '🔴 AGX 已关闭';
        btn.className = 'btn btn-sm btn-danger';
        btn.title = '点击开启 AGX 电源';
    }
}

async function toggleAgxPower() {
    const action = agxPowerState ? 'off' : 'on';
    const actionText = agxPowerState ? '断电' : '上电';

    try {
        showToast(`AGX ${actionText}中...`, 'info');
        const result = await api.call('device.power', { device: 'agx', action: action }, 'POST');

        if (result.code === 0) {
            agxPowerState = !agxPowerState;
            updateAgxPowerButton();
            showToast(`AGX 已${actionText}`, 'success');
        } else {
            showToast(`AGX ${actionText}失败: ${result.message || '未知错误'}`, 'error');
        }
    } catch (e) {
        showToast(`AGX ${actionText}失败: ${e.message}`, 'error');
    }
}

// LPMU 电源控制（脉冲触发，像按物理按钮）
// LPMU 状态: 'unknown' | 'online' | 'offline' | 'detecting'
let lpmuState = 'unknown';
let deviceStateInterval = null;
let lpmuPollingInterval = null;
let lpmuPollingStartTime = 0;
let lpmuPollingMode = 'startup'; // 'startup' | 'shutdown'

async function toggleLpmuPower() {
    if (!confirm('确定要触发 LPMU 电源按钮吗？\n\n这将发送一个脉冲信号，效果类似按物理电源按钮。')) {
        return;
    }

    try {
        showToast('LPMU 电源触发中...', 'info');
        // 记录触发前的状态（用于决定检测逻辑）
        const wasOnline = (lpmuState === 'online');

        // 使用 toggle 动作直接发送脉冲，不检查当前状态
        const result = await api.call('device.power', { device: 'lpmu', action: 'toggle' }, 'POST');

        if (result.code === 0) {
            showToast('LPMU 电源已触发，开始检测状态...', 'success');
            // 启动状态检测（传入之前的状态）
            startLpmuStatePolling(wasOnline);
        } else {
            showToast(`LPMU 触发失败: ${result.message || '未知错误'}`, 'error');
        }
    } catch (e) {
        showToast(`LPMU 触发失败: ${e.message}`, 'error');
    }
}

// 启动 LPMU 状态轮询（触发电源后调用）
function startLpmuStatePolling(wasOnline = false) {
    stopLpmuStatePolling();

    lpmuState = 'detecting';
    lpmuPollingMode = wasOnline ? 'shutdown' : 'startup';
    updateLpmuPowerButton();

    lpmuPollingStartTime = Date.now();

    const minWaitSec = wasOnline ? 40 : 0;
    const maxWaitSec = wasOnline ? 60 : 80;

    lpmuPollingInterval = setInterval(async () => {
        const elapsed = (Date.now() - lpmuPollingStartTime) / 1000;
        const remaining = Math.round(maxWaitSec - elapsed);

        let isReachable = false;
        try {
            const result = await api.call('device.ping', { host: '10.10.99.99', timeout: 1000 });
            isReachable = result.code === 0 && result.data && result.data.reachable;
        } catch (e) {}

        if (lpmuPollingMode === 'startup') {
            if (isReachable) {
                lpmuState = 'online';
                updateLpmuPowerButton();
                stopLpmuStatePolling();
                showToast(`LPMU 已上线 (${Math.round(elapsed)}秒)`, 'success');
                return;
            }
            updateLpmuPowerButton(remaining);
            if (elapsed >= maxWaitSec) {
                lpmuState = 'offline';
                updateLpmuPowerButton();
                stopLpmuStatePolling();
                showToast('LPMU 开机检测超时，认定为已关闭', 'warning');
            }
        } else {
            if (elapsed < minWaitSec) {
                updateLpmuPowerButton(remaining);
                return;
            }
            if (!isReachable) {
                lpmuState = 'offline';
                updateLpmuPowerButton();
                stopLpmuStatePolling();
                showToast(`LPMU 已关闭 (${Math.round(elapsed)}秒)`, 'success');
                return;
            }
            updateLpmuPowerButton(remaining);
            if (elapsed >= maxWaitSec) {
                lpmuState = 'online';
                updateLpmuPowerButton();
                stopLpmuStatePolling();
                showToast('LPMU 关机检测超时，设备可能仍在运行', 'warning');
            }
        }
    }, 5000);
}

function stopLpmuStatePolling() {
    if (lpmuPollingInterval) {
        clearInterval(lpmuPollingInterval);
        lpmuPollingInterval = null;
    }
}

function startDeviceStateMonitor() {
    if (deviceStateInterval) {
        clearInterval(deviceStateInterval);
    }

    if (!lpmuPollingInterval) {
        refreshLpmuState();
    }
    refreshAgxPowerState();

    deviceStateInterval = setInterval(() => {
        if (!lpmuPollingInterval) {
            refreshLpmuState();
        }
    }, 10000);
}

function stopDeviceStateMonitor() {
    if (deviceStateInterval) {
        clearInterval(deviceStateInterval);
        deviceStateInterval = null;
    }
}

async function refreshLpmuState() {
    if (lpmuPollingInterval) return;

    try {
        const result = await api.call('device.ping', { host: '10.10.99.99', timeout: 1000 });
        if (result.code === 0 && result.data) {
            lpmuState = result.data.reachable ? 'online' : 'offline';
        } else {
            lpmuState = 'unknown';
        }
    } catch (e) {
        lpmuState = 'unknown';
    }
    updateLpmuPowerButton();
}

function updateLpmuPowerButton(remainingSec = 0) {
    const btn = document.getElementById('lpmu-power-btn');
    if (!btn) return;

    switch (lpmuState) {
        case 'online':
            btn.innerHTML = '🟢 LPMU 运行中';
            btn.className = 'btn btn-sm btn-success';
            btn.title = 'LPMU 在线 (ping 10.10.99.99 可达)\n点击触发电源按钮';
            break;
        case 'offline':
            btn.innerHTML = '🔴 LPMU 已关闭';
            btn.className = 'btn btn-sm btn-danger';
            btn.title = 'LPMU 离线 (ping 10.10.99.99 不可达)\n点击触发电源按钮';
            break;
        case 'detecting':
            const timeText = remainingSec > 0 ? ` (${remainingSec}s)` : '';
            btn.innerHTML = `⏳ 状态获取中${timeText}`;
            btn.className = 'btn btn-sm btn-warning';
            btn.title = '正在检测 LPMU 状态...\n最多等待 80 秒';
            break;
        default:
            btn.innerHTML = '⚠️ LPMU 检测中';
            btn.className = 'btn btn-sm btn-warning';
            btn.title = 'LPMU 状态未知\n点击触发电源按钮';
    }
}

// 更新系统信息
function updateSystemInfo(data) {
    if (!data) return;
    document.getElementById('sys-chip').textContent = data.chip?.model || '-';
    document.getElementById('sys-version').textContent = data.app?.version || '-';
    document.getElementById('sys-idf').textContent = data.app?.idf_version || '-';
    document.getElementById('sys-compile').textContent = 
        (data.app?.compile_date || '') + ' ' + (data.app?.compile_time || '');
    
    // 直接显示服务器提供的运行时间（不再前端计算）
    const uptimeElem = document.getElementById('sys-uptime');
    if (uptimeElem && data.uptime_ms !== undefined) {
        uptimeElem.textContent = formatUptime(data.uptime_ms);
    }
}

// 更新时间信息
// 启动本地时间更新定时器
let localTimeInterval = null;
function startLocalTimeUpdate() {
    // 清除旧定时器（如果存在）
    if (localTimeInterval) {
        clearInterval(localTimeInterval);
    }
    
    // 立即更新一次
    updateLocalTime();
    
    // 每秒更新
    localTimeInterval = setInterval(updateLocalTime, 1000);
}

function updateLocalTime() {
    const now = new Date();
    const timeStr = now.toLocaleString('zh-CN', { 
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false 
    });
    const datetimeElem = document.getElementById('sys-datetime');
    if (datetimeElem) {
        datetimeElem.textContent = timeStr;
    }
}

// 自动同步标志（避免重复触发）
let autoSyncTriggered = false;

function updateTimeInfo(data) {
    // 系统时间现在使用浏览器本地时间（通过 startLocalTimeUpdate 定时器更新）
    // 此函数保留以备后续扩展（例如显示 NTP 同步状态等）
    if (!data) return;
    
    // 检查 ESP32 时间是否早于 2025 年，自动同步浏览器时间（只触发一次）
    const deviceYear = data.year || (data.datetime ? parseInt(data.datetime.substring(0, 4)) : 0);
    if (deviceYear > 0 && deviceYear < 2025 && !autoSyncTriggered && !data.synced) {
        console.log(`检测到 ESP32 时间早于 2025 年 (${deviceYear})，自动从浏览器同步...`);
        autoSyncTriggered = true;  // 标记已触发，避免重复
        setTimeout(() => syncTimeFromBrowser(true), 500);  // 延迟执行避免阻塞页面加载
    }
    
    const statusText = data.synced ? '已同步' : '未同步';
    const statusElem = document.getElementById('sys-time-status');
    if (statusElem) {
        statusElem.textContent = statusText;
    }
    const sourceMap = { ntp: 'NTP', http: '浏览器', manual: '手动', none: '未同步' };
    const sourceElem = document.getElementById('sys-time-source');
    if (sourceElem) {
        sourceElem.textContent = sourceMap[data.source] || data.source;
    }
    const timezoneElem = document.getElementById('sys-timezone');
    if (timezoneElem) {
        timezoneElem.textContent = data.timezone || '-';
    }
}

// 更新内存信息
function updateMemoryInfo(data) {
    if (!data) return;
    
    const heapTotal = data.internal?.total || 1;
    const heapFree = data.internal?.free || data.free_heap || 0;
    const heapUsed = heapTotal - heapFree;
    const heapPercent = Math.round((heapUsed / heapTotal) * 100);
    
    document.getElementById('heap-progress').style.width = heapPercent + '%';
    document.getElementById('heap-text').textContent = 
        `${formatBytes(heapUsed)} / ${formatBytes(heapTotal)} (${heapPercent}%)`;
    
    if (data.psram?.total) {
        const psramTotal = data.psram.total;
        const psramFree = data.psram.free || 0;
        const psramUsed = psramTotal - psramFree;
        const psramPercent = Math.round((psramUsed / psramTotal) * 100);
        
        document.getElementById('psram-progress').style.width = psramPercent + '%';
        document.getElementById('psram-text').textContent = 
            `${formatBytes(psramUsed)} / ${formatBytes(psramTotal)} (${psramPercent}%)`;
    } else {
        document.getElementById('psram-text').textContent = '不可用';
    }
}

// 更新 CPU 信息
function updateCpuInfo(data) {
    if (!data || !data.cores) {
        console.log('CPU data missing cores:', data);
        return;
    }
    
    const container = document.getElementById('cpu-cores');
    if (!container) return;
    
    let html = '';
    data.cores.forEach(core => {
        const usage = Math.round(core.usage || 0);
        const color = usage > 80 ? '#e74c3c' : (usage > 50 ? '#f39c12' : '#2ecc71');
        html += `
            <p style="font-size:0.85em;margin:3px 0"><strong>Core ${core.id}:</strong> ${usage}%</p>
            <div class="progress-bar" style="height:10px">
                <div class="progress" style="width:${usage}%;background-color:${color}"></div>
            </div>
        `;
    });
    
    if (data.total_usage !== undefined) {
        const avgUsage = Math.round(data.total_usage);
        html += `<p style="margin-top:5px;font-size:0.8em;color:#888">平均: ${avgUsage}%</p>`;
    }
    
    container.innerHTML = html;
}

// 更新网络信息
function updateNetworkInfo(data) {
    if (!data) return;
    const eth = data.ethernet || {};
    const wifi = data.wifi || {};
    document.getElementById('eth-status').textContent = eth.status === 'connected' ? '已连接' : '未连接';
    document.getElementById('wifi-status').textContent = wifi.connected ? '已连接' : '未连接';
    document.getElementById('ip-addr').textContent = eth.ip || wifi.ip || '-';
}

// 更新电源信息
function updatePowerInfo(data) {
    if (!data) return;
    
    // 输入电压：来自电源芯片 (GPIO47 UART)
    const inputVoltage = data.power_chip?.voltage_v;
    // 内部电压：来自 ADC 监控 (GPIO18 ADC)
    const internalVoltage = data.voltage?.supply_v;
    
    const current = data.power_chip?.current_a || data.current?.value_a;
    const power = data.power_chip?.power_w || data.power?.value_w;
    
    // 显示输入电压（主电压）
    document.getElementById('voltage').textContent = 
        (typeof inputVoltage === 'number' ? inputVoltage.toFixed(1) + ' V' : '-');
    
    // 显示内部电压（如果可用）- ADC 需要 -1V 校准
    const internalVoltageElem = document.getElementById('internal-voltage');
    if (internalVoltageElem) {
        const calibratedVoltage = typeof internalVoltage === 'number' ? internalVoltage - 1.0 : null;
        internalVoltageElem.textContent = 
            (calibratedVoltage !== null ? calibratedVoltage.toFixed(2) + ' V' : '-');
    }
    
    document.getElementById('current').textContent = 
        (typeof current === 'number' ? current.toFixed(2) + ' A' : '-');
    document.getElementById('power-watts').textContent = 
        (typeof power === 'number' ? power.toFixed(1) + ' W' : '-');
}

// 更新风扇信息
function updateFanInfo(data) {
    const container = document.getElementById('fans-grid');
    
    // 更新全局温度状态栏（从 data.temperature 获取绑定变量的温度）
    const globalTempEl = document.getElementById('fan-global-temp');
    const globalDutyEl = document.getElementById('fan-global-duty');
    if (globalTempEl && data?.temperature !== undefined) {
        const temp = typeof data.temperature === 'number' ? data.temperature.toFixed(1) : '--';
        globalTempEl.textContent = `${temp}°C`;
        globalTempEl.style.color = data.temp_valid ? 'var(--primary)' : 'var(--warning)';
    }
    if (globalDutyEl && data?.fans?.length > 0) {
        // 显示第一个曲线模式风扇的目标转速，或平均值
        const curveFan = data.fans.find(f => f.mode === 'curve' || f.mode === 'auto');
        if (curveFan) {
            // 曲线/自动模式：显示 target_duty（目标转速）
            globalDutyEl.textContent = `${curveFan.target_duty ?? curveFan.duty ?? 0}%`;
        } else {
            // 手动/关闭模式：显示当前转速
            const avgDuty = Math.round(data.fans.reduce((s, f) => s + (f.duty ?? 0), 0) / data.fans.length);
            globalDutyEl.textContent = `${avgDuty}%`;
        }
    }
    
    if (data?.fans && data.fans.length > 0) {
        container.innerHTML = data.fans.map(fan => {
            const mode = fan.mode || 'auto';
            // 曲线/自动模式显示目标转速 (target_duty)，手动模式显示当前转速 (duty)
            const isCurveOrAuto = (mode === 'curve' || mode === 'auto');
            const displayDuty = isCurveOrAuto ? (fan.target_duty ?? fan.duty ?? 0) : (fan.duty ?? 0);
            const duty = fan.duty ?? 0;  // 当前实际转速（用于滑块）
            const rpm = fan.rpm || 0;
            const isManual = mode === 'manual';
            const isOff = mode === 'off';
            
            const modeInfo = {
                'off':    { label: '关闭', color: '#6b7280', icon: '⏹' },
                'manual': { label: '手动', color: '#f59e0b', icon: '' },
                'auto':   { label: '自动', color: '#10b981', icon: '⚙️' },
                'curve':  { label: '曲线', color: '#3b82f6', icon: '📈' }
            };
            const currentMode = modeInfo[mode] || modeInfo['auto'];
            
            return `
            <div class="fan-card ${isOff ? 'is-off' : ''}">
                <!-- 顶部：风扇名 + 状态 -->
                <div class="fan-header">
                    <span class="fan-title">风扇 ${fan.id}</span>
                    <span class="fan-status-badge" style="background:${currentMode.color}20;color:${currentMode.color}">
                        ${currentMode.icon} ${currentMode.label}
                    </span>
                </div>
                
                <!-- 中间：大转速数字（曲线/自动模式显示目标，手动模式显示当前） -->
                <div class="fan-speed-display">
                    <span class="fan-speed-num">${displayDuty}</span>
                    <span class="fan-speed-percent">%</span>
                    ${isCurveOrAuto && duty !== displayDuty ? `<div class="fan-rpm-small">当前: ${duty}%</div>` : ''}
                    ${rpm > 0 ? `<div class="fan-rpm-small">${rpm} RPM</div>` : ''}
                </div>
                
                <!-- 模式选择 -->
                <div class="fan-mode-tabs">
                    <button class="fan-mode-tab ${mode === 'off' ? 'active off' : ''}" 
                            onclick="setFanMode(${fan.id}, 'off')">关闭</button>
                    <button class="fan-mode-tab ${mode === 'manual' ? 'active manual' : ''}" 
                            onclick="setFanMode(${fan.id}, 'manual')">手动</button>
                    <button class="fan-mode-tab ${mode === 'auto' ? 'active auto' : ''}" 
                            onclick="setFanMode(${fan.id}, 'auto')">自动</button>
                    <button class="fan-mode-tab ${mode === 'curve' ? 'active curve' : ''}" 
                            onclick="setFanMode(${fan.id}, 'curve')">曲线</button>
                </div>
                
                <!-- 速度调节滑块 -->
                <div class="fan-slider-wrap ${isManual ? '' : 'disabled'}">
                    <div class="fan-slider-label">
                        <span>转速调节</span>
                        <span class="fan-slider-value">${duty}%</span>
                    </div>
                    <input type="range" class="fan-slider" min="0" max="100" value="${duty}" 
                           id="fan-slider-${fan.id}"
                           onchange="setFanSpeed(${fan.id}, this.value)"
                           oninput="updateFanSliderUI(${fan.id}, this.value)"
                           ${!isManual ? 'disabled title="切换到手动模式后可调节"' : ''}>
                </div>
                
                <!-- 底部操作 -->
                ${mode === 'curve' ? `
                <button class="fan-curve-btn" onclick="showFanCurveModal(${fan.id})">
                    ⚙️ 编辑温度曲线
                </button>` : ''}
            </div>
        `;
        }).join('');
    } else {
        container.innerHTML = '<p class="text-muted">无可用风扇</p>';
    }
}

// 更新滑块 UI（实时反馈）
function updateFanSliderUI(fanId, value) {
    const slider = document.getElementById(`fan-slider-${fanId}`);
    if (!slider) return;
    
    const card = slider.closest('.fan-card');
    if (card) {
        // 更新大数字
        const numSpan = card.querySelector('.fan-speed-num');
        if (numSpan) numSpan.textContent = value;
        // 更新滑块旁边的值
        const valSpan = card.querySelector('.fan-slider-value');
        if (valSpan) valSpan.textContent = value + '%';
    }
}

// 更新服务列表
function updateServiceList(data) {
    if (!data || !data.services) return;
    
    const services = data.services;
    const runningCount = services.filter(s => s.state === 'RUNNING').length;
    const totalCount = services.length;
    
    // 更新卡片统计
    const runningElem = document.getElementById('services-running');
    const totalElem = document.getElementById('services-total');
    if (runningElem) runningElem.textContent = runningCount;
    if (totalElem) totalElem.textContent = totalCount;
    
    // 更新模态框表格
    const tbody = document.getElementById('services-body');
    if (!tbody) return;
    
    tbody.innerHTML = '';
    services.forEach(svc => {
        const tr = document.createElement('tr');
        const stateClass = svc.state === 'RUNNING' ? 'status-ok' : 
                          svc.state === 'ERROR' ? 'status-error' : 'status-warn';
        tr.innerHTML = `
            <td>${svc.name}</td>
            <td><span class="status-badge ${stateClass}">${svc.state}</span></td>
            <td>${svc.phase}</td>
            <td>${svc.healthy ? '<i class="ri-check-line health-ok"></i>' : '<i class="ri-close-line health-fail"></i>'}</td>
            <td>
                <button class="btn btn-small" onclick="serviceAction('${svc.name}', 'restart')">重启</button>
            </td>
        `;
        tbody.appendChild(tr);
    });
}

// 显示/隐藏服务模态框
function showServicesModal() {
    const modal = document.getElementById('services-modal');
    if (modal) modal.classList.remove('hidden');
}

/**
 * 应用测试温度
 */
async function applyTestTemp() {
    const input = document.getElementById('fan-test-temp');
    const temp = parseFloat(input?.value);
    
    if (isNaN(temp) || temp < 0 || temp > 100) {
        showToast('请输入有效温度 (0-100°C)', 'warning');
        return;
    }
    
    try {
        // 使用 temp.manual API 设置手动温度
        const result = await api.call('temp.manual', { temperature: temp });
        
        if (result.code === 0) {
            showToast(`测试温度已设置为 ${temp}°C`, 'success');
            // 刷新风扇状态
            await refreshFans();
        } else {
            showToast(`设置失败: ${result.message}`, 'error');
        }
    } catch (e) {
        console.error('设置测试温度失败:', e);
        showToast(`设置失败: ${e.message}`, 'error');
    }
}

/**
 * 清除测试温度（恢复正常模式）
 */
async function clearTestTemp() {
    try {
        // 清除手动温度，恢复自动模式
        const result = await api.call('temp.select', { source: 'variable' });
        
        if (result.code === 0) {
            showToast('测试温度已清除，恢复正常模式', 'success');
            document.getElementById('fan-test-temp').value = '';
            // 刷新风扇状态
            await refreshFans();
        } else {
            showToast(`清除失败: ${result.message}`, 'error');
        }
    } catch (e) {
        console.error('清除测试温度失败:', e);
        showToast(`清除失败: ${e.message}`, 'error');
    }
}

function hideServicesModal() {
    const modal = document.getElementById('services-modal');
    if (modal) modal.classList.add('hidden');
}

async function setFanSpeed(id, speed) {
    try {
        await api.fanSet(id, parseInt(speed));
        showToast(`风扇 ${id} 速度已设置为 ${speed}%`, 'success');
    } catch (e) { showToast('设置风扇失败: ' + e.message, 'error'); }
}

async function setFanMode(id, mode) {
    try {
        await api.call('fan.mode', { id: id, mode: mode });
        showToast(`风扇 ${id} 模式已切换为 ${mode}`, 'success');
        await refreshFans();
    } catch (e) { showToast('设置风扇模式失败: ' + e.message, 'error'); }
}

async function refreshFans() {
    try {
        const result = await api.call('fan.status');
        if (result.data) {
            updateFanInfo(result.data);
        }
    } catch (e) { console.error('刷新风扇状态失败:', e); }
}

/*===========================================================================*/
/*                          风扇曲线管理                                       */
/*===========================================================================*/

// 存储当前编辑的风扇曲线配置
let fanCurveConfig = {
    fanId: 0,
    hysteresis: 3.0,
    minInterval: 2000,
    minDuty: 20,
    maxDuty: 100,
    curve: [
        { temp: 30, duty: 30 },
        { temp: 50, duty: 60 },
        { temp: 70, duty: 100 }
    ]
};

/**
 * 显示风扇曲线管理模态框
 */
async function showFanCurveModal(fanId = 0) {
    fanCurveConfig.fanId = fanId;
    
    // 从设备加载配置
    try {
        const result = await api.call('fan.config', { id: fanId });
        if (result.code === 0 && result.data) {
            const cfg = result.data;
            if (cfg.curve && cfg.curve.length >= 2) {
                fanCurveConfig.curve = cfg.curve;
            }
            if (typeof cfg.hysteresis === 'number') {
                fanCurveConfig.hysteresis = cfg.hysteresis;
            }
            if (typeof cfg.min_interval === 'number') {
                fanCurveConfig.minInterval = cfg.min_interval;
            }
            if (typeof cfg.min_duty === 'number') {
                fanCurveConfig.minDuty = cfg.min_duty;
            }
            if (typeof cfg.max_duty === 'number') {
                fanCurveConfig.maxDuty = cfg.max_duty;
            }
            console.log('从设备加载风扇配置:', cfg);
        }
    } catch (e) {
        console.warn('从设备加载配置失败，使用默认值:', e);
    }
    
    const modal = document.createElement('div');
    modal.id = 'fan-curve-modal';
    modal.className = 'modal show';
    modal.onclick = (e) => { if (e.target === modal) closeFanCurveModal(); };
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:650px;">
            <div class="modal-header">
                <h3>风扇曲线管理</h3>
                <button class="modal-close" onclick="closeFanCurveModal()">&times;</button>
            </div>
            <div class="modal-body">
                <!-- 风扇选择 -->
                <div class="form-group">
                    <label>选择风扇</label>
                    <select id="fan-curve-fan-select" class="input" onchange="updateFanCurvePreview()">
                        <option value="0">风扇 0</option>
                        <option value="1">风扇 1</option>
                        <option value="2">风扇 2</option>
                        <option value="3">风扇 3</option>
                    </select>
                </div>
                
                <!-- 温度变量绑定 -->
                <div class="form-group" style="background:var(--bg-tertiary); border-radius:8px; padding:12px 0;">
                    <label style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px;">
                        <span>绑定温度变量</span>
                        <div style="display:flex; gap:8px; align-items:center;">
                            <span id="variable-bind-status" class="badge" style="background:#f0f8ff;color:#007bff;border:1px solid #d0e8ff;">未绑定</span>
                            <div id="fan-curve-temp-current" style="
                                padding:4px 12px; background:var(--bg-secondary); border-radius:6px;
                                font-size:16px; font-weight:bold; color:var(--primary);">
                                --°C
                            </div>
                        </div>
                    </label>
                    <div style="display:flex; gap:8px; align-items:center;">
                        <select id="temp-variable-select" class="input" style="flex:1;">
                            <option value="">-- 选择变量 --</option>
                        </select>
                        <button class="btn btn-sm btn-service-style" onclick="bindTempVariable()">绑定</button>
                        <button class="btn btn-sm btn-secondary" onclick="unbindTempVariable()"><i class="ri-delete-bin-line"></i></button>
                    </div>
                    <small class="form-hint" id="temp-source-hint" style="margin-top:4px;">
                        选择一个浮点类型变量作为温度源（如 agx.cpu_temp）
                    </small>
                </div>
                
                <!-- 曲线点编辑 -->
                <div class="form-group">
                    <label style="display:flex;justify-content:space-between;align-items:center;">
                        <span>温度-转速曲线</span>
                        <button class="btn btn-sm btn-success" onclick="addCurvePoint()"><i class="ri-add-line"></i> 添加点</button>
                    </label>
                    <div id="fan-curve-points" class="fan-curve-points">
                        ${renderCurvePoints()}
                    </div>
                    <small class="form-hint">温度低于最小点时使用最小转速，高于最大点时使用最大转速</small>
                </div>
                
                <!-- 曲线预览 -->
                <div class="form-group">
                    <label>曲线预览</label>
                    <div class="fan-curve-preview">
                        <canvas id="fan-curve-canvas" width="560" height="200"></canvas>
                    </div>
                </div>
                
                <!-- 占空比限制 -->
                <div class="form-row">
                    <div class="form-group" style="flex:1;">
                        <label>最小占空比 (%)</label>
                        <input type="number" id="fan-curve-min-duty" class="input" 
                               value="${fanCurveConfig.minDuty}" min="0" max="100" step="1">
                        <small class="form-hint">低于此值时的最低转速</small>
                    </div>
                    <div class="form-group" style="flex:1;">
                        <label>最大占空比 (%)</label>
                        <input type="number" id="fan-curve-max-duty" class="input" 
                               value="${fanCurveConfig.maxDuty}" min="0" max="100" step="1">
                        <small class="form-hint">高于此值时的最高转速</small>
                    </div>
                </div>
                
                <!-- 迟滞设置 -->
                <div class="form-row">
                    <div class="form-group" style="flex:1;">
                        <label>温度迟滞 (°C)</label>
                        <input type="number" id="fan-curve-hysteresis" class="input" 
                               value="${fanCurveConfig.hysteresis}" min="0" max="20" step="0.5">
                        <small class="form-hint">防止频繁调速</small>
                    </div>
                    <div class="form-group" style="flex:1;">
                        <label>最小间隔 (ms)</label>
                        <input type="number" id="fan-curve-interval" class="input" 
                               value="${fanCurveConfig.minInterval}" min="500" max="30000" step="100">
                        <small class="form-hint">调速最小时间间隔</small>
                    </div>
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn" onclick="closeFanCurveModal()">取消</button>
                <button class="btn btn-service-style" onclick="applyFanCurve()">应用曲线</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // 设置当前风扇
    document.getElementById('fan-curve-fan-select').value = fanId;
    
    // 加载温度源状态
    loadTempSourceStatus();
    
    // 绘制曲线预览
    setTimeout(() => drawCurvePreview(), 50);
}

/**
 * 关闭风扇曲线模态框
 */
function closeFanCurveModal() {
    const modal = document.getElementById('fan-curve-modal');
    if (modal) modal.remove();
}

/**
 * 渲染曲线点列表
 */
function renderCurvePoints() {
    return fanCurveConfig.curve.map((point, index) => `
        <div class="curve-point-row" data-index="${index}">
            <div class="curve-point-inputs">
                <div class="curve-point-field">
                    <span class="field-icon"><i class="ri-temp-hot-line"></i></span>
                    <input type="number" class="input curve-temp-input" 
                           value="${point.temp}" min="-20" max="120" step="1"
                           onchange="updateCurvePoint(${index}, 'temp', this.value)"
                           placeholder="温度">
                    <span class="field-unit">°C</span>
                </div>
                <span class="curve-arrow"><i class="ri-arrow-right-line"></i></span>
                <div class="curve-point-field">
                    <span class="field-icon"><i class="ri-dashboard-3-line"></i></span>
                    <input type="number" class="input curve-duty-input" 
                           value="${point.duty}" min="0" max="100" step="1"
                           onchange="updateCurvePoint(${index}, 'duty', this.value)"
                           placeholder="转速">
                    <span class="field-unit">%</span>
                </div>
            </div>
            <button class="btn btn-sm btn-secondary curve-point-delete" 
                    onclick="removeCurvePoint(${index})" 
                    ${fanCurveConfig.curve.length <= 2 ? 'disabled' : ''}>
                <i class="ri-delete-bin-line"></i>
            </button>
        </div>
    `).join('');
}

/**
 * 添加曲线点
 */
function addCurvePoint() {
    if (fanCurveConfig.curve.length >= 10) {
        showToast('最多支持 10 个曲线点', 'warning');
        return;
    }
    
    // 在最后一个点后添加
    const lastPoint = fanCurveConfig.curve[fanCurveConfig.curve.length - 1];
    const newTemp = Math.min(lastPoint.temp + 10, 100);
    const newDuty = Math.min(lastPoint.duty + 10, 100);
    
    fanCurveConfig.curve.push({ temp: newTemp, duty: newDuty });
    refreshCurveEditor();
}

/**
 * 删除曲线点
 */
function removeCurvePoint(index) {
    if (fanCurveConfig.curve.length <= 2) {
        showToast('至少需要 2 个曲线点', 'warning');
        return;
    }
    fanCurveConfig.curve.splice(index, 1);
    refreshCurveEditor();
}

/**
 * 更新曲线点
 */
function updateCurvePoint(index, field, value) {
    fanCurveConfig.curve[index][field] = parseFloat(value);
    // 排序（按温度升序）
    fanCurveConfig.curve.sort((a, b) => a.temp - b.temp);
    drawCurvePreview();
}

/**
 * 刷新曲线编辑器
 */
function refreshCurveEditor() {
    // 按温度排序
    fanCurveConfig.curve.sort((a, b) => a.temp - b.temp);
    
    const container = document.getElementById('fan-curve-points');
    if (container) {
        container.innerHTML = renderCurvePoints();
    }
    drawCurvePreview();
}

/**
 * 绘制曲线预览
 */
function drawCurvePreview() {
    const canvas = document.getElementById('fan-curve-canvas');
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;
    const padding = { top: 20, right: 20, bottom: 35, left: 45 };
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    
    // 清空画布
    ctx.fillStyle = getComputedStyle(document.body).getPropertyValue('--bg-color').trim() || '#f5f6fa';
    ctx.fillRect(0, 0, width, height);
    
    // 绘制网格
    ctx.strokeStyle = 'rgba(0,0,0,0.1)';
    ctx.lineWidth = 1;
    
    // 垂直网格线 (温度轴)
    for (let t = 0; t <= 100; t += 20) {
        const x = padding.left + (t / 100) * plotWidth;
        ctx.beginPath();
        ctx.moveTo(x, padding.top);
        ctx.lineTo(x, height - padding.bottom);
        ctx.stroke();
    }
    
    // 水平网格线 (转速轴)
    for (let d = 0; d <= 100; d += 20) {
        const y = height - padding.bottom - (d / 100) * plotHeight;
        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(width - padding.right, y);
        ctx.stroke();
    }
    
    // 绘制坐标轴
    ctx.strokeStyle = 'rgba(0,0,0,0.5)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(padding.left, padding.top);
    ctx.lineTo(padding.left, height - padding.bottom);
    ctx.lineTo(width - padding.right, height - padding.bottom);
    ctx.stroke();
    
    // 坐标轴标签
    ctx.fillStyle = 'rgba(0,0,0,0.7)';
    ctx.font = '11px system-ui';
    ctx.textAlign = 'center';
    
    // X 轴标签
    for (let t = 0; t <= 100; t += 20) {
        const x = padding.left + (t / 100) * plotWidth;
        ctx.fillText(`${t}°C`, x, height - 8);
    }
    
    // Y 轴标签
    ctx.textAlign = 'right';
    for (let d = 0; d <= 100; d += 20) {
        const y = height - padding.bottom - (d / 100) * plotHeight;
        ctx.fillText(`${d}%`, padding.left - 8, y + 4);
    }
    
    // 绘制曲线
    if (fanCurveConfig.curve.length < 2) return;
    
    const points = [...fanCurveConfig.curve].sort((a, b) => a.temp - b.temp);
    
    // 曲线路径
    ctx.beginPath();
    ctx.strokeStyle = '#4fc3f7';
    ctx.lineWidth = 3;
    
    // 左侧延伸线（低于最低温度）
    const firstPoint = points[0];
    const firstX = padding.left;
    const firstY = height - padding.bottom - (firstPoint.duty / 100) * plotHeight;
    ctx.moveTo(firstX, firstY);
    ctx.lineTo(padding.left + (firstPoint.temp / 100) * plotWidth, firstY);
    
    // 曲线点连接
    points.forEach((point, i) => {
        const x = padding.left + (point.temp / 100) * plotWidth;
        const y = height - padding.bottom - (point.duty / 100) * plotHeight;
        ctx.lineTo(x, y);
    });
    
    // 右侧延伸线（高于最高温度）
    const lastPoint = points[points.length - 1];
    const lastX = padding.left + (lastPoint.temp / 100) * plotWidth;
    const lastY = height - padding.bottom - (lastPoint.duty / 100) * plotHeight;
    ctx.lineTo(width - padding.right, lastY);
    
    ctx.stroke();
    
    // 填充区域
    ctx.lineTo(width - padding.right, height - padding.bottom);
    ctx.lineTo(padding.left, height - padding.bottom);
    ctx.closePath();
    ctx.fillStyle = 'rgba(79, 195, 247, 0.15)';
    ctx.fill();
    
    // 绘制曲线点
    points.forEach((point, i) => {
        const x = padding.left + (point.temp / 100) * plotWidth;
        const y = height - padding.bottom - (point.duty / 100) * plotHeight;
        
        // 点
        ctx.beginPath();
        ctx.arc(x, y, 6, 0, Math.PI * 2);
        ctx.fillStyle = '#4fc3f7';
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2;
        ctx.stroke();
        
        // 标签
        ctx.fillStyle = '#fff';
        ctx.font = 'bold 10px system-ui';
        ctx.textAlign = 'center';
        ctx.fillText(`${point.temp}°/${point.duty}%`, x, y - 12);
    });
}

/**
 * 加载温度源状态
 */
async function loadTempSourceStatus() {
    try {
        const result = await api.call('temp.status');
        
        if (result.code === 0 && result.data) {
            const data = result.data;
            
            // 更新当前温度显示
            const tempEl = document.getElementById('fan-curve-temp-current');
            if (tempEl) {
                const temp = data.temperature_c?.toFixed(1) || '--';
                tempEl.textContent = `${temp}°C`;
                tempEl.style.color = data.valid ? 'var(--primary)' : 'var(--warning)';
            }
            
            // 更新提示信息
            const hintEl = document.getElementById('temp-source-hint');
            if (hintEl && data.bound_variable) {
                hintEl.textContent = `当前绑定: ${data.bound_variable}`;
            }
        }
        
        // 加载变量绑定状态
        await loadVariableBindStatus();
        
    } catch (e) {
        console.error('获取温度源状态失败:', e);
        const tempEl = document.getElementById('fan-curve-temp-current');
        if (tempEl) {
            tempEl.textContent = '??°C';
            tempEl.style.color = 'var(--error)';
        }
    }
}

/**
 * 加载变量绑定状态和变量列表
 */
async function loadVariableBindStatus() {
    try {
        // 获取当前绑定状态
        const bindResult = await api.call('temp.bind');
        const statusEl = document.getElementById('variable-bind-status');
        const selectEl = document.getElementById('temp-variable-select');
        
        if (bindResult.code === 0 && bindResult.data) {
            const boundVar = bindResult.data.bound_variable;
            if (statusEl) {
                if (boundVar) {
                    statusEl.textContent = `已绑定: ${boundVar}`;
                    statusEl.className = 'badge badge-success';
                } else {
                    statusEl.textContent = '未绑定';
                    statusEl.className = 'badge badge-secondary';
                }
            }
            
            // 设置选择器当前值
            if (selectEl && boundVar) {
                // 先检查选项是否存在，如果不存在则添加临时选项
                let optionExists = false;
                for (let opt of selectEl.options) {
                    if (opt.value === boundVar) {
                        optionExists = true;
                        break;
                    }
                }
                if (!optionExists) {
                    const tempOpt = document.createElement('option');
                    tempOpt.value = boundVar;
                    tempOpt.textContent = `📊 ${boundVar} (当前)`;
                    selectEl.appendChild(tempOpt);
                }
                selectEl.value = boundVar;
            }
        }
        
        // 获取可用变量列表
        const varsResult = await api.call('automation.variables.list');
        if (varsResult.code === 0 && varsResult.data?.variables && selectEl) {
            // 保存当前选中值
            const currentVal = selectEl.value;
            
            // 清空并重建选项
            selectEl.innerHTML = '<option value="">-- 选择变量 --</option>';
            
            // 过滤并添加浮点类型变量（温度相关）
            const tempVars = varsResult.data.variables.filter(v => 
                v.type === 'float' || v.type === 'double' || v.type === 'number' ||
                v.name.includes('temp') || v.name.includes('cpu') || v.name.includes('gpu')
            );
            
            // 先添加温度相关的变量（优先显示）
            const priorityVars = tempVars.filter(v => v.name.includes('temp'));
            const otherVars = tempVars.filter(v => !v.name.includes('temp'));
            
            if (priorityVars.length > 0) {
                const group1 = document.createElement('optgroup');
                group1.label = '🌡️ 温度变量';
                priorityVars.forEach(v => {
                    const opt = document.createElement('option');
                    opt.value = v.name;
                    opt.textContent = `${v.name} (${v.value?.toFixed?.(1) || v.value})`;
                    group1.appendChild(opt);
                });
                selectEl.appendChild(group1);
            }
            
            if (otherVars.length > 0) {
                const group2 = document.createElement('optgroup');
                group2.label = '📊 其他数值变量';
                otherVars.forEach(v => {
                    const opt = document.createElement('option');
                    opt.value = v.name;
                    opt.textContent = `${v.name} (${v.value?.toFixed?.(1) || v.value})`;
                    group2.appendChild(opt);
                });
                selectEl.appendChild(group2);
            }
            
            // 如果没有找到匹配变量，显示所有浮点变量
            if (tempVars.length === 0) {
                const allFloats = varsResult.data.variables.filter(v => 
                    v.type === 'float' || v.type === 'double' || typeof v.value === 'number'
                );
                allFloats.forEach(v => {
                    const opt = document.createElement('option');
                    opt.value = v.name;
                    opt.textContent = `${v.name} (${v.value?.toFixed?.(1) || v.value})`;
                    selectEl.appendChild(opt);
                });
            }
            
            // 恢复选中值
            if (currentVal) {
                selectEl.value = currentVal;
            }
        }
    } catch (e) {
        console.error('加载变量绑定状态失败:', e);
    }
}

/**
 * 绑定温度变量
 */
async function bindTempVariable() {
    const selectEl = document.getElementById('temp-variable-select');
    const varName = selectEl?.value;
    
    if (!varName) {
        showToast('请选择要绑定的变量', 'warning');
        return;
    }
    
    try {
        // 绑定变量
        const result = await api.call('temp.bind', { variable: varName });
        
        if (result.code === 0) {
            // 自动切换到变量模式
            await api.call('temp.select', { source: 'variable' });
            
            showToast(`温度已绑定到变量: ${varName}`, 'success');
            
            // 刷新状态
            await loadTempSourceStatus();
        } else {
            showToast(`绑定失败: ${result.message}`, 'error');
        }
    } catch (e) {
        console.error('绑定温度变量失败:', e);
        showToast(`绑定失败: ${e.message}`, 'error');
    }
}

/**
 * 解绑温度变量
 */
async function unbindTempVariable() {
    try {
        const result = await api.call('temp.bind', { variable: null });
        
        if (result.code === 0) {
            showToast('温度变量绑定已解除', 'success');
            
            // 刷新状态
            await loadTempSourceStatus();
        } else {
            showToast(`解绑失败: ${result.message}`, 'error');
        }
    } catch (e) {
        console.error('解绑温度变量失败:', e);
        showToast(`解绑失败: ${e.message}`, 'error');
    }
}

/**
 * 保存 AGX 服务器配置 (保留用于兼容)
 */
async function saveAgxConfig() {
    showToast('AGX 配置已移至变量绑定', 'info');
    await loadVariableBindStatus();
}

/**
 * 应用风扇曲线
 */
async function applyFanCurve() {
    const fanId = parseInt(document.getElementById('fan-curve-fan-select').value);
    const hysteresis = parseFloat(document.getElementById('fan-curve-hysteresis').value);
    const minInterval = parseInt(document.getElementById('fan-curve-interval').value);
    const minDuty = parseInt(document.getElementById('fan-curve-min-duty').value);
    const maxDuty = parseInt(document.getElementById('fan-curve-max-duty').value);
    
    // 验证
    if (fanCurveConfig.curve.length < 2) {
        showToast('至少需要 2 个曲线点', 'error');
        return;
    }
    
    if (minDuty > maxDuty) {
        showToast('最小占空比不能大于最大占空比', 'error');
        return;
    }
    
    // 排序曲线点
    const sortedCurve = [...fanCurveConfig.curve].sort((a, b) => a.temp - b.temp);
    
    try {
        // 1. 设置占空比限制
        const limitsResult = await api.call('fan.limits', {
            id: fanId,
            min_duty: minDuty,
            max_duty: maxDuty
        });
        
        if (limitsResult.code !== 0) {
            throw new Error(limitsResult.message || '设置占空比限制失败');
        }
        
        // 2. 设置曲线（同时传递 hysteresis 和 min_interval，会自动保存到 NVS）
        const curveResult = await api.call('fan.curve', {
            id: fanId,
            curve: sortedCurve,
            hysteresis: hysteresis,
            min_interval: minInterval
        });
        
        if (curveResult.code !== 0) {
            throw new Error(curveResult.message || '设置曲线失败');
        }
        
        // 3. 切换到曲线模式
        const modeResult = await api.call('fan.mode', {
            id: fanId,
            mode: 'curve'
        });
        
        if (modeResult.code !== 0) {
            throw new Error(modeResult.message || '切换模式失败');
        }
        
        showToast(`风扇 ${fanId} 曲线已应用并保存`, 'success');
        closeFanCurveModal();
        
        // 刷新风扇状态
        await refreshFans();
        
    } catch (e) {
        console.error('应用曲线失败:', e);
        showToast('应用曲线失败: ' + e.message, 'error');
    }
}

/**
 * 更新曲线预览（风扇选择变化时）
 */
async function updateFanCurvePreview() {
    const newFanId = parseInt(document.getElementById('fan-curve-fan-select').value);
    
    // 如果切换了风扇，重新加载该风扇的配置
    if (newFanId !== fanCurveConfig.fanId) {
        fanCurveConfig.fanId = newFanId;
        
        try {
            const result = await api.call('fan.config', { id: newFanId });
            if (result.code === 0 && result.data) {
                const cfg = result.data;
                if (cfg.curve && cfg.curve.length >= 2) {
                    fanCurveConfig.curve = cfg.curve;
                }
                if (typeof cfg.hysteresis === 'number') {
                    fanCurveConfig.hysteresis = cfg.hysteresis;
                    document.getElementById('fan-curve-hysteresis').value = cfg.hysteresis;
                }
                if (typeof cfg.min_interval === 'number') {
                    fanCurveConfig.minInterval = cfg.min_interval;
                    document.getElementById('fan-curve-interval').value = cfg.min_interval;
                }
                if (typeof cfg.min_duty === 'number') {
                    fanCurveConfig.minDuty = cfg.min_duty;
                    document.getElementById('fan-curve-min-duty').value = cfg.min_duty;
                }
                if (typeof cfg.max_duty === 'number') {
                    fanCurveConfig.maxDuty = cfg.max_duty;
                    document.getElementById('fan-curve-max-duty').value = cfg.max_duty;
                }
                // 刷新曲线点编辑器
                refreshCurveEditor();
            }
        } catch (e) {
            console.warn('加载风扇配置失败:', e);
        }
    }
    
    drawCurvePreview();
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


// LED 控制（系统页面内嵌版）
async function refreshSystemLeds() {
    const container = document.getElementById('system-led-devices-grid');
    if (!container) return;
    
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
                    <p>LED 设备可能尚未启动</p>
                </div>
            `;
        }
    } catch (e) {
        console.error('LED list error:', e);
        container.innerHTML = `<div class="error-state">加载失败: ${e.message}</div>`;
    }
}

// ==================== 数据监控面板 - 动态可视化组件系统 ====================

/**
 * 可用的组件类型定义
 */
const WIDGET_TYPES = {
    ring: {
        name: '环形进度',
        icon: '⭕',
        description: '圆环百分比，适合 CPU/内存/磁盘使用率',
        defaultConfig: { min: 0, max: 100, unit: '%', color: '#4dabf7', decimals: 0 }
    },
    gauge: {
        name: '仪表盘',
        icon: '🎯',
        description: '半圆仪表，适合带刻度的数值',
        defaultConfig: { min: 0, max: 100, unit: '', color: '#69db7c', decimals: 1 }
    },
    temp: {
        name: '温度计',
        icon: '🌡️',
        description: '垂直温度条，颜色随温度变化',
        defaultConfig: { min: 0, max: 100, unit: '°C', color: '#ff8787', decimals: 0 }
    },
    number: {
        name: '数字',
        icon: '🔢',
        description: '大号数字显示，适合功率/电压/电流',
        defaultConfig: { unit: 'W', color: '#74c0fc', icon: '⚡', decimals: 1 }
    },
    bar: {
        name: '进度条',
        icon: '📊',
        description: '水平进度条',
        defaultConfig: { min: 0, max: 100, unit: '%', color: '#ffd43b', decimals: 1 }
    },
    text: {
        name: '文本',
        icon: '📝',
        description: '显示文本或格式化字符串',
        defaultConfig: { unit: '', color: '#868e96' }
    },
    status: {
        name: '状态灯',
        icon: '🔴',
        description: '根据值显示不同颜色状态',
        defaultConfig: { thresholds: [0, 50, 80], colors: ['#40c057', '#fab005', '#fa5252'] }
    },
    icon: {
        name: '图标状态',
        icon: '🎭',
        description: '根据值显示不同图标',
        defaultConfig: { icons: { '0': '❌', '1': '✅', 'default': '❓' } }
    },
    dual: {
        name: '双数值',
        icon: '📈',
        description: '主值+副值，适合显示当前/最大等',
        defaultConfig: { unit: '', color: '#74c0fc', decimals: 1 }
    },
    percent: {
        name: '百分比',
        icon: '💯',
        description: '大号百分比数字',
        defaultConfig: { min: 0, max: 100, color: '#4dabf7', decimals: 0 }
    },
    log: {
        name: '日志流',
        icon: '📜',
        description: '从变量读取日志文本流，支持手动刷新',
        defaultConfig: { maxLines: 15, color: '#495057', fullWidth: true }
    }
};

/**
 * 布局选项定义
 */
const LAYOUT_OPTIONS = {
    width: [
        { value: 'auto', label: '自动', desc: '根据内容自适应' },
        { value: 'small', label: '小', desc: '1/4 宽度' },
        { value: 'medium', label: '中', desc: '1/2 宽度' },
        { value: 'large', label: '大', desc: '3/4 宽度' },
        { value: 'full', label: '整行', desc: '独占一整行' }
    ]
};

/**
 * 预设组件模板
 */
const WIDGET_PRESETS = [
    { id: 'cpu', label: 'CPU', type: 'ring', icon: '💻', color: '#4dabf7', unit: '%' },
    { id: 'mem', label: '内存', type: 'ring', icon: '🧠', color: '#69db7c', unit: '%' },
    { id: 'disk', label: '硬盘', type: 'ring', icon: '💾', color: '#ffd43b', unit: '%' },
    { id: 'temp', label: '温度', type: 'temp', icon: '🌡️', color: '#ff8787', unit: '°C' },
    { id: 'gpu', label: 'GPU', type: 'ring', icon: '🎮', color: '#da77f2', unit: '%' },
    { id: 'power', label: '功耗', type: 'number', icon: '⚡', color: '#74c0fc', unit: 'W' },
    { id: 'voltage', label: '电压', type: 'number', icon: '🔌', color: '#ffa94d', unit: 'V' },
    { id: 'current', label: '电流', type: 'number', icon: '💡', color: '#ff6b6b', unit: 'A' },
    { id: 'network', label: '网速', type: 'bar', icon: '🌐', color: '#38d9a9', unit: 'Mbps' },
    { id: 'status', label: '状态', type: 'status', icon: '🔴', color: '#40c057', unit: '' },
    { id: 'uptime', label: '运行时间', type: 'text', icon: '⏱️', color: '#868e96', unit: '' },
    { id: 'log', label: '日志流', type: 'log', icon: '📜', color: '#495057', maxLines: 15, layout: 'full' },
];

// 当前配置的组件列表
let dataWidgets = [];

// 数据刷新间隔配置（毫秒）
let dataWidgetsRefreshInterval = 5000;
let dataWidgetsIntervalId = null;

// 标记是否正在保存（防止重复保存）
let dataWidgetsSaving = false;

/**
 * 加载数据组件配置
 * 优先级：后端 API (SD卡/NVS) > localStorage (兼容旧版)
 */
async function loadDataWidgets() {
    try {
        // 1. 尝试从后端加载
        const response = await api.call('ui.widgets.get');
        // API 响应格式: {code: 0, data: {widgets: [...], refresh_interval: 5000, source: "sdcard"}}
        if (response && response.code === 0 && response.data && response.data.widgets) {
            const result = response.data;
            dataWidgets = result.widgets;
            dataWidgetsRefreshInterval = result.refresh_interval || 5000;
            console.log(`已从后端加载数据组件配置 (来源: ${result.source}, ${dataWidgets.length} 个组件)`);
            
            // 如果后端是默认空配置，检查 localStorage 是否有旧数据需要迁移
            if (result.source === 'default' && dataWidgets.length === 0) {
                const localData = loadDataWidgetsFromLocalStorage();
                if (localData && localData.length > 0) {
                    dataWidgets = localData;
                    console.log(`从 localStorage 迁移 ${localData.length} 个组件到后端`);
                    // 保存到后端
                    await saveDataWidgets();
                }
            }
            return;
        }
    } catch (e) {
        console.warn('从后端加载数据组件配置失败:', e);
    }
    
    // 2. 后端失败，回退到 localStorage
    const localData = loadDataWidgetsFromLocalStorage();
    if (localData) {
        dataWidgets = localData;
        console.log('从 localStorage 加载数据组件配置');
    } else {
        dataWidgets = [];
    }
}

/**
 * 从 localStorage 加载（兼容旧版）
 */
function loadDataWidgetsFromLocalStorage() {
    try {
        let saved = localStorage.getItem('data_widgets_v2');
        
        // 兼容旧版数据：从 data_widgets 迁移
        if (!saved) {
            const oldSaved = localStorage.getItem('data_widgets');
            if (oldSaved) {
                const oldWidgets = JSON.parse(oldSaved);
                // 迁移旧数据：variable -> expression
                return oldWidgets.map(w => ({
                    ...w,
                    expression: w.variable ? `\${${w.variable}}` : null,
                    decimals: w.decimals || 1
                }));
            }
        }
        
        if (saved) {
            return JSON.parse(saved);
        }
    } catch (e) {
        console.warn('从 localStorage 加载失败:', e);
    }
    return null;
}

/**
 * 保存数据组件配置
 * 双写：后端 API (SD卡/NVS) + localStorage (备份)
 */
async function saveDataWidgets() {
    // 防止重复保存
    if (dataWidgetsSaving) return;
    dataWidgetsSaving = true;
    
    try {
        // 1. 保存到 localStorage（本地备份）
        localStorage.setItem('data_widgets_v2', JSON.stringify(dataWidgets));
        localStorage.setItem('data_widgets_refresh_interval', dataWidgetsRefreshInterval.toString());
        
        // 2. 保存到后端（SD卡 + NVS）
        const response = await api.call('ui.widgets.set', {
            widgets: dataWidgets,
            refresh_interval: dataWidgetsRefreshInterval
        }, 'POST');
        
        // API 响应格式: {code: 0, data: {sdcard_saved: true, nvs_saved: true}}
        if (response && response.code === 0 && response.data) {
            console.log(`数据组件配置已保存 (sdcard=${response.data.sdcard_saved}, nvs=${response.data.nvs_saved})`);
        }
    } catch (e) {
        console.warn('保存数据组件配置到后端失败:', e);
        // localStorage 已保存，不影响使用
    } finally {
        dataWidgetsSaving = false;
    }
}

/**
 * 加载刷新间隔配置（已整合到 loadDataWidgets）
 */
function loadDataWidgetsRefreshInterval() {
    // 刷新间隔已在 loadDataWidgets 中一起加载
    // 此函数保留用于兼容性
    try {
        const saved = localStorage.getItem('data_widgets_refresh_interval');
        if (saved) {
            dataWidgetsRefreshInterval = parseInt(saved) || 5000;
        }
    } catch (e) {
        dataWidgetsRefreshInterval = 5000;
    }
}

/**
 * 保存刷新间隔配置（已整合到 saveDataWidgets）
 */
function saveDataWidgetsRefreshInterval() {
    // 触发完整保存
    saveDataWidgets();
}

/**
 * 启动自动刷新
 */
function startDataWidgetsAutoRefresh() {
    stopDataWidgetsAutoRefresh();
    if (dataWidgetsRefreshInterval > 0) {
        dataWidgetsIntervalId = setInterval(() => {
            refreshDataWidgets();
        }, dataWidgetsRefreshInterval);
    }
}

/**
 * 停止自动刷新
 */
function stopDataWidgetsAutoRefresh() {
    if (dataWidgetsIntervalId) {
        clearInterval(dataWidgetsIntervalId);
        dataWidgetsIntervalId = null;
    }
}

/**
 * 生成唯一 ID
 */
function generateWidgetId() {
    return 'w_' + Date.now().toString(36) + Math.random().toString(36).substr(2, 5);
}

/**
 * 计算表达式值
 * 支持: ${var}, 数学运算, 文本拼接
 * @param {string} expression - 表达式，如 "${var} * 100" 或 "${var1} + ${var2}"
 * @param {object} variables - 变量名到值的映射
 * @returns {any} - 计算后的值
 */
function evaluateExpression(expression, variables) {
    if (!expression) return null;
    
    // 替换所有 ${varName} 为实际值
    let evalStr = expression.replace(/\$\{([^}]+)\}/g, (match, varName) => {
        const val = variables[varName.trim()];
        if (val === undefined || val === null) return 'null';
        if (typeof val === 'string') return `"${val}"`;
        return val;
    });
    
    // 如果只是单个变量引用，直接返回
    if (expression.match(/^\$\{[^}]+\}$/)) {
        const varName = expression.slice(2, -1).trim();
        return variables[varName];
    }
    
    // 安全计算表达式
    try {
        // 只允许基本数学运算和字符串操作
        if (evalStr.includes('null')) return null;
        // eslint-disable-next-line no-new-func
        const result = new Function('return ' + evalStr)();
        return result;
    } catch (e) {
        console.warn('表达式计算失败:', expression, e);
        return null;
    }
}

/**
 * 格式化显示值
 * @param {any} value - 原始值
 * @param {object} config - 格式化配置 { decimals, prefix, suffix, format }
 */
function formatDisplayValue(value, config) {
    if (value === null || value === undefined) return '-';
    
    const { decimals = 1, prefix = '', suffix = '', format } = config || {};
    
    // 自定义格式
    if (format) {
        return format.replace('{value}', value).replace('{prefix}', prefix).replace('{suffix}', suffix);
    }
    
    // 数字格式化
    if (typeof value === 'number') {
        return prefix + value.toFixed(decimals) + suffix;
    }
    
    return prefix + String(value) + suffix;
}

/**
 * 渲染单个组件的 HTML
 */
function renderWidgetHtml(widget) {
    const { id, type, label, icon, color, unit } = widget;
    
    let contentHtml = '';
    
    switch (type) {
        case 'ring':
            contentHtml = `
                <div class="dw-ring-container">
                    <svg class="dw-ring" viewBox="0 0 100 100">
                        <circle class="dw-ring-bg" cx="50" cy="50" r="42"/>
                        <circle class="dw-ring-progress" id="dw-${id}-ring" cx="50" cy="50" r="42" style="stroke: ${color};"/>
                    </svg>
                    <div class="dw-ring-value" id="dw-${id}-value">-</div>
                </div>`;
            break;
            
        case 'gauge':
            contentHtml = `
                <div class="dw-gauge-container">
                    <svg class="dw-gauge" viewBox="0 0 100 60">
                        <path class="dw-gauge-bg" d="M10,50 A40,40 0 0,1 90,50"/>
                        <path class="dw-gauge-progress" id="dw-${id}-gauge" d="M10,50 A40,40 0 0,1 90,50" style="stroke: ${color};"/>
                    </svg>
                    <div class="dw-gauge-value" id="dw-${id}-value">-</div>
                </div>`;
            break;
            
        case 'temp':
            contentHtml = `
                <div class="dw-temp-container">
                    <div class="dw-temp-bar">
                        <div class="dw-temp-fill" id="dw-${id}-fill"></div>
                        <div class="dw-temp-scale"><span>100°</span><span>50°</span><span>0°</span></div>
                    </div>
                    <div class="dw-temp-value" id="dw-${id}-value">-</div>
                </div>`;
            break;
            
        case 'number':
            contentHtml = `
                <div class="dw-number-container">
                    <div class="dw-number-icon" style="color: ${color};">${icon || '<i class="ri-dashboard-line"></i>'}</div>
                    <div class="dw-number-value">
                        <span class="dw-number-num" id="dw-${id}-value" style="color: ${color};">-</span>
                        <span class="dw-number-unit">${unit || ''}</span>
                    </div>
                </div>`;
            break;
            
        case 'bar':
            contentHtml = `
                <div class="dw-bar-container">
                    <div class="dw-bar-track">
                        <div class="dw-bar-fill" id="dw-${id}-fill" style="background: ${color};"></div>
                    </div>
                    <div class="dw-bar-value" id="dw-${id}-value">-</div>
                </div>`;
            break;
            
        case 'status':
            contentHtml = `
                <div class="dw-status-container">
                    <div class="dw-status-light" id="dw-${id}-light"></div>
                    <div class="dw-status-label" id="dw-${id}-value">-</div>
                </div>`;
            break;
            
        case 'icon':
            contentHtml = `
                <div class="dw-icon-container">
                    <div class="dw-icon-display" id="dw-${id}-icon">❓</div>
                    <div class="dw-icon-label" id="dw-${id}-value">-</div>
                </div>`;
            break;
            
        case 'dual':
            contentHtml = `
                <div class="dw-dual-container">
                    <div class="dw-dual-main" id="dw-${id}-value" style="color: ${color};">-</div>
                    <div class="dw-dual-sep">/</div>
                    <div class="dw-dual-sub" id="dw-${id}-sub">-</div>
                    <div class="dw-dual-unit">${unit || ''}</div>
                </div>`;
            break;
            
        case 'percent':
            contentHtml = `
                <div class="dw-percent-container">
                    <div class="dw-percent-value" id="dw-${id}-value" style="color: ${color};">-</div>
                    <div class="dw-percent-symbol">%</div>
                </div>`;
            break;
            
        case 'log':
            const maxLines = widget.maxLines || 15;
            const isReading = widget._isReading || false;
            // 默认折叠（除非明确设置了 _isCollapsed: false）
            const isCollapsed = widget._isCollapsed !== false;
            contentHtml = `
                <div class="dw-log-toolbar ${isCollapsed ? 'dw-log-toolbar-collapsed' : ''}">
                    <button class="btn btn-sm dw-log-collapse-btn" 
                            id="dw-${id}-collapse" onclick="event.stopPropagation();toggleLogCollapse('${id}')"
                            title="${isCollapsed ? '展开日志' : '折叠日志'}">
                        <i class="ri-arrow-${isCollapsed ? 'down' : 'up'}-s-line"></i>
                    </button>
                    <button class="btn btn-sm ${isReading ? 'btn-danger' : 'btn-primary'}" 
                            id="dw-${id}-toggle" onclick="event.stopPropagation();toggleLogReading('${id}')">
                        <i class="ri-${isReading ? 'stop' : 'play'}-line"></i> ${isReading ? '停止' : '读取'}
                    </button>
                    <button class="btn btn-sm" onclick="event.stopPropagation();refreshLogOnce('${id}')" title="刷新一次">
                        <i class="ri-refresh-line"></i>
                    </button>
                    <button class="btn btn-sm" onclick="event.stopPropagation();clearLogWidget('${id}')" title="清空">
                        <i class="ri-delete-bin-line"></i>
                    </button>
                    <span class="dw-log-status" id="dw-${id}-status">${isReading ? '读取中...' : '已停止'}</span>
                </div>
                <div class="dw-log-container ${isCollapsed ? 'dw-log-collapsed' : ''}" id="dw-${id}-log" data-max-lines="${maxLines}">
                    <div class="dw-log-empty">点击「读取」开始获取日志</div>
                </div>`;
            break;
            
        case 'text':
        default:
            contentHtml = `
                <div class="dw-text-container">
                    <div class="dw-text-icon">${icon || '<i class="ri-file-text-line"></i>'}</div>
                    <div class="dw-text-value" id="dw-${id}-value" style="color: ${color};">-</div>
                </div>`;
            break;
    }
    
    // 计算布局类名
    const layout = widget.layout || 'auto';
    const layoutClass = layout !== 'auto' ? `dw-layout-${layout}` : '';
    
    return `
        <div class="dw-card ${layoutClass}" data-widget-id="${id}" data-layout="${layout}" onclick="event.target.closest('.dw-card-actions') || event.target.closest('.dw-log-toolbar') || showWidgetManager('${id}')">
            <div class="dw-card-header">
                <span class="dw-card-label">${icon ? icon + ' ' : ''}${label}</span>
            </div>
            ${contentHtml}
        </div>
    `;
}

/**
 * 渲染所有组件
 */
function renderDataWidgets() {
    const grid = document.getElementById('data-widgets-grid');
    const empty = document.getElementById('data-widgets-empty');
    if (!grid) return;
    
    if (dataWidgets.length === 0) {
        grid.innerHTML = '';
        if (empty) empty.style.display = 'block';
    } else {
        if (empty) empty.style.display = 'none';
        grid.innerHTML = dataWidgets.map(w => renderWidgetHtml(w)).join('');
    }
}

/**
 * 更新单个组件的值
 */
function updateWidgetValue(widget, value) {
    const { id, type, color, unit, min = 0, max = 100, decimals = 1, thresholds, colors, icons, expression2 } = widget;
    
    // 处理空值
    if (value === null || value === undefined) {
        const valueEl = document.getElementById(`dw-${id}-value`);
        if (valueEl) valueEl.textContent = '-';
        
        // 重置视觉元素
        if (type === 'ring') {
            const ringEl = document.getElementById(`dw-${id}-ring`);
            if (ringEl) ringEl.style.strokeDashoffset = 264;
        } else if (type === 'gauge') {
            const gaugeEl = document.getElementById(`dw-${id}-gauge`);
            if (gaugeEl) gaugeEl.style.strokeDashoffset = 126;
        } else if (type === 'temp' || type === 'bar') {
            const fillEl = document.getElementById(`dw-${id}-fill`);
            if (fillEl) {
                fillEl.style.height = type === 'temp' ? '0%' : '';
                if (type === 'bar') fillEl.style.width = '0%';
            }
        } else if (type === 'status') {
            const lightEl = document.getElementById(`dw-${id}-light`);
            if (lightEl) lightEl.style.background = '#868e96';
        }
        return;
    }
    
    const numVal = typeof value === 'number' ? value : parseFloat(value);
    const percent = isNaN(numVal) ? 0 : Math.min(100, Math.max(0, ((numVal - min) / (max - min)) * 100));
    
    switch (type) {
        case 'ring': {
            const ringEl = document.getElementById(`dw-${id}-ring`);
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (ringEl) {
                ringEl.style.strokeDashoffset = 264 - (percent / 100) * 264;
            }
            if (valueEl) valueEl.textContent = (isNaN(numVal) ? value : numVal.toFixed(decimals)) + (unit || '%');
            break;
        }
        case 'gauge': {
            const gaugeEl = document.getElementById(`dw-${id}-gauge`);
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (gaugeEl) {
                gaugeEl.style.strokeDashoffset = 126 - (percent / 100) * 126;
            }
            if (valueEl) valueEl.textContent = (isNaN(numVal) ? value : numVal.toFixed(decimals)) + (unit || '');
            break;
        }
        case 'temp': {
            const fillEl = document.getElementById(`dw-${id}-fill`);
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (fillEl) fillEl.style.height = percent + '%';
            if (valueEl) {
                valueEl.textContent = (isNaN(numVal) ? value : numVal.toFixed(0)) + '°C';
                if (!isNaN(numVal)) {
                    if (numVal < 40) valueEl.style.color = '#4dabf7';
                    else if (numVal < 60) valueEl.style.color = '#69db7c';
                    else if (numVal < 80) valueEl.style.color = '#ffd43b';
                    else valueEl.style.color = '#ff6b6b';
                }
            }
            break;
        }
        case 'bar': {
            const fillEl = document.getElementById(`dw-${id}-fill`);
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (fillEl) fillEl.style.width = percent + '%';
            if (valueEl) valueEl.textContent = (isNaN(numVal) ? value : numVal.toFixed(decimals)) + (unit || '%');
            break;
        }
        case 'number': {
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (valueEl) valueEl.textContent = isNaN(numVal) ? value : numVal.toFixed(decimals);
            break;
        }
        case 'percent': {
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (valueEl) valueEl.textContent = isNaN(numVal) ? value : numVal.toFixed(decimals);
            break;
        }
        case 'status': {
            const lightEl = document.getElementById(`dw-${id}-light`);
            const valueEl = document.getElementById(`dw-${id}-value`);
            const th = thresholds || [0, 50, 80];
            const cl = colors || ['#40c057', '#fab005', '#fa5252'];
            let statusColor = cl[0];
            let statusText = '正常';
            if (!isNaN(numVal)) {
                if (numVal >= th[2]) { statusColor = cl[2]; statusText = '警告'; }
                else if (numVal >= th[1]) { statusColor = cl[1]; statusText = '注意'; }
            }
            if (lightEl) lightEl.style.background = statusColor;
            if (valueEl) valueEl.textContent = statusText;
            break;
        }
        case 'icon': {
            const iconEl = document.getElementById(`dw-${id}-icon`);
            const valueEl = document.getElementById(`dw-${id}-value`);
            const iconMap = icons || { '0': '❌', '1': '✅', 'default': '❓' };
            const displayIcon = iconMap[String(value)] || iconMap['default'] || '❓';
            if (iconEl) iconEl.textContent = displayIcon;
            if (valueEl) valueEl.textContent = value;
            break;
        }
        case 'dual': {
            const valueEl = document.getElementById(`dw-${id}-value`);
            const subEl = document.getElementById(`dw-${id}-sub`);
            if (valueEl) valueEl.textContent = isNaN(numVal) ? value : numVal.toFixed(decimals);
            // 副值需要从 expression2 获取
            if (subEl && widget.subValue !== undefined) {
                subEl.textContent = typeof widget.subValue === 'number' ? widget.subValue.toFixed(decimals) : widget.subValue;
            }
            break;
        }
        case 'log': {
            // 日志组件特殊处理，在 refreshDataWidgets 中单独刷新
            break;
        }
        case 'text':
        default: {
            const valueEl = document.getElementById(`dw-${id}-value`);
            if (valueEl) valueEl.textContent = String(value);
            break;
        }
    }
}

/**
 * 初始化数据组件面板
 */
async function initDataWidgets() {
    await loadDataWidgets();  // 异步加载（优先后端 API）
    loadDataWidgetsRefreshInterval();  // 兼容性保留
    renderDataWidgets();
    await refreshDataWidgets();
    startDataWidgetsAutoRefresh();
}

/**
 * 刷新所有组件的数据
 */
async function refreshDataWidgets() {
    // 先批量获取所有需要的变量
    const varNames = new Set();
    dataWidgets.forEach(w => {
        if (w.type !== 'log' && w.expression) {
            // 从表达式中提取变量名
            const matches = w.expression.match(/\$\{([^}]+)\}/g);
            if (matches) {
                matches.forEach(m => varNames.add(m.slice(2, -1).trim()));
            }
        }
        if (w.expression2) {
            const matches = w.expression2.match(/\$\{([^}]+)\}/g);
            if (matches) {
                matches.forEach(m => varNames.add(m.slice(2, -1).trim()));
            }
        }
    });
    
    // 获取所有变量的值
    const variables = {};
    for (const name of varNames) {
        try {
            const resp = await api.call('automation.variables.get', { name });
            if (resp.code === 0 && resp.data) {
                variables[name] = resp.data.value;
            }
        } catch (e) {
            console.warn('获取变量失败:', name, e);
        }
    }
    
    // 更新每个组件（日志组件不自动刷新）
    for (const widget of dataWidgets) {
        if (widget.type === 'log') {
            // 日志组件由用户手动控制，不自动刷新
            continue;
        } else if (widget.expression) {
            const value = evaluateExpression(widget.expression, variables);
            updateWidgetValue(widget, value);
            
            // 处理副值
            if (widget.expression2) {
                widget.subValue = evaluateExpression(widget.expression2, variables);
            }
        } else {
            updateWidgetValue(widget, null);
        }
    }
}

/**
 * 日志组件读取状态和定时器
 */
const logWidgetTimers = {};

/**
 * 切换日志组件折叠状态
 */
function toggleLogCollapse(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (!widget) return;
    
    widget._isCollapsed = !widget._isCollapsed;
    
    // 更新 DOM
    const container = document.getElementById(`dw-${widgetId}-log`);
    const toolbar = container?.previousElementSibling;
    const btn = document.getElementById(`dw-${widgetId}-collapse`);
    
    if (container) {
        if (widget._isCollapsed) {
            container.classList.add('dw-log-collapsed');
        } else {
            container.classList.remove('dw-log-collapsed');
        }
    }
    
    if (toolbar) {
        if (widget._isCollapsed) {
            toolbar.classList.add('dw-log-toolbar-collapsed');
        } else {
            toolbar.classList.remove('dw-log-toolbar-collapsed');
        }
    }
    
    if (btn) {
        btn.innerHTML = `<i class="ri-arrow-${widget._isCollapsed ? 'down' : 'up'}-s-line"></i>`;
        btn.title = widget._isCollapsed ? '展开日志' : '折叠日志';
    }
    
    // 保存状态
    saveDataWidgets();
}

/**
 * 切换日志读取状态
 */
function toggleLogReading(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (!widget) return;
    
    if (widget._isReading) {
        // 停止读取
        stopLogReading(widgetId);
    } else {
        // 开始读取
        startLogReading(widgetId);
    }
}

/**
 * 开始读取日志
 */
function startLogReading(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (!widget || !widget.expression) {
        showToast('请先配置日志变量', 'warning');
        return;
    }
    
    widget._isReading = true;
    
    // 开始读取时自动展开
    if (widget._isCollapsed !== false) {
        widget._isCollapsed = false;
        const container = document.getElementById(`dw-${widgetId}-log`);
        const toolbar = container?.previousElementSibling;
        const btn = document.getElementById(`dw-${widgetId}-collapse`);
        if (container) container.classList.remove('dw-log-collapsed');
        if (toolbar) toolbar.classList.remove('dw-log-toolbar-collapsed');
        if (btn) {
            btn.innerHTML = '<i class="ri-arrow-up-s-line"></i>';
            btn.title = '折叠日志';
        }
    }
    
    updateLogToggleButton(widgetId, true);
    
    // 立即读取一次
    refreshLogOnce(widgetId);
    
    // 设置定时器
    const interval = widget.refreshInterval || 2000;
    logWidgetTimers[widgetId] = setInterval(() => {
        refreshLogOnce(widgetId);
    }, interval);
}

/**
 * 停止读取日志
 */
function stopLogReading(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (widget) {
        widget._isReading = false;
    }
    
    if (logWidgetTimers[widgetId]) {
        clearInterval(logWidgetTimers[widgetId]);
        delete logWidgetTimers[widgetId];
    }
    
    updateLogToggleButton(widgetId, false);
}

/**
 * 更新日志切换按钮状态
 */
function updateLogToggleButton(widgetId, isReading) {
    const btn = document.getElementById(`dw-${widgetId}-toggle`);
    const status = document.getElementById(`dw-${widgetId}-status`);
    
    if (btn) {
        btn.className = `btn btn-sm ${isReading ? 'btn-danger' : 'btn-primary'}`;
        btn.innerHTML = `<i class="ri-${isReading ? 'stop' : 'play'}-line"></i> ${isReading ? '停止' : '读取'}`;
    }
    if (status) {
        status.textContent = isReading ? '读取中...' : '已停止';
    }
}

/**
 * 刷新日志组件一次（从变量读取）
 */
async function refreshLogOnce(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (!widget) return;
    
    const container = document.getElementById(`dw-${widget.id}-log`);
    if (!container) return;
    
    if (!widget.expression) {
        container.innerHTML = '<div class="dw-log-empty">未配置日志变量</div>';
        return;
    }
    
    try {
        // 从表达式中提取变量名
        const varMatch = widget.expression.match(/\$\{([^}]+)\}/);
        if (!varMatch) {
            container.innerHTML = '<div class="dw-log-error">无效的变量表达式</div>';
            return;
        }
        
        const varName = varMatch[1].trim();
        const result = await api.call('automation.variables.get', { name: varName });
        
        if (result.code !== 0 || result.data?.value === undefined) {
            container.innerHTML = '<div class="dw-log-error">变量不存在或无数据</div>';
            return;
        }
        
        const logText = String(result.data.value);
        appendLogToWidget(widget.id, logText, widget.maxLines || 15);
        
    } catch (e) {
        console.warn('获取日志变量失败:', e);
        container.innerHTML = '<div class="dw-log-error">读取失败</div>';
    }
}

/**
 * 追加日志到组件（去重、限制行数）
 */
function appendLogToWidget(widgetId, newText, maxLines) {
    const container = document.getElementById(`dw-${widgetId}-log`);
    if (!container) return;
    
    // 获取现有内容
    let existingLines = [];
    const existingElements = container.querySelectorAll('.dw-log-line');
    existingElements.forEach(el => {
        existingLines.push(el.dataset.text || el.textContent);
    });
    
    // 处理新日志（可能是多行）
    const newLines = newText.split('\\n').filter(l => l.trim());
    
    // 追加新行（去重）
    newLines.forEach(line => {
        const trimmed = line.trim();
        if (trimmed && !existingLines.includes(trimmed)) {
            existingLines.push(trimmed);
        }
    });
    
    // 限制行数
    if (existingLines.length > maxLines) {
        existingLines = existingLines.slice(-maxLines);
    }
    
    // 渲染
    if (existingLines.length === 0) {
        container.innerHTML = '<div class="dw-log-empty">暂无日志</div>';
    } else {
        container.innerHTML = existingLines.map(line => {
            const escaped = escapeHtml(line);
            // 尝试检测日志级别着色
            let colorClass = '';
            if (/\bERR(OR)?\b/i.test(line)) colorClass = 'dw-log-error-line';
            else if (/\bWARN(ING)?\b/i.test(line)) colorClass = 'dw-log-warn-line';
            else if (/\bINFO\b/i.test(line)) colorClass = 'dw-log-info-line';
            else if (/\bDEBUG\b/i.test(line)) colorClass = 'dw-log-debug-line';
            
            return `<div class="dw-log-line ${colorClass}" data-text="${escaped}">${escaped}</div>`;
        }).join('');
        
        // 滚动到底部
        container.scrollTop = container.scrollHeight;
    }
}

/**
 * 清空日志组件
 */
function clearLogWidget(widgetId) {
    const container = document.getElementById(`dw-${widgetId}-log`);
    if (container) {
        container.innerHTML = '<div class="dw-log-empty">已清空</div>';
    }
}

/**
 * 显示组件管理器
 */
function showWidgetManager(editWidgetId = null) {
    const modal = document.createElement('div');
    modal.className = 'modal show';
    modal.id = 'widget-manager-modal';
    modal.onclick = (e) => { if (e.target === modal) closeModal('widget-manager-modal'); };
    
    modal.innerHTML = `
        <div class="modal-content dw-manager-modal">
            <div class="modal-header">
                <h3>数据监控管理</h3>
                <button class="modal-close" onclick="closeModal('widget-manager-modal')">&times;</button>
            </div>
            <div class="modal-body dw-manager-body">
                <div class="dw-manager-sidebar">
                    <div class="dw-manager-section">
                        <h4>面板设置</h4>
                        <div class="form-group" style="margin-bottom:15px;">
                            <label style="font-size:0.9em;">自动刷新间隔</label>
                            <div style="display:flex;gap:8px;align-items:center;">
                                <select id="dw-refresh-interval" onchange="updateRefreshInterval()" style="flex:1;">
                                    <option value="0" ${dataWidgetsRefreshInterval === 0 ? 'selected' : ''}>禁用</option>
                                    <option value="1000" ${dataWidgetsRefreshInterval === 1000 ? 'selected' : ''}>1 秒</option>
                                    <option value="2000" ${dataWidgetsRefreshInterval === 2000 ? 'selected' : ''}>2 秒</option>
                                    <option value="5000" ${dataWidgetsRefreshInterval === 5000 ? 'selected' : ''}>5 秒</option>
                                    <option value="10000" ${dataWidgetsRefreshInterval === 10000 ? 'selected' : ''}>10 秒</option>
                                    <option value="30000" ${dataWidgetsRefreshInterval === 30000 ? 'selected' : ''}>30 秒</option>
                                    <option value="60000" ${dataWidgetsRefreshInterval === 60000 ? 'selected' : ''}>1 分钟</option>
                                </select>
                            </div>
                        </div>
                    </div>
                    <div class="dw-manager-section">
                        <h4>已添加组件</h4>
                        <div id="dw-manager-list" class="dw-manager-list"></div>
                        <button class="btn btn-service-style btn-block" onclick="showAddWidgetPanel()" style="margin-top:12px;">
                            <i class="ri-add-line"></i> 添加新组件
                        </button>
                    </div>
                </div>
                <div class="dw-manager-main" id="dw-manager-main">
                    <div class="dw-manager-empty">
                        <i class="ri-dashboard-line" style="font-size:48px;opacity:0.3;"></i>
                        <p>选择左侧组件进行编辑<br>或添加新组件</p>
                    </div>
                </div>
            </div>
        </div>
    `;
    document.body.appendChild(modal);
    
    // 渲染组件列表
    renderWidgetManagerList();
    
    // 如果指定了编辑的组件，直接打开编辑面板
    if (editWidgetId) {
        showWidgetEditPanel(editWidgetId);
    }
}

/**
 * 更新刷新间隔
 */
function updateRefreshInterval() {
    const select = document.getElementById('dw-refresh-interval');
    if (select) {
        dataWidgetsRefreshInterval = parseInt(select.value) || 0;
        saveDataWidgetsRefreshInterval();
        startDataWidgetsAutoRefresh();
        showToast(`刷新间隔已设置为 ${dataWidgetsRefreshInterval > 0 ? (dataWidgetsRefreshInterval / 1000) + ' 秒' : '禁用'}`, 'success');
    }
}

/**
 * 渲染管理器中的组件列表
 */
function renderWidgetManagerList() {
    const list = document.getElementById('dw-manager-list');
    if (!list) return;
    
    if (dataWidgets.length === 0) {
        list.innerHTML = '<div class="dw-manager-empty-list">暂无组件</div>';
        return;
    }
    
    list.innerHTML = dataWidgets.map((w, idx) => `
        <div class="dw-manager-item" data-id="${w.id}" onclick="showWidgetEditPanel('${w.id}')">
            <span class="dw-manager-item-icon">${w.icon || WIDGET_TYPES[w.type]?.icon || '<i class="ri-dashboard-line"></i>'}</span>
            <span class="dw-manager-item-label">${w.label}</span>
            <span class="dw-manager-item-type">${WIDGET_TYPES[w.type]?.name || w.type}</span>
            <div class="dw-manager-item-actions">
                <button class="dw-btn-icon" onclick="event.stopPropagation();moveWidget('${w.id}',-1)" title="上移" ${idx === 0 ? 'disabled' : ''}><i class="ri-arrow-up-line"></i></button>
                <button class="dw-btn-icon" onclick="event.stopPropagation();moveWidget('${w.id}',1)" title="下移" ${idx === dataWidgets.length - 1 ? 'disabled' : ''}><i class="ri-arrow-down-line"></i></button>
                <button class="dw-btn-icon" onclick="event.stopPropagation();deleteDataWidget('${w.id}')" title="删除"><i class="ri-delete-bin-line"></i></button>
            </div>
        </div>
    `).join('');
}

/**
 * 移动组件位置
 */
function moveWidget(widgetId, direction) {
    const idx = dataWidgets.findIndex(w => w.id === widgetId);
    if (idx === -1) return;
    
    const newIdx = idx + direction;
    if (newIdx < 0 || newIdx >= dataWidgets.length) return;
    
    [dataWidgets[idx], dataWidgets[newIdx]] = [dataWidgets[newIdx], dataWidgets[idx]];
    saveDataWidgets();
    renderWidgetManagerList();
    renderDataWidgets();
}

/**
 * 显示添加组件面板
 */
function showAddWidgetPanel() {
    const main = document.getElementById('dw-manager-main');
    if (!main) return;
    
    // 预设模板
    const presetsHtml = WIDGET_PRESETS.map(p => `
        <div class="dw-preset-item" onclick="addWidgetFromPreset('${p.id}')">
            <span class="dw-preset-icon">${p.icon}</span>
            <span class="dw-preset-label">${p.label}</span>
        </div>
    `).join('');
    
    // 组件类型
    const typesHtml = Object.entries(WIDGET_TYPES).map(([key, t]) => `
        <div class="dw-type-card" onclick="createNewWidget('${key}')">
            <span class="dw-type-icon">${t.icon}</span>
            <span class="dw-type-name">${t.name}</span>
            <span class="dw-type-desc">${t.description}</span>
        </div>
    `).join('');
    
    main.innerHTML = `
        <div class="dw-add-panel">
            <h4>快速添加预设</h4>
            <div class="dw-presets-grid">${presetsHtml}</div>
            
            <h4 style="margin-top:20px;">自定义组件类型</h4>
            <div class="dw-types-grid">${typesHtml}</div>
        </div>
    `;
}

/**
 * 从预设添加组件
 */
function addWidgetFromPreset(presetId) {
    const preset = WIDGET_PRESETS.find(p => p.id === presetId);
    if (!preset) return;
    
    const widget = {
        id: generateWidgetId(),
        type: preset.type,
        label: preset.label,
        icon: preset.icon,
        color: preset.color,
        unit: preset.unit,
        min: 0,
        max: 100,
        decimals: WIDGET_TYPES[preset.type]?.defaultConfig?.decimals || 1,
        expression: null
    };
    
    dataWidgets.push(widget);
    saveDataWidgets();
    renderDataWidgets();
    renderWidgetManagerList();
    showWidgetEditPanel(widget.id);
    showToast(`已添加 ${preset.label}`, 'success');
}

/**
 * 创建新的自定义组件
 */
function createNewWidget(type) {
    const typeConfig = WIDGET_TYPES[type];
    if (!typeConfig) return;
    
    const defaults = typeConfig.defaultConfig || {};
    
    const widget = {
        id: generateWidgetId(),
        type,
        label: '新组件',
        icon: typeConfig.icon,
        color: defaults.color || '#4dabf7',
        unit: defaults.unit || '',
        min: defaults.min || 0,
        max: defaults.max || 100,
        decimals: defaults.decimals || 1,
        expression: null
    };
    
    if (type === 'status') {
        widget.thresholds = defaults.thresholds || [0, 50, 80];
        widget.colors = defaults.colors || ['#40c057', '#fab005', '#fa5252'];
    }
    if (type === 'icon') {
        widget.icons = defaults.icons || { '0': '❌', '1': '✅', 'default': '❓' };
    }
    if (type === 'log') {
        widget.maxLines = defaults.maxLines || 15;
        widget.refreshInterval = 2000;
        widget.layout = 'full';  // 日志组件默认独占一行
        widget.label = '日志流';
    }
    
    dataWidgets.push(widget);
    saveDataWidgets();
    renderDataWidgets();
    renderWidgetManagerList();
    showWidgetEditPanel(widget.id);
}

/**
 * 显示组件编辑面板
 */
function showWidgetEditPanel(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (!widget) return;
    
    const main = document.getElementById('dw-manager-main');
    if (!main) return;
    
    // 高亮当前编辑项
    document.querySelectorAll('.dw-manager-item').forEach(el => el.classList.remove('active'));
    document.querySelector(`.dw-manager-item[data-id="${widgetId}"]`)?.classList.add('active');
    
    const typeConfig = WIDGET_TYPES[widget.type] || {};
    
    // 额外配置（根据组件类型）
    let extraConfigHtml = '';
    if (widget.type === 'status') {
        extraConfigHtml = `
            <div class="form-group">
                <label>阈值设置（正常/注意/警告）</label>
                <div class="form-row">
                    <input type="number" id="edit-threshold-1" class="input" value="${widget.thresholds?.[0] || 0}" placeholder="0">
                    <input type="number" id="edit-threshold-2" class="input" value="${widget.thresholds?.[1] || 50}" placeholder="50">
                    <input type="number" id="edit-threshold-3" class="input" value="${widget.thresholds?.[2] || 80}" placeholder="80">
                </div>
            </div>`;
    }
    if (widget.type === 'dual') {
        extraConfigHtml = `
            <div class="form-group">
                <label>副值表达式</label>
                <input type="text" id="edit-expression2" class="input" value="${widget.expression2 || ''}" 
                       placeholder="例如: \${max_value}">
                <small class="form-hint">显示在主值右侧的副值</small>
            </div>`;
    }
    if (widget.type === 'log') {
        extraConfigHtml = `
            <div class="form-row">
                <div class="form-group">
                    <label>显示行数</label>
                    <input type="number" id="edit-max-lines" class="input" value="${widget.maxLines || 15}" min="5" max="100">
                </div>
                <div class="form-group">
                    <label>刷新间隔（毫秒）</label>
                    <input type="number" id="edit-refresh-interval" class="input" value="${widget.refreshInterval || 2000}" min="500" max="60000" step="500">
                </div>
            </div>
            <div class="form-group dw-expression-group">
                <label>日志变量 <span class="badge">核心</span></label>
                <div class="dw-expression-input">
                    <input type="text" id="edit-expression" class="input" value="${widget.expression || ''}" 
                           placeholder="选择包含日志文本的变量">
                    <button class="btn" onclick="selectVariableForWidget()">选择变量</button>
                </div>
                <small class="form-hint">
                    选择一个包含日志文本的变量，日志会追加显示（支持多行，用 \\n 分隔）
                </small>
            </div>`;
    }
    
    main.innerHTML = `
        <div class="dw-edit-panel">
            <div class="dw-edit-header">
                <span class="dw-edit-type-badge">${typeConfig.icon || '📊'} ${typeConfig.name || widget.type}</span>
            </div>
            
            <div class="form-group">
                <label>标签名称</label>
                <input type="text" id="edit-label" class="input" value="${widget.label}" placeholder="组件名称">
            </div>
            
            <div class="form-row">
                <div class="form-group">
                    <label>图标</label>
                    <input type="text" id="edit-icon" class="input" value="${widget.icon || ''}" placeholder="emoji">
                </div>
                <div class="form-group">
                    <label>颜色</label>
                    <input type="color" id="edit-color" class="input input-color" value="${widget.color || '#4dabf7'}">
                </div>
            </div>
            
            <!-- 布局选项 -->
            <div class="form-group">
                <label>📐 布局宽度</label>
                <div class="dw-layout-options">
                    ${LAYOUT_OPTIONS.width.map(opt => `
                        <label class="dw-layout-option ${widget.layout === opt.value || (!widget.layout && opt.value === 'auto') ? 'active' : ''}">
                            <input type="radio" name="edit-layout" value="${opt.value}" 
                                   ${widget.layout === opt.value || (!widget.layout && opt.value === 'auto') ? 'checked' : ''}
                                   onchange="updateLayoutPreview()">
                            <span class="dw-layout-label">${opt.label}</span>
                            <span class="dw-layout-desc">${opt.desc}</span>
                        </label>
                    `).join('')}
                </div>
            </div>
            
            ${widget.type !== 'log' ? `
            <div class="form-row">
                <div class="form-group">
                    <label>单位</label>
                    <input type="text" id="edit-unit" class="input" value="${widget.unit || ''}" placeholder="%、°C、W">
                </div>
                <div class="form-group">
                    <label>小数位</label>
                    <input type="number" id="edit-decimals" class="input" value="${widget.decimals || 1}" min="0" max="4">
                </div>
            </div>
            ` : ''}
            
            ${widget.type !== 'text' && widget.type !== 'icon' && widget.type !== 'status' && widget.type !== 'log' ? `
            <div class="form-row">
                <div class="form-group">
                    <label>最小值</label>
                    <input type="number" id="edit-min" class="input" value="${widget.min || 0}">
                </div>
                <div class="form-group">
                    <label>最大值</label>
                    <input type="number" id="edit-max" class="input" value="${widget.max || 100}">
                </div>
            </div>
            ` : ''}
            
            ${extraConfigHtml}
            
            ${widget.type !== 'log' ? `
            <div class="form-group dw-expression-group">
                <label>数据表达式 <span class="badge">核心</span></label>
                <div class="dw-expression-input">
                    <input type="text" id="edit-expression" class="input" value="${widget.expression || ''}" 
                           placeholder="点击选择变量或输入表达式">
                    <button class="btn" onclick="selectVariableForWidget()">选择变量</button>
                </div>
                <small class="form-hint">
                    支持: <code>\${变量名}</code> 引用变量，<code>\${a} + \${b}</code> 数学运算，<code>\${a} + "单位"</code> 文本拼接
                </small>
            </div>
            ` : ''}
            
            <div class="dw-edit-preview">
                <label>预览</label>
                <div class="dw-preview-card" id="dw-preview-card">
                    ${renderWidgetHtml(widget)}
                </div>
            </div>
            
            <div class="dw-edit-actions">
                <button class="btn btn-danger" onclick="deleteDataWidget('${widget.id}')"><i class="ri-delete-bin-line"></i> 删除</button>
                <button class="btn btn-primary" onclick="saveWidgetEdit('${widget.id}')"><i class="ri-save-line"></i> 保存</button>
            </div>
        </div>
    `;
}

/**
 * 选择变量插入到表达式
 */
async function selectVariableForWidget() {
    await showVariableSelectModal(null, 'replace');
    const modal = document.getElementById('variable-select-modal');
    if (modal) {
        modal.dataset.callback = 'widgetExpression';
    }
}

/**
 * 保存组件编辑
 */
function saveWidgetEdit(widgetId) {
    const widget = dataWidgets.find(w => w.id === widgetId);
    if (!widget) return;
    
    widget.label = document.getElementById('edit-label')?.value?.trim() || widget.label;
    widget.icon = document.getElementById('edit-icon')?.value?.trim() || '';
    widget.color = document.getElementById('edit-color')?.value || '#4dabf7';
    
    // 布局
    const layoutRadio = document.querySelector('input[name="edit-layout"]:checked');
    widget.layout = layoutRadio?.value || 'auto';
    
    // 非日志组件的通用属性
    if (widget.type !== 'log') {
        widget.unit = document.getElementById('edit-unit')?.value?.trim() || '';
        widget.decimals = parseInt(document.getElementById('edit-decimals')?.value) || 1;
        widget.min = parseFloat(document.getElementById('edit-min')?.value) || 0;
        widget.max = parseFloat(document.getElementById('edit-max')?.value) || 100;
        widget.expression = document.getElementById('edit-expression')?.value?.trim() || null;
    }
    
    // 额外配置
    if (widget.type === 'status') {
        widget.thresholds = [
            parseFloat(document.getElementById('edit-threshold-1')?.value) || 0,
            parseFloat(document.getElementById('edit-threshold-2')?.value) || 50,
            parseFloat(document.getElementById('edit-threshold-3')?.value) || 80
        ];
    }
    if (widget.type === 'dual') {
        widget.expression2 = document.getElementById('edit-expression2')?.value?.trim() || null;
    }
    if (widget.type === 'log') {
        widget.maxLines = parseInt(document.getElementById('edit-max-lines')?.value) || 15;
        widget.refreshInterval = parseInt(document.getElementById('edit-refresh-interval')?.value) || 2000;
        widget.expression = document.getElementById('edit-expression')?.value?.trim() || null;
        
        // 如果日志正在读取，重启定时器以应用新配置
        if (widget._isReading) {
            stopLogReading(widgetId);
            startLogReading(widgetId);
        }
    }
    
    saveDataWidgets();
    renderDataWidgets();
    renderWidgetManagerList();
    refreshDataWidgets();
    showToast('组件已保存', 'success');
    
    // 关闭管理器模态框
    closeModal('widget-manager-modal');
}

/**
 * 删除组件
 */
function deleteDataWidget(widgetId) {
    const idx = dataWidgets.findIndex(w => w.id === widgetId);
    if (idx === -1) return;
    
    const widget = dataWidgets[idx];
    
    if (!confirm(`确定要删除"${widget.label}"组件吗？`)) return;
    
    dataWidgets.splice(idx, 1);
    saveDataWidgets();
    renderDataWidgets();
    renderWidgetManagerList();
    
    // 清空编辑面板
    const main = document.getElementById('dw-manager-main');
    if (main) {
        main.innerHTML = `
            <div class="dw-manager-empty">
                <div style="font-size:48px;opacity:0.3;">📊</div>
                <p>选择左侧组件进行编辑<br>或添加新组件</p>
            </div>
        `;
    }
    
    showToast(`已删除 ${widget.label}`, 'info');
}

// ==================== 快捷操作（手动触发规则） ====================

/**
 * 刷新快捷操作面板
 */
async function refreshQuickActions() {
    const container = document.getElementById('quick-actions-grid');
    if (!container) {
        console.warn('refreshQuickActions: quick-actions-grid not found');
        return;
    }
    
    try {
        console.log('refreshQuickActions: Fetching rules...');
        
        // 确保 SSH 主机数据已加载（用于 nohup 按钮）
        if (!window._sshHostsData || Object.keys(window._sshHostsData).length === 0) {
            await loadSshHostsData();
        }
        
        const result = await api.call('automation.rules.list');
        console.log('refreshQuickActions: API result:', result);
        
        if (result.code === 0 && result.data && result.data.rules) {
            // 过滤出启用且标记为可手动触发的规则
            const allRules = result.data.rules;
            console.log('refreshQuickActions: All rules:', allRules.map(r => ({ id: r.id, enabled: r.enabled, manual_trigger: r.manual_trigger })));
            const manualRules = allRules.filter(r => r.enabled && r.manual_trigger);
            console.log('refreshQuickActions: Manual rules count:', manualRules.length);
            
            if (manualRules.length > 0) {
                // 检查每个规则是否包含 nohup SSH 命令
                const cardsHtml = await Promise.all(manualRules.map(async rule => {
                    const iconValue = rule.icon || '⚡';
                    const iconHtml = iconValue.startsWith('/sdcard/') 
                        ? `<img src="/api/v1/file/download?path=${encodeURIComponent(iconValue)}" alt="icon" onerror="this.textContent='⚡'">`
                        : iconValue;
                    
                    // 检查是否有 nohup SSH 命令动作
                    const nohupInfo = await checkRuleHasNohupSsh(rule);
                    
                    // 基础卡片 + nohup 控制按钮（带运行状态）
                    let nohupBtns = '';
                    let isRunning = false;
                    if (nohupInfo) {
                        // 检测进程是否正在运行
                        try {
                            const host = window._sshHostsData?.[nohupInfo.hostId];
                            if (host) {
                                console.log('Checking process status:', nohupInfo.checkCmd);
                                const checkResult = await api.call('ssh.exec', {
                                    host: host.host,
                                    port: host.port,
                                    user: host.username,
                                    keyid: host.keyid,
                                    command: nohupInfo.checkCmd,
                                    timeout_ms: 5000
                                });
                                const stdout = checkResult.data?.stdout?.trim() || '';
                                console.log('Process check result:', stdout, 'code:', checkResult.code, 'full result:', checkResult);
                                // 检查 stdout 是否包含 'running'（兼容可能的额外输出）
                                isRunning = stdout.includes('running');
                            }
                        } catch (e) {
                            console.warn('Check process status failed:', e);
                            isRunning = false;
                        }
                        
                        const statusIcon = isRunning ? '<i class="ri-record-circle-fill" style="color:#2e7d32"></i>' : '<i class="ri-record-circle-line" style="color:#999"></i>';
                        const statusTitle = isRunning ? '进程运行中' : '进程未运行';
                        
                        // 服务模式状态显示（只有进程运行时才显示服务状态栏）
                        let serviceStatusHtml = '';
                        if (nohupInfo.serviceMode && nohupInfo.varName && isRunning) {
                            const serviceStatusId = `service-status-${escapeHtml(rule.id)}`;
                            serviceStatusHtml = `
                                <div class="quick-action-service-status" id="${serviceStatusId}" data-var="${escapeHtml(nohupInfo.varName)}" data-running="true">
                                    <span class="service-value">...</span>
                                </div>
                            `;
                        }
                        
                        // 停止按钮：进程未运行时禁用
                        const stopBtnDisabled = isRunning ? '' : 'disabled';
                        const stopBtnClass = isRunning ? 'btn-stop' : 'btn-stop btn-disabled';
                        
                        // 状态徽章 + 底部操作栏（传递 pidFile 用于精确停止）
                        // 日志文件路径统一使用 nohupInfo.logFile（基于 cmd.name）
                        nohupBtns = `
                            <span class="nohup-status-badge" title="${statusTitle}">${statusIcon}</span>
                            ${serviceStatusHtml}
                            <div class="quick-action-nohup-bar" onclick="event.stopPropagation()">
                                <button onclick="quickActionViewLog('${escapeHtml(nohupInfo.logFile)}', '${escapeHtml(nohupInfo.hostId)}')" title="查看日志">
                                    <i class="ri-file-text-line"></i> 日志
                                </button>
                                <button class="${stopBtnClass}" onclick="quickActionStopProcess('${escapeHtml(nohupInfo.pidFile)}', '${escapeHtml(nohupInfo.hostId)}', '${escapeHtml(nohupInfo.cmdName)}')" title="终止进程" ${stopBtnDisabled}>
                                    <i class="ri-stop-line"></i> 停止
                                </button>
                            </div>
                        `;
                    }
                    
                    // 如果进程正在运行，点击卡片时提示而不是触发
                    const cardOnClick = (nohupInfo && isRunning) 
                        ? `showToast('进程正在运行中，请先停止', 'warning')`
                        : `triggerQuickAction('${escapeHtml(rule.id)}')`;
                    
                    // 移除名称开头的emoji (包括常见emoji和零宽字符)
                    const cleanName = rule.name.replace(/^[\u{1F300}-\u{1F9FF}\u{2600}-\u{26FF}\u{2700}-\u{27BF}\u{FE00}-\u{FE0F}\u{1F1E0}-\u{1F1FF}\u200D]+\s*/gu, '').trim();
                    
                    return `
                        <div class="quick-action-card${nohupInfo ? ' has-nohup' : ''}${nohupInfo?.serviceMode ? ' has-service' : ''}${isRunning ? ' is-running' : ''}" 
                             id="quick-action-${escapeHtml(rule.id)}"
                             data-rule-id="${escapeHtml(rule.id)}"
                             onclick="${cardOnClick}" 
                             title="${escapeHtml(cleanName)}">
                            <div class="quick-action-icon">${iconHtml}</div>
                            <div class="quick-action-name">${escapeHtml(cleanName)}</div>
                            ${nohupBtns}
                        </div>
                    `;
                }));
                container.innerHTML = cardsHtml.join('');
                
                // 更新服务状态
                updateQuickActionServiceStatus();
                
                // 启动定时刷新服务状态（每 3 秒）
                startServiceStatusRefresh();
            } else {
                container.innerHTML = `
                    <div class="quick-actions-empty">
                        <i class="ri-dashboard-line" style="font-size:2.5em;opacity:0.3"></i>
                        <p>暂无快捷操作</p>
                        <small>在自动化规则中启用"手动触发"选项</small>
                    </div>
                `;
            }
        } else {
            container.innerHTML = '<p class="text-muted">无法加载快捷操作</p>';
        }
    } catch (e) {
        console.error('Quick actions error:', e);
        container.innerHTML = `<p class="text-muted">加载失败</p>`;
    }
}

/**
 * 更新快捷操作卡片中的服务状态
 */
let serviceStatusRefreshInterval = null;

function startServiceStatusRefresh() {
    // 清除之前的定时器
    stopServiceStatusRefresh();
    
    // 每 3 秒刷新一次服务状态
    serviceStatusRefreshInterval = setInterval(() => {
        const statusContainers = document.querySelectorAll('.quick-action-service-status');
        if (statusContainers.length === 0) {
            stopServiceStatusRefresh();
            return;
        }
        updateQuickActionServiceStatus();
    }, 3000);
}

function stopServiceStatusRefresh() {
    if (serviceStatusRefreshInterval) {
        clearInterval(serviceStatusRefreshInterval);
        serviceStatusRefreshInterval = null;
    }
}

async function updateQuickActionServiceStatus() {
    const statusContainers = document.querySelectorAll('.quick-action-service-status');
    if (statusContainers.length === 0) return;
    
    for (const container of statusContainers) {
        const varName = container.dataset.var;
        if (!varName) continue;
        
        const valueEl = container.querySelector('.service-value');
        if (!valueEl) continue;
        
        // 检查进程是否运行（通过 data-running 属性）
        const isRunning = container.dataset.running === 'true';
        
        // 如果进程未运行，始终显示"未启动"
        if (!isRunning) {
            valueEl.textContent = '未启动';
            container.className = 'quick-action-service-status status-idle';
            continue;
        }
        
        try {
            const result = await api.call('automation.variables.get', { name: `${varName}.status` });
            if (result && result.data && result.data.value !== undefined) {
                const status = result.data.value;
                valueEl.textContent = getServiceStatusLabel(status);
                container.className = `quick-action-service-status status-${status}`;
            } else {
                valueEl.textContent = '检测中';
                container.className = 'quick-action-service-status status-checking';
            }
        } catch (e) {
            valueEl.textContent = '未知';
            container.className = 'quick-action-service-status status-unknown';
        }
    }
}

/**
 * 触发快捷操作
 * @param {string} ruleId - 规则 ID
 */
async function triggerQuickAction(ruleId) {
    // 获取卡片元素
    const card = event?.currentTarget || document.getElementById(`quick-action-${ruleId}`);
    if (!card) {
        console.error('triggerQuickAction: card not found for ruleId=', ruleId);
        showToast('❌ 无法找到操作卡片', 'error');
        return;
    }
    
    // 检查是否已经在执行中（防止重复点击）
    if (card.classList.contains('triggering')) {
        showToast('⏳ 操作正在执行中...', 'warning');
        return;
    }
    
    try {
        // 添加按下效果并禁用点击
        card.classList.add('triggering');
        card.style.pointerEvents = 'none';  // 禁用点击防止重复
        
        // 更新图标显示加载状态
        const iconEl = card.querySelector('.quick-action-icon');
        const originalIcon = iconEl?.innerHTML;
        if (iconEl) {
            iconEl.innerHTML = '<span class="spinner-small">⏳</span>';
        }
        
        console.log('triggerQuickAction: calling API for ruleId=', ruleId);
        const result = await api.call('automation.rules.trigger', { id: ruleId });
        console.log('triggerQuickAction: result=', result);
        
        if (result.code === 0) {
            showToast('操作已执行', 'success');
            // 对于 nohup 命令，需要等待更长时间让进程启动并创建 PID 文件
            // 先显示执行中状态，然后延迟刷新获取实际状态
            card.classList.add('is-running');
            setTimeout(() => refreshQuickActions(), 2500);  // 等待 2.5 秒让进程启动
        } else {
            showToast((result.message || '执行失败'), 'error');
            card.style.pointerEvents = '';  // 失败时恢复点击
            // 恢复原始图标
            if (iconEl && originalIcon) {
                iconEl.innerHTML = originalIcon;
            }
        }
        
        card.classList.remove('triggering');
    } catch (e) {
        console.error('triggerQuickAction error:', e);
        showToast('执行失败: ' + e.message, 'error');
        if (card) {
            card.classList.remove('triggering');
            card.style.pointerEvents = '';
        }
    }
}

/**
 * 检查规则是否包含 nohup SSH 命令
 * @param {object} rule - 规则对象（列表中的简化数据）
 * @returns {object|null} - 返回 {logFile, keyword, hostId} 或 null
 */
async function checkRuleHasNohupSsh(rule) {
    // 列表 API 只返回 actions_count，需要获取完整规则
    if (!rule.actions_count || rule.actions_count === 0) {
        console.log('checkRuleHasNohupSsh: rule', rule.id, 'has no actions');
        return null;
    }
    
    // 获取规则详情
    try {
        const detailResult = await api.call('automation.rules.get', { id: rule.id });
        if (detailResult.code !== 0 || !detailResult.data || !detailResult.data.actions) {
            console.log('checkRuleHasNohupSsh: failed to get rule details for', rule.id);
            return null;
        }
        
        const actions = detailResult.data.actions;
        console.log('checkRuleHasNohupSsh: rule', rule.id, 'actions=', actions);
        
        // 确保 SSH 命令已加载
        if (Object.keys(sshCommands).length === 0) {
            await loadSshCommands();
        }
        
        // 遍历所有动作
        for (const action of actions) {
            let sshCmdId = null;
            
            // 方式1: 动作本身是 ssh_cmd_ref 类型
            if (action.type === 'ssh_cmd_ref' && action.ssh_ref?.cmd_id) {
                sshCmdId = action.ssh_ref.cmd_id;
            }
            // 方式2: 动作有 template_id，需要查询模板获取实际类型
            else if (action.template_id) {
                try {
                    const tplResult = await api.call('automation.actions.get', { id: action.template_id });
                    if (tplResult.code === 0 && tplResult.data) {
                        console.log('checkRuleHasNohupSsh: template', action.template_id, '=', tplResult.data);
                        if (tplResult.data.type === 'ssh_cmd_ref' && tplResult.data.ssh_ref?.cmd_id) {
                            sshCmdId = tplResult.data.ssh_ref.cmd_id;
                        }
                    }
                } catch (e) {
                    console.warn('checkRuleHasNohupSsh: failed to get template', action.template_id);
                }
            }
            
            if (sshCmdId) {
                const cmdId = String(sshCmdId);
                console.log('checkRuleHasNohupSsh: looking for cmdId=', cmdId);
                // 在所有主机的命令中查找
                for (const [hostId, cmds] of Object.entries(sshCommands)) {
                    const cmd = cmds.find(c => String(c.id) === cmdId);
                    if (cmd) {
                        console.log('checkRuleHasNohupSsh: found cmd=', cmd.name, 'nohup=', cmd.nohup, 'serviceMode=', cmd.serviceMode);
                        if (cmd.nohup) {
                            // 找到了 nohup 命令
                            // 文件名统一使用 cmd.name（用户可读的命令名称）
                            // varName 只用于服务模式的状态变量
                            const safeName = cmd.name.replace(/[^a-zA-Z0-9]/g, '').slice(0, 20) || 'cmd';
                            const logFile = `/tmp/ts_nohup_${safeName}.log`;
                            const pidFile = `/tmp/ts_nohup_${safeName}.pid`;
                            const varName = cmd.varName || '';  // 服务模式变量名
                            
                            // 使用 PID 文件检测进程状态（最可靠）
                            // 检查 PID 文件存在且进程仍在运行
                            const checkCmd = `[ -f ${pidFile} ] && kill -0 $(cat ${pidFile}) 2>/dev/null && echo 'running' || echo 'stopped'`;
                            
                            return {
                                logFile: logFile,
                                pidFile: pidFile,
                                keyword: cmd.command,
                                progName: safeName,
                                hostId: hostId,
                                cmdName: cmd.name,
                                checkCmd: checkCmd,
                                // 服务模式信息
                                serviceMode: cmd.serviceMode || false,
                                varName: varName,
                                readyPattern: cmd.readyPattern || '',
                                serviceFailPattern: cmd.serviceFailPattern || ''
                            };
                        }
                    }
                }
            }
        }
    } catch (e) {
        console.error('checkRuleHasNohupSsh error:', e);
    }
    return null;
}

/**
  * 快捷操作 - 查看日志
 */
let quickActionTailInterval = null;
let quickActionLastContent = '';

async function quickActionViewLog(logFile, hostId) {
    // 清空上次的日志缓存
    quickActionLastContent = '';
    
    // 获取主机信息
    const host = window._sshHostsData?.[hostId];
    if (!host) {
        showToast('主机不存在', 'error');
        return;
    }
    
    // 显示日志模态框
    const modalHtml = `
        <div id="quick-log-modal" class="modal">
            <div class="modal-content" style="max-width:1400px;width:90%">
                <div class="modal-header">
                    <h2><i class="ri-file-text-line"></i> 日志 - <small style="font-weight:normal;font-size:0.7em;color:#888">${escapeHtml(logFile)}</small></h2>
                    <button class="modal-close" onclick="closeQuickLogModal()">&times;</button>
                </div>
                <div class="modal-body" style="padding:0">
                    <pre id="quick-log-content" style="max-height:400px;overflow:auto;padding:15px;margin:0;background:#1a1a2e;color:#eee;font-size:12px;white-space:pre-wrap">加载中...</pre>
                </div>
                <div class="modal-footer" style="display:flex;gap:10px;padding:10px 15px;justify-content:space-between;align-items:center">
                    <div style="display:flex;gap:8px;align-items:center">
                        <button class="btn btn-danger" id="quick-log-tail-btn" onclick="toggleQuickLogTail('${escapeHtml(logFile)}', '${escapeHtml(hostId)}')"><i class="ri-stop-line"></i> 停止跟踪</button>
                        <label style="display:flex;align-items:center;gap:4px;font-size:0.85em;color:#888">
                            间隔
                            <select id="quick-log-interval" onchange="updateQuickLogInterval('${escapeHtml(logFile)}', '${escapeHtml(hostId)}')" style="padding:2px 6px;border-radius:4px;border:1px solid var(--border-color);background:var(--bg-color);color:var(--text-color);font-size:0.9em">
                                <option value="1000">1秒</option>
                                <option value="2000">2秒</option>
                                <option value="3000">3秒</option>
                                <option value="5000" selected>5秒</option>
                                <option value="10000">10秒</option>
                                <option value="30000">30秒</option>
                            </select>
                        </label>
                        <span id="quick-log-status" style="font-size:0.85em;color:#888;display:flex;align-items:center"><span style="color:#27ae60">● 实时更新中</span></span>
                    </div>
                    <button class="btn" onclick="closeQuickLogModal()">关闭</button>
                </div>
            </div>
        </div>
    `;
    
    // 添加模态框
    const existing = document.getElementById('quick-log-modal');
    if (existing) existing.remove();
    document.body.insertAdjacentHTML('beforeend', modalHtml);
    
    // 加载日志并自动开始跟踪
    await quickActionRefreshLog(logFile, hostId);
    
    // 自动开始实时跟踪（默认5秒间隔）
    startQuickLogTail(logFile, hostId, 5000);
}

async function quickActionRefreshLog(logFile, hostId) {
    const host = window._sshHostsData?.[hostId];
    if (!host) return;
    
    const contentEl = document.getElementById('quick-log-content');
    if (!contentEl) return;
    
    try {
        // 使用 tail -n 200 限制行数，避免日志过大
        const result = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            user: host.username,
            keyid: host.keyid,
            command: `if [ -f ${logFile} ]; then tail -n 200 ${logFile}; else echo '[日志文件不存在或为空]'; fi`,
            timeout_ms: 10000
        });
        
        if (result.code === 0 && result.data) {
            const output = result.data.stdout || result.data.stderr || '[空]';
            // 只有内容变化时才更新（避免闪烁）
            if (output !== quickActionLastContent) {
                contentEl.textContent = output;
                contentEl.scrollTop = contentEl.scrollHeight;
                quickActionLastContent = output;
            }
        } else {
            contentEl.textContent = '[获取失败] ' + (result.message || '');
        }
    } catch (e) {
        contentEl.textContent = '[错误] ' + e.message;
    }
}

// 保存当前跟踪的参数，用于更新间隔时重启
let quickActionTailParams = { logFile: null, hostId: null };

/**
 * 开始日志跟踪
 * @param {string} logFile - 日志文件路径
 * @param {string} hostId - 主机 ID
 * @param {number} intervalMs - 刷新间隔（毫秒），默认 5000
 */
function startQuickLogTail(logFile, hostId, intervalMs = 5000) {
    if (quickActionTailInterval) {
        clearInterval(quickActionTailInterval);
    }
    
    // 保存参数
    quickActionTailParams = { logFile, hostId };
    
    const btn = document.getElementById('quick-log-tail-btn');
    const status = document.getElementById('quick-log-status');
    
    if (btn) {
        btn.textContent = '⏹️ 停止跟踪';
        btn.classList.remove('btn-primary');
        btn.classList.add('btn-danger');
    }
    if (status) status.innerHTML = '<span style="color:#27ae60">● 实时更新中</span>';
    quickActionLastContent = '';
    
    // 定义刷新函数
    const doRefresh = async () => {
        // 检查模态框是否还存在
        if (!document.getElementById('quick-log-modal')) {
            clearInterval(quickActionTailInterval);
            quickActionTailInterval = null;
            return;
        }
        try {
            await quickActionRefreshLog(logFile, hostId);
        } catch (e) {
            console.error('Tail refresh error:', e);
        }
    };
    
    // 立即执行一次
    doRefresh();
    
    // 设置定时器
    quickActionTailInterval = setInterval(doRefresh, intervalMs);
}

/**
 * 更新日志刷新间隔
 */
function updateQuickLogInterval(logFile, hostId) {
    const select = document.getElementById('quick-log-interval');
    if (!select) return;
    
    const intervalMs = parseInt(select.value, 10);
    
    // 如果正在跟踪，重新启动以应用新间隔
    if (quickActionTailInterval) {
        startQuickLogTail(logFile, hostId, intervalMs);
    }
}

/**
 * 停止日志跟踪
 */
function stopQuickLogTail() {
    if (quickActionTailInterval) {
        clearInterval(quickActionTailInterval);
        quickActionTailInterval = null;
    }
    
    const btn = document.getElementById('quick-log-tail-btn');
    const status = document.getElementById('quick-log-status');
    
    if (btn) {
        btn.textContent = '▶️ 开始跟踪';
        btn.classList.remove('btn-danger');
        btn.classList.add('btn-primary');
    }
    if (status) status.textContent = '已暂停';
}

/**
 * 切换日志跟踪状态
 */
function toggleQuickLogTail(logFile, hostId) {
    if (quickActionTailInterval) {
        stopQuickLogTail();
    } else {
        // 获取当前选择的间隔
        const select = document.getElementById('quick-log-interval');
        const intervalMs = select ? parseInt(select.value, 10) : 5000;
        startQuickLogTail(logFile, hostId, intervalMs);
    }
}

function closeQuickLogModal() {
    if (quickActionTailInterval) {
        clearInterval(quickActionTailInterval);
        quickActionTailInterval = null;
    }
    const modal = document.getElementById('quick-log-modal');
    if (modal) modal.remove();
}

/**
 * 快捷操作 - 终止进程（基于 PID 文件精确停止）
 */
async function quickActionStopProcess(pidFile, hostId, cmdName) {
    const host = window._sshHostsData?.[hostId];
    if (!host) {
        showToast('主机不存在', 'error');
        return;
    }
    
    if (!confirm(`确定要终止 "${cmdName}" 吗？`)) {
        return;
    }
    
    try {
        showToast('正在终止进程...', 'info');
        // 使用 PID 文件精确终止进程
        const result = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            user: host.username,
            keyid: host.keyid,
            command: `if [ -f ${pidFile} ]; then kill $(cat ${pidFile}) 2>/dev/null && rm -f ${pidFile} && echo "已终止进程" || echo "进程已不存在"; else echo "PID 文件不存在"; fi`,
            timeout_ms: 10000
        });
        
        if (result.code === 0 && result.data) {
            const output = result.data.stdout || result.data.stderr || '操作完成';
            showToast(output.trim(), output.includes('已终止') ? 'success' : 'info');
            // 刷新状态
            setTimeout(() => refreshQuickActions(), 1000);
        } else {
            showToast((result.message || '操作失败'), 'error');
        }
    } catch (e) {
        showToast('错误: ' + e.message, 'error');
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
            
            // 重新获取时间信息并更新显示
            try {
                const timeInfo = await api.timeInfo();
                if (timeInfo.data) {
                    updateTimeInfo(timeInfo.data);
                }
            } catch (e) {
                console.error('Failed to refresh time info:', e);
            }
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
    
    // 取消系统页面的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
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
                
                <!-- 快捷动画 -->
                <div class="led-effects-row">
                    <div class="led-quick-effects">
                        ${quickEffectsHtml}
                        ${deviceEffects.length > 4 ? `<button class="led-quick-effect more" onclick="openLedModal('${dev.name}', 'effect')" title="更多动画">+${deviceEffects.length - 4}</button>` : ''}
                    </div>
                    <button class="led-stop-btn" onclick="stopEffect('${dev.name}')" title="停止动画">⏹</button>
                </div>
            </div>
            
            <!-- 底部操作栏 -->
            <div class="led-card-footer">
                <button class="led-power-btn ${isOn ? 'on' : ''}" id="toggle-${dev.name}" onclick="toggleLed('${dev.name}')">
                    <span class="power-icon">${isOn ? '🔆' : '💡'}</span>
                    <span class="power-text">${isOn ? '关闭' : '开启'}</span>
                </button>
                ${matrixButtons}
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
            : '<span class="empty">暂无可用动画</span>';
        
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
            : '<span class="empty">暂无可用动画</span>';
        
        return `
            <div class="modal-tabs">
                <button class="modal-tab active" onclick="switchModalTab(this, 'modal-tab-image')">📷 图像</button>
                <button class="modal-tab" onclick="switchModalTab(this, 'modal-tab-qr')">📱 QR码</button>
            </div>
            
            <!-- 图像 Tab -->
            <div class="modal-tab-content active" id="modal-tab-image">
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
                    <button class="btn filter-btn" data-filter="rainbow" onclick="selectFilterInModal('rainbow', this)">🌈 彩虹</button>
                    <button class="btn filter-btn" data-filter="sparkle" onclick="selectFilterInModal('sparkle', this)">✨ 闪耀</button>
                    <button class="btn filter-btn" data-filter="plasma" onclick="selectFilterInModal('plasma', this)">🎆 等离子体</button>
                    <button class="btn filter-btn" data-filter="sepia" onclick="selectFilterInModal('sepia', this)">🖼️ 怀旧</button>
                    <button class="btn filter-btn" data-filter="posterize" onclick="selectFilterInModal('posterize', this)">🎨 色阶分离</button>
                    <button class="btn filter-btn" data-filter="contrast" onclick="selectFilterInModal('contrast', this)">🔆 对比度</button>
                    <button class="btn filter-btn" data-filter="invert" onclick="selectFilterInModal('invert', this)">🔄 反色</button>
                    <button class="btn filter-btn" data-filter="grayscale" onclick="selectFilterInModal('grayscale', this)">⬜ 灰度</button>
                </div>
                <div class="filter-config-modal" id="modal-filter-config" style="display:none;">
                    <span class="filter-name" id="modal-filter-name">未选择</span>
                    <div id="modal-filter-params"></div>
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
        'content': `📷 ${device} - 图像/QR码`,
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

// 模态框内应用动画
async function applyEffectFromModal(device) {
    const effect = selectedEffects[device];
    if (!effect) {
        showToast('请先选择一个动画', 'warning');
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
        showToast(`启动动画失败: ${e.message}`, 'error');
    }
}

// 模态框内停止动画
async function stopEffectFromModal(device) {
    try {
        await api.ledEffectStop(device);
        delete selectedEffects[device];
        updateLedCardState(device, ledStates[device], null);
        showToast(`${device} 动画已停止`, 'success');
    } catch (e) {
        showToast(`停止动画失败: ${e.message}`, 'error');
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
        await api.call('led.qrcode', { device: 'matrix', text, ecc, fg_color: fg, bg_image: bgImage || undefined });
        showToast('QR 码已生成', 'success');
    } catch (e) {
        showToast(`生成 QR 码失败: ${e.message}`, 'error');
    }
}

// 加载字体列表（模态框版本）
async function loadFontListForModal() {
    const fontSelect = document.getElementById('modal-text-font');
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
        
        // 添加字体文件（移除扩展名，因为后端会自动添加 .fnt）
        fonts.forEach(f => {
            const option = document.createElement('option');
            // 移除扩展名 (.fnt, .bdf, .pcf)
            const baseName = f.name.substring(0, f.name.lastIndexOf('.'));
            option.value = baseName;
            option.textContent = f.name;  // 显示完整文件名
            fontSelect.appendChild(option);
        });
        
        // 恢复之前选中的字体
        if (currentFont && Array.from(fontSelect.options).some(opt => opt.value === currentFont)) {
            fontSelect.value = currentFont;
        }
    } catch (e) {
        console.error('加载字体失败:', e);
        // 如果加载失败，显示提示
        fontSelect.innerHTML = '<option value="">无可用字体</option>';
    }
}

// 模态框内显示文本
async function displayTextFromModal() {
    const text = document.getElementById('modal-text-content')?.value;
    const font = document.getElementById('modal-text-font')?.value;
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
            align,
            color,
            scroll: scroll !== 'none' ? scroll : undefined,
            speed,
            loop
        };
        // 只有当用户选择了字体时才传递 font 参数
        if (font && font !== '') {
            params.font = font;
        }
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
        await api.call('led.text.stop', { device: 'matrix' });
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
    
    const configDiv = document.getElementById('modal-filter-config');
    const paramsDiv = document.getElementById('modal-filter-params');
    
    // \u6e05\u7a7a\u73b0\u6709\u53c2\u6570
    if (paramsDiv) {
        paramsDiv.innerHTML = '';
        
        // \u6839\u636e\u6ede\u955c\u914d\u7f6e\u751f\u6210\u53c2\u6570\u63a7\u4ef6
        const config = filterConfig[filter];
        if (config && config.params && config.params.length > 0) {
            config.params.forEach(param => {
                const paramInfo = paramLabels[param];
                const defaultValue = config.defaults[param] || 50;
                
                const row = document.createElement('div');
                row.className = 'config-row';
                row.innerHTML = `
                    <label>${paramInfo.label}</label>
                    <input type="range" id="modal-filter-${param}" 
                           min="${paramInfo.min}" max="${paramInfo.max}" 
                           value="${defaultValue}" style="flex:1"
                           oninput="document.getElementById('modal-filter-${param}-val').textContent=this.value+'${paramInfo.unit}'">
                    <span id="modal-filter-${param}-val">${defaultValue}${paramInfo.unit}</span>
                `;
                paramsDiv.appendChild(row);
            });
        }
    }
    
    if (configDiv) configDiv.style.display = 'flex';
    
    const applyBtn = document.getElementById('modal-apply-filter-btn');
    if (applyBtn) applyBtn.disabled = false;
}

// \u6a21\u6001\u6846\u5185\u5e94\u7528\u6ede\u955c
async function applyFilterFromModal() {
    if (!selectedModalFilter) {
        showToast('\u8bf7\u5148\u9009\u62e9\u4e00\u4e2a\u6ede\u955c', 'warning');
        return;
    }
    
    // \u6536\u96c6\u6240\u6709\u53c2\u6570
    const params = { device: 'matrix', filter: selectedModalFilter };
    const config = filterConfig[selectedModalFilter];
    
    if (config && config.params) {
        config.params.forEach(param => {
            const input = document.getElementById(`modal-filter-${param}`);
            if (input) {
                let value = parseInt(input.value);
                // \u6839\u636e\u53c2\u6570\u7c7b\u578b\u8f6c\u6362\u503c
                if (param === 'saturation') {
                    value = Math.round(value * 2.55); // 0-100 \u8f6c 0-255
                } else if (param === 'decay') {
                    value = Math.round(value * 2.55); // 0-100 \u8f6c 0-255
                } else if (param === 'amount') {
                    value = value - 50; // 0-100 \u8f6c -50 to +50
                }
                params[param] = value;
            }
        });
    }
    
    try {
        await api.call('led.filter.start', params);
        showToast(`\u6ede\u955c ${selectedModalFilter} \u5df2\u5e94\u7528`, 'success');
    } catch (e) {
        showToast(`\u5e94\u7528\u6ede\u955c\u5931\u8d25: ${e.message}`, 'error');
    }
}

// 模态框内停止滤镜
async function stopFilterFromModal() {
    try {
        await api.call('led.filter.stop', { device: 'matrix' });
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

// 当前选中的动画
const selectedEffects = {};

// 支持颜色参数的动画
const colorSupportedEffects = ['breathing', 'solid', 'rain'];

// 选择动画（旧版兼容，保留）
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
        showToast('请先选择一个动画', 'warning');
        return;
    }
    
    const speed = parseInt(document.getElementById(`effect-speed-${device}`)?.value || '50');
    const color = document.getElementById(`effect-color-${device}`)?.value || '#ff0000';
    
    try {
        const params = { speed };
        // 只有支持颜色的动画才传递颜色参数
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
        showToast(`启动动画失败: ${e.message}`, 'error');
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
        showToast(`启动动画失败: ${e.message}`, 'error');
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
        showToast(`${device} 动画已停止`, 'success');
    } catch (e) {
        showToast(`停止动画失败: ${e.message}`, 'error');
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

// 滤镜配置：每个滤镜的参数列表和默认值
const filterConfig = {
    'pulse': { params: ['speed'], defaults: { speed: 50 } },
    'breathing': { params: ['speed'], defaults: { speed: 30 } },
    'blink': { params: ['speed'], defaults: { speed: 50 } },
    'wave': { params: ['speed', 'wavelength', 'amplitude', 'angle'], defaults: { speed: 40, wavelength: 8, amplitude: 128, angle: 0 } },
    'scanline': { params: ['speed', 'width', 'angle', 'intensity'], defaults: { speed: 60, width: 3, angle: 0, intensity: 150 } },
    'glitch': { params: ['intensity', 'frequency'], defaults: { intensity: 70, frequency: 30 } },
    'rainbow': { params: ['speed', 'saturation'], defaults: { speed: 50, saturation: 100 } },
    'sparkle': { params: ['speed', 'density', 'decay'], defaults: { speed: 5, density: 50, decay: 150 } },
    'plasma': { params: ['speed', 'scale'], defaults: { speed: 50, scale: 20 } },
    'posterize': { params: ['levels'], defaults: { levels: 4 } },
    'contrast': { params: ['amount'], defaults: { amount: 50 } },
    'fade-in': { params: ['speed'], defaults: { speed: 30 } },
    'fade-out': { params: ['speed'], defaults: { speed: 30 } },
    'color-shift': { params: ['speed'], defaults: { speed: 20 } },
    'invert': { params: [], defaults: {} },
    'grayscale': { params: [], defaults: {} },
    'sepia': { params: [], defaults: {} }
};

// 参数标签和范围定义
const paramLabels = {
    'speed': { label: '速度', min: 1, max: 100, unit: '', help: '闪耀效果：推荐1-10，低值更慢' },
    'intensity': { label: '强度', min: 0, max: 255, unit: '', help: '亮度增益倍数，推荐100-200产生明显对比' },
    'wavelength': { label: '波长', min: 1, max: 32, unit: 'px' },
    'amplitude': { label: '振幅', min: 0, max: 255, unit: '', help: '波浪亮度变化幅度，推荐50-200' },
    'direction': { label: '方向', min: 0, max: 3, unit: '', labels: ['横向', '纵向', '对角↘', '对角↙'] },
    'angle': { label: '角度', min: 0, max: 360, unit: '°', help: '波浪/扫描线旋转角度：0°=水平向右，90°=垂直向上' },
    'width': { label: '宽度', min: 1, max: 16, unit: 'px', help: '扫描线宽度，值越大光晕越宽' },
    'frequency': { label: '频率', min: 0, max: 100, unit: '%' },
    'saturation': { label: '饱和度', min: 0, max: 100, unit: '%' },
    'density': { label: '密度', min: 0, max: 255, unit: '', help: '同时闪烁的像素数量，推荐50-150' },
    'decay': { label: '衰减', min: 0, max: 255, unit: '', help: '余晖衰减速度，推荐100-200（值越大衰减越快）' },
    'scale': { label: '缩放', min: 1, max: 100, unit: '' },
    'levels': { label: '色阶', min: 2, max: 16, unit: '' },
    'amount': { label: '程度', min: 0, max: 100, unit: '%' }
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
    
    // 动态生成参数控件
    const paramsDiv = document.getElementById('filter-params');
    const config = filterConfig[filterName];
    
    if (config && config.params && config.params.length > 0) {
        paramsDiv.style.display = 'block';
        paramsDiv.innerHTML = ''; // 清空现有控件
        
        config.params.forEach(param => {
            const paramInfo = paramLabels[param];
            const defaultValue = config.defaults[param] || 50;
            
            const row = document.createElement('div');
            row.className = 'config-row';
            row.style.cssText = 'display: flex; align-items: center; gap: 10px; margin-bottom: 10px;';
            
            const label = document.createElement('label');
            label.textContent = paramInfo.label;
            label.style.minWidth = '60px';
            
            const slider = document.createElement('input');
            slider.type = 'range';
            slider.id = `filter-${param}`;
            slider.min = paramInfo.min;
            slider.max = paramInfo.max;
            slider.value = defaultValue;
            slider.style.flex = '1';
            
            const valueSpan = document.createElement('span');
            valueSpan.id = `filter-${param}-val`;
            valueSpan.textContent = defaultValue + paramInfo.unit;
            valueSpan.style.minWidth = '50px';
            valueSpan.style.textAlign = 'right';
            
            slider.oninput = () => {
                valueSpan.textContent = slider.value + paramInfo.unit;
            };
            
            row.appendChild(label);
            row.appendChild(slider);
            row.appendChild(valueSpan);
            paramsDiv.appendChild(row);
        });
    } else {
        paramsDiv.style.display = 'none';
        paramsDiv.innerHTML = '';
    }
}

// 应用选中的滤镜
async function applySelectedFilter() {
    if (!selectedFilter) {
        showToast('请先选择滤镜', 'error');
        return;
    }
    
    // 收集所有参数
    const params = { device: 'matrix', filter: selectedFilter };
    const config = filterConfig[selectedFilter];
    
    if (config && config.params) {
        config.params.forEach(param => {
            const input = document.getElementById(`filter-${param}`);
            if (input) {
                let value = parseInt(input.value);
                // 根据参数类型转换值
                if (param === 'saturation') {
                    value = Math.round(value * 2.55); // 0-100 转 0-255
                } else if (param === 'decay') {
                    value = Math.round(value * 2.55); // 0-100 转 0-255
                } else if (param === 'amount') {
                    value = value - 50; // 0-100 转 -50 to +50
                }
                params[param] = value;
            }
        });
    }
    
    try {
        await api.call('led.filter.start', params);
        showToast(`已应用滤镜: ${selectedFilter}`, 'success');
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
    stopServiceStatusRefresh();
    
    // 取消系统页面的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-network">
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
                        <input type="text" id="ap-ssid-input" placeholder="TianshanOS">
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

// WiFi 模式显示文本
function getWifiModeDisplay(mode) {
    const modeMap = {
        'off': '关闭',
        'sta': '站点 (STA)',
        'ap': '热点 (AP)',
        'apsta': 'STA+AP'
    };
    return modeMap[mode] || mode;
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
//                         文件管理页面
// =========================================================================

let currentFilePath = '/sdcard';

async function loadFilesPage() {
    clearInterval(refreshInterval);
    
    // 取消系统页面的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-files">
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
            
            // 检查是否是配置包上传，显示验证结果
            if (result.config_pack) {
                const pack = result.config_pack;
                if (pack.valid) {
                    const signer = pack.signature?.signer_cn || '未知';
                    const isOfficial = pack.signature?.is_official ? '(官方)' : '';
                    showToast(`配置包验证成功 ✓\n签名者: ${signer} ${isOfficial}`, 'success', 5000);
                    
                    // 显示应用确认对话框
                    setTimeout(() => {
                        showConfigPackApplyConfirm(targetPath, pack);
                    }, 500);
                } else {
                    showToast(`配置包验证失败: ${pack.result_message}`, 'error', 5000);
                }
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

// =========================================================================
//                         指令页面
// =========================================================================

// SSH 指令存储（ESP32 后端持久化，不再使用 localStorage）
let sshCommands = {};

/**
 * 从 ESP32 后端加载 SSH 指令
 * 所有指令都保存在 NVS 中，不同浏览器看到相同数据
 * 
 * 指令 ID 格式：
 * - 新格式（语义化）: 基于名称生成，如 "Start_Jetson_Inference", "Check_GPU_Status"
 * - 旧格式（兼容）: "cmd_xxxxxxxx" (随机 hex)
 */
async function loadSshCommands() {
    try {
        const result = await api.call('ssh.commands.list', {});
        if (result && result.data && result.data.commands) {
            // 按 host_id 组织
            sshCommands = {};
            // 收集孤儿命令（单独分组）
            const orphanCommands = [];
            
            for (const cmd of result.data.commands) {
                // 如果是孤儿命令，单独收集
                if (cmd.orphan) {
                    orphanCommands.push({
                        id: cmd.id,
                        name: cmd.name,
                        command: cmd.command,
                        desc: cmd.desc || '',
                        icon: cmd.icon || '🚀',
                        nohup: cmd.nohup || false,
                        expectPattern: cmd.expectPattern || '',
                        failPattern: cmd.failPattern || '',
                        extractPattern: cmd.extractPattern || '',
                        varName: cmd.varName || '',
                        timeout: cmd.timeout || 30,
                        stopOnMatch: cmd.stopOnMatch || false,
                        serviceMode: cmd.serviceMode || false,
                        readyPattern: cmd.readyPattern || '',
                        serviceFailPattern: cmd.serviceFailPattern || '',
                        readyTimeout: cmd.readyTimeout || 120,
                        readyInterval: cmd.readyInterval || 5000,
                        orphan: true,
                        originalHostId: cmd.host_id  // 保留原始 host_id 用于显示
                    });
                    continue;
                }
                
                if (!sshCommands[cmd.host_id]) {
                    sshCommands[cmd.host_id] = [];
                }
                // 字段名与后端 API 返回一致 (camelCase)
                sshCommands[cmd.host_id].push({
                    id: cmd.id,
                    name: cmd.name,
                    command: cmd.command,
                    desc: cmd.desc || '',
                    icon: cmd.icon || '🚀',
                    nohup: cmd.nohup || false,
                    expectPattern: cmd.expectPattern || '',
                    failPattern: cmd.failPattern || '',
                    extractPattern: cmd.extractPattern || '',
                    varName: cmd.varName || '',
                    timeout: cmd.timeout || 30,
                    stopOnMatch: cmd.stopOnMatch || false,
                    // 服务模式字段
                    serviceMode: cmd.serviceMode || false,
                    readyPattern: cmd.readyPattern || '',
                    serviceFailPattern: cmd.serviceFailPattern || '',
                    readyTimeout: cmd.readyTimeout || 120,
                    readyInterval: cmd.readyInterval || 5000,
                    orphan: false
                });
            }
            
            // 如果有孤儿命令，创建特殊分组
            if (orphanCommands.length > 0) {
                sshCommands['__orphan__'] = orphanCommands;
            }
        }
    } catch (e) {
        console.error('Failed to load SSH commands from backend:', e);
        sshCommands = {};
    }
}

/**
 * 保存单个 SSH 指令到后端
 * @param {string} hostId - 主机 ID
 * @param {object} cmdData - 指令数据
 * @param {string|null} existingId - 已有指令 ID（编辑时传入，如 "AGX_Power_On"）
 * @returns {Promise<string>} 返回指令 ID（新建时基于名称生成，如 "Start_Service"）
 */
async function saveSshCommandToBackend(hostId, cmdData, cmdId) {
    /* ID 是必填参数，由前端输入 */
    const params = {
        id: cmdId,  // 必填
        host_id: hostId,
        name: cmdData.name,
        command: cmdData.command,
        ...(cmdData.desc && { desc: cmdData.desc }),
        ...(cmdData.icon && { icon: cmdData.icon }),
        nohup: !!cmdData.nohup,  // 始终发送，确保能取消勾选
        ...(cmdData.expectPattern && { expectPattern: cmdData.expectPattern }),
        ...(cmdData.failPattern && { failPattern: cmdData.failPattern }),
        ...(cmdData.extractPattern && { extractPattern: cmdData.extractPattern }),
        ...(cmdData.varName && { varName: cmdData.varName }),
        ...(cmdData.timeout && { timeout: cmdData.timeout }),
        ...(cmdData.stopOnMatch !== undefined && { stopOnMatch: cmdData.stopOnMatch }),
        // 服务模式字段（仅在 nohup 时有效）
        serviceMode: !!cmdData.serviceMode,
        ...(cmdData.readyPattern && { readyPattern: cmdData.readyPattern }),
        ...(cmdData.serviceFailPattern && { serviceFailPattern: cmdData.serviceFailPattern }),
        ...(cmdData.readyTimeout && { readyTimeout: cmdData.readyTimeout }),
        ...(cmdData.readyInterval && { readyInterval: cmdData.readyInterval })
    };
    
    const result = await api.call('ssh.commands.add', params);
    if (result && result.code === 0 && result.data && result.data.id) {
        return result.data.id;
    }
    throw new Error(result?.message || 'Failed to save command');
}

/**
 * 从后端删除 SSH 指令
 * @param {string} cmdId - 指令 ID（如 "AGX_Power_On" 或 "cmd_xxxxxxxx"）
 */
async function deleteSshCommandFromBackend(cmdId) {
    await api.call('ssh.commands.remove', { id: cmdId });
}

/**
 * 预创建 SSH 命令相关的变量（保留用于兼容，实际由后端处理）
 * 后端在保存指令时已自动创建变量
 * @param {string} varName - 变量名前缀（如 "ping_test"）
 */
async function preCreateCommandVariables(varName) {
    // 后端 ssh.commands.add API 在保存时已自动创建变量
    // 此函数保留作为兼容占位符
    console.debug(`Variables for ${varName}.* are managed by backend`);
}

/**
 * 确保所有已保存指令的变量都已创建（保留用于兼容，实际由后端处理）
 * 后端在 ESP32 启动时会自动预创建所有命令变量
 */
async function ensureAllCommandVariables() {
    // 后端在 ts_automation_init() 时会调用 ts_ssh_commands_precreate_variables()
    // 自动为 NVS 中保存的所有命令创建变量
    console.debug('Command variables are pre-created by backend on ESP32 boot');
}

async function loadCommandsPage() {
    clearInterval(refreshInterval);
    stopServiceStatusRefresh();
    
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
    // 重置执行状态（防止页面切换后状态残留）
    currentExecSessionId = null;
    
    // 加载已保存的指令（从后端）
    await loadSshCommands();
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-commands">
            <!-- 主机选择和指令列表 -->
            <div class="section">
                <div class="section-header">
                    <h2>🖥️ 选择主机</h2>
                    <div class="section-actions">
                        <button class="btn" onclick="showImportSshCommandModal()" style="background:#17a2b8;color:white">📥 导入指令</button>
                        <button class="btn btn-primary" onclick="showAddCommandModal()">➕ 新建指令</button>
                    </div>
                </div>
                <div id="host-selector" class="host-selector">
                    <div class="loading">加载主机列表...</div>
                </div>
            </div>
            
            <!-- 指令列表 -->
            <div class="section">
                <h2>📋 指令列表</h2>
                <div id="commands-list" class="commands-list">
                    <div class="empty-state">请先选择一个主机</div>
                </div>
            </div>
            
            <!-- 执行结果 -->
            <div class="section" id="exec-result-section" style="display:none">
                <div class="section-header">
                    <h2>📤 执行结果</h2>
                    <div class="section-actions">
                        <button id="cancel-exec-btn" class="btn btn-sm" onclick="cancelExecution()" style="display:none;background:#dc3545;color:white">⏹️ 取消 (Esc)</button>
                        <button class="btn btn-sm" onclick="clearExecResult()">🗑️ 清除</button>
                    </div>
                </div>
                <!-- nohup 快捷操作按钮 -->
                <div id="nohup-actions" class="nohup-actions" style="display:none">
                    <button class="btn btn-sm" id="nohup-view-log" onclick="nohupViewLog()">📄 查看日志</button>
                    <button class="btn btn-sm" id="nohup-tail-log" onclick="nohupTailLog()">👁️ 实时跟踪</button>
                    <button class="btn btn-sm" id="nohup-stop-tail" onclick="nohupStopTail()" style="display:none;background:#ffc107;color:#333">⏹️ 停止跟踪</button>
                    <button class="btn btn-sm" id="nohup-check-process" onclick="nohupCheckProcess()">🔍 检查进程</button>
                    <button class="btn btn-sm" id="nohup-stop-process" onclick="nohupStopProcess()" style="background:#dc3545;color:white">🛑 停止进程</button>
                </div>
                <pre id="exec-result" class="exec-result"></pre>
                
                <!-- 模式匹配结果面板 -->
                <div id="match-result-panel" class="match-result-panel" style="display:none">
                    <div class="match-panel-header">
                        <h3>🎯 匹配结果</h3>
                        <span class="match-status" id="match-status-badge"></span>
                    </div>
                    <div class="match-result-grid">
                        <div class="match-result-item">
                            <div class="match-label">✅ 成功匹配</div>
                            <div class="match-value" id="match-expect-result">-</div>
                            <code class="match-var">msg.expect_matched</code>
                        </div>
                        <div class="match-result-item">
                            <div class="match-label">❌ 失败匹配</div>
                            <div class="match-value" id="match-fail-result">-</div>
                            <code class="match-var">msg.fail_matched</code>
                        </div>
                        <div class="match-result-item">
                            <div class="match-label">📋 提取内容</div>
                            <div class="match-value match-extracted" id="match-extracted-result">-</div>
                            <code class="match-var">msg.extracted</code>
                        </div>
                        <div class="match-result-item">
                            <div class="match-label">🏷️ 最终状态</div>
                            <div class="match-value" id="match-final-status">-</div>
                            <code class="match-var">msg.status</code>
                        </div>
                    </div>
                    <div class="match-api-hint">
                        <small>💡 WebSocket 消息字段可在 <code>handleSshExecMessage(msg)</code> 回调中使用</small>
                    </div>
                </div>
            </div>
        </div>
        
        <!-- 新建/编辑指令模态框 -->
        <div id="command-modal" class="modal hidden">
            <div class="modal-content" style="max-width:500px">
                <div class="modal-header">
                    <h2 id="command-modal-title">➕ 新建指令</h2>
                    <button class="modal-close" onclick="closeCommandModal()">&times;</button>
                </div>
                <div class="modal-body">
                    <form id="command-form" onsubmit="return false;">
                        <div class="form-group" id="cmd-id-group">
                            <label>指令 ID *</label>
                            <input type="text" id="cmd-edit-id" placeholder="例如：restart_nginx, check_status" 
                                   pattern="^[a-zA-Z0-9][a-zA-Z0-9_-]*[a-zA-Z0-9]$|^[a-zA-Z0-9]$"
                                   oninput="validateCommandId(this)" required>
                            <small style="color:#666">唯一标识符，仅限字母、数字、下划线、连字符，不能以 _ 或 - 开头/结尾</small>
                            <span id="cmd-id-error" class="error-hint" style="display:none;color:var(--danger-color);font-size:12px"></span>
                        </div>
                        <div class="form-group">
                            <label>指令名称 *</label>
                            <input type="text" id="cmd-name" placeholder="例如：重启服务" required>
                        </div>
                        <div class="form-group">
                            <label>SSH 命令 *</label>
                            <textarea id="cmd-command" rows="3" placeholder="例如：sudo systemctl restart nginx" required></textarea>
                            <small style="color:#666">支持多行命令，每行一条</small>
                        </div>
                        <div class="form-group">
                            <label>描述（可选）</label>
                            <input type="text" id="cmd-desc" placeholder="简要说明这个指令的作用">
                        </div>
                        <div class="form-group">
                            <label>图标</label>
                            <div class="icon-type-tabs">
                                <button type="button" class="icon-tab active" onclick="switchCmdIconType('emoji')">😀 Emoji</button>
                                <button type="button" class="icon-tab" onclick="switchCmdIconType('image')">🖼️ 图片</button>
                            </div>
                            <div id="icon-emoji-picker" class="icon-picker">
                                ${['🚀', '🔄', '⚡', '🛠️', '📊', '🔍', '💾', '🗑️', '⏹️', '▶️', '📦', '🔧'].map(icon => 
                                    `<button type="button" class="icon-btn" onclick="selectCmdIcon('${icon}')">${icon}</button>`
                                ).join('')}
                            </div>
                            <div id="icon-image-picker" class="icon-image-picker hidden">
                                <div class="icon-preview-row">
                                    <div id="cmd-icon-preview" class="icon-image-preview">
                                        <span class="preview-placeholder">无</span>
                                    </div>
                                    <div class="icon-path-input">
                                        <input type="text" id="cmd-icon-path" class="input" placeholder="/sdcard/images/..." readonly>
                                        <button type="button" class="btn btn-sm" onclick="browseCmdIconImage()">📂 浏览</button>
                                        <button type="button" class="btn btn-sm btn-danger" onclick="clearCmdIconImage()" title="清除">✕</button>
                                    </div>
                                </div>
                            </div>
                            <input type="hidden" id="cmd-icon" value="🚀">
                            <input type="hidden" id="cmd-icon-type" value="emoji">
                        </div>
                        
                        <!-- 高级选项 -->
                        <details class="advanced-options">
                            <summary>⚙️ 高级选项（模式匹配）</summary>
                            <div class="advanced-content">
                                <div class="form-group">
                                    <label class="checkbox-label">
                                        <input type="checkbox" id="cmd-nohup" onchange="updateNohupState()">
                                        <span>🚀 后台执行（nohup）</span>
                                    </label>
                                    <small>命令将在服务器后台运行，SSH 断开后不受影响。适合重启、长时间任务等场景</small>
                                </div>
                                
                                <!-- 服务模式配置（nohup 启用时显示） -->
                                <div id="cmd-service-mode-options" class="service-mode-options hidden">
                                    <div class="service-mode-header">
                                        <label class="checkbox-label">
                                            <input type="checkbox" id="cmd-service-mode" onchange="updateServiceModeState()">
                                            <span>服务模式（监测就绪状态）</span>
                                        </label>
                                        <small>启动后持续监测日志，检测到就绪字符串后更新变量状态</small>
                                    </div>
                                    <div id="cmd-service-mode-fields" class="service-mode-fields hidden">
                                        <div class="form-group">
                                            <label>✅ 就绪匹配模式 *</label>
                                            <input type="text" id="cmd-ready-pattern" placeholder="例如：Running on|Server started">
                                            <small>日志中出现此字符串时标记为就绪（支持 | 分隔多个模式）</small>
                                        </div>
                                        <div class="form-group">
                                            <label>❌ 失败匹配模式</label>
                                            <input type="text" id="cmd-service-fail-pattern" placeholder="例如：error|failed|Exception">
                                            <small>日志中出现此字符串时标记为失败（可选，支持 | 分隔多个模式）</small>
                                        </div>
                                        <div class="form-group">
                                            <label>⏱️ 超时（秒）</label>
                                            <input type="number" id="cmd-ready-timeout" value="120" min="10" max="600" step="10">
                                            <small>超过此时间未匹配到就绪模式则标记为 timeout</small>
                                        </div>
                                        <div class="form-group">
                                            <label>🔄 检测间隔（毫秒）</label>
                                            <input type="number" id="cmd-ready-interval" value="5000" min="1000" max="30000" step="1000">
                                            <small>每隔多久检测一次日志文件</small>
                                        </div>
                                        <div class="service-mode-hint">
                                            <small>💡 服务启动后，系统将监测日志文件：<code>/tmp/ts_nohup_[命令名].log</code></small><br>
                                            <small>变量 <code>[变量名].status</code> 会根据日志匹配自动更新状态</small>
                                        </div>
                                    </div>
                                </div>
                                
                                <!-- 存储变量名（独立字段，nohup 模式下也可用） -->
                                <div class="form-group" id="cmd-var-name-group">
                                    <label>📝 存储变量名</label>
                                    <input type="text" id="cmd-var-name" placeholder="例如：ping_test">
                                    <small id="cmd-var-name-hint">执行结果将存储为 \${变量名.status}、\${变量名.extracted} 等，可在后续命令中引用</small>
                                </div>
                                
                                <div id="cmd-pattern-options">
                                    <div class="form-group">
                                        <label>✅ 成功匹配模式</label>
                                        <input type="text" id="cmd-expect-pattern" placeholder="例如：active (running)" oninput="updateTimeoutState()">
                                        <small>输出中包含此文本时标记为成功</small>
                                    </div>
                                    <div class="form-group">
                                        <label>❌ 失败匹配模式</label>
                                        <input type="text" id="cmd-fail-pattern" placeholder="例如：failed|error" oninput="updateTimeoutState()">
                                        <small>输出中包含此文本时标记为失败</small>
                                    </div>
                                    <div class="form-group">
                                        <label>📋 提取模式</label>
                                        <input type="text" id="cmd-extract-pattern" placeholder="例如：version: (.*)">
                                        <small>从输出中提取匹配内容，使用 (.*) 捕获组</small>
                                    </div>
                                    <div class="form-group">
                                        <label class="checkbox-label">
                                            <input type="checkbox" id="cmd-stop-on-match" onchange="updateTimeoutState()">
                                            <span>⏹️ 匹配后自动停止</span>
                                        </label>
                                        <small>适用于 ping 等持续运行的命令，匹配成功后自动终止</small>
                                    </div>
                                    <div class="form-group" id="cmd-timeout-group">
                                        <label>⏱️ 超时（秒）</label>
                                        <input type="number" id="cmd-timeout" value="30" min="5" max="300" step="5">
                                        <small id="cmd-timeout-hint">超时仅在设置了成功/失败模式或勾选了"匹配后停止"时有效</small>
                                    </div>
                                </div>
                            </div>
                        </details>
                        
                        <div class="form-actions">
                            <button type="button" class="btn" onclick="closeCommandModal()">取消</button>
                            <button type="submit" class="btn btn-primary" onclick="saveCommand()">保存</button>
                        </div>
                    </form>
                </div>
            </div>
        </div>
    `;
    
    // 添加指令页面专用样式
    addCommandsPageStyles();
    
    // 加载主机列表
    await loadHostSelector();
    
    // 确保所有已保存指令的变量都已创建（后台执行，不阻塞 UI）
    ensureAllCommandVariables().catch(e => {
        console.warn('Failed to ensure command variables:', e);
    });
}

function addCommandsPageStyles() {
    if (document.getElementById('commands-page-styles')) return;
    
    const style = document.createElement('style');
    style.id = 'commands-page-styles';
    style.textContent = `
        .host-selector {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
        }
        .host-card {
            background: var(--bg-secondary);
            border: 2px solid var(--border-color);
            border-radius: 8px;
            padding: 12px 16px;
            cursor: pointer;
            transition: all 0.2s;
            min-width: 180px;
        }
        .host-card:hover {
            border-color: var(--primary);
            transform: translateY(-2px);
        }
        .host-card.selected {
            border-color: var(--primary);
            background: rgba(var(--primary-rgb), 0.1);
        }
        .host-card .host-name {
            font-weight: bold;
            margin-bottom: 4px;
        }
        .host-card .host-info {
            font-size: 0.85em;
            color: #666;
        }
        
        .commands-list {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
            gap: 12px;
        }
        .command-card {
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 12px;
            display: flex;
            flex-direction: column;
            gap: 8px;
        }
        .command-card .cmd-header {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .command-card .cmd-icon {
            font-size: 1.5em;
        }
        .command-card .cmd-name {
            font-weight: bold;
            font-size: 1em;
            flex: 1;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .command-card .cmd-desc {
            color: #666;
            font-size: 0.85em;
            line-height: 1.3;
            display: -webkit-box;
            -webkit-line-clamp: 2;
            -webkit-box-orient: vertical;
            overflow: hidden;
        }
        .command-card .cmd-code {
            font-family: monospace;
            font-size: 0.8em;
            color: #888;
            background: rgba(0,0,0,0.1);
            padding: 4px 8px;
            border-radius: 4px;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .command-card .cmd-actions {
            display: flex;
            gap: 6px;
            margin-top: auto;
        }
        .command-card .btn-exec {
            background: var(--success);
            color: white;
            flex: 1;
        }
        .command-card .btn-sm {
            padding: 4px 8px;
            font-size: 0.85em;
        }
        
        /* 模式匹配标签 */
        .cmd-patterns {
            display: flex;
            gap: 4px;
            margin-left: auto;
        }
        .pattern-tag {
            font-size: 0.9em;
            cursor: help;
            opacity: 0.7;
        }
        .pattern-tag:hover {
            opacity: 1;
        }
        
        /* 高级选项折叠面板 */
        .advanced-options {
            margin-top: 15px;
            border: 1px solid var(--border-color);
            border-radius: 8px;
            overflow: hidden;
        }
        .advanced-options summary {
            padding: 10px 15px;
            background: var(--bg-secondary);
            cursor: pointer;
            font-weight: 500;
            user-select: none;
        }
        .advanced-options summary:hover {
            background: rgba(var(--primary-rgb), 0.1);
        }
        .advanced-content {
            padding: 15px;
            border-top: 1px solid var(--border-color);
        }
        .advanced-content .form-group {
            margin-bottom: 12px;
        }
        .advanced-content small {
            display: block;
            color: #888;
            font-size: 0.8em;
            margin-top: 4px;
        }
        .checkbox-label {
            display: flex;
            align-items: center;
            gap: 8px;
            cursor: pointer;
        }
        .checkbox-label input[type="checkbox"] {
            width: 18px;
            height: 18px;
            cursor: pointer;
        }
        
        .exec-result {
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 15px;
            border-radius: 8px;
            font-family: monospace;
            font-size: 0.9em;
            max-height: 400px;
            overflow: auto;
            white-space: pre-wrap;
            word-break: break-all;
        }
        
        /* nohup 快捷操作按钮 */
        .nohup-actions {
            display: flex;
            gap: 8px;
            margin-bottom: 10px;
            flex-wrap: wrap;
        }
        .nohup-actions .btn {
            background: var(--primary-color);
            color: white;
        }
        .nohup-actions .btn:hover {
            filter: brightness(1.1);
        }
        
        /* 匹配结果面板 */
        .match-result-panel {
            margin-top: 15px;
            background: var(--bg-secondary);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 15px;
        }
        .match-panel-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 12px;
        }
        .match-panel-header h3 {
            margin: 0;
            font-size: 1em;
        }
        .match-status {
            padding: 4px 10px;
            border-radius: 12px;
            font-size: 0.85em;
            font-weight: 500;
        }
        .match-status.success {
            background: rgba(40, 167, 69, 0.2);
            color: #28a745;
        }
        .match-status.failed {
            background: rgba(220, 53, 69, 0.2);
            color: #dc3545;
        }
        .match-status.timeout {
            background: rgba(255, 193, 7, 0.2);
            color: #ffc107;
        }
        .match-status.extracting {
            background: rgba(0, 123, 255, 0.2);
            color: #007bff;
            animation: pulse 1.5s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.6; }
        }
        .match-result-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 12px;
        }
        .match-result-item {
            background: var(--bg-primary);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 10px 12px;
        }
        .match-label {
            font-size: 0.85em;
            color: #888;
            margin-bottom: 4px;
        }
        .match-value {
            font-weight: 600;
            font-size: 1em;
            margin-bottom: 6px;
            word-break: break-all;
        }
        .match-value.true {
            color: #28a745;
        }
        .match-value.false {
            color: #dc3545;
        }
        .match-extracted {
            font-family: monospace;
            font-size: 0.9em;
            max-height: 60px;
            overflow: auto;
        }
        .match-var {
            display: block;
            font-size: 0.75em;
            color: #6c757d;
            background: rgba(0,0,0,0.1);
            padding: 2px 6px;
            border-radius: 3px;
        }
        .match-api-hint {
            margin-top: 12px;
            padding-top: 10px;
            border-top: 1px solid var(--border-color);
            color: #888;
        }
        .match-api-hint code {
            background: rgba(0,0,0,0.1);
            padding: 2px 6px;
            border-radius: 3px;
            font-size: 0.85em;
        }
        
        .icon-picker {
            display: flex;
            flex-wrap: wrap;
            gap: 5px;
        }
        .icon-btn {
            width: 36px;
            height: 36px;
            font-size: 1.2em;
            border: 2px solid var(--border-color);
            border-radius: 6px;
            background: var(--bg-secondary);
            cursor: pointer;
        }
        .icon-btn:hover, .icon-btn.selected {
            border-color: var(--primary);
            background: rgba(var(--primary-rgb), 0.1);
        }
        
        .empty-state {
            text-align: center;
            padding: 40px;
            color: #666;
        }
    `;
    document.head.appendChild(style);
}

// 当前选中的主机
let selectedHostId = null;

async function loadHostSelector() {
    const container = document.getElementById('host-selector');
    
    try {
        const result = await api.call('ssh.hosts.list', {});
        const hosts = result.data?.hosts || [];
        
        if (hosts.length === 0) {
            container.innerHTML = `
                <div class="empty-state" style="width:100%">
                    <p>暂无已部署主机</p>
                    <p style="font-size:0.9em">请先到 <a href="#/security">安全</a> 页面部署 SSH 公钥</p>
                </div>
            `;
            return;
        }
        
        // 存储主机数据
        window._cmdHostsList = hosts;
        
        // 检查是否有孤儿命令
        const hasOrphanCommands = sshCommands['__orphan__'] && sshCommands['__orphan__'].length > 0;
        const orphanCount = hasOrphanCommands ? sshCommands['__orphan__'].length : 0;
        
        let html = hosts.map(h => `
            <div class="host-card ${selectedHostId === h.id ? 'selected' : ''}" 
                 onclick="selectHost('${escapeHtml(h.id)}')" 
                 data-host-id="${escapeHtml(h.id)}">
                <div class="host-name">🖥️ ${escapeHtml(h.id)}</div>
                <div class="host-info">${escapeHtml(h.username)}@${escapeHtml(h.host)}:${h.port}</div>
            </div>
        `).join('');
        
        // 如果有孤儿命令，添加特殊分组
        if (hasOrphanCommands) {
            html += `
            <div class="host-card orphan-group ${selectedHostId === '__orphan__' ? 'selected' : ''}" 
                 onclick="selectHost('__orphan__')" 
                 data-host-id="__orphan__"
                 style="background:#fff3cd;border-color:#ffc107">
                <div class="host-name">⚠️ 孤儿命令</div>
                <div class="host-info" style="color:#856404">${orphanCount} 个命令引用了不存在的主机</div>
            </div>
            `;
        }
        
        container.innerHTML = html;
        
        // 如果之前有选中的主机，刷新指令列表
        if (selectedHostId) {
            refreshCommandsList();
        }
        
    } catch (e) {
        container.innerHTML = `<div class="error">加载失败: ${e.message}</div>`;
    }
}

function selectHost(hostId) {
    selectedHostId = hostId;
    
    // 更新选中状态
    document.querySelectorAll('.host-card').forEach(card => {
        card.classList.toggle('selected', card.dataset.hostId === hostId);
    });
    
    // 刷新指令列表
    refreshCommandsList();
}

function refreshCommandsList() {
    const container = document.getElementById('commands-list');
    
    if (!selectedHostId) {
        container.innerHTML = '<div class="empty-state">请先选择一个主机</div>';
        return;
    }
    
    const hostCommands = sshCommands[selectedHostId] || [];
    
    if (hostCommands.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <p>该主机暂无指令</p>
                <button class="btn btn-primary" onclick="showAddCommandModal()">➕ 创建第一个指令</button>
            </div>
        `;
        return;
    }
    
    container.innerHTML = hostCommands.map((cmd, idx) => {
        // 孤儿命令警告（引用的主机不存在）
        const isOrphan = cmd.orphan === true;
        const orphanWarningHtml = isOrphan ? `
            <div class="orphan-warning" style="background:#fff3cd;border:1px solid #ffc107;border-radius:4px;padding:4px 8px;margin-bottom:8px;color:#856404;font-size:12px;">
                ⚠️ 主机 "${escapeHtml(cmd.originalHostId || '?')}" 不存在，请删除或重新关联
            </div>
        ` : '';
        
        // 构建模式匹配标签
        const hasPatternsConfig = cmd.expectPattern || cmd.failPattern || cmd.extractPattern;
        const patternsHtml = hasPatternsConfig ? `
            <div class="cmd-patterns">
                ${cmd.expectPattern ? '<span class="pattern-tag success" title="成功模式: ' + escapeHtml(cmd.expectPattern) + '">✅</span>' : ''}
                ${cmd.failPattern ? '<span class="pattern-tag fail" title="失败模式: ' + escapeHtml(cmd.failPattern) + '">❌</span>' : ''}
                ${cmd.extractPattern ? '<span class="pattern-tag extract" title="提取模式: ' + escapeHtml(cmd.extractPattern) + '">📋</span>' : ''}
            </div>
        ` : '';
        
        // nohup 标签（显示服务模式或普通后台执行）
        let nohupHtml = '';
        if (cmd.nohup) {
            if (cmd.serviceMode) {
                // 服务模式：显示服务状态标签（无色块背景）
                // 使用 cmd.id 作为唯一标识，避免多个服务时 ID 冲突
                const statusId = `service-status-${cmd.id || idx}`;
                nohupHtml = `<span class="service-mode-status" title="服务模式: ${escapeHtml(cmd.readyPattern)}" data-var="${escapeHtml(cmd.varName)}" data-status-id="${statusId}"><span id="${statusId}" class="service-status">...</span></span>`;
            } else {
                nohupHtml = '<span class="pattern-tag nohup" title="后台执行（nohup）">🚀</span>';
            }
        }
        
        // 变量按钮（仅当设置了 varName 时显示）
        const varBtnHtml = cmd.varName ? `<button class="btn btn-sm" onclick="showCommandVariables('${escapeHtml(cmd.varName)}')" title="查看变量: ${escapeHtml(cmd.varName)}.*">📊</button>` : '';
        
        // 服务模式按钮（日志、停止）
        const safeName = cmd.name.replace(/[^a-zA-Z0-9]/g, '').slice(0, 20) || 'cmd';
        const serviceActionsHtml = (cmd.nohup && cmd.serviceMode) ? `
            <button class="btn btn-sm" onclick="viewServiceLog(${idx}, '${escapeHtml(safeName)}')" title="查看日志">📄</button>
            <button class="btn btn-sm" onclick="stopServiceProcess(${idx}, '${escapeHtml(safeName)}')" title="停止服务" style="background:#dc3545;color:white">⏹️</button>
        ` : '';
        
        // 图标显示：支持 Emoji 或图片路径
        const iconValue = cmd.icon || '🚀';
        const iconHtml = iconValue.startsWith('/sdcard/') 
            ? `<span class="cmd-icon"><img src="/api/v1/file/download?path=${encodeURIComponent(iconValue)}" alt="icon" onerror="this.parentElement.textContent='🚀'"></span>`
            : `<span class="cmd-icon">${iconValue}</span>`;
        
        // 孤儿命令禁用执行按钮
        const execBtnDisabled = isOrphan ? 'disabled style="opacity:0.5;cursor:not-allowed"' : '';
        
        return `
        <div class="command-card ${isOrphan ? 'orphan-command' : ''}" data-cmd-idx="${idx}" data-has-service="${cmd.serviceMode || false}" ${isOrphan ? 'style="border:2px solid #ffc107;background:#fffbe6"' : ''}>
            ${orphanWarningHtml}
            <div class="cmd-header">
                ${iconHtml}
                <span class="cmd-name" title="${escapeHtml(cmd.name)}">${escapeHtml(cmd.name)}</span>
                ${nohupHtml}
                ${patternsHtml}
            </div>
            ${cmd.desc ? `<div class="cmd-desc" title="${escapeHtml(cmd.desc)}">${escapeHtml(cmd.desc)}</div>` : ''}
            <div class="cmd-code" title="${escapeHtml(cmd.command)}">${escapeHtml(cmd.command.split('\n')[0])}${cmd.command.includes('\n') ? ' ...' : ''}</div>
            <div class="cmd-actions">
                <button class="btn btn-sm btn-exec" onclick="executeCommand(${idx})" title="${isOrphan ? '主机不存在，无法执行' : '执行'}" ${execBtnDisabled}>▶️</button>
                ${serviceActionsHtml}
                ${varBtnHtml}
                <button class="btn btn-sm" onclick="exportSshCommand('${escapeHtml(cmd.id)}')" title="导出配置" style="background:#17a2b8;color:white">📤</button>
                <button class="btn btn-sm" onclick="editCommand(${idx})" title="编辑">✏️</button>
                <button class="btn btn-sm" onclick="deleteCommand(${idx})" title="删除" style="background:#dc3545;color:white">🗑️</button>
            </div>
        </div>
    `}).join('');
    
    // 更新服务模式状态
    updateServiceStatusInList();
}

/**
 * 更新指令列表中的服务状态
 * 查询每个服务模式指令的变量状态并更新显示
 */
async function updateServiceStatusInList() {
    const serviceModeTags = document.querySelectorAll('.service-mode-status');
    if (serviceModeTags.length === 0) return;
    
    console.log(`[ServiceStatus] Updating ${serviceModeTags.length} service status tags`);
    
    for (const tag of serviceModeTags) {
        const varName = tag.dataset.var;
        const statusId = tag.dataset.statusId;
        const statusEl = document.getElementById(statusId);
        
        console.log(`[ServiceStatus] Processing: varName=${varName}, statusId=${statusId}, statusEl=${statusEl ? 'found' : 'NOT FOUND'}`);
        
        if (!varName || !statusEl) {
            console.warn(`[ServiceStatus] Skipping: varName=${varName}, statusEl=${!!statusEl}`);
            continue;
        }
        
        try {
            const result = await api.call('automation.variables.get', { name: `${varName}.status` });
            console.log(`[ServiceStatus] ${varName}.status =`, result?.data?.value);
            
            if (result && result.data && result.data.value !== undefined) {
                const status = result.data.value;
                statusEl.textContent = getServiceStatusLabel(status);
                statusEl.className = `service-status status-${status}`;
            } else {
                statusEl.textContent = '⏸️ 未启动';
                statusEl.className = 'service-status status-idle';
            }
        } catch (e) {
            console.error(`[ServiceStatus] Error getting ${varName}.status:`, e);
            statusEl.textContent = '❓ 未知';
            statusEl.className = 'service-status status-unknown';
        }
    }
}

/**
 * 获取服务状态显示文本
 */
function getServiceStatusLabel(status) {
    const labels = {
        'ready': '✅ 就绪',
        'checking': '🔄 检测中',
        'timeout': '⚠️ 超时',
        'failed': '❌ 失败',
        'idle': '⏸️ 未启动',
        'stopped': '⏹️ 已停止'
    };
    return labels[status] || status;
}

function showAddCommandModal() {
    if (!selectedHostId) {
        showToast('请先选择一个主机', 'warning');
        return;
    }
    
    document.getElementById('command-modal-title').textContent = '➕ 新建指令';
    
    /* 新建模式：ID 可编辑 */
    const idInput = document.getElementById('cmd-edit-id');
    const idGroup = document.getElementById('cmd-id-group');
    idInput.value = '';
    idInput.readOnly = false;
    idInput.style.backgroundColor = '';
    idInput.style.cursor = '';
    idInput.style.borderColor = '';
    idGroup.classList.remove('edit-mode');
    document.getElementById('cmd-id-error').style.display = 'none';
    
    document.getElementById('cmd-name').value = '';
    document.getElementById('cmd-command').value = '';
    document.getElementById('cmd-desc').value = '';
    document.getElementById('cmd-icon').value = '🚀';
    document.getElementById('cmd-icon-type').value = 'emoji';
    document.getElementById('cmd-icon-path').value = '';
    
    // 重置图标选择 UI
    switchCmdIconType('emoji');
    updateCmdIconPreview(null);
    
    // 重置 nohup 选项
    const nohupCheckbox = document.getElementById('cmd-nohup');
    if (nohupCheckbox) nohupCheckbox.checked = false;
    
    // 重置高级选项
    document.getElementById('cmd-expect-pattern').value = '';
    document.getElementById('cmd-fail-pattern').value = '';
    document.getElementById('cmd-extract-pattern').value = '';
    document.getElementById('cmd-var-name').value = '';
    document.getElementById('cmd-timeout').value = 30;
    document.getElementById('cmd-stop-on-match').checked = false;
    
    // 重置服务模式选项
    const serviceModeCheckbox = document.getElementById('cmd-service-mode');
    if (serviceModeCheckbox) serviceModeCheckbox.checked = false;
    document.getElementById('cmd-ready-pattern').value = '';
    document.getElementById('cmd-ready-timeout').value = 120;
    document.getElementById('cmd-ready-interval').value = 5000;
    
    // 折叠高级选项面板
    const advDetails = document.querySelector('.advanced-options');
    if (advDetails) advDetails.open = false;
    
    // 重置图标选中状态
    document.querySelectorAll('.icon-btn').forEach(btn => btn.classList.remove('selected'));
    document.querySelector('.icon-btn')?.classList.add('selected');
    
    document.getElementById('command-modal').classList.remove('hidden');
    
    // 更新超时输入框状态
    updateTimeoutState();
    // 更新 nohup 状态
    updateNohupState();
    // 更新服务模式状态
    updateServiceModeState();
}

function closeCommandModal() {
    document.getElementById('command-modal').classList.add('hidden');
}

/**
 * 显示指令变量
 * @param {string} varName - 变量名前缀（不含 cmd.）
 */
async function showCommandVariables(varName) {
    const modal = document.getElementById('source-variables-modal');
    const body = document.getElementById('source-variables-body');
    if (!modal || !body) return;
    
    // 更新标题
    const header = modal.querySelector('.modal-header h2');
    if (header) header.textContent = `📊 指令变量: ${varName}.*`;
    
    body.innerHTML = '<div class="loading">加载中...</div>';
    modal.classList.remove('hidden');
    
    try {
        const result = await api.call('automation.variables.list');
        if (result.code === 0 && result.data && result.data.variables) {
            // 过滤出属于该指令的变量
            // SSH 指令变量的 source_id 就是 varName（不带 cmd. 前缀）
            // 变量名格式为 varName.status, varName.exit_code, varName.extracted 等
            const vars = result.data.variables.filter(v => 
                v.source_id === varName || v.name.startsWith(varName + '.'));
            
            if (vars.length === 0) {
                body.innerHTML = '<p style="text-align:center;color:var(--text-light);padding:20px">该指令暂无变量数据，请先执行一次</p>';
                return;
            }
            
            body.innerHTML = `
                <table class="data-table">
                    <thead>
                        <tr>
                            <th>变量名</th>
                            <th>类型</th>
                            <th>当前值</th>
                            <th>更新时间</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${vars.map(v => `
                            <tr>
                                <td><code class="variable-name">${v.name}</code></td>
                                <td><span class="type-badge type-${v.type || 'unknown'}">${v.type || '-'}</span></td>
                                <td class="variable-value">${formatVariableValue(v.value, v.type)}</td>
                                <td class="variable-time">${v.updated_at ? formatTimeAgo(v.updated_at) : '-'}</td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            `;
        } else {
            body.innerHTML = `<p style="text-align:center;color:var(--danger-color)">⚠️ ${result.message || '获取变量失败'}</p>`;
        }
    } catch (error) {
        body.innerHTML = `<p style="text-align:center;color:var(--danger-color)">❌ ${error.message}</p>`;
    }
}

/* 更新超时输入框的启用状态 */
function updateTimeoutState() {
    const expectPattern = document.getElementById('cmd-expect-pattern')?.value?.trim();
    const failPattern = document.getElementById('cmd-fail-pattern')?.value?.trim();
    const stopOnMatch = document.getElementById('cmd-stop-on-match')?.checked;
    
    const timeoutGroup = document.getElementById('cmd-timeout-group');
    const timeoutInput = document.getElementById('cmd-timeout');
    const timeoutHint = document.getElementById('cmd-timeout-hint');
    
    /* 超时在以下情况有效：设定了成功/失败条件，或勾选了匹配后停止 */
    const isTimeoutEffective = stopOnMatch || expectPattern || failPattern;
    
    if (timeoutGroup) {
        timeoutGroup.style.opacity = isTimeoutEffective ? '1' : '0.5';
    }
    if (timeoutInput) {
        timeoutInput.disabled = !isTimeoutEffective;
    }
    if (timeoutHint) {
        timeoutHint.textContent = isTimeoutEffective 
            ? '匹配超时后命令将被终止' 
            : '超时仅在设置了成功/失败模式或勾选了"匹配后停止"时有效';
        timeoutHint.style.color = isTimeoutEffective ? '' : 'var(--text-muted)';
    }
}

/* 更新 nohup 选项的状态（显示/隐藏服务模式选项，禁用模式匹配选项） */
function updateNohupState() {
    const nohup = document.getElementById('cmd-nohup')?.checked;
    const patternOptions = document.getElementById('cmd-pattern-options');
    const serviceModeOptions = document.getElementById('cmd-service-mode-options');
    const varNameGroup = document.getElementById('cmd-var-name-group');
    const varNameHint = document.getElementById('cmd-var-name-hint');
    
    // 显示/隐藏服务模式选项（仅 nohup 启用时显示）
    if (serviceModeOptions) {
        serviceModeOptions.classList.toggle('hidden', !nohup);
    }
    
    // nohup 模式下隐藏模式匹配选项
    if (patternOptions) {
        patternOptions.classList.toggle('hidden', nohup);
    }
    
    // 更新变量名提示
    if (varNameGroup && varNameHint) {
        if (nohup) {
            varNameHint.innerHTML = '服务模式下，状态变量为 <code>${变量名}.status</code>（ready/checking/timeout）';
        } else {
            varNameHint.innerHTML = '执行结果将存储为 <code>${变量名}.status</code>、<code>${变量名}.extracted</code> 等，可在后续命令中引用';
        }
    }
    
    // 如果启用 nohup，清空模式匹配选项
    if (nohup) {
        const fields = ['cmd-expect-pattern', 'cmd-fail-pattern', 'cmd-extract-pattern'];
        fields.forEach(id => {
            const el = document.getElementById(id);
            if (el) el.value = '';
        });
        const stopMatch = document.getElementById('cmd-stop-on-match');
        if (stopMatch) stopMatch.checked = false;
    } else {
        // 禁用 nohup 时，重置服务模式
        const serviceMode = document.getElementById('cmd-service-mode');
        if (serviceMode) serviceMode.checked = false;
        updateServiceModeState();
    }
}

/* 更新服务模式选项的状态（显示/隐藏配置字段） */
function updateServiceModeState() {
    const serviceMode = document.getElementById('cmd-service-mode')?.checked;
    const serviceModeFields = document.getElementById('cmd-service-mode-fields');
    const varNameInput = document.getElementById('cmd-var-name');
    
    if (serviceModeFields) {
        serviceModeFields.classList.toggle('hidden', !serviceMode);
    }
    
    // 如果启用服务模式，变量名字段变为必填并提示
    if (varNameInput) {
        if (serviceMode) {
            varNameInput.placeholder = '必填，例如：vllm（用于状态变量）';
            varNameInput.style.borderColor = varNameInput.value ? '' : 'var(--warning-color)';
        } else {
            varNameInput.placeholder = '例如：ping_test';
            varNameInput.style.borderColor = '';
        }
    }
}

/**
 * 切换图标类型（Emoji / 图片）
 */
function switchCmdIconType(type) {
    const iconTypeInput = document.getElementById('cmd-icon-type');
    iconTypeInput.value = type;
    
    // 更新 Tab 状态
    document.querySelectorAll('.icon-type-tabs .icon-tab').forEach(tab => {
        tab.classList.toggle('active', tab.textContent.includes(type === 'emoji' ? 'Emoji' : '图片'));
    });
    
    // 切换面板显示
    document.getElementById('icon-emoji-picker').classList.toggle('hidden', type !== 'emoji');
    document.getElementById('icon-image-picker').classList.toggle('hidden', type !== 'image');
    
    // 如果切换到 Emoji 且当前是图片，恢复默认
    if (type === 'emoji') {
        const currentIcon = document.getElementById('cmd-icon').value;
        if (currentIcon.startsWith('/sdcard/')) {
            document.getElementById('cmd-icon').value = '🚀';
            selectCmdIcon('🚀');
        }
    }
}

/**
 * 浏览 SD 卡图像
 */
async function browseCmdIconImage() {
    filePickerCurrentPath = '/sdcard/images';
    filePickerSelectedFile = null;
    filePickerCallback = (path) => {
        document.getElementById('cmd-icon').value = path;
        document.getElementById('cmd-icon-path').value = path;
        updateCmdIconPreview(path);
    };
    document.getElementById('file-picker-modal').classList.remove('hidden');
    await loadFilePickerDirectory(filePickerCurrentPath);
}

/**
 * 更新图标图片预览
 */
function updateCmdIconPreview(path) {
    const preview = document.getElementById('cmd-icon-preview');
    if (path && path.startsWith('/sdcard/')) {
        preview.innerHTML = `<img src="/api/v1/file/download?path=${encodeURIComponent(path)}" alt="icon" onerror="this.parentElement.innerHTML='<span class=\\'preview-placeholder\\'>加载失败</span>'">`;
    } else {
        preview.innerHTML = '<span class="preview-placeholder">无</span>';
    }
}

/**
 * 清除图标图片
 */
function clearCmdIconImage() {
    document.getElementById('cmd-icon').value = '🚀';
    document.getElementById('cmd-icon-path').value = '';
    document.getElementById('cmd-icon-type').value = 'emoji';
    updateCmdIconPreview(null);
    switchCmdIconType('emoji');
}

function selectCmdIcon(icon) {
    document.getElementById('cmd-icon').value = icon;
    document.getElementById('cmd-icon-type').value = 'emoji';
    document.querySelectorAll('.icon-btn').forEach(btn => {
        btn.classList.toggle('selected', btn.textContent === icon);
    });
}

/**
 * 验证指令 ID 格式
 * 规则：只允许字母、数字、下划线、连字符，不能以 _ 或 - 开头/结尾
 */
function validateCommandId(input) {
    const value = input.value;
    const errorSpan = document.getElementById('cmd-id-error');
    
    if (!value) {
        input.style.borderColor = '';
        errorSpan.style.display = 'none';
        return false;
    }
    
    // 验证规则
    const validPattern = /^[a-zA-Z0-9][a-zA-Z0-9_-]*[a-zA-Z0-9]$|^[a-zA-Z0-9]$/;
    const hasInvalidChars = /[^a-zA-Z0-9_-]/.test(value);
    const startsWithInvalid = /^[_-]/.test(value);
    const endsWithInvalid = /[_-]$/.test(value);
    
    let errorMsg = '';
    if (hasInvalidChars) {
        errorMsg = '只允许字母、数字、下划线、连字符';
    } else if (startsWithInvalid) {
        errorMsg = '不能以 _ 或 - 开头';
    } else if (endsWithInvalid) {
        errorMsg = '不能以 _ 或 - 结尾';
    } else if (value.length > 31) {
        errorMsg = 'ID 过长（最多 31 个字符）';
    }
    
    if (errorMsg) {
        input.style.borderColor = 'var(--danger-color)';
        errorSpan.textContent = '⚠️ ' + errorMsg;
        errorSpan.style.display = 'block';
        return false;
    }
    
    input.style.borderColor = 'var(--success-color)';
    errorSpan.style.display = 'none';
    return true;
}

async function saveCommand() {
    const cmdId = document.getElementById('cmd-edit-id').value.trim();
    const name = document.getElementById('cmd-name').value.trim();
    const command = document.getElementById('cmd-command').value.trim();
    const desc = document.getElementById('cmd-desc').value.trim();
    const icon = document.getElementById('cmd-icon').value;
    const nohup = document.getElementById('cmd-nohup')?.checked || false;
    const expectPattern = document.getElementById('cmd-expect-pattern').value.trim();
    const failPattern = document.getElementById('cmd-fail-pattern').value.trim();
    const extractPattern = document.getElementById('cmd-extract-pattern').value.trim();
    const varName = document.getElementById('cmd-var-name').value.trim();
    const timeout = parseInt(document.getElementById('cmd-timeout').value) || 30;
    const stopOnMatch = document.getElementById('cmd-stop-on-match').checked;
    const isEditMode = document.getElementById('cmd-id-group').classList.contains('edit-mode');
    
    // 服务模式字段
    const serviceMode = document.getElementById('cmd-service-mode')?.checked || false;
    const readyPattern = document.getElementById('cmd-ready-pattern')?.value?.trim() || '';
    const serviceFailPattern = document.getElementById('cmd-service-fail-pattern')?.value?.trim() || '';
    const readyTimeout = parseInt(document.getElementById('cmd-ready-timeout')?.value) || 120;
    const readyInterval = parseInt(document.getElementById('cmd-ready-interval')?.value) || 5000;
    
    if (!name || !command) {
        showToast('请填写指令名称和命令', 'warning');
        return;
    }
    
    /* ID 验证（必填） */
    if (!cmdId) {
        showToast('请填写指令 ID', 'warning');
        document.getElementById('cmd-edit-id').focus();
        return;
    }
    if (!validateCommandId(document.getElementById('cmd-edit-id'))) {
        showToast('指令 ID 格式不正确', 'warning');
        document.getElementById('cmd-edit-id').focus();
        return;
    }
    
    // 服务模式验证
    if (nohup && serviceMode && !readyPattern) {
        showToast('启用服务模式时必须设置就绪匹配模式', 'warning');
        return;
    }
    if (nohup && serviceMode && !varName) {
        showToast('启用服务模式时必须设置变量名', 'warning');
        return;
    }
    
    if (!sshCommands[selectedHostId]) {
        sshCommands[selectedHostId] = [];
    }
    
    const cmdData = { 
        name, command, desc, icon,
        // nohup 后台执行（优先于模式匹配）
        ...(nohup && { nohup: true }),
        // 高级选项（仅在有值时保存，nohup 时忽略）
        ...(!nohup && expectPattern && { expectPattern }),
        ...(!nohup && failPattern && { failPattern }),
        ...(!nohup && extractPattern && { extractPattern }),
        ...(varName && { varName }),  // varName 现在在 nohup 模式下也保留（用于服务模式）
        ...(!nohup && timeout !== 30 && { timeout }),
        ...(!nohup && stopOnMatch && { stopOnMatch }),
        // 服务模式字段（仅 nohup 时有效）
        ...(nohup && serviceMode && { serviceMode: true }),
        ...(nohup && serviceMode && readyPattern && { readyPattern }),
        ...(nohup && serviceMode && serviceFailPattern && { serviceFailPattern }),
        ...(nohup && serviceMode && readyTimeout !== 120 && { readyTimeout }),
        ...(nohup && serviceMode && readyInterval !== 5000 && { readyInterval })
    };
    
    try {
        /* 
         * ID 由用户在前端输入，直接传给后端
         * 后端会验证 ID 格式，如果 ID 已存在则执行更新
         */
        const savedId = await saveSshCommandToBackend(selectedHostId, cmdData, cmdId);
        cmdData.id = savedId;
        
        if (isEditMode) {
            // 编辑模式：更新本地缓存（根据 ID 查找）
            const existingIdx = sshCommands[selectedHostId].findIndex(c => c.id === cmdId);
            if (existingIdx >= 0) {
                sshCommands[selectedHostId][existingIdx] = cmdData;
            }
            showToast('指令已更新', 'success');
        } else {
            // 新建模式：添加到本地缓存
            sshCommands[selectedHostId].push(cmdData);
            showToast('指令已创建', 'success');
        }
        
        closeCommandModal();
        refreshCommandsList();
        
    } catch (e) {
        console.error('Failed to save command:', e);
        showToast('保存指令失败: ' + e.message, 'error');
    }
}

function editCommand(idx) {
    const cmd = sshCommands[selectedHostId]?.[idx];
    if (!cmd) return;
    
    document.getElementById('command-modal-title').textContent = '✏️ 编辑指令';
    
    /* 编辑模式：设置 ID 并标记为只读 */
    const idInput = document.getElementById('cmd-edit-id');
    const idGroup = document.getElementById('cmd-id-group');
    idInput.value = cmd.id || '';
    idInput.readOnly = true;
    idInput.style.backgroundColor = 'var(--bg-tertiary)';
    idInput.style.cursor = 'not-allowed';
    idGroup.classList.add('edit-mode');
    
    document.getElementById('cmd-name').value = cmd.name;
    document.getElementById('cmd-command').value = cmd.command;
    document.getElementById('cmd-desc').value = cmd.desc || '';
    
    // 处理图标：判断是 Emoji 还是图片路径
    const icon = cmd.icon || '🚀';
    document.getElementById('cmd-icon').value = icon;
    
    if (icon.startsWith('/sdcard/')) {
        // 图片路径
        document.getElementById('cmd-icon-type').value = 'image';
        document.getElementById('cmd-icon-path').value = icon;
        switchCmdIconType('image');
        updateCmdIconPreview(icon);
    } else {
        // Emoji
        document.getElementById('cmd-icon-type').value = 'emoji';
        document.getElementById('cmd-icon-path').value = '';
        switchCmdIconType('emoji');
        updateCmdIconPreview(null);
    }
    
    // nohup 选项
    const nohupCheckbox = document.getElementById('cmd-nohup');
    if (nohupCheckbox) {
        nohupCheckbox.checked = cmd.nohup || false;
    }
    
    // 高级选项
    document.getElementById('cmd-expect-pattern').value = cmd.expectPattern || '';
    document.getElementById('cmd-fail-pattern').value = cmd.failPattern || '';
    document.getElementById('cmd-extract-pattern').value = cmd.extractPattern || '';
    document.getElementById('cmd-var-name').value = cmd.varName || '';
    document.getElementById('cmd-timeout').value = cmd.timeout || 30;
    document.getElementById('cmd-stop-on-match').checked = cmd.stopOnMatch || false;
    
    // 服务模式选项
    const serviceModeCheckbox = document.getElementById('cmd-service-mode');
    if (serviceModeCheckbox) {
        serviceModeCheckbox.checked = cmd.serviceMode || false;
    }
    document.getElementById('cmd-ready-pattern').value = cmd.readyPattern || '';
    document.getElementById('cmd-service-fail-pattern').value = cmd.serviceFailPattern || '';
    document.getElementById('cmd-ready-timeout').value = cmd.readyTimeout || 120;
    document.getElementById('cmd-ready-interval').value = cmd.readyInterval || 5000;
    
    // 如果有高级选项，展开面板
    const advDetails = document.querySelector('.advanced-options');
    if (advDetails && (cmd.nohup || cmd.expectPattern || cmd.failPattern || cmd.extractPattern || cmd.varName || cmd.timeout !== 30 || cmd.stopOnMatch || cmd.serviceMode)) {
        advDetails.open = true;
    }
    
    // 更新图标选中状态
    document.querySelectorAll('.icon-btn').forEach(btn => {
        btn.classList.toggle('selected', btn.textContent === (cmd.icon || '🚀'));
    });
    
    document.getElementById('command-modal').classList.remove('hidden');
    
    // 更新超时输入框状态
    updateTimeoutState();
    // 更新 nohup 状态（显示/隐藏服务模式选项）
    updateNohupState();
    // 更新服务模式状态
    updateServiceModeState();
}

/**
 * 导出 SSH 指令配置为 .tscfg 文件
 * 开发机：显示模态框输入目标证书
 * 非开发机：使用设备证书自加密
 */
async function exportSshCommand(cmdId) {
    // 确保已加载设备类型信息
    if (!window._configPackStatus) {
        try {
            const result = await api.configPackInfo();
            window._configPackStatus = result.data;
        } catch (e) {
            console.warn('无法获取设备类型信息，使用默认导出', e);
        }
    }
    
    // 检查设备类型
    const canExport = window._configPackStatus?.can_export;
    
    if (canExport) {
        // 开发机：显示模态框让用户输入目标证书和选项
        showExportSshCommandModal(cmdId);
    } else {
        // 非开发机：直接使用设备证书加密，询问是否包含主机
        const includeHost = confirm('是否同时导出该指令依赖的主机配置？\n\n点击「确定」将主机配置一起打包（推荐），点击「取消」仅导出指令。');
        await doExportSshCommand(cmdId, null, includeHost);
    }
}

/**
 * 显示导出 SSH 指令模态框（开发机专用）
 */
function showExportSshCommandModal(cmdId) {
    let modal = document.getElementById('export-ssh-cmd-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'export-ssh-cmd-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📤 导出 SSH 指令配置</h2>
            <p style="color:#666;font-size:0.9rem">导出指令 <strong>${escapeHtml(cmdId)}</strong> 的配置为加密配置包</p>
            
            <div class="form-group" style="margin-top:15px">
                <label>
                    <input type="checkbox" id="export-ssh-cmd-include-host" checked> 同时导出依赖的主机配置
                </label>
                <div style="font-size:0.85em;color:#666;margin-top:4px">💡 推荐勾选，便于在目标设备完整导入</div>
            </div>
            
            <div class="form-group">
                <label>目标设备证书 (PEM)</label>
                <textarea id="export-ssh-cmd-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:120px;font-family:monospace;font-size:11px"></textarea>
                <div style="font-size:0.85em;color:#666;margin-top:4px">💡 粘贴目标设备的证书。留空则使用本机证书（自加密）</div>
            </div>
            
            <div id="export-ssh-cmd-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideExportSshCommandModal()">取消</button>
                <button class="btn btn-primary" id="export-ssh-cmd-btn" onclick="doExportSshCommandFromModal('${escapeHtml(cmdId)}')">📤 导出</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function hideExportSshCommandModal() {
    const modal = document.getElementById('export-ssh-cmd-modal');
    if (modal) modal.classList.add('hidden');
}

async function doExportSshCommandFromModal(cmdId) {
    const certText = document.getElementById('export-ssh-cmd-cert').value.trim();
    const includeHost = document.getElementById('export-ssh-cmd-include-host').checked;
    const resultBox = document.getElementById('export-ssh-cmd-result');
    const exportBtn = document.getElementById('export-ssh-cmd-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成配置包...';
    exportBtn.disabled = true;
    
    try {
        await doExportSshCommand(cmdId, certText || null, includeHost);
        resultBox.className = 'result-box success';
        resultBox.textContent = '✅ 导出成功！';
        setTimeout(() => hideExportSshCommandModal(), 1000);
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    } finally {
        exportBtn.disabled = false;
    }
}

/**
 * 执行导出 SSH 指令
 * @param {string} cmdId - 指令 ID
 * @param {string|null} recipientCert - 目标证书（null 使用设备证书）
 * @param {boolean} includeHost - 是否包含主机配置
 */
async function doExportSshCommand(cmdId, recipientCert, includeHost) {
    const params = { 
        id: cmdId,
        include_host: includeHost
    };
    if (recipientCert) {
        params.recipient_cert = recipientCert;
    }
    
    const result = await api.call('ssh.commands.export', params);
    
    if (result.code !== 0) {
        throw new Error(result.message || '导出失败');
    }
    
    const data = result.data;
    if (!data?.tscfg) {
        throw new Error('无效的响应数据');
    }
    
    // 下载文件
    const blob = new Blob([data.tscfg], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = data.filename || `${cmdId}.tscfg`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    
    const msg = data.host_included 
        ? `已导出指令配置（包含主机 ${data.host_id}）: ${data.filename}`
        : `已导出指令配置: ${data.filename}`;
    showToast(msg, 'success');
}

/**
 * 显示导入 SSH 指令配置弹窗
 */
async function showImportSshCommandModal() {
    let modal = document.getElementById('import-ssh-cmd-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'import-ssh-cmd-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    // 加载主机列表用于下拉选择
    let hostsOptions = '<option value="">-- 使用配置中的主机 --</option>';
    try {
        const result = await api.call('ssh.hosts.list', {});
        const hosts = result.data?.hosts || [];
        for (const h of hosts) {
            hostsOptions += `<option value="${escapeHtml(h.id)}">${escapeHtml(h.id)} (${escapeHtml(h.host)}:${h.port})</option>`;
        }
    } catch (e) {
        console.warn('Failed to load hosts list:', e);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📥 导入 SSH 指令配置</h2>
            <p style="color:#666;font-size:0.9rem">选择 .tscfg 配置包文件以导入 SSH 指令</p>
            
            <!-- 步骤 1: 选择文件 -->
            <div id="import-ssh-cmd-step1">
                <div class="form-group" style="margin-top:15px">
                    <label>选择文件</label>
                    <input type="file" id="import-ssh-cmd-file" class="form-control" accept=".tscfg" onchange="previewSshCommandImport()">
                </div>
            </div>
            
            <!-- 步骤 2: 预览 (默认隐藏) -->
            <div id="import-ssh-cmd-step2" style="display:none">
                <div class="info-card" style="background:#f8f9fa;padding:15px;border-radius:8px;margin-top:15px">
                    <h4 style="margin:0 0 10px 0">📋 配置包内容</h4>
                    <div id="import-ssh-cmd-preview"></div>
                </div>
                <div class="form-group" style="margin-top:15px">
                    <label>
                        <input type="checkbox" id="import-ssh-cmd-overwrite"> 覆盖已存在的配置
                    </label>
                </div>
                <div class="form-group" id="import-ssh-cmd-host-group" style="display:none">
                    <label>
                        <input type="checkbox" id="import-ssh-cmd-host" checked> 同时导入包含的主机配置
                    </label>
                </div>
                <div class="form-group">
                    <label>绑定到主机（可选）</label>
                    <select id="import-ssh-cmd-target-host" class="form-control">
                        ${hostsOptions}
                    </select>
                    <small style="color:#888">留空则使用配置包中指定的主机</small>
                </div>
            </div>
            
            <div id="import-ssh-cmd-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideImportSshCommandModal()">取消</button>
                <button class="btn btn-primary" id="import-ssh-cmd-btn" onclick="confirmSshCommandImport()" disabled>📥 确认导入</button>
            </div>
        </div>
    `;
    
    window._importSshCmdTscfg = null;
    modal.classList.remove('hidden');
}

function hideImportSshCommandModal() {
    const modal = document.getElementById('import-ssh-cmd-modal');
    if (modal) modal.classList.add('hidden');
    window._importSshCmdTscfg = null;
}

/**
 * 预览 SSH 指令导入内容
 */
async function previewSshCommandImport() {
    const fileInput = document.getElementById('import-ssh-cmd-file');
    const resultBox = document.getElementById('import-ssh-cmd-result');
    const step2 = document.getElementById('import-ssh-cmd-step2');
    const previewDiv = document.getElementById('import-ssh-cmd-preview');
    const hostGroup = document.getElementById('import-ssh-cmd-host-group');
    const importBtn = document.getElementById('import-ssh-cmd-btn');
    
    if (!fileInput.files || !fileInput.files[0]) return;
    
    const file = fileInput.files[0];
    
    resultBox.classList.remove('hidden', 'success', 'error', 'warning');
    resultBox.textContent = '🔄 正在验证配置包...';
    importBtn.disabled = true;
    step2.style.display = 'none';
    
    try {
        const content = await file.text();
        window._importSshCmdTscfg = content;
        window._importSshCmdFilename = file.name;  // 保存文件名
        
        const result = await api.call('ssh.commands.import', { 
            tscfg: content,
            filename: file.name,
            preview: true
        });
        
        if (result.code === 0 && result.data?.valid) {
            const data = result.data;
            
            // 轻量级验证只返回基本信息
            let html = `
                <table style="width:100%;font-size:0.9em">
                    <tr><td style="width:80px;color:#666">配置 ID:</td><td><code>${escapeHtml(data.id)}</code></td></tr>
                    <tr><td style="color:#666">类型:</td><td>${data.type === 'ssh_command' ? '📋 SSH 指令' : data.type}</td></tr>
                    <tr><td style="color:#666">签名者:</td><td>${escapeHtml(data.signer)} ${data.official ? '✅ 官方' : ''}</td></tr>
                    <tr><td style="color:#666">备注:</td><td style="color:#888;font-size:0.85em">${escapeHtml(data.note || '重启后自动加载')}</td></tr>
                </table>
            `;
            
            // 隐藏主机选项（因为现在不解密，不知道是否包含主机）
            if (hostGroup) hostGroup.style.display = 'none';
            
            if (data.exists) {
                html += `<div style="margin-top:10px;padding:8px;background:#fff3cd;border-radius:4px;color:#856404">⚠️ 该配置已存在，导入将覆盖现有文件</div>`;
            }
            
            previewDiv.innerHTML = html;
            step2.style.display = 'block';
            resultBox.className = 'result-box success';
            resultBox.textContent = '✅ 签名验证通过';
            importBtn.disabled = false;
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '无法验证配置包');
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    }
}

/**
 * 确认导入 SSH 指令
 */
async function confirmSshCommandImport() {
    const overwrite = document.getElementById('import-ssh-cmd-overwrite').checked;
    const resultBox = document.getElementById('import-ssh-cmd-result');
    const importBtn = document.getElementById('import-ssh-cmd-btn');
    
    if (!window._importSshCmdTscfg) {
        showToast('请先选择文件', 'error');
        return;
    }
    
    resultBox.classList.remove('hidden', 'success', 'error', 'warning');
    resultBox.textContent = '🔄 正在保存配置...';
    importBtn.disabled = true;
    
    try {
        const params = { 
            tscfg: window._importSshCmdTscfg,
            filename: window._importSshCmdFilename,
            overwrite: overwrite
        };
        
        const result = await api.call('ssh.commands.import', params);
        
        if (result.code === 0) {
            const data = result.data;
            if (data?.exists && !data?.imported) {
                resultBox.className = 'result-box warning';
                resultBox.textContent = `⚠️ 配置 ${data.id} 已存在，请勾选「覆盖」选项`;
                importBtn.disabled = false;
            } else {
                resultBox.className = 'result-box success';
                resultBox.innerHTML = `✅ 已保存配置: <code>${escapeHtml(data?.id)}</code><br><small style="color:#666">重启系统后生效</small>`;
                showToast(`已导入配置，重启后生效`, 'success');
                // 不刷新列表，因为还没加载
                setTimeout(() => hideImportSshCommandModal(), 2000);
            }
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '导入失败');
            importBtn.disabled = false;
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
        importBtn.disabled = false;
    }
}

async function deleteCommand(idx) {
    const cmd = sshCommands[selectedHostId]?.[idx];
    if (!cmd) return;
    
    if (!confirm(`确定要删除指令「${cmd.name}」吗？`)) return;
    
    try {
        // 从后端删除（需要指令 ID）
        if (cmd.id) {
            await deleteSshCommandFromBackend(cmd.id);
        }
        
        // 从本地缓存删除
        sshCommands[selectedHostId].splice(idx, 1);
        refreshCommandsList();
        showToast('指令已删除', 'success');
    } catch (e) {
        console.error('Failed to delete command:', e);
        showToast('删除指令失败: ' + e.message, 'error');
    }
}

/* 当前执行中的会话 ID */
let currentExecSessionId = null;

/* nohup 相关状态（用于快捷按钮） */
let currentNohupInfo = {
    logFile: null,
    processKeyword: null,
    hostId: null
};

/* nohup 快捷操作：查看日志 */
async function nohupViewLog() {
    if (!currentNohupInfo.logFile || !currentNohupInfo.hostId) {
        showToast('没有可用的日志信息', 'warning');
        return;
    }
    await executeNohupHelperCommand(`cat "${currentNohupInfo.logFile}"`);
}

/* nohup 实时跟踪状态 */
let tailIntervalId = null;
let lastTailContent = '';

/* nohup 快捷操作：实时跟踪 */
async function nohupTailLog() {
    if (!currentNohupInfo.logFile || !currentNohupInfo.hostId) {
        showToast('没有可用的日志信息', 'warning');
        return;
    }
    
    // 如果已在跟踪，则停止
    if (tailIntervalId) {
        nohupStopTail();
        return;
    }
    
    const tailBtn = document.getElementById('nohup-tail-log');
    const stopBtn = document.getElementById('nohup-stop-tail');
    const resultPre = document.getElementById('exec-result');
    
    // 切换按钮状态
    tailBtn.style.display = 'none';
    stopBtn.style.display = 'inline-block';
    
    resultPre.textContent += `\n\n━━━━━━━━━━━━━━━━━━━━━━\n📡 开始实时跟踪: ${currentNohupInfo.logFile}\n（点击"停止跟踪"按钮退出）\n━━━━━━━━━━━━━━━━━━━━━━\n`;
    lastTailContent = '';
    
    // 定时获取日志
    const fetchLog = async () => {
        try {
            const host = window._cmdHostsList?.find(h => h.id === currentNohupInfo.hostId);
            if (!host) return;
            
            const result = await api.call('ssh.exec', {
                host: host.host,
                port: host.port,
                user: host.username,
                keyid: host.keyid,
                command: `tail -100 "${currentNohupInfo.logFile}" 2>/dev/null`,
                timeout_ms: 5000
            });
            
            const content = result.data?.stdout || '';
            // 只显示新增内容
            if (content && content !== lastTailContent) {
                if (lastTailContent === '') {
                    resultPre.textContent += content;
                } else if (content.length > lastTailContent.length && content.startsWith(lastTailContent)) {
                    resultPre.textContent += content.substring(lastTailContent.length);
                } else {
                    // 内容完全变化，显示全部
                    resultPre.textContent += '\n' + content;
                }
                lastTailContent = content;
                resultPre.scrollTop = resultPre.scrollHeight;
            }
        } catch (e) {
            console.error('Tail log error:', e);
        }
    };
    
    // 立即获取一次
    await fetchLog();
    // 每2秒获取一次
    tailIntervalId = setInterval(fetchLog, 2000);
}

/* nohup 快捷操作：停止实时跟踪 */
function nohupStopTail() {
    if (tailIntervalId) {
        clearInterval(tailIntervalId);
        tailIntervalId = null;
    }
    
    const tailBtn = document.getElementById('nohup-tail-log');
    const stopBtn = document.getElementById('nohup-stop-tail');
    const resultPre = document.getElementById('exec-result');
    
    if (tailBtn) tailBtn.style.display = 'inline-block';
    if (stopBtn) stopBtn.style.display = 'none';
    
    resultPre.textContent += `\n━━━━━━━━━━━━━━━━━━━━━━\n⏹️ 已停止实时跟踪\n`;
    resultPre.scrollTop = resultPre.scrollHeight;
}

/* nohup 快捷操作：检查进程（使用 PID 文件） */
async function nohupCheckProcess() {
    if (!currentNohupInfo.pidFile || !currentNohupInfo.hostId) {
        showToast('没有可用的进程信息', 'warning');
        return;
    }
    // 使用 PID 文件检查进程状态，并显示进程详情
    await executeNohupHelperCommand(`if [ -f ${currentNohupInfo.pidFile} ]; then PID=$(cat ${currentNohupInfo.pidFile}); if kill -0 $PID 2>/dev/null; then echo "✅ 进程运行中 (PID: $PID)"; ps -p $PID -o pid,user,%cpu,%mem,etime,args --no-headers 2>/dev/null || ps -p $PID 2>/dev/null; else echo "⚠️ 进程已退出 (PID: $PID)"; fi; else echo "❌ PID 文件不存在"; fi`);
}

/* nohup 快捷操作：停止进程（使用 PID 文件） */
async function nohupStopProcess() {
    if (!currentNohupInfo.pidFile || !currentNohupInfo.hostId) {
        showToast('没有可用的进程信息', 'warning');
        return;
    }
    
    // 确认对话框
    if (!confirm(`确定要停止此后台进程吗？`)) {
        return;
    }
    
    // 停止实时跟踪（如果正在进行）
    nohupStopTail();
    
    // 使用 PID 文件精确停止
    await executeNohupHelperCommand(`if [ -f ${currentNohupInfo.pidFile} ]; then kill $(cat ${currentNohupInfo.pidFile}) 2>/dev/null && rm -f ${currentNohupInfo.pidFile} && echo "✅ 进程已停止"; else echo "⚠️ PID 文件不存在"; fi`);
    
    // 再次检查进程状态
    await executeNohupHelperCommand(`[ -f ${currentNohupInfo.pidFile} ] && kill -0 $(cat ${currentNohupInfo.pidFile}) 2>/dev/null && echo "⚠️ 进程仍在运行" || echo "✅ 确认：进程已停止"`);
}

/* 执行 nohup 辅助命令 */
async function executeNohupHelperCommand(command) {
    const host = window._cmdHostsList?.find(h => h.id === currentNohupInfo.hostId);
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    const resultPre = document.getElementById('exec-result');
    resultPre.textContent += `\n\n━━━━━━━━━━━━━━━━━━━━━━\n$ ${command}\n`;
    
    try {
        const result = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            user: host.username,
            keyid: host.keyid,
            command: command,
            timeout_ms: 10000
        });
        
        // ssh.exec 返回 stdout 和 stderr，不是 output
        const stdout = result.data?.stdout || '';
        const stderr = result.data?.stderr || '';
        if (stdout || stderr) {
            if (stdout) resultPre.textContent += stdout;
            if (stderr) resultPre.textContent += `[stderr] ${stderr}`;
        } else {
            resultPre.textContent += '（无输出）\n';
        }
    } catch (e) {
        resultPre.textContent += `执行失败: ${e.message}\n`;
    }
    
    // 滚动到底部
    resultPre.scrollTop = resultPre.scrollHeight;
}

/**
 * 服务模式：查看日志（从命令列表卡片调用）
 * @param {number} idx - 命令索引
 * @param {string} safeName - 安全名称（用于日志文件）
 */
async function viewServiceLog(idx, safeName) {
    const cmd = sshCommands[selectedHostId]?.[idx];
    if (!cmd) {
        showToast('命令不存在', 'error');
        return;
    }
    
    const host = window._cmdHostsList?.find(h => h.id === selectedHostId);
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    const logFile = `/tmp/ts_nohup_${safeName}.log`;
    
    // 显示结果区域
    const resultSection = document.getElementById('exec-result-section');
    const resultPre = document.getElementById('exec-result');
    resultSection.style.display = 'block';
    document.getElementById('cancel-exec-btn').style.display = 'none';
    document.getElementById('nohup-actions').style.display = 'none';
    
    resultPre.textContent = `📄 查看服务日志: ${cmd.name}\n文件: ${logFile}\n\n`;
    resultSection.scrollIntoView({ behavior: 'smooth' });
    
    try {
        const result = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            user: host.username,
            keyid: host.keyid,
            command: `tail -200 "${logFile}" 2>/dev/null || echo "日志文件不存在或为空"`,
            timeout_ms: 10000
        });
        
        const stdout = result.data?.stdout || '';
        const stderr = result.data?.stderr || '';
        
        if (stdout) {
            resultPre.textContent += stdout;
        } else if (stderr) {
            resultPre.textContent += `[错误] ${stderr}`;
        } else {
            resultPre.textContent += '（日志为空）';
        }
    } catch (e) {
        resultPre.textContent += `获取日志失败: ${e.message}`;
    }
    
    resultPre.scrollTop = resultPre.scrollHeight;
}

/**
 * 服务模式：停止进程（从命令列表卡片调用）
 * @param {number} idx - 命令索引
 * @param {string} safeName - 安全名称（用于 PID 文件）
 */
async function stopServiceProcess(idx, safeName) {
    const cmd = sshCommands[selectedHostId]?.[idx];
    if (!cmd) {
        showToast('命令不存在', 'error');
        return;
    }
    
    const host = window._cmdHostsList?.find(h => h.id === selectedHostId);
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    // 确认对话框
    if (!confirm(`确定要停止服务 "${cmd.name}" 吗？`)) {
        return;
    }
    
    const pidFile = `/tmp/ts_nohup_${safeName}.pid`;
    
    // 显示结果区域
    const resultSection = document.getElementById('exec-result-section');
    const resultPre = document.getElementById('exec-result');
    resultSection.style.display = 'block';
    document.getElementById('cancel-exec-btn').style.display = 'none';
    document.getElementById('nohup-actions').style.display = 'none';
    
    resultPre.textContent = `🛑 停止服务: ${cmd.name}\n\n`;
    resultSection.scrollIntoView({ behavior: 'smooth' });
    
    try {
        // 先检查进程状态
        const checkResult = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            user: host.username,
            keyid: host.keyid,
            command: `if [ -f ${pidFile} ]; then PID=$(cat ${pidFile}); if kill -0 $PID 2>/dev/null; then echo "RUNNING:$PID"; else echo "STOPPED"; fi; else echo "NO_PID"; fi`,
            timeout_ms: 5000
        });
        
        const status = (checkResult.data?.stdout || '').trim();
        
        if (status.startsWith('RUNNING:')) {
            const pid = status.split(':')[1];
            resultPre.textContent += `进程运行中 (PID: ${pid})，正在停止...\n`;
            
            // 发送 SIGTERM
            const killResult = await api.call('ssh.exec', {
                host: host.host,
                port: host.port,
                user: host.username,
                keyid: host.keyid,
                command: `kill ${pid} 2>/dev/null && sleep 0.5 && (kill -0 ${pid} 2>/dev/null && echo "STILL_RUNNING" || echo "STOPPED")`,
                timeout_ms: 10000
            });
            
            const killStatus = (killResult.data?.stdout || '').trim();
            if (killStatus === 'STOPPED') {
                resultPre.textContent += `✅ 服务已停止\n`;
                showToast('服务已停止', 'success');
                
                // 更新状态变量
                if (cmd.varName) {
                    try {
                        await api.call('automation.variables.set', { name: `${cmd.varName}.status`, value: 'stopped' });
                    } catch (e) {}
                }
                
                // 刷新命令列表状态
                updateServiceStatusInList();
            } else {
                resultPre.textContent += `⚠️ 进程可能仍在运行，尝试强制终止...\n`;
                // 发送 SIGKILL
                await api.call('ssh.exec', {
                    host: host.host,
                    port: host.port,
                    user: host.username,
                    keyid: host.keyid,
                    command: `kill -9 ${pid} 2>/dev/null; rm -f ${pidFile}`,
                    timeout_ms: 5000
                });
                resultPre.textContent += `✅ 已强制终止\n`;
                showToast('服务已强制停止', 'warning');
                updateServiceStatusInList();
            }
        } else if (status === 'STOPPED') {
            resultPre.textContent += `⚠️ 进程已经停止\n`;
            showToast('进程已经停止', 'info');
        } else {
            resultPre.textContent += `⚠️ PID 文件不存在，服务可能未启动\n`;
            showToast('服务未运行', 'info');
        }
    } catch (e) {
        resultPre.textContent += `停止服务失败: ${e.message}`;
        showToast('停止服务失败: ' + e.message, 'error');
    }
    
    resultPre.scrollTop = resultPre.scrollHeight;
}

async function executeCommand(idx) {
    const cmd = sshCommands[selectedHostId]?.[idx];
    if (!cmd) return;
    
    const host = window._cmdHostsList?.find(h => h.id === selectedHostId);
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    // 检查是否有正在运行的命令（nohup 模式不需要检查）
    if (currentExecSessionId && !cmd.nohup) {
        showToast('有命令正在执行中，请先取消或等待完成', 'warning');
        return;
    }
    
    // 显示结果区域
    const resultSection = document.getElementById('exec-result-section');
    const resultPre = document.getElementById('exec-result');
    const cancelBtn = document.getElementById('cancel-exec-btn');
    const nohupActions = document.getElementById('nohup-actions');
    resultSection.style.display = 'block';
    
    // nohup 模式下隐藏取消按钮，显示快捷按钮
    if (cmd.nohup) {
        cancelBtn.style.display = 'none';
        nohupActions.style.display = 'flex';
    } else {
        cancelBtn.style.display = 'inline-block';
        cancelBtn.disabled = false;
        nohupActions.style.display = 'none';
    }
    
    // 对于 nohup 命令，包装命令以实现后台执行，并记录日志和 PID
    let actualCommand = cmd.command;
    let nohupLogFile = null;
    let nohupPidFile = null;
    if (cmd.nohup) {
        // 基于命令名生成固定文件名（每次执行会覆盖）
        const safeName = cmd.name.replace(/[^a-zA-Z0-9]/g, '').slice(0, 20) || 'cmd';
        nohupLogFile = `/tmp/ts_nohup_${safeName}.log`;
        nohupPidFile = `/tmp/ts_nohup_${safeName}.pid`;
        
        // 使用 PID 文件方式（最可靠，能区分任意数量的不同命令）
        // 启动命令：nohup cmd > log 2>&1 & echo $! > pidfile; 检测启动
        actualCommand = `nohup ${cmd.command} > ${nohupLogFile} 2>&1 & echo $! > ${nohupPidFile}; sleep 0.3; cat ${nohupPidFile}`;
        
        // 保存 nohup 信息供快捷按钮使用
        currentNohupInfo = {
            logFile: nohupLogFile,
            pidFile: nohupPidFile,
            processKeyword: safeName,
            hostId: selectedHostId
        };
    }
    
    // 构建状态信息
    let statusInfo = `⏳ 正在连接: ${cmd.name}\n主机: ${host.username}@${host.host}:${host.port}\n命令: ${actualCommand}\n`;
    if (cmd.nohup) {
        statusInfo += `\n🚀 后台执行模式: 命令将在服务器后台运行，断开后不受影响\n`;
        statusInfo += `📄 日志文件: ${nohupLogFile}\n`;
    } else if (cmd.expectPattern || cmd.failPattern || cmd.extractPattern) {
        statusInfo += `\n📋 模式匹配配置:\n`;
        if (cmd.expectPattern) statusInfo += `  ✅ 成功模式: ${cmd.expectPattern}\n`;
        if (cmd.failPattern) statusInfo += `  ❌ 失败模式: ${cmd.failPattern}\n`;
        if (cmd.extractPattern) statusInfo += `  📋 提取模式: ${cmd.extractPattern}\n`;
        if (cmd.stopOnMatch) statusInfo += `  ⏹️ 匹配后自动停止: 是\n`;
        if (cmd.varName) statusInfo += `  📝 存储变量: \${${cmd.varName}.*}\n`;
    }
    statusInfo += `\n`;
    resultPre.textContent = statusInfo;
    
    // 滚动到结果区域
    resultSection.scrollIntoView({ behavior: 'smooth' });
    
    try {
        // 构建 API 参数
        const params = {
            host: host.host,
            port: host.port,
            user: host.username,
            keyid: host.keyid,
            command: actualCommand
        };
        
        // 添加高级选项（仅非 nohup 模式）
        if (!cmd.nohup) {
            if (cmd.expectPattern) params.expect_pattern = cmd.expectPattern;
            if (cmd.failPattern) params.fail_pattern = cmd.failPattern;
            if (cmd.extractPattern) params.extract_pattern = cmd.extractPattern;
            if (cmd.varName) params.var_name = cmd.varName;
            if (cmd.timeout) params.timeout = cmd.timeout * 1000; // 转为毫秒
            if (cmd.stopOnMatch) params.stop_on_match = true;
        } else {
            // nohup 模式设置短超时，命令发送后立即返回
            params.timeout = 5000;
        }
        
        // 使用流式执行 API
        const result = await api.call('ssh.exec_stream', params);
        
        currentExecSessionId = result.data?.session_id;
        
        if (cmd.nohup) {
            resultPre.textContent += `✅ 命令已提交到服务器后台\n\n`;
            resultPre.textContent += `💡 使用上方按钮查看日志、跟踪输出或检查进程状态\n`;
            resultPre.textContent += `\n📄 日志文件: ${nohupLogFile}\n`;
            resultPre.textContent += `🔍 进程关键词: ${cmd.command.split(' ')[0]}\n`;
            // nohup 命令不跟踪会话
            currentExecSessionId = null;
        } else {
            resultPre.textContent += `会话 ID: ${currentExecSessionId}\n等待输出...\n\n`;
        }
        
        // 输出将通过 WebSocket 实时推送
        
    } catch (e) {
        // nohup 模式下超时是正常的（命令在后台运行）
        if (cmd.nohup && (e.message.includes('timeout') || e.message.includes('超时'))) {
            resultPre.textContent += `✅ 命令已提交到服务器后台\n\n`;
            resultPre.textContent += `💡 使用上方按钮查看日志、跟踪输出或检查进程状态\n`;
            resultPre.textContent += `\n📄 日志文件: ${nohupLogFile}\n`;
            resultPre.textContent += `🔍 进程关键词: ${cmd.command.split(' ')[0]}\n`;
            currentExecSessionId = null;
            return;
        }
        resultPre.textContent = `❌ 启动执行失败\n\n${e.message}`;
        showToast('启动执行失败: ' + e.message, 'error');
        cancelBtn.style.display = 'none';
        currentExecSessionId = null;
    }
}

async function cancelExecution() {
    if (!currentExecSessionId) {
        showToast('没有正在执行的命令', 'info');
        return;
    }
    
    const cancelBtn = document.getElementById('cancel-exec-btn');
    cancelBtn.disabled = true;
    cancelBtn.textContent = '取消中...';
    
    try {
        await api.call('ssh.cancel', { session_id: currentExecSessionId });
        showToast('取消请求已发送', 'info');
    } catch (e) {
        showToast('取消失败: ' + e.message, 'error');
        cancelBtn.disabled = false;
        cancelBtn.textContent = '⏹️ 取消 (Esc)';
    }
}

/* 处理 SSH Exec WebSocket 消息 */
function handleSshExecMessage(msg) {
    const resultPre = document.getElementById('exec-result');
    const cancelBtn = document.getElementById('cancel-exec-btn');
    const matchPanel = document.getElementById('match-result-panel');
    
    if (!resultPre) return;
    
    switch (msg.type) {
        case 'ssh_exec_start':
            // 从 WebSocket 消息中获取 session_id
            // 总是更新 session_id，因为这是新执行的开始
            if (msg.session_id) {
                currentExecSessionId = msg.session_id;
                console.log('[SSH] Session ID from ssh_exec_start:', currentExecSessionId);
            }
            resultPre.textContent += `--- 开始执行 ---\n`;
            // 隐藏匹配结果面板（新执行开始）
            if (matchPanel) matchPanel.style.display = 'none';
            break;
            
        case 'ssh_exec_output':
            // 接受消息如果：session_id 匹配，或者我们还没有 session_id（等待 API 返回）
            if (msg.session_id === currentExecSessionId || 
                (currentExecSessionId === null && msg.session_id)) {
                // 如果还没有 session_id，从消息中获取
                if (currentExecSessionId === null) {
                    currentExecSessionId = msg.session_id;
                    console.log('[SSH] Session ID from ssh_exec_output:', currentExecSessionId);
                }
                // 追加输出
                if (msg.is_stderr) {
                    resultPre.textContent += msg.data;
                } else {
                    resultPre.textContent += msg.data;
                }
                // 自动滚动到底部
                resultPre.scrollTop = resultPre.scrollHeight;
            }
            break;
            
        case 'ssh_exec_match':
            /* 实时匹配结果 */
            if (msg.session_id === currentExecSessionId) {
                const isFinal = msg.is_final === true;  /* 是否为终止匹配（expect/fail 匹配）*/
                const isExtractOnly = !msg.expect_matched && !msg.fail_matched && msg.extracted;
                
                if (isFinal) {
                    /* 终止匹配（expect/fail 模式匹配成功）*/
                    resultPre.textContent += `\n🎯 模式匹配成功!\n`;
                    if (msg.expect_matched) {
                        resultPre.textContent += `  ✅ 期望模式匹配: 是\n`;
                    }
                    if (msg.fail_matched) {
                        resultPre.textContent += `  ❌ 失败模式匹配: 是\n`;
                    }
                    if (msg.extracted) {
                        resultPre.textContent += `  📋 提取内容: ${msg.extracted}\n`;
                    }
                    showToast('模式匹配成功', msg.fail_matched ? 'error' : 'success');
                } else if (isExtractOnly) {
                    /* 仅提取更新（持续提取场景）*/
                    /* 不在输出区显示，只更新面板 */
                }
                
                // 更新匹配结果面板
                updateMatchResultPanel(msg, isExtractOnly);
            }
            break;
            
        case 'ssh_exec_done':
            if (msg.session_id === currentExecSessionId) {
                resultPre.textContent += `\n--- 执行完成 ---\n`;
                resultPre.textContent += `退出码: ${msg.exit_code}\n`;
                
                // 显示模式匹配结果
                if (msg.status) {
                    const statusMap = {
                        'running': '⏳ 运行中',
                        'success': '✅ 成功',
                        'failed': '❌ 失败',
                        'timeout': '⏱️ 超时',
                        'cancelled': '⏹️ 已取消',
                        'match_success': '✅ 模式匹配成功',
                        'match_failed': '❌ 模式匹配失败'
                    };
                    resultPre.textContent += `状态: ${statusMap[msg.status] || msg.status}\n`;
                }
                
                // 显示期望模式匹配结果
                if (msg.expect_matched !== undefined) {
                    resultPre.textContent += `期望模式匹配: ${msg.expect_matched ? '✅ 是' : '❌ 否'}\n`;
                }
                
                // 显示失败模式匹配结果
                if (msg.fail_matched !== undefined) {
                    resultPre.textContent += `失败模式匹配: ${msg.fail_matched ? '⚠️ 是' : '✅ 否'}\n`;
                }
                
                // 显示提取的内容
                if (msg.extracted) {
                    resultPre.textContent += `\n📋 提取内容:\n${msg.extracted}\n`;
                }
                
                // 更新匹配结果面板
                console.log('ssh_exec_done received:', JSON.stringify(msg, null, 2));
                updateMatchResultPanel(msg);
                
                if (cancelBtn) {
                    cancelBtn.style.display = 'none';
                }
                currentExecSessionId = null;
                
                // 根据状态显示 Toast
                if (msg.status === 'match_success' || (msg.exit_code === 0 && !msg.fail_matched)) {
                    showToast('命令执行成功', 'success');
                } else if (msg.status === 'match_failed' || msg.fail_matched) {
                    showToast('命令执行完成，模式匹配失败', 'warning');
                } else if (msg.status === 'timeout') {
                    showToast('命令执行超时', 'warning');
                } else if (msg.exit_code === 0) {
                    showToast('命令执行成功', 'success');
                } else {
                    showToast(`命令执行完成，退出码: ${msg.exit_code}`, 'warning');
                }
            }
            break;
            
        case 'ssh_exec_error':
            if (msg.session_id === currentExecSessionId) {
                resultPre.textContent += `\n❌ 错误: ${msg.error}\n`;
                if (cancelBtn) {
                    cancelBtn.style.display = 'none';
                }
                currentExecSessionId = null;
                showToast('执行出错: ' + msg.error, 'error');
            }
            break;
            
        case 'ssh_exec_cancelled':
            if (msg.session_id === currentExecSessionId) {
                resultPre.textContent += `\n⏹️ 已取消执行\n`;
                if (cancelBtn) {
                    cancelBtn.style.display = 'none';
                }
                currentExecSessionId = null;
                showToast('命令已取消', 'info');
            }
            break;
    }
}

/* 更新匹配结果面板 */
function updateMatchResultPanel(msg, isExtractOnly = false) {
    const panel = document.getElementById('match-result-panel');
    if (!panel) {
        console.warn('match-result-panel not found');
        return;
    }
    
    console.log('updateMatchResultPanel called with:', msg, 'isExtractOnly:', isExtractOnly);
    
    // 始终显示面板（只要有匹配就显示）
    panel.style.display = 'block';
    
    // 更新状态徽章
    const statusBadge = document.getElementById('match-status-badge');
    if (statusBadge) {
        if (isExtractOnly) {
            // 持续提取模式 - 显示"提取中"
            statusBadge.textContent = '提取中...';
            statusBadge.className = 'match-status extracting';
        } else {
            const statusConfig = {
                'success': { text: '成功', class: 'success' },
                'match_success': { text: '匹配成功', class: 'success' },
                'failed': { text: '失败', class: 'failed' },
                'match_failed': { text: '匹配失败', class: 'failed' },
                'timeout': { text: '超时', class: 'timeout' },
                'cancelled': { text: '已取消', class: 'failed' }
            };
            const config = statusConfig[msg.status] || { text: msg.status || '完成', class: 'success' };
            statusBadge.textContent = config.text;
            statusBadge.className = `match-status ${config.class}`;
        }
    }
    
    // 更新成功匹配结果
    const expectResult = document.getElementById('match-expect-result');
    if (expectResult) {
        if (msg.expect_matched !== undefined) {
            expectResult.textContent = msg.expect_matched ? '✅ true' : '❌ false';
            expectResult.className = `match-value ${msg.expect_matched ? 'true' : 'false'}`;
        } else {
            expectResult.textContent = '未配置';
            expectResult.className = 'match-value';
        }
    }
    
    // 更新失败匹配结果
    const failResult = document.getElementById('match-fail-result');
    if (failResult) {
        if (msg.fail_matched !== undefined) {
            failResult.textContent = msg.fail_matched ? '⚠️ true (检测到错误)' : '✅ false';
            failResult.className = `match-value ${msg.fail_matched ? 'false' : 'true'}`;
        } else {
            failResult.textContent = '未配置';
            failResult.className = 'match-value';
        }
    }
    
    // 更新提取内容
    const extractedResult = document.getElementById('match-extracted-result');
    if (extractedResult) {
        if (msg.extracted) {
            extractedResult.textContent = msg.extracted;
            extractedResult.title = msg.extracted;
        } else {
            extractedResult.textContent = '无';
        }
    }
    
    // 更新最终状态
    const finalStatus = document.getElementById('match-final-status');
    if (finalStatus) {
        const statusMap = {
            'running': '运行中',
            'success': '成功',
            'failed': '失败',
            'timeout': '超时',
            'cancelled': '已取消',
            'match_success': '匹配成功',
            'match_failed': '匹配失败'
        };
        finalStatus.textContent = `"${msg.status || 'success'}"`;
        finalStatus.title = statusMap[msg.status] || msg.status;
    }
}

function clearExecResult() {
    document.getElementById('exec-result-section').style.display = 'none';
    document.getElementById('exec-result').textContent = '';
    document.getElementById('cancel-exec-btn').style.display = 'none';
    // 隐藏 nohup 快捷按钮
    const nohupActions = document.getElementById('nohup-actions');
    if (nohupActions) nohupActions.style.display = 'none';
    // 隐藏匹配结果面板
    const matchPanel = document.getElementById('match-result-panel');
    if (matchPanel) matchPanel.style.display = 'none';
    currentExecSessionId = null;
    // 清除 nohup 信息
    currentNohupInfo = { logFile: null, processKeyword: null, hostId: null };
}

// =========================================================================
//                         安全页面
// =========================================================================

async function loadSecurityPage() {
    clearInterval(refreshInterval);
    
    // 取消系统页面的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-security">
            <div class="section">
                <h2>� 密钥管理</h2>
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
                <h2>🖥️ 已部署主机</h2>
                <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:15px">
                    <p style="color:#666;font-size:0.9em;margin:0">💡 通过上方密钥的「部署」按钮将公钥部署到远程服务器后，主机将自动出现在此列表</p>
                    <button class="btn btn-sm" onclick="showImportSshHostModal()" style="background:#17a2b8;color:white">📥 导入主机</button>
                </div>
                <table class="data-table">
                    <thead>
                        <tr><th>主机 ID</th><th>地址</th><th>端口</th><th>用户名</th><th>部署密钥</th><th>操作</th></tr>
                    </thead>
                    <tbody id="ssh-hosts-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>� 已知主机指纹</h2>
                <p style="color:#666;margin-bottom:15px;font-size:0.9em">💡 SSH 连接时自动记录的服务器指纹，用于防止中间人攻击。如果服务器重装需要更新指纹。</p>
                <table class="data-table">
                    <thead>
                        <tr><th>主机</th><th>端口</th><th>密钥类型</th><th>指纹 (SHA256)</th><th>添加时间</th><th>操作</th></tr>
                    </thead>
                    <tbody id="known-hosts-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>�🔒 HTTPS 证书</h2>
                <div id="cert-status-card" class="info-card" style="margin-bottom:15px">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
                        <span style="font-size:1.1em;font-weight:bold">
                            <span id="cert-status-icon">🔄</span>
                            <span id="cert-status-text">加载中...</span>
                        </span>
                        <span id="cert-expiry-badge" class="badge" style="display:none"></span>
                    </div>
                    <div id="cert-info-details" style="display:none">
                        <div style="display:grid;grid-template-columns:repeat(2, 1fr);gap:8px;font-size:0.9em">
                            <div><strong>主体 CN：</strong><span id="cert-subject-cn">-</span></div>
                            <div><strong>签发者：</strong><span id="cert-issuer-cn">-</span></div>
                            <div><strong>生效时间：</strong><span id="cert-not-before">-</span></div>
                            <div><strong>过期时间：</strong><span id="cert-not-after">-</span></div>
                            <div><strong>序列号：</strong><span id="cert-serial" style="font-family:monospace;font-size:0.85em">-</span></div>
                            <div><strong>有效状态：</strong><span id="cert-valid-status">-</span></div>
                        </div>
                    </div>
                    <div id="cert-no-key-hint" style="display:none;color:#666;font-style:italic">
                        尚未生成密钥对，请先点击下方按钮生成
                    </div>
                </div>
                <div class="button-group" style="display:flex;flex-wrap:wrap;gap:8px">
                    <button class="btn" id="btn-cert-gen-key" onclick="showCertGenKeyModal()">🔑 生成密钥对</button>
                    <button class="btn" id="btn-cert-gen-csr" onclick="showCertCSRModal()" disabled>📋 生成 CSR</button>
                    <button class="btn" id="btn-cert-install" onclick="showCertInstallModal()" disabled>📥 安装证书</button>
                    <button class="btn" id="btn-cert-install-ca" onclick="showCertInstallCAModal()" disabled>🏛️ 安装 CA</button>
                    <button class="btn" id="btn-cert-view" onclick="showCertViewModal()" disabled>👁️ 查看证书</button>
                    <button class="btn btn-danger" id="btn-cert-delete" onclick="deleteCertCredentials()" disabled>🗑️ 删除凭证</button>
                </div>
            </div>
            
            <div class="section">
                <h2>📦 配置包 (Config Pack)</h2>
                <div id="config-pack-status-card" class="info-card" style="margin-bottom:15px">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
                        <span style="font-size:1.1em;font-weight:bold">
                            <span id="pack-status-icon">🔄</span>
                            <span id="pack-status-text">加载中...</span>
                        </span>
                        <span id="pack-device-type-badge" class="badge" style="display:none"></span>
                    </div>
                    <div id="pack-info-details" style="display:none">
                        <div style="display:grid;grid-template-columns:repeat(2, 1fr);gap:8px;font-size:0.9em">
                            <div><strong>设备类型：</strong><span id="pack-device-type">-</span></div>
                            <div><strong>证书 CN：</strong><span id="pack-cert-cn">-</span></div>
                            <div><strong>证书指纹：</strong><span id="pack-cert-fp" style="font-family:monospace;font-size:0.8em">-</span></div>
                            <div><strong>格式版本：</strong><span id="pack-version">-</span></div>
                        </div>
                    </div>
                    <p style="color:#666;margin-top:10px;font-size:0.9em">
                        💡 配置包系统允许安全地加密和签名配置文件，用于设备间配置分发
                    </p>
                </div>
                <div class="button-group" style="display:flex;flex-wrap:wrap;gap:8px">
                    <button class="btn" onclick="showConfigPackExportCertModal()">📤 导出设备证书</button>
                    <button class="btn" onclick="showConfigPackImportModal()">📥 导入配置包</button>
                    <button class="btn" id="btn-pack-export" onclick="showConfigPackExportModal()" disabled>📦 导出配置包</button>
                    <button class="btn" onclick="showConfigPackListModal()">📋 查看配置包列表</button>
                </div>
            </div>
            
            <!-- 配置包：导出设备证书弹窗 -->
            <div class="modal hidden" id="pack-export-cert-modal">
                <div class="modal-content" style="max-width:600px">
                    <h2>📤 导出设备证书</h2>
                    <p style="color:#666;margin-bottom:15px">将此证书发送给需要向您发送加密配置的开发者</p>
                    <div id="pack-export-cert-loading" style="text-align:center;padding:20px">🔄 加载中...</div>
                    <div id="pack-export-cert-content" class="hidden">
                        <div class="form-group">
                            <label>证书指纹 (SHA256)</label>
                            <input type="text" id="pack-cert-fingerprint" readonly style="font-family:monospace;font-size:0.9em;background:#f5f5f5">
                        </div>
                        <div class="form-group">
                            <label>证书 CN</label>
                            <input type="text" id="pack-cert-cn-display" readonly style="background:#f5f5f5">
                        </div>
                        <div class="form-group">
                            <label>证书 PEM</label>
                            <textarea id="pack-cert-pem" readonly style="width:100%;height:200px;font-family:monospace;font-size:11px"></textarea>
                        </div>
                        <button class="btn btn-small" onclick="copyPackCertToClipboard()" style="margin-top:8px">📋 复制到剪贴板</button>
                    </div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideConfigPackExportCertModal()">关闭</button>
                    </div>
                </div>
            </div>
            
            <!-- 配置包：导入弹窗 -->
            <div class="modal hidden" id="pack-import-modal">
                <div class="modal-content" style="max-width:700px">
                    <h2>📥 导入配置包</h2>
                    <p style="color:#666;margin-bottom:15px">上传或粘贴 .tscfg 配置包，验证后保存到设备（加密存储）</p>
                    <div class="form-group">
                        <label>选择文件</label>
                        <input type="file" id="pack-import-file" accept=".tscfg,.json" onchange="handlePackFileSelect(event)">
                    </div>
                    <div class="form-group">
                        <label>或粘贴 JSON 内容</label>
                        <textarea id="pack-import-content" placeholder='{"tscfg_version":"1.0", ...}' style="width:100%;height:150px;font-family:monospace;font-size:11px"></textarea>
                    </div>
                    <div id="pack-import-result" class="result-box hidden" style="margin-top:10px"></div>
                    <div id="pack-import-preview" class="hidden" style="margin-top:15px;padding:10px;background:#f8f9fa;border-radius:4px">
                        <h4 style="margin:0 0 10px">📋 配置包信息</h4>
                        <div id="pack-preview-content"></div>
                    </div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideConfigPackImportModal()">取消</button>
                        <button class="btn" onclick="verifyConfigPack()">🔍 仅验证</button>
                        <button class="btn btn-primary" onclick="importConfigPack()">📥 导入</button>
                    </div>
                </div>
            </div>
            
            <!-- 配置包：导出弹窗（仅 Developer 可用） -->
            <div class="modal hidden" id="pack-export-modal">
                <div class="modal-content" style="width:800px;max-width:90vw;height:auto;min-height:600px;max-height:90vh;overflow-y:auto">
                    <h2>📦 导出加密配置包</h2>
                    <p style="color:#666;margin-bottom:15px">选择配置文件并加密发送给目标设备（支持多选）</p>
                    
                    <!-- 文件浏览器 -->
                    <div class="form-group">
                        <label>选择配置文件 <span style="color:#999;font-size:0.9em">(可多选)</span></label>
                        <div style="display:flex;gap:8px;margin-bottom:8px">
                            <input type="text" id="pack-export-browse-path" value="/sdcard/config" style="flex:1" readonly>
                            <button class="btn btn-small" onclick="packExportBrowseUp()">⬆️ 上级</button>
                            <button class="btn btn-small" onclick="packExportBrowseRefresh()">🔄 刷新</button>
                        </div>
                        <div style="display:flex;gap:8px;margin-bottom:8px">
                            <button class="btn btn-small" onclick="packExportSelectAll()">☑️ 全选</button>
                            <button class="btn btn-small" onclick="packExportDeselectAll()">☐ 取消全选</button>
                            <button class="btn btn-small" onclick="packExportSelectDir()">📁 选择整个目录</button>
                        </div>
                        <div id="pack-export-file-list" style="border:1px solid #ddd;border-radius:4px;height:180px;overflow-y:auto;background:#f9f9f9">
                            <div style="padding:20px;text-align:center;color:#666">🔄 加载中...</div>
                        </div>
                        <div id="pack-export-selected" style="margin-top:8px;padding:8px;background:#e8f5e9;border-radius:4px;min-height:36px;display:none">
                            <strong>已选择:</strong> <span id="pack-export-selected-file"></span>
                        </div>
                    </div>
                    
                    <div class="form-group">
                        <label>配置名称</label>
                        <input type="text" id="pack-export-name" placeholder="自动从文件名获取" required>
                    </div>
                    <div class="form-group">
                        <label>描述 (可选)</label>
                        <input type="text" id="pack-export-desc" placeholder="LED 特效配置">
                    </div>
                    <div class="form-group">
                        <label>目标设备证书 (PEM)</label>
                        <textarea id="pack-export-recipient-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:100px;font-family:monospace;font-size:11px" required></textarea>
                        <div style="font-size:0.85em;color:#666;margin-top:4px">💡 粘贴目标设备导出的证书</div>
                    </div>
                    <div id="pack-export-result" class="result-box" style="margin-top:10px;min-height:24px;visibility:hidden"></div>
                    <div id="pack-export-output" style="margin-top:15px">
                        <label>生成的配置包 (.tscfg)</label>
                        <textarea id="pack-export-tscfg" readonly style="width:100%;height:100px;font-family:monospace;font-size:10px" placeholder="配置包将在此显示..."></textarea>
                        <div style="margin-top:10px;display:flex;gap:8px;flex-wrap:wrap;align-items:center">
                            <button class="btn btn-small" onclick="copyPackTscfgToClipboard()" id="btn-pack-copy" style="display:none">📋 复制到剪贴板</button>
                            <button class="btn btn-small btn-primary" onclick="downloadPackTscfg()" id="btn-pack-download" style="display:none">💾 下载到本地</button>
                            <span id="pack-export-saved-path" style="color:#4caf50;font-size:0.9em;display:none"></span>
                        </div>
                    </div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideConfigPackExportModal()">取消</button>
                        <button class="btn btn-primary" id="btn-pack-export-generate" onclick="exportConfigPack()" disabled>📦 生成配置包</button>
                    </div>
                </div>
            </div>
            
            <!-- 配置包：列表弹窗 -->
            <div class="modal hidden" id="pack-list-modal">
                <div class="modal-content" style="max-width:800px">
                    <h2>📋 配置包列表</h2>
                    <div class="form-group">
                        <label>目录路径</label>
                        <div style="display:flex;gap:8px">
                            <input type="text" id="pack-list-path" value="/sdcard/config" style="flex:1">
                            <button class="btn" onclick="refreshConfigPackList()">🔄 刷新</button>
                        </div>
                    </div>
                    <div id="pack-list-loading" style="text-align:center;padding:20px">🔄 加载中...</div>
                    <table class="data-table hidden" id="pack-list-table">
                        <thead>
                            <tr><th>文件名</th><th>大小</th><th>签名者</th><th>官方</th><th>状态</th><th>操作</th></tr>
                        </thead>
                        <tbody id="pack-list-tbody"></tbody>
                    </table>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideConfigPackListModal()">关闭</button>
                    </div>
                </div>
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
                            <option value="rsa2048" selected>RSA 2048-bit (推荐)</option>
                            <option value="rsa4096">RSA 4096-bit</option>
                            <option value="ec256">ECDSA P-256 ⚠️</option>
                            <option value="ec384">ECDSA P-384 ⚠️</option>
                        </select>
                        <div style="font-size:0.85em;color:#e67e22;margin-top:4px">⚠️ ECDSA 密钥暂不支持 SSH 公钥认证，请使用 RSA</div>
                    </div>
                    <div class="form-group">
                        <label>备注 (可选)</label>
                        <input type="text" id="keygen-comment" placeholder="如: TianshanOS@device">
                    </div>
                    <div class="form-group">
                        <label>别名 (可选)</label>
                        <input type="text" id="keygen-alias" placeholder="用于替代密钥 ID 显示">
                        <div style="font-size:0.85em;color:#666;margin-top:4px">💡 启用「隐藏密钥」时建议填写，用于显示</div>
                    </div>
                    <div class="form-group">
                        <label><input type="checkbox" id="keygen-exportable"> 允许导出私钥</label>
                    </div>
                    <div class="form-group">
                        <label><input type="checkbox" id="keygen-hidden"> 隐藏密钥 ID</label>
                        <div style="font-size:0.85em;color:#666;margin-top:4px">🔒 启用后，低权限用户无法看到真实的密钥 ID</div>
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
                        💡 部署成功后，该主机将自动添加到「已部署主机」列表，之后可使用此密钥免密登录
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
                        <strong>建议</strong>：如果您确认服务器已重装或密钥已更新，可以点击"更新主机密钥"移除旧记录，然后重新连接以信任新密钥。
                    </p>
                    <div class="form-actions">
                        <button class="btn" onclick="hideHostMismatchModal()">取消</button>
                        <button class="btn btn-warning" onclick="removeAndRetry()">🔄 更新主机密钥</button>
                    </div>
                </div>
            </div>
            
            <!-- HTTPS 证书：生成密钥对弹窗 -->
            <div class="modal hidden" id="cert-genkey-modal">
                <div class="modal-content" style="max-width:450px">
                    <h2>🔑 生成 HTTPS 密钥对</h2>
                    <p style="color:#666;margin-bottom:15px">为设备生成 ECDSA P-256 密钥对，用于 mTLS 身份验证</p>
                    <div id="cert-genkey-existing-warning" class="hidden" style="background:#fff3cd;border:1px solid #ffc107;border-radius:4px;padding:10px;margin-bottom:15px">
                        ⚠️ 已存在密钥对，继续将覆盖现有密钥！
                    </div>
                    <div id="cert-genkey-result" class="result-box hidden" style="margin-bottom:15px"></div>
                    <div class="form-actions">
                        <button class="btn" onclick="hideCertGenKeyModal()">取消</button>
                        <button class="btn btn-primary" id="cert-genkey-btn" onclick="generateCertKeypair()">🔑 生成</button>
                    </div>
                </div>
            </div>
            
            <!-- HTTPS 证书：生成/查看 CSR 弹窗 -->
            <div class="modal hidden" id="cert-csr-modal">
                <div class="modal-content" style="max-width:600px">
                    <h2>📋 证书签名请求 (CSR)</h2>
                    <div class="form-group">
                        <label>设备 ID (CN)</label>
                        <input type="text" id="csr-device-id" placeholder="TIANSHAN-RM01-0001">
                        <div style="font-size:0.85em;color:#666;margin-top:4px">留空则使用默认配置</div>
                    </div>
                    <div class="form-group">
                        <label>组织 (O)</label>
                        <input type="text" id="csr-org" placeholder="HiddenPeak Labs">
                    </div>
                    <div class="form-group">
                        <label>部门 (OU)</label>
                        <input type="text" id="csr-ou" placeholder="Device">
                    </div>
                    <div id="csr-result-box" class="hidden" style="margin-top:15px">
                        <label>CSR 内容（复制到 CA 服务器签发）</label>
                        <textarea id="csr-pem-output" readonly style="width:100%;height:200px;font-family:monospace;font-size:11px"></textarea>
                        <button class="btn btn-small" onclick="copyCSRToClipboard()" style="margin-top:8px">📋 复制到剪贴板</button>
                    </div>
                    <div id="csr-gen-result" class="result-box hidden" style="margin-top:10px"></div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideCertCSRModal()">关闭</button>
                        <button class="btn btn-primary" id="csr-gen-btn" onclick="generateCSR()">📋 生成 CSR</button>
                    </div>
                </div>
            </div>
            
            <!-- HTTPS 证书：安装证书弹窗 -->
            <div class="modal hidden" id="cert-install-modal">
                <div class="modal-content" style="max-width:600px">
                    <h2>📥 安装设备证书</h2>
                    <p style="color:#666;margin-bottom:15px">粘贴 CA 签发的 PEM 格式证书</p>
                    <div class="form-group">
                        <label>证书 PEM</label>
                        <textarea id="cert-pem-input" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:200px;font-family:monospace;font-size:11px"></textarea>
                    </div>
                    <div id="cert-install-result" class="result-box hidden" style="margin-top:10px"></div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideCertInstallModal()">取消</button>
                        <button class="btn btn-primary" onclick="installCertificate()">📥 安装</button>
                    </div>
                </div>
            </div>
            
            <!-- HTTPS 证书：安装 CA 链弹窗 -->
            <div class="modal hidden" id="cert-ca-modal">
                <div class="modal-content" style="max-width:600px">
                    <h2>🏛️ 安装 CA 证书链</h2>
                    <p style="color:#666;margin-bottom:15px">粘贴根证书和中间证书（PEM 格式，可拼接多个）</p>
                    <div class="form-group">
                        <label>CA 证书链 PEM</label>
                        <textarea id="ca-pem-input" placeholder="-----BEGIN CERTIFICATE-----&#10;(Root CA)&#10;-----END CERTIFICATE-----&#10;-----BEGIN CERTIFICATE-----&#10;(Intermediate CA)&#10;-----END CERTIFICATE-----" style="width:100%;height:200px;font-family:monospace;font-size:11px"></textarea>
                    </div>
                    <div id="ca-install-result" class="result-box hidden" style="margin-top:10px"></div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideCertInstallCAModal()">取消</button>
                        <button class="btn btn-primary" onclick="installCAChain()">🏛️ 安装</button>
                    </div>
                </div>
            </div>
            
            <!-- HTTPS 证书：查看证书弹窗 -->
            <div class="modal hidden" id="cert-view-modal">
                <div class="modal-content" style="max-width:600px">
                    <h2>👁️ 查看设备证书</h2>
                    <div id="cert-view-loading" style="text-align:center;padding:20px">🔄 加载中...</div>
                    <div id="cert-view-content" class="hidden">
                        <textarea id="cert-view-pem" readonly style="width:100%;height:250px;font-family:monospace;font-size:11px"></textarea>
                        <button class="btn btn-small" onclick="copyCertToClipboard()" style="margin-top:8px">📋 复制到剪贴板</button>
                    </div>
                    <div class="form-actions" style="margin-top:15px">
                        <button class="btn" onclick="hideCertViewModal()">关闭</button>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    await refreshSecurityPage();
}

async function refreshSecurityPage() {
    // 密钥列表
    const tbody = document.getElementById('keys-table-body');
    let allKeysHtml = '';
    let sshKeys = [];
    
    // 1. 加载 SSH 密钥
    try {
        const keys = await api.keyList();
        const sshKeySelect = document.getElementById('ssh-keyid');
        
        // 更新 SSH 测试的密钥下拉列表
        if (sshKeySelect) {
            sshKeySelect.innerHTML = '<option value="">-- 选择密钥 --</option>';
            if (keys.data?.keys && keys.data.keys.length > 0) {
                keys.data.keys.forEach(key => {
                    const option = document.createElement('option');
                    option.value = key.id;
                    // 隐藏密钥显示别名或掩码 ID，否则显示真实 ID
                    const displayName = (key.hidden && key.alias) ? key.alias : key.id;
                    option.textContent = `${key.hidden ? '🔒 ' : ''}${displayName} (${key.type_desc || key.type})`;
                    sshKeySelect.appendChild(option);
                });
            }
        }
        
        if (keys.data?.keys && keys.data.keys.length > 0) {
            sshKeys = keys.data.keys;
            allKeysHtml += keys.data.keys.map(key => {
                // 隐藏密钥显示别名，否则显示真实 ID
                const displayId = (key.hidden && key.alias) ? key.alias : key.id;
                const hiddenIcon = key.hidden ? '🔒 ' : '';
                
                return `
                <tr>
                    <td>
                        <code>${hiddenIcon}${escapeHtml(displayId)}</code>
                        ${key.alias && !key.hidden ? `<div style="font-size:0.85em;color:#666;margin-top:2px">${escapeHtml(key.alias)}</div>` : ''}
                    </td>
                    <td>${escapeHtml(key.type_desc || key.type)}</td>
                    <td><span class="badge badge-info">SSH</span> ${escapeHtml(key.comment) || '-'}</td>
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
                `;
            }).join('');
        }
    } catch (e) {
        console.error('加载 SSH 密钥失败:', e);
    }
    
    // 2. 加载 HTTPS 密钥（来自 ts_cert）
    try {
        const certStatus = await api.certStatus();
        console.log('HTTPS cert status:', certStatus);
        
        if (certStatus.code === 0) {
            // 字段名是 has_private_key，不是 has_keypair
            const hasKeypair = certStatus.data?.has_private_key;
            const hasCert = certStatus.data?.has_certificate;
            const certInfo = certStatus.data?.cert_info || {};
            
            if (hasKeypair) {
                // 已有密钥对
                const comment = hasCert ? `CN=${certInfo.subject_cn || 'unknown'}` : '(未安装证书)';
                
                allKeysHtml += `
                <tr style="background:#f0f7ff">
                    <td>
                        <code>🔐 https</code>
                        <div style="font-size:0.85em;color:#666;margin-top:2px">HTTPS 服务器密钥</div>
                    </td>
                    <td>ECDSA P-256</td>
                    <td><span class="badge" style="background:#2196f3;color:white">HTTPS</span> ${escapeHtml(comment)}</td>
                    <td>-</td>
                    <td>❌ 否</td>
                    <td>
                        <button class="btn btn-small" onclick="showCertCSRModal()" title="生成证书签名请求">📋 CSR</button>
                        <button class="btn btn-small" onclick="showCertViewModal()" ${hasCert ? '' : 'disabled'} title="查看证书">👁️ 证书</button>
                        <button class="btn btn-small btn-danger" onclick="deleteCertCredentials()" title="删除 HTTPS 密钥和证书">🗑️ 删除</button>
                    </td>
                </tr>
                `;
            } else {
                // 未生成密钥对，显示提示行
                allKeysHtml += `
                <tr style="background:#fff8e1">
                    <td>
                        <code style="color:#888">🔒 https</code>
                        <div style="font-size:0.85em;color:#999;margin-top:2px">HTTPS 服务器密钥</div>
                    </td>
                    <td style="color:#888">-</td>
                    <td><span class="badge" style="background:#ff9800;color:white">HTTPS</span> <em style="color:#888">未生成密钥</em></td>
                    <td>-</td>
                    <td>-</td>
                    <td>
                        <button class="btn btn-small btn-primary" onclick="showCertGenKeyModal()" title="生成 HTTPS 密钥对">🔑 生成密钥</button>
                    </td>
                </tr>
                `;
            }
        }
    } catch (e) {
        console.error('加载 HTTPS 密钥状态失败:', e);
    }
    
    // 3. 更新表格
    if (allKeysHtml) {
        tbody.innerHTML = allKeysHtml;
    } else {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:#888">暂无密钥，点击上方按钮生成新密钥</td></tr>';
    }
    
    // SSH 已部署主机列表（加载数据并渲染到 DOM）
    await refreshSshHostsList();
    
    // 已知主机指纹列表
    await refreshKnownHostsList();
    
    // HTTPS 证书状态
    await refreshCertStatus();
    
    // Config Pack 状态
    await refreshConfigPackStatus();
}

/**
 * 刷新安全页面的已部署主机列表
 */
/**
 * 仅加载 SSH hosts 数据到 window._sshHostsData（不渲染 DOM）
 */
async function loadSshHostsData() {
    try {
        const result = await api.call('ssh.hosts.list', {});
        const hosts = result.data?.hosts || [];
        window._sshHostsData = {};
        hosts.forEach(h => { window._sshHostsData[h.id] = h; });
        console.log('loadSshHostsData: loaded', Object.keys(window._sshHostsData).length, 'hosts');
    } catch (e) {
        console.error('loadSshHostsData error:', e);
        window._sshHostsData = {};
    }
}

async function refreshSshHostsList() {
    // 首先加载 SSH hosts 数据（无需 DOM）
    await loadSshHostsData();
    
    const tbody = document.getElementById('ssh-hosts-table-body');
    if (!tbody) return;  // DOM 渲染部分可选
    
    try {
        const hosts = Object.values(window._sshHostsData || {});
        
        if (hosts.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" class="empty-state">暂无已部署主机，请先在上方密钥管理中点击「部署」</td></tr>';
            return;
        }
        
        tbody.innerHTML = hosts.map((h, idx) => `
            <tr>
                <td><code>${escapeHtml(h.id)}</code></td>
                <td>${escapeHtml(h.host)}</td>
                <td>${h.port}</td>
                <td>${escapeHtml(h.username)}</td>
                <td><span class="badge badge-info">🔑 ${escapeHtml(h.keyid || 'default')}</span></td>
                <td>
                    <button class="btn btn-sm" onclick="testSshHostByIndex(${idx})" title="测试连接">🔍 测试</button>
                    <button class="btn btn-sm" onclick="exportSshHost('${escapeHtml(h.id)}')" title="导出配置为 .tscfg" style="background:#17a2b8;color:white">📤 导出</button>
                    <button class="btn btn-sm btn-danger" onclick="revokeKeyFromHost(${idx})" title="撤销公钥">🔓 撤销</button>
                    <button class="btn btn-sm" onclick="removeHostByIndex(${idx})" title="仅移除本地记录" style="background:#6c757d;color:white">🗑️ 移除</button>
                </td>
            </tr>
        `).join('');
        
        // 存储主机列表供索引访问
        window._sshHostsList = hosts;
    } catch (e) {
        console.error('Refresh SSH hosts error:', e);
        tbody.innerHTML = '<tr><td colspan="6" class="error">加载失败</td></tr>';
    }
}

/**
 * 刷新已知主机指纹列表
 */
async function refreshKnownHostsList() {
    const tbody = document.getElementById('known-hosts-table-body');
    if (!tbody) return;
    
    try {
        const result = await api.call('hosts.list', {});
        const hosts = result.data?.hosts || [];
        
        if (hosts.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" class="empty-state">暂无已知主机指纹</td></tr>';
            return;
        }
        
        // 存储已知主机列表
        window._knownHostsList = hosts;
        
        tbody.innerHTML = hosts.map((h, idx) => `
            <tr>
                <td><code>${escapeHtml(h.host)}</code></td>
                <td>${h.port}</td>
                <td><span class="badge">${escapeHtml(h.type)}</span></td>
                <td><code style="font-size:0.8em;word-break:break-all">${escapeHtml(h.fingerprint.substring(0, 32))}...</code></td>
                <td>${formatTimestamp(h.added)}</td>
                <td>
                    <button class="btn btn-sm" onclick="showFullFingerprint(${idx})" title="查看完整指纹">👁️ 查看</button>
                    <button class="btn btn-sm" onclick="removeKnownHost(${idx})" title="删除指纹记录" style="background:#dc3545;color:white">🗑️ 删除</button>
                </td>
            </tr>
        `).join('');
    } catch (e) {
        console.error('Refresh known hosts error:', e);
        tbody.innerHTML = '<tr><td colspan="6" class="error">加载失败</td></tr>';
    }
}

/**
 * 显示完整指纹
 */
function showFullFingerprint(index) {
    const host = window._knownHostsList?.[index];
    if (!host) return;
    
    alert(`主机: ${host.host}:${host.port}\n类型: ${host.type}\n指纹 (SHA256):\n${host.fingerprint}`);
}

/**
 * 删除已知主机指纹
 */
async function removeKnownHost(index) {
    const host = window._knownHostsList?.[index];
    if (!host) return;
    
    if (!confirm(`确定要删除主机 ${host.host}:${host.port} 的指纹记录吗？\n\n删除后下次连接将重新验证服务器指纹。`)) return;
    
    try {
        const result = await api.call('hosts.remove', { host: host.host, port: host.port });
        if (result.code === 0) {
            showToast('已删除主机指纹', 'success');
            await refreshKnownHostsList();
        } else {
            showToast('删除失败: ' + (result.message || '未知错误'), 'error');
        }
    } catch (e) {
        showToast('删除失败: ' + e.message, 'error');
    }
}

/**
 * 测试 SSH 连接
 */
async function testSshConnection(hostId) {
    showToast(`正在测试连接 ${hostId}...`, 'info');
    
    try {
        // 获取主机信息
        const hostResult = await api.call('ssh.hosts.get', { id: hostId });
        console.log('ssh.hosts.get result:', hostResult);
        
        if (hostResult.code !== 0) {
            showToast(`无法获取主机信息: ${hostResult.message || '未知错误'}`, 'error');
            return;
        }
        
        if (!hostResult.data) {
            showToast('主机信息为空', 'error');
            return;
        }
        
        const host = hostResult.data;
        
        // 执行 ssh.exec 测试连接（执行简单命令）
        const execResult = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            username: host.username,
            keyid: host.keyid || 'default',
            command: 'echo "TianshanOS SSH Test OK"'
        });
        
        if (execResult.code === 0) {
            showToast(`✅ 连接 ${hostId} 成功！`, 'success');
        } else {
            showToast(`❌ 连接失败: ${execResult.message || '未知错误'}`, 'error');
        }
    } catch (e) {
        console.error('Test SSH connection error:', e);
        showToast(`❌ 测试失败: ${e.message}`, 'error');
    }
}

/**
 * 通过索引测试 SSH 连接（避免 ID 中的特殊字符问题）
 */
async function testSshHostByIndex(index) {
    const host = window._sshHostsList?.[index];
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    showToast(`正在测试连接 ${host.id}...`, 'info');
    
    try {
        const execResult = await api.call('ssh.exec', {
            host: host.host,
            port: host.port,
            user: host.username,  // API 需要 'user' 而不是 'username'
            keyid: host.keyid || 'default',
            command: 'echo "TianshanOS SSH Test OK"',
            trust_new: true
        });
        
        if (execResult.code === 0) {
            showToast(`✅ 连接 ${host.id} 成功！`, 'success');
        } else {
            showToast(`❌ 连接失败: ${execResult.message || '未知错误'}`, 'error');
        }
    } catch (e) {
        console.error('Test SSH connection error:', e);
        showToast(`❌ 测试失败: ${e.message}`, 'error');
    }
}

/**
 * 导出 SSH 主机配置为 .tscfg 文件
 * 开发机：显示模态框输入目标证书
 * 非开发机：使用设备证书自加密
 */
async function exportSshHost(hostId) {
    // 确保已加载设备类型信息
    if (!window._configPackStatus) {
        try {
            const result = await api.configPackInfo();
            window._configPackStatus = result.data;
        } catch (e) {
            console.warn('无法获取设备类型信息，使用默认导出', e);
        }
    }
    
    // 检查设备类型
    const canExport = window._configPackStatus?.can_export;
    
    if (canExport) {
        // 开发机：显示模态框让用户输入目标证书
        showExportSshHostModal(hostId);
    } else {
        // 非开发机：直接使用设备证书加密
        await doExportSshHost(hostId, null);
    }
}

/**
 * 显示导出 SSH 主机模态框（开发机专用）
 */
function showExportSshHostModal(hostId) {
    let modal = document.getElementById('export-ssh-host-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'export-ssh-host-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📤 导出 SSH 主机配置</h2>
            <p style="color:#666;font-size:0.9rem">导出主机 <strong>${escapeHtml(hostId)}</strong> 的配置为加密配置包</p>
            
            <div class="form-group" style="margin-top:15px">
                <label>目标设备证书 (PEM)</label>
                <textarea id="export-ssh-host-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:120px;font-family:monospace;font-size:11px"></textarea>
                <div style="font-size:0.85em;color:#666;margin-top:4px">💡 粘贴目标设备的证书。留空则使用本机证书（自加密）</div>
            </div>
            
            <div id="export-ssh-host-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideExportSshHostModal()">取消</button>
                <button class="btn btn-primary" id="export-ssh-host-btn" onclick="doExportSshHostFromModal('${escapeHtml(hostId)}')">📤 导出</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function hideExportSshHostModal() {
    const modal = document.getElementById('export-ssh-host-modal');
    if (modal) modal.classList.add('hidden');
}

async function doExportSshHostFromModal(hostId) {
    const certText = document.getElementById('export-ssh-host-cert').value.trim();
    const resultBox = document.getElementById('export-ssh-host-result');
    const exportBtn = document.getElementById('export-ssh-host-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成配置包...';
    exportBtn.disabled = true;
    
    try {
        await doExportSshHost(hostId, certText || null);
        resultBox.className = 'result-box success';
        resultBox.textContent = '✅ 导出成功！';
        setTimeout(() => hideExportSshHostModal(), 1000);
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    } finally {
        exportBtn.disabled = false;
    }
}

/**
 * 执行导出 SSH 主机
 * @param {string} hostId - 主机 ID
 * @param {string|null} recipientCert - 目标证书（null 使用设备证书）
 */
async function doExportSshHost(hostId, recipientCert) {
    const params = { id: hostId };
    if (recipientCert) {
        params.recipient_cert = recipientCert;
    }
    
    const result = await api.call('ssh.hosts.export', params);
    
    if (result.code !== 0) {
        throw new Error(result.message || '导出失败');
    }
    
    const data = result.data;
    if (!data?.tscfg) {
        throw new Error('无效的响应数据');
    }
    
    // 下载文件
    const blob = new Blob([data.tscfg], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = data.filename || `${hostId}.tscfg`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    
    showToast(`已导出主机配置: ${data.filename}`, 'success');
}

/**
 * 显示导入 SSH 主机配置弹窗
 */
function showImportSshHostModal() {
    let modal = document.getElementById('import-ssh-host-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'import-ssh-host-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:550px">
            <h2>📥 导入 SSH 主机配置</h2>
            <p style="color:#666;font-size:0.9rem">选择 .tscfg 配置包文件以导入 SSH 主机配置</p>
            
            <!-- 步骤 1: 选择文件 -->
            <div id="import-ssh-host-step1">
                <div class="form-group" style="margin-top:15px">
                    <label>选择文件</label>
                    <input type="file" id="import-ssh-host-file" class="form-control" accept=".tscfg" onchange="previewSshHostImport()">
                </div>
            </div>
            
            <!-- 步骤 2: 预览 (默认隐藏) -->
            <div id="import-ssh-host-step2" style="display:none">
                <div class="info-card" style="background:#f8f9fa;padding:15px;border-radius:8px;margin-top:15px">
                    <h4 style="margin:0 0 10px 0">📋 配置包内容</h4>
                    <div id="import-ssh-host-preview"></div>
                </div>
                <div class="form-group" style="margin-top:15px">
                    <label>
                        <input type="checkbox" id="import-ssh-host-overwrite"> 覆盖已存在的配置
                    </label>
                </div>
            </div>
            
            <div id="import-ssh-host-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideImportSshHostModal()">取消</button>
                <button class="btn btn-primary" id="import-ssh-host-btn" onclick="confirmSshHostImport()" disabled>📥 确认导入</button>
            </div>
        </div>
    `;
    
    // 存储 tscfg 内容
    window._importSshHostTscfg = null;
    
    modal.classList.remove('hidden');
}

function hideImportSshHostModal() {
    const modal = document.getElementById('import-ssh-host-modal');
    if (modal) modal.classList.add('hidden');
    window._importSshHostTscfg = null;
}

/**
 * 预览 SSH 主机导入内容
 */
async function previewSshHostImport() {
    const fileInput = document.getElementById('import-ssh-host-file');
    const resultBox = document.getElementById('import-ssh-host-result');
    const step2 = document.getElementById('import-ssh-host-step2');
    const previewDiv = document.getElementById('import-ssh-host-preview');
    const importBtn = document.getElementById('import-ssh-host-btn');
    
    if (!fileInput.files || !fileInput.files[0]) return;
    
    const file = fileInput.files[0];
    
    resultBox.classList.remove('hidden', 'success', 'error', 'warning');
    resultBox.textContent = '🔄 正在验证配置包...';
    importBtn.disabled = true;
    step2.style.display = 'none';
    
    try {
        const content = await file.text();
        window._importSshHostTscfg = content;
        window._importSshHostFilename = file.name;  // 保存文件名
        
        // 预览模式调用（轻量级验证，不解密）
        const result = await api.call('ssh.hosts.import', { 
            tscfg: content,
            filename: file.name,
            preview: true
        });
        
        if (result.code === 0 && result.data?.valid) {
            const data = result.data;
            
            // 构建预览 HTML（轻量级验证只返回基本信息）
            let html = `
                <table style="width:100%;font-size:0.9em">
                    <tr><td style="width:80px;color:#666">配置 ID:</td><td><code>${escapeHtml(data.id)}</code></td></tr>
                    <tr><td style="color:#666">签名者:</td><td>${escapeHtml(data.signer)} ${data.official ? '✅ 官方' : ''}</td></tr>
                    <tr><td style="color:#666">备注:</td><td style="color:#888;font-size:0.85em">${escapeHtml(data.note || '重启后自动加载')}</td></tr>
                </table>
            `;
            
            if (data.exists) {
                html += `<div style="margin-top:10px;padding:8px;background:#fff3cd;border-radius:4px;color:#856404">⚠️ 该配置已存在，导入将覆盖现有文件</div>`;
            }
            
            previewDiv.innerHTML = html;
            step2.style.display = 'block';
            resultBox.className = 'result-box success';
            resultBox.textContent = '✅ 签名验证通过';
            importBtn.disabled = false;
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '无法验证配置包');
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    }
}

/**
 * 确认导入 SSH 主机
 */
async function confirmSshHostImport() {
    const overwrite = document.getElementById('import-ssh-host-overwrite').checked;
    const resultBox = document.getElementById('import-ssh-host-result');
    const importBtn = document.getElementById('import-ssh-host-btn');
    
    if (!window._importSshHostTscfg) {
        showToast('请先选择文件', 'error');
        return;
    }
    
    resultBox.classList.remove('hidden', 'success', 'error', 'warning');
    resultBox.textContent = '🔄 正在保存配置...';
    importBtn.disabled = true;
    
    try {
        const result = await api.call('ssh.hosts.import', { 
            tscfg: window._importSshHostTscfg,
            filename: window._importSshHostFilename,
            overwrite: overwrite
        });
        
        if (result.code === 0) {
            const data = result.data;
            if (data?.exists && !data?.imported) {
                resultBox.className = 'result-box warning';
                resultBox.textContent = `⚠️ 配置 ${data.id} 已存在，请勾选「覆盖」选项`;
                importBtn.disabled = false;
            } else {
                resultBox.className = 'result-box success';
                resultBox.innerHTML = `✅ 已保存配置: <code>${escapeHtml(data?.id)}</code><br><small style="color:#666">重启系统后生效</small>`;
                showToast(`已导入配置，重启后生效`, 'success');
                // 不刷新列表，因为还没加载
                setTimeout(() => hideImportSshHostModal(), 2000);
            }
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '导入失败');
            importBtn.disabled = false;
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
        importBtn.disabled = false;
    }
}

/**
 * 通过索引移除主机记录
 */
async function removeHostByIndex(index) {
    const host = window._sshHostsList?.[index];
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    if (!confirm(`确定要从列表中移除主机 "${host.id}" 吗？\n\n注意：这只会移除本地记录，不会删除已部署到服务器上的公钥。如需撤销公钥，请点击「撤销」按钮。`)) return;
    
    try {
        const result = await api.call('ssh.hosts.remove', { id: host.id });
        if (result.code === 0) {
            showToast(`SSH 主机 ${host.id} 已从列表移除`, 'success');
            await loadSshHostsData();
        } else {
            showToast('移除失败: ' + (result.message || '未知错误'), 'error');
        }
    } catch (e) {
        showToast('移除失败: ' + e.message, 'error');
    }
}

/**
 * 从已部署主机撤销公钥（弹出密码输入框）
 */
function revokeKeyFromHost(index) {
    const host = window._sshHostsList?.[index];
    if (!host) {
        showToast('主机信息不存在', 'error');
        return;
    }
    
    // 创建撤销确认弹窗
    let modal = document.getElementById('revoke-host-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'revoke-host-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:500px">
            <h2>🔓 撤销并移除主机</h2>
            <p>将从服务器 <strong>${escapeHtml(host.username)}@${escapeHtml(host.host)}:${host.port}</strong> 撤销密钥 <code>${escapeHtml(host.keyid || 'default')}</code></p>
            <p style="color:#666;font-size:0.9rem;margin-top:10px">撤销成功后将自动从列表中移除该主机</p>
            <div class="form-group" style="margin-top:15px">
                <label>服务器密码</label>
                <input type="password" id="revoke-host-password" class="form-control" placeholder="输入 SSH 密码">
            </div>
            <div id="revoke-host-result" class="result-box hidden" style="margin-top:10px"></div>
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideRevokeHostModal()">取消</button>
                <button class="btn btn-danger" id="revoke-host-btn" onclick="doRevokeFromHost(${index})">🔓 撤销并移除</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
    document.getElementById('revoke-host-password').focus();
}

function hideRevokeHostModal() {
    const modal = document.getElementById('revoke-host-modal');
    if (modal) modal.classList.add('hidden');
}

async function doRevokeFromHost(index) {
    const host = window._sshHostsList?.[index];
    if (!host) return;
    
    const password = document.getElementById('revoke-host-password').value;
    if (!password) {
        showToast('请输入密码', 'error');
        return;
    }
    
    const resultBox = document.getElementById('revoke-host-result');
    const revokeBtn = document.getElementById('revoke-host-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在撤销公钥...';
    revokeBtn.disabled = true;
    
    try {
        const result = await api.sshRevoke(host.host, host.username, password, host.keyid || 'default', host.port);
        
        if (result.data?.revoked) {
            resultBox.textContent = `✅ 撤销成功！已从服务器移除 ${result.data.removed_count || 1} 个匹配的公钥`;
            resultBox.classList.add('success');
            
            // 自动移除本地记录
            await api.call('ssh.hosts.remove', { id: host.id });
            showToast('已撤销公钥并移除主机记录', 'success');
            
            setTimeout(() => {
                hideRevokeHostModal();
                refreshSshHostsList();
            }, 1000);
        } else if (result.data?.found === false) {
            resultBox.textContent = '⚠️ 未在服务器上找到匹配的公钥（可能已被移除）\n是否仍要移除本地记录？';
            resultBox.classList.add('error');
            
            // 提供移除本地记录的选项
            revokeBtn.textContent = '🗑️ 仅移除本地记录';
            revokeBtn.onclick = async () => {
                await api.call('ssh.hosts.remove', { id: host.id });
                showToast('已移除本地主机记录', 'success');
                hideRevokeHostModal();
                refreshSshHostsList();
            };
            revokeBtn.disabled = false;
            return;  // 不进入 finally
        } else {
            throw new Error(result.message || '撤销失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 撤销失败: ' + e.message;
        resultBox.classList.add('error');
    } finally {
        revokeBtn.disabled = false;
    }
}

/**
 * 从安全页面删除 SSH 主机（保留兼容性）
 */
async function deleteSshHostFromSecurity(id) {
    if (!confirm(`确定要从列表中移除主机 "${id}" 吗？\n\n注意：这只会移除本地记录，不会删除已部署到服务器上的公钥。如需撤销公钥，请使用密钥管理中的「撤销」功能。`)) return;
    
    try {
        const result = await api.call('ssh.hosts.remove', { id });
        if (result.code === 0) {
            showToast(`SSH 主机 ${id} 已从列表移除`, 'success');
            await loadSshHostsData();
        } else {
            showToast('移除失败: ' + (result.message || '未知错误'), 'error');
        }
    } catch (e) {
        showToast('移除失败: ' + e.message, 'error');
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
            // 刷新已部署主机列表（后端 ssh.copyid 会自动注册主机）
            await loadSshHostsData();
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
        // 使用新的 hosts.update API 更新主机密钥
        await api.hostsUpdate(currentMismatchInfo.host, currentMismatchInfo.port || 22);
        showToast('旧主机密钥已移除，请重新连接以信任新密钥', 'success');
        hideHostMismatchModal();
        await refreshSecurityPage();
    } catch (e) {
        showToast('更新失败: ' + e.message, 'error');
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

// =========================================================================
//                  Config Pack Management
// =========================================================================

/**
 * 刷新配置包状态卡片
 */
async function refreshConfigPackStatus() {
    const statusIcon = document.getElementById('pack-status-icon');
    const statusText = document.getElementById('pack-status-text');
    const deviceTypeBadge = document.getElementById('pack-device-type-badge');
    const infoDetails = document.getElementById('pack-info-details');
    const btnExport = document.getElementById('btn-pack-export');
    
    if (!statusIcon) return; // 不在安全页面
    
    try {
        const result = await api.configPackInfo();
        const data = result.data;
        
        if (!data) throw new Error('无响应数据');
        
        // 存储状态供弹窗使用
        window._configPackStatus = data;
        
        // 更新状态
        const canExport = data.can_export;
        const deviceType = data.device_type;
        
        statusIcon.textContent = canExport ? '🛠️' : '📱';
        statusText.textContent = canExport ? 'Developer 设备' : 'Device 设备';
        
        // 设备类型徽章
        deviceTypeBadge.style.display = 'inline-block';
        deviceTypeBadge.textContent = deviceType;
        deviceTypeBadge.className = 'badge ' + (canExport ? 'badge-success' : 'badge-info');
        
        // 显示详细信息
        infoDetails.style.display = 'block';
        document.getElementById('pack-device-type').textContent = deviceType;
        document.getElementById('pack-cert-cn').textContent = data.cert_cn || '-';
        document.getElementById('pack-cert-fp').textContent = data.cert_fingerprint 
            ? data.cert_fingerprint.substring(0, 32) + '...' 
            : '-';
        document.getElementById('pack-version').textContent = data.pack_version || '-';
        
        // 导出按钮只对 Developer 设备启用
        if (btnExport) {
            btnExport.disabled = !canExport;
            btnExport.title = canExport ? '' : '仅 Developer 设备可导出配置包';
        }
        
    } catch (e) {
        console.error('Refresh config pack status error:', e);
        statusIcon.textContent = '❌';
        statusText.textContent = '加载失败';
        if (deviceTypeBadge) deviceTypeBadge.style.display = 'none';
        if (infoDetails) infoDetails.style.display = 'none';
    }
}

// 配置包：导出设备证书弹窗
function showConfigPackExportCertModal() {
    document.getElementById('pack-export-cert-modal').classList.remove('hidden');
    loadConfigPackCert();
}

function hideConfigPackExportCertModal() {
    document.getElementById('pack-export-cert-modal').classList.add('hidden');
}

async function loadConfigPackCert() {
    const loading = document.getElementById('pack-export-cert-loading');
    const content = document.getElementById('pack-export-cert-content');
    
    loading.style.display = 'block';
    content.classList.add('hidden');
    
    try {
        const result = await api.configPackExportCert();
        if (result.code !== 0) throw new Error(result.message || result.error);
        
        const data = result.data;
        document.getElementById('pack-cert-fingerprint').value = data.fingerprint || '';
        document.getElementById('pack-cert-cn-display').value = data.cn || '';
        document.getElementById('pack-cert-pem').value = data.certificate || '';
        
        loading.style.display = 'none';
        content.classList.remove('hidden');
    } catch (e) {
        loading.textContent = '❌ 加载失败: ' + e.message;
    }
}

function copyPackCertToClipboard() {
    const pem = document.getElementById('pack-cert-pem').value;
    navigator.clipboard.writeText(pem).then(() => {
        showToast('证书已复制到剪贴板', 'success');
    }).catch(e => {
        showToast('复制失败: ' + e.message, 'error');
    });
}

// 配置包：导入弹窗
function showConfigPackImportModal() {
    document.getElementById('pack-import-modal').classList.remove('hidden');
    document.getElementById('pack-import-file').value = '';
    document.getElementById('pack-import-content').value = '';
    document.getElementById('pack-import-result').classList.add('hidden');
    document.getElementById('pack-import-preview').classList.add('hidden');
}

function hideConfigPackImportModal() {
    document.getElementById('pack-import-modal').classList.add('hidden');
}

function handlePackFileSelect(event) {
    const file = event.target.files[0];
    if (!file) return;
    
    const reader = new FileReader();
    reader.onload = (e) => {
        document.getElementById('pack-import-content').value = e.target.result;
    };
    reader.readAsText(file);
}

async function verifyConfigPack() {
    const content = document.getElementById('pack-import-content').value.trim();
    const resultBox = document.getElementById('pack-import-result');
    const preview = document.getElementById('pack-import-preview');
    
    if (!content) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '请上传文件或粘贴配置包内容';
        resultBox.classList.remove('hidden');
        return;
    }
    
    resultBox.className = 'result-box';
    resultBox.textContent = '🔄 验证中...';
    resultBox.classList.remove('hidden');
    preview.classList.add('hidden');
    
    try {
        const result = await api.configPackVerify(content);
        if (result.code !== 0) throw new Error(result.message || result.error);
        
        const data = result.data;
        if (data.valid) {
            resultBox.className = 'result-box success';
            resultBox.innerHTML = '✅ 签名验证通过';
            
            // 显示签名信息
            if (data.signature) {
                const sig = data.signature;
                document.getElementById('pack-preview-content').innerHTML = `
                    <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:0.9em">
                        <div><strong>签名者：</strong>${sig.signer_cn || '-'}</div>
                        <div><strong>组织：</strong>${sig.signer_ou || '-'}</div>
                        <div><strong>官方签名：</strong>${sig.is_official ? '✅ 是' : '❌ 否'}</div>
                        <div><strong>签名时间：</strong>${sig.signed_at ? formatTimestamp(sig.signed_at) : '-'}</div>
                    </div>
                `;
                preview.classList.remove('hidden');
            }
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ 验证失败: ' + (data.result_message || '签名无效');
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ 验证失败: ' + e.message;
    }
}

async function importConfigPack() {
    const content = document.getElementById('pack-import-content').value.trim();
    const resultBox = document.getElementById('pack-import-result');
    const preview = document.getElementById('pack-import-preview');
    
    if (!content) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '请上传文件或粘贴配置包内容';
        resultBox.classList.remove('hidden');
        return;
    }
    
    resultBox.className = 'result-box';
    resultBox.textContent = '🔄 导入中...';
    resultBox.classList.remove('hidden');
    
    try {
        const result = await api.configPackImport(content, null, false);
        if (result.code !== 0) throw new Error(result.message || result.error);
        
        const data = result.data;
        resultBox.className = 'result-box success';
        resultBox.innerHTML = `✅ 配置包已导入<br><small>保存至: ${data.saved_path || '未知'}</small>`;
        
        // 显示详细信息（无解密内容）
        const sig = data.signature || {};
        document.getElementById('pack-preview-content').innerHTML = `
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:0.9em">
                <div><strong>配置名称：</strong>${data.name || '-'}</div>
                <div><strong>描述：</strong>${data.description || '-'}</div>
                <div><strong>目标设备：</strong>${data.target_device || '-'}</div>
                <div><strong>创建时间：</strong>${data.created_at ? formatTimestamp(data.created_at) : '-'}</div>
                <div><strong>签名者：</strong>${sig.signer_cn || '-'} (${sig.signer_ou || '-'})</div>
                <div><strong>签名时间：</strong>${sig.signed_at ? formatTimestamp(sig.signed_at) : '-'}</div>
                <div><strong>官方签名：</strong>${sig.is_official ? '✅ 是' : '❌ 否'}</div>
                <div><strong>保存路径：</strong>${data.saved_path || '-'}</div>
            </div>
            <div style="margin-top:10px;padding:8px;background:#e8f5e9;border-radius:4px;font-size:12px">
                📦 配置包已加密保存，使用 <code>config.pack.content</code> API 可按需解密
            </div>
        `;
        preview.classList.remove('hidden');
        
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ 导入失败: ' + e.message;
    }
}

/**
 * 显示配置包应用确认对话框
 * 当通过文件管理上传 .tscfg 文件并验证成功后调用
 */
function showConfigPackApplyConfirm(path, packInfo) {
    const sig = packInfo.signature || {};
    const signerInfo = sig.signer_cn ? `${sig.signer_cn}${sig.is_official ? ' (官方)' : ''}` : '未知';
    
    // 创建确认对话框
    const dialog = document.createElement('div');
    dialog.className = 'modal';
    dialog.id = 'config-pack-apply-confirm';
    dialog.innerHTML = `
        <div class="modal-content" style="max-width:450px">
            <div class="modal-header">
                <span class="modal-title">📦 配置包已上传</span>
            </div>
            <div class="modal-body">
                <div style="background:#e8f5e9;padding:12px;border-radius:6px;margin-bottom:15px">
                    <div style="color:#2e7d32;font-weight:bold;margin-bottom:8px">✅ 验证成功</div>
                    <div style="font-size:0.9em;color:#333">
                        <div>📄 文件: <code>${path.split('/').pop()}</code></div>
                        <div>🔐 签名者: ${signerInfo}</div>
                        ${sig.is_official ? '<div style="color:#1976d2">✓ 官方签名</div>' : ''}
                    </div>
                </div>
                <p style="margin:0;color:#666;font-size:0.9em">
                    配置包已保存到设备。是否立即应用此配置？
                </p>
            </div>
            <div class="modal-footer">
                <button class="btn" onclick="closeConfigPackApplyConfirm()">稍后应用</button>
                <button class="btn btn-primary" onclick="applyConfigPackFromPath('${path}')">🚀 立即应用</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(dialog);
}

function closeConfigPackApplyConfirm() {
    const dialog = document.getElementById('config-pack-apply-confirm');
    if (dialog) {
        dialog.remove();
    }
}

/**
 * 应用指定路径的配置包
 */
async function applyConfigPackFromPath(path) {
    closeConfigPackApplyConfirm();
    showToast('🔄 正在应用配置...', 'info');
    
    try {
        const result = await api.call('config.pack.apply', { path }, 'POST');
        if (result.code !== 0) {
            throw new Error(result.message || result.data?.result_message || '应用失败');
        }
        
        const data = result.data;
        if (data.success) {
            const modules = data.applied_modules || [];
            const moduleList = modules.length > 0 ? modules.join(', ') : '无';
            showToast(`✅ 配置已应用\n模块: ${moduleList}`, 'success', 5000);
        } else {
            showToast(`❌ 应用失败: ${data.result_message}`, 'error');
        }
    } catch (e) {
        console.error('Apply config pack error:', e);
        showToast('❌ 应用失败: ' + e.message, 'error');
    }
}

// 配置包导出：文件浏览器状态
let packExportCurrentPath = '/sdcard/config';
let packExportSelectedFile = null;
let packExportFileContent = null;
let packExportSelectedFiles = new Map();  // Map<fullPath, {name, content, status}>
let packExportCurrentEntries = [];  // 当前目录的条目缓存

// 配置包：导出弹窗（仅 Developer）
function showConfigPackExportModal() {
    if (!window._configPackStatus?.can_export) {
        showToast('仅 Developer 设备可导出配置包', 'error');
        return;
    }
    document.getElementById('pack-export-modal').classList.remove('hidden');
    document.getElementById('pack-export-name').value = '';
    document.getElementById('pack-export-desc').value = '';
    document.getElementById('pack-export-recipient-cert').value = '';
    document.getElementById('pack-export-result').classList.add('hidden');
    document.getElementById('pack-export-output').classList.add('hidden');
    document.getElementById('pack-export-selected').style.display = 'none';
    document.getElementById('btn-pack-export-generate').disabled = true;
    
    // 重置文件选择状态
    packExportSelectedFile = null;
    packExportFileContent = null;
    packExportSelectedFiles.clear();
    packExportCurrentEntries = [];
    packExportCurrentPath = '/sdcard/config';
    document.getElementById('pack-export-browse-path').value = packExportCurrentPath;
    
    // 加载文件列表
    packExportBrowseRefresh();
}

function hideConfigPackExportModal() {
    document.getElementById('pack-export-modal').classList.add('hidden');
    // 重置按钮状态
    document.getElementById('btn-pack-copy').style.display = 'none';
    document.getElementById('btn-pack-download').style.display = 'none';
    document.getElementById('pack-export-saved-path').style.display = 'none';
}

// 文件浏览器：刷新当前目录
async function packExportBrowseRefresh() {
    const fileList = document.getElementById('pack-export-file-list');
    fileList.innerHTML = '<div style="padding:20px;text-align:center;color:#666">🔄 加载中...</div>';
    
    try {
        const result = await api.storageList(packExportCurrentPath);
        if (result.code !== 0) throw new Error(result.message);
        
        const entries = result.data?.entries || [];
        
        // 排序：目录在前，然后按名称排序
        entries.sort((a, b) => {
            if (a.type === 'dir' && b.type !== 'dir') return -1;
            if (a.type !== 'dir' && b.type === 'dir') return 1;
            return a.name.localeCompare(b.name);
        });
        
        // 只显示 .json 文件和目录
        const filteredEntries = entries.filter(e => 
            e.type === 'dir' || e.name.endsWith('.json')
        );
        
        // 缓存当前目录条目
        packExportCurrentEntries = filteredEntries;
        
        if (filteredEntries.length === 0) {
            fileList.innerHTML = '<div style="padding:20px;text-align:center;color:#999">📁 没有配置文件 (.json)</div>';
            return;
        }
        
        let html = '<div style="padding:4px">';
        for (const entry of filteredEntries) {
            const icon = entry.type === 'dir' ? '📁' : '📄';
            const size = entry.type === 'file' ? ` <span style="color:#999;font-size:0.9em">(${formatFileSize(entry.size)})</span>` : '';
            const fullPath = packExportCurrentPath + '/' + entry.name;
            const isSelected = packExportSelectedFiles.has(fullPath);
            const bgColor = isSelected ? '#e3f2fd' : '';
            // 转义文件名中的特殊字符
            const safeName = entry.name.replace(/'/g, "\\'").replace(/"/g, '&quot;');
            
            if (entry.type === 'dir') {
                // 目录：点击进入，无复选框
                html += `<div onclick="packExportBrowseInto('${safeName}')" 
                    style="padding:8px 12px;cursor:pointer;border-bottom:1px solid #eee;display:flex;align-items:center" 
                    onmouseover="this.style.background='#f0f0f0'" onmouseout="this.style.background=''">
                    <span style="margin-right:8px">${icon}</span>
                    <span style="flex:1">${entry.name}</span>
                </div>`;
            } else {
                // 文件：带复选框
                const checkboxId = 'pack-export-cb-' + entry.name.replace(/[^a-zA-Z0-9]/g, '_');
                html += `<div style="padding:8px 12px;border-bottom:1px solid #eee;background:${bgColor};display:flex;align-items:center">
                    <input type="checkbox" id="${checkboxId}" ${isSelected ? 'checked' : ''} 
                        onclick="packExportToggleFile('${safeName}', this.checked)" style="margin-right:8px">
                    <label for="${checkboxId}" style="flex:1;cursor:pointer;margin:0;display:flex;align-items:center">
                        <span style="margin-right:4px">${icon}</span>
                        <span>${entry.name}</span>${size}
                    </label>
                </div>`;
            }
        }
        html += '</div>';
        fileList.innerHTML = html;
        
        // 更新选择状态显示
        packExportUpdateSelectedDisplay();
        
    } catch (e) {
        fileList.innerHTML = `<div style="padding:20px;text-align:center;color:#e74c3c">❌ 加载失败: ${e.message}</div>`;
    }
}

// 文件浏览器：进入子目录
function packExportBrowseInto(dirName) {
    packExportCurrentPath = packExportCurrentPath + '/' + dirName;
    document.getElementById('pack-export-browse-path').value = packExportCurrentPath;
    packExportBrowseRefresh();
}

// 文件浏览器：返回上级目录
function packExportBrowseUp() {
    const parts = packExportCurrentPath.split('/').filter(p => p);
    if (parts.length <= 1) {
        // 不能再往上了
        return;
    }
    parts.pop();
    packExportCurrentPath = '/' + parts.join('/');
    document.getElementById('pack-export-browse-path').value = packExportCurrentPath;
    packExportBrowseRefresh();
}

// 文件浏览器：切换文件选中状态
async function packExportToggleFile(fileName, checked) {
    const fullPath = packExportCurrentPath + '/' + fileName;
    
    if (!checked) {
        // 取消选择
        packExportSelectedFiles.delete(fullPath);
        packExportUpdateSelectedDisplay();
        packExportBrowseRefresh();
        return;
    }
    
    // 选中文件，读取内容
    packExportSelectedFiles.set(fullPath, { name: fileName, content: null, status: 'loading' });
    packExportUpdateSelectedDisplay();
    
    try {
        const result = await api.storageRead(fullPath);
        if (result.code !== 0) throw new Error(result.message);
        
        const rawContent = result.data?.content;
        if (rawContent === undefined || rawContent === null) throw new Error('文件内容为空');
        
        // 后端 storage.read 会自动解析 JSON
        let contentStr;
        if (typeof rawContent === 'object') {
            contentStr = JSON.stringify(rawContent, null, 2);
        } else {
            contentStr = rawContent;
            JSON.parse(contentStr);  // 验证
        }
        
        packExportSelectedFiles.set(fullPath, { name: fileName, content: contentStr, status: 'ok' });
        
    } catch (e) {
        packExportSelectedFiles.set(fullPath, { name: fileName, content: null, status: 'error', error: e.message });
    }
    
    packExportUpdateSelectedDisplay();
    packExportBrowseRefresh();
}

// 更新选择状态显示
function packExportUpdateSelectedDisplay() {
    const selectedDiv = document.getElementById('pack-export-selected');
    const selectedSpan = document.getElementById('pack-export-selected-file');
    const generateBtn = document.getElementById('btn-pack-export-generate');
    
    const files = Array.from(packExportSelectedFiles.entries());
    const okFiles = files.filter(([_, v]) => v.status === 'ok');
    const loadingFiles = files.filter(([_, v]) => v.status === 'loading');
    const errorFiles = files.filter(([_, v]) => v.status === 'error');
    
    if (files.length === 0) {
        selectedDiv.style.display = 'none';
        generateBtn.disabled = true;
        return;
    }
    
    selectedDiv.style.display = 'block';
    
    let text = `已选择 ${files.length} 个文件`;
    if (loadingFiles.length > 0) {
        text += ` (${loadingFiles.length} 个加载中...)`;
        selectedDiv.style.background = '#fff3e0';
        generateBtn.disabled = true;
    } else if (errorFiles.length > 0) {
        text += ` (${errorFiles.length} 个错误: ${errorFiles[0][1].error})`;
        selectedDiv.style.background = '#ffebee';
        generateBtn.disabled = errorFiles.length === files.length;  // 全部错误则禁用
    } else {
        selectedDiv.style.background = '#e8f5e9';
        generateBtn.disabled = false;
    }
    
    selectedSpan.textContent = text;
    
    // 自动填充配置名称
    const nameInput = document.getElementById('pack-export-name');
    if (!nameInput.value && okFiles.length > 0) {
        if (okFiles.length === 1) {
            nameInput.value = okFiles[0][1].name.replace(/\.json$/i, '');
        } else {
            nameInput.value = 'batch_config_' + okFiles.length;
        }
    }
}

// 全选当前目录的文件
async function packExportSelectAll() {
    const files = packExportCurrentEntries.filter(e => e.type === 'file');
    if (files.length === 0) return;
    
    for (const file of files) {
        const fullPath = packExportCurrentPath + '/' + file.name;
        if (!packExportSelectedFiles.has(fullPath)) {
            await packExportToggleFile(file.name, true);
        }
    }
}

// 取消全选
function packExportDeselectAll() {
    packExportSelectedFiles.clear();
    packExportUpdateSelectedDisplay();
    packExportBrowseRefresh();
}

// 选择整个目录（当前目录下的所有 JSON 文件）
async function packExportSelectDir() {
    // 与全选功能相同，但可以在 UI 上有区分
    await packExportSelectAll();
    showToast(`已选择当前目录下的所有 JSON 文件`, 'success');
}

// 文件大小格式化
function formatFileSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

async function exportConfigPack() {
    const name = document.getElementById('pack-export-name').value.trim();
    const desc = document.getElementById('pack-export-desc').value.trim();
    const recipientCert = document.getElementById('pack-export-recipient-cert').value.trim();
    const resultBox = document.getElementById('pack-export-result');
    const outputBox = document.getElementById('pack-export-output');
    const copyBtn = document.getElementById('btn-pack-copy');
    const downloadBtn = document.getElementById('btn-pack-download');
    
    // 重置按钮状态
    copyBtn.disabled = true;
    downloadBtn.disabled = true;
    
    // 验证输入
    if (!name) {
        resultBox.className = 'result-box error';
        resultBox.style.visibility = 'visible';
        resultBox.textContent = '请输入配置名称';
        return;
    }
    
    // 收集所有成功加载的文件
    const okFiles = Array.from(packExportSelectedFiles.entries()).filter(([_, v]) => v.status === 'ok');
    if (okFiles.length === 0) {
        resultBox.className = 'result-box error';
        resultBox.style.visibility = 'visible';
        resultBox.textContent = '请选择配置文件';
        return;
    }
    
    if (!recipientCert) {
        resultBox.className = 'result-box error';
        resultBox.style.visibility = 'visible';
        resultBox.textContent = '请粘贴目标设备证书';
        return;
    }
    
    // 合并多个配置文件为一个对象
    let content;
    try {
        if (okFiles.length === 1) {
            // 单文件：直接使用
            content = JSON.parse(okFiles[0][1].content);
        } else {
            // 多文件：合并到一个对象中，使用文件名作为 key
            content = { _batch: true, _files: {} };
            for (const [path, info] of okFiles) {
                const key = info.name.replace(/\.json$/i, '');
                content._files[key] = JSON.parse(info.content);
            }
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.style.visibility = 'visible';
        resultBox.textContent = '配置文件不是有效的 JSON: ' + e.message;
        return;
    }
    
    resultBox.className = 'result-box';
    resultBox.style.visibility = 'visible';
    resultBox.textContent = `🔄 生成配置包中 (${okFiles.length} 个文件)...`;
    document.getElementById('pack-export-tscfg').value = '';
    
    try {
        // 同时保存到 SD 卡
        const savePath = '/sdcard/output_config/' + name + '.tscfg';
        const result = await api.configPackExport(name, content, recipientCert, desc || null, savePath);
        console.log('[ConfigPack] Export result:', result);
        if (result.code !== 0) throw new Error(result.message || result.error);
        
        const data = result.data || {};
        const tscfgContent = data.tscfg || '';
        const fileSize = data.size || tscfgContent.length;
        const fileName = data.filename || (name + '.tscfg');
        const savedPath = data.saved_path || '';
        
        resultBox.className = 'result-box success';
        let resultHtml = `✅ 配置包已生成<br>📁 文件名: <b>${fileName}</b><br>📊 大小: ${fileSize} 字节 (${okFiles.length} 个配置文件)`;
        if (savedPath) {
            resultHtml += `<br>💾 已保存到: <code>${savedPath}</code>`;
        }
        resultBox.innerHTML = resultHtml;
        
        // 显示输出
        const tscfgTextarea = document.getElementById('pack-export-tscfg');
        tscfgTextarea.value = tscfgContent;
        window._packExportFilename = fileName;
        
        // 显示按钮
        if (tscfgContent) {
            copyBtn.style.display = 'inline-block';
            downloadBtn.style.display = 'inline-block';
        }
        
        // 显示保存路径
        const savedPathSpan = document.getElementById('pack-export-saved-path');
        if (savedPath && savedPathSpan) {
            savedPathSpan.textContent = `✅ 已保存到设备`;
            savedPathSpan.style.display = 'inline';
        }
        
        // 确保输出区域可见
        outputBox.style.display = 'block';
        
        if (!tscfgContent) {
            console.warn('[ConfigPack] tscfg content is empty!');
            resultBox.innerHTML += '<br>⚠️ 警告: 配置包内容为空';
        }
        
    } catch (e) {
        console.error('[ConfigPack] Export error:', e);
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ 生成失败: ' + e.message;
    }
}

function copyPackTscfgToClipboard() {
    const tscfg = document.getElementById('pack-export-tscfg').value;
    navigator.clipboard.writeText(tscfg).then(() => {
        showToast('配置包已复制到剪贴板', 'success');
    }).catch(e => {
        showToast('复制失败: ' + e.message, 'error');
    });
}

function downloadPackTscfg() {
    const tscfg = document.getElementById('pack-export-tscfg').value;
    const filename = window._packExportFilename || 'config.tscfg';
    
    const blob = new Blob([tscfg], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    
    showToast('配置包已下载: ' + filename, 'success');
}

// 配置包：列表弹窗
function showConfigPackListModal() {
    document.getElementById('pack-list-modal').classList.remove('hidden');
    refreshConfigPackList();
}

function hideConfigPackListModal() {
    document.getElementById('pack-list-modal').classList.add('hidden');
}

async function refreshConfigPackList() {
    const path = document.getElementById('pack-list-path').value.trim() || '/sdcard/config';
    const loading = document.getElementById('pack-list-loading');
    const table = document.getElementById('pack-list-table');
    const tbody = document.getElementById('pack-list-tbody');
    
    loading.style.display = 'block';
    table.classList.add('hidden');
    
    try {
        const result = await api.configPackList(path);
        if (result.code !== 0) throw new Error(result.message || result.error);
        
        const data = result.data;
        const files = data.files || [];
        
        tbody.innerHTML = '';
        
        if (files.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:#666">目录中没有 .tscfg 文件</td></tr>';
        } else {
            files.forEach(file => {
                const tr = document.createElement('tr');
                tr.innerHTML = `
                    <td>${escapeHtml(file.name)}</td>
                    <td>${formatBytes(file.size || 0)}</td>
                    <td>${escapeHtml(file.signer || '-')}</td>
                    <td>${file.is_official ? '✅' : '❌'}</td>
                    <td>${file.valid ? '<span style="color:green">✅ 有效</span>' : '<span style="color:red">❌ 无效</span>'}</td>
                    <td>
                        <button class="btn btn-small" onclick="importPackFromList('${escapeHtml(path)}/${escapeHtml(file.name)}')">📥 导入</button>
                    </td>
                `;
                tbody.appendChild(tr);
            });
        }
        
        loading.style.display = 'none';
        table.classList.remove('hidden');
        
    } catch (e) {
        loading.textContent = '❌ 加载失败: ' + e.message;
    }
}

async function importPackFromList(filePath) {
    if (!confirm(`确定要导入配置包: ${filePath} ?`)) return;
    
    try {
        const result = await api.configPackImport(null, filePath, false);
        if (result.code !== 0) throw new Error(result.message || result.error);
        
        showToast('配置包导入成功', 'success');
        hideConfigPackListModal();
    } catch (e) {
        showToast('导入失败: ' + e.message, 'error');
    }
}

// 辅助函数
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

// =========================================================================
//                  HTTPS Certificate (PKI) Management
// =========================================================================

/**
 * 刷新证书状态卡片
 */
async function refreshCertStatus() {
    const statusIcon = document.getElementById('cert-status-icon');
    const statusText = document.getElementById('cert-status-text');
    const expiryBadge = document.getElementById('cert-expiry-badge');
    const infoDetails = document.getElementById('cert-info-details');
    const noKeyHint = document.getElementById('cert-no-key-hint');
    
    // 按钮引用
    const btnGenKey = document.getElementById('btn-cert-gen-key');
    const btnGenCSR = document.getElementById('btn-cert-gen-csr');
    const btnInstall = document.getElementById('btn-cert-install');
    const btnInstallCA = document.getElementById('btn-cert-install-ca');
    const btnView = document.getElementById('btn-cert-view');
    const btnDelete = document.getElementById('btn-cert-delete');
    
    if (!statusIcon) return; // 不在安全页面
    
    try {
        const result = await api.certStatus();
        const data = result.data;
        
        if (!data) throw new Error('无响应数据');
        
        // 存储状态供弹窗使用
        window._certPkiStatus = data;
        
        // 根据状态更新 UI
        const hasKey = data.has_private_key;
        const hasCert = data.has_certificate;
        const hasCa = data.has_ca_chain;
        
        // 更新按钮状态
        btnGenKey.disabled = false;
        btnGenCSR.disabled = !hasKey;
        btnInstall.disabled = !hasKey;
        btnInstallCA.disabled = !hasKey;
        btnView.disabled = !hasCert;
        btnDelete.disabled = !hasKey && !hasCert && !hasCa;
        
        // 状态显示
        switch (data.status) {
            case 'not_initialized':
                statusIcon.textContent = '⚪';
                statusText.textContent = '未初始化';
                noKeyHint.style.display = 'block';
                infoDetails.style.display = 'none';
                expiryBadge.style.display = 'none';
                break;
            case 'key_generated':
                statusIcon.textContent = '🔑';
                statusText.textContent = '密钥已生成，等待 CSR';
                noKeyHint.style.display = 'none';
                infoDetails.style.display = 'none';
                expiryBadge.style.display = 'none';
                break;
            case 'csr_pending':
                statusIcon.textContent = '📋';
                statusText.textContent = 'CSR 已生成，等待签发';
                noKeyHint.style.display = 'none';
                infoDetails.style.display = 'none';
                expiryBadge.style.display = 'none';
                break;
            case 'activated':
                statusIcon.textContent = '✅';
                statusText.textContent = '已激活';
                noKeyHint.style.display = 'none';
                infoDetails.style.display = 'block';
                updateCertInfoDetails(data.cert_info);
                break;
            case 'expired':
                statusIcon.textContent = '❌';
                statusText.textContent = '已过期';
                noKeyHint.style.display = 'none';
                infoDetails.style.display = 'block';
                updateCertInfoDetails(data.cert_info);
                break;
            case 'error':
                statusIcon.textContent = '⚠️';
                statusText.textContent = '错误';
                noKeyHint.style.display = 'none';
                infoDetails.style.display = 'none';
                expiryBadge.style.display = 'none';
                break;
            default:
                statusIcon.textContent = '❓';
                statusText.textContent = data.status_display || data.status;
        }
        
    } catch (e) {
        console.error('Refresh cert status error:', e);
        statusIcon.textContent = '❌';
        statusText.textContent = '加载失败';
        if (noKeyHint) noKeyHint.style.display = 'none';
        if (infoDetails) infoDetails.style.display = 'none';
        if (expiryBadge) expiryBadge.style.display = 'none';
    }
}

function updateCertInfoDetails(certInfo) {
    if (!certInfo) return;
    
    document.getElementById('cert-subject-cn').textContent = certInfo.subject_cn || '-';
    document.getElementById('cert-issuer-cn').textContent = certInfo.issuer_cn || '-';
    document.getElementById('cert-not-before').textContent = certInfo.not_before ? formatTimestamp(certInfo.not_before) : '-';
    document.getElementById('cert-not-after').textContent = certInfo.not_after ? formatTimestamp(certInfo.not_after) : '-';
    document.getElementById('cert-serial').textContent = certInfo.serial || '-';
    document.getElementById('cert-valid-status').textContent = certInfo.is_valid ? '✅ 有效' : '❌ 无效';
    
    // 更新过期徽章
    const expiryBadge = document.getElementById('cert-expiry-badge');
    if (certInfo.days_until_expiry !== undefined) {
        expiryBadge.style.display = 'inline-block';
        if (certInfo.days_until_expiry < 0) {
            expiryBadge.textContent = `已过期 ${Math.abs(certInfo.days_until_expiry)} 天`;
            expiryBadge.className = 'badge badge-danger';
        } else if (certInfo.days_until_expiry < 30) {
            expiryBadge.textContent = `${certInfo.days_until_expiry} 天后过期`;
            expiryBadge.className = 'badge badge-warning';
        } else {
            expiryBadge.textContent = `剩余 ${certInfo.days_until_expiry} 天`;
            expiryBadge.className = 'badge badge-success';
        }
    } else {
        expiryBadge.style.display = 'none';
    }
}

// ====== 证书管理弹窗 ======

function showCertGenKeyModal() {
    const modal = document.getElementById('cert-genkey-modal');
    const warningBox = document.getElementById('cert-genkey-existing-warning');
    const resultBox = document.getElementById('cert-genkey-result');
    
    // 如果已有密钥，显示警告
    if (window._certPkiStatus?.has_private_key) {
        warningBox.classList.remove('hidden');
    } else {
        warningBox.classList.add('hidden');
    }
    
    resultBox.classList.add('hidden');
    modal.classList.remove('hidden');
}

function hideCertGenKeyModal() {
    document.getElementById('cert-genkey-modal').classList.add('hidden');
}

async function generateCertKeypair() {
    const resultBox = document.getElementById('cert-genkey-result');
    const btn = document.getElementById('cert-genkey-btn');
    
    const force = window._certPkiStatus?.has_private_key;
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成密钥对...';
    btn.disabled = true;
    
    try {
        const result = await api.certGenerateKeypair(force);
        if (result.code === 0 || result.data?.success) {
            resultBox.textContent = '✅ ECDSA P-256 密钥对生成成功！';
            resultBox.classList.add('success');
            showToast('密钥对生成成功', 'success');
            
            setTimeout(() => {
                hideCertGenKeyModal();
                refreshCertStatus();
            }, 1000);
        } else {
            throw new Error(result.message || '生成失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 生成失败: ' + e.message;
        resultBox.classList.add('error');
    } finally {
        btn.disabled = false;
    }
}

function showCertCSRModal() {
    const modal = document.getElementById('cert-csr-modal');
    document.getElementById('csr-result-box').classList.add('hidden');
    document.getElementById('csr-gen-result').classList.add('hidden');
    document.getElementById('csr-pem-output').value = '';
    modal.classList.remove('hidden');
}

function hideCertCSRModal() {
    document.getElementById('cert-csr-modal').classList.add('hidden');
}

async function generateCSR() {
    const deviceId = document.getElementById('csr-device-id').value.trim();
    const org = document.getElementById('csr-org').value.trim();
    const ou = document.getElementById('csr-ou').value.trim();
    
    const resultBox = document.getElementById('csr-gen-result');
    const csrResultBox = document.getElementById('csr-result-box');
    const btn = document.getElementById('csr-gen-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成 CSR...';
    btn.disabled = true;
    
    try {
        const opts = {};
        if (deviceId) opts.device_id = deviceId;
        if (org) opts.organization = org;
        if (ou) opts.org_unit = ou;
        
        const result = await api.certGenerateCSR(opts);
        if (result.code === 0 && result.data?.csr_pem) {
            resultBox.classList.add('hidden');
            csrResultBox.classList.remove('hidden');
            document.getElementById('csr-pem-output').value = result.data.csr_pem;
            showToast('CSR 生成成功', 'success');
        } else {
            throw new Error(result.message || '生成失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 生成失败: ' + e.message;
        resultBox.classList.add('error');
    } finally {
        btn.disabled = false;
    }
}

function copyCSRToClipboard() {
    const csr = document.getElementById('csr-pem-output').value;
    navigator.clipboard.writeText(csr).then(() => {
        showToast('CSR 已复制到剪贴板', 'success');
    }).catch(e => {
        showToast('复制失败: ' + e.message, 'error');
    });
}

function showCertInstallModal() {
    const modal = document.getElementById('cert-install-modal');
    document.getElementById('cert-pem-input').value = '';
    document.getElementById('cert-install-result').classList.add('hidden');
    modal.classList.remove('hidden');
}

function hideCertInstallModal() {
    document.getElementById('cert-install-modal').classList.add('hidden');
}

async function installCertificate() {
    const certPem = document.getElementById('cert-pem-input').value.trim();
    if (!certPem) {
        showToast('请输入证书 PEM', 'error');
        return;
    }
    
    const resultBox = document.getElementById('cert-install-result');
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在安装证书...';
    
    try {
        const result = await api.certInstall(certPem);
        if (result.code === 0 || result.data?.success) {
            resultBox.textContent = '✅ 证书安装成功！';
            resultBox.classList.add('success');
            showToast('证书安装成功', 'success');
            
            setTimeout(() => {
                hideCertInstallModal();
                refreshCertStatus();
            }, 1000);
        } else {
            throw new Error(result.message || '安装失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 安装失败: ' + e.message;
        resultBox.classList.add('error');
    }
}

function showCertInstallCAModal() {
    const modal = document.getElementById('cert-ca-modal');
    document.getElementById('ca-pem-input').value = '';
    document.getElementById('ca-install-result').classList.add('hidden');
    modal.classList.remove('hidden');
}

function hideCertInstallCAModal() {
    document.getElementById('cert-ca-modal').classList.add('hidden');
}

async function installCAChain() {
    const caPem = document.getElementById('ca-pem-input').value.trim();
    if (!caPem) {
        showToast('请输入 CA 证书链 PEM', 'error');
        return;
    }
    
    const resultBox = document.getElementById('ca-install-result');
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在安装 CA 证书链...';
    
    try {
        const result = await api.certInstallCA(caPem);
        if (result.code === 0 || result.data?.success) {
            resultBox.textContent = '✅ CA 证书链安装成功！';
            resultBox.classList.add('success');
            showToast('CA 证书链安装成功', 'success');
            
            setTimeout(() => {
                hideCertInstallCAModal();
                refreshCertStatus();
            }, 1000);
        } else {
            throw new Error(result.message || '安装失败');
        }
    } catch (e) {
        resultBox.textContent = '❌ 安装失败: ' + e.message;
        resultBox.classList.add('error');
    }
}

async function showCertViewModal() {
    const modal = document.getElementById('cert-view-modal');
    const loading = document.getElementById('cert-view-loading');
    const content = document.getElementById('cert-view-content');
    
    loading.style.display = 'block';
    content.classList.add('hidden');
    modal.classList.remove('hidden');
    
    try {
        const result = await api.certGetCertificate();
        if (result.code === 0 && result.data?.cert_pem) {
            document.getElementById('cert-view-pem').value = result.data.cert_pem;
            loading.style.display = 'none';
            content.classList.remove('hidden');
        } else {
            throw new Error(result.message || '获取证书失败');
        }
    } catch (e) {
        loading.textContent = '❌ 加载失败: ' + e.message;
    }
}

function hideCertViewModal() {
    document.getElementById('cert-view-modal').classList.add('hidden');
}

function copyCertToClipboard() {
    const cert = document.getElementById('cert-view-pem').value;
    navigator.clipboard.writeText(cert).then(() => {
        showToast('证书已复制到剪贴板', 'success');
    }).catch(e => {
        showToast('复制失败: ' + e.message, 'error');
    });
}

async function deleteCertCredentials() {
    if (!confirm('⚠️ 确定要删除所有 PKI 凭证吗？\n\n这将删除：\n• 私钥\n• 设备证书\n• CA 证书链\n\n此操作不可撤销！')) {
        return;
    }
    
    try {
        const result = await api.certDelete();
        if (result.code === 0 || result.data?.success) {
            showToast('PKI 凭证已删除', 'success');
            await refreshCertStatus();
        } else {
            throw new Error(result.message || '删除失败');
        }
    } catch (e) {
        showToast('删除失败: ' + e.message, 'error');
    }
}

function showGenerateKeyModal() {
    document.getElementById('keygen-modal').classList.remove('hidden');
    document.getElementById('keygen-id').value = '';
    document.getElementById('keygen-type').value = 'rsa2048';  // RSA 是唯一支持 SSH 公钥认证的类型
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
    const alias = document.getElementById('keygen-alias').value.trim();
    const exportable = document.getElementById('keygen-exportable').checked;
    const hidden = document.getElementById('keygen-hidden').checked;
    
    if (!id) {
        showToast('请输入密钥 ID', 'error');
        return;
    }
    
    try {
        showToast('正在生成密钥...', 'info');
        await api.keyGenerate(id, type, comment, exportable, alias, hidden);
        hideGenerateKeyModal();
        showToast(`密钥 "${alias || id}" 生成成功`, 'success');
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

function showToast(message, type = 'info', duration = 3000) {
    // 创建 toast 元素
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        document.body.appendChild(toast);
    }
    
    // 支持多行文本（将 \n 转为 <br>）
    toast.innerHTML = message.replace(/\n/g, '<br>');
    toast.className = `toast toast-${type} show`;
    
    setTimeout(() => {
        toast.classList.remove('show');
    }, duration);
}

// =========================================================================
//                         终端页面
// =========================================================================

async function loadTerminalPage() {
    // 取消系统页面的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
    // 清理之前的终端实例
    if (webTerminal) {
        webTerminal.destroy();
        webTerminal = null;
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="terminal-page">
            <div class="terminal-header">
                <div class="terminal-actions">
                    <button class="btn btn-sm" onclick="console.log('Button clicked!'); window.showTerminalLogsModal();">📋 日志</button>
                    <button class="btn btn-sm" onclick="terminalClear()">清屏</button>
                    <button class="btn btn-sm btn-danger" onclick="terminalDisconnect()">断开</button>
                </div>
            </div>
            <div class="terminal-container" id="terminal-container"></div>
            <div class="terminal-help">
                <span>💡 提示: 输入 <code>help</code> 查看命令 | <code>Ctrl+C</code> 中断 | <code>Ctrl+L</code> 清屏 | <code>↑↓</code> 历史</span>
            </div>
        </div>
        
        <!-- 日志模态框 -->
        <div id="terminal-logs-modal" class="modal" style="display:none" onclick="if(event.target===this) closeTerminalLogsModal()">
            <div class="modal-content" style="width:90%; max-width:1200px; height:85vh">
                <div class="modal-header">
                    <h2>📋 系统日志</h2>
                    <button class="modal-close" onclick="closeTerminalLogsModal()">&times;</button>
                </div>
                <div class="modal-body" style="padding:0; display:flex; flex-direction:column; height:calc(100% - 60px)">
                    <!-- 工具栏 -->
                    <div class="log-toolbar" style="margin:15px; margin-bottom:10px">
                        <div class="toolbar-left">
                            <div class="toolbar-item">
                                <label>级别</label>
                                <select id="modal-log-level-filter" class="form-control" onchange="updateModalLogFilter()">
                                    <option value="5">全部</option>
                                    <option value="1">ERROR</option>
                                    <option value="2">WARN+</option>
                                    <option value="3" selected>INFO+</option>
                                    <option value="4">DEBUG+</option>
                                </select>
                            </div>
                            <div class="toolbar-item">
                                <label>TAG</label>
                                <input type="text" id="modal-log-tag-filter" class="form-control" 
                                       placeholder="过滤TAG..." onkeyup="debounceRenderModalLogs()">
                            </div>
                            <div class="toolbar-item search">
                                <label>搜索</label>
                                <input type="text" id="modal-log-keyword-filter" class="form-control" 
                                       placeholder="搜索日志..." onkeyup="debounceRenderModalLogs()">
                            </div>
                        </div>
                        <div class="toolbar-right">
                            <span id="modal-ws-status" class="ws-status connecting" title="WebSocket 连接状态">
                                <span class="dot"></span>
                            </span>
                            <span id="modal-log-stats" class="log-stats"></span>
                            <label class="auto-scroll-toggle">
                                <input type="checkbox" id="modal-log-auto-scroll" checked>
                                <span>自动滚动</span>
                            </label>
                            <button class="btn btn-small" onclick="loadModalHistoryLogs()" title="刷新日志">🔄</button>
                            <button class="btn btn-small btn-danger" onclick="clearModalLogs()" title="清空日志">🗑️</button>
                        </div>
                    </div>
                    
                    <!-- 日志内容 -->
                    <div class="log-panel" style="flex:1; margin:0 15px 15px; overflow:hidden">
                        <div id="modal-log-container" class="log-viewer">
                            <div class="log-empty">
                                <div class="icon">📋</div>
                                <div class="text">等待日志...</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
        
        <style>
            /* 日志工具栏 */
            .log-toolbar {
                display: flex;
                justify-content: space-between;
                align-items: center;
                gap: 15px;
                padding: 12px 15px;
                background: var(--card-bg);
                border-radius: 8px;
                flex-wrap: wrap;
            }
            .toolbar-left {
                display: flex;
                gap: 12px;
                flex-wrap: wrap;
                align-items: center;
            }
            .toolbar-right {
                display: flex;
                gap: 10px;
                align-items: center;
                flex-wrap: wrap;
            }
            .toolbar-item {
                display: flex;
                align-items: center;
                gap: 6px;
            }
            .toolbar-item label {
                font-size: 0.85em;
                color: var(--text-light);
                white-space: nowrap;
            }
            .toolbar-item .form-control {
                padding: 6px 10px;
                font-size: 0.9em;
                min-width: 100px;
            }
            .toolbar-item.search .form-control {
                min-width: 150px;
            }
            
            .log-stats {
                font-size: 0.85em;
                color: var(--text-light);
            }
            
            .auto-scroll-toggle {
                display: flex;
                align-items: center;
                gap: 4px;
                font-size: 0.85em;
                color: var(--text-light);
                cursor: pointer;
            }
            .auto-scroll-toggle input {
                cursor: pointer;
            }
            
            /* 日志面板 */
            .log-panel {
                flex: 1;
                background: var(--card-bg);
                border-radius: 8px;
                overflow: hidden;
                display: flex;
                flex-direction: column;
            }
            
            .log-viewer {
                flex: 1;
                font-family: 'SF Mono', 'Monaco', 'Menlo', 'Ubuntu Mono', 'Consolas', monospace;
                font-size: 12px;
                line-height: 1.6;
                background: #1a1a2e;
                color: #eee;
                padding: 12px;
                overflow-y: auto;
                min-height: 400px;
            }
            
            .log-entry {
                padding: 3px 8px;
                border-radius: 3px;
                margin: 2px 0;
                display: flex;
                align-items: baseline;
                gap: 8px;
            }
            .log-entry:hover {
                background: rgba(255,255,255,0.05);
            }
            .log-time {
                color: #666;
                font-size: 0.9em;
                flex-shrink: 0;
            }
            .log-level {
                font-weight: 600;
                font-size: 0.85em;
                padding: 1px 6px;
                border-radius: 3px;
                flex-shrink: 0;
                min-width: 55px;
                text-align: center;
            }
            .log-tag {
                color: #64b5f6;
                flex-shrink: 0;
                max-width: 150px;
                overflow: hidden;
                text-overflow: ellipsis;
            }
            .log-message {
                flex: 1;
                word-break: break-word;
            }
            .log-task {
                color: #666;
                font-size: 0.85em;
                flex-shrink: 0;
            }
            
            /* 日志级别颜色 */
            .level-error { border-left: 3px solid #ef5350; }
            .level-error .log-level { background: #ef5350; color: #fff; }
            .level-warn { border-left: 3px solid #ffa726; }
            .level-warn .log-level { background: #ffa726; color: #000; }
            .level-info { border-left: 3px solid #66bb6a; }
            .level-info .log-level { background: rgba(102,187,106,0.2); color: #66bb6a; }
            .level-debug { border-left: 3px solid #42a5f5; }
            .level-debug .log-level { background: rgba(66,165,245,0.2); color: #42a5f5; }
            .level-verbose { border-left: 3px solid #78909c; }
            .level-verbose .log-level { background: rgba(120,144,156,0.2); color: #78909c; }
            
            .log-empty {
                display: flex;
                flex-direction: column;
                align-items: center;
                justify-content: center;
                height: 200px;
                color: #666;
            }
            .log-empty .icon {
                font-size: 3em;
                margin-bottom: 10px;
                opacity: 0.5;
            }
            .log-empty .text {
                font-size: 1.1em;
            }
            
            .log-highlight {
                background: #ffeb3b;
                color: #000;
                padding: 0 3px;
                border-radius: 2px;
            }
            
            /* WebSocket 状态动画 */
            @keyframes pulse {
                0%, 100% { opacity: 1; }
                50% { opacity: 0.5; }
            }
            @keyframes blink {
                0%, 100% { opacity: 1; }
                50% { opacity: 0.3; }
            }
        </style>
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

// 终端页面日志模态框
let modalLogEntries = [];
let modalLogDebounceTimer = null;
let modalLogSubscribed = false;
const MAX_MODAL_LOG_ENTRIES = 1000;

function showTerminalLogsModal() {
    console.log('[Modal] showTerminalLogsModal called - START');
    const modal = document.getElementById('terminal-logs-modal');
    console.log('[Modal] showTerminalLogsModal called, modal:', modal);
    
    if (!modal) {
        console.error('[Modal] Modal element not found!');
        return;
    }
    
    modal.style.display = 'flex';
    console.log('[Modal] Modal display set to flex');
    modalLogEntries.length = 0;  // 清空但保持引用
    
    console.log('[Modal] About to call subscribeToModalLogs...');
    console.log('[Modal] typeof subscribeToModalLogs:', typeof subscribeToModalLogs);
    
    // 启动实时订阅
    subscribeToModalLogs();
    
    console.log('[Modal] showTerminalLogsModal - END');
}

function closeTerminalLogsModal() {
    const modal = document.getElementById('terminal-logs-modal');
    if (modal) {
        modal.style.display = 'none';
        unsubscribeFromModalLogs();
        modalLogEntries.length = 0;
    }
}

// 订阅模态框日志
function subscribeToModalLogs() {
    console.log('[Modal] subscribeToModalLogs called');
    const levelFilter = document.getElementById('modal-log-level-filter')?.value || '3';
    const minLevel = parseInt(levelFilter);
    console.log('[Modal] Level filter:', minLevel);
    
    if (window.ws && window.ws.readyState === WebSocket.OPEN) {
        console.log('[Modal] WebSocket is open, sending log_subscribe...');
        window.ws.send({
            type: 'log_subscribe',
            minLevel: minLevel
        });
        modalLogSubscribed = true;
        updateModalWsStatus(true);
        console.log('[Modal] Subscription sent, loading history...');
        // 订阅成功后加载历史日志
        loadModalHistoryLogs();
    } else {
        console.warn('[Modal] WebSocket not ready, readyState:', window.ws?.readyState);
        updateModalWsStatus(false);
        setTimeout(subscribeToModalLogs, 1000);
    }
}

// 取消订阅模态框日志
function unsubscribeFromModalLogs() {
    console.log('[Modal] unsubscribeFromModalLogs called, subscribed:', modalLogSubscribed);
    if (window.ws && window.ws.readyState === WebSocket.OPEN && modalLogSubscribed) {
        console.log('[Modal] Sending log_unsubscribe...');
        window.ws.send({ type: 'log_unsubscribe' });
    }
    modalLogSubscribed = false;
    updateModalWsStatus(false);
}

// 加载历史日志
async function loadModalHistoryLogs() {
    if (!window.ws || window.ws.readyState !== WebSocket.OPEN) {
        console.error('[Modal] WebSocket 未连接');
        return;
    }
    
    const levelFilter = document.getElementById('modal-log-level-filter')?.value || '3';
    
    // 通过 WebSocket 请求历史日志
    window.ws.send({
        type: 'log_get_history',
        limit: 500,
        minLevel: 1,
        maxLevel: parseInt(levelFilter)
    });
}

// 更新模态框WebSocket状态显示
function updateModalWsStatus(connected) {
    const statusEl = document.getElementById('modal-ws-status');
    if (statusEl) {
        if (connected) {
            statusEl.className = 'ws-status connected';
            statusEl.title = 'WebSocket 已连接 - 实时日志';
        } else {
            statusEl.className = 'ws-status connecting';
            statusEl.title = 'WebSocket 连接中...';
        }
    }
}

function updateModalLogFilter() {
    const levelFilter = document.getElementById('modal-log-level-filter')?.value || '3';
    const minLevel = parseInt(levelFilter);
    
    // 更新 WebSocket 订阅级别
    if (window.ws && window.ws.readyState === WebSocket.OPEN && modalLogSubscribed) {
        window.ws.send({
            type: 'log_set_level',
            minLevel: minLevel
        });
    }
    
    // 重新渲染现有日志
    renderModalLogs();
}

function debounceRenderModalLogs() {
    if (modalLogDebounceTimer) clearTimeout(modalLogDebounceTimer);
    modalLogDebounceTimer = setTimeout(renderModalLogs, 300);
}

function renderModalLogs() {
    const container = document.getElementById('modal-log-container');
    console.log('[Modal] renderModalLogs called, container:', container);
    console.log('[Modal] modalLogEntries length:', modalLogEntries.length);
    
    if (!container) {
        console.error('[Modal] Container not found!');
        return;
    }
    
    // 获取过滤条件
    const levelFilter = parseInt(document.getElementById('modal-log-level-filter')?.value || '3');
    const tagFilter = document.getElementById('modal-log-tag-filter')?.value.toLowerCase().trim() || '';
    const keywordFilter = document.getElementById('modal-log-keyword-filter')?.value.toLowerCase().trim() || '';
    
    // 过滤日志
    let filtered = modalLogEntries.filter(entry => {
        // 级别过滤
        if (entry.level > levelFilter) return false;
        // TAG 过滤
        if (tagFilter && !entry.tag.toLowerCase().includes(tagFilter)) return false;
        // 关键词过滤
        if (keywordFilter && !entry.message.toLowerCase().includes(keywordFilter)) return false;
        return true;
    });
    
    // 更新统计
    const statsElem = document.getElementById('modal-log-stats');
    if (statsElem) {
        statsElem.textContent = `显示 ${filtered.length}/${modalLogEntries.length} 条`;
    }
    
    if (filtered.length === 0) {
        container.innerHTML = `
            <div class="log-empty">
                <div class="icon">📋</div>
                <div class="text">暂无日志</div>
            </div>
        `;
        return;
    }
    
    // 渲染日志
    const html = filtered.map(entry => {
        const time = new Date(entry.timestamp).toLocaleTimeString('zh-CN', { hour12: false });
        const levelClass = `level-${entry.levelName.toLowerCase()}`;
        
        // 高亮关键词
        let message = escapeHtml(entry.message);
        if (keywordFilter) {
            const regex = new RegExp(`(${escapeRegex(keywordFilter)})`, 'gi');
            message = message.replace(regex, '<span class="log-highlight">$1</span>');
        }
        
        return `
            <div class="log-entry ${levelClass}">
                <span class="log-time">${time}</span>
                <span class="log-level">${entry.levelName}</span>
                <span class="log-tag">${escapeHtml(entry.tag)}</span>
                <span class="log-message">${message}</span>
                ${entry.task ? `<span class="log-task">[${escapeHtml(entry.task)}]</span>` : ''}
            </div>
        `;
    }).join('');
    
    container.innerHTML = html;
    
    // 自动滚动
    const autoScroll = document.getElementById('modal-log-auto-scroll')?.checked;
    if (autoScroll) {
        container.scrollTop = container.scrollHeight;
    }
}

function clearModalLogs() {
    modalLogEntries.length = 0;
    renderModalLogs();
}

// 处理模态框实时日志消息
function handleModalLogMessage(msg) {
    console.log('[Modal] Received log message:', msg);
    
    const logEntry = {
        level: msg.level || 3,
        levelName: getLevelName(msg.level || 3),
        tag: msg.tag || 'unknown',
        message: msg.message || '',
        timestamp: msg.timestamp || Date.now(),
        task: msg.task || ''
    };
    
    // 追加日志（限制最大数量）
    modalLogEntries.push(logEntry);
    console.log('[Modal] Added log, total entries:', modalLogEntries.length);
    
    if (modalLogEntries.length > MAX_MODAL_LOG_ENTRIES) {
        modalLogEntries.shift();  // 移除最旧的日志
    }
    
    // 重新渲染
    renderModalLogs();
}

function getLevelName(level) {
    const names = { 1: 'ERROR', 2: 'WARN', 3: 'INFO', 4: 'DEBUG', 5: 'VERBOSE' };
    return names[level] || 'UNKNOWN';
}

function escapeHtml(text) {
    const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#039;' };
    return text.replace(/[&<>"']/g, m => map[m]);
}

function escapeRegex(text) {
    return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// 暴露给全局作用域（WebSocket 处理需要）
window.getLevelName = getLevelName;
window.handleModalLogMessage = handleModalLogMessage;

// 暴露给 HTML onclick
window.showTerminalLogsModal = showTerminalLogsModal;
window.closeTerminalLogsModal = closeTerminalLogsModal;
window.loadModalHistoryLogs = loadModalHistoryLogs;
window.updateModalLogFilter = updateModalLogFilter;
window.debounceRenderModalLogs = debounceRenderModalLogs;
window.clearModalLogs = clearModalLogs;
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
window.setFanSpeed = setFanSpeed;
// Commands page functions
window.loadCommandsPage = loadCommandsPage;
window.selectHost = selectHost;
window.showAddCommandModal = showAddCommandModal;
window.closeCommandModal = closeCommandModal;
window.selectCmdIcon = selectCmdIcon;
window.saveCommand = saveCommand;
window.editCommand = editCommand;
window.deleteCommand = deleteCommand;
window.exportSshCommand = exportSshCommand;
window.showExportSshCommandModal = showExportSshCommandModal;
window.hideExportSshCommandModal = hideExportSshCommandModal;
window.doExportSshCommandFromModal = doExportSshCommandFromModal;
window.showImportSshCommandModal = showImportSshCommandModal;
window.hideImportSshCommandModal = hideImportSshCommandModal;
window.previewSshCommandImport = previewSshCommandImport;
window.confirmSshCommandImport = confirmSshCommandImport;
window.executeCommand = executeCommand;
window.cancelExecution = cancelExecution;
window.clearExecResult = clearExecResult;
window.validateCommandId = validateCommandId;
// Security page functions
window.refreshSshHostsList = refreshSshHostsList;
window.refreshKnownHostsList = refreshKnownHostsList;
window.showFullFingerprint = showFullFingerprint;
window.removeKnownHost = removeKnownHost;
window.deleteSshHostFromSecurity = deleteSshHostFromSecurity;
window.testSshConnection = testSshConnection;
window.testSshHostByIndex = testSshHostByIndex;
window.removeHostByIndex = removeHostByIndex;
window.exportSshHost = exportSshHost;
window.showExportSshHostModal = showExportSshHostModal;
window.hideExportSshHostModal = hideExportSshHostModal;
window.doExportSshHostFromModal = doExportSshHostFromModal;
window.showImportSshHostModal = showImportSshHostModal;
window.hideImportSshHostModal = hideImportSshHostModal;
window.previewSshHostImport = previewSshHostImport;
window.confirmSshHostImport = confirmSshHostImport;
window.revokeKeyFromHost = revokeKeyFromHost;
window.hideRevokeHostModal = hideRevokeHostModal;
window.doRevokeFromHost = doRevokeFromHost;
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
// 数据监控组件
window.refreshDataWidgets = refreshDataWidgets;
window.showWidgetManager = showWidgetManager;
window.showAddWidgetPanel = showAddWidgetPanel;
window.addWidgetFromPreset = addWidgetFromPreset;
window.createNewWidget = createNewWidget;
window.showWidgetEditPanel = showWidgetEditPanel;
window.saveWidgetEdit = saveWidgetEdit;
window.deleteDataWidget = deleteDataWidget;
window.moveWidget = moveWidget;
window.selectVariableForWidget = selectVariableForWidget;
window.updateRefreshInterval = updateRefreshInterval;
// 日志组件
window.toggleLogReading = toggleLogReading;
window.startLogReading = startLogReading;
window.stopLogReading = stopLogReading;
window.refreshLogOnce = refreshLogOnce;
window.clearLogWidget = clearLogWidget;
window.updateLayoutPreview = function() {
    // 更新布局选项的激活状态
    document.querySelectorAll('.dw-layout-option').forEach(opt => {
        const radio = opt.querySelector('input[type="radio"]');
        opt.classList.toggle('active', radio?.checked);
    });
};

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

// =========================================================================
//                         OTA 页面
// =========================================================================

async function loadOtaPage() {
    clearInterval(refreshInterval);
    
    // 取消系统页面的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-ota">
            <h1>固件升级</h1>
            
            <!-- 核心信息区：版本 + OTA服务器 -->
            <div class="ota-main-card">
                <!-- 第一行：版本号（最醒目） -->
                <div class="ota-current-version">
                    <span class="version-label">当前版本</span>
                    <span class="version-number" id="ota-current-version">-</span>
                </div>
                <div class="version-meta" id="ota-version-meta">加载中...</div>
                
                <!-- 第二行：OTA服务器 -->
                <div class="ota-server-row">
                    <label class="server-label">OTA 服务器</label>
                    <div class="server-input-group">
                        <input type="text" id="ota-server-input" class="form-input" 
                               placeholder="http://192.168.1.100:57807">
                        <button class="btn btn-icon" onclick="saveOtaServer()" title="保存到设备">保存</button>
                        <button class="btn btn-primary" onclick="checkForUpdates()" style="background-color: #f0f8ff; color: #333;">检查更新</button>
                    </div>
                </div>
                
                <!-- 更新状态区（动态显示） -->
                <div id="ota-update-status" class="ota-update-status" style="display:none"></div>
                
                <!-- 升级进度区（动态显示） -->
                <div id="ota-progress-section" class="ota-progress-section" style="display:none">
                    <div class="progress-header">
                        <span class="progress-state" id="ota-state-text">准备中...</span>
                        <span class="progress-percent" id="ota-progress-percent">0%</span>
                    </div>
                    <div class="progress-bar-container">
                        <div class="progress-bar-fill" id="ota-progress-bar" style="width:0%"></div>
                    </div>
                    <div class="progress-footer">
                        <span id="ota-progress-size">0 / 0</span>
                        <span id="ota-message"></span>
                    </div>
                    <div class="progress-actions">
                        <button class="btn btn-danger btn-small" id="ota-abort-btn" onclick="abortOta()">中止</button>
                    </div>
                </div>
            </div>
            
            <!-- 分区管理（放在升级方式之前，让用户先了解当前状态） -->
            <details class="ota-section" open>
                <summary>分区管理</summary>
                <div class="ota-partitions" id="ota-partitions">
                    <div class="loading">加载中...</div>
                </div>
            </details>
            
            <!-- 手动升级（可折叠） -->
            <details class="ota-section">
                <summary>手动升级</summary>
                <div class="ota-methods">
                    <div class="ota-method">
                        <h4>从 URL 升级</h4>
                        <div class="method-content">
                            <input type="text" id="ota-url-input" class="form-input" 
                                   placeholder="http://example.com/firmware.bin">
                            <div class="method-options">
                                <label><input type="checkbox" id="ota-url-include-www" checked> 包含 WebUI</label>
                                <label><input type="checkbox" id="ota-url-skip-verify"> 跳过验证</label>
                            </div>
                            <button class="btn btn-primary btn-small" onclick="otaFromUrl()" style="background-color: #f0f8ff; color: #333;">升级</button>
                        </div>
                    </div>
                    <div class="ota-method">
                        <h4>从 SD 卡升级</h4>
                        <div class="method-content">
                            <input type="text" id="ota-file-input" class="form-input" 
                                   placeholder="/sdcard/firmware.bin">
                            <div class="method-options">
                                <label><input type="checkbox" id="ota-file-include-www" checked> 包含 WebUI</label>
                            </div>
                            <button class="btn btn-primary btn-small" onclick="otaFromFile()" style="background-color: #f0f8ff; color: #333;">升级</button>
                        </div>
                    </div>
                </div>
            </details>
        </div>
        
        <style>
        .page-ota {
            padding: 15px;
            max-width: 700px;
            margin: 0 auto;
        }
        
        .page-ota h1 {
            margin: 0 0 15px 0;
            font-size: 1.4em;
        }
        
        /* 主卡片 */
        .ota-main-card {
            background: white;
            border-radius: 12px;
            padding: 20px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.1);
            margin-bottom: 15px;
        }
        
        /* 版本显示 */
        .ota-current-version {
            display: flex;
            align-items: baseline;
            gap: 12px;
            margin-bottom: 4px;
        }
        
        .version-label {
            font-size: 0.9em;
            color: #666;
        }
        
        .version-number {
            font-size: 1em;
            font-weight: 700;
            color: #333;
            font-family: 'SF Mono', 'Courier New', monospace;
            letter-spacing: -0.5px;
        }
        
        .version-meta {
            font-size: 0.85em;
            color: #888;
            margin-bottom: 16px;
            padding-bottom: 16px;
            border-bottom: 1px solid #eee;
        }
        
        /* OTA 服务器行 */
        .ota-server-row {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        
        .server-label {
            font-size: 0.9em;
            color: #666;
            white-space: nowrap;
        }
        
        .server-input-group {
            flex: 1;
            display: flex;
            gap: 8px;
            align-items: center;
        }
        
        .server-input-group .form-input {
            flex: 1;
            padding: 10px 12px;
            border: 1px solid #ddd;
            border-radius: 6px;
            font-size: 0.95em;
            min-width: 0;
        }
        
        .server-input-group .form-input:focus {
            outline: none;
            border-color: #4CAF50;
        }
        
        .btn-icon {
            padding: 8px 10px;
            border: 1px solid #ddd;
            background: #f9f9f9;
            border-radius: 6px;
            cursor: pointer;
            font-size: 1em;
        }
        
        .btn-icon:hover {
            background: #eee;
        }
        
        /* 更新状态 */
        .ota-update-status {
            margin-top: 15px;
            padding: 15px;
            border-radius: 8px;
            animation: fadeIn 0.3s ease;
        }
        
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(-10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        
        .ota-update-status.has-update {
            background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%);
            border: 1px solid #81c784;
        }
        
        .ota-update-status.no-update {
            background: #e3f2fd;
            border: 1px solid #90caf9;
        }
        
        .ota-update-status.downgrade {
            background: #fff3e0;
            border: 1px solid #ffb74d;
        }
        
        .ota-update-status.error {
            background: #ffebee;
            border: 1px solid #ef9a9a;
        }
        
        /* 进度区 */
        .ota-progress-section {
            margin-top: 15px;
            padding: 15px;
            background: #f5f5f5;
            border-radius: 8px;
        }
        
        .progress-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }
        
        .progress-state {
            font-weight: 600;
            color: #333;
        }
        
        .progress-percent {
            font-weight: 700;
            font-size: 1.2em;
            color: #4CAF50;
        }
        
        .progress-bar-container {
            height: 8px;
            background: #ddd;
            border-radius: 4px;
            overflow: hidden;
        }
        
        .progress-bar-fill {
            height: 100%;
            background: linear-gradient(90deg, #4CAF50, #81c784);
            transition: width 0.3s ease;
        }
        
        .progress-footer {
            display: flex;
            justify-content: space-between;
            font-size: 0.85em;
            color: #666;
            margin-top: 8px;
        }
        
        .progress-actions {
            margin-top: 10px;
            text-align: right;
        }
        
        /* 可折叠区 */
        .ota-section {
            background: white;
            border-radius: 8px;
            margin-bottom: 10px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.08);
        }
        
        .ota-section summary {
            padding: 12px 15px;
            cursor: pointer;
            font-weight: 600;
            color: #333;
            user-select: none;
        }
        
        .ota-section summary:hover {
            background: #f9f9f9;
        }
        
        .ota-section[open] summary {
            border-bottom: 1px solid #eee;
        }
        
        /* 升级方式 */
        .ota-methods {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
            gap: 15px;
            padding: 15px;
        }
        
        .ota-method {
            border: 1px solid #eee;
            border-radius: 8px;
            padding: 15px;
        }
        
        .ota-method h4 {
            margin: 0 0 10px 0;
            font-size: 1em;
            color: #555;
        }
        
        .method-content {
            display: flex;
            flex-direction: column;
            gap: 10px;
        }
        
        .method-content .form-input {
            padding: 8px 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 0.9em;
        }
        
        .method-options {
            display: flex;
            gap: 15px;
            font-size: 0.85em;
            color: #666;
        }
        
        .method-options label {
            display: flex;
            align-items: center;
            gap: 4px;
            cursor: pointer;
        }
        
        /* 分区管理 - 合并后的样式 */
        .ota-partitions {
            padding: 15px;
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 12px;
        }
        
        .partition-card {
            border: 2px solid #ddd;
            border-radius: 10px;
            padding: 15px;
            background: #fafafa;
            display: flex;
            flex-direction: column;
        }
        
        .partition-card.running {
            border-color: #4CAF50;
            background: linear-gradient(135deg, #f1f8e9 0%, #e8f5e9 100%);
        }
        
        .partition-card.bootable {
            border-color: #ff9800;
            background: linear-gradient(135deg, #fff8e1 0%, #fff3e0 100%);
        }
        
        .partition-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 8px;
        }
        
        .partition-name {
            font-weight: 600;
            font-family: monospace;
            font-size: 1.1em;
        }
        
        .partition-badge {
            font-size: 0.75em;
            padding: 3px 10px;
            border-radius: 12px;
            color: white;
            font-weight: 500;
        }
        
        .partition-badge.running { background: #4CAF50; }
        .partition-badge.bootable { background: #ff9800; }
        .partition-badge.idle { background: #999; }
        
        .partition-version {
            font-size: 1em;
            font-weight: 600;
            color: #333;
            margin-bottom: 4px;
        }
        
        .partition-info {
            font-size: 0.85em;
            color: #666;
            margin-bottom: 12px;
        }
        
        .partition-action {
            margin-top: auto;
            padding-top: 10px;
            border-top: 1px solid rgba(0,0,0,0.1);
        }
        
        .partition-action .btn {
            width: 100%;
            justify-content: center;
        }
        
        .partition-action-desc {
            font-size: 0.8em;
            color: #888;
            margin-top: 6px;
            text-align: center;
        }
        
        /* 移动端适配 */
        @media (max-width: 600px) {
            .ota-server-row {
                flex-direction: column;
                align-items: stretch;
            }
            
            .server-label {
                margin-bottom: 5px;
            }
            
            .server-input-group {
                flex-wrap: wrap;
            }
            
            .server-input-group .form-input {
                width: 100%;
                flex: none;
            }
            
            .server-input-group .btn {
                flex: 1;
            }
        }
        </style>
    `;
    
    // 加载数据
    await loadOtaData();
    
    // 设置定时刷新进度
    refreshInterval = setInterval(refreshOtaProgress, 1000);
}

async function loadOtaData() {
    try {
        // 1. 加载 OTA 服务器地址
        const serverResult = await api.call('ota.server.get');
        if (serverResult?.code === 0 && serverResult.data?.url) {
            document.getElementById('ota-server-input').value = serverResult.data.url;
        }
        
        // 2. 加载版本信息
        const versionResult = await api.call('ota.version');
        if (versionResult?.code === 0 && versionResult.data) {
            const v = versionResult.data;
            document.getElementById('ota-current-version').textContent = v.version || '未知';
            document.getElementById('ota-version-meta').textContent = 
                `${v.project || 'TianshanOS'} · ${v.compile_date || ''} ${v.compile_time || ''} · IDF ${v.idf_version || ''}`;
            currentFirmwareVersion = v;
        }
        
        // 3. 加载分区信息
        const partResult = await api.call('ota.partitions');
        if (partResult?.code === 0 && partResult.data) {
            displayPartitionsCompact(partResult.data);
        }
        
        // 4. 检查当前升级状态
        await refreshOtaProgress();
        
    } catch (error) {
        console.error('Failed to load OTA data:', error);
    }
}

function displayPartitionsCompact(data) {
    const container = document.getElementById('ota-partitions');
    let html = '';
    
    // 运行中的分区
    if (data.running) {
        const p = data.running;
        html += `
            <div class="partition-card running">
                <div class="partition-header">
                    <span class="partition-name">${p.label}</span>
                    <span class="partition-badge running">运行中</span>
                </div>
                <div class="partition-version">${p.version || '未知版本'}</div>
                <div class="partition-info">
                    0x${p.address.toString(16).toUpperCase().padStart(8,'0')} · ${formatSize(p.size)}
                </div>
                <div class="partition-action">
                    <button class="btn btn-success btn-small" onclick="validateOta()">
                        标记有效
                    </button>
                    <div class="partition-action-desc">取消自动回滚保护</div>
                </div>
            </div>
        `;
    }
    
    // 备用分区
    if (data.next) {
        const p = data.next;
        const hasVersion = p.is_bootable && p.version;
        const canRollback = data.can_rollback;  // 使用 API 返回的实际可回滚状态
        html += `
            <div class="partition-card ${p.is_bootable ? 'bootable' : ''}">
                <div class="partition-header">
                    <span class="partition-name">${p.label}</span>
                    <span class="partition-badge ${p.is_bootable ? 'bootable' : 'idle'}">${p.is_bootable ? '可启动' : '空闲'}</span>
                </div>
                <div class="partition-version">${hasVersion ? p.version : (p.is_bootable ? '上一版本' : '无固件')}</div>
                <div class="partition-info">
                    0x${p.address.toString(16).toUpperCase().padStart(8,'0')} · ${formatSize(p.size)}
                </div>
                ${canRollback ? `
                <div class="partition-action">
                    <button class="btn btn-warning btn-small" onclick="confirmRollback()">
                        回滚到此版本
                    </button>
                    <div class="partition-action-desc">重启后加载此分区</div>
                </div>
                ` : `
                <div class="partition-action">
                    <div class="partition-action-desc" style="text-align:center;color:#999">
                        ${p.is_bootable ? '此分区固件无法回滚（可能已损坏）' : '此分区为空，升级后将写入新固件'}
                    </div>
                </div>
                `}
            </div>
        `;
    }
    
    container.innerHTML = html || '<p style="color:#888;padding:10px">无分区信息</p>';
}

async function refreshOtaInfo() {
    await loadOtaData();
}

// OTA 两步升级状态
let otaStep = 'idle'; // 'idle' | 'app' | 'www'
let wwwOtaEnabled = true;  // 是否启用 WebUI 升级
let sdcardOtaSource = '';  // SD卡升级时的文件路径，用于推导 www.bin 路径

async function refreshOtaProgress() {
    try {
        // 根据当前步骤获取不同的进度
        let result;
        if (otaStep === 'www') {
            result = await api.call('ota.www.progress');
        } else {
            result = await api.call('ota.progress');
        }
        
        if (result.code === 0 && result.data) {
            const data = result.data;
            const state = data.state || 'idle';
            const percent = data.percent || 0;
            const received = data.received_size || data.received || 0;
            const total = data.total_size || data.total || 0;
            const message = data.message || '';
            
            // 更新状态文本
            const stateMap = {
                'idle': '空闲',
                'checking': '检查更新中...',
                'downloading': otaStep === 'www' ? '下载 WebUI...' : '下载固件...',
                'verifying': '验证中...',
                'writing': otaStep === 'www' ? '写入 WebUI...' : '写入闪存...',
                'pending_reboot': '等待重启',
                'completed': otaStep === 'www' ? 'WebUI 完成' : '固件完成',
                'error': '错误'
            };
            
            const stateEl = document.getElementById('ota-state-text');
            const progressSection = document.getElementById('ota-progress-section');
            const abortBtn = document.getElementById('ota-abort-btn');
            
            if (!stateEl || !progressSection) return;
            
            // 显示当前步骤
            const stepText = otaStep === 'www' ? '[2/2] WebUI ' : (wwwOtaEnabled ? '[1/2] 固件 ' : '');
            stateEl.textContent = stepText + (stateMap[state] || state);
            
            if (state !== 'idle') {
                progressSection.style.display = 'block';
                
                // 更新进度条
                document.getElementById('ota-progress-bar').style.width = percent + '%';
                document.getElementById('ota-progress-percent').textContent = percent + '%';
                document.getElementById('ota-progress-size').textContent = 
                    `${formatSize(received)} / ${formatSize(total)}`;
                
                // 更新消息
                document.getElementById('ota-message').textContent = message;
                
                // 显示中止按钮（除非已完成或出错）
                if (state !== 'pending_reboot' && state !== 'completed' && state !== 'error') {
                    abortBtn.style.display = 'inline-block';
                } else {
                    abortBtn.style.display = 'none';
                }
                
                // 处理 App OTA 完成 - 开始 WWW OTA
                if (otaStep === 'app' && (state === 'pending_reboot' || state === 'completed') && wwwOtaEnabled) {
                    stateEl.textContent = '固件升级完成，准备升级 WebUI...';
                    await startWwwOta();
                    return;
                }
                
                // 处理 WWW OTA 完成或 App OTA 完成（无 www 升级）
                if ((otaStep === 'www' && (state === 'pending_reboot' || state === 'completed')) ||
                    (otaStep === 'app' && (state === 'pending_reboot' || state === 'completed') && !wwwOtaEnabled)) {
                    clearInterval(refreshInterval);
                    refreshInterval = null;
                    otaStep = 'idle';
                    
                    // 显示重启倒计时
                    stateEl.textContent = '全部升级完成';
                    document.getElementById('ota-message').innerHTML = `
                        <div style="text-align:center">
                            <p>固件和 WebUI 升级完成，设备正在重启...</p>
                            <p id="reboot-countdown" style="color:#888;margin-top:5px">正在触发重启...</p>
                        </div>
                    `;
                    
                    // 触发设备重启
                    try {
                        await api.call('system.reboot', { delay: 1 });
                    } catch (e) {
                        console.log('Reboot triggered (connection may have closed)');
                    }
                    
                    // 开始检测设备重启
                    startRebootDetection();
                } else if (state === 'error') {
                    showToast('升级失败: ' + message, 'error');
                    clearInterval(refreshInterval);
                    refreshInterval = null;
                    otaStep = 'idle';
                }
            } else {
                // 如果 app OTA 是 idle 但我们在 www 步骤，检查 www 进度
                if (otaStep !== 'www') {
                    progressSection.style.display = 'none';
                }
            }
        }
    } catch (error) {
        console.error('Failed to get OTA status:', error);
    }
}

// 启动 WWW OTA（第二步）
async function startWwwOta() {
    try {
        let wwwSource = '';
        let isFromSdcard = false;
        
        // 判断来源：SD卡 或 HTTP URL
        if (sdcardOtaSource) {
            // SD卡升级：推导 www.bin 路径
            isFromSdcard = true;
            if (sdcardOtaSource.match(/\.bin$/i)) {
                wwwSource = sdcardOtaSource.replace(/[^\/]+\.bin$/i, 'www.bin');
            } else {
                wwwSource = sdcardOtaSource.replace(/\/?$/, '/www.bin');
            }
        } else {
            // HTTP 升级：从服务器 URL 推导
            const serverUrl = document.getElementById('ota-server-input').value.trim() ||
                              document.getElementById('ota-url-input').value.trim();
            
            if (serverUrl) {
                // 尝试多种方式推导 www.bin URL
                if (serverUrl.includes('firmware.bin') || serverUrl.includes('TianshanOS.bin')) {
                    wwwSource = serverUrl.replace(/firmware\.bin|TianshanOS\.bin/gi, 'www.bin');
                } else if (serverUrl.match(/\.bin$/i)) {
                    wwwSource = serverUrl.replace(/[^\/]+\.bin$/i, 'www.bin');
                } else if (serverUrl.endsWith('/')) {
                    wwwSource = serverUrl + 'www.bin';
                } else {
                    wwwSource = serverUrl + '/www.bin';
                }
            }
        }
        
        if (!wwwSource) {
            console.log('No www source configured, skipping WebUI upgrade');
            wwwOtaEnabled = false;
            sdcardOtaSource = '';  // 重置
            return;
        }
        
        otaStep = 'www';
        
        document.getElementById('ota-state-text').textContent = '[2/2] 开始升级 WebUI...';
        document.getElementById('ota-progress-bar').style.width = '0%';
        document.getElementById('ota-progress-percent').textContent = '0%';
        document.getElementById('ota-message').textContent = wwwSource;
        
        let result;
        if (isFromSdcard) {
            // SD卡方式
            result = await api.call('ota.www.start_sdcard', {
                file: wwwSource
            });
        } else {
            // HTTP 方式
            const skipVerify = document.getElementById('ota-url-skip-verify')?.checked || false;
            result = await api.call('ota.www.start', {
                url: wwwSource,
                skip_verify: skipVerify
            });
        }
        
        sdcardOtaSource = '';  // 重置
        
        if (result.code !== 0) {
            showToast('WebUI 升级启动失败: ' + result.message, 'error');
            // 即使 www 失败也继续重启（因为 app 已经更新）
            otaStep = 'idle';
            clearInterval(refreshInterval);
            refreshInterval = null;
            
            document.getElementById('ota-state-text').textContent = '固件升级完成（WebUI 跳过）';
            document.getElementById('ota-message').innerHTML = `
                <div style="text-align:center">
                    <p>固件已更新，WebUI 升级跳过，设备正在重启...</p>
                    <p id="reboot-countdown" style="color:#888;margin-top:5px">正在触发重启...</p>
                </div>
            `;
            
            // 触发设备重启
            try {
                await api.call('system.reboot', { delay: 1 });
            } catch (e) {
                console.log('Reboot triggered (connection may have closed)');
            }
            
            startRebootDetection();
        }
    } catch (error) {
        console.error('Failed to start WWW OTA:', error);
        otaStep = 'idle';
        sdcardOtaSource = '';  // 重置
    }
}

// 检测设备重启完成
let rebootCheckInterval = null;
let rebootStartTime = null;

function startRebootDetection() {
    rebootStartTime = Date.now();
    let checkCount = 0;
    
    // 每 2 秒检测一次设备是否恢复
    rebootCheckInterval = setInterval(async () => {
        checkCount++;
        const elapsed = Math.floor((Date.now() - rebootStartTime) / 1000);
        const countdownEl = document.getElementById('reboot-countdown');
        
        if (countdownEl) {
            countdownEl.textContent = `已等待 ${elapsed} 秒...`;
        }
        
        try {
            // 尝试连接设备
            const result = await api.call('ota.version');
            if (result.code === 0) {
                // 设备恢复了！
                clearInterval(rebootCheckInterval);
                rebootCheckInterval = null;
                
                const newVersion = result.data?.version || '未知';
                
                if (countdownEl) {
                    countdownEl.innerHTML = `
                        <span style="color:#27ae60">✅ 设备已恢复！</span>
                        <br><span style="font-size:0.9em">当前版本: ${newVersion}</span>
                    `;
                }
                
                showToast(`OTA 升级成功！当前版本: ${newVersion}`, 'success');
                
                // 3 秒后刷新页面
                setTimeout(() => {
                    window.location.reload();
                }, 3000);
            }
        } catch (e) {
            // 设备还在重启，继续等待
            if (checkCount > 60) {
                // 超过 2 分钟，提示用户手动检查
                clearInterval(rebootCheckInterval);
                rebootCheckInterval = null;
                
                if (countdownEl) {
                    countdownEl.innerHTML = `
                        <span style="color:#e74c3c">⚠️ 等待超时</span>
                        <br><span style="font-size:0.9em">请手动检查设备状态并刷新页面</span>
                        <br><button class="btn btn-primary btn-small" onclick="window.location.reload()" 
                            style="margin-top:10px">刷新页面</button>
                    `;
                }
            }
        }
    }, 2000);
}

async function otaFromUrl() {
    const url = document.getElementById('ota-url-input').value.trim();
    if (!url) {
        showToast('请输入固件 URL', 'error');
        return;
    }
    
    // 允许 http 和 https
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
        showToast('URL 必须以 http:// 或 https:// 开头', 'error');
        return;
    }
    
    const skipVerify = document.getElementById('ota-url-skip-verify').checked;
    const includeWww = document.getElementById('ota-url-include-www').checked;
    
    const params = {
        url: url,
        no_reboot: true,  // 不自动重启，由前端控制流程
        skip_verify: skipVerify
    };
    
    // 设置 OTA 步骤
    otaStep = 'app';
    wwwOtaEnabled = includeWww;  // 根据用户选择决定是否升级 www
    
    // 立即显示进度区域，提供即时反馈
    const progressSection = document.getElementById('ota-progress-section');
    progressSection.style.display = 'block';
    document.getElementById('ota-state-text').textContent = '[1/2] 正在连接服务器...';
    document.getElementById('ota-progress-bar').style.width = '0%';
    document.getElementById('ota-progress-percent').textContent = '0%';
    document.getElementById('ota-progress-size').textContent = '准备中...';
    document.getElementById('ota-message').textContent = url;
    document.getElementById('ota-abort-btn').style.display = 'inline-block';
    
    try {
        showToast('开始两步升级：固件 + WebUI', 'info');
        const result = await api.call('ota.upgrade_url', params);
        
        if (result.code === 0) {
            showToast('固件升级已启动', 'success');
            document.getElementById('ota-state-text').textContent = '下载中...';
            // 开始刷新进度
            if (!refreshInterval) {
                refreshInterval = setInterval(refreshOtaProgress, 1000);
            }
            // 立即刷新一次
            await refreshOtaProgress();
        } else {
            showToast('启动升级失败: ' + result.message, 'error');
            // 显示错误状态
            document.getElementById('ota-state-text').textContent = '错误';
            document.getElementById('ota-message').textContent = result.message || '启动失败';
            document.getElementById('ota-abort-btn').style.display = 'none';
        }
    } catch (error) {
        showToast('启动升级失败: ' + error.message, 'error');
        document.getElementById('ota-state-text').textContent = '错误';
        document.getElementById('ota-message').textContent = error.message || '网络错误';
        document.getElementById('ota-abort-btn').style.display = 'none';
    }
}

async function otaFromFile() {
    const filepath = document.getElementById('ota-file-input').value.trim();
    if (!filepath) {
        showToast('请输入文件路径', 'error');
        return;
    }
    
    const includeWww = document.getElementById('ota-file-include-www').checked;
    
    const params = {
        file: filepath,
        no_reboot: true  // 不自动重启，由前端控制流程
    };
    
    // 设置 OTA 步骤
    otaStep = 'app';
    wwwOtaEnabled = includeWww;  // 根据用户选择决定是否升级 www
    sdcardOtaSource = filepath;  // 保存 SD 卡路径用于推导 www.bin 路径
    
    // 立即显示进度区域
    const progressSection = document.getElementById('ota-progress-section');
    progressSection.style.display = 'block';
    const stepText = includeWww ? '[1/2] ' : '';
    document.getElementById('ota-state-text').textContent = stepText + '正在读取文件...';
    document.getElementById('ota-progress-bar').style.width = '0%';
    document.getElementById('ota-progress-percent').textContent = '0%';
    document.getElementById('ota-progress-size').textContent = '准备中...';
    document.getElementById('ota-message').textContent = filepath;
    document.getElementById('ota-abort-btn').style.display = 'inline-block';
    
    try {
        showToast('开始从文件升级固件...', 'info');
        const result = await api.call('ota.upgrade_file', params);
        
        if (result.code === 0) {
            showToast('固件升级已启动', 'success');
            document.getElementById('ota-state-text').textContent = '写入中...';
            // 开始刷新进度
            if (!refreshInterval) {
                refreshInterval = setInterval(refreshOtaProgress, 1000);
            }
            await refreshOtaProgress();
        } else {
            showToast('启动升级失败: ' + result.message, 'error');
            document.getElementById('ota-state-text').textContent = '错误';
            document.getElementById('ota-message').textContent = result.message || '启动失败';
            document.getElementById('ota-abort-btn').style.display = 'none';
        }
    } catch (error) {
        showToast('启动升级失败: ' + error.message, 'error');
        document.getElementById('ota-state-text').textContent = '错误';
        document.getElementById('ota-message').textContent = error.message || '网络错误';
        document.getElementById('ota-abort-btn').style.display = 'none';
    }
}

async function validateOta() {
    if (!confirm('确认将当前固件标记为有效？\n这将取消自动回滚保护。')) {
        return;
    }
    
    try {
        const result = await api.call('ota.validate');
        
        if (result.code === 0) {
            showToast('固件已标记为有效', 'success');
            await refreshOtaInfo();
        } else {
            showToast('操作失败: ' + result.message, 'error');
        }
    } catch (error) {
        showToast('操作失败: ' + error.message, 'error');
    }
}

function confirmRollback() {
    if (!confirm('⚠️ 确认回滚到上一版本固件？\n\n系统将立即重启并加载上一个分区的固件。\n请确保上一版本固件可用！')) {
        return;
    }
    
    rollbackOta();
}

async function rollbackOta() {
    try {
        showToast('正在回滚固件...', 'info');
        const result = await api.call('ota.rollback');
        
        if (result.code === 0) {
            showToast('回滚成功！系统将在 3 秒后重启...', 'success');
            // 3秒后页面会因为重启而断开连接
        } else {
            showToast('回滚失败: ' + result.message, 'error');
        }
    } catch (error) {
        showToast('回滚失败: ' + error.message, 'error');
    }
}

async function abortOta() {
    if (!confirm('确认中止当前升级？')) {
        return;
    }
    
    try {
        // 根据当前步骤中止相应的 OTA
        let result;
        if (otaStep === 'www') {
            result = await api.call('ota.www.abort');
        } else {
            result = await api.call('ota.abort');
        }
        
        if (result.code === 0) {
            showToast('升级已中止', 'info');
            otaStep = 'idle';
            await refreshOtaInfo();
            clearInterval(refreshInterval);
            refreshInterval = null;
        } else {
            showToast('中止失败: ' + result.message, 'error');
        }
    } catch (error) {
        showToast('中止失败: ' + error.message, 'error');
    }
}

function formatSize(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return (bytes / Math.pow(k, i)).toFixed(2) + ' ' + sizes[i];
}

// ============================================================================
// 语义化版本工具函数
// ============================================================================

/**
 * 解析语义化版本号
 * @param {string} version - 版本字符串 (如 "1.2.3-rc1+build123")
 * @returns {object} - { major, minor, patch, prerelease, build }
 */
function parseVersion(version) {
    const result = { major: 0, minor: 0, patch: 0, prerelease: '', build: '' };
    if (!version) return result;
    
    // 移除前缀 v/V
    let v = version.trim();
    if (v.startsWith('v') || v.startsWith('V')) {
        v = v.substring(1);
    }
    
    // 分离构建元数据 (+xxx)
    const buildIdx = v.indexOf('+');
    if (buildIdx !== -1) {
        result.build = v.substring(buildIdx + 1);
        v = v.substring(0, buildIdx);
    }
    
    // 分离预发布标识 (-xxx)
    const preIdx = v.indexOf('-');
    if (preIdx !== -1) {
        result.prerelease = v.substring(preIdx + 1);
        v = v.substring(0, preIdx);
    }
    
    // 解析核心版本号
    const parts = v.split('.');
    result.major = parseInt(parts[0]) || 0;
    result.minor = parseInt(parts[1]) || 0;
    result.patch = parseInt(parts[2]) || 0;
    
    return result;
}

/**
 * 比较两个语义化版本
 * @param {string} v1 - 第一个版本
 * @param {string} v2 - 第二个版本
 * @returns {number} - -1 (v1 < v2), 0 (v1 == v2), 1 (v1 > v2)
 */
function compareSemVer(v1, v2) {
    const a = parseVersion(v1);
    const b = parseVersion(v2);
    
    // 比较主版本号
    if (a.major !== b.major) return a.major > b.major ? 1 : -1;
    
    // 比较次版本号
    if (a.minor !== b.minor) return a.minor > b.minor ? 1 : -1;
    
    // 比较修订号
    if (a.patch !== b.patch) return a.patch > b.patch ? 1 : -1;
    
    // 比较预发布标识
    // 有预发布 < 无预发布 (1.0.0-rc1 < 1.0.0)
    if (a.prerelease && !b.prerelease) return -1;
    if (!a.prerelease && b.prerelease) return 1;
    if (a.prerelease && b.prerelease) {
        return a.prerelease.localeCompare(b.prerelease);
    }
    
    return 0;
}

/**
 * 格式化版本显示
 * @param {object} versionInfo - 版本信息对象
 * @returns {string} - 格式化的版本字符串
 */
function formatVersionDisplay(versionInfo) {
    if (!versionInfo) return 'Unknown';
    const v = versionInfo.version || '0.0.0';
    const date = versionInfo.compile_date || '';
    const time = versionInfo.compile_time || '';
    return `${v} (${date} ${time})`.trim();
}

// OTA 服务器相关函数
async function saveOtaServer() {
    const serverUrl = document.getElementById('ota-server-input').value.trim();
    
    try {
        const result = await api.call('ota.server.set', {
            url: serverUrl,
            save: true  // 保存到 NVS
        });
        
        if (result.code === 0) {
            if (serverUrl) {
                showToast('✅ OTA 服务器地址已保存', 'success');
            } else {
                showToast('OTA 服务器地址已清除', 'info');
            }
        } else {
            showToast('保存失败: ' + result.message, 'error');
        }
    } catch (error) {
        showToast('保存失败: ' + error.message, 'error');
    }
}

// 当前固件版本缓存
let currentFirmwareVersion = null;

async function checkForUpdates() {
    const serverUrl = document.getElementById('ota-server-input').value.trim();
    if (!serverUrl) {
        showToast('请先输入 OTA 服务器地址', 'error');
        return;
    }
    
    const statusDiv = document.getElementById('ota-update-status');
    statusDiv.style.display = 'block';
    statusDiv.className = 'ota-update-status';
    statusDiv.innerHTML = '<p>🔍 正在检查更新...</p>';
    
    try {
        // 尝试通过设备测试连接（如果 API 存在）
        try {
            console.log('Testing device connectivity to:', serverUrl);
            const testResult = await api.call('ota.test_connection', { url: serverUrl }, 'POST');
            if (testResult && testResult.data) {
                const testData = testResult.data;
                console.log('Device connection test result:', testData);
                
                if (!testData.dns_ok) {
                    throw new Error(`设备 DNS 解析失败: ${testData.host}`);
                }
                if (!testData.tcp_ok) {
                    throw new Error(`设备 TCP 连接失败: ${testData.resolved_ip}:${testData.port}`);
                }
                if (!testData.http_ok) {
                    throw new Error(`设备 HTTP 请求失败: ${testData.http_error || '无响应'}`);
                }
                console.log(`Device connectivity OK: DNS=${testData.dns_time_ms}ms, TCP=${testData.tcp_time_ms}ms, HTTP=${testData.http_time_ms}ms`);
            }
        } catch (testError) {
            // 连接测试 API 可能不存在（旧固件），跳过测试继续检查版本
            console.warn('Device connection test skipped:', testError.message);
        }
        
        // 获取服务器版本信息（从浏览器获取）
        const versionUrl = serverUrl.replace(/\/$/, '') + '/version';
        console.log('Checking for updates:', versionUrl);
        
        const response = await fetch(versionUrl);
        if (!response.ok) {
            throw new Error(`服务器响应错误: ${response.status}`);
        }
        
        const serverInfo = await response.json();
        console.log('Server version info:', serverInfo);
        
        // 获取当前版本
        if (!currentFirmwareVersion) {
            const localResult = await api.call('ota.version');
            if (localResult && localResult.code === 0 && localResult.data) {
                currentFirmwareVersion = localResult.data;
            }
        }
        
        // 比较版本
        const localVersion = currentFirmwareVersion?.version || '0.0.0';
        const serverVersion = serverInfo.version || '0.0.0';
        const serverCompileDate = serverInfo.compile_date || '';
        const serverCompileTime = serverInfo.compile_time || '';
        const serverSize = serverInfo.size || 0;
        
        // 语义化版本比较
        const versionComparison = compareSemVer(serverVersion, localVersion);
        const hasUpdate = versionComparison > 0 || 
                         (versionComparison === 0 && (
                             serverCompileDate !== currentFirmwareVersion?.compile_date ||
                             serverCompileTime !== currentFirmwareVersion?.compile_time
                         ));
        
        // 版本变更类型说明
        let updateType = '';
        if (versionComparison > 0) {
            const localParts = parseVersion(localVersion);
            const serverParts = parseVersion(serverVersion);
            if (serverParts.major > localParts.major) {
                updateType = '<span style="color:#e74c3c;font-weight:bold">🔴 主版本更新</span>';
            } else if (serverParts.minor > localParts.minor) {
                updateType = '<span style="color:#f39c12;font-weight:bold">🟡 功能更新</span>';
            } else {
                updateType = '<span style="color:#27ae60;font-weight:bold">🟢 补丁更新</span>';
            }
        }
        
        if (hasUpdate) {
            statusDiv.className = 'ota-update-status has-update';
            statusDiv.innerHTML = `
                <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px">
                    <div>
                        <span style="font-weight:600">发现新版本</span>
                        ${updateType ? ` · ${updateType}` : ''}
                        <div style="margin-top:5px;font-size:0.9em;color:#666">
                            <code>${localVersion}</code> → <code style="color:#27ae60;font-weight:bold">${serverVersion}</code>
                            <span style="margin-left:10px">${formatSize(serverSize)}</span>
                        </div>
                    </div>
                    <button class="btn btn-success btn-small" onclick="upgradeFromServer()">
                        立即升级
                    </button>
                </div>
            `;
        } else if (versionComparison < 0) {
            statusDiv.className = 'ota-update-status downgrade';
            statusDiv.innerHTML = `
                <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px">
                    <div>
                        <span style="font-weight:600">⚠️ 服务器版本较旧</span>
                        <div style="margin-top:5px;font-size:0.9em;color:#666">
                            <code>${localVersion}</code> → <code style="color:#ff9800">${serverVersion}</code>
                        </div>
                    </div>
                    <button class="btn btn-warning btn-small" onclick="upgradeFromServer()">
                        降级
                    </button>
                </div>
            `;
        } else {
            statusDiv.className = 'ota-update-status no-update';
            statusDiv.innerHTML = `
                <div style="display:flex;align-items:center;gap:10px">
                    <span style="font-weight:600">已是最新版本</span>
                    <code style="color:#2196f3">${localVersion}</code>
                </div>
            `;
        }
        
    } catch (error) {
        console.error('Check for updates failed:', error);
        statusDiv.className = 'ota-update-status error';
        statusDiv.innerHTML = `
            <div>
                <span style="font-weight:600">❌ 检查更新失败</span>
                <div style="margin-top:5px;font-size:0.9em;color:#666">${error.message}</div>
            </div>
        `;
    }
}

async function upgradeFromServer() {
    const serverUrl = document.getElementById('ota-server-input').value.trim();
    if (!serverUrl) {
        showToast('OTA 服务器地址未设置', 'error');
        return;
    }
    
    // 立即显示进度区域，给用户即时反馈
    const progressSection = document.getElementById('ota-progress-section');
    progressSection.style.display = 'block';
    document.getElementById('ota-state-text').textContent = '⏳ 准备升级...';
    document.getElementById('ota-progress-bar').style.width = '0%';
    document.getElementById('ota-progress-percent').textContent = '';
    document.getElementById('ota-progress-size').textContent = '正在初始化...';
    document.getElementById('ota-message').textContent = serverUrl;
    document.getElementById('ota-abort-btn').style.display = 'none';
    
    // 隐藏更新状态区域
    const statusDiv = document.getElementById('ota-update-status');
    if (statusDiv) statusDiv.style.display = 'none';
    
    // 使用浏览器代理模式：浏览器下载固件后转发给 ESP32
    await upgradeViaProxy(serverUrl);
}

/**
 * 浏览器代理升级模式
 * 浏览器从 OTA 服务器下载固件，然后转发给 ESP32
 * 优势：ESP32 无需上网，只需要浏览器能访问 OTA 服务器
 */
async function upgradeViaProxy(serverUrl) {
    const includeWww = document.getElementById('ota-url-include-www')?.checked ?? true;
    
    // 获取进度区域元素（已在 upgradeFromServer 中显示）
    const progressSection = document.getElementById('ota-progress-section');
    const stateEl = document.getElementById('ota-state-text');
    const progressBar = document.getElementById('ota-progress-bar');
    const progressPercent = document.getElementById('ota-progress-percent');
    const progressSize = document.getElementById('ota-progress-size');
    const messageEl = document.getElementById('ota-message');
    const abortBtn = document.getElementById('ota-abort-btn');
    
    // 设置 OTA 步骤
    otaStep = 'app';
    wwwOtaEnabled = includeWww;
    
    // 计算总步骤数
    const totalSteps = includeWww ? 4 : 2;  // 下载固件、上传固件、[下载WebUI、上传WebUI]
    let currentStep = 0;
    
    const updateStep = (step, desc) => {
        currentStep = step;
        const prefix = `[${step}/${totalSteps}] `;
        stateEl.textContent = prefix + desc;
    };
    
    try {
        // ===== 第一步：浏览器下载固件 =====
        updateStep(1, '📥 下载固件中...');
        const firmwareUrl = serverUrl.replace(/\/$/, '') + '/firmware';
        messageEl.textContent = '从 OTA 服务器下载';
        progressBar.style.width = '0%';
        progressPercent.textContent = '0%';
        progressSize.textContent = '正在连接服务器...';
        abortBtn.style.display = 'none';  // 浏览器下载阶段暂不支持中止
        
        console.log('Proxy OTA: Downloading firmware from', firmwareUrl);
        
        // 使用 fetch 下载固件（带进度）
        const firmwareData = await downloadWithProgress(firmwareUrl, (loaded, total) => {
            const percent = total > 0 ? Math.round((loaded / total) * 100) : 0;
            progressBar.style.width = percent + '%';
            progressPercent.textContent = percent + '%';
            progressSize.textContent = `${formatSize(loaded)} / ${formatSize(total)}`;
        });
        
        console.log('Proxy OTA: Firmware downloaded,', firmwareData.byteLength, 'bytes');
        showToast(`固件下载完成 (${formatSize(firmwareData.byteLength)})`, 'success');
        
        // ===== 第二步：上传固件到 ESP32 =====
        updateStep(2, '📤 上传固件到设备...');
        messageEl.textContent = `固件大小: ${formatSize(firmwareData.byteLength)}`;
        progressBar.style.width = '0%';
        progressPercent.textContent = '';
        progressSize.textContent = '正在写入 Flash（这可能需要1-2分钟）...';
        
        // 调用 ESP32 上传接口（复用现有的 /api/v1/ota/firmware）
        // 注意：不自动重启，等 www 也完成后再重启
        const uploadResult = await uploadFirmwareToDevice(firmwareData, !includeWww);
        
        if (!uploadResult.success) {
            throw new Error(uploadResult.error || '上传固件失败');
        }
        
        console.log('Proxy OTA: Firmware uploaded to device');
        showToast('固件写入完成！', 'success');
        progressBar.style.width = '100%';
        progressPercent.textContent = '✓';
        
        // ===== 第三步：处理 WebUI（如果启用）=====
        if (includeWww) {
            updateStep(3, '📥 下载 WebUI...');
            const wwwUrl = serverUrl.replace(/\/$/, '') + '/www';
            messageEl.textContent = '从 OTA 服务器下载';
            progressBar.style.width = '0%';
            progressPercent.textContent = '0%';
            progressSize.textContent = '正在连接...';
            
            try {
                // 下载 www.bin
                const wwwData = await downloadWithProgress(wwwUrl, (loaded, total) => {
                    const percent = total > 0 ? Math.round((loaded / total) * 100) : 0;
                    progressBar.style.width = percent + '%';
                    progressPercent.textContent = percent + '%';
                    progressSize.textContent = `${formatSize(loaded)} / ${formatSize(total)}`;
                });
                
                console.log('Proxy OTA: WWW downloaded,', wwwData.byteLength, 'bytes');
                showToast(`WebUI 下载完成 (${formatSize(wwwData.byteLength)})`, 'success');
                
                // 上传 www.bin
                updateStep(4, '📤 上传 WebUI 到设备...');
                messageEl.textContent = `WebUI 大小: ${formatSize(wwwData.byteLength)}`;
                progressBar.style.width = '0%';
                progressPercent.textContent = '';
                progressSize.textContent = '正在写入 SPIFFS...';
                
                const wwwResult = await uploadWwwToDevice(wwwData);
                
                if (!wwwResult.success) {
                    console.warn('WWW upload failed:', wwwResult.error);
                    showToast('WebUI 升级跳过: ' + wwwResult.error, 'warning');
                } else {
                    console.log('Proxy OTA: WWW uploaded to device');
                    showToast('WebUI 写入完成！', 'success');
                    progressBar.style.width = '100%';
                    progressPercent.textContent = '✓';
                }
            } catch (wwwError) {
                console.warn('WWW download/upload failed:', wwwError);
                showToast('WebUI 升级跳过: ' + wwwError.message, 'warning');
            }
        }
        
        // ===== 最终步骤：升级完成，触发重启 =====
        stateEl.textContent = '✅ 全部升级完成！';
        progressBar.style.width = '100%';
        progressBar.style.background = 'linear-gradient(90deg, #27ae60, #2ecc71)';
        progressPercent.textContent = '✓';
        messageEl.innerHTML = `
            <div style="text-align:center">
                <p>固件${includeWww ? '和 WebUI ' : ''}升级完成，设备正在重启...</p>
                <p id="reboot-countdown" style="color:#888;margin-top:5px">正在触发重启...</p>
            </div>
        `;
        
        otaStep = 'idle';
        
        // 触发设备重启
        try {
            await api.call('system.reboot', { delay: 1 });
        } catch (e) {
            console.log('Reboot triggered (connection may have closed)');
        }
        
        // 检测设备重启
        startRebootDetection();
        
    } catch (error) {
        console.error('Proxy OTA failed:', error);
        stateEl.textContent = '❌ 升级失败';
        messageEl.textContent = error.message;
        progressBar.style.width = '0%';
        progressPercent.textContent = '';
        otaStep = 'idle';
        showToast('升级失败: ' + error.message, 'error');
    }
}

/**
 * 带进度的文件下载
 */
async function downloadWithProgress(url, onProgress) {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`下载失败: HTTP ${response.status}`);
    }
    
    const contentLength = response.headers.get('content-length');
    const total = contentLength ? parseInt(contentLength, 10) : 0;
    
    const reader = response.body.getReader();
    const chunks = [];
    let loaded = 0;
    
    while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        
        chunks.push(value);
        loaded += value.length;
        
        if (onProgress) {
            onProgress(loaded, total);
        }
    }
    
    // 合并所有块
    const result = new Uint8Array(loaded);
    let position = 0;
    for (const chunk of chunks) {
        result.set(chunk, position);
        position += chunk.length;
    }
    
    return result.buffer;
}

/**
 * 上传固件到设备
 */
async function uploadFirmwareToDevice(firmwareData, autoReboot = false) {
    try {
        const url = getApiUrl(`/ota/firmware?auto_reboot=${autoReboot}`);
        
        const response = await fetch(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/octet-stream'
            },
            body: firmwareData
        });
        
        const result = await response.json();
        
        if (response.ok && result.status === 'success') {
            return { success: true, data: result };
        } else {
            return { success: false, error: result.message || result.error || '上传失败' };
        }
    } catch (error) {
        return { success: false, error: error.message };
    }
}

/**
 * 上传 WebUI 到设备
 */
async function uploadWwwToDevice(wwwData) {
    try {
        const url = getApiUrl('/ota/www');
        
        const response = await fetch(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/octet-stream'
            },
            body: wwwData
        });
        
        const result = await response.json();
        
        if (response.ok && result.status === 'success') {
            return { success: true, data: result };
        } else {
            return { success: false, error: result.message || result.error || '上传失败' };
        }
    } catch (error) {
        return { success: false, error: error.message };
    }
}

// 导出全局函数
window.loadOtaPage = loadOtaPage;
window.otaFromUrl = otaFromUrl;
window.otaFromFile = otaFromFile;
window.validateOta = validateOta;
window.confirmRollback = confirmRollback;
window.rollbackOta = rollbackOta;
window.abortOta = abortOta;
window.saveOtaServer = saveOtaServer;
window.checkForUpdates = checkForUpdates;
window.upgradeFromServer = upgradeFromServer;
window.upgradeViaProxy = upgradeViaProxy;

// =========================================================================
//                         日志页面
// =========================================================================

let logRefreshInterval = null;
let logAutoScroll = true;
let logLastTimestamp = 0;
let logWsConnected = false;
// =========================================================================
// 日志页面已废弃 - 功能已整合到终端页面的日志模态框

// =========================================================================
//                         内存详情模态框
// =========================================================================

// 任务排序状态
let taskSortState = { key: 'stack_hwm', ascending: true };  // 默认按剩余栈升序（最危险的在前）
let cachedTasksData = [];  // 缓存任务数据用于排序

/**
 * 渲染任务行 HTML
 */
function renderTaskRows(tasks, formatBytes) {
    return tasks.map(task => {
        const hwm = task.stack_hwm || 0;
        const alloc = task.stack_alloc || 0;
        const used = task.stack_used || 0;
        const usagePct = task.stack_usage_pct || 0;
        const hwmColor = hwm < 256 ? '#e74c3c' : hwm < 512 ? '#f39c12' : '#2ecc71';
        const usageColor = usagePct >= 90 ? '#e74c3c' : usagePct >= 75 ? '#f39c12' : '#2ecc71';
        const stateIcon = {
            'Running': '<i class="ri-checkbox-blank-circle-fill" style="color:#22c55e"></i>',
            'Ready': '<i class="ri-checkbox-blank-circle-fill" style="color:#3b82f6"></i>', 
            'Blocked': '<i class="ri-checkbox-blank-circle-fill" style="color:#eab308"></i>',
            'Suspended': '<i class="ri-checkbox-blank-circle-fill" style="color:#9ca3af"></i>',
            'Deleted': '<i class="ri-checkbox-blank-circle-fill" style="color:#ef4444"></i>'
        }[task.state] || '<i class="ri-checkbox-blank-circle-fill" style="color:#374151"></i>';
        return `
        <tr>
            <td><code>${task.name}</code></td>
            <td>${alloc ? formatBytes(alloc) : '-'}</td>
            <td>${used ? formatBytes(used) : '-'}</td>
            <td style="color:${hwmColor};font-weight:bold">${formatBytes(hwm)}</td>
            <td><span style="color:${usageColor}">${usagePct}%</span></td>
            <td>${task.priority}</td>
            <td>${stateIcon} ${task.state}</td>
            ${task.cpu_percent !== undefined ? `<td>${task.cpu_percent}%</td>` : ''}
        </tr>
        `;
    }).join('');
}

/**
 * 对任务列表排序
 */
function sortTasks(tasks, key, ascending) {
    const stateOrder = { 'Running': 0, 'Ready': 1, 'Blocked': 2, 'Suspended': 3, 'Deleted': 4 };
    
    return [...tasks].sort((a, b) => {
        let valA, valB;
        
        if (key === 'state') {
            valA = stateOrder[a.state] ?? 5;
            valB = stateOrder[b.state] ?? 5;
        } else {
            valA = a[key] || 0;
            valB = b[key] || 0;
        }
        
        if (ascending) {
            return valA - valB;
        } else {
            return valB - valA;
        }
    });
}

/**
 * 初始化任务表格排序
 */
function initTaskTableSort() {
    const table = document.getElementById('task-memory-table');
    if (!table) return;
    
    const headers = table.querySelectorAll('th.sortable');
    headers.forEach(th => {
        th.style.cursor = 'pointer';
        th.addEventListener('click', () => {
            const key = th.dataset.sort;
            
            // 切换排序方向
            if (taskSortState.key === key) {
                taskSortState.ascending = !taskSortState.ascending;
            } else {
                taskSortState.key = key;
                taskSortState.ascending = true;
            }
            
            // 更新表头指示器
            headers.forEach(h => {
                const baseText = h.textContent.replace(/ [↑↓⇅]$/, '');
                if (h.dataset.sort === key) {
                    h.textContent = baseText + (taskSortState.ascending ? ' ↑' : ' ↓');
                } else {
                    h.textContent = baseText + ' ⇅';
                }
            });
            
            // 重新排序并渲染
            const sortedTasks = sortTasks(cachedTasksData, key, taskSortState.ascending);
            const tbody = document.getElementById('task-table-body');
            if (tbody) {
                // 复用已定义的 formatBytes
                const formatBytes = (bytes) => {
                    if (bytes >= 1024 * 1024) return (bytes / 1024 / 1024).toFixed(2) + ' MB';
                    if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
                    return bytes + ' B';
                };
                tbody.innerHTML = renderTaskRows(sortedTasks, formatBytes);
            }
        });
    });
}

/**
 * 显示内存详情模态框
 */
async function showMemoryDetailModal() {
    const modal = document.getElementById('memory-detail-modal');
    modal.classList.remove('hidden');
    await refreshMemoryDetail();
}

/**
 * 隐藏内存详情模态框
 */
function hideMemoryDetailModal() {
    const modal = document.getElementById('memory-detail-modal');
    modal.classList.add('hidden');
}

/**
 * 刷新内存详情数据
 */
async function refreshMemoryDetail() {
    const body = document.getElementById('memory-detail-body');
    const timestamp = document.getElementById('memory-detail-timestamp');
    
    body.innerHTML = '<div class="loading">加载中...</div>';
    
    try {
        const result = await api.getMemoryDetail();
        if (result.code !== 0 || !result.data) {
            throw new Error(result.message || '获取数据失败');
        }
        
        const data = result.data;
        const dram = data.dram || {};
        const psram = data.psram || {};
        const dma = data.dma || {};
        const tips = data.tips || [];
        const staticMem = data.static || {};
        const iram = data.iram || {};
        const rtc = data.rtc || {};
        const nvs = data.nvs || {};
        const caps = data.caps || {};
        
        // 格式化字节数
        const formatBytes = (bytes) => {
            if (bytes >= 1024 * 1024) return (bytes / 1024 / 1024).toFixed(2) + ' MB';
            if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
            return bytes + ' B';
        };
        
        // 获取进度条颜色
        const getProgressColor = (percent) => {
            if (percent >= 85) return '#e74c3c';  // 红色 - 危险
            if (percent >= 70) return '#f39c12';  // 橙色 - 警告
            return '#2ecc71';  // 绿色 - 正常
        };
        
        // 获取碎片化颜色
        const getFragColor = (frag) => {
            if (frag >= 60) return '#e74c3c';
            if (frag >= 40) return '#f39c12';
            return '#2ecc71';
        };
        
        // 构建提示信息 HTML
        let tipsHtml = '';
        if (tips.length > 0) {
            tipsHtml = `
                <div class="memory-tips">
                    <h4>优化建议</h4>
                    ${tips.map(tip => {
                        const [level, msg] = tip.split(':');
                        const iconColor = level === 'critical' ? '#ef4444' : level === 'warning' ? '#f59e0b' : '#3b82f6';
                        const icon = `<i class="ri-checkbox-blank-circle-fill" style="color:${iconColor}"></i>`;
                        const bgColor = level === 'critical' ? '#fff5f5' : level === 'warning' ? '#fffbf0' : '#f0f8ff';
                        return `<div class="memory-tip" style="background:${bgColor}">${icon} ${msg}</div>`;
                    }).join('')}
                </div>
            `;
        }
        
        body.innerHTML = `
            <!-- 概览卡片 -->
            <div class="memory-overview">
                <div class="memory-gauge dram">
                    <div class="gauge-ring">
                        <svg viewBox="0 0 100 100">
                            <circle cx="50" cy="50" r="45" fill="none" stroke="#e0e0e0" stroke-width="8"/>
                            <circle cx="50" cy="50" r="45" fill="none" stroke="${getProgressColor(dram.used_percent || 0)}" stroke-width="8"
                                stroke-dasharray="${(dram.used_percent || 0) * 2.83} 283" stroke-linecap="round"
                                transform="rotate(-90 50 50)"/>
                        </svg>
                        <div class="gauge-text">
                            <span class="gauge-percent">${dram.used_percent || 0}%</span>
                            <span class="gauge-label">DRAM</span>
                        </div>
                    </div>
                    <div class="gauge-info">
                        <div class="info-row">
                            <span>总计</span>
                            <strong>${formatBytes(dram.total || 0)}</strong>
                        </div>
                        <div class="info-row">
                            <span>已用</span>
                            <strong style="color:${getProgressColor(dram.used_percent || 0)}">${formatBytes(dram.used || 0)}</strong>
                        </div>
                        <div class="info-row">
                            <span>空闲</span>
                            <strong style="color:#2ecc71">${formatBytes(dram.free || 0)}</strong>
                        </div>
                    </div>
                </div>
                
                ${psram.total ? `
                <div class="memory-gauge psram">
                    <div class="gauge-ring">
                        <svg viewBox="0 0 100 100">
                            <circle cx="50" cy="50" r="45" fill="none" stroke="#e0e0e0" stroke-width="8"/>
                            <circle cx="50" cy="50" r="45" fill="none" stroke="${getProgressColor(psram.used_percent || 0)}" stroke-width="8"
                                stroke-dasharray="${(psram.used_percent || 0) * 2.83} 283" stroke-linecap="round"
                                transform="rotate(-90 50 50)"/>
                        </svg>
                        <div class="gauge-text">
                            <span class="gauge-percent">${psram.used_percent || 0}%</span>
                            <span class="gauge-label">PSRAM</span>
                        </div>
                    </div>
                    <div class="gauge-info">
                        <div class="info-row">
                            <span>总计</span>
                            <strong>${formatBytes(psram.total || 0)}</strong>
                        </div>
                        <div class="info-row">
                            <span>已用</span>
                            <strong>${formatBytes(psram.used || 0)}</strong>
                        </div>
                        <div class="info-row">
                            <span>空闲</span>
                            <strong style="color:#2ecc71">${formatBytes(psram.free || 0)}</strong>
                        </div>
                    </div>
                </div>
                ` : ''}
            </div>
            
            <!-- 静态内存段 (关键优化信息) -->
            <div class="memory-static-sections">
                <h4>静态内存占用 (编译时固定)</h4>
                <div class="static-grid">
                    <div class="static-item">
                        <span class="static-label">.data</span>
                        <span class="static-value">${formatBytes(staticMem.data_size || 0)}</span>
                        <span class="static-desc">初始化全局变量</span>
                    </div>
                    <div class="static-item">
                        <span class="static-label">.bss</span>
                        <span class="static-value">${formatBytes(staticMem.bss_size || 0)}</span>
                        <span class="static-desc">未初始化全局变量</span>
                    </div>
                    <div class="static-item">
                        <span class="static-label">.rodata</span>
                        <span class="static-value">${formatBytes(staticMem.rodata_size || 0)}</span>
                        <span class="static-desc">只读数据 (Flash)</span>
                    </div>
                    <div class="static-item highlight">
                        <span class="static-label">DRAM 静态总计</span>
                        <span class="static-value">${formatBytes(staticMem.total_dram_static || 0)}</span>
                        <span class="static-desc">.data + .bss</span>
                    </div>
                </div>
            </div>
            
            <!-- IRAM 信息 -->
            <div class="memory-iram">
                <h4>IRAM (指令内存)</h4>
                <div class="iram-grid">
                    <div class="iram-item">
                        <span class="iram-label">代码段</span>
                        <span class="iram-value">${formatBytes(iram.text_size || 0)}</span>
                    </div>
                    <div class="iram-item">
                        <span class="iram-label">堆总计</span>
                        <span class="iram-value">${formatBytes(iram.heap_total || 0)}</span>
                    </div>
                    <div class="iram-item">
                        <span class="iram-label">堆空闲</span>
                        <span class="iram-value" style="color:#2ecc71">${formatBytes(iram.heap_free || 0)}</span>
                    </div>
                </div>
            </div>
            
            <!-- RTC 内存 -->
            ${rtc.total_available ? `
            <div class="memory-rtc">
                <h4>RTC 内存 (深度睡眠保持)</h4>
                <div class="rtc-bar">
                    <div class="progress-bar" style="height:12px;background:#f0f0f0">
                        <div class="progress" style="width:${(rtc.total_used / rtc.total_available * 100) || 0}%;background:#9b59b6"></div>
                    </div>
                    <div class="rtc-labels">
                        <span>已用 ${formatBytes(rtc.total_used || 0)}</span>
                        <span>总计 ${formatBytes(rtc.total_available)}</span>
                    </div>
                </div>
            </div>
            ` : ''}
            
            <!-- 详细数据表格 -->
            <div class="memory-details">
                <h4>堆内存详细统计</h4>
                <table class="memory-table">
                    <thead>
                        <tr>
                            <th>类型</th>
                            <th>最大块</th>
                            <th>碎片率</th>
                            <th>分配块</th>
                            <th>空闲块</th>
                            <th>历史最低</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr>
                            <td><strong>DRAM</strong></td>
                            <td>${formatBytes(dram.largest_block || 0)}</td>
                            <td><span style="color:${getFragColor(dram.fragmentation || 0)}">${(dram.fragmentation || 0).toFixed(1)}%</span></td>
                            <td>${dram.alloc_blocks || '-'}</td>
                            <td>${dram.free_blocks || '-'}</td>
                            <td>${formatBytes(dram.min_free_ever || 0)}</td>
                        </tr>
                        ${psram.total ? `
                        <tr>
                            <td><strong>PSRAM</strong></td>
                            <td>${formatBytes(psram.largest_block || 0)}</td>
                            <td><span style="color:${getFragColor(psram.fragmentation || 0)}">${(psram.fragmentation || 0).toFixed(1)}%</span></td>
                            <td>${psram.alloc_blocks || '-'}</td>
                            <td>${psram.free_blocks || '-'}</td>
                            <td>${formatBytes(psram.min_free_ever || 0)}</td>
                        </tr>
                        ` : ''}
                        ${dma.total ? `
                        <tr>
                            <td><strong>DMA</strong></td>
                            <td>${formatBytes(dma.largest_block || 0)}</td>
                            <td>-</td>
                            <td>-</td>
                            <td>-</td>
                            <td>-</td>
                        </tr>
                        ` : ''}
                    </tbody>
                </table>
            </div>
            
            <!-- 内存能力汇总 -->
            <div class="memory-caps">
                <h4>内存能力分布</h4>
                <table class="memory-table">
                    <thead>
                        <tr>
                            <th>能力类型</th>
                            <th>空闲</th>
                            <th>总计</th>
                            <th>说明</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr>
                            <td>8-bit 可访问</td>
                            <td>${formatBytes(caps.d8_free || 0)}</td>
                            <td>${formatBytes(caps.d8_total || 0)}</td>
                            <td>char/byte 数组</td>
                        </tr>
                        <tr>
                            <td>32-bit 可访问</td>
                            <td>${formatBytes(caps.d32_free || 0)}</td>
                            <td>${formatBytes(caps.d32_total || 0)}</td>
                            <td>int/指针</td>
                        </tr>
                        <tr>
                            <td>默认 (malloc)</td>
                            <td>${formatBytes(caps.default_free || 0)}</td>
                            <td>${formatBytes(caps.default_total || 0)}</td>
                            <td>普通 malloc()</td>
                        </tr>
                        ${dma.total ? `
                        <tr>
                            <td>DMA 可用</td>
                            <td>${formatBytes(dma.free || 0)}</td>
                            <td>${formatBytes(dma.total || 0)}</td>
                            <td>DMA 传输缓冲</td>
                        </tr>
                        ` : ''}
                    </tbody>
                </table>
            </div>
            
            <!-- NVS 使用统计 -->
            ${nvs.total_entries ? `
            <div class="memory-nvs">
                <h4>NVS 存储使用</h4>
                <div class="nvs-bar">
                    <div class="progress-bar" style="height:16px;background:#f0f0f0">
                        <div class="progress" style="width:${nvs.used_percent || 0}%;background:${getProgressColor(nvs.used_percent || 0)}"></div>
                    </div>
                    <div class="nvs-stats">
                        <span>已用条目: <strong>${nvs.used_entries}</strong></span>
                        <span>空闲条目: <strong>${nvs.free_entries}</strong></span>
                        <span>命名空间: <strong>${nvs.namespace_count}</strong></span>
                        <span>使用率: <strong>${nvs.used_percent}%</strong></span>
                    </div>
                </div>
            </div>
            ` : ''}
            
            <!-- 优化建议 -->
            ${tipsHtml}
            
            <!-- 任务内存占用 -->
            ${data.tasks && data.tasks.length > 0 ? `
            <div class="memory-tasks">
                <h4>任务栈使用 (共 ${data.tasks.length} 个任务) <span style="font-size:0.8em;color:#888;font-weight:normal">点击表头排序</span></h4>
                <table class="memory-table task-table sortable-table" id="task-memory-table">
                    <thead>
                        <tr>
                            <th>任务名</th>
                            <th data-sort="stack_alloc" class="sortable">分配栈 ⇅</th>
                            <th data-sort="stack_used" class="sortable">已用栈 ⇅</th>
                            <th data-sort="stack_hwm" class="sortable">剩余栈 ⇅</th>
                            <th data-sort="stack_usage_pct" class="sortable">使用率 ⇅</th>
                            <th data-sort="priority" class="sortable">优先级 ⇅</th>
                            <th data-sort="state" class="sortable">状态 ⇅</th>
                            ${data.tasks[0]?.cpu_percent !== undefined ? '<th data-sort="cpu_percent" class="sortable">CPU ⇅</th>' : ''}
                        </tr>
                    </thead>
                    <tbody id="task-table-body">
                        ${renderTaskRows(data.tasks, formatBytes)}
                    </tbody>
                </table>
                ${data.total_stack_allocated ? `
                <p style="font-size:0.85em;color:#666;margin-top:8px">
                    任务栈总分配: <strong>${formatBytes(data.total_stack_allocated)}</strong> | 
                    任务总数: <strong>${data.task_count}</strong>
                </p>
                ` : ''}
                <p style="font-size:0.85em;color:#888;margin-top:4px">
                    剩余栈 &lt;256B 为危险区域，&lt;512B 为警告区域
                </p>
            </div>
            ` : ''}
            
            <!-- 历史记录 -->
            <div class="memory-history">
                <h4>运行时统计</h4>
                <div class="history-stats">
                    <div class="history-item">
                        <span class="history-label">历史最低空闲堆</span>
                        <span class="history-value">${formatBytes(data.history?.min_free_heap_ever || 0)}</span>
                    </div>
                    <div class="history-item">
                        <span class="history-label">当前运行任务数</span>
                        <span class="history-value">${data.task_count || 0}</span>
                    </div>
                </div>
            </div>
        `;
        
        // 缓存任务数据并初始化排序
        if (data.tasks && data.tasks.length > 0) {
            cachedTasksData = data.tasks;
            // 应用默认排序
            const sortedTasks = sortTasks(cachedTasksData, taskSortState.key, taskSortState.ascending);
            const tbody = document.getElementById('task-table-body');
            if (tbody) {
                tbody.innerHTML = renderTaskRows(sortedTasks, formatBytes);
            }
            // 初始化排序事件
            initTaskTableSort();
        }
        
        timestamp.textContent = '更新时间: ' + new Date().toLocaleTimeString();
        
    } catch (error) {
        console.error('Memory detail error:', error);
        body.innerHTML = `
            <div class="error-message">
                <p>❌ 获取内存详情失败</p>
                <p style="font-size:0.9em;color:#666">${error.message}</p>
            </div>
        `;
    }
}

// 点击模态框背景关闭
document.addEventListener('click', (e) => {
    const modal = document.getElementById('memory-detail-modal');
    if (e.target === modal) {
        hideMemoryDetailModal();
    }
});

// 导出全局函数
window.showMemoryDetailModal = showMemoryDetailModal;
window.hideMemoryDetailModal = hideMemoryDetailModal;
window.refreshMemoryDetail = refreshMemoryDetail;
// 旧路由 #/logs 会自动重定向到 #/terminal 并打开日志模态框
// =========================================================================

// =========================================================================
//                         自动化引擎页面
// =========================================================================

/**
 * 加载自动化引擎测试页面
 */
async function loadAutomationPage() {
    // 取消之前的订阅
    if (subscriptionManager) {
        subscriptionManager.unsubscribe('system.dashboard');
    }
    stopServiceStatusRefresh();
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-automation">
            <div class="page-header-row">
                <div class="header-actions">
                    <button class="btn btn-primary" onclick="automationControl('start')">▶️ 启动</button>
                    <button class="btn btn-danger" onclick="automationControl('stop')">⏹️ 停止</button>
                    <button class="btn" onclick="automationControl('pause')">⏸️ 暂停</button>
                    <button class="btn" onclick="automationControl('reload')">🔄 重载</button>
                </div>
            </div>
            
            <!-- 状态卡片 -->
            <div class="status-grid" id="automation-status">
                <div class="status-card loading">加载中...</div>
            </div>
            
            <!-- 数据源列表 -->
            <div class="section">
                <div class="section-header">
                    <h2>📡 数据源</h2>
                    <div class="section-actions">
                        <button class="btn btn-primary btn-sm" onclick="showAddSourceModal()">➕ 添加</button>
                        <button class="btn btn-sm" onclick="showImportSourceModal()" title="导入配置包">📥 导入</button>
                        <button class="btn btn-sm" onclick="refreshSources()">🔄</button>
                    </div>
                </div>
                <div class="card compact">
                    <div id="sources-list" class="card-content">
                        <div class="loading-small">加载中...</div>
                    </div>
                </div>
            </div>
            
            <!-- 规则列表 -->
            <div class="section">
                <div class="section-header">
                    <h2>📋 规则列表</h2>
                    <div class="section-actions">
                        <button class="btn btn-primary btn-sm" onclick="showAddRuleModal()">➕ 添加</button>
                        <button class="btn btn-sm" onclick="showImportRuleModal()" title="导入配置包">📥 导入</button>
                        <button class="btn btn-sm" onclick="refreshRules()">🔄</button>
                    </div>
                </div>
                <div class="card compact">
                    <div id="rules-list" class="card-content">
                        <div class="loading-small">加载中...</div>
                    </div>
                </div>
            </div>
            
            <!-- 动作模板管理 -->
            <div class="section">
                <div class="section-header">
                    <h2>⚡ 动作模板</h2>
                    <div class="section-actions">
                        <button class="btn btn-primary btn-sm" onclick="showAddActionModal()">➕ 添加</button>
                        <button class="btn btn-sm" onclick="showImportActionModal()" title="导入配置包">📥 导入</button>
                        <button class="btn btn-sm" onclick="refreshActions()">🔄</button>
                    </div>
                </div>
                <div class="card compact">
                    <div id="actions-list" class="card-content">
                        <div class="loading-small">加载中...</div>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    // 加载所有数据（忽略单个失败，允许部分加载）
    await Promise.allSettled([
        refreshAutomationStatus(),
        refreshRules(),
        refreshSources(),
        refreshActions()
    ]);
}

/**
 * 刷新自动化引擎状态
 */
async function refreshAutomationStatus() {
    const container = document.getElementById('automation-status');
    if (!container) return;
    
    try {
        const result = await api.call('automation.status');
        if (result.code === 0 && result.data) {
            const d = result.data;
            const stateClass = d.state === 'running' ? 'running' : d.state === 'paused' ? 'paused' : 'stopped';
            const stateText = d.state === 'running' ? '运行中' : d.state === 'paused' ? '已暂停' : '已停止';
            
            // 计算运行时间（API 返回 uptime_ms，转换为秒）
            const uptimeSec = Math.floor((d.uptime_ms || 0) / 1000);
            
            container.innerHTML = `
                <div class="status-card primary">
                    <div class="status-icon state-${stateClass}">●</div>
                    <div class="status-info">
                        <span class="status-value">${stateText}</span>
                        <span class="status-label">引擎状态</span>
                    </div>
                </div>
                <div class="status-card">
                    <div class="status-value">${d.rules_count || 0}</div>
                    <div class="status-label">规则</div>
                </div>
                <div class="status-card">
                    <div class="status-value">${d.variables_count || 0}</div>
                    <div class="status-label">变量</div>
                </div>
                <div class="status-card">
                    <div class="status-value">${d.sources_count || 0}</div>
                    <div class="status-label">数据源</div>
                </div>
                <div class="status-card">
                    <div class="status-value">${d.rule_triggers || 0}</div>
                    <div class="status-label">触发次数</div>
                </div>
                <div class="status-card">
                    <div class="status-value">${formatUptimeSec(uptimeSec)}</div>
                    <div class="status-label">运行时长</div>
                </div>
            `;
        } else {
            container.innerHTML = `<div class="status-card error"><span>⚠️ ${result.message || '获取状态失败'}</span></div>`;
        }
    } catch (error) {
        const isNetworkError = error.message.includes('fetch') || error.message.includes('network');
        container.innerHTML = `<div class="status-card error"><span>${isNetworkError ? '🔌 网络连接失败' : '❌ ' + error.message}</span></div>`;
    }
}

/**
 * 格式化运行时长（秒）
 */
function formatUptimeSec(seconds) {
    if (seconds < 60) return `${seconds}秒`;
    if (seconds < 3600) return `${Math.floor(seconds/60)}分${seconds%60}秒`;
    const h = Math.floor(seconds/3600);
    const m = Math.floor((seconds%3600)/60);
    return `${h}时${m}分`;
}

/**
 * 自动化引擎控制
 */
async function automationControl(action) {
    try {
        const result = await api.call(`automation.${action}`);
        showToast(`${action}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
        if (result.code === 0) {
            await refreshAutomationStatus();
        }
    } catch (error) {
        showToast(`${action} 失败: ${error.message}`, 'error');
    }
}

/**
 * 刷新规则列表
 */
async function refreshRules() {
    const container = document.getElementById('rules-list');
    if (!container) return;
    
    try {
        const result = await api.call('automation.rules.list');
        if (result.code === 0 && result.data && result.data.rules) {
            const rules = result.data.rules;
            if (rules.length === 0) {
                container.innerHTML = '<p style="text-align:center;color:var(--text-light)">暂无规则，点击"添加"创建第一条</p>';
                return;
            }
            
            container.innerHTML = `
                <table class="data-table">
                    <thead>
                        <tr>
                            <th style="width:40px"></th>
                            <th>ID</th>
                            <th>名称</th>
                            <th>状态</th>
                            <th>条件</th>
                            <th>动作</th>
                            <th>触发次数</th>
                            <th>操作</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${rules.map(r => {
                            const iconValue = r.icon || '⚡';
                            const iconHtml = iconValue.startsWith('/sdcard/') 
                                ? `<img src="/api/v1/file/download?path=${encodeURIComponent(iconValue)}" style="width:24px;height:24px;object-fit:contain" onerror="this.textContent='⚡'">`
                                : iconValue;
                            const manualBadge = r.manual_trigger ? '<span class="badge" style="background:#27ae60;font-size:0.7em">手动</span>' : '';
                            
                            return `
                            <tr>
                                <td style="font-size:1.2em;text-align:center">${iconHtml}</td>
                                <td><code>${r.id}</code></td>
                                <td>${r.name || r.id} ${manualBadge}</td>
                                <td><span class="status-badge ${r.enabled ? 'status-running' : 'status-stopped'}">${r.enabled ? '启用' : '禁用'}</span></td>
                                <td>${r.conditions_count || 0}</td>
                                <td>${r.actions_count || 0}</td>
                                <td>${r.trigger_count || 0}</td>
                                <td style="white-space:nowrap">
                                    <button class="btn btn-sm" onclick="toggleRule('${r.id}', ${!r.enabled})" title="${r.enabled ? '禁用' : '启用'}">${r.enabled ? '🔴' : '🟢'}</button>
                                    <button class="btn btn-sm" onclick="triggerRule('${r.id}')" title="手动触发">▶️</button>
                                    <button class="btn btn-sm" onclick="editRule('${r.id}')" title="编辑">✏️</button>
                                    <button class="btn btn-sm" onclick="showExportRuleModal('${r.id}')" title="导出配置包">📤</button>
                                    <button class="btn btn-sm btn-danger" onclick="deleteRule('${r.id}')" title="删除">🗑️</button>
                                </td>
                            </tr>
                        `}).join('')}
                    </tbody>
                </table>
            `;
        } else {
            container.innerHTML = `<p style="text-align:center;color:var(--text-light)">⚠️ ${result.message || '获取规则失败'}</p>`;
        }
    } catch (error) {
        const isNetworkError = error.message.includes('fetch') || error.message.includes('network');
        container.innerHTML = `<p style="text-align:center;color:var(--danger-color)">${isNetworkError ? '🔌 网络连接失败' : '❌ ' + error.message}</p>`;
    }
}

/**
 * 切换规则启用状态
 */
async function toggleRule(id, enable) {
    try {
        const action = enable ? 'automation.rules.enable' : 'automation.rules.disable';
        const result = await api.call(action, { id });
        showToast(`规则 ${id} ${enable ? '启用' : '禁用'}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
        if (result.code === 0) {
            await refreshRules();
        }
    } catch (error) {
        showToast(`切换规则状态失败: ${error.message}`, 'error');
    }
}

/**
 * 手动触发规则
 */
async function triggerRule(id) {
    try {
        const result = await api.call('automation.rules.trigger', { id });
        showToast(`触发规则 ${id}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
    } catch (error) {
        showToast(`触发规则失败: ${error.message}`, 'error');
    }
}

/**
 * 刷新数据源列表
 */
async function refreshSources() {
    const container = document.getElementById('sources-list');
    if (!container) return;
    
    try {
        const result = await api.call('automation.sources.list');
        if (result.code === 0 && result.data && result.data.sources) {
            const sources = result.data.sources;
            if (sources.length === 0) {
                container.innerHTML = '<p style="text-align:center;color:var(--text-light)">暂无数据源，点击"添加"创建第一个</p>';
                return;
            }
            
            container.innerHTML = `
                <table class="data-table">
                    <thead>
                        <tr>
                            <th>ID</th>
                            <th>标签</th>
                            <th>类型</th>
                            <th>状态</th>
                            <th>更新间隔</th>
                            <th>操作</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${sources.map(s => `
                            <tr>
                                <td><code>${s.id}</code></td>
                                <td>${s.label || s.id}</td>
                                <td><span style="padding:2px 8px;background:var(--primary-color);color:white;border-radius:4px;font-size:0.85em">${s.type || 'unknown'}</span></td>
                                <td><span class="status-badge ${s.enabled ? 'status-running' : 'status-stopped'}">${s.enabled ? '启用' : '禁用'}</span></td>
                                <td>${s.poll_interval_ms ? (s.poll_interval_ms / 1000) + '秒' : '-'}</td>
                                <td style="white-space:nowrap">
                                    <button class="btn btn-sm" onclick="showSourceVariables('${s.id}')" title="查看变量">📊</button>
                                    <button class="btn btn-sm" onclick="toggleSource('${s.id}', ${!s.enabled})" title="${s.enabled ? '禁用' : '启用'}">${s.enabled ? '🔴' : '🟢'}</button>
                                    <button class="btn btn-sm" onclick="showExportSourceModal('${s.id}')" title="导出配置包">📤</button>
                                    <button class="btn btn-sm btn-danger" onclick="deleteSource('${s.id}')" title="删除">🗑️</button>
                                </td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            `;
        } else {
            container.innerHTML = `<p style="text-align:center;color:var(--text-light)">⚠️ ${result.message || '获取数据源失败'}</p>`;
        }
    } catch (error) {
        const isNetworkError = error.message.includes('fetch') || error.message.includes('network');
        container.innerHTML = `<p style="text-align:center;color:var(--danger-color)">${isNetworkError ? '🔌 网络连接失败' : '❌ ' + error.message}</p>`;
    }
}

// 缓存变量数据用于过滤
let allVariables = [];

/**
 * 刷新变量列表
 */
async function refreshVariables() {
    const container = document.getElementById('variables-list');
    const countBadge = document.getElementById('variables-count');
    if (!container) return;
    
    container.innerHTML = '<div class="loading-small">加载中...</div>';
    
    try {
        const result = await api.call('automation.variables.list');
        if (result.code === 0 && result.data && result.data.variables) {
            allVariables = result.data.variables;
            if (countBadge) countBadge.textContent = allVariables.length;
            renderVariables(allVariables);
        } else {
            container.innerHTML = `<p style="text-align:center;color:var(--text-light)">⚠️ ${result.message || '获取变量失败'}</p>`;
        }
    } catch (error) {
        container.innerHTML = `<p style="text-align:center;color:var(--danger-color)">❌ ${error.message}</p>`;
    }
}

/**
 * 过滤变量
 */
function filterVariables() {
    const filter = document.getElementById('variable-filter').value.toLowerCase().trim();
    if (!filter) {
        renderVariables(allVariables);
        return;
    }
    const filtered = allVariables.filter(v => 
        v.name.toLowerCase().includes(filter) || 
        (v.source_id && v.source_id.toLowerCase().includes(filter))
    );
    renderVariables(filtered);
}

/**
 * 渲染变量列表
 */
function renderVariables(variables) {
    const container = document.getElementById('variables-list');
    if (!container) return;
    
    if (variables.length === 0) {
        container.innerHTML = '<p style="text-align:center;color:var(--text-light)">暂无变量数据</p>';
        return;
    }
    
    // 按来源分组
    const grouped = {};
    variables.forEach(v => {
        const source = v.source_id || 'system';
        if (!grouped[source]) grouped[source] = [];
        grouped[source].push(v);
    });
    
    let html = '<div class="variables-grouped">';
    for (const [source, vars] of Object.entries(grouped)) {
        html += `
            <details class="variable-group" open>
                <summary class="variable-group-header">
                    <span class="source-name">📡 ${source}</span>
                    <span class="variable-count">${vars.length} 个变量</span>
                </summary>
                <div class="variable-items">
                    <table class="data-table compact">
                        <thead>
                            <tr>
                                <th>变量名</th>
                                <th>类型</th>
                                <th>当前值</th>
                                <th>更新时间</th>
                            </tr>
                        </thead>
                        <tbody>
                            ${vars.map(v => `
                                <tr>
                                    <td><code class="variable-name">${v.name}</code></td>
                                    <td><span class="type-badge type-${v.type || 'unknown'}">${v.type || '-'}</span></td>
                                    <td class="variable-value">${formatVariableValue(v.value, v.type)}</td>
                                    <td class="variable-time">${v.updated_at ? formatTimeAgo(v.updated_at) : '-'}</td>
                                </tr>
                            `).join('')}
                        </tbody>
                    </table>
                </div>
            </details>
        `;
    }
    html += '</div>';
    container.innerHTML = html;
}

/**
 * 格式化变量值显示
 */
function formatVariableValue(value, type) {
    if (value === undefined || value === null) return '<span class="null-value">null</span>';
    
    if (type === 'number') {
        // 数字保留2位小数
        const num = parseFloat(value);
        if (!isNaN(num)) {
            return `<span class="number-value">${num % 1 === 0 ? num : num.toFixed(2)}</span>`;
        }
    } else if (type === 'boolean') {
        return value ? '<span class="bool-true">✓ true</span>' : '<span class="bool-false">✗ false</span>';
    } else if (type === 'string') {
        const str = String(value);
        if (str.length > 50) {
            return `<span class="string-value" title="${str}">"${str.substring(0, 47)}..."</span>`;
        }
        return `<span class="string-value">"${str}"</span>`;
    }
    
    // 默认：JSON 格式
    const str = JSON.stringify(value);
    if (str.length > 60) {
        return `<code title="${str}">${str.substring(0, 57)}...</code>`;
    }
    return `<code>${str}</code>`;
}

/**
 * 格式化时间为相对时间
 */
function formatTimeAgo(timestamp) {
    const now = Date.now();
    const ts = typeof timestamp === 'number' ? timestamp * 1000 : new Date(timestamp).getTime();
    const diff = now - ts;
    
    if (diff < 1000) return '刚刚';
    if (diff < 60000) return `${Math.floor(diff / 1000)}秒前`;
    if (diff < 3600000) return `${Math.floor(diff / 60000)}分钟前`;
    if (diff < 86400000) return `${Math.floor(diff / 3600000)}小时前`;
    return new Date(ts).toLocaleString();
}

/**
 * HTML 转义
 */
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

/**
 * 刷新动作模板列表
 */
async function refreshActions() {
    const container = document.getElementById('actions-list');
    if (!container) return;
    
    try {
        const result = await api.call('automation.actions.list', {});
        const actions = result.data?.templates || [];
        
        if (actions.length === 0) {
            container.innerHTML = '<p style="text-align:center;color:var(--text-light)">暂无动作模板，点击"添加"创建</p>';
        } else {
            container.innerHTML = `
                <table class="data-table compact">
                    <thead>
                        <tr>
                            <th>ID</th>
                            <th>名称</th>
                            <th>类型</th>
                            <th>模式</th>
                            <th>描述</th>
                            <th>操作</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${actions.map(a => `
                            <tr>
                                <td><code>${a.id}</code></td>
                                <td>${a.name || a.id}</td>
                                <td><span class="badge badge-${getActionTypeBadge(a.type)}">${getActionTypeLabel(a.type)}</span></td>
                                <td>${a.async ? '<span class="badge badge-warning">异步</span>' : '<span class="badge badge-light">同步</span>'}</td>
                                <td class="text-muted">${a.description || '-'}</td>
                                <td>
                                    <button class="btn btn-xs" onclick="testAction('${a.id}')" title="测试">▶️</button>
                                    <button class="btn btn-xs" onclick="editAction('${a.id}')" title="编辑">✏️</button>
                                    <button class="btn btn-xs" onclick="showExportActionModal('${a.id}')" title="导出配置包">📤</button>
                                    <button class="btn btn-danger btn-xs" onclick="deleteAction('${a.id}')" title="删除">🗑️</button>
                                </td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            `;
        }
    } catch (error) {
        container.innerHTML = `<p style="color:var(--danger)">加载失败: ${error.message}</p>`;
    }
}

/**
 * 获取动作类型标签
 */
function getActionTypeLabel(type) {
    const labels = {
        'led': 'LED',
        'ssh_cmd': 'SSH',
        'gpio': 'GPIO',
        'webhook': 'Webhook',
        'log': '日志',
        'set_var': '变量',
        'device_ctrl': '设备'
    };
    return labels[type] || type;
}

/**
 * 获取动作类型徽章样式
 */
function getActionTypeBadge(type) {
    const badges = {
        'led': 'info',
        'ssh_cmd': 'primary',
        'gpio': 'warning',
        'webhook': 'secondary',
        'log': 'light',
        'set_var': 'dark',
        'device_ctrl': 'danger'
    };
    return badges[type] || 'secondary';
}

/**
 * 显示添加动作模板对话框
 */
function showAddActionModal() {
    const modal = document.createElement('div');
    modal.className = 'modal active';
    modal.id = 'action-modal';
    modal.innerHTML = `
        <div class="modal-content modal-lg">
            <div class="modal-header">
                <h3>⚡ 新建动作模板</h3>
                <button class="modal-close" onclick="closeModal('action-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <!-- 第一步：选择动作类型 -->
                <div class="action-section">
                    <div class="section-title">1️⃣ 选择动作类型</div>
                    <div class="action-type-grid">
                        <label class="action-type-card" data-type="cli">
                            <input type="radio" name="action-type" value="cli" checked>
                            <div class="card-icon">⚡</div>
                            <div class="card-title">CLI 命令</div>
                            <div class="card-desc">执行本地控制台命令</div>
                        </label>
                        <label class="action-type-card" data-type="ssh_cmd_ref">
                            <input type="radio" name="action-type" value="ssh_cmd_ref">
                            <div class="card-icon">🔐</div>
                            <div class="card-title">SSH 命令</div>
                            <div class="card-desc">执行已配置的SSH命令</div>
                        </label>
                        <label class="action-type-card" data-type="led">
                            <input type="radio" name="action-type" value="led">
                            <div class="card-icon">💡</div>
                            <div class="card-title">LED 控制</div>
                            <div class="card-desc">控制 LED 颜色和效果</div>
                        </label>
                        <label class="action-type-card" data-type="log">
                            <input type="radio" name="action-type" value="log">
                            <div class="card-icon">📝</div>
                            <div class="card-title">日志记录</div>
                            <div class="card-desc">输出日志消息</div>
                        </label>
                        <label class="action-type-card" data-type="set_var">
                            <input type="radio" name="action-type" value="set_var">
                            <div class="card-icon">📊</div>
                            <div class="card-title">设置变量</div>
                            <div class="card-desc">修改系统变量值</div>
                        </label>
                        <label class="action-type-card" data-type="webhook">
                            <input type="radio" name="action-type" value="webhook">
                            <div class="card-icon">🌐</div>
                            <div class="card-title">Webhook</div>
                            <div class="card-desc">发送 HTTP 请求</div>
                        </label>
                    </div>
                </div>
                
                <!-- 第二步：配置参数 -->
                <div class="action-section">
                    <div class="section-title">2️⃣ 配置参数</div>
                    <div id="action-type-fields" class="action-params-container">
                        <!-- 动态生成的类型特定字段 -->
                    </div>
                </div>
                
                <!-- 第三步：基本信息 -->
                <div class="action-section">
                    <div class="section-title">3️⃣ 基本信息</div>
                    <div class="form-row">
                        <div class="form-group" style="flex:1">
                            <label>动作 ID <span class="required">*</span></label>
                            <input type="text" id="action-id" class="input" placeholder="唯一标识，如: restart_agx">
                            <small class="form-hint">用于规则引用，只能包含字母、数字和下划线</small>
                        </div>
                        <div class="form-group" style="flex:1">
                            <label>显示名称</label>
                            <input type="text" id="action-name" class="input" placeholder="如: 重启 AGX">
                            <small class="form-hint">留空则使用 ID</small>
                        </div>
                    </div>
                    <div class="form-row">
                        <div class="form-group" style="flex:2">
                            <label>描述</label>
                            <input type="text" id="action-description" class="input" placeholder="动作说明（可选）">
                        </div>
                        <div class="form-group" style="flex:1">
                            <label>执行延迟</label>
                            <div class="input-with-unit">
                                <input type="number" id="action-delay" class="input" value="0" min="0">
                                <span class="unit">ms</span>
                            </div>
                        </div>
                    </div>
                    
                    <!-- 执行模式选项 -->
                    <div class="execution-mode-option">
                        <label class="mode-switch">
                            <input type="checkbox" id="action-async">
                            <span class="mode-slider"></span>
                        </label>
                        <div class="mode-info">
                            <span class="mode-title">异步执行</span>
                            <span class="mode-desc">API 调用立即返回，动作在后台队列执行</span>
                        </div>
                    </div>
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn" onclick="closeModal('action-modal')">取消</button>
                <button class="btn btn-primary" onclick="submitAction()">💾 保存动作</button>
            </div>
        </div>
    `;
    document.body.appendChild(modal);
    
    // 绑定类型卡片点击事件
    modal.querySelectorAll('.action-type-card input').forEach(radio => {
        radio.addEventListener('change', updateActionTypeFields);
    });
    
    updateActionTypeFields();
}

/**
 * 更新动作类型字段
 */
function updateActionTypeFields() {
    const checked = document.querySelector('input[name="action-type"]:checked');
    const type = checked ? checked.value : 'cli';
    const container = document.getElementById('action-type-fields');
    
    // 更新卡片选中状态
    document.querySelectorAll('.action-type-card').forEach(card => {
        card.classList.toggle('selected', card.dataset.type === type);
    });
    
    const fields = {
        cli: `
            <div class="params-card">
                <div class="params-header">
                    <span class="params-icon">⚡</span>
                    <span>CLI 命令配置</span>
                </div>
                <div class="form-group">
                    <label>命令行 <span class="required">*</span></label>
                    <input type="text" id="action-cli-command" class="input input-mono" placeholder="如: gpio --set 48 1">
                    <small class="form-hint">支持所有控制台命令: gpio, device, fan, led, net 等</small>
                </div>
                <div class="quick-commands">
                    <span class="quick-label">快捷命令:</span>
                    <button type="button" class="quick-btn" onclick="setCliPreset('gpio --set 48 1')">GPIO</button>
                    <button type="button" class="quick-btn" onclick="setCliPreset('device --power-on agx0')">AGX开机</button>
                    <button type="button" class="quick-btn" onclick="setCliPreset('device --reset agx0')">AGX重启</button>
                    <button type="button" class="quick-btn" onclick="setCliPreset('fan --set --id 0 --speed 80')">风扇</button>
                    <button type="button" class="quick-btn" onclick="setCliPreset('led --effect --device board --name fire')">LED</button>
                </div>
                <details class="advanced-toggle">
                    <summary>高级选项</summary>
                    <div class="advanced-content">
                        <div class="form-group">
                            <label>结果变量</label>
                            <input type="text" id="action-cli-var" class="input" placeholder="如: cli.result">
                            <small class="form-hint">存储命令输出到变量</small>
                        </div>
                        <div class="form-group">
                            <label>超时时间</label>
                            <div class="input-with-unit">
                                <input type="number" id="action-cli-timeout" class="input" value="5000">
                                <span class="unit">ms</span>
                            </div>
                        </div>
                    </div>
                </details>
            </div>
        `,
        ssh_cmd_ref: `
            <div class="params-card">
                <div class="params-header">
                    <span class="params-icon">🔐</span>
                    <span>SSH 命令配置</span>
                </div>
                <div class="form-group">
                    <label>选择命令 <span class="required">*</span></label>
                    <select id="action-ssh-cmd-id" class="input" onchange="updateSshCmdRefPreview()">
                        <option value="">-- 加载中 --</option>
                    </select>
                    <small class="form-hint">选择已在 SSH 管理页面配置的命令</small>
                </div>
                <div id="ssh-cmd-preview" class="ssh-cmd-preview" style="display:none;">
                    <div class="preview-title">📋 命令详情</div>
                    <div class="preview-content">
                        <div class="preview-row"><span class="preview-label">主机:</span> <span id="preview-host">-</span></div>
                        <div class="preview-row"><span class="preview-label">命令:</span> <code id="preview-cmd">-</code></div>
                        <div class="preview-row"><span class="preview-label">变量:</span> <span id="preview-var">-</span></div>
                    </div>
                </div>
            </div>
        `,
        led: `
            <div class="params-card">
                <div class="params-header">
                    <span class="params-icon">💡</span>
                    <span>LED 控制配置</span>
                </div>
                <div class="form-group">
                    <label>设备 <span class="required">*</span></label>
                    <select id="action-led-device" class="input" onchange="updateActionLedOptions()">
                        <option value="">-- 选择设备 --</option>
                    </select>
                    <small class="form-hint">选择要控制的 LED 设备</small>
                </div>
                
                <!-- 控制类型选择 -->
                <div class="form-group" id="action-led-type-group" style="display:none;">
                    <label>控制类型 <span class="required">*</span></label>
                    <select id="action-led-type" class="input" onchange="updateActionLedTypeFields()">
                        <option value="fill">🎨 纯色填充</option>
                        <option value="effect">🎬 程序动画</option>
                        <option value="brightness">☀️ 仅调节亮度</option>
                        <option value="off">⏹ 关闭</option>
                    </select>
                </div>
                
                <!-- Matrix 专属控制类型 -->
                <div class="form-group" id="action-led-matrix-type-group" style="display:none;">
                    <label>控制类型 <span class="required">*</span></label>
                    <select id="action-led-matrix-type" class="input" onchange="updateActionLedTypeFields()">
                        <option value="fill">🎨 纯色填充</option>
                        <option value="effect">🎬 程序动画</option>
                        <option value="text">📝 文本显示</option>
                        <option value="image">📷 显示图像</option>
                        <option value="qrcode">📱 显示QR码</option>
                        <option value="filter">🎨 后处理滤镜</option>
                        <option value="filter_stop">⏹ 停止滤镜</option>
                        <option value="text_stop">⏹ 停止文本</option>
                        <option value="brightness">☀️ 仅调节亮度</option>
                        <option value="off">⏹ 关闭设备</option>
                    </select>
                </div>
                
                <!-- 动态参数区域 -->
                <div id="action-led-params"></div>
            </div>
        `,
        log: `
            <div class="params-card">
                <div class="params-header">
                    <span class="params-icon">📝</span>
                    <span>日志配置</span>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>级别</label>
                        <select id="action-log-level" class="input">
                            <option value="3">ℹ️ INFO</option>
                            <option value="2">⚠️ WARN</option>
                            <option value="1">❌ ERROR</option>
                            <option value="4">🔍 DEBUG</option>
                        </select>
                    </div>
                </div>
                <div class="form-group">
                    <label>消息 <span class="required">*</span></label>
                    <input type="text" id="action-log-message" class="input" placeholder="如: 设备状态变更: \${device.status}">
                    <small class="form-hint">支持变量: \${变量名}</small>
                </div>
            </div>
        `,
        set_var: `
            <div class="params-card">
                <div class="params-header">
                    <span class="params-icon">📊</span>
                    <span>变量配置</span>
                </div>
                <div class="form-group">
                    <label>变量名 <span class="required">*</span></label>
                    <input type="text" id="action-var-name" class="input" placeholder="如: system.flag">
                </div>
                <div class="form-group">
                    <label>值 <span class="required">*</span></label>
                    <input type="text" id="action-var-value" class="input" placeholder="支持表达式和变量引用">
                    <small class="form-hint">示例: true, 123, \${other_var}</small>
                </div>
            </div>
        `,
        webhook: `
            <div class="params-card">
                <div class="params-header">
                    <span class="params-icon">🌐</span>
                    <span>Webhook 配置</span>
                </div>
                <div class="form-group">
                    <label>URL <span class="required">*</span></label>
                    <input type="text" id="action-webhook-url" class="input" placeholder="https://api.example.com/webhook">
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>方法</label>
                        <select id="action-webhook-method" class="input">
                            <option value="POST">POST</option>
                            <option value="GET">GET</option>
                            <option value="PUT">PUT</option>
                        </select>
                    </div>
                </div>
                <div class="form-group">
                    <label>请求体</label>
                    <input type="text" id="action-webhook-body" class="input input-mono" placeholder='{"event": "\${trigger}"}'>
                    <small class="form-hint">JSON 格式，支持变量</small>
                </div>
            </div>
        `
    };
    
    container.innerHTML = fields[type] || '<div class="params-card"><p>请选择动作类型</p></div>';
    
    // SSH 命令类型时加载命令列表
    if (type === 'ssh_cmd_ref') {
        loadSshCommandsForAction();
    }
    
    // LED 类型时加载设备列表
    if (type === 'led') {
        loadLedDevicesForAction();
        // 亮度滑块实时更新
        const slider = document.getElementById('action-led-brightness');
        if (slider) {
            slider.addEventListener('input', () => {
                const val = document.getElementById('action-led-brightness-val');
                if (val) val.textContent = slider.value;
            });
        }
    }
}

/**
 * 提交动作模板
 */
async function submitAction() {
    const id = document.getElementById('action-id').value.trim();
    const name = document.getElementById('action-name').value.trim();
    const checked = document.querySelector('input[name="action-type"]:checked');
    const type = checked ? checked.value : '';
    const description = document.getElementById('action-description').value.trim();
    const delay = parseInt(document.getElementById('action-delay').value) || 0;
    const async = document.getElementById('action-async')?.checked || false;
    
    if (!id) {
        showToast('请填写动作 ID', 'error');
        return;
    }
    if (!type) {
        showToast('请选择动作类型', 'error');
        return;
    }
    
    const data = { id, name: name || id, type, description, delay_ms: delay, async };
    
    // 根据类型收集特定字段
    switch (type) {
        case 'cli':
            const cliCmd = document.getElementById('action-cli-command')?.value?.trim();
            if (!cliCmd) {
                showToast('请填写命令行', 'error');
                return;
            }
            data.cli = {
                command: cliCmd,
                var_name: document.getElementById('action-cli-var')?.value?.trim() || '',
                timeout_ms: parseInt(document.getElementById('action-cli-timeout')?.value) || 5000
            };
            break;
        case 'ssh_cmd_ref':
            const cmdId = document.getElementById('action-ssh-cmd-id')?.value;
            if (!cmdId) {
                showToast('请选择 SSH 命令', 'error');
                return;
            }
            data.ssh_ref = { cmd_id: cmdId };
            break;
        case 'led':
            const ledDevice = document.getElementById('action-led-device')?.value;
            if (!ledDevice) {
                showToast('请选择 LED 设备', 'error');
                return;
            }
            const isMatrix = ledDevice === 'matrix';
            const ledTypeSelect = isMatrix 
                ? document.getElementById('action-led-matrix-type')
                : document.getElementById('action-led-type');
            const ledCtrlType = ledTypeSelect?.value || 'fill';
            
            data.led = {
                device: ledDevice,
                ctrl_type: ledCtrlType
            };
            
            // 根据控制类型收集参数
            switch (ledCtrlType) {
                case 'fill':
                    data.led.color = document.getElementById('action-led-color')?.value || '#FF0000';
                    data.led.brightness = parseInt(document.getElementById('action-led-brightness')?.value) || 128;
                    data.led.index = parseInt(document.getElementById('action-led-index')?.value) || 255;
                    break;
                case 'effect':
                    data.led.effect = document.getElementById('action-led-effect')?.value;
                    data.led.speed = parseInt(document.getElementById('action-led-speed')?.value) || 50;
                    data.led.color = document.getElementById('action-led-color')?.value || '#FF0000';
                    if (!data.led.effect) {
                        showToast('请选择动画', 'error');
                        return;
                    }
                    break;
                case 'brightness':
                    data.led.brightness = parseInt(document.getElementById('action-led-brightness')?.value) || 128;
                    break;
                case 'off':
                    // 无需额外参数
                    break;
                case 'text':
                    data.led.text = document.getElementById('action-led-text')?.value?.trim();
                    if (!data.led.text) {
                        showToast('请输入文本内容', 'error');
                        return;
                    }
                    data.led.font = document.getElementById('action-led-font')?.value || '';
                    data.led.color = document.getElementById('action-led-color')?.value || '#00FF00';
                    data.led.align = document.getElementById('action-led-align')?.value || 'center';
                    data.led.scroll = document.getElementById('action-led-scroll')?.value || 'none';
                    data.led.speed = parseInt(document.getElementById('action-led-speed')?.value) || 50;
                    data.led.loop = document.getElementById('action-led-loop')?.checked || false;
                    data.led.x = parseInt(document.getElementById('action-led-x')?.value) || 0;
                    data.led.y = parseInt(document.getElementById('action-led-y')?.value) || 0;
                    data.led.auto_pos = document.getElementById('action-led-auto-pos')?.checked || false;
                    break;
                case 'image':
                    data.led.image_path = document.getElementById('action-led-image-path')?.value?.trim();
                    if (!data.led.image_path) {
                        showToast('请输入图像路径', 'error');
                        return;
                    }
                    data.led.center = document.getElementById('action-led-center')?.checked || false;
                    break;
                case 'qrcode':
                    data.led.qr_text = document.getElementById('action-led-qr-text')?.value?.trim();
                    if (!data.led.qr_text) {
                        showToast('请输入QR码内容', 'error');
                        return;
                    }
                    data.led.qr_ecc = document.getElementById('action-led-qr-ecc')?.value || 'M';
                    data.led.qr_fg = document.getElementById('action-led-qr-fg')?.value || '#FFFFFF';
                    data.led.qr_bg_image = document.getElementById('action-led-qr-bg')?.value || '';
                    break;
                case 'filter':
                    data.led.filter = document.getElementById('action-led-filter')?.value;
                    if (!data.led.filter) {
                        showToast('请选择滤镜', 'error');
                        return;
                    }
                    // 根据滤镜类型收集对应参数
                    const fConfig = filterConfig[data.led.filter];
                    if (fConfig && fConfig.params) {
                        data.led.filter_params = {};
                        fConfig.params.forEach(param => {
                            const el = document.getElementById(`action-filter-${param}`);
                            if (el) {
                                data.led.filter_params[param] = parseInt(el.value) || fConfig.defaults[param] || 50;
                            }
                        });
                    }
                    break;
            }
            break;
        case 'log':
            const logMsg = document.getElementById('action-log-message')?.value?.trim();
            if (!logMsg) {
                showToast('请填写日志消息', 'error');
                return;
            }
            data.log = {
                level: parseInt(document.getElementById('action-log-level').value),
                message: logMsg
            };
            break;
        case 'set_var':
            const varName = document.getElementById('action-var-name')?.value?.trim();
            const varValue = document.getElementById('action-var-value')?.value?.trim();
            if (!varName || !varValue) {
                showToast('请填写变量名和值', 'error');
                return;
            }
            data.set_var = {
                variable: varName,
                value: varValue
            };
            break;
        case 'webhook':
            const webhookUrl = document.getElementById('action-webhook-url')?.value?.trim();
            if (!webhookUrl) {
                showToast('请填写 Webhook URL', 'error');
                return;
            }
            data.webhook = {
                url: webhookUrl,
                method: document.getElementById('action-webhook-method').value,
                body_template: document.getElementById('action-webhook-body')?.value || ''
            };
            break;
    }
    
    try {
        const result = await api.call('automation.actions.add', data);
        if (result.code === 0) {
            showToast(`动作模板 ${id} 创建成功`, 'success');
            closeModal('action-modal');
            await refreshActions();
        } else {
            showToast(`创建失败: ${result.message}`, 'error');
        }
    } catch (error) {
        showToast(`创建失败: ${error.message}`, 'error');
    }
}

/**
 * CLI 命令快捷填充
 */
function setCliPreset(cmd) {
    const input = document.getElementById('action-cli-command');
    if (input) {
        input.value = cmd;
        input.focus();
    }
}

/**
 * 加载 SSH 主机列表 (用于动作模板)
 */
async function loadSshHostsForAction() {
    try {
        const select = document.getElementById('action-ssh-host');
        if (!select) return;
        
        const result = await api.call('ssh.hosts.list', {});
        select.innerHTML = '<option value="">-- 选择主机 --</option>';
        
        if (result.code === 0 && result.data?.hosts) {
            result.data.hosts.forEach(host => {
                const opt = document.createElement('option');
                opt.value = host.id;
                opt.textContent = `${host.name || host.id} (${host.host}:${host.port || 22})`;
                select.appendChild(opt);
            });
        }
        
        if (select.options.length === 1) {
            // 没有配置主机，提示用户
            select.innerHTML = '<option value="">-- 请先配置 SSH 主机 --</option>';
        }
    } catch (e) {
        console.error('加载 SSH 主机列表失败:', e);
        const select = document.getElementById('action-ssh-host');
        if (select) {
            select.innerHTML = '<option value="">-- 加载失败 --</option>';
        }
    }
}

/**
 * 加载 SSH 指令列表 (用于动作模板) - 保留用于兼容
 */
async function loadSshCommandsForAction() {
    try {
        const select = document.getElementById('action-ssh-cmd-id');
        if (!select) return;
        
        const result = await api.call('ssh.commands.list', {});
        select.innerHTML = '<option value="">-- 选择指令 --</option>';
        
        if (result.code === 0 && result.data?.commands) {
            result.data.commands.forEach(cmd => {
                const opt = document.createElement('option');
                opt.value = cmd.id;
                opt.textContent = `${cmd.name || cmd.id} (${cmd.host_id || 'localhost'})`;
                opt.dataset.host = cmd.host_id || '';
                opt.dataset.command = cmd.command || '';
                opt.dataset.varName = cmd.var_name || '';
                select.appendChild(opt);
            });
        }
    } catch (e) {
        console.error('加载 SSH 指令列表失败:', e);
    }
}

/**
 * 更新 SSH 指令预览
 */
async function updateSshCmdRefPreview() {
    const select = document.getElementById('action-ssh-cmd-id');
    const preview = document.getElementById('ssh-cmd-preview');
    if (!select || !preview) return;
    
    const cmdId = select.value;
    if (!cmdId) {
        preview.style.display = 'none';
        return;
    }
    
    // 从 API 获取完整指令信息
    try {
        const result = await api.call('ssh.commands.get', { id: cmdId });
        console.log('SSH command get result:', result);
        if (result.code === 0 && result.data) {
            const cmd = result.data;
            console.log('SSH command data:', cmd);
            document.getElementById('preview-host').textContent = cmd.host_id || '-';
            document.getElementById('preview-cmd').textContent = cmd.command || '-';
            // varName 字段只在配置了变量名时才存在
            const varName = cmd.varName || cmd.var_name || '';
            document.getElementById('preview-var').textContent = varName || '(未配置)';
            preview.style.display = 'block';
        } else {
            preview.style.display = 'none';
        }
    } catch (e) {
        console.error('获取 SSH 指令详情失败:', e);
        preview.style.display = 'none';
    }
}

/**
 * 加载 LED 设备列表 (用于动作模板)
 */
async function loadLedDevicesForAction() {
    try {
        const select = document.getElementById('action-led-device');
        const effectSelect = document.getElementById('action-led-effect');
        if (!select) return;
        
        const result = await api.ledList();
        select.innerHTML = '<option value="">-- 选择设备 --</option>';
        
        if (result.data?.devices) {
            result.data.devices.forEach(dev => {
                const opt = document.createElement('option');
                opt.value = dev.name;
                opt.textContent = `${dev.name} (${dev.count || 0} LEDs)`;
                opt.dataset.effects = JSON.stringify(dev.effects || []);
                select.appendChild(opt);
            });
        }
    } catch (e) {
        console.error('加载 LED 设备列表失败:', e);
    }
}

/**
 * 更新动作 LED 选项（根据设备类型显示不同控制类型）
 */
function updateActionLedOptions() {
    const deviceSelect = document.getElementById('action-led-device');
    const typeGroup = document.getElementById('action-led-type-group');
    const matrixTypeGroup = document.getElementById('action-led-matrix-type-group');
    const paramsContainer = document.getElementById('action-led-params');
    
    if (!deviceSelect) return;
    
    const deviceName = deviceSelect.value;
    const opt = deviceSelect.options[deviceSelect.selectedIndex];
    const isMatrix = deviceName === 'matrix';
    
    // 显示对应的控制类型选择器
    if (typeGroup) typeGroup.style.display = !deviceName ? 'none' : (isMatrix ? 'none' : 'block');
    if (matrixTypeGroup) matrixTypeGroup.style.display = isMatrix ? 'block' : 'none';
    
    // 存储设备特效列表
    if (opt && opt.dataset.effects) {
        window._actionLedEffects = JSON.parse(opt.dataset.effects || '[]');
    } else {
        window._actionLedEffects = [];
    }
    
    // 清空参数区域
    if (paramsContainer) paramsContainer.innerHTML = '';
    
    // 如果选择了设备，自动更新参数
    if (deviceName) {
        updateActionLedTypeFields();
    }
}

/**
 * 根据控制类型更新 LED 参数字段
 */
function updateActionLedTypeFields() {
    const deviceSelect = document.getElementById('action-led-device');
    const paramsContainer = document.getElementById('action-led-params');
    if (!deviceSelect || !paramsContainer) return;
    
    const deviceName = deviceSelect.value;
    const isMatrix = deviceName === 'matrix';
    const typeSelect = isMatrix 
        ? document.getElementById('action-led-matrix-type')
        : document.getElementById('action-led-type');
    
    if (!typeSelect) return;
    const ledType = typeSelect.value;
    const effects = window._actionLedEffects || [];
    
    let html = '';
    
    switch (ledType) {
        case 'fill':
            html = `
                <div class="form-group">
                    <label>颜色</label>
                    <div class="led-color-config">
                        <input type="color" value="#FF0000" id="action-led-color" class="led-color-picker-sm">
                        <div class="color-presets-inline">
                            <button type="button" class="color-dot" style="background:#ff0000" onclick="setActionLedColor('#ff0000')"></button>
                            <button type="button" class="color-dot" style="background:#ff6600" onclick="setActionLedColor('#ff6600')"></button>
                            <button type="button" class="color-dot" style="background:#ffff00" onclick="setActionLedColor('#ffff00')"></button>
                            <button type="button" class="color-dot" style="background:#00ff00" onclick="setActionLedColor('#00ff00')"></button>
                            <button type="button" class="color-dot" style="background:#00ffff" onclick="setActionLedColor('#00ffff')"></button>
                            <button type="button" class="color-dot" style="background:#0066ff" onclick="setActionLedColor('#0066ff')"></button>
                            <button type="button" class="color-dot" style="background:#ffffff" onclick="setActionLedColor('#ffffff')"></button>
                        </div>
                    </div>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>亮度</label>
                        <div class="brightness-config">
                            <input type="range" min="0" max="255" value="128" id="action-led-brightness" class="brightness-slider-sm" oninput="document.getElementById('action-led-brightness-val').textContent=this.value">
                            <span class="brightness-val" id="action-led-brightness-val">128</span>
                        </div>
                    </div>
                    <div class="form-group" style="flex:1">
                        <label>索引</label>
                        <input type="number" id="action-led-index" class="input" value="255" placeholder="255=全部">
                    </div>
                </div>
            `;
            break;
            
        case 'effect':
            const effectOptions = effects.map(e => `<option value="${e}">${e}</option>`).join('');
            html = `
                <div class="form-group">
                    <label>动画 <span class="required">*</span></label>
                    <select id="action-led-effect" class="input">
                        ${effectOptions || '<option value="">无可用动画</option>'}
                    </select>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>速度</label>
                        <div class="brightness-config">
                            <input type="range" min="1" max="100" value="50" id="action-led-speed" class="brightness-slider-sm" oninput="document.getElementById('action-led-speed-val').textContent=this.value">
                            <span class="brightness-val" id="action-led-speed-val">50</span>
                        </div>
                    </div>
                    <div class="form-group" style="flex:1">
                        <label>颜色</label>
                        <input type="color" value="#FF0000" id="action-led-color" class="led-color-picker-sm">
                    </div>
                </div>
            `;
            break;
            
        case 'brightness':
            html = `
                <div class="form-group">
                    <label>亮度</label>
                    <div class="brightness-config">
                        <input type="range" min="0" max="255" value="128" id="action-led-brightness" class="brightness-slider-sm" oninput="document.getElementById('action-led-brightness-val').textContent=this.value">
                        <span class="brightness-val" id="action-led-brightness-val">128</span>
                    </div>
                </div>
            `;
            break;
            
        case 'off':
            html = `<div class="form-hint" style="padding:10px;color:var(--text-light);">关闭 LED 设备，无需额外参数</div>`;
            break;
            
        case 'filter_stop':
            html = `<div class="form-hint" style="padding:10px;color:var(--text-light);">⏹ 停止当前运行的滤镜效果，无需额外参数</div>`;
            break;
            
        case 'text_stop':
            html = `<div class="form-hint" style="padding:10px;color:var(--text-light);">⏹ 停止当前运行的文本覆盖层，无需额外参数</div>`;
            break;
            
        case 'text':
            html = `
                <div class="form-group">
                    <label>文本内容 <span class="required">*</span></label>
                    <div class="input-with-btn">
                        <input type="text" id="action-led-text" class="input" placeholder="要显示的文本，支持 \${变量名}">
                        <button type="button" class="btn btn-sm" onclick="showVariableSelectModal('action-led-text')" title="插入变量">📊</button>
                    </div>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>字体</label>
                        <select id="action-led-font" class="input">
                            <option value="">默认</option>
                        </select>
                    </div>
                    <div class="form-group" style="flex:1">
                        <label>颜色</label>
                        <input type="color" value="#00FF00" id="action-led-color" class="led-color-picker-sm">
                    </div>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>对齐</label>
                        <select id="action-led-align" class="input">
                            <option value="left">左对齐</option>
                            <option value="center" selected>居中</option>
                            <option value="right">右对齐</option>
                        </select>
                    </div>
                    <div class="form-group" style="flex:1">
                        <label>滚动</label>
                        <select id="action-led-scroll" class="input">
                            <option value="none">无滚动</option>
                            <option value="left" selected>← 向左</option>
                            <option value="right">→ 向右</option>
                            <option value="up">↑ 向上</option>
                            <option value="down">↓ 向下</option>
                        </select>
                    </div>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:0.5">
                        <label>X</label>
                        <input type="number" id="action-led-x" class="input" value="0" min="0" max="255">
                    </div>
                    <div class="form-group" style="flex:0.5">
                        <label>Y</label>
                        <input type="number" id="action-led-y" class="input" value="0" min="0" max="255">
                    </div>
                    <div class="form-group" style="flex:1">
                        <label style="visibility:hidden;">自动</label>
                        <label class="checkbox-label"><input type="checkbox" id="action-led-auto-pos" checked> 自动位置</label>
                    </div>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>速度</label>
                        <input type="number" id="action-led-speed" class="input" value="50" min="1" max="100">
                    </div>
                    <div class="form-group" style="flex:1">
                        <label style="visibility:hidden;">循环</label>
                        <label class="checkbox-label"><input type="checkbox" id="action-led-loop" checked> 循环滚动</label>
                    </div>
                </div>
            `;
            // 加载字体列表
            setTimeout(loadActionLedFonts, 100);
            break;
            
        case 'image':
            html = `
                <div class="form-group">
                    <label>图像路径 <span class="required">*</span></label>
                    <div class="input-with-btn">
                        <input type="text" id="action-led-image-path" class="input" placeholder="/sdcard/images/xxx.png 或 \${变量名}" value="/sdcard/images/">
                        <button type="button" class="btn btn-sm" onclick="browseActionImages()" title="浏览文件">📁</button>
                        <button type="button" class="btn btn-sm" onclick="showVariableSelectModal('action-led-image-path')" title="插入变量">📊</button>
                    </div>
                    <small class="form-hint">支持 PNG、JPG、BMP、GIF 格式，路径支持变量</small>
                </div>
                <div class="form-group">
                    <label class="checkbox-label"><input type="checkbox" id="action-led-center" checked> 居中显示</label>
                </div>
            `;
            break;
            
        case 'qrcode':
            html = `
                <div class="form-group">
                    <label>编码内容 <span class="required">*</span></label>
                    <div class="input-with-btn">
                        <input type="text" id="action-led-qr-text" class="input" placeholder="文本或URL，支持 \${变量名}">
                        <button type="button" class="btn btn-sm" onclick="showVariableSelectModal('action-led-qr-text')" title="插入变量">📊</button>
                    </div>
                </div>
                <div class="form-row">
                    <div class="form-group" style="flex:1">
                        <label>纠错级别</label>
                        <select id="action-led-qr-ecc" class="input">
                            <option value="L">L - 7%</option>
                            <option value="M" selected>M - 15%</option>
                            <option value="Q">Q - 25%</option>
                            <option value="H">H - 30%</option>
                        </select>
                    </div>
                    <div class="form-group" style="flex:1">
                        <label>前景色</label>
                        <input type="color" value="#FFFFFF" id="action-led-qr-fg" class="led-color-picker-sm">
                    </div>
                </div>
                <div class="form-group">
                    <label>背景图（可选）</label>
                    <div class="input-with-btn">
                        <input type="text" id="action-led-qr-bg" class="input" placeholder="无" readonly>
                        <button type="button" class="btn btn-sm" onclick="browseActionQrBg()">📁 浏览</button>
                        <button type="button" class="btn btn-sm" onclick="document.getElementById('action-led-qr-bg').value=''" title="清除">✕</button>
                    </div>
                </div>
            `;
            break;
            
        case 'filter':
            html = `
                <div class="form-group">
                    <label>滤镜 <span class="required">*</span></label>
                    <select id="action-led-filter" class="input" onchange="updateActionFilterParams()">
                        <option value="pulse">💓 脉冲</option>
                        <option value="breathing">💨 呼吸</option>
                        <option value="blink">💡 闪烁</option>
                        <option value="wave">🌊 波浪</option>
                        <option value="scanline">📺 扫描线</option>
                        <option value="glitch">⚡ 故障艺术</option>
                        <option value="rainbow">🌈 彩虹</option>
                        <option value="sparkle">✨ 闪耀</option>
                        <option value="plasma">🎆 等离子体</option>
                        <option value="sepia">🖼️ 怀旧</option>
                        <option value="posterize">🎨 色阶分离</option>
                        <option value="contrast">🔆 对比度</option>
                        <option value="invert">🔄 反色</option>
                        <option value="grayscale">⬜ 灰度</option>
                    </select>
                </div>
                <div id="action-filter-params"></div>
            `;
            // 初始化滤镜参数
            setTimeout(updateActionFilterParams, 50);
            break;
    }
    
    paramsContainer.innerHTML = html;
}

/**
 * 加载动作 LED 字体列表
 */
async function loadActionLedFonts() {
    const fontSelect = document.getElementById('action-led-font');
    if (!fontSelect) return;
    
    try {
        const result = await api.storageList('/sdcard/fonts');
        const files = result.data?.entries || [];
        const fontExts = ['.fnt', '.bdf', '.pcf'];
        const fonts = files.filter(f => {
            if (f.type === 'dir' || f.type === 'directory') return false;
            const ext = f.name.toLowerCase().substring(f.name.lastIndexOf('.'));
            return fontExts.includes(ext);
        });
        
        fontSelect.innerHTML = '<option value="">默认</option>';
        fonts.forEach(f => {
            const option = document.createElement('option');
            const baseName = f.name.substring(0, f.name.lastIndexOf('.'));
            option.value = baseName;
            option.textContent = f.name;
            fontSelect.appendChild(option);
        });
    } catch (e) {
        console.error('加载字体失败:', e);
    }
}

/**
 * 设置动作 LED 颜色
 */
function setActionLedColor(color) {
    const picker = document.getElementById('action-led-color');
    if (picker) picker.value = color;
}

/**
 * 显示图像选择模态框
 * @param {string} title - 模态框标题
 * @param {function} onSelect - 选择回调，接收完整路径
 */
async function showImageSelectModal(title, onSelect) {
    // 创建模态框
    const modal = document.createElement('div');
    modal.id = 'image-select-modal';
    modal.className = 'modal show';
    modal.onclick = (e) => { if (e.target === modal) closeModal('image-select-modal');
                    };
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:500px;">
            <div class="modal-header">
                <h3>📁 ${title}</h3>
                <button class="modal-close" onclick="closeModal('image-select-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <div id="image-select-loading" style="text-align:center;padding:20px;">
                    <div class="spinner"></div>
                    <p>加载中...</p>
                </div>
                <div id="image-select-list" style="display:none;max-height:400px;overflow-y:auto;"></div>
                <div id="image-select-empty" style="display:none;text-align:center;padding:30px;color:var(--text-light);">
                    <div style="font-size:48px;margin-bottom:10px;">📭</div>
                    <p>没有找到图像文件</p>
                    <small>支持 PNG、JPG、BMP、GIF 格式</small>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // 加载图像列表
    try {
        const result = await api.storageList('/sdcard/images');
        const files = result.data?.entries || [];
        const imageExts = ['.png', '.jpg', '.jpeg', '.bmp', '.gif'];
        const images = files.filter(f => {
            if (f.type === 'dir' || f.type === 'directory') return false;
            const ext = f.name.toLowerCase().substring(f.name.lastIndexOf('.'));
            return imageExts.includes(ext);
        });
        
        document.getElementById('image-select-loading').style.display = 'none';
        
        if (images.length === 0) {
            document.getElementById('image-select-empty').style.display = 'block';
            return;
        }
        
        // 按名称排序
        images.sort((a, b) => a.name.localeCompare(b.name));
        
        const listEl = document.getElementById('image-select-list');
        listEl.style.display = 'block';
        listEl.innerHTML = images.map(img => {
            const fullPath = `/sdcard/images/${img.name}`;
            const icon = img.name.toLowerCase().endsWith('.gif') ? '🎞️' : '🖼️';
            const size = formatFileSize(img.size);
            return `
                <div class="image-select-item" data-path="${fullPath}" style="
                    display:flex;align-items:center;padding:12px;
                    border-bottom:1px solid var(--border);cursor:pointer;
                    transition:background 0.2s;"
                    onmouseover="this.style.background='var(--bg-hover)'"
                    onmouseout="this.style.background='transparent'"
                    onclick="selectImageItem(this, '${fullPath.replace(/'/g, "\\'")}')">
                    <span style="font-size:24px;margin-right:12px;">${icon}</span>
                    <div style="flex:1;min-width:0;">
                        <div style="font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${img.name}</div>
                        <small style="color:var(--text-light);">${size}</small>
                    </div>
                    <span class="image-select-check" style="display:none;color:var(--success);font-size:20px;">✓</span>
                </div>
            `;
        }).join('');
        
        // 存储回调
        window._imageSelectCallback = onSelect;
        
    } catch (e) {
        console.error('加载图像列表失败:', e);
        document.getElementById('image-select-loading').innerHTML = `
            <div style="color:var(--danger);">
                <p>加载失败: ${e.message}</p>
                <button class="btn btn-sm" onclick="closeModal('image-select-modal')">关闭</button>
            </div>
        `;
    }
}

/**
 * 选择图像项目
 */
function selectImageItem(el, path) {
    // 移除其他选中状态
    document.querySelectorAll('.image-select-item').forEach(item => {
        item.style.background = 'transparent';
        item.querySelector('.image-select-check').style.display = 'none';
    });
    
    // 选中当前项
    el.style.background = 'var(--bg-hover)';
    el.querySelector('.image-select-check').style.display = 'block';
    
    // 调用回调
    if (window._imageSelectCallback) {
        window._imageSelectCallback(path);
    }
    
    // 关闭模态框
    closeModal('image-select-modal');
}

/**
 * 浏览图像文件 (动作模板用)
 */
async function browseActionImages() {
    showImageSelectModal('选择图像文件', (path) => {
        document.getElementById('action-led-image-path').value = path;
    });
}

/**
 * 浏览 QR 背景图 (动作模板用)
 */
async function browseActionQrBg() {
    showImageSelectModal('选择背景图', (path) => {
        document.getElementById('action-led-qr-bg').value = path;
    });
}

/**
 * 显示变量选择模态框
 * @param {string} targetInputId - 目标输入框 ID
 * @param {string} mode - 'insert' 插入 ${var} 或 'replace' 替换整个值
 */
async function showVariableSelectModal(targetInputId, mode = 'insert') {
    const modal = document.createElement('div');
    modal.id = 'variable-select-modal';
    modal.className = 'modal show';
    modal.onclick = (e) => { if (e.target === modal) closeModal('variable-select-modal');
                    };
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:550px;">
            <div class="modal-header">
                <h3>📊 选择变量</h3>
                <button class="modal-close" onclick="closeModal('variable-select-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <div style="margin-bottom:12px;">
                    <input type="text" id="var-search" class="input" placeholder="🔍 搜索变量..." 
                           oninput="filterVariableList(this.value)" style="width:100%;">
                </div>
                <div id="variable-select-loading" style="text-align:center;padding:20px;">
                    <div class="spinner"></div>
                    <p>加载变量列表...</p>
                </div>
                <div id="variable-select-list" style="display:none;max-height:400px;overflow-y:auto;"></div>
                <div id="variable-select-empty" style="display:none;text-align:center;padding:30px;color:var(--text-light);">
                    <div style="font-size:48px;margin-bottom:10px;">📭</div>
                    <p>没有可用的变量</p>
                    <small>请先配置数据源并启用</small>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // 保存目标信息
    window._varSelectTarget = { inputId: targetInputId, mode: mode };
    
    // 加载变量列表
    try {
        const result = await api.call('automation.variables.list');
        const variables = result.data?.variables || [];
        
        document.getElementById('variable-select-loading').style.display = 'none';
        
        if (variables.length === 0) {
            document.getElementById('variable-select-empty').style.display = 'block';
            return;
        }
        
        // 按数据源分组
        const grouped = {};
        variables.forEach(v => {
            const sourceId = v.source_id || '_system';
            if (!grouped[sourceId]) grouped[sourceId] = [];
            grouped[sourceId].push(v);
        });
        
        const listEl = document.getElementById('variable-select-list');
        listEl.style.display = 'block';
        
        let html = '';
        for (const [sourceId, vars] of Object.entries(grouped)) {
            const groupId = `var-group-${sourceId.replace(/[^a-zA-Z0-9]/g, '_')}`;
            html += `<div class="var-group" data-source="${sourceId}">
                <div class="var-group-header" style="padding:10px 12px;background:var(--bg-elevated);font-weight:600;border-bottom:1px solid var(--border);cursor:pointer;display:flex;align-items:center;justify-content:space-between;"
                     onclick="toggleVarGroup('${groupId}')">
                    <span>📦 ${sourceId === '_system' ? '系统变量' : sourceId} <span style="font-weight:normal;color:var(--text-light);">(${vars.length})</span></span>
                    <span class="var-group-arrow" id="${groupId}-arrow" style="transition:transform 0.2s;">▶</span>
                </div>
                <div class="var-group-items" id="${groupId}" style="display:none;">`;
            
            vars.forEach(v => {
                const typeIcon = { 'bool': '🔘', 'int': '🔢', 'float': '📊', 'string': '📝' }[v.type] || '📋';
                const displayValue = v.value !== undefined ? String(v.value).substring(0, 30) : '-';
                html += `
                    <div class="var-select-item" data-name="${v.name}" data-source="${sourceId}" style="
                        display:flex;align-items:center;padding:10px 12px;padding-left:24px;
                        border-bottom:1px solid var(--border);cursor:pointer;
                        transition:background 0.2s;"
                        onmouseover="this.style.background='var(--bg-hover)'"
                        onmouseout="this.style.background='transparent'"
                        onclick="selectVariable('${v.name}')">
                        <span style="font-size:18px;margin-right:10px;">${typeIcon}</span>
                        <div style="flex:1;min-width:0;">
                            <div style="font-weight:500;font-family:monospace;">\${${v.name}}</div>
                            <small style="color:var(--text-light);">当前值: ${displayValue}${displayValue.length >= 30 ? '...' : ''}</small>
                        </div>
                        <span class="var-select-check" style="display:none;color:var(--success);font-size:20px;">✓</span>
                    </div>
                `;
            });
            html += '</div></div>';
        }
        
        listEl.innerHTML = html;
        
        // 聚焦搜索框
        setTimeout(() => document.getElementById('var-search')?.focus(), 100);
        
    } catch (e) {
        console.error('加载变量列表失败:', e);
        document.getElementById('variable-select-loading').innerHTML = `
            <div style="color:var(--danger);">
                <p>加载失败: ${e.message}</p>
                <button class="btn btn-sm" onclick="closeModal('variable-select-modal')">关闭</button>
            </div>
        `;
    }
}

/**
 * 切换变量分组的折叠状态
 */
function toggleVarGroup(groupId) {
    const itemsEl = document.getElementById(groupId);
    const arrowEl = document.getElementById(groupId + '-arrow');
    if (!itemsEl) return;
    
    const isHidden = itemsEl.style.display === 'none';
    itemsEl.style.display = isHidden ? 'block' : 'none';
    if (arrowEl) {
        arrowEl.style.transform = isHidden ? 'rotate(90deg)' : 'rotate(0deg)';
    }
}

/**
 * 过滤变量列表
 */
function filterVariableList(keyword) {
    const items = document.querySelectorAll('.var-select-item');
    const kw = keyword.toLowerCase();
    
    // 如果有搜索关键词，展开所有分组并过滤
    if (kw) {
        items.forEach(item => {
            const name = item.dataset.name.toLowerCase();
            item.style.display = name.includes(kw) ? 'flex' : 'none';
        });
        
        // 展开有匹配项的分组，隐藏没有匹配项的分组
        document.querySelectorAll('.var-group').forEach(group => {
            const itemsContainer = group.querySelector('.var-group-items');
            const arrow = group.querySelector('.var-group-arrow');
            const visibleItems = group.querySelectorAll('.var-select-item[style*="flex"]');
            
            if (visibleItems.length > 0) {
                group.style.display = 'block';
                if (itemsContainer) itemsContainer.style.display = 'block';
                if (arrow) arrow.style.transform = 'rotate(90deg)';
            } else {
                group.style.display = 'none';
            }
        });
    } else {
        // 清空搜索时，显示所有项目但折叠分组
        items.forEach(item => {
            item.style.display = 'flex';
        });
        
        document.querySelectorAll('.var-group').forEach(group => {
            const itemsContainer = group.querySelector('.var-group-items');
            const arrow = group.querySelector('.var-group-arrow');
            group.style.display = 'block';
            if (itemsContainer) itemsContainer.style.display = 'none';
            if (arrow) arrow.style.transform = 'rotate(0deg)';
        });
    }
}

/**
 * 选择变量
 */
function selectVariable(varName) {
    const varSelectModal = document.getElementById('variable-select-modal');
    
    // 检查是否是数据组件表达式编辑回调模式
    if (varSelectModal && varSelectModal.dataset.callback === 'widgetExpression') {
        const input = document.getElementById('edit-expression');
        if (input) {
            const curVal = input.value || '';
            // 在光标位置插入变量引用
            const start = input.selectionStart || curVal.length;
            const end = input.selectionEnd || curVal.length;
            const text = `\${${varName}}`;
            input.value = curVal.substring(0, start) + text + curVal.substring(end);
            input.focus();
            input.selectionStart = input.selectionEnd = start + text.length;
        }
        closeModal('variable-select-modal');
        delete varSelectModal.dataset.callback;
        return;
    }
    
    // 检查是否是数据组件绑定回调模式
    if (varSelectModal && varSelectModal.dataset.callback === 'widgetBind') {
        const widgetId = varSelectModal.dataset.widgetId;
        if (widgetId) {
            const widget = dataWidgets.find(w => w.id === widgetId);
            if (widget) {
                widget.variable = varName;
                saveDataWidgets();
                renderDataWidgets();
                refreshDataWidgets();
                showToast(`已绑定 ${widget.label} → ${varName}`, 'success');
            }
        }
        closeModal('variable-select-modal');
        return;
    }
    
    // 检查是否是数据组件编辑模态框回调模式
    if (varSelectModal && varSelectModal.dataset.callback === 'widgetEdit') {
        const input = document.getElementById('edit-widget-var');
        if (input) input.value = varName;
        closeModal('variable-select-modal');
        return;
    }
    
    // 检查是否是动作条件回调模式
    if (varSelectModal && varSelectModal.dataset.callback === 'actionCondition') {
        handleActionConditionVarSelect(varName);
        delete varSelectModal.dataset.callback;
        return;
    }
    
    // 检查是否是触发条件回调模式
    if (varSelectModal && varSelectModal.dataset.callback === 'ruleCondition') {
        handleConditionVarSelect(varName);
        delete varSelectModal.dataset.callback;
        return;
    }
    
    const target = window._varSelectTarget;
    if (!target) return;
    
    const input = document.getElementById(target.inputId);
    if (!input) return;
    
    if (target.mode === 'replace') {
        // 替换整个值
        input.value = `\${${varName}}`;
    } else {
        // 在光标位置插入
        const start = input.selectionStart || input.value.length;
        const end = input.selectionEnd || input.value.length;
        const text = `\${${varName}}`;
        input.value = input.value.substring(0, start) + text + input.value.substring(end);
        // 移动光标到插入文本之后
        input.selectionStart = input.selectionEnd = start + text.length;
    }
    
    input.focus();
    closeModal('variable-select-modal');
}

/**
 * 更新滤镜参数控件 (动作模板用)
 */
function updateActionFilterParams() {
    const filterSelect = document.getElementById('action-led-filter');
    const paramsContainer = document.getElementById('action-filter-params');
    if (!filterSelect || !paramsContainer) return;
    
    const filter = filterSelect.value;
    const config = filterConfig[filter];
    
    if (!config || !config.params || config.params.length === 0) {
        paramsContainer.innerHTML = '<div class="form-hint" style="padding:10px;color:var(--text-light);">此滤镜无额外参数</div>';
        return;
    }
    
    let html = '';
    config.params.forEach(param => {
        const paramInfo = paramLabels[param];
        if (!paramInfo) return;
        
        const defaultValue = config.defaults[param] || 50;
        html += `
            <div class="form-group">
                <label>${paramInfo.label}</label>
                <div class="brightness-config">
                    <input type="range" min="${paramInfo.min}" max="${paramInfo.max}" value="${defaultValue}" 
                           id="action-filter-${param}" class="brightness-slider-sm" 
                           oninput="document.getElementById('action-filter-${param}-val').textContent=this.value+'${paramInfo.unit || ''}'">
                    <span class="brightness-val" id="action-filter-${param}-val">${defaultValue}${paramInfo.unit || ''}</span>
                </div>
            </div>
        `;
    });
    
    paramsContainer.innerHTML = html;
}

/**
 * 测试动作
 */
async function testAction(id) {
    try {
        showToast(`正在执行动作: ${id}...`, 'info');
        const result = await api.call('automation.actions.execute', { id });
        console.log('Action execute result:', result);
        
        if (result.code === 0) {
            let msg = result.message || '执行成功';
            if (result.data?.output) {
                msg += ` - 输出: ${result.data.output.substring(0, 100)}`;
            }
            showToast(msg, 'success');
        } else {
            showToast(`动作 ${id} 失败: ${result.message || '未知错误'}`, 'error');
        }
    } catch (error) {
        console.error('Action execute error:', error);
        showToast(`动作执行失败: ${error.message}`, 'error');
    }
}

/**
 * 编辑动作
 */
async function editAction(id) {
    try {
        const result = await api.call('automation.actions.get', { id });
        if (result.code !== 0) {
            showToast(`获取动作详情失败: ${result.message}`, 'error');
            return;
        }
        
        const tpl = result.data;
        
        // 打开添加对话框并填充数据
        await showAddActionModal();
        
        // 等待 DOM 更新
        await new Promise(r => setTimeout(r, 100));
        
        // 填充基本信息
        document.getElementById('action-id').value = tpl.id;
        document.getElementById('action-id').disabled = true; // ID 不可编辑
        document.getElementById('action-name').value = tpl.name || '';
        document.getElementById('action-description').value = tpl.description || '';
        document.getElementById('action-delay').value = tpl.delay_ms || 0;
        document.getElementById('action-async').checked = tpl.async || false;
        
        // 选择类型
        const typeRadio = document.querySelector(`input[name="action-type"][value="${tpl.type}"]`);
        if (typeRadio) {
            typeRadio.checked = true;
            await updateActionTypeFields();
            await new Promise(r => setTimeout(r, 100));
            
            // 根据类型填充字段
            switch (tpl.type) {
                case 'cli':
                    if (tpl.cli) {
                        document.getElementById('action-cli-command').value = tpl.cli.command || '';
                        document.getElementById('action-cli-var').value = tpl.cli.var_name || '';
                        document.getElementById('action-cli-timeout').value = tpl.cli.timeout_ms || 5000;
                    }
                    break;
                case 'ssh_cmd_ref':
                    if (tpl.ssh_ref) {
                        await loadSshCommandsForAction();
                        await new Promise(r => setTimeout(r, 100));
                        document.getElementById('action-ssh-cmd-id').value = tpl.ssh_ref.cmd_id || '';
                        updateSshCmdRefPreview();
                    }
                    break;
                case 'led':
                    if (tpl.led) {
                        // 先等待设备列表加载完成
                        await loadLedDevicesForAction();
                        await new Promise(r => setTimeout(r, 100));
                        
                        // 设置设备
                        const deviceEl = document.getElementById('action-led-device');
                        if (deviceEl) {
                            deviceEl.value = tpl.led.device || 'board';
                        }
                        updateActionLedOptions();
                        await new Promise(r => setTimeout(r, 100));
                        
                        // 设置控制类型
                        const isMatrix = tpl.led.device === 'matrix';
                        const ctrlTypeEl = isMatrix 
                            ? document.getElementById('action-led-matrix-type')
                            : document.getElementById('action-led-type');
                        if (ctrlTypeEl && tpl.led.ctrl_type) {
                            ctrlTypeEl.value = tpl.led.ctrl_type;
                            updateActionLedTypeFields();
                            // 等待字段渲染和异步加载（如字体列表）
                            await new Promise(r => setTimeout(r, 300));
                        }
                        
                        // 根据控制类型填充对应字段
                        switch (tpl.led.ctrl_type) {
                            case 'fill':
                                if (tpl.led.color) {
                                    const colorEl = document.getElementById('action-led-color');
                                    if (colorEl) colorEl.value = tpl.led.color;
                                }
                                if (tpl.led.brightness !== undefined) {
                                    const brEl = document.getElementById('action-led-brightness');
                                    if (brEl) brEl.value = tpl.led.brightness;
                                    const brVal = document.getElementById('action-led-brightness-val');
                                    if (brVal) brVal.textContent = tpl.led.brightness;
                                }
                                if (tpl.led.index !== undefined && tpl.led.index !== 255) {
                                    const idxEl = document.getElementById('action-led-index');
                                    if (idxEl) idxEl.value = tpl.led.index;
                                }
                                break;
                            case 'effect':
                                if (tpl.led.effect) {
                                    const effectEl = document.getElementById('action-led-effect');
                                    if (effectEl) effectEl.value = tpl.led.effect;
                                }
                                if (tpl.led.color) {
                                    const colorEl = document.getElementById('action-led-color');
                                    if (colorEl) colorEl.value = tpl.led.color;
                                }
                                if (tpl.led.speed !== undefined) {
                                    const speedEl = document.getElementById('action-led-speed');
                                    if (speedEl) speedEl.value = tpl.led.speed;
                                }
                                break;
                            case 'brightness':
                                if (tpl.led.brightness !== undefined) {
                                    const brEl = document.getElementById('action-led-brightness');
                                    if (brEl) brEl.value = tpl.led.brightness;
                                    const brVal = document.getElementById('action-led-brightness-val');
                                    if (brVal) brVal.textContent = tpl.led.brightness;
                                }
                                break;
                            case 'text':
                                if (tpl.led.text) {
                                    const textEl = document.getElementById('action-led-text');
                                    if (textEl) textEl.value = tpl.led.text;
                                }
                                if (tpl.led.font) {
                                    // 字体列表可能还在加载，设置一个延迟重试
                                    const setFont = () => {
                                        const fontEl = document.getElementById('action-led-font');
                                        if (fontEl) {
                                            fontEl.value = tpl.led.font;
                                            // 如果字体选项还没加载，等待后重试
                                            if (fontEl.value !== tpl.led.font && fontEl.options.length <= 1) {
                                                setTimeout(setFont, 200);
                                            }
                                        }
                                    };
                                    setFont();
                                }
                                if (tpl.led.color) {
                                    const colorEl = document.getElementById('action-led-color');
                                    if (colorEl) colorEl.value = tpl.led.color;
                                }
                                if (tpl.led.scroll) {
                                    const scrollEl = document.getElementById('action-led-scroll');
                                    if (scrollEl) scrollEl.value = tpl.led.scroll;
                                }
                                if (tpl.led.speed !== undefined) {
                                    const speedEl = document.getElementById('action-led-speed');
                                    if (speedEl) speedEl.value = tpl.led.speed;
                                }
                                if (tpl.led.loop !== undefined) {
                                    const loopEl = document.getElementById('action-led-loop');
                                    if (loopEl) loopEl.checked = tpl.led.loop;
                                }
                                if (tpl.led.align) {
                                    const alignEl = document.getElementById('action-led-align');
                                    if (alignEl) alignEl.value = tpl.led.align;
                                }
                                break;
                            case 'image':
                                if (tpl.led.image_path) {
                                    const imgEl = document.getElementById('action-led-image-path');
                                    if (imgEl) imgEl.value = tpl.led.image_path;
                                }
                                if (tpl.led.center !== undefined) {
                                    const centerEl = document.getElementById('action-led-center');
                                    if (centerEl) centerEl.checked = tpl.led.center;
                                }
                                break;
                            case 'qrcode':
                                if (tpl.led.qr_text) {
                                    const qrEl = document.getElementById('action-led-qr-text');
                                    if (qrEl) qrEl.value = tpl.led.qr_text;
                                }
                                if (tpl.led.qr_ecc) {
                                    const eccEl = document.getElementById('action-led-qr-ecc');
                                    if (eccEl) eccEl.value = tpl.led.qr_ecc;
                                }
                                if (tpl.led.color) {
                                    const fgEl = document.getElementById('action-led-qr-fg');
                                    if (fgEl) fgEl.value = tpl.led.color;
                                }
                                break;
                            case 'filter':
                                if (tpl.led.filter) {
                                    const filterEl = document.getElementById('action-led-filter');
                                    if (filterEl) filterEl.value = tpl.led.filter;
                                }
                                break;
                        }
                    }
                    break;
                case 'log':
                    if (tpl.log) {
                        document.getElementById('action-log-level').value = tpl.log.level || 3;
                        document.getElementById('action-log-message').value = tpl.log.message || '';
                    }
                    break;
                case 'set_var':
                    if (tpl.set_var) {
                        document.getElementById('action-var-name').value = tpl.set_var.variable || '';
                        document.getElementById('action-var-value').value = tpl.set_var.value || '';
                    }
                    break;
                case 'webhook':
                    if (tpl.webhook) {
                        document.getElementById('action-webhook-url').value = tpl.webhook.url || '';
                        document.getElementById('action-webhook-method').value = tpl.webhook.method || 'POST';
                        document.getElementById('action-webhook-body').value = tpl.webhook.body_template || '';
                    }
                    break;
            }
        }
        
        // 更改模态框标题和按钮
        const modalTitle = document.querySelector('#action-modal .modal-header h3');
        if (modalTitle) modalTitle.textContent = '✏️ 编辑动作模板';
        
        const submitBtn = document.querySelector('#action-modal button[onclick="submitAction()"]');
        if (submitBtn) {
            submitBtn.textContent = '💾 更新';
            submitBtn.setAttribute('onclick', `updateAction('${tpl.id}')`);
        }
        
    } catch (error) {
        showToast(`编辑动作失败: ${error.message}`, 'error');
    }
}

/**
 * 更新动作模板
 */
async function updateAction(originalId) {
    // 先删除旧的，再创建新的（因为没有 update API）
    try {
        // 获取表单数据
        const id = document.getElementById('action-id').value.trim();
        
        // 删除旧模板
        const deleteResult = await api.call('automation.actions.delete', { id: originalId });
        if (deleteResult.code !== 0) {
            showToast(`更新失败: 无法删除旧模板`, 'error');
            return;
        }
        
        // 重新启用 ID 字段并提交
        document.getElementById('action-id').disabled = false;
        
        // 调用添加逻辑
        await submitAction();
        
    } catch (error) {
        showToast(`更新失败: ${error.message}`, 'error');
    }
}

/**
 * 删除动作
 */
async function deleteAction(id) {
    if (!confirm(`确定要删除动作模板 "${id}" 吗？`)) return;
    
    try {
        const result = await api.call('automation.actions.delete', { id });
        showToast(`删除动作 ${id}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
        if (result.code === 0) {
            await refreshActions();
        }
    } catch (error) {
        showToast(`删除失败: ${error.message}`, 'error');
    }
}

/**
 * 切换数据源启用状态
 */
async function toggleSource(id, enable) {
    try {
        const action = enable ? 'automation.sources.enable' : 'automation.sources.disable';
        const result = await api.call(action, { id });
        showToast(`数据源 ${id} ${enable ? '启用' : '禁用'}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
        if (result.code === 0) {
            await refreshSources();
        }
    } catch (error) {
        showToast(`切换数据源状态失败: ${error.message}`, 'error');
    }
}

/**
 * 删除数据源
 */
async function deleteSource(id) {
    if (!confirm(`确定要删除数据源 "${id}" 吗？此操作不可撤销。`)) {
        return;
    }
    
    try {
        const result = await api.call('automation.sources.delete', { id });
        showToast(`删除数据源 ${id}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
        if (result.code === 0) {
            await Promise.all([refreshSources(), refreshAutomationStatus()]);
        }
    } catch (error) {
        showToast(`删除数据源失败: ${error.message}`, 'error');
    }
}

/**
 * 显示数据源的变量列表
 */
async function showSourceVariables(sourceId) {
    const modal = document.getElementById('source-variables-modal');
    const body = document.getElementById('source-variables-body');
    if (!modal || !body) return;
    
    // 更新标题
    const header = modal.querySelector('.modal-header h2');
    if (header) header.textContent = `📊 ${sourceId} 变量`;
    
    body.innerHTML = '<div class="loading">加载中...</div>';
    modal.classList.remove('hidden');
    
    try {
        const result = await api.call('automation.variables.list');
        if (result.code === 0 && result.data && result.data.variables) {
            // 过滤出属于该数据源的变量
            const vars = result.data.variables.filter(v => v.source_id === sourceId);
            
            if (vars.length === 0) {
                body.innerHTML = '<p style="text-align:center;color:var(--text-light);padding:20px">该数据源暂无变量数据</p>';
                return;
            }
            
            body.innerHTML = `
                <table class="data-table">
                    <thead>
                        <tr>
                            <th>变量名</th>
                            <th>类型</th>
                            <th>当前值</th>
                            <th>更新时间</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${vars.map(v => `
                            <tr>
                                <td><code class="variable-name">${v.name}</code></td>
                                <td><span class="type-badge type-${v.type || 'unknown'}">${v.type || '-'}</span></td>
                                <td class="variable-value">${formatVariableValue(v.value, v.type)}</td>
                                <td class="variable-time">${v.updated_at ? formatTimeAgo(v.updated_at) : '-'}</td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            `;
        } else {
            body.innerHTML = `<p style="text-align:center;color:var(--danger-color)">⚠️ ${result.message || '获取变量失败'}</p>`;
        }
    } catch (error) {
        body.innerHTML = `<p style="text-align:center;color:var(--danger-color)">❌ ${error.message}</p>`;
    }
}

/**
 * 关闭数据源变量模态框
 */
function closeSourceVariablesModal() {
    const modal = document.getElementById('source-variables-modal');
    if (modal) modal.classList.add('hidden');
}

// =========================================================================
//                         关机设置模态框
// =========================================================================

/**
 * 显示关机设置模态框
 */
async function showShutdownSettingsModal() {
    const modal = document.getElementById('shutdown-settings-modal');
    if (!modal) return;
    
    // 隐藏错误信息
    const errorDiv = document.getElementById('shutdown-settings-error');
    if (errorDiv) errorDiv.classList.add('hidden');
    
    // 显示模态框
    modal.classList.remove('hidden');
    
    // 加载当前配置
    try {
        const result = await api.powerProtectionConfig();
        if (result.code === 0 && result.data) {
            const config = result.data;
            document.getElementById('ss-low-voltage').value = config.low_voltage_threshold || 12.6;
            document.getElementById('ss-recovery-voltage').value = config.recovery_voltage_threshold || 18.0;
            document.getElementById('ss-shutdown-delay').value = config.shutdown_delay_sec || 60;
            document.getElementById('ss-recovery-hold').value = config.recovery_hold_sec || 5;
            document.getElementById('ss-fan-stop-delay').value = config.fan_stop_delay_sec || 60;
        }
    } catch (e) {
        console.error('Failed to load shutdown settings:', e);
        // 使用默认值
        document.getElementById('ss-low-voltage').value = 12.6;
        document.getElementById('ss-recovery-voltage').value = 18.0;
        document.getElementById('ss-shutdown-delay').value = 60;
        document.getElementById('ss-recovery-hold').value = 5;
        document.getElementById('ss-fan-stop-delay').value = 60;
    }
}

/**
 * 关闭关机设置模态框
 */
function closeShutdownSettingsModal() {
    const modal = document.getElementById('shutdown-settings-modal');
    if (modal) modal.classList.add('hidden');
}

/**
 * 保存关机设置
 */
async function saveShutdownSettings() {
    const errorDiv = document.getElementById('shutdown-settings-error');
    
    const config = {
        low_threshold: parseFloat(document.getElementById('ss-low-voltage').value),
        recovery_threshold: parseFloat(document.getElementById('ss-recovery-voltage').value),
        shutdown_delay: parseInt(document.getElementById('ss-shutdown-delay').value),
        recovery_hold: parseInt(document.getElementById('ss-recovery-hold').value),
        fan_stop_delay: parseInt(document.getElementById('ss-fan-stop-delay').value),
        persist: true  // 标记需要持久化
    };
    
    // 验证
    if (config.low_threshold >= config.recovery_threshold) {
        errorDiv.textContent = '低电压阈值必须小于恢复电压阈值';
        errorDiv.classList.remove('hidden');
        return;
    }
    
    if (config.shutdown_delay < 10 || config.shutdown_delay > 600) {
        errorDiv.textContent = '关机倒计时必须在 10-600 秒之间';
        errorDiv.classList.remove('hidden');
        return;
    }
    
    try {
        const result = await api.powerProtectionSet(config);
        if (result.code === 0) {
            showToast('✅ 关机设置已保存', 'success');
            closeShutdownSettingsModal();
        } else {
            errorDiv.textContent = result.message || '保存失败';
            errorDiv.classList.remove('hidden');
        }
    } catch (e) {
        errorDiv.textContent = '保存失败: ' + e.message;
        errorDiv.classList.remove('hidden');
    }
}

/**
 * 恢复默认关机设置
 */
async function resetShutdownSettings() {
    if (!confirm('确认恢复默认设置？')) return;
    
    const config = {
        low_threshold: 12.6,
        recovery_threshold: 18.0,
        shutdown_delay: 60,
        recovery_hold: 5,
        fan_stop_delay: 60,
        persist: true
    };
    
    try {
        const result = await api.powerProtectionSet(config);
        if (result.code === 0) {
            // 更新界面
            document.getElementById('ss-low-voltage').value = 12.6;
            document.getElementById('ss-recovery-voltage').value = 18.0;
            document.getElementById('ss-shutdown-delay').value = 60;
            document.getElementById('ss-recovery-hold').value = 5;
            document.getElementById('ss-fan-stop-delay').value = 60;
            showToast('✅ 已恢复默认设置', 'success');
        } else {
            showToast('恢复失败: ' + (result.message || 'Unknown error'), 'error');
        }
    } catch (e) {
        showToast('恢复失败: ' + e.message, 'error');
    }
}

/**
 * 删除规则
 */
async function deleteRule(id) {
    if (!confirm(`确定要删除规则 "${id}" 吗？此操作不可撤销。`)) {
        return;
    }
    
    try {
        const result = await api.call('automation.rules.delete', { id });
        showToast(`删除规则 ${id}: ${result.message || 'OK'}`, result.code === 0 ? 'success' : 'error');
        if (result.code === 0) {
            await Promise.all([refreshRules(), refreshAutomationStatus()]);
        }
    } catch (error) {
        showToast(`删除规则失败: ${error.message}`, 'error');
    }
}

/**
 * 编辑规则
 */
async function editRule(id) {
    try {
        // 获取规则详情
        const result = await api.call('automation.rules.get', { id });
        if (result.code !== 0 || !result.data) {
            showToast(`获取规则详情失败: ${result.message || '未知错误'}`, 'error');
            return;
        }
        
        // 打开编辑模态框
        showAddRuleModal(result.data);
    } catch (error) {
        showToast(`获取规则详情失败: ${error.message}`, 'error');
    }
}

/**
 * 显示添加数据源模态框
 */
function showAddSourceModal() {
    // 移除可能存在的旧模态框
    const oldModal = document.getElementById('add-source-modal');
    if (oldModal) oldModal.remove();
    
    const modal = document.createElement('div');
    modal.id = 'add-source-modal';
    modal.className = 'modal';
    modal.innerHTML = `
        <div class="modal-content automation-modal wide">
            <div class="modal-header">
                <h3>➕ 添加外部数据源</h3>
                <button class="modal-close" onclick="closeModal('add-source-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <!-- 数据源类型选择（标签页样式） -->
                <div class="modal-tabs">
                    <button type="button" class="modal-tab active" data-type="rest" onclick="switchSourceType('rest')">
                        🌐 REST API
                    </button>
                    <button type="button" class="modal-tab" data-type="websocket" onclick="switchSourceType('websocket')">
                        🔌 WebSocket
                    </button>
                    <button type="button" class="modal-tab" data-type="socketio" onclick="switchSourceType('socketio')">
                        ⚡ Socket.IO
                    </button>
                    <button type="button" class="modal-tab" data-type="variable" onclick="switchSourceType('variable')">
                        📦 指令变量
                    </button>
                </div>
                <input type="hidden" id="source-type" value="rest">
                
                <!-- 基本信息 -->
                <div class="form-row">
                    <div class="form-group">
                        <label>数据源 ID <span class="required">*</span></label>
                        <input type="text" id="source-id" class="input" placeholder="如: agx_temp">
                    </div>
                    <div class="form-group">
                        <label>显示名称</label>
                        <input type="text" id="source-label" class="input" placeholder="如: AGX 温度">
                    </div>
                </div>
                
                <!-- REST API 配置 -->
                <div id="source-rest-config" class="config-section">
                    <div class="config-title">🌐 REST API 配置</div>
                    <div class="form-group">
                        <label>请求地址 <span class="required">*</span></label>
                        <div class="input-with-btn">
                            <input type="text" id="source-rest-url" class="input" placeholder="http://192.168.1.100/api/status">
                            <button class="btn btn-sm" onclick="testRestConnection()" id="btn-test-rest">🔍 测试</button>
                        </div>
                    </div>
                    <div class="form-row">
                        <div class="form-group" style="flex:0 0 100px">
                            <label>方法</label>
                            <select id="source-rest-method" class="input">
                                <option value="GET">GET</option>
                                <option value="POST">POST</option>
                            </select>
                        </div>
                        <div class="form-group" style="flex:0 0 120px">
                            <label>轮询间隔 (ms)</label>
                            <input type="number" id="source-interval" class="input" value="5000" min="500">
                        </div>
                        <div class="form-group">
                            <label>Authorization 头（可选）</label>
                            <input type="text" id="source-rest-auth" class="input" placeholder="Bearer token">
                        </div>
                    </div>
                    
                    <!-- 测试结果 & 变量选择 -->
                    <div id="rest-test-result" class="test-result-panel" style="display:none">
                        <div class="test-result-header">
                            <span class="test-status"></span>
                            <button class="btn btn-sm" onclick="toggleJsonPreview()">📄 原始数据</button>
                        </div>
                        <div id="rest-json-preview" class="json-preview" style="display:none"></div>
                        <div id="rest-var-selector" class="var-selector">
                            <div class="var-selector-title">📊 选择要提取的字段：</div>
                            <div class="var-list"></div>
                        </div>
                    </div>
                    
                    <div class="form-group">
                        <label>JSON 数据路径 <span style="color:var(--text-light);font-weight:normal">(点击上方字段自动填入)</span></label>
                        <input type="text" id="source-rest-path" class="input" placeholder="如: data.temperature（留空取整个响应）">
                    </div>
                </div>
                
                <!-- WebSocket 配置 -->
                <div id="source-websocket-config" class="config-section" style="display:none">
                    <div class="config-title">🔌 WebSocket 配置</div>
                    <div class="form-group">
                        <label>WebSocket 地址 <span class="required">*</span></label>
                        <div class="input-with-btn">
                            <input type="text" id="source-ws-uri" class="input" placeholder="ws://192.168.1.100:8080/ws">
                            <button class="btn btn-sm" onclick="testWsConnection()" id="btn-test-ws">🔍 测试</button>
                        </div>
                    </div>
                    
                    <!-- WebSocket 测试结果 -->
                    <div id="ws-test-result" class="test-result-panel" style="display:none">
                        <div class="test-result-header">
                            <span class="test-status"></span>
                            <button class="btn btn-sm" onclick="toggleWsJsonPreview()">📄 原始数据</button>
                        </div>
                        <div id="ws-json-preview" class="json-preview" style="display:none"></div>
                        <div id="ws-var-selector" class="var-selector">
                            <div class="var-selector-title">📊 选择要提取的字段：</div>
                            <div class="var-list"></div>
                        </div>
                    </div>
                    
                    <div class="form-group">
                        <label>JSON 数据路径</label>
                        <input type="text" id="source-ws-path" class="input" placeholder="如: data.temperature（留空取整个消息）">
                    </div>
                    <div class="form-group">
                        <label>断线重连间隔 (ms)</label>
                        <input type="number" id="source-ws-reconnect" class="input" value="5000" min="1000">
                    </div>
                </div>
                
                <!-- Socket.IO 配置 -->
                <div id="source-socketio-config" class="config-section" style="display:none">
                    <div class="config-title">⚡ Socket.IO 配置</div>
                    <div class="form-group">
                        <label>服务器地址 <span class="required">*</span></label>
                        <div class="input-with-btn">
                            <input type="text" id="source-sio-url" class="input" placeholder="http://10.10.99.99:59090">
                            <button class="btn btn-sm" onclick="testSioConnection()" id="btn-test-sio">🔍 测试</button>
                        </div>
                        <small style="color:var(--text-light)">Socket.IO v4 协议，使用 HTTP/HTTPS 地址</small>
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label>事件名称 <span style="color:var(--text-light);font-weight:normal">(留空自动发现)</span></label>
                            <input type="text" id="source-sio-event" class="input" placeholder="测试时留空可自动发现事件">
                        </div>
                        <div class="form-group" style="flex:0 0 150px">
                            <label>超时时间 (ms)</label>
                            <input type="number" id="source-sio-timeout" class="input" value="15000" min="5000">
                        </div>
                    </div>
                    
                    <!-- Socket.IO 测试结果 -->
                    <div id="sio-test-result" class="test-result-panel" style="display:none">
                        <div class="test-result-header">
                            <span class="test-status"></span>
                            <button class="btn btn-sm" onclick="toggleSioJsonPreview()">📄 原始数据</button>
                        </div>
                        <div id="sio-json-preview" class="json-preview" style="display:none"></div>
                        <div id="sio-var-selector" class="var-selector">
                            <div class="var-selector-title">📊 选择要提取的字段：</div>
                            <div class="var-list"></div>
                        </div>
                    </div>
                    
                    <div class="form-group">
                        <label>JSON 数据路径</label>
                        <input type="text" id="source-sio-path" class="input" placeholder="如: cpu.avg_usage（留空取整个事件数据）">
                    </div>
                    
                    <!-- Socket.IO 自动发现开关 -->
                    <label class="checkbox-label">
                        <input type="checkbox" id="source-sio-auto-discover" checked>
                        <span>自动发现所有 JSON 字段为变量</span>
                    </label>
                    <small style="color:var(--text-light);display:block;margin-top:-10px;margin-bottom:10px;padding-left:24px">
                        关闭后仅使用上方选中的字段作为变量
                    </small>
                </div>
                
                <!-- 指令变量数据源配置 -->
                <div id="source-variable-config" class="config-section" style="display:none">
                    <div class="config-title">🔌 SSH 指令变量</div>
                    
                    <!-- SSH 主机选择 -->
                    <div class="form-group">
                        <label>SSH 主机 <span class="required">*</span></label>
                        <select id="source-ssh-host" class="input" onchange="onSshHostChangeForSource()">
                            <option value="">-- 加载中... --</option>
                        </select>
                        <small style="color:var(--text-light)">选择已配置的 SSH 主机（在 SSH 页面添加）</small>
                    </div>
                    
                    <!-- 选择已创建的命令 -->
                    <div class="form-group">
                        <label>选择指令 <span class="required">*</span></label>
                        <select id="source-ssh-cmd" class="input" onchange="onSshCmdChange()">
                            <option value="">-- 先选择主机 --</option>
                        </select>
                        <small style="color:var(--text-light)">选择要监视的指令（在 SSH 页面创建）</small>
                    </div>
                    
                    <!-- 选中命令的详情预览 -->
                    <div id="source-ssh-cmd-preview" class="ssh-cmd-preview" style="display:none">
                        <div class="preview-title">📋 指令详情</div>
                        <div class="preview-content">
                            <div class="preview-row"><span class="preview-label">命令:</span> <code id="preview-command">-</code></div>
                            <div class="preview-row"><span class="preview-label">描述:</span> <span id="preview-desc">-</span></div>
                            <div class="preview-row"><span class="preview-label">超时:</span> <span id="preview-timeout">30</span> 秒</div>
                        </div>
                    </div>
                    
                    <!-- 变量预览 -->
                    <div class="form-group">
                        <div class="ssh-vars-preview">
                            <div class="preview-title">📦 将监视以下变量（需先执行指令）：</div>
                            <div id="ssh-vars-list" class="ssh-vars-list">
                                <span class="text-muted">请先选择 SSH 主机和指令</span>
                            </div>
                        </div>
                    </div>
                    
                    <!-- 检测间隔 -->
                    <div class="form-group">
                        <label>检测间隔 (秒)</label>
                        <input type="number" id="source-var-interval" class="input" value="5" min="1" max="3600">
                        <small style="color:var(--text-light)">定期读取变量值的间隔</small>
                    </div>
                </div>
                
                <!-- 启用选项 -->
                <label class="checkbox-label">
                    <input type="checkbox" id="source-enabled" checked>
                    <span>创建后立即启用</span>
                </label>
            </div>
            <div class="modal-footer">
                <button class="btn" onclick="closeModal('add-source-modal')">取消</button>
                <button class="btn btn-primary" onclick="submitAddSource()">添加数据源</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    setTimeout(() => modal.classList.add('show'), 10);
}

// 存储测试结果数据
let lastTestData = null;
let wsTestSocket = null;

/**
 * 测试 REST API 连接
 */
async function testRestConnection() {
    const url = document.getElementById('source-rest-url').value.trim();
    const method = document.getElementById('source-rest-method').value;
    const auth = document.getElementById('source-rest-auth').value.trim();
    
    if (!url) {
        alert('请输入 API 地址');
        return;
    }
    
    const btn = document.getElementById('btn-test-rest');
    const resultPanel = document.getElementById('rest-test-result');
    const statusSpan = resultPanel.querySelector('.test-status');
    
    btn.disabled = true;
    btn.textContent = '⏳ 测试中...';
    resultPanel.style.display = 'block';
    statusSpan.innerHTML = '<span style="color:var(--warning-color)">🔄 正在请求...</span>';
    
    try {
        // 通过 ESP32 代理请求（避免 CORS）
        const result = await api.call('automation.proxy.fetch', { 
            url, 
            method,
            headers: auth ? { 'Authorization': auth } : {}
        });
        
        if (result.code === 0 && result.data) {
            lastTestData = result.data.body;
            statusSpan.innerHTML = `<span style="color:var(--secondary-color)">✅ 连接成功</span> <span style="color:var(--text-light)">(${result.data.status || 200})</span>`;
            
            // 解析并显示可选变量
            try {
                const jsonData = typeof lastTestData === 'string' ? JSON.parse(lastTestData) : lastTestData;
                renderVarSelector('rest-var-selector', jsonData, 'source-rest-path');
                document.getElementById('rest-json-preview').textContent = JSON.stringify(jsonData, null, 2);
            } catch (e) {
                // 非 JSON 响应
                document.querySelector('#rest-var-selector .var-list').innerHTML = 
                    '<div class="var-item disabled">响应非 JSON 格式，无法解析字段</div>';
                document.getElementById('rest-json-preview').textContent = lastTestData;
            }
        } else {
            statusSpan.innerHTML = `<span style="color:var(--danger-color)">❌ 请求失败: ${result.message || '未知错误'}</span>`;
            document.querySelector('#rest-var-selector .var-list').innerHTML = '';
        }
    } catch (error) {
        statusSpan.innerHTML = `<span style="color:var(--danger-color)">❌ 错误: ${error.message}</span>`;
        document.querySelector('#rest-var-selector .var-list').innerHTML = '';
    }
    
    btn.disabled = false;
    btn.textContent = '🔍 测试';
}

/**
 * 测试 WebSocket 连接
 */
async function testWsConnection() {
    const uri = document.getElementById('source-ws-uri').value.trim();
    
    if (!uri) {
        alert('请输入 WebSocket 地址');
        return;
    }
    
    const btn = document.getElementById('btn-test-ws');
    const resultPanel = document.getElementById('ws-test-result');
    const statusSpan = resultPanel.querySelector('.test-status');
    
    // 关闭之前的测试连接
    if (wsTestSocket) {
        wsTestSocket.close();
        wsTestSocket = null;
    }
    
    btn.disabled = true;
    btn.textContent = '⏳ 连接中...';
    resultPanel.style.display = 'block';
    statusSpan.innerHTML = '<span style="color:var(--warning-color)">🔄 正在连接...</span>';
    
    try {
        // 通过 ESP32 测试 WebSocket（获取第一条消息）
        const result = await api.call('automation.proxy.websocket_test', { uri, timeout_ms: 5000 });
        
        if (result.code === 0 && result.data) {
            lastTestData = result.data.message;
            statusSpan.innerHTML = `<span style="color:var(--secondary-color)">✅ 连接成功，已收到数据</span>`;
            
            try {
                const jsonData = typeof lastTestData === 'string' ? JSON.parse(lastTestData) : lastTestData;
                renderVarSelector('ws-var-selector', jsonData, 'source-ws-path');
                document.getElementById('ws-json-preview').textContent = JSON.stringify(jsonData, null, 2);
            } catch (e) {
                document.querySelector('#ws-var-selector .var-list').innerHTML = 
                    '<div class="var-item disabled">消息非 JSON 格式，无法解析字段</div>';
                document.getElementById('ws-json-preview').textContent = lastTestData;
            }
        } else {
            statusSpan.innerHTML = `<span style="color:var(--danger-color)">❌ ${result.message || '连接失败'}</span>`;
            document.querySelector('#ws-var-selector .var-list').innerHTML = '';
        }
    } catch (error) {
        statusSpan.innerHTML = `<span style="color:var(--danger-color)">❌ 错误: ${error.message}</span>`;
        document.querySelector('#ws-var-selector .var-list').innerHTML = '';
    }
    
    btn.disabled = false;
    btn.textContent = '🔍 测试';
}

/**
 * 测试 Socket.IO 连接
 */
async function testSioConnection() {
    const url = document.getElementById('source-sio-url').value.trim();
    const event = document.getElementById('source-sio-event').value.trim();
    const timeout = parseInt(document.getElementById('source-sio-timeout').value) || 15000;
    
    if (!url) {
        alert('请输入 Socket.IO 服务器地址');
        return;
    }
    
    const btn = document.getElementById('btn-test-sio');
    const resultPanel = document.getElementById('sio-test-result');
    const statusSpan = resultPanel.querySelector('.test-status');
    const eventInput = document.getElementById('source-sio-event');
    
    btn.disabled = true;
    btn.textContent = '⏳ 连接中...';
    resultPanel.style.display = 'block';
    
    // 显示连接阶段状态
    const statusText = event ? `正在连接并等待事件: ${event}` : '正在连接并自动发现事件...';
    statusSpan.innerHTML = `<span style="color:var(--warning-color)">🔄 ${statusText}</span>`;
    
    try {
        // 通过 ESP32 测试 Socket.IO 连接
        // 如果没有指定事件，将获取服务器推送的第一个事件
        const params = { url, timeout_ms: timeout };
        if (event) params.event = event;
        
        const result = await api.call('automation.proxy.socketio_test', params);
        
        if (result.code === 0 && result.data) {
            const data = result.data;
            const eventName = data.event || '(未知事件)';
            lastTestData = data.data;
            
            // 显示成功状态和发现的事件
            let statusHtml = `<span style="color:var(--secondary-color)">✅ 连接成功</span>`;
            if (data.event) {
                statusHtml += ` <span style="color:var(--text-light)">| 事件: <strong>${eventName}</strong></span>`;
            }
            if (data.sid) {
                statusHtml += ` <span style="color:var(--text-light);font-size:0.85em">| SID: ${data.sid.substring(0, 8)}...</span>`;
            }
            statusSpan.innerHTML = statusHtml;
            
            // 自动填充发现的事件名（如果用户没有手动输入）
            if (data.event && !eventInput.value) {
                eventInput.value = data.event;
                eventInput.style.borderColor = 'var(--secondary-color)';
                setTimeout(() => eventInput.style.borderColor = '', 2000);
            }
            
            try {
                const jsonData = typeof lastTestData === 'string' ? JSON.parse(lastTestData) : lastTestData;
                renderVarSelector('sio-var-selector', jsonData, 'source-sio-path');
                document.getElementById('sio-json-preview').textContent = JSON.stringify(jsonData, null, 2);
            } catch (e) {
                document.querySelector('#sio-var-selector .var-list').innerHTML = 
                    '<div class="var-item disabled">事件数据非 JSON 格式，无法解析字段</div>';
                document.getElementById('sio-json-preview').textContent = String(lastTestData);
            }
        } else {
            // 显示详细错误信息
            let errorMsg = result.message || '连接失败';
            if (result.data && result.data.sid) {
                errorMsg += ` (已获取 SID，但未收到事件数据)`;
            }
            statusSpan.innerHTML = `<span style="color:var(--danger-color)">❌ ${errorMsg}</span>`;
            document.querySelector('#sio-var-selector .var-list').innerHTML = 
                '<div class="var-item disabled">提示：留空事件名称可自动发现服务器推送的事件</div>';
            
            // 显示详细错误
            if (result.data && result.data.error) {
                document.getElementById('sio-json-preview').textContent = result.data.error;
                document.getElementById('sio-json-preview').style.display = 'block';
            }
        }
    } catch (error) {
        statusSpan.innerHTML = `<span style="color:var(--danger-color)">❌ 错误: ${error.message}</span>`;
        document.querySelector('#sio-var-selector .var-list').innerHTML = '';
    }
    
    btn.disabled = false;
    btn.textContent = '🔍 测试';
}

/**
 * 切换 Socket.IO JSON 预览显示
 */
function toggleSioJsonPreview() {
    const preview = document.getElementById('sio-json-preview');
    preview.style.display = preview.style.display === 'none' ? 'block' : 'none';
}

/**
 * 渲染变量选择器
 */
function renderVarSelector(containerId, data, targetInputId, prefix = '') {
    const container = document.querySelector(`#${containerId} .var-list`);
    if (!container) return;
    
    const items = [];
    flattenJson(data, prefix, items);
    
    if (items.length === 0) {
        container.innerHTML = '<div class="var-item disabled">无可选字段</div>';
        return;
    }
    
    container.innerHTML = items.map(item => `
        <div class="var-item" onclick="selectVarPath('${targetInputId}', '${item.path}')">
            <span class="var-path">${item.path}</span>
            <span class="var-type">${item.type}</span>
            <span class="var-value">${item.preview}</span>
        </div>
    `).join('');
}

/**
 * 扁平化 JSON 对象，提取所有叶子节点路径
 */
function flattenJson(obj, prefix, result, maxDepth = 5) {
    if (maxDepth <= 0) return;
    
    if (obj === null || obj === undefined) {
        result.push({ path: prefix || '(root)', type: 'null', preview: 'null' });
        return;
    }
    
    const type = typeof obj;
    
    if (Array.isArray(obj)) {
        if (obj.length === 0) {
            result.push({ path: prefix || '(root)', type: 'array', preview: '[]' });
        } else {
            // 显示数组本身
            result.push({ 
                path: prefix || '(root)', 
                type: `array[${obj.length}]`, 
                preview: `[${obj.length} items]` 
            });
            // 展开第一个元素作为示例
            if (typeof obj[0] === 'object' && obj[0] !== null) {
                flattenJson(obj[0], prefix ? `${prefix}[0]` : '[0]', result, maxDepth - 1);
            }
        }
    } else if (type === 'object') {
        const keys = Object.keys(obj);
        if (keys.length === 0) {
            result.push({ path: prefix || '(root)', type: 'object', preview: '{}' });
        } else {
            for (const key of keys) {
                const newPath = prefix ? `${prefix}.${key}` : key;
                const value = obj[key];
                const valueType = typeof value;
                
                if (value === null) {
                    result.push({ path: newPath, type: 'null', preview: 'null' });
                } else if (Array.isArray(value)) {
                    flattenJson(value, newPath, result, maxDepth - 1);
                } else if (valueType === 'object') {
                    flattenJson(value, newPath, result, maxDepth - 1);
                } else {
                    // 叶子节点
                    let preview = String(value);
                    if (preview.length > 30) preview = preview.substring(0, 27) + '...';
                    result.push({ path: newPath, type: valueType, preview });
                }
            }
        }
    } else {
        // 基本类型
        let preview = String(obj);
        if (preview.length > 30) preview = preview.substring(0, 27) + '...';
        result.push({ path: prefix || '(root)', type, preview });
    }
}

/**
 * 选择变量路径
 */
function selectVarPath(inputId, path) {
    const input = document.getElementById(inputId);
    if (input) {
        input.value = path;
        input.focus();
        // 高亮效果
        input.style.borderColor = 'var(--secondary-color)';
        setTimeout(() => input.style.borderColor = '', 1000);
    }
}

/**
 * 切换 JSON 预览显示
 */
function toggleJsonPreview() {
    const preview = document.getElementById('rest-json-preview');
    preview.style.display = preview.style.display === 'none' ? 'block' : 'none';
}

function toggleWsJsonPreview() {
    const preview = document.getElementById('ws-json-preview');
    preview.style.display = preview.style.display === 'none' ? 'block' : 'none';
}

/**
 * 切换数据源类型
 */
function switchSourceType(type) {
    // 更新隐藏字段
    document.getElementById('source-type').value = type;
    
    // 更新标签页状态
    document.querySelectorAll('.modal-tab').forEach(tab => {
        tab.classList.toggle('active', tab.dataset.type === type);
    });
    
    // 切换配置区块
    document.getElementById('source-rest-config').style.display = type === 'rest' ? 'block' : 'none';
    document.getElementById('source-websocket-config').style.display = type === 'websocket' ? 'block' : 'none';
    const sioConfig = document.getElementById('source-socketio-config');
    if (sioConfig) sioConfig.style.display = type === 'socketio' ? 'block' : 'none';
    const varConfig = document.getElementById('source-variable-config');
    if (varConfig) {
        varConfig.style.display = type === 'variable' ? 'block' : 'none';
        // 切换到指令变量类型时自动加载主机列表
        if (type === 'variable') {
            loadSshHostsForSource();
        }
    }
    
    // 处理数据源 ID 输入框的只读状态
    const sourceIdInput = document.getElementById('source-id');
    if (sourceIdInput) {
        if (type === 'variable') {
            // 指令变量类型：ID 由选择的命令决定，设为只读
            sourceIdInput.readOnly = true;
            sourceIdInput.style.backgroundColor = 'var(--bg-color)';
            sourceIdInput.placeholder = '由选择的指令自动填入';
        } else {
            // 其他类型：允许手动输入
            sourceIdInput.readOnly = false;
            sourceIdInput.style.backgroundColor = '';
            sourceIdInput.placeholder = '如: agx_temp';
            sourceIdInput.value = '';  // 清空之前可能由指令填入的值
        }
    }
}

/**
 * 根据数据源类型更新表单字段（兼容旧调用）
 */
function updateSourceTypeFields() {
    const type = document.getElementById('source-type').value;
    switchSourceType(type);
}

// updateBuiltinFields 函数已移除 - 内置数据源由系统自动注册，无需手动配置

/**
 * 加载 SSH 主机列表（用于数据源配置）
 */
async function loadSshHostsForSource() {
    const hostSelect = document.getElementById('source-ssh-host');
    if (!hostSelect) return;
    
    hostSelect.innerHTML = '<option value="">-- 加载中... --</option>';
    
    // 重置命令选择
    const cmdSelect = document.getElementById('source-ssh-cmd');
    if (cmdSelect) {
        cmdSelect.innerHTML = '<option value="">-- 先选择主机 --</option>';
    }
    
    // 隐藏命令预览
    const preview = document.getElementById('source-ssh-cmd-preview');
    if (preview) preview.style.display = 'none';
    
    // 重置变量预览
    const varsListDiv = document.getElementById('ssh-vars-list');
    if (varsListDiv) varsListDiv.innerHTML = '<span class="text-muted">请先选择 SSH 主机和指令</span>';
    
    try {
        const result = await api.call('ssh.hosts.list');
        if (result.code === 0 && result.data && result.data.hosts) {
            const hosts = result.data.hosts;
            
            if (hosts.length === 0) {
                hostSelect.innerHTML = '<option value="">-- 暂无主机，请先在 SSH 页面添加 --</option>';
                return;
            }
            
            let html = '<option value="">-- 请选择主机 --</option>';
            hosts.forEach(h => {
                const label = `${h.id} (${h.username}@${h.host}:${h.port || 22})`;
                html += `<option value="${h.id}">${label}</option>`;
            });
            hostSelect.innerHTML = html;
        } else {
            hostSelect.innerHTML = `<option value="">-- 加载失败: ${result.message || '未知错误'} --</option>`;
        }
    } catch (error) {
        hostSelect.innerHTML = `<option value="">-- 加载失败: ${error.message} --</option>`;
    }
}

/**
 * SSH 主机选择变化时的处理（数据源配置用）
 */
async function onSshHostChangeForSource() {
    const hostId = document.getElementById('source-ssh-host').value;
    const cmdSelect = document.getElementById('source-ssh-cmd');
    
    if (!cmdSelect) return;
    
    // 隐藏命令预览
    const preview = document.getElementById('source-ssh-cmd-preview');
    if (preview) preview.style.display = 'none';
    
    // 重置变量预览
    const varsListDiv = document.getElementById('ssh-vars-list');
    if (varsListDiv) varsListDiv.innerHTML = '<span class="text-muted">请先选择指令</span>';
    
    if (!hostId) {
        cmdSelect.innerHTML = '<option value="">-- 先选择主机 --</option>';
        return;
    }
    
    // 确保 sshCommands 已加载（异步操作）
    if (typeof sshCommands === 'undefined' || Object.keys(sshCommands).length === 0) {
        cmdSelect.innerHTML = '<option value="">-- 加载中... --</option>';
        await loadSshCommands();
    }
    
    // 获取该主机下的命令列表
    const commands = sshCommands[hostId] || [];
    
    if (commands.length === 0) {
        cmdSelect.innerHTML = '<option value="">-- 该主机暂无指令，请在 SSH 页面添加 --</option>';
        return;
    }
    
    let html = '<option value="">-- 请选择指令 --</option>';
    commands.forEach((cmd, idx) => {
        const icon = cmd.icon || '🚀';
        const label = `${icon} ${cmd.name}`;
        html += `<option value="${idx}">${label}</option>`;
    });
    cmdSelect.innerHTML = html;
}

/**
 * SSH 命令选择变化时的处理
 */
function onSshCmdChange() {
    const hostId = document.getElementById('source-ssh-host').value;
    const cmdIdx = document.getElementById('source-ssh-cmd').value;
    const preview = document.getElementById('source-ssh-cmd-preview');
    const varsListDiv = document.getElementById('ssh-vars-list');
    const sourceIdInput = document.getElementById('source-id');
    const sourceLabelInput = document.getElementById('source-label');
    
    if (!hostId || cmdIdx === '') {
        if (preview) preview.style.display = 'none';
        if (varsListDiv) varsListDiv.innerHTML = '<span class="text-muted">请先选择指令</span>';
        return;
    }
    
    // 获取选中的命令
    const cmd = sshCommands[hostId]?.[parseInt(cmdIdx)];
    if (!cmd) {
        if (preview) preview.style.display = 'none';
        if (varsListDiv) varsListDiv.innerHTML = '<span class="text-muted">指令不存在</span>';
        return;
    }
    
    // 显示命令详情预览
    if (preview) {
        preview.style.display = 'block';
        document.getElementById('preview-command').textContent = cmd.command;
        document.getElementById('preview-desc').textContent = cmd.desc || '无描述';
        document.getElementById('preview-timeout').textContent = cmd.timeout || 30;
    }
    
    // 更新变量预览
    const varName = cmd.varName || cmd.name;  // 优先使用 varName，否则用 name
    if (varsListDiv) {
        varsListDiv.innerHTML = `
            <div class="var-item-preview"><code>${varName}.status</code> - 执行状态 (success/failed/error)</div>
            <div class="var-item-preview"><code>${varName}.exit_code</code> - 退出码</div>
            <div class="var-item-preview"><code>${varName}.extracted</code> - 提取的值</div>
            <div class="var-item-preview"><code>${varName}.expect_matched</code> - 成功模式匹配结果</div>
            <div class="var-item-preview"><code>${varName}.fail_matched</code> - 失败模式匹配结果</div>
            <div class="var-item-preview"><code>${varName}.host</code> - 执行主机</div>
            <div class="var-item-preview"><code>${varName}.timestamp</code> - 执行时间戳</div>
        `;
    }
    
    // 自动填充数据源 ID 和显示名称（基于命令的 varName）
    if (sourceIdInput) {
        sourceIdInput.value = varName;
        sourceIdInput.readOnly = true;  // 设为只读，因为必须与 varName 一致
        sourceIdInput.style.backgroundColor = 'var(--bg-color)';
    }
    if (sourceLabelInput && !sourceLabelInput.value) {
        sourceLabelInput.value = cmd.name || varName;
    }
}

/**
 * 提交添加数据源
 */
async function submitAddSource() {
    const id = document.getElementById('source-id').value.trim();
    const label = document.getElementById('source-label').value.trim() || id;
    const type = document.getElementById('source-type').value;
    const interval = parseInt(document.getElementById('source-interval')?.value) || 1000;
    const enabled = document.getElementById('source-enabled').checked;
    
    if (!id) {
        alert('请输入数据源 ID');
        return;
    }
    
    const params = { id, label, type, poll_interval_ms: interval, enabled };
    
    // 根据类型添加额外参数
    if (type === 'websocket') {
        params.uri = document.getElementById('source-ws-uri').value.trim();
        params.json_path = document.getElementById('source-ws-path').value.trim();
        params.reconnect_ms = parseInt(document.getElementById('source-ws-reconnect').value) || 5000;
        
        if (!params.uri) {
            alert('请输入 WebSocket URI');
            return;
        }
    } else if (type === 'rest') {
        params.url = document.getElementById('source-rest-url').value.trim();
        params.method = document.getElementById('source-rest-method').value;
        params.json_path = document.getElementById('source-rest-path').value.trim();
        params.auth_header = document.getElementById('source-rest-auth').value.trim();
        
        if (!params.url) {
            alert('请输入 REST URL');
            return;
        }
    } else if (type === 'socketio') {
        // Socket.IO 数据源配置
        params.url = document.getElementById('source-sio-url').value.trim();
        params.event = document.getElementById('source-sio-event').value.trim();
        params.json_path = document.getElementById('source-sio-path').value.trim();
        params.timeout_ms = parseInt(document.getElementById('source-sio-timeout').value) || 15000;
        
        // 自动发现开关
        const autoDiscoverEl = document.getElementById('source-sio-auto-discover');
        params.auto_discover = autoDiscoverEl ? autoDiscoverEl.checked : true;
        
        if (!params.url) {
            alert('请输入 Socket.IO 服务器地址');
            return;
        }
        if (!params.event) {
            alert('请输入要监听的事件名称（可先通过测试按钮自动发现）');
            return;
        }
    } else if (type === 'variable') {
        // 指令变量数据源配置 - 选择已创建的指令
        const hostId = document.getElementById('source-ssh-host').value;
        const cmdIdx = document.getElementById('source-ssh-cmd').value;
        
        if (!hostId) {
            alert('请选择 SSH 主机');
            return;
        }
        if (cmdIdx === '') {
            alert('请选择 SSH 指令');
            return;
        }
        
        // 获取选中的命令配置
        const cmd = sshCommands[hostId]?.[parseInt(cmdIdx)];
        if (!cmd) {
            alert('指令不存在，请重新选择');
            return;
        }
        
        // 使用命令的 varName 或 name 作为变量前缀
        const varName = cmd.varName || cmd.name;
        
        // SSH 命令配置 - 从已创建的指令中获取
        params.ssh_host_id = hostId;
        params.ssh_command = cmd.command;
        params.var_prefix = varName + '.';  // 变量前缀
        params.var_watch_all = true;         // 监视所有生成的变量
        
        // 高级选项（从命令配置中获取）
        if (cmd.expectPattern) params.ssh_expect_pattern = cmd.expectPattern;
        if (cmd.failPattern) params.ssh_fail_pattern = cmd.failPattern;
        if (cmd.extractPattern) params.ssh_extract_pattern = cmd.extractPattern;
        if (cmd.timeout && cmd.timeout !== 30) params.ssh_timeout = cmd.timeout;
        
        // 执行间隔（秒转毫秒）
        params.poll_interval_ms = (parseInt(document.getElementById('source-var-interval').value) || 60) * 1000;
    }
    
    try {
        const result = await api.call('automation.sources.add', params);
        if (result.code === 0) {
            showToast(`数据源 ${id} 创建成功`, 'success');
            closeModal('add-source-modal');
            await Promise.all([refreshSources(), refreshAutomationStatus()]);
        } else {
            showToast(`创建数据源失败: ${result.message}`, 'error');
        }
    } catch (error) {
        showToast(`创建数据源失败: ${error.message}`, 'error');
    }
}

/**
 * 显示添加/编辑规则模态框
 * @param {object} ruleData - 编辑时传入现有规则数据，添加时为 null
 */
function showAddRuleModal(ruleData = null) {
    const isEdit = !!ruleData;
    
    // 移除可能存在的旧模态框
    const oldModal = document.getElementById('add-rule-modal');
    if (oldModal) oldModal.remove();
    
    // 重置计数器
    conditionRowCount = 0;
    actionRowCount = 0;
    
    const modal = document.createElement('div');
    modal.id = 'add-rule-modal';
    modal.className = 'modal';
    modal.innerHTML = `
        <div class="modal-content automation-modal wide">
            <div class="modal-header">
                <h3>${isEdit ? '✏️ 编辑规则' : '➕ 添加规则'}</h3>
                <button class="modal-close" onclick="closeModal('add-rule-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <!-- 基本信息 -->
                <div class="form-row three-col">
                    <div class="form-group">
                        <label>规则 ID <span class="required">*</span></label>
                        <input type="text" id="rule-id" class="input" placeholder="唯一标识符" ${isEdit ? 'readonly style="background:var(--bg-color)"' : ''}>
                    </div>
                    <div class="form-group" style="flex:2">
                        <label>规则名称 <span class="required">*</span></label>
                        <input type="text" id="rule-name" class="input" placeholder="规则显示名称">
                    </div>
                </div>
                
                <!-- 图标选择 -->
                <div class="form-group">
                    <label>图标</label>
                    <div class="icon-type-tabs">
                        <button type="button" class="icon-tab active" onclick="switchRuleIconType('emoji')">😀 Emoji</button>
                        <button type="button" class="icon-tab" onclick="switchRuleIconType('image')">🖼️ 图片</button>
                    </div>
                    <div id="rule-icon-emoji-picker" class="icon-picker">
                        <div class="emoji-custom-input">
                            <input type="text" id="rule-emoji-input" class="input" placeholder="输入或粘贴 emoji" maxlength="8" onchange="selectRuleIconFromInput()" style="width:100px;text-align:center;font-size:1.2em">
                        </div>
                        ${['⚡','🔔','💡','🔌','🌡️','⏰','📊','🎯','🚀','⚙️','🔧','🎵','📱','🖥️','🌐','🔒','🛡️','📝','🎬','🔄'].map(e => 
                            `<button type="button" class="icon-btn${e === '⚡' ? ' selected' : ''}" onclick="selectRuleIcon('${e}')">${e}</button>`
                        ).join('')}
                    </div>
                    <div id="rule-icon-image-picker" class="icon-image-picker hidden">
                        <div class="icon-preview-row">
                            <div id="rule-icon-preview" class="icon-image-preview">
                                <span class="preview-placeholder">无</span>
                            </div>
                            <div class="icon-path-input">
                                <input type="text" id="rule-icon-path" readonly placeholder="选择图片...">
                                <button type="button" class="btn btn-sm" onclick="browseRuleIconImage()">📂 浏览</button>
                                <button type="button" class="btn btn-sm" onclick="clearRuleIconImage()">✕</button>
                            </div>
                        </div>
                    </div>
                    <input type="hidden" id="rule-icon" value="⚡">
                    <input type="hidden" id="rule-icon-type" value="emoji">
                </div>
                
                <div class="form-row three-col">
                    <div class="form-group">
                        <label>条件逻辑</label>
                        <select id="rule-logic" class="input">
                            <option value="and">全部满足 (AND)</option>
                            <option value="or">任一满足 (OR)</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>冷却时间 (ms)</label>
                        <input type="number" id="rule-cooldown" class="input" value="0" min="0">
                    </div>
                    <label class="checkbox-label" style="padding-top:24px">
                        <input type="checkbox" id="rule-enabled" checked>
                        <span>立即启用</span>
                    </label>
                </div>
                
                <!-- 条件配置 -->
                <div class="config-section">
                    <div class="config-header">
                        <span class="config-title">📋 触发条件</span>
                        <div style="display:flex;gap:8px;align-items:center">
                            <label class="checkbox-label" style="margin:0;padding:0">
                                <input type="checkbox" id="rule-manual-only" onchange="toggleManualOnly()">
                                <span>仅手动触发</span>
                            </label>
                            <button class="btn btn-sm btn-primary" id="add-condition-btn" onclick="addConditionRow()">➕ 添加</button>
                        </div>
                    </div>
                    <div id="conditions-container">
                        <p class="empty-hint">点击"添加"创建触发条件，或勾选"仅手动触发"作为快捷动作</p>
                    </div>
                </div>
                
                <!-- 动作配置 -->
                <div class="config-section">
                    <div class="config-header">
                        <span class="config-title">⚡ 执行动作</span>
                        <button class="btn btn-sm btn-primary" onclick="addActionTemplateRow()">➕ 添加</button>
                    </div>
                    <div id="actions-container">
                        <p class="empty-hint">从已创建的动作模板中选择要执行的动作</p>
                    </div>
                    <small class="form-hint" style="display:block;margin-top:8px;">
                        💡 请先在"动作模板"区域创建动作，然后在这里选择使用
                    </small>
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn" onclick="closeModal('add-rule-modal')">取消</button>
                <button class="btn btn-primary" onclick="submitAddRule(${isEdit ? "'" + ruleData.id + "'" : ''})">${isEdit ? '保存修改' : '添加规则'}</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    setTimeout(() => modal.classList.add('show'), 10);
    
    // 如果是编辑模式，填充现有数据
    if (isEdit && ruleData) {
        document.getElementById('rule-id').value = ruleData.id;
        document.getElementById('rule-name').value = ruleData.name || '';
        document.getElementById('rule-logic').value = ruleData.logic || 'and';
        document.getElementById('rule-cooldown').value = ruleData.cooldown_ms || 0;
        document.getElementById('rule-enabled').checked = ruleData.enabled !== false;
        
        // 填充图标
        const icon = ruleData.icon || '⚡';
        document.getElementById('rule-icon').value = icon;
        if (icon.startsWith('/sdcard/')) {
            document.getElementById('rule-icon-type').value = 'image';
            document.getElementById('rule-icon-path').value = icon;
            switchRuleIconType('image');
            updateRuleIconPreview(icon);
        } else {
            document.getElementById('rule-icon-type').value = 'emoji';
            selectRuleIcon(icon);
        }
        
        // 填充条件
        if (ruleData.conditions && ruleData.conditions.length > 0) {
            ruleData.conditions.forEach(cond => {
                addConditionRow(cond.variable, cond.operator, cond.value);
            });
        }
        
        // 填充手动触发标记（在填充条件之后设置，以便正确更新 UI）
        if (ruleData.manual_trigger) {
            document.getElementById('rule-manual-only').checked = true;
            toggleManualOnly();  // 更新 UI 状态
        }
        
        // 填充动作
        if (ruleData.actions && ruleData.actions.length > 0) {
            // 异步加载动作模板行
            (async () => {
                for (const act of ruleData.actions) {
                    await addActionTemplateRow(
                        act.template_id, 
                        act.delay_ms || 0,
                        act.repeat_mode || 'once',
                        act.repeat_count || 1,
                        act.repeat_interval_ms || 1000,
                        act.condition || null
                    );
                }
            })();
        }
    }
}

/**
 * 切换仅手动触发模式
 */
function toggleManualOnly() {
    const checked = document.getElementById('rule-manual-only').checked;
    const addBtn = document.getElementById('add-condition-btn');
    const container = document.getElementById('conditions-container');
    
    if (checked) {
        // 禁用添加条件按钮，清空现有条件
        addBtn.disabled = true;
        addBtn.style.opacity = '0.5';
        container.innerHTML = '<p class="empty-hint" style="color:var(--secondary-color)">👆 此规则仅可通过手动触发按钮执行</p>';
    } else {
        // 启用添加条件按钮
        addBtn.disabled = false;
        addBtn.style.opacity = '1';
        container.innerHTML = '<p class="empty-hint">点击"添加"创建触发条件</p>';
    }
}

// ==================== 规则图标选择 ====================

function switchRuleIconType(type) {
    const emojiPicker = document.getElementById('rule-icon-emoji-picker');
    const imagePicker = document.getElementById('rule-icon-image-picker');
    const tabs = document.querySelectorAll('#add-rule-modal .icon-type-tabs .icon-tab');
    
    tabs.forEach(tab => tab.classList.remove('active'));
    
    if (type === 'image') {
        emojiPicker.classList.add('hidden');
        imagePicker.classList.remove('hidden');
        tabs[1]?.classList.add('active');
        document.getElementById('rule-icon-type').value = 'image';
    } else {
        emojiPicker.classList.remove('hidden');
        imagePicker.classList.add('hidden');
        tabs[0]?.classList.add('active');
        document.getElementById('rule-icon-type').value = 'emoji';
    }
}

function selectRuleIcon(icon) {
    document.getElementById('rule-icon').value = icon;
    document.getElementById('rule-icon-type').value = 'emoji';
    document.getElementById('rule-emoji-input').value = icon;
    document.querySelectorAll('#add-rule-modal .icon-btn').forEach(btn => {
        btn.classList.toggle('selected', btn.textContent === icon);
    });
}

function selectRuleIconFromInput() {
    const input = document.getElementById('rule-emoji-input');
    const icon = input.value.trim();
    if (icon) {
        document.getElementById('rule-icon').value = icon;
        document.getElementById('rule-icon-type').value = 'emoji';
        // 取消预设按钮的选中状态
        document.querySelectorAll('#add-rule-modal .icon-btn').forEach(btn => {
            btn.classList.remove('selected');
        });
    }
}

async function browseRuleIconImage() {
    filePickerCurrentPath = '/sdcard/images';
    filePickerSelectedFile = null;
    filePickerCallback = (path) => {
        document.getElementById('rule-icon').value = path;
        document.getElementById('rule-icon-path').value = path;
        updateRuleIconPreview(path);
    };
    document.getElementById('file-picker-modal').classList.remove('hidden');
    await loadFilePickerDirectory(filePickerCurrentPath);
}

function updateRuleIconPreview(path) {
    const preview = document.getElementById('rule-icon-preview');
    if (path && path.startsWith('/sdcard/')) {
        preview.innerHTML = `<img src="/api/v1/file/download?path=${encodeURIComponent(path)}" alt="icon" onerror="this.parentElement.innerHTML='<span class=\\'preview-placeholder\\'>加载失败</span>'">`;
    } else {
        preview.innerHTML = '<span class="preview-placeholder">无</span>';
    }
}

function clearRuleIconImage() {
    document.getElementById('rule-icon').value = '⚡';
    document.getElementById('rule-icon-path').value = '';
    document.getElementById('rule-icon-type').value = 'emoji';
    updateRuleIconPreview(null);
    switchRuleIconType('emoji');
}

// 条件行计数器
let conditionRowCount = 0;

/**
 * 添加条件行
 * @param {string} variable - 预填充变量名
 * @param {string} operator - 预填充操作符
 * @param {any} value - 预填充比较值
 */
function addConditionRow(variable = '', operator = 'eq', value = '') {
    const container = document.getElementById('conditions-container');
    
    // 移除空提示
    const emptyP = container.querySelector('.empty-hint');
    if (emptyP) emptyP.remove();
    
    // 取消仅手动触发勾选
    const manualOnly = document.getElementById('rule-manual-only');
    if (manualOnly && manualOnly.checked) {
        manualOnly.checked = false;
        toggleManualOnly();
    }
    
    // 处理值显示
    let displayValue = value;
    if (typeof value === 'object') {
        displayValue = JSON.stringify(value);
    } else if (typeof value === 'boolean') {
        displayValue = value ? 'true' : 'false';
    }
    
    const rowId = conditionRowCount;
    const row = document.createElement('div');
    row.className = 'condition-row';
    row.id = `condition-row-${rowId}`;
    row.innerHTML = `
        <button class="btn btn-sm btn-secondary cond-variable-btn" 
                onclick="openConditionVarSelector(${rowId})" 
                title="选择变量"
                style="min-width:140px;text-align:left;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">
            ${variable || '📊 选择变量'}
        </button>
        <input type="hidden" class="cond-variable" value="${variable}">
        <select class="input cond-operator">
            <option value="eq" ${operator === 'eq' ? 'selected' : ''}>== 等于</option>
            <option value="ne" ${operator === 'ne' ? 'selected' : ''}>!= 不等于</option>
            <option value="gt" ${operator === 'gt' ? 'selected' : ''}>> 大于</option>
            <option value="ge" ${operator === 'ge' ? 'selected' : ''}>>=  大于等于</option>
            <option value="lt" ${operator === 'lt' ? 'selected' : ''}>< 小于</option>
            <option value="le" ${operator === 'le' ? 'selected' : ''}><=  小于等于</option>
            <option value="changed" ${operator === 'changed' ? 'selected' : ''}>值变化</option>
            <option value="contains" ${operator === 'contains' ? 'selected' : ''}>包含</option>
        </select>
        <input type="text" class="input cond-value" placeholder="比较值" value="${displayValue}">
        <button class="btn btn-sm btn-danger" onclick="this.parentElement.remove()">✕</button>
    `;
    
    container.appendChild(row);
    conditionRowCount++;
}

// 用于存储当前正在配置的条件行 ID
let currentConditionVarRowId = null;

/**
 * 打开触发条件变量选择器
 */
async function openConditionVarSelector(rowId) {
    currentConditionVarRowId = rowId;
    
    // 移除旧的模态框
    const oldModal = document.getElementById('variable-select-modal');
    if (oldModal) oldModal.remove();
    
    const modal = document.createElement('div');
    modal.id = 'variable-select-modal';
    modal.className = 'modal show';
    modal.dataset.callback = 'ruleCondition';
    modal.onclick = (e) => { if (e.target === modal) closeModal('variable-select-modal');
                    };
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:550px;">
            <div class="modal-header">
                <h3>📊 选择触发条件变量</h3>
                <button class="modal-close" onclick="closeModal('variable-select-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <div style="margin-bottom:12px;">
                    <input type="text" id="var-search" class="input" placeholder="🔍 搜索变量..." 
                           oninput="filterVariableList(this.value)" style="width:100%;">
                </div>
                <div id="variable-select-loading" style="text-align:center;padding:20px;">
                    <div class="spinner"></div>
                    <p>加载变量列表...</p>
                </div>
                <div id="variable-select-list" style="display:none;max-height:400px;overflow-y:auto;"></div>
                <div id="variable-select-empty" style="display:none;text-align:center;padding:30px;color:var(--text-light);">
                    <div style="font-size:48px;margin-bottom:10px;">📭</div>
                    <p>没有可用的变量</p>
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn btn-sm" onclick="closeModal('variable-select-modal')">关闭</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // 加载变量列表
    try {
        const result = await api.call('automation.variables.list');
        const variables = result.data?.variables || [];
        
        document.getElementById('variable-select-loading').style.display = 'none';
        
        if (variables.length === 0) {
            document.getElementById('variable-select-empty').style.display = 'block';
            return;
        }
        
        // 按数据源分组
        const grouped = {};
        variables.forEach(v => {
            const sourceId = v.source_id || '_system';
            if (!grouped[sourceId]) grouped[sourceId] = [];
            grouped[sourceId].push(v);
        });
        
        const listEl = document.getElementById('variable-select-list');
        listEl.style.display = 'block';
        
        let html = '';
        for (const [sourceId, vars] of Object.entries(grouped)) {
            const groupId = `var-group-${sourceId.replace(/[^a-zA-Z0-9]/g, '_')}`;
            html += `<div class="var-group" data-source="${sourceId}">
                <div class="var-group-header" style="padding:10px 12px;background:var(--bg-elevated);font-weight:600;border-bottom:1px solid var(--border);cursor:pointer;display:flex;align-items:center;justify-content:space-between;"
                     onclick="toggleVarGroup('${groupId}')">
                    <span>📦 ${sourceId === '_system' ? '系统变量' : sourceId} <span style="font-weight:normal;color:var(--text-light);">(${vars.length})</span></span>
                    <span class="var-group-arrow" id="${groupId}-arrow" style="transition:transform 0.2s;">▶</span>
                </div>
                <div class="var-group-items" id="${groupId}" style="display:none;">`;
            
            vars.forEach(v => {
                const typeIcon = { 'bool': '🔘', 'int': '🔢', 'float': '📊', 'string': '📝' }[v.type] || '📋';
                const displayValue = v.value !== undefined ? String(v.value).substring(0, 30) : '-';
                html += `
                    <div class="var-select-item" data-name="${v.name}" data-source="${sourceId}" style="
                        display:flex;align-items:center;padding:10px 12px;padding-left:24px;
                        border-bottom:1px solid var(--border);cursor:pointer;
                        transition:background 0.2s;"
                        onmouseover="this.style.background='var(--bg-hover)'"
                        onmouseout="this.style.background='transparent'"
                        onclick="selectVariable('${v.name}')">
                        <span style="font-size:18px;margin-right:10px;">${typeIcon}</span>
                        <div style="flex:1;min-width:0;">
                            <div style="font-weight:500;font-family:monospace;">${v.name}</div>
                            <small style="color:var(--text-light);">当前值: ${displayValue}${displayValue.length >= 30 ? '...' : ''}</small>
                        </div>
                    </div>
                `;
            });
            html += '</div></div>';
        }
        
        listEl.innerHTML = html;
        
        // 聚焦搜索框
        setTimeout(() => document.getElementById('var-search')?.focus(), 100);
        
    } catch (e) {
        console.error('加载变量列表失败:', e);
        document.getElementById('variable-select-loading').innerHTML = `
            <div style="color:var(--danger);">
                <p>加载失败: ${e.message}</p>
                <button class="btn btn-sm" onclick="closeModal('variable-select-modal')">关闭</button>
            </div>
        `;
    }
}

/**
 * 处理触发条件变量选择
 */
function handleConditionVarSelect(varName) {
    if (currentConditionVarRowId === null) return;
    
    const row = document.getElementById(`condition-row-${currentConditionVarRowId}`);
    if (!row) return;
    
    const varBtn = row.querySelector('.cond-variable-btn');
    const varInput = row.querySelector('.cond-variable');
    
    if (varBtn) varBtn.textContent = varName;
    if (varInput) varInput.value = varName;
    
    // 关闭模态框
    closeModal('variable-select-modal');
    
    currentConditionVarRowId = null;
}

// 动作行计数器
let actionRowCount = 0;

// 缓存的动作模板列表
let cachedActionTemplates = [];

/**
 * 加载动作模板列表
 */
async function loadActionTemplatesForRule() {
    try {
        const result = await api.call('automation.actions.list', {});
        if (result.code === 0 && result.data?.templates) {
            cachedActionTemplates = result.data.templates;
        }
    } catch (e) {
        console.error('加载动作模板失败:', e);
        cachedActionTemplates = [];
    }
}

/**
 * 添加动作模板选择行
 * @param {string} templateId - 预选中的模板 ID
 * @param {number} delayMs - 预填充的延迟时间
 * @param {string} repeatMode - 重复模式: 'once' | 'while_true' | 'count'
 * @param {number} repeatCount - 重复次数（当 repeatMode='count' 时）
 * @param {number} repeatIntervalMs - 重复间隔毫秒
 * @param {Object|null} condition - 动作条件配置 {variable, operator, value}
 */
async function addActionTemplateRow(templateId = '', delayMs = 0, repeatMode = 'once', repeatCount = 1, repeatIntervalMs = 1000, condition = null) {
    const container = document.getElementById('actions-container');
    
    // 先加载模板列表
    await loadActionTemplatesForRule();
    
    if (cachedActionTemplates.length === 0) {
        showToast('请先创建动作模板', 'warning');
        return;
    }
    
    // 移除空提示
    const emptyP = container.querySelector('.empty-hint');
    if (emptyP) emptyP.remove();
    
    const row = document.createElement('div');
    row.className = 'action-row template-select-row';
    row.id = `action-row-${actionRowCount}`;
    
    // 构建模板选项
    let optionsHtml = '<option value="">-- 选择动作模板 --</option>';
    cachedActionTemplates.forEach(tpl => {
        const typeLabel = getActionTypeLabel(tpl.type);
        const selected = tpl.id === templateId ? 'selected' : '';
        optionsHtml += `<option value="${tpl.id}" ${selected}>${tpl.name || tpl.id} (${typeLabel})</option>`;
    });
    
    const rowId = actionRowCount;
    const showRepeatOptions = repeatMode !== 'once';
    const hasCondition = condition && condition.variable;
    
    row.innerHTML = `
        <div style="display:flex;flex-direction:column;gap:8px;flex:1;">
            <div style="display:flex;gap:8px;align-items:center;">
                <select class="input action-template-id" onchange="updateActionTemplatePreview(this)" style="flex:2">
                    ${optionsHtml}
                </select>
                <button class="btn btn-sm btn-danger" onclick="this.closest('.action-row').remove()">✕</button>
            </div>
            <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;">
                <label style="display:flex;align-items:center;gap:4px;font-size:12px;color:var(--text-light);">
                    ⏱️ 延迟
                    <input type="number" class="input action-delay" placeholder="0" value="${delayMs}" min="0" style="width:70px;padding:4px 6px;">
                    <span>ms</span>
                </label>
                <label style="display:flex;align-items:center;gap:4px;font-size:12px;color:var(--text-light);">
                    🔄 执行
                    <select class="input action-repeat-mode" onchange="toggleRepeatOptions(${rowId})" style="padding:4px 6px;">
                        <option value="once" ${repeatMode === 'once' ? 'selected' : ''}>单次</option>
                        <option value="while_true" ${repeatMode === 'while_true' ? 'selected' : ''}>条件持续时重复</option>
                        <option value="count" ${repeatMode === 'count' ? 'selected' : ''}>指定次数</option>
                    </select>
                </label>
                <span class="repeat-options" id="repeat-options-${rowId}" style="display:${showRepeatOptions ? 'flex' : 'none'};gap:8px;align-items:center;">
                    <label class="repeat-count-label" style="display:${repeatMode === 'count' ? 'flex' : 'none'};align-items:center;gap:4px;font-size:12px;color:var(--text-light);">
                        次数
                        <input type="number" class="input action-repeat-count" value="${repeatCount}" min="1" max="100" style="width:50px;padding:4px 6px;">
                    </label>
                    <label style="display:flex;align-items:center;gap:4px;font-size:12px;color:var(--text-light);">
                        间隔
                        <input type="number" class="input action-repeat-interval" value="${repeatIntervalMs}" min="100" style="width:70px;padding:4px 6px;">
                        <span>ms</span>
                    </label>
                </span>
            </div>
            <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;">
                <label style="display:flex;align-items:center;gap:4px;font-size:12px;color:var(--text-light);">
                    <input type="checkbox" class="action-has-condition" onchange="toggleActionCondition(${rowId})" ${hasCondition ? 'checked' : ''}>
                    🎯 执行条件
                </label>
                <span class="action-condition-fields" id="action-condition-${rowId}" style="display:${hasCondition ? 'flex' : 'none'};gap:6px;align-items:center;">
                    <button class="btn btn-xs btn-secondary action-condition-var-btn" 
                            onclick="openActionConditionVarSelector(${rowId})" 
                            title="选择变量"
                            style="padding:2px 6px;font-size:11px;">
                        ${hasCondition && condition.variable ? condition.variable : '选择变量'}
                    </button>
                    <input type="hidden" class="action-condition-variable" value="${hasCondition ? condition.variable : ''}">
                    <select class="input action-condition-operator" style="padding:4px 6px;width:80px;">
                        <option value="eq" ${hasCondition && condition.operator === 'eq' ? 'selected' : ''}>=</option>
                        <option value="ne" ${hasCondition && condition.operator === 'ne' ? 'selected' : ''}>≠</option>
                        <option value="gt" ${hasCondition && condition.operator === 'gt' ? 'selected' : ''}>&gt;</option>
                        <option value="ge" ${hasCondition && condition.operator === 'ge' ? 'selected' : ''}>≥</option>
                        <option value="lt" ${hasCondition && condition.operator === 'lt' ? 'selected' : ''}>&lt;</option>
                        <option value="le" ${hasCondition && condition.operator === 'le' ? 'selected' : ''}>≤</option>
                    </select>
                    <input type="text" class="input action-condition-value" 
                           placeholder="值" value="${hasCondition ? condition.value : ''}" 
                           style="width:80px;padding:4px 6px;">
                </span>
            </div>
        </div>
    `;
    
    container.appendChild(row);
    actionRowCount++;
}

/**
 * 获取动作类型标签
 */
function getActionTypeLabel(type) {
    const labels = {
        'cli': 'CLI',
        'ssh_cmd_ref': 'SSH',
        'led': 'LED',
        'log': '日志',
        'set_var': '变量',
        'webhook': 'Webhook',
        'gpio': 'GPIO',
        'device_ctrl': '设备'
    };
    return labels[type] || type;
}

/**
 * 切换重复执行选项的显示
 */
function toggleRepeatOptions(rowId) {
    const row = document.getElementById(`action-row-${rowId}`);
    if (!row) return;
    
    const modeSelect = row.querySelector('.action-repeat-mode');
    const repeatOptions = document.getElementById(`repeat-options-${rowId}`);
    const countLabel = repeatOptions?.querySelector('.repeat-count-label');
    
    if (!modeSelect || !repeatOptions) return;
    
    const mode = modeSelect.value;
    
    if (mode === 'once') {
        repeatOptions.style.display = 'none';
    } else {
        repeatOptions.style.display = 'flex';
        if (countLabel) {
            countLabel.style.display = mode === 'count' ? 'flex' : 'none';
        }
    }
}

/**
 * 更新动作模板预览（可选）
 */
function updateActionTemplatePreview(selectElement) {
    const templateId = selectElement.value;
    if (!templateId) return;
    
    const tpl = cachedActionTemplates.find(t => t.id === templateId);
    if (tpl) {
        console.log('选择动作模板:', tpl);
    }
}

/**
 * 切换动作条件配置的显示
 */
function toggleActionCondition(rowId) {
    const row = document.getElementById(`action-row-${rowId}`);
    if (!row) return;
    
    const checkbox = row.querySelector('.action-has-condition');
    const conditionFields = document.getElementById(`action-condition-${rowId}`);
    
    if (!checkbox || !conditionFields) return;
    
    conditionFields.style.display = checkbox.checked ? 'flex' : 'none';
}

// 用于存储当前正在配置条件的动作行 ID
let currentConditionRowId = null;

/**
 * 打开动作条件变量选择器
 */
async function openActionConditionVarSelector(rowId) {
    currentConditionRowId = rowId;
    
    // 设置回调模式标记
    window._actionConditionMode = true;
    
    // 复用现有的变量选择模态框（通过创建一个临时 input）
    // showVariableSelectModal 需要一个 inputId，我们用特殊标记来识别
    await showVariableSelectModalForCondition();
}

/**
 * 为动作条件显示变量选择模态框
 */
async function showVariableSelectModalForCondition() {
    // 移除旧的模态框
    const oldModal = document.getElementById('variable-select-modal');
    if (oldModal) oldModal.remove();
    
    const modal = document.createElement('div');
    modal.id = 'variable-select-modal';
    modal.className = 'modal show';
    modal.dataset.callback = 'actionCondition';
    modal.onclick = (e) => { if (e.target === modal) closeModal('variable-select-modal');
                    };
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:550px;">
            <div class="modal-header">
                <h3>📊 选择条件变量</h3>
                <button class="modal-close" onclick="closeModal('variable-select-modal')">&times;</button>
            </div>
            <div class="modal-body">
                <div style="margin-bottom:12px;">
                    <input type="text" id="var-search" class="input" placeholder="🔍 搜索变量..." 
                           oninput="filterVariableList(this.value)" style="width:100%;">
                </div>
                <div id="variable-select-loading" style="text-align:center;padding:20px;">
                    <div class="spinner"></div>
                    <p>加载变量列表...</p>
                </div>
                <div id="variable-select-list" style="display:none;max-height:400px;overflow-y:auto;"></div>
                <div id="variable-select-empty" style="display:none;text-align:center;padding:30px;color:var(--text-light);">
                    <div style="font-size:48px;margin-bottom:10px;">📭</div>
                    <p>没有可用的变量</p>
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn btn-sm" onclick="closeModal('variable-select-modal')">关闭</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // 加载变量列表
    try {
        const result = await api.call('automation.variables.list');
        const variables = result.data?.variables || [];
        
        document.getElementById('variable-select-loading').style.display = 'none';
        
        if (variables.length === 0) {
            document.getElementById('variable-select-empty').style.display = 'block';
            return;
        }
        
        // 按数据源分组
        const grouped = {};
        variables.forEach(v => {
            const sourceId = v.source_id || '_system';
            if (!grouped[sourceId]) grouped[sourceId] = [];
            grouped[sourceId].push(v);
        });
        
        const listEl = document.getElementById('variable-select-list');
        listEl.style.display = 'block';
        
        let html = '';
        for (const [sourceId, vars] of Object.entries(grouped)) {
            const groupId = `var-group-${sourceId.replace(/[^a-zA-Z0-9]/g, '_')}`;
            html += `<div class="var-group" data-source="${sourceId}">
                <div class="var-group-header" style="padding:10px 12px;background:var(--bg-elevated);font-weight:600;border-bottom:1px solid var(--border);cursor:pointer;display:flex;align-items:center;justify-content:space-between;"
                     onclick="toggleVarGroup('${groupId}')">
                    <span>📦 ${sourceId === '_system' ? '系统变量' : sourceId} <span style="font-weight:normal;color:var(--text-light);">(${vars.length})</span></span>
                    <span class="var-group-arrow" id="${groupId}-arrow" style="transition:transform 0.2s;">▶</span>
                </div>
                <div class="var-group-items" id="${groupId}" style="display:none;">`;
            
            vars.forEach(v => {
                const typeIcon = { 'bool': '🔘', 'int': '🔢', 'float': '📊', 'string': '📝' }[v.type] || '📋';
                const displayValue = v.value !== undefined ? String(v.value).substring(0, 30) : '-';
                html += `
                    <div class="var-select-item" data-name="${v.name}" data-source="${sourceId}" style="
                        display:flex;align-items:center;padding:10px 12px;padding-left:24px;
                        border-bottom:1px solid var(--border);cursor:pointer;
                        transition:background 0.2s;"
                        onmouseover="this.style.background='var(--bg-hover)'"
                        onmouseout="this.style.background='transparent'"
                        onclick="selectVariable('${v.name}')">
                        <span style="font-size:18px;margin-right:10px;">${typeIcon}</span>
                        <div style="flex:1;min-width:0;">
                            <div style="font-weight:500;font-family:monospace;">${v.name}</div>
                            <small style="color:var(--text-light);">当前值: ${displayValue}${displayValue.length >= 30 ? '...' : ''}</small>
                        </div>
                    </div>
                `;
            });
            html += '</div></div>';
        }
        
        listEl.innerHTML = html;
        
        // 聚焦搜索框
        setTimeout(() => document.getElementById('var-search')?.focus(), 100);
        
    } catch (e) {
        console.error('加载变量列表失败:', e);
        document.getElementById('variable-select-loading').innerHTML = `
            <div style="color:var(--danger);">
                <p>加载失败: ${e.message}</p>
                <button class="btn btn-sm" onclick="closeModal('variable-select-modal')">关闭</button>
            </div>
        `;
    }
}

/**
 * 处理动作条件变量选择
 */
function handleActionConditionVarSelect(varName) {
    if (currentConditionRowId === null) return;
    
    const row = document.getElementById(`action-row-${currentConditionRowId}`);
    if (!row) return;
    
    const varBtn = row.querySelector('.action-condition-var-btn');
    const varInput = row.querySelector('.action-condition-variable');
    
    if (varBtn) varBtn.textContent = varName;
    if (varInput) varInput.value = varName;
    
    // 关闭模态框
    closeModal('variable-select-modal');
    
    currentConditionRowId = null;
    window._actionConditionMode = false;
}

// 保留旧的 addActionRow 和 updateActionFields 用于兼容，但标记为废弃
/**
 * @deprecated 使用 addActionTemplateRow 代替
 */
function addActionRow() {
    console.warn('addActionRow 已废弃，请使用 addActionTemplateRow');
    addActionTemplateRow();
}

/**
 * 根据动作类型更新参数字段
 */
function updateActionFields(selectElement) {
    const row = selectElement.closest('.action-row');
    const paramsContainer = row.querySelector('.action-params');
    const type = selectElement.value;
    
    switch (type) {
        case 'led':
            paramsContainer.innerHTML = `
                <select class="input action-led-device">
                    <option value="board">Board</option>
                    <option value="matrix">Matrix</option>
                    <option value="touch">Touch</option>
                </select>
                <input type="number" class="input action-led-index" placeholder="索引" value="255" min="0" max="255" style="width:70px">
                <input type="text" class="input action-led-color" placeholder="#RRGGBB" value="#FF0000" style="width:90px">
            `;
            break;
        case 'gpio':
            paramsContainer.innerHTML = `
                <input type="number" class="input action-gpio-pin" placeholder="Pin" value="0" min="0" max="48" style="width:60px">
                <select class="input action-gpio-level">
                    <option value="true">高电平</option>
                    <option value="false">低电平</option>
                </select>
                <input type="number" class="input action-gpio-pulse" placeholder="脉冲ms" value="0" min="0" style="width:80px">
            `;
            break;
        case 'device':
            paramsContainer.innerHTML = `
                <select class="input action-device-name">
                    <option value="agx0">AGX 0</option>
                    <option value="lpmu0">LPMU 0</option>
                </select>
                <select class="input action-device-action">
                    <option value="power_on">开机</option>
                    <option value="power_off">关机</option>
                    <option value="reset">重启</option>
                    <option value="force_off">强制关机</option>
                </select>
            `;
            break;
        case 'set_var':
            paramsContainer.innerHTML = `
                <input type="text" class="input action-setvar-name" placeholder="变量名" style="width:120px">
                <input type="text" class="input action-setvar-value" placeholder="值 (JSON)" style="flex:1">
            `;
            break;
        case 'log':
            paramsContainer.innerHTML = `
                <select class="input action-log-level" style="width:100px">
                    <option value="3">INFO</option>
                    <option value="4">WARN</option>
                    <option value="5">ERROR</option>
                </select>
                <input type="text" class="input action-log-message" placeholder="日志消息" style="flex:1">
            `;
            break;
        case 'webhook':
            paramsContainer.innerHTML = `
                <select class="input action-webhook-method" style="width:80px">
                    <option value="POST">POST</option>
                    <option value="GET">GET</option>
                    <option value="PUT">PUT</option>
                </select>
                <input type="text" class="input action-webhook-url" placeholder="URL" style="flex:1">
                <input type="text" class="input action-webhook-body" placeholder='Body JSON' style="width:120px">
            `;
            break;
    }
}

/**
 * 提交添加/更新规则
 * 规则只引用动作模板 ID，不再内联定义动作
 * @param {string} originalId - 编辑模式时传入原规则 ID
 */
async function submitAddRule(originalId = null) {
    const isEdit = !!originalId;
    const id = document.getElementById('rule-id').value.trim();
    const name = document.getElementById('rule-name').value.trim();
    const icon = document.getElementById('rule-icon').value || '⚡';
    const logic = document.getElementById('rule-logic').value;
    const cooldown = parseInt(document.getElementById('rule-cooldown').value) || 0;
    const enabled = document.getElementById('rule-enabled').checked;
    const manualTrigger = document.getElementById('rule-manual-only')?.checked || false;
    
    if (!id) {
        alert('请输入规则 ID');
        return;
    }
    if (!name) {
        alert('请输入规则名称');
        return;
    }
    
    // 收集条件（仅手动触发时为空数组）
    const conditions = [];
    if (!manualTrigger) {
        document.querySelectorAll('.condition-row').forEach(row => {
            const variable = row.querySelector('.cond-variable').value.trim();
            const operator = row.querySelector('.cond-operator').value;
            let value = row.querySelector('.cond-value').value.trim();
            
            if (variable) {
                // 尝试解析值为 JSON
                try {
                    value = JSON.parse(value);
                } catch (e) {
                    // 保持字符串
                }
                
                conditions.push({ variable, operator, value });
            }
        });
    }
    
    // 收集动作模板引用（包含 template_id、delay_ms、重复选项和动作条件）
    const actions = [];
    document.querySelectorAll('.action-row').forEach(row => {
        const templateId = row.querySelector('.action-template-id')?.value;
        const delay_ms = parseInt(row.querySelector('.action-delay')?.value) || 0;
        const repeat_mode = row.querySelector('.action-repeat-mode')?.value || 'once';
        const repeat_count = parseInt(row.querySelector('.action-repeat-count')?.value) || 1;
        const repeat_interval_ms = parseInt(row.querySelector('.action-repeat-interval')?.value) || 1000;
        
        // 收集动作条件
        const hasCondition = row.querySelector('.action-has-condition')?.checked;
        const condVariable = row.querySelector('.action-condition-variable')?.value?.trim();
        const condOperator = row.querySelector('.action-condition-operator')?.value;
        const condValueRaw = row.querySelector('.action-condition-value')?.value?.trim();
        
        if (templateId) {
            const actionRef = {
                template_id: templateId,
                delay_ms: delay_ms
            };
            
            // 只有非单次执行时才添加重复参数
            if (repeat_mode !== 'once') {
                actionRef.repeat_mode = repeat_mode;
                actionRef.repeat_interval_ms = repeat_interval_ms;
                if (repeat_mode === 'count') {
                    actionRef.repeat_count = repeat_count;
                }
            }
            
            // 添加动作条件
            if (hasCondition && condVariable) {
                // 尝试解析条件值
                let condValue = condValueRaw;
                try {
                    condValue = JSON.parse(condValueRaw);
                } catch (e) {
                    // 保持字符串
                }
                
                actionRef.condition = {
                    variable: condVariable,
                    operator: condOperator,
                    value: condValue
                };
            }
            
            actions.push(actionRef);
        }
    });
    
    if (actions.length === 0) {
        alert('请至少选择一个动作模板');
        return;
    }
    
    const params = {
        id,
        name,
        icon,
        logic,
        cooldown_ms: cooldown,
        enabled,
        manual_trigger: manualTrigger,
        conditions,
        actions
    };
    
    try {
        // 编辑模式：先删除旧规则，再创建新规则
        if (isEdit) {
            await api.call('automation.rules.delete', { id: originalId });
        }
        
        const result = await api.call('automation.rules.add', params);
        if (result.code === 0) {
            showToast(`规则 ${id} ${isEdit ? '更新' : '创建'}成功`, 'success');
            closeModal('add-rule-modal');
            await Promise.all([refreshRules(), refreshAutomationStatus()]);
        } else {
            showToast(`${isEdit ? '更新' : '创建'}规则失败: ${result.message}`, 'error');
        }
    } catch (error) {
        showToast(`${isEdit ? '更新' : '创建'}规则失败: ${error.message}`, 'error');
    }
}

/**
 * 关闭模态框
 */
function closeModal(modalId) {
    const modal = document.getElementById(modalId);
    if (modal) {
        modal.classList.remove('show');
        setTimeout(() => modal.remove(), 300);
    }
}

// 导出自动化页面函数
window.refreshAutomationStatus = refreshAutomationStatus;
window.automationControl = automationControl;
window.refreshRules = refreshRules;
window.toggleRule = toggleRule;
window.triggerRule = triggerRule;
window.deleteRule = deleteRule;
window.editRule = editRule;
window.refreshSources = refreshSources;
window.toggleSource = toggleSource;
window.deleteSource = deleteSource;
window.showAddSourceModal = showAddSourceModal;
window.switchSourceType = switchSourceType;
window.updateSourceTypeFields = updateSourceTypeFields;
window.loadSshHostsForSource = loadSshHostsForSource;
window.onSshHostChangeForSource = onSshHostChangeForSource;
window.onSshCmdChange = onSshCmdChange;
window.submitAddSource = submitAddSource;
window.showAddRuleModal = showAddRuleModal;
window.addConditionRow = addConditionRow;
window.openConditionVarSelector = openConditionVarSelector;
window.handleConditionVarSelect = handleConditionVarSelect;
window.addActionRow = addActionRow;
window.updateActionFields = updateActionFields;
window.submitAddRule = submitAddRule;
window.closeModal = closeModal;
window.toggleManualOnly = toggleManualOnly;
window.toggleRepeatOptions = toggleRepeatOptions;
window.toggleActionCondition = toggleActionCondition;
window.openActionConditionVarSelector = openActionConditionVarSelector;
window.handleActionConditionVarSelect = handleActionConditionVarSelect;
window.showVariableSelectModalForCondition = showVariableSelectModalForCondition;
// 动作模板管理
window.refreshActions = refreshActions;
window.showAddActionModal = showAddActionModal;
window.updateActionTypeFields = updateActionTypeFields;
window.submitAction = submitAction;
window.testAction = testAction;
window.editAction = editAction;
window.deleteAction = deleteAction;
window.showImageSelectModal = showImageSelectModal;
window.selectImageItem = selectImageItem;
window.browseActionImages = browseActionImages;
window.browseActionQrBg = browseActionQrBg;
window.showVariableSelectModal = showVariableSelectModal;
window.filterVariableList = filterVariableList;
window.selectVariable = selectVariable;
window.toggleVarGroup = toggleVarGroup;
// 规则图标相关
window.switchRuleIconType = switchRuleIconType;
window.selectRuleIcon = selectRuleIcon;
window.selectRuleIconFromInput = selectRuleIconFromInput;
window.browseRuleIconImage = browseRuleIconImage;
window.clearRuleIconImage = clearRuleIconImage;
window.updateRuleIconPreview = updateRuleIconPreview;
// 快捷操作
window.refreshQuickActions = refreshQuickActions;
window.triggerQuickAction = triggerQuickAction;

/*===========================================================================*/
/*              Automation Export/Import Functions                             */
/*===========================================================================*/

/**
 * 显示导出数据源配置模态框
 */
function showExportSourceModal(sourceId) {
    let modal = document.getElementById('export-source-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'export-source-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📤 导出数据源配置</h2>
            <p style="color:#666;font-size:0.9rem">导出数据源 <strong>${escapeHtml(sourceId)}</strong> 的配置为加密配置包</p>
            
            <div class="form-group">
                <label>目标设备证书 (PEM)</label>
                <textarea id="export-source-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:120px;font-family:monospace;font-size:11px"></textarea>
                <div style="font-size:0.85em;color:#666;margin-top:4px">💡 粘贴目标设备的证书。留空则使用本机证书（自加密）</div>
            </div>
            
            <div id="export-source-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideExportSourceModal()">取消</button>
                <button class="btn btn-primary" id="export-source-btn" onclick="doExportSource('${escapeHtml(sourceId)}')">📤 导出</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function hideExportSourceModal() {
    const modal = document.getElementById('export-source-modal');
    if (modal) modal.classList.add('hidden');
}

async function doExportSource(sourceId) {
    const certText = document.getElementById('export-source-cert').value.trim();
    const resultBox = document.getElementById('export-source-result');
    const exportBtn = document.getElementById('export-source-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成配置包...';
    exportBtn.disabled = true;
    
    try {
        const params = { id: sourceId };
        if (certText) params.recipient_cert = certText;
        
        const result = await api.call('automation.sources.export', params);
        if (result.code !== 0) throw new Error(result.message || '导出失败');
        
        const data = result.data;
        if (!data?.tscfg) throw new Error('无效的响应数据');
        
        // 下载文件
        const blob = new Blob([data.tscfg], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = data.filename || `source_${sourceId}.tscfg`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        resultBox.className = 'result-box success';
        resultBox.textContent = '✅ 导出成功！';
        showToast(`已导出数据源配置: ${data.filename}`, 'success');
        setTimeout(() => hideExportSourceModal(), 1000);
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    } finally {
        exportBtn.disabled = false;
    }
}

/**
 * 显示导入数据源配置模态框
 */
function showImportSourceModal() {
    let modal = document.getElementById('import-source-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'import-source-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📥 导入数据源配置</h2>
            <p style="color:#666;font-size:0.9rem">选择 .tscfg 配置包文件以导入数据源</p>
            
            <div id="import-source-step1">
                <div class="form-group" style="margin-top:15px">
                    <label>选择文件</label>
                    <input type="file" id="import-source-file" class="form-control" accept=".tscfg" onchange="previewSourceImport()">
                </div>
            </div>
            
            <div id="import-source-step2" style="display:none">
                <div class="info-card" style="background:#f8f9fa;padding:15px;border-radius:8px;margin-top:15px">
                    <h4 style="margin:0 0 10px 0">📋 配置包内容</h4>
                    <div id="import-source-preview"></div>
                </div>
                <div class="form-group" style="margin-top:15px">
                    <label>
                        <input type="checkbox" id="import-source-overwrite"> 覆盖已存在的配置
                    </label>
                </div>
            </div>
            
            <div id="import-source-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideImportSourceModal()">取消</button>
                <button class="btn btn-primary" id="import-source-btn" onclick="confirmSourceImport()" disabled>📥 确认导入</button>
            </div>
        </div>
    `;
    
    window._importSourceTscfg = null;
    modal.classList.remove('hidden');
}

function hideImportSourceModal() {
    const modal = document.getElementById('import-source-modal');
    if (modal) modal.classList.add('hidden');
    window._importSourceTscfg = null;
}

async function previewSourceImport() {
    const fileInput = document.getElementById('import-source-file');
    const resultBox = document.getElementById('import-source-result');
    const step2 = document.getElementById('import-source-step2');
    const previewDiv = document.getElementById('import-source-preview');
    const importBtn = document.getElementById('import-source-btn');
    
    if (!fileInput.files || !fileInput.files[0]) return;
    
    const file = fileInput.files[0];
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在验证配置包...';
    importBtn.disabled = true;
    step2.style.display = 'none';
    
    try {
        const content = await file.text();
        window._importSourceTscfg = content;
        window._importSourceFilename = file.name;
        
        const result = await api.call('automation.sources.import', { 
            tscfg: content,
            filename: file.name,
            preview: true
        });
        
        if (result.code === 0 && result.data?.valid) {
            const data = result.data;
            let html = `
                <table style="width:100%;font-size:0.9em">
                    <tr><td style="width:80px;color:#666">配置 ID:</td><td><code>${escapeHtml(data.id)}</code></td></tr>
                    <tr><td style="color:#666">类型:</td><td>📡 数据源</td></tr>
                    <tr><td style="color:#666">签名者:</td><td>${escapeHtml(data.signer)} ${data.official ? '✅ 官方' : ''}</td></tr>
                    <tr><td style="color:#666">备注:</td><td style="color:#888;font-size:0.85em">${escapeHtml(data.note || '重启后自动加载')}</td></tr>
                </table>
            `;
            if (data.exists) {
                html += `<div style="margin-top:10px;padding:8px;background:#fff3cd;border-radius:4px;color:#856404">⚠️ 该配置已存在，导入将覆盖现有文件</div>`;
            }
            previewDiv.innerHTML = html;
            step2.style.display = 'block';
            resultBox.className = 'result-box success';
            resultBox.textContent = '✅ 签名验证通过';
            importBtn.disabled = false;
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '无法验证配置包');
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    }
}

async function confirmSourceImport() {
    const overwrite = document.getElementById('import-source-overwrite').checked;
    const resultBox = document.getElementById('import-source-result');
    const importBtn = document.getElementById('import-source-btn');
    
    if (!window._importSourceTscfg) {
        showToast('请先选择文件', 'error');
        return;
    }
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在保存配置...';
    importBtn.disabled = true;
    
    try {
        const params = { 
            tscfg: window._importSourceTscfg,
            filename: window._importSourceFilename,
            overwrite: overwrite
        };
        
        const result = await api.call('automation.sources.import', params);
        
        if (result.code === 0) {
            const data = result.data;
            if (data?.exists && !data?.imported) {
                resultBox.className = 'result-box warning';
                resultBox.textContent = `⚠️ 配置 ${data.id} 已存在，请勾选「覆盖」选项`;
                importBtn.disabled = false;
            } else {
                resultBox.className = 'result-box success';
                resultBox.innerHTML = `✅ 已保存配置: <code>${escapeHtml(data?.id)}</code><br><small style="color:#666">重启系统后生效</small>`;
                showToast(`已导入配置，重启后生效`, 'success');
                setTimeout(() => hideImportSourceModal(), 2000);
            }
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '导入失败');
            importBtn.disabled = false;
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
        importBtn.disabled = false;
    }
}

/**
 * 显示导出规则配置模态框
 */
function showExportRuleModal(ruleId) {
    let modal = document.getElementById('export-rule-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'export-rule-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📤 导出规则配置</h2>
            <p style="color:#666;font-size:0.9rem">导出规则 <strong>${escapeHtml(ruleId)}</strong> 的配置为加密配置包</p>
            
            <div class="form-group">
                <label>目标设备证书 (PEM)</label>
                <textarea id="export-rule-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:120px;font-family:monospace;font-size:11px"></textarea>
                <div style="font-size:0.85em;color:#666;margin-top:4px">💡 粘贴目标设备的证书。留空则使用本机证书（自加密）</div>
            </div>
            
            <div id="export-rule-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideExportRuleModal()">取消</button>
                <button class="btn btn-primary" id="export-rule-btn" onclick="doExportRule('${escapeHtml(ruleId)}')">📤 导出</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function hideExportRuleModal() {
    const modal = document.getElementById('export-rule-modal');
    if (modal) modal.classList.add('hidden');
}

async function doExportRule(ruleId) {
    const certText = document.getElementById('export-rule-cert').value.trim();
    const resultBox = document.getElementById('export-rule-result');
    const exportBtn = document.getElementById('export-rule-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成配置包...';
    exportBtn.disabled = true;
    
    try {
        const params = { id: ruleId };
        if (certText) params.recipient_cert = certText;
        
        const result = await api.call('automation.rules.export', params);
        if (result.code !== 0) throw new Error(result.message || '导出失败');
        
        const data = result.data;
        if (!data?.tscfg) throw new Error('无效的响应数据');
        
        // 下载文件
        const blob = new Blob([data.tscfg], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = data.filename || `rule_${ruleId}.tscfg`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        resultBox.className = 'result-box success';
        resultBox.textContent = '✅ 导出成功！';
        showToast(`已导出规则配置: ${data.filename}`, 'success');
        setTimeout(() => hideExportRuleModal(), 1000);
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    } finally {
        exportBtn.disabled = false;
    }
}

/**
 * 显示导入规则配置模态框
 */
function showImportRuleModal() {
    let modal = document.getElementById('import-rule-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'import-rule-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📥 导入规则配置</h2>
            <p style="color:#666;font-size:0.9rem">选择 .tscfg 配置包文件以导入规则</p>
            
            <div id="import-rule-step1">
                <div class="form-group" style="margin-top:15px">
                    <label>选择文件</label>
                    <input type="file" id="import-rule-file" class="form-control" accept=".tscfg" onchange="previewRuleImport()">
                </div>
            </div>
            
            <div id="import-rule-step2" style="display:none">
                <div class="info-card" style="background:#f8f9fa;padding:15px;border-radius:8px;margin-top:15px">
                    <h4 style="margin:0 0 10px 0">📋 配置包内容</h4>
                    <div id="import-rule-preview"></div>
                </div>
                <div class="form-group" style="margin-top:15px">
                    <label>
                        <input type="checkbox" id="import-rule-overwrite"> 覆盖已存在的配置
                    </label>
                </div>
            </div>
            
            <div id="import-rule-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideImportRuleModal()">取消</button>
                <button class="btn btn-primary" id="import-rule-btn" onclick="confirmRuleImport()" disabled>📥 确认导入</button>
            </div>
        </div>
    `;
    
    window._importRuleTscfg = null;
    modal.classList.remove('hidden');
}

function hideImportRuleModal() {
    const modal = document.getElementById('import-rule-modal');
    if (modal) modal.classList.add('hidden');
    window._importRuleTscfg = null;
}

async function previewRuleImport() {
    const fileInput = document.getElementById('import-rule-file');
    const resultBox = document.getElementById('import-rule-result');
    const step2 = document.getElementById('import-rule-step2');
    const previewDiv = document.getElementById('import-rule-preview');
    const importBtn = document.getElementById('import-rule-btn');
    
    if (!fileInput.files || !fileInput.files[0]) return;
    
    const file = fileInput.files[0];
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在验证配置包...';
    importBtn.disabled = true;
    step2.style.display = 'none';
    
    try {
        const content = await file.text();
        window._importRuleTscfg = content;
        window._importRuleFilename = file.name;
        
        const result = await api.call('automation.rules.import', { 
            tscfg: content,
            filename: file.name,
            preview: true
        });
        
        if (result.code === 0 && result.data?.valid) {
            const data = result.data;
            let html = `
                <table style="width:100%;font-size:0.9em">
                    <tr><td style="width:80px;color:#666">配置 ID:</td><td><code>${escapeHtml(data.id)}</code></td></tr>
                    <tr><td style="color:#666">类型:</td><td>📋 自动化规则</td></tr>
                    <tr><td style="color:#666">签名者:</td><td>${escapeHtml(data.signer)} ${data.official ? '✅ 官方' : ''}</td></tr>
                    <tr><td style="color:#666">备注:</td><td style="color:#888;font-size:0.85em">${escapeHtml(data.note || '重启后自动加载')}</td></tr>
                </table>
            `;
            if (data.exists) {
                html += `<div style="margin-top:10px;padding:8px;background:#fff3cd;border-radius:4px;color:#856404">⚠️ 该配置已存在，导入将覆盖现有文件</div>`;
            }
            previewDiv.innerHTML = html;
            step2.style.display = 'block';
            resultBox.className = 'result-box success';
            resultBox.textContent = '✅ 签名验证通过';
            importBtn.disabled = false;
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '无法验证配置包');
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    }
}

async function confirmRuleImport() {
    const overwrite = document.getElementById('import-rule-overwrite').checked;
    const resultBox = document.getElementById('import-rule-result');
    const importBtn = document.getElementById('import-rule-btn');
    
    if (!window._importRuleTscfg) {
        showToast('请先选择文件', 'error');
        return;
    }
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在保存配置...';
    importBtn.disabled = true;
    
    try {
        const params = { 
            tscfg: window._importRuleTscfg,
            filename: window._importRuleFilename,
            overwrite: overwrite
        };
        
        const result = await api.call('automation.rules.import', params);
        
        if (result.code === 0) {
            const data = result.data;
            if (data?.exists && !data?.imported) {
                resultBox.className = 'result-box warning';
                resultBox.textContent = `⚠️ 配置 ${data.id} 已存在，请勾选「覆盖」选项`;
                importBtn.disabled = false;
            } else {
                resultBox.className = 'result-box success';
                resultBox.innerHTML = `✅ 已保存配置: <code>${escapeHtml(data?.id)}</code><br><small style="color:#666">重启系统后生效</small>`;
                showToast(`已导入配置，重启后生效`, 'success');
                setTimeout(() => hideImportRuleModal(), 2000);
            }
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '导入失败');
            importBtn.disabled = false;
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
        importBtn.disabled = false;
    }
}

/**
 * 显示导出动作模板配置模态框
 */
function showExportActionModal(actionId) {
    let modal = document.getElementById('export-action-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'export-action-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📤 导出动作模板</h2>
            <p style="color:#666;font-size:0.9rem">导出动作模板 <strong>${escapeHtml(actionId)}</strong> 的配置为加密配置包</p>
            
            <div class="form-group">
                <label>目标设备证书 (PEM)</label>
                <textarea id="export-action-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----" style="width:100%;height:120px;font-family:monospace;font-size:11px"></textarea>
                <div style="font-size:0.85em;color:#666;margin-top:4px">💡 粘贴目标设备的证书。留空则使用本机证书（自加密）</div>
            </div>
            
            <div id="export-action-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideExportActionModal()">取消</button>
                <button class="btn btn-primary" id="export-action-btn" onclick="doExportAction('${escapeHtml(actionId)}')">📤 导出</button>
            </div>
        </div>
    `;
    
    modal.classList.remove('hidden');
}

function hideExportActionModal() {
    const modal = document.getElementById('export-action-modal');
    if (modal) modal.classList.add('hidden');
}

async function doExportAction(actionId) {
    const certText = document.getElementById('export-action-cert').value.trim();
    const resultBox = document.getElementById('export-action-result');
    const exportBtn = document.getElementById('export-action-btn');
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在生成配置包...';
    exportBtn.disabled = true;
    
    try {
        const params = { id: actionId };
        if (certText) params.recipient_cert = certText;
        
        const result = await api.call('automation.actions.export', params);
        if (result.code !== 0) throw new Error(result.message || '导出失败');
        
        const data = result.data;
        if (!data?.tscfg) throw new Error('无效的响应数据');
        
        // 下载文件
        const blob = new Blob([data.tscfg], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = data.filename || `action_${actionId}.tscfg`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        resultBox.className = 'result-box success';
        resultBox.textContent = '✅ 导出成功！';
        showToast(`已导出动作模板: ${data.filename}`, 'success');
        setTimeout(() => hideExportActionModal(), 1000);
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    } finally {
        exportBtn.disabled = false;
    }
}

/**
 * 显示导入动作模板配置模态框
 */
function showImportActionModal() {
    let modal = document.getElementById('import-action-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'import-action-modal';
        modal.className = 'modal';
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div class="modal-content" style="max-width:600px">
            <h2>📥 导入动作模板</h2>
            <p style="color:#666;font-size:0.9rem">选择 .tscfg 配置包文件以导入动作模板</p>
            
            <div id="import-action-step1">
                <div class="form-group" style="margin-top:15px">
                    <label>选择文件</label>
                    <input type="file" id="import-action-file" class="form-control" accept=".tscfg" onchange="previewActionImport()">
                </div>
            </div>
            
            <div id="import-action-step2" style="display:none">
                <div class="info-card" style="background:#f8f9fa;padding:15px;border-radius:8px;margin-top:15px">
                    <h4 style="margin:0 0 10px 0">📋 配置包内容</h4>
                    <div id="import-action-preview"></div>
                </div>
                <div class="form-group" style="margin-top:15px">
                    <label>
                        <input type="checkbox" id="import-action-overwrite"> 覆盖已存在的配置
                    </label>
                </div>
            </div>
            
            <div id="import-action-result" class="result-box hidden" style="margin-top:10px"></div>
            
            <div class="form-actions" style="margin-top:15px">
                <button class="btn" onclick="hideImportActionModal()">取消</button>
                <button class="btn btn-primary" id="import-action-btn" onclick="confirmActionImport()" disabled>📥 确认导入</button>
            </div>
        </div>
    `;
    
    window._importActionTscfg = null;
    modal.classList.remove('hidden');
}

function hideImportActionModal() {
    const modal = document.getElementById('import-action-modal');
    if (modal) modal.classList.add('hidden');
    window._importActionTscfg = null;
}

async function previewActionImport() {
    const fileInput = document.getElementById('import-action-file');
    const resultBox = document.getElementById('import-action-result');
    const step2 = document.getElementById('import-action-step2');
    const previewDiv = document.getElementById('import-action-preview');
    const importBtn = document.getElementById('import-action-btn');
    
    if (!fileInput.files || !fileInput.files[0]) return;
    
    const file = fileInput.files[0];
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在验证配置包...';
    importBtn.disabled = true;
    step2.style.display = 'none';
    
    try {
        const content = await file.text();
        window._importActionTscfg = content;
        window._importActionFilename = file.name;
        
        const result = await api.call('automation.actions.import', { 
            tscfg: content,
            filename: file.name,
            preview: true
        });
        
        if (result.code === 0 && result.data?.valid) {
            const data = result.data;
            let html = `
                <table style="width:100%;font-size:0.9em">
                    <tr><td style="width:80px;color:#666">配置 ID:</td><td><code>${escapeHtml(data.id)}</code></td></tr>
                    <tr><td style="color:#666">类型:</td><td>⚡ 动作模板</td></tr>
                    <tr><td style="color:#666">签名者:</td><td>${escapeHtml(data.signer)} ${data.official ? '✅ 官方' : ''}</td></tr>
                    <tr><td style="color:#666">备注:</td><td style="color:#888;font-size:0.85em">${escapeHtml(data.note || '重启后自动加载')}</td></tr>
                </table>
            `;
            if (data.exists) {
                html += `<div style="margin-top:10px;padding:8px;background:#fff3cd;border-radius:4px;color:#856404">⚠️ 该配置已存在，导入将覆盖现有文件</div>`;
            }
            previewDiv.innerHTML = html;
            step2.style.display = 'block';
            resultBox.className = 'result-box success';
            resultBox.textContent = '✅ 签名验证通过';
            importBtn.disabled = false;
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '无法验证配置包');
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
    }
}

async function confirmActionImport() {
    const overwrite = document.getElementById('import-action-overwrite').checked;
    const resultBox = document.getElementById('import-action-result');
    const importBtn = document.getElementById('import-action-btn');
    
    if (!window._importActionTscfg) {
        showToast('请先选择文件', 'error');
        return;
    }
    
    resultBox.classList.remove('hidden', 'success', 'error');
    resultBox.textContent = '🔄 正在保存配置...';
    importBtn.disabled = true;
    
    try {
        const params = { 
            tscfg: window._importActionTscfg,
            filename: window._importActionFilename,
            overwrite: overwrite
        };
        
        const result = await api.call('automation.actions.import', params);
        
        if (result.code === 0) {
            const data = result.data;
            if (data?.exists && !data?.imported) {
                resultBox.className = 'result-box warning';
                resultBox.textContent = `⚠️ 配置 ${data.id} 已存在，请勾选「覆盖」选项`;
                importBtn.disabled = false;
            } else {
                resultBox.className = 'result-box success';
                resultBox.innerHTML = `✅ 已保存配置: <code>${escapeHtml(data?.id)}</code><br><small style="color:#666">重启系统后生效</small>`;
                showToast(`已导入配置，重启后生效`, 'success');
                setTimeout(() => hideImportActionModal(), 2000);
            }
        } else {
            resultBox.className = 'result-box error';
            resultBox.textContent = '❌ ' + (result.message || '导入失败');
            importBtn.disabled = false;
        }
    } catch (e) {
        resultBox.className = 'result-box error';
        resultBox.textContent = '❌ ' + e.message;
        importBtn.disabled = false;
    }
}

// 导出导入函数
window.showExportSourceModal = showExportSourceModal;
window.hideExportSourceModal = hideExportSourceModal;
window.doExportSource = doExportSource;
window.showImportSourceModal = showImportSourceModal;
window.hideImportSourceModal = hideImportSourceModal;
window.previewSourceImport = previewSourceImport;
window.confirmSourceImport = confirmSourceImport;
window.showExportRuleModal = showExportRuleModal;
window.hideExportRuleModal = hideExportRuleModal;
window.doExportRule = doExportRule;
window.showImportRuleModal = showImportRuleModal;
window.hideImportRuleModal = hideImportRuleModal;
window.previewRuleImport = previewRuleImport;
window.confirmRuleImport = confirmRuleImport;
window.showExportActionModal = showExportActionModal;
window.hideExportActionModal = hideExportActionModal;
window.doExportAction = doExportAction;
window.showImportActionModal = showImportActionModal;
window.hideImportActionModal = hideImportActionModal;
window.previewActionImport = previewActionImport;
window.confirmActionImport = confirmActionImport;

