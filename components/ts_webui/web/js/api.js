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
            
            if (!response.ok) {
                throw new Error(json.message || json.error || 'Request failed');
            }
            
            return json;
        } catch (error) {
            console.error(`API Error: ${endpoint}`, error);
            throw error;
        }
    }

    /**
     * 调用 Core API（将端点名转为 URL 路径）
     * 例如: call('system.info') -> GET /api/v1/system/info
     */
    async call(apiName, params = null, method = null) {
        const endpoint = '/' + apiName.replace(/\./g, '/');
        
        // 自动判断方法：有参数且非查询类用 POST
        if (!method) {
            const isQuery = apiName.includes('.list') || apiName.includes('.status') || 
                           apiName.includes('.info') || apiName.includes('.get');
            method = (params && !isQuery) ? 'POST' : 'GET';
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
    //                         配置 API (config.*)
    // =====================================================================
    
    async configGet(key) { return this.call('config.get', { key }); }
    async configSet(key, value, persist = false) { return this.call('config.set', { key, value, persist }, 'POST'); }
    async configDelete(key) { return this.call('config.delete', { key }, 'POST'); }
    async configList(prefix = '') { return this.call('config.list', prefix ? { prefix } : null); }
    
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
    
    // =====================================================================
    //                         网络 API (network.*, wifi.*, dhcp.*, nat.*)
    // =====================================================================
    
    async networkStatus() { return this.call('network.status'); }
    async wifiStatus() { return this.call('wifi.status'); }
    async wifiScan() { return this.call('wifi.scan'); }
    async wifiConnect(ssid, password) { return this.call('wifi.connect', { ssid, password }, 'POST'); }
    async wifiDisconnect() { return this.call('wifi.disconnect', null, 'POST'); }
    async dhcpStatus(iface = null) { return this.call('dhcp.status', iface ? { interface: iface } : null); }
    async dhcpClients(iface = null) { return this.call('dhcp.clients', iface ? { interface: iface } : null); }
    async natStatus() { return this.call('nat.status'); }
    async natEnable() { return this.call('nat.enable', null, 'POST'); }
    async natDisable() { return this.call('nat.disable', null, 'POST'); }
    
    // =====================================================================
    //                         设备 API (device.*, agx.*, fan.*)
    // =====================================================================
    
    async deviceStatus(name = null) { return this.call('device.status', name ? { name } : null); }
    async devicePower(name, on) { return this.call('device.power', { name, on }, 'POST'); }
    async deviceReset(name) { return this.call('device.reset', { name }, 'POST'); }
    async agxStatus() { return this.call('agx.status'); }
    async agxData() { return this.call('agx.data'); }
    async fanStatus(id = null) { return this.call('fan.status', id !== null ? { id } : null); }
    async fanSet(id, speed) { return this.call('fan.set', { id, speed }, 'POST'); }
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
    
    async sshTest(host, user, password, port = 22) { return this.call('ssh.test', { host, user, password, port }, 'POST'); }
    async sshExec(host, user, password, command, port = 22) { return this.call('ssh.exec', { host, user, password, command, port }, 'POST'); }
    async sftpLs(host, user, password, path, port = 22) { return this.call('sftp.ls', { host, user, password, path, port }, 'POST'); }
    async keyList() { return this.call('key.list'); }
    async hostsList() { return this.call('hosts.list'); }
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
}

// 全局实例
const api = new TianShanAPI();
