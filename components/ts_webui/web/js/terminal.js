/**
 * TianShanOS Web Terminal
 * 基于 xterm.js 的 Web 终端实现
 */

class WebTerminal {
    constructor(containerId) {
        this.containerId = containerId;
        this.terminal = null;
        this.fitAddon = null;
        this.ws = null;
        this.connected = false;
        this.inputBuffer = '';
        this.history = [];
        this.historyIndex = -1;
        this.prompt = 'tianshan> ';
        this.cursorPosition = 0;
        
        // SSH Shell 模式
        this.sshMode = false;
        this.sshConnecting = false;
    }

    /**
     * 初始化终端
     */
    async init() {
        const container = document.getElementById(this.containerId);
        if (!container) {
            console.error('Terminal container not found:', this.containerId);
            return false;
        }

        // 创建 xterm.js 终端
        this.terminal = new Terminal({
            cursorBlink: true,
            cursorStyle: 'block',
            fontSize: 14,
            fontFamily: '"Cascadia Code", "Fira Code", "Source Code Pro", monospace',
            theme: {
                background: '#1e1e2e',
                foreground: '#cdd6f4',
                cursor: '#f5e0dc',
                cursorAccent: '#1e1e2e',
                selectionBackground: '#585b70',
                black: '#45475a',
                red: '#f38ba8',
                green: '#a6e3a1',
                yellow: '#f9e2af',
                blue: '#89b4fa',
                magenta: '#f5c2e7',
                cyan: '#94e2d5',
                white: '#bac2de',
                brightBlack: '#585b70',
                brightRed: '#f38ba8',
                brightGreen: '#a6e3a1',
                brightYellow: '#f9e2af',
                brightBlue: '#89b4fa',
                brightMagenta: '#f5c2e7',
                brightCyan: '#94e2d5',
                brightWhite: '#a6adc8'
            },
            scrollback: 1000,
            convertEol: true
        });

        // 加载 fit 插件用于自适应大小
        if (typeof FitAddon !== 'undefined') {
            this.fitAddon = new FitAddon.FitAddon();
            this.terminal.loadAddon(this.fitAddon);
        }

        this.terminal.open(container);
        
        if (this.fitAddon) {
            this.fitAddon.fit();
        }

        // 设置输入处理
        this.setupInputHandler();

        // 监听窗口大小变化
        window.addEventListener('resize', () => this.fit());

        // 显示欢迎信息
        this.writeln('\x1b[1;36m╔══════════════════════════════════════════╗\x1b[0m');
        this.writeln('\x1b[1;36m║\x1b[0m     \x1b[1;33m⛰️  TianShanOS Web Terminal\x1b[0m          \x1b[1;36m║\x1b[0m');
        this.writeln('\x1b[1;36m╚══════════════════════════════════════════╝\x1b[0m');
        this.writeln('');
        this.writeln('正在连接到设备...');

        return true;
    }

    /**
     * 设置输入处理
     */
    setupInputHandler() {
        this.terminal.onData(data => {
            if (!this.connected) return;
            
            // SSH Shell 模式
            if (this.sshMode) {
                // 检查 Ctrl+\ (0x1C) 退出 SSH
                if (data.charCodeAt(0) === 0x1C) {
                    this.writeln('\r\n^\\  (退出 SSH shell)');
                    this.exitSshShell();
                    return;
                }
                // 其他输入直接转发
                this.sendSshInput(data);
                return;
            }

            // 处理特殊字符
            for (let i = 0; i < data.length; i++) {
                const char = data[i];
                const code = char.charCodeAt(0);

                if (code === 13) { // Enter
                    this.handleEnter();
                } else if (code === 127 || code === 8) { // Backspace
                    this.handleBackspace();
                } else if (code === 3) { // Ctrl+C
                    this.handleInterrupt();
                } else if (code === 27) { // Escape sequence
                    // 处理方向键等
                    if (data.slice(i, i + 3) === '\x1b[A') { // Up
                        this.handleHistoryUp();
                        i += 2;
                    } else if (data.slice(i, i + 3) === '\x1b[B') { // Down
                        this.handleHistoryDown();
                        i += 2;
                    } else if (data.slice(i, i + 3) === '\x1b[C') { // Right
                        this.handleCursorRight();
                        i += 2;
                    } else if (data.slice(i, i + 3) === '\x1b[D') { // Left
                        this.handleCursorLeft();
                        i += 2;
                    }
                } else if (code === 12) { // Ctrl+L (clear)
                    this.terminal.clear();
                    this.writePrompt();
                    this.terminal.write(this.inputBuffer);
                } else if (code >= 32) { // 可打印字符
                    this.handlePrintable(char);
                }
            }
        });
    }

