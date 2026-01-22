/**
 * TianShanOS API Client
 * 
 * 统一的 Core API 调用客户端，支持所有已注册的 API 端点
 */

const API_BASE = '/api/v1';

class TianShanAPI {
    constructor() {
        this.token = localStorage.getItem('token');
    }
    
    /**
     * 通用 API 请求方法
     */
    async request(endpoint, method = 'GET', data = null) {
        const headers = {
            'Content-Type': 'application/json'
        };
        
        if (this.token) {
            headers['Authorization'] = `Bearer ${this.token}`;
        }
        
        const options = {
            method,
            headers
        };
        
        if (data && method !== 'GET') {
            options.body = JSON.stringify(data);
        }
        
        try {
            const response = await fetch(`${API_BASE}${endpoint}`, options);
            const json = await response.json();
            
            // 返回 JSON 响应，即使是错误码也返回（让调用者决定如何处理）
            // 只有真正的 HTTP 错误（如网络错误）才抛出异常
            if (!response.ok && !json.code) {
                throw new Error(json.message || json.error || 'Request failed');
            }
            
            return json;
        } catch (error) {
            // 只对非预期错误打印日志
            if (error.name !== 'AbortError') {
                console.error(`API Error: ${endpoint}`, error);
            }
            throw error;
        }
    }

    /**
     * 调用 Core API（将端点名转为 URL 路径）
     * 例如: call('system.info') -> GET /api/v1/system/info
     */
    async call(apiName, params = null, method = null) {
        let endpoint = '/' + apiName.replace(/\./g, '/');
        
        // 自动判断方法：有参数且非查询类用 POST
        if (!method) {
            const isQuery = apiName.includes('.list') || apiName.includes('.status') || 
                           apiName.includes('.info') || apiName.includes('.get') ||
                           apiName.includes('.version') || apiName.includes('.partitions') ||
                           apiName.includes('.progress') || apiName.includes('.stats');
            method = (params && !isQuery) ? 'POST' : 'GET';
        }
        
        // GET 请求时，将 params 转为 query string
        if (method === 'GET' && params) {
            const queryParams = new URLSearchParams();
            for (const [key, value] of Object.entries(params)) {
                if (value !== null && value !== undefined && value !== '') {
                    queryParams.append(key, value);
                }
            }
            const queryString = queryParams.toString();
            if (queryString) {
                endpoint += '?' + queryString;
            }
            params = null;  // GET 请求不发送 body
        }
        
        return this.request(endpoint, method, params);
    }
    
    // =====================================================================
    //                         认证 API
    // =====================================================================
    
    async login(username, password) {
        const result = await this.request('/auth/login', 'POST', { username, password });
        if (result.token) {
            this.token = result.token;
            localStorage.setItem('token', result.token);
        }
        return result;
    }
    
    async logout() {
        try {
            await this.request('/auth/logout', 'POST');
        } finally {
            this.token = null;
            localStorage.removeItem('token');
        }
    }
    
    isLoggedIn() {
        return !!this.token;
    }
    
    // =====================================================================
    //                         系统 API (system.*)
    // =====================================================================
    
    async getSystemInfo() { return this.call('system.info'); }
    async getMemoryInfo() { return this.call('system.memory'); }
    async getTasks() { return this.call('system.tasks'); }
    async reboot(delay = 0) { return this.call('system.reboot', { delay }, 'POST'); }
    
    // =====================================================================
    //                         时间 API (time.*)
    // =====================================================================
    
    async timeInfo() { return this.call('time.info'); }
    async timeSync(timestampMs) { return this.call('time.sync', { timestamp_ms: timestampMs }, 'POST'); }
    async timeSetNtp(server, index = 0) { return this.call('time.set_ntp', { server, index }, 'POST'); }
    async timeSetTimezone(timezone) { return this.call('time.set_timezone', { timezone }, 'POST'); }
    async timeForceSync() { return this.call('time.force_sync', null, 'POST'); }
    
    // =====================================================================
    //                         配置 API (config.*)
    // =====================================================================
    
    // 基础配置
    async configGet(key) { return this.call('config.get', { key }); }
    async configSet(key, value, persist = false) { return this.call('config.set', { key, value, persist }, 'POST'); }
    async configDelete(key) { return this.call('config.delete', { key }, 'POST'); }
    async configList(prefix = '') { return this.call('config.list', prefix ? { prefix } : null); }
    async configSave() { return this.call('config.save', null, 'POST'); }
    
