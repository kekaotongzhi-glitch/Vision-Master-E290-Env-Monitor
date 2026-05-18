# 架构说明

这份文档记录当前固件的模块划分。项目运行在 Heltec Vision Master E290 上，当前通信主链路是 Wi-Fi + MQTT，本地维护入口是 Web 后台，SX1262 LoRa 暂未接入主流程。

## 运行主线

```text
传感器 / 电池
    ↓
main.cpp 采样调度
    ↓
app_state 缓存当前状态
    ↓
environment_monitor 判断环境状态
    ↓
alarm_manager 记录报警事件
    ↓
history_manager 写入历史样本
    ↓
墨水屏 / Web API / MQTT
```

## 采样与调度

相关文件：

- `src/main.cpp`
- `src/battery_manager.cpp`

`main.cpp` 负责主循环调度。温湿度采样、显示刷新、周期上报都在这里协调。DHT11 有安全采样下限，周期上报和周期显示遵循“等待新采样”的策略，不把旧数据重复伪装成新数据输出。

电池电压由 `battery_manager.cpp` 读取，ADC 使能和校准系数也在这里处理。

## 配置管理

相关文件：

- `src/config_manager.cpp`
- `src/wifi_profile_manager.cpp`

`config_manager` 使用 Preferences/NVS 保存业务配置，包括 MQTT、采样间隔、显示刷新间隔、电池校准系数和环境预设。

`wifi_profile_manager` 保存 Wi-Fi profile。设备启动后会扫描附近网络，在已保存 profile 中选择优先级和信号合适的网络连接。

## 墨水屏 UI

相关文件：

- `src/ui_manager.cpp`
- `include/lvgl_eink_bridge.h`

UI 分为三个主要页面：

- 主页面：温湿度、电池、网络状态、环境状态
- BLE 配网二维码页
- Web 后台二维码页

墨水屏刷新由 UI 层发起请求，再通过 LVGL/e-ink bridge 提交到屏幕。二维码页设计成静态页，避免周期性刷新。

## BLE 配网

相关文件：

- `src/provisioning_manager.cpp`

BLE 配网使用 Arduino-ESP32 的 `WiFiProv`。设备没有可用 Wi-Fi，或者用户按键进入配网页时，会显示 BLE 配网二维码。配网成功后，Wi-Fi 信息会保存到 Wi-Fi profile 中。

## 网络与 MQTT

相关文件：

- `src/network_manager.cpp`

网络层负责：

- Wi-Fi profile 选择和连接
- Wi-Fi 重连
- NTP 校时
- MQTT 连接和订阅
- MQTT telemetry/status 发布
- MQTT control/set 和 config/set 处理

本地 Web 后台不作为公网入口。远程控制主要通过 MQTT 完成。

## Web 后台

相关文件：

- `src/web_config_server.cpp`

Web 后台用于同一局域网维护。当前页面包含状态看板、环境预设管理、报警记录、历史数据和趋势图。Web API 也在这个文件中注册。

## 环境状态与报警

相关文件：

- `src/environment_monitor.cpp`
- `src/alarm_manager.cpp`

`environment_monitor` 根据当前 EnvProfile 阈值计算：

- `normal`
- `warning`
- `alarm`

`alarm_manager` 只记录 alarm 事件。事件保存在 RAM 环形缓冲中，设备重启后清空。

## 历史数据

相关文件：

- `src/history_manager.cpp`

历史数据使用 RAM 环形缓冲保存。每条记录同时保存：

- `sample_ms`：设备启动后的毫秒数
- `sample_ts`：NTP 有效时的 Unix 时间戳
- `has_real_time`：是否有可信真实时间

Web 趋势图和 MQTT `report_history` 都使用这份历史缓存。