    handleEnter() {
        this.terminal.write('\r\n');
        const cmd = this.inputBuffer.trim();
        
        if (cmd) {
            // 添加到历史记录
            this.history.push(cmd);
            if (this.history.length > 100) {
                this.history.shift();
            }
            this.historyIndex = this.history.length;
            
            // 发送命令
            this.sendCommand(cmd);
        } else {
            this.writePrompt();
        }
        
        this.inputBuffer = '';
        this.cursorPosition = 0;
    }

    handleBackspace() {
        if (this.cursorPosition > 0) {
            this.inputBuffer = 
                this.inputBuffer.slice(0, this.cursorPosition - 1) + 
                this.inputBuffer.slice(this.cursorPosition);
            this.cursorPosition--;
            this.refreshLine();
        }
    }

    handleInterrupt() {
        this.terminal.write('^C\r\n');
        this.inputBuffer = '';
        this.cursorPosition = 0;
        
        // 发送中断信号
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({ type: 'terminal_interrupt' }));
        }
        
        this.writePrompt();
    }

    handleHistoryUp() {
        if (this.historyIndex > 0) {
            this.historyIndex--;
            this.inputBuffer = this.history[this.historyIndex];
            this.cursorPosition = this.inputBuffer.length;
            this.refreshLine();
        }
    }

    handleHistoryDown() {
        if (this.historyIndex < this.history.length - 1) {
            this.historyIndex++;
            this.inputBuffer = this.history[this.historyIndex];
        } else {
            this.historyIndex = this.history.length;
            this.inputBuffer = '';
        }
        this.cursorPosition = this.inputBuffer.length;
        this.refreshLine();
    }

    handleCursorLeft() {
        if (this.cursorPosition > 0) {
            this.cursorPosition--;
            this.terminal.write('\x1b[D');
        }
    }

    handleCursorRight() {
        if (this.cursorPosition < this.inputBuffer.length) {
            this.cursorPosition++;
            this.terminal.write('\x1b[C');
        }
    }

    handlePrintable(char) {
        this.inputBuffer = 
            this.inputBuffer.slice(0, this.cursorPosition) + 
            char + 
            this.inputBuffer.slice(this.cursorPosition);
        this.cursorPosition++;
        this.refreshLine();
    }

    refreshLine() {
        // 清除当前行并重新绘制
        this.terminal.write('\r\x1b[K');
        this.terminal.write(this.prompt + this.inputBuffer);
        // 移动光标到正确位置
        const backMoves = this.inputBuffer.length - this.cursorPosition;
        if (backMoves > 0) {
            this.terminal.write(`\x1b[${backMoves}D`);
        }
    }

    /**
     * 连接到 WebSocket
     */
    connect() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;
        
        this.ws = new WebSocket(wsUrl);
        this.pingInterval = null;
        
        this.ws.onopen = () => {
            console.log('Terminal WebSocket connected');
            // 发送终端启动请求
            this.ws.send(JSON.stringify({ type: 'terminal_start' }));
            
            // 启动心跳机制
            this.pingInterval = setInterval(() => {
                if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                    this.ws.send(JSON.stringify({ type: 'ping' }));
                }
            }, 15000); // 每15秒发送心跳
        };
        
        this.ws.onmessage = (event) => {
            try {
                const msg = JSON.parse(event.data);
                this.handleMessage(msg);
            } catch (e) {
                console.error('Failed to parse message:', e);
            }
        };
        
        this.ws.onclose = (event) => {
            console.log('Terminal WebSocket disconnected, code:', event.code);
            this.connected = false;
            
            // 清除心跳
            if (this.pingInterval) {
                clearInterval(this.pingInterval);
                this.pingInterval = null;
            }
            
            this.writeln('\r\n\x1b[1;31m连接已断开\x1b[0m');
            
            // 尝试重连
            if (event.code !== 1000) { // 非正常关闭
                this.writeln('\x1b[33m5秒后尝试重新连接...\x1b[0m');
                setTimeout(() => {
                    if (!this.connected) {
                        this.writeln('正在重新连接...');
                        this.connect();
                    }
                }, 5000);
            }
        };
        
        this.ws.onerror = (error) => {
            console.error('Terminal WebSocket error:', error);
            this.writeln('\r\n\x1b[1;31m连接错误\x1b[0m');
        };
    }

    /**
     * 处理 WebSocket 消息
     */
    handleMessage(msg) {
        switch (msg.type) {
            case 'connected':
                this.connected = true;
                this.prompt = msg.prompt || 'tianshan> ';
                this.writeln('\x1b[1;32m已连接到设备\x1b[0m');
                this.writeln('输入 \x1b[1;33mhelp\x1b[0m 查看可用命令');
                this.writeln('');
                this.writePrompt();
                break;
                
            case 'output':
                // 命令输出
                if (msg.data) {
                    this.write(msg.data);
                }
                break;
                
            case 'done':
                // 命令执行完成
                this.writePrompt();
                break;
                
            case 'error':
                this.writeln('\x1b[1;31m错误: ' + (msg.message || '未知错误') + '\x1b[0m');
                this.writePrompt();
                break;
                
            case 'pong':
                // 心跳响应，忽略
                break;
            
            case 'power_event':
                // 电压保护事件通知
                this.handlePowerEvent(msg);
                break;
            
            // SSH Shell 消息
            case 'ssh_status':
                this.handleSshStatus(msg);
                break;
            case 'ssh_output':
                // SSH Shell 输出
                if (msg.data) {
                    this.write(msg.data);
                }
                break;
                
            default:
                console.log('Unknown message type:', msg.type);
        }
    }

    /**
     * 处理电压保护事件
     */
    handlePowerEvent(msg) {
        const state = msg.state || 'UNKNOWN';
        const voltage = msg.voltage ? msg.voltage.toFixed(2) : '?.??';
        const countdown = msg.countdown || 0;
        const event = msg.event || 'unknown';
        
        let notification = '';
        let color = '\x1b[33m'; // 默认黄色
        
        switch (event) {
            case 'low_voltage':
                color = '\x1b[1;31m'; // 亮红色
                notification = `⚠️  低电压警告! 电压: ${voltage}V - 开始关机倒计时`;
                break;
            case 'countdown_tick':
                if (countdown <= 10) {
                    color = '\x1b[1;31m'; // 亮红色
                } else if (countdown <= 30) {
                    color = '\x1b[33m'; // 黄色
                } else {
                    return; // 不显示每秒倒计时，只显示关键时刻
                }
                notification = `⏱️  关机倒计时: ${countdown}秒 | 电压: ${voltage}V`;
                break;
            case 'shutdown_start':
                color = '\x1b[1;31m';
                notification = `🔴 正在执行关机... 电压: ${voltage}V`;
                break;
            case 'protected':
                color = '\x1b[35m'; // 紫色
                notification = `🛡️  进入保护状态 | 等待电压恢复...`;
                break;
            case 'recovery_start':
                color = '\x1b[36m'; // 青色
                notification = `🔄 电压恢复中: ${voltage}V | 等待稳定...`;
                break;
            case 'recovery_complete':
                color = '\x1b[1;32m'; // 亮绿色
                notification = `✅ 电压恢复完成! ${voltage}V | 系统即将重启`;
                break;
            case 'debug_tick':
                // 调试模式：每秒显示状态
                color = '\x1b[36m'; // 青色
                notification = `📊 [调试] ${state} | 电压: ${voltage}V | 倒计时: ${countdown}s`;
                break;
            case 'state_changed':
                if (state === 'NORMAL') {
                    color = '\x1b[32m';
                    notification = `✓ 电压状态正常: ${voltage}V`;
                } else {
                    notification = `状态变更: ${state} | 电压: ${voltage}V`;
                }
                break;
            default:
                notification = `[电源] ${event}: 状态=${state}, 电压=${voltage}V`;
        }
        
        if (notification) {
            // 保存当前输入状态
            const savedBuffer = this.inputBuffer;
            const savedPosition = this.cursorPosition;
            
            // 清除当前行，显示通知
            this.terminal.write('\r\x1b[K');
            this.writeln(`${color}${notification}\x1b[0m`);
            
            // 恢复提示符和输入
            this.terminal.write(this.prompt + savedBuffer);
            const backMoves = savedBuffer.length - savedPosition;
            if (backMoves > 0) {
                this.terminal.write(`\x1b[${backMoves}D`);
            }
        }
    }

    /**
     * 发送命令
     */
    sendCommand(command) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            // 检查是否是 SSH shell 命令
            if (this.parseSshCommand(command)) {
                return; // SSH 命令已处理
            }
            
            this.ws.send(JSON.stringify({
                type: 'terminal_input',
                data: command
            }));
        } else {
            this.writeln('\x1b[1;31m未连接到设备\x1b[0m');
            this.writePrompt();
        }
    }
    
    /**
     * 解析并处理 SSH shell 命令
     * 返回 true 如果是 SSH 命令
     */
    parseSshCommand(command) {
        // 匹配 ssh --host xxx --user xxx [--password xxx] --shell
        const sshMatch = command.match(/^ssh\s+(.*)--shell/i);
        if (!sshMatch) return false;
        
        const argsStr = sshMatch[1];
        
        // 解析参数
        const hostMatch = argsStr.match(/--host\s+(\S+)/i);
        const userMatch = argsStr.match(/--user\s+(\S+)/i);
        const passwordMatch = argsStr.match(/--password\s+(\S+)/i);
        const portMatch = argsStr.match(/--port\s+(\d+)/i);
        
        if (!hostMatch || !userMatch) {
            this.writeln('\x1b[1;31m错误: SSH shell 需要 --host 和 --user 参数\x1b[0m');
            this.writePrompt();
            return true;
        }
        
        const sshParams = {
            host: hostMatch[1],
            user: userMatch[1],
            password: passwordMatch ? passwordMatch[1] : '',
            port: portMatch ? parseInt(portMatch[1]) : 22
        };
        
        this.startSshShell(sshParams);
        return true;
    }
    
    /**
     * 启动 SSH Shell
     */
    startSshShell(params) {
        if (this.sshConnecting || this.sshMode) {
            this.writeln('\x1b[1;31mSSH 会话已在进行中\x1b[0m');
            this.writePrompt();
            return;
        }
        
        this.sshConnecting = true;
        this.writeln(`\x1b[36m正在连接到 ${params.user}@${params.host}:${params.port}...\x1b[0m`);
        
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({
                type: 'ssh_connect',
                host: params.host,
                port: params.port,
                user: params.user,
                password: params.password
            }));
        }
    }
    
    /**
     * 发送 SSH 输入
     */
    sendSshInput(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({
                type: 'ssh_input',
                data: data
            }));
        }
    }
    
    /**
     * 处理 SSH 状态消息
     */
    handleSshStatus(msg) {
        const status = msg.status;
        const message = msg.message || '';
        
        switch (status) {
            case 'connecting':
                this.writeln(`\x1b[33m${message}\x1b[0m`);
                break;
            case 'connected':
                this.sshMode = true;
                this.sshConnecting = false;
                this.writeln(`\x1b[1;32m${message}\x1b[0m`);
                this.writeln('\x1b[90m(按 Ctrl+\\ 退出 SSH shell)\x1b[0m');
                this.writeln('');
                break;
            case 'closed':
            case 'disconnecting':
                this.sshMode = false;
                this.sshConnecting = false;
                this.writeln(`\r\n\x1b[33m${message}\x1b[0m`);
                this.writePrompt();
                break;
            case 'error':
                this.sshMode = false;
                this.sshConnecting = false;
                this.writeln(`\x1b[1;31m${message}\x1b[0m`);
                this.writePrompt();
                break;
        }
    }
    
    /**
     * 退出 SSH Shell
     */
    exitSshShell() {
        if (!this.sshMode) return;
        
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({ type: 'ssh_disconnect' }));
        }
    }

    /**
     * 写入提示符
     */
    writePrompt() {
        if (this.terminal) {
            this.terminal.write(this.prompt);
        }
    }

    /**
     * 写入文本
     */
    write(text) {
        if (this.terminal) {
            this.terminal.write(text);
        }
    }

    /**
     * 写入一行
     */
    writeln(text) {
        if (this.terminal) {
            this.terminal.writeln(text);
        }
    }

    /**
     * 调整大小
     */
    fit() {
        if (this.fitAddon) {
            this.fitAddon.fit();
        }
    }

    /**
     * 断开连接
     */
    disconnect() {
        // 清除心跳
        if (this.pingInterval) {
            clearInterval(this.pingInterval);
            this.pingInterval = null;
        }
        
        if (this.ws) {
            // 发送停止终端会话请求
            if (this.ws.readyState === WebSocket.OPEN) {
                this.ws.send(JSON.stringify({ type: 'terminal_stop' }));
            }
            this.ws.close(1000, 'User disconnect'); // 正常关闭代码
            this.ws = null;
        }
        this.connected = false;
    }

    /**
     * 销毁终端
     */
    destroy() {
        this.disconnect();
        if (this.terminal) {
            this.terminal.dispose();
            this.terminal = null;
        }
    }
}

// 全局终端实例
let webTerminal = null;