    // 模块化配置
    async configModuleList() { return this.call('config.module.list'); }
    async configModuleShow(module) { return this.call('config.module.show', { module }); }
    async configModuleSet(module, key, value) { return this.call('config.module.set', { module, key, value }, 'POST'); }
    async configModuleSave(module = null) { return this.call('config.module.save', module ? { module } : null, 'POST'); }
    async configModuleReset(module, persist = true) { return this.call('config.module.reset', { module, persist }, 'POST'); }
    async configSync() { return this.call('config.sync', null, 'POST'); }
    
    // =====================================================================
    //                         服务 API (service.*)
    // =====================================================================
    
    async serviceList() { return this.call('service.list'); }
    async serviceStatus(name) { return this.call('service.status', { name }); }
    async serviceStart(name) { return this.call('service.start', { name }, 'POST'); }
    async serviceStop(name) { return this.call('service.stop', { name }, 'POST'); }
    async serviceRestart(name) { return this.call('service.restart', { name }, 'POST'); }
    
    // =====================================================================
    //                         LED API (led.*)
    // =====================================================================
    
    async ledList() { return this.call('led.list'); }
    async ledBrightness(device, brightness) { return this.call('led.brightness', { device, brightness }, 'POST'); }
    async ledClear(device) { return this.call('led.clear', { device }, 'POST'); }
    async ledSet(device, index, color) { return this.call('led.set', { device, index, color }, 'POST'); }
    async ledFill(device, color) { return this.call('led.fill', { device, color }, 'POST'); }
    async ledEffectList(device = null) { return this.call('led.effect.list', device ? { device } : null); }
    async ledEffectStart(device, effect, params = {}) { return this.call('led.effect.start', { device, effect, ...params }, 'POST'); }
    async ledEffectStop(device) { return this.call('led.effect.stop', { device }, 'POST'); }
    async ledColorParse(color) { return this.call('led.color.parse', { color }); }
    async ledFilterList() { return this.call('led.filter.list'); }
    async ledFilterStart(device, filter, speed = 50) { return this.call('led.filter.start', { device, filter, speed }, 'POST'); }
    async ledFilterStop(device) { return this.call('led.filter.stop', { device }, 'POST'); }
    async ledSave(device) { return this.call('led.save', { device }, 'POST'); }
    
    // Matrix-specific LED APIs
    async ledImage(path, device = 'matrix', center = 'image') { return this.call('led.image', { device, path, center }, 'POST'); }
    async ledQrcode(text, options = {}) { return this.call('led.qrcode', { device: 'matrix', text, ...options }, 'POST'); }
    async ledText(text, options = {}) { return this.call('led.text', { device: 'matrix', text, ...options }, 'POST'); }
    async ledTextStop(device = 'matrix') { return this.call('led.text.stop', { device }, 'POST'); }
    
    // =====================================================================
    //                         网络 API (network.*, dhcp.*, nat.*)
    // =====================================================================
    
    // 综合网络状态 (包含 ethernet, wifi_sta, wifi_ap)
    async networkStatus() { return this.call('network.status'); }
    
    // WiFi 相关
    async wifiMode(mode = null) { 
        return mode ? this.call('network.wifi.mode', { mode }, 'POST') : this.call('network.wifi.mode'); 
    }
    async wifiScan() { return this.call('network.wifi.scan'); }
    async wifiConnect(ssid, password) { return this.call('network.wifi.connect', { ssid, password }, 'POST'); }
    async wifiDisconnect() { return this.call('network.wifi.disconnect', null, 'POST'); }
    async wifiApConfig(ssid, password = '', channel = 6, hidden = false) { 
        return this.call('network.wifi.ap.config', { ssid, password, channel, hidden }, 'POST'); 
    }
    async wifiApStations() { return this.call('network.wifi.ap.stations'); }
    
    // 以太网
    async ethStatus() { return this.call('network.eth.status'); }
    
    // 主机名
    async hostname(name = null) { 
        return name ? this.call('network.hostname', { hostname: name }, 'POST') : this.call('network.hostname'); 
    }
    
    // DHCP 服务器
    async dhcpStatus(iface = null) { return this.call('dhcp.status', iface ? { interface: iface } : null); }
    async dhcpClients(iface = null) { return this.call('dhcp.clients', iface ? { interface: iface } : null); }
    async dhcpStart(iface = 'ap') { return this.call('dhcp.start', { interface: iface }, 'POST'); }
    async dhcpStop(iface = 'ap') { return this.call('dhcp.stop', { interface: iface }, 'POST'); }
    
