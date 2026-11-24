# 控制台使用

控制台是运行在你自己电脑上的本机网页，用来连接 AI Passport、查看 Codex/DSH 状态和配置 DeepSeek 余额。所有操作都在本机完成；设备只接收心跳数据。

## 连接设备

1. 确认设备已开机；蓝牙连接时在设备菜单打开蓝牙。
2. Windows 双击 `start-console.vbs`，macOS 运行 `bash start-console.command`。首次启动会创建 Python 环境并安装依赖。
3. 控制台「连接」页选择蓝牙或局域网：
   - 蓝牙：搜索设备，选择后按系统提示输入设备上的六位配对码；
   - 局域网：设备先完成 Wi-Fi 配网，再搜索并核对六位码。
4. 设备左上角显示「已连接BLE」或「已连接LAN」即成功。同一设备一次只连接一台电脑。

## 状态页

- **Codex**：用量窗口、近 7 天/近 30 天 Token、当前任务。
- **DeepSeek**：余额、Token、今天/请求数、API Key 表单。
- 顶部 DSH 状态：工作中／就绪／未连接。

DeepSeek Key 只保存在本机控制台配置目录，权限 0600；不会发送到设备、写入日志或出现在 API 状态中。余额来自 DeepSeek 官方 `GET /user/balance`，每 5 分钟刷新；Token 来自本机 DSH 日志。

## DSH 状态

把仓库里的 `dsh-plugin/` 安装到 DSH（Plugins → Add plugin）。插件只发布工作状态和完成序号；不读取提示词、回答或工具参数。卸载插件或退出 DSH 后状态会在 30 秒内失效。

## 声音与息屏

完成提示遵循设备声音开关和夜间静音；状态页的「测试提示音」可以试听。设备空闲后自动息屏，按键唤醒。

## 注意

Windows 控制台从源码运行，不需要安装或适配 EXE。需要 Python 3.10+ 和 WebView2 Runtime。
