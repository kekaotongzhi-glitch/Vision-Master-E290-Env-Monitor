# API 与 MQTT

本文档记录当前固件已经实现的 Web API 和 MQTT 命令。

## Web API

### `GET /`

返回本地 Web 后台首页。

### `GET /api/status`

返回设备当前状态。

示例：

```json
{
  "device_id": "eink_a994",
  "ip": "192.168.x.x",
  "wifi_connected": true,
  "mqtt_connected": true,
  "rssi": -45,
  "temperature": 25.8,
  "humidity": 52.3,
  "battery_percent": 88,
  "battery_voltage": 4.05,
  "uptime_ms": 120000,
  "env_level": "normal",
  "env_reason": "none",
  "alarm_active": false,
  "warning_active": false,
  "history_count": 20
}
```

### `GET /api/history?limit=120`

返回最近历史样本。样本按旧到新排列。

每条样本包含：

- `sample_ms`
- `sample_ts`
- `has_real_time`
- `temperature`
- `humidity`
- `battery_voltage`
- `battery_percent`
- `rssi`
- `env_level`
- `env_reason`
- `alarm_active`
- `warning_active`

### `GET /api/alarms`

返回 RAM 中保存的报警事件。

### `POST /api/alarms/ack`

确认单条报警。

请求：

```text
id=<event_id>
```

### `POST /api/alarms/ack-all`

确认全部报警。

### `GET /api/env/profiles`

返回环境预设列表和当前正在使用的 profile。

### `POST /api/env/profile/save`

新增或更新环境预设。

阈值顺序必须满足：

```text
temp_alarm_min <= temp_normal_min <= temp_normal_max <= temp_alarm_max
humi_alarm_min <= humi_normal_min <= humi_normal_max <= humi_alarm_max
```

### `POST /api/env/profile/select`

切换当前使用的环境预设。

### `POST /api/env/profile/delete`

删除环境预设。最后一个 profile 不允许删除。

### `GET /api/wifi/profiles`

返回已保存的 Wi-Fi profile 列表。接口不返回 Wi-Fi 密码，只返回 `has_password`。

### `POST /api/wifi/profile/save`

新增或编辑 Wi-Fi profile。

新增：

```json
{
  "ssid": "YourWiFiSSID",
  "password": "YourWiFiPassword",
  "priority": 50,
  "enabled": true
}
```

编辑但不修改密码：

```json
{
  "ssid": "YourWiFiSSID",
  "priority": 60,
  "enabled": true
}
```

### `POST /api/wifi/profile/delete`

删除 Wi-Fi profile。

### `POST /api/wifi/reconnect`

请求设备重新选择 Wi-Fi profile 并连接。

### `POST /save-mqtt`

保存 MQTT 和系统配置。保存后设备延迟重启。

## MQTT Topic

默认 topic prefix：

```text
devices/<device_id>
```

主要 topic：

```text
devices/<device_id>/telemetry
devices/<device_id>/status
devices/<device_id>/control/set
devices/<device_id>/control/report
devices/<device_id>/config/set
devices/<device_id>/config/report
```

## `control/set`

### ping

```json
{"cmd":"ping"}
```

回复：

```json
{"ok":true,"cmd":"ping","msg":"pong"}
```

### report_status

```json
{"cmd":"report_status"}
```

设备立即发布 status，并回复 control/report。

### report_now

```json
{"cmd":"report_now"}
```

设备立即发布 telemetry。若当前不满足 DHT11 安全采样间隔，则使用缓存数据，并在 telemetry 中标记 `data_fresh=false`。

### report_alarms

```json
{"cmd":"report_alarms"}
```

返回报警摘要和事件列表。

### ack_alarms

```json
{"cmd":"ack_alarms"}
```

确认全部报警。

### ack_alarm

```json
{"cmd":"ack_alarm","id":3}
```

确认单条报警。

### report_history

```json
{"cmd":"report_history","limit":20}
```

返回最近历史样本。MQTT 返回最多限制为 50 条。

## `config/set`

### 采样、上报、显示间隔

```json
{
  "sensor_sample_interval_ms": 10000,
  "report_interval_ms": 60000,
  "display_refresh_interval_ms": 50000
}
```

固件会对间隔做安全下限和联动修正。

### 选择环境预设

```json
{
  "active_env_profile_id": "home"
}
```

### 保存环境预设

```json
{
  "env_profile_action": "save",
  "profile": {
    "id": "lab",
    "name": "实验室",
    "temp_normal_min": 18,
    "temp_normal_max": 26,
    "temp_alarm_min": 5,
    "temp_alarm_max": 35,
    "humi_normal_min": 40,
    "humi_normal_max": 70,
    "humi_alarm_min": 20,
    "humi_alarm_max": 85
  }
}
```

### 删除环境预设

```json
{
  "env_profile_action": "delete",
  "profile_id": "lab"
}
```

### 旧字段兼容

下面这种旧 payload 仍能工作：

```json
{"environment_preset":"home"}
```

新接口使用 `active_env_profile_id` 和 `env_profile_action`。