    // NAT 网关
    async natStatus() { return this.call('nat.status'); }
    async natEnable() { return this.call('nat.enable', null, 'POST'); }
    async natDisable() { return this.call('nat.disable', null, 'POST'); }
    async natSave() { return this.call('nat.save', null, 'POST'); }
    
    // =====================================================================
    //                         设备 API (device.*, agx.*, fan.*)
    // =====================================================================
    
    async deviceStatus(name = null) { return this.call('device.status', name ? { name } : null); }
    async devicePower(name, on) { return this.call('device.power', { name, on }, 'POST'); }
    async deviceReset(name) { return this.call('device.reset', { name }, 'POST'); }
    async agxStatus() { return this.call('agx.status'); }
    async agxData() { return this.call('agx.data'); }
    async fanStatus(id = null) { return this.call('fan.status', id !== null ? { id } : null); }
    async fanSet(id, duty) { return this.call('fan.set', { id, duty }, 'POST'); }
    async fanMode(id, mode) { return this.call('fan.mode', { id, mode }, 'POST'); }
    
    // =====================================================================
    //                         电源 API (power.*)
    // =====================================================================
    
    async powerStatus() { return this.call('power.status'); }
    async powerVoltage(channel = null) { return this.call('power.voltage', channel !== null ? { channel } : null); }
    async powerStats() { return this.call('power.stats'); }
    async powerProtectionStatus() { return this.call('power.protection.status'); }
    
    // =====================================================================
    //                         存储/GPIO/温度 API
    // =====================================================================
    
    async storageStatus() { return this.call('storage.status'); }
    async storageMount() { return this.call('storage.mount', null, 'POST'); }
    async storageUnmount() { return this.call('storage.unmount', null, 'POST'); }
    async storageList(path = '/sdcard') { return this.call('storage.list', { path }, 'POST'); }
    async storageDelete(path) { return this.call('storage.delete', { path }, 'POST'); }
    async storageMkdir(path) { return this.call('storage.mkdir', { path }, 'POST'); }
    async storageRename(from, to) { return this.call('storage.rename', { from, to }, 'POST'); }
    async storageInfo(path) { return this.call('storage.info', { path }, 'POST'); }
    
    /**
     * 下载文件
     * @param {string} path - 文件路径 (如 /sdcard/test.txt)
     * @returns {Promise<Blob>} 文件内容
     */
    async fileDownload(path) {
        const url = `${API_BASE}/file/download?path=${encodeURIComponent(path)}`;
        const response = await fetch(url, {
            headers: this.token ? { 'Authorization': `Bearer ${this.token}` } : {}
        });
        if (!response.ok) {
            const error = await response.json().catch(() => ({ message: 'Download failed' }));
            throw new Error(error.message);
        }
        return response.blob();
    }
    
    /**
     * 上传文件
     * @param {string} path - 目标路径 (如 /sdcard/uploads/file.txt)
     * @param {File|Blob|ArrayBuffer} content - 文件内容
     * @returns {Promise<Object>} 上传结果
     */
    async fileUpload(path, content) {
        const url = `${API_BASE}/file/upload?path=${encodeURIComponent(path)}`;
        const response = await fetch(url, {
            method: 'POST',
            headers: this.token ? { 'Authorization': `Bearer ${this.token}` } : {},
            body: content
        });
        const json = await response.json();
        if (!response.ok) {
            throw new Error(json.message || 'Upload failed');
        }
        return json;
    }
    
    async gpioList() { return this.call('gpio.list'); }
    async gpioInfo(pin) { return this.call('gpio.info', { pin }); }
    async tempStatus() { return this.call('temp.status'); }
    
    // =====================================================================
    //                         SSH/SFTP API
    // =====================================================================
    
    // SSH 测试连接 - 支持密码或密钥认证
    // options: { trust_new: bool, accept_changed: bool }
    async sshTest(host, user, auth, port = 22, options = {}) { 
        const params = { host, user, port };
        if (typeof auth === 'string') {
            params.password = auth;  // 兼容旧调用方式
        } else if (auth.password) {
            params.password = auth.password;
        } else if (auth.keyid) {
            params.keyid = auth.keyid;
        }
        // 主机指纹验证参数
        params.trust_new = options.trust_new ?? true;      // 新主机自动信任（默认 true）
        params.accept_changed = options.accept_changed ?? false; // 指纹变化是否接受（默认 false）
        return this.call('ssh.test', params, 'POST'); 
    }
    
    // SSH 执行命令 - 支持密码或密钥认证
    // options: { trust_new: bool, accept_changed: bool }
    async sshExec(host, user, auth, command, port = 22, options = {}) { 
        const params = { host, user, command, port };
        if (typeof auth === 'string') {
            params.password = auth;  // 兼容旧调用方式
        } else if (auth.password) {
            params.password = auth.password;
        } else if (auth.keyid) {
            params.keyid = auth.keyid;
        }
        // 主机指纹验证参数
        params.trust_new = options.trust_new ?? true;
        params.accept_changed = options.accept_changed ?? false;
        return this.call('ssh.exec', params, 'POST'); 
    }
    
    // 部署公钥 - options: { trust_new, accept_changed }
    async sshCopyid(host, user, password, keyid, port = 22, verify = true, options = {}) { 
        return this.call('ssh.copyid', { 
            host, user, password, keyid, port, verify,
            trust_new: options.trust_new ?? true,
            accept_changed: options.accept_changed ?? false
        }, 'POST'); 
    }
    
    // 撤销公钥 - options: { trust_new, accept_changed }
    async sshRevoke(host, user, password, keyid, port = 22, options = {}) { 
        return this.call('ssh.revoke', { 
            host, user, password, keyid, port,
            trust_new: options.trust_new ?? true,
            accept_changed: options.accept_changed ?? false
        }, 'POST'); 
    }
    async sshKeygen(id, type = 'ecdsa', comment = '') { return this.call('ssh.keygen', { id, type, comment }, 'POST'); }
    async sftpLs(host, user, password, path, port = 22) { return this.call('sftp.ls', { host, user, password, path, port }, 'POST'); }
    
    // =====================================================================
    //                         Key Management API
    // =====================================================================
    
    async keyList() { return this.call('key.list'); }
    async keyInfo(id) { return this.call('key.info', { id }); }
    async keyGenerate(id, type = 'rsa2048', comment = '', exportable = false, alias = '', hidden = false) { 
        return this.call('key.generate', { id, type, comment, exportable, alias, hidden }, 'POST'); 
    }
    async keyDelete(id) { return this.call('key.delete', { id }, 'POST'); }
    async keyExport(id) { return this.call('key.export', { id }); }
    async keyExportPrivate(id) { return this.call('key.exportPrivate', { id }, 'POST'); }
    
    // =====================================================================
    //                         Known Hosts API
    // =====================================================================
    
    async hostsList() { return this.call('hosts.list'); }
    async hostsInfo(host, port = 22) { return this.call('hosts.info', { host, port }); }
    async hostsRemove(host, port = 22) { return this.call('hosts.remove', { host, port }, 'POST'); }
    async hostsUpdate(host, port = 22) { return this.call('hosts.update', { host, port }, 'POST'); }
    async hostsClear() { return this.call('hosts.clear', {}, 'POST'); }
}

// =========================================================================
//                         WebSocket Client
// =========================================================================

class TianShanWS {
    constructor(onMessage, onConnect, onDisconnect) {
        this.ws = null;
        this.onMessage = onMessage;
        this.onConnect = onConnect;
        this.onDisconnect = onDisconnect;
        this.reconnectDelay = 1000;
        this.shouldReconnect = true;
    }
    
    connect() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const url = `${protocol}//${window.location.host}/ws`;
        
        try {
            this.ws = new WebSocket(url);
            
            this.ws.onopen = () => {
                console.log('WebSocket connected');
                this.reconnectDelay = 1000;
                this.send({ type: 'subscribe', events: ['*'] });
                if (this.onConnect) this.onConnect();
            };
            
            this.ws.onmessage = (event) => {
                try {
                    const msg = JSON.parse(event.data);
                    if (this.onMessage) this.onMessage(msg);
                } catch (e) {
                    console.error('WebSocket parse error:', e);
                }
            };
            
            this.ws.onclose = () => {
                console.log('WebSocket disconnected');
                if (this.onDisconnect) this.onDisconnect();
                if (this.shouldReconnect) {
                    setTimeout(() => this.connect(), this.reconnectDelay);
                    this.reconnectDelay = Math.min(this.reconnectDelay * 2, 30000);
                }
            };
            
            this.ws.onerror = (error) => console.error('WebSocket error:', error);
        } catch (e) {
            console.error('WebSocket connect failed:', e);
        }
    }
    
    disconnect() {
        this.shouldReconnect = false;
        if (this.ws) { this.ws.close(); this.ws = null; }
    }
    
    send(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(data));
        }
    }
    
    // 暴露 readyState 属性，供日志页面检查连接状态
    get readyState() {
        return this.ws ? this.ws.readyState : WebSocket.CLOSED;
    }
}

// 全局实例
const api = new TianShanAPI();
