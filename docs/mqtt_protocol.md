# MQTT 协议记录

当前固件把本地 Web 后台作为局域网维护入口，把 MQTT 作为远程上报和控制入口。设备不直接暴露 WebServer 到公网。

## Topic

默认前缀：

```text
devices/<device_id>
```

遥测：

```text
devices/<device_id>/telemetry
```

状态：

```text
devices/<device_id>/status
```

控制下发：

```text
devices/<device_id>/control/set
```

控制回复：

```text
devices/<device_id>/control/report
```

配置下发：

```text
devices/<device_id>/config/set
```

配置回复：

```text
devices/<device_id>/config/report
```

## telemetry

示例：

```json
{
  "temperature": 25.4,
  "humidity": 52.5,
  "battery_percent": 100,
  "battery_voltage": 4.313,
  "rssi": -28,
  "uptime_ms": 922059,
  "report_reason": "interval",
  "data_age_ms": 0,
  "data_fresh": true,
  "env_level": "normal",
  "env_reason": "none",
  "env_message": "环境适宜",
  "alarm_active": false,
  "warning_active": false,
  "has_active_alarm": false,
  "has_unack_alarm": false,
  "unack_alarm_count": 0
}
```

`report_reason` 当前可能值：

- `interval`
- `manual`
- `alarm`
- `alarm_cleared`

## status

示例：

```json
{
  "online": true,
  "ip": "192.168.x.x",
  "firmware": "dev",
  "mqtt_connected": true
}
```

## control/set

### ping

```json
{"cmd":"ping"}
```

### report_status

```json
{"cmd":"report_status"}
```

### report_now

```json
{"cmd":"report_now"}
```

### report_alarms

```json
{"cmd":"report_alarms"}
```

### ack_alarms

```json
{"cmd":"ack_alarms"}
```

### ack_alarm

```json
{"cmd":"ack_alarm","id":3}
```

### report_history

```json
{"cmd":"report_history","limit":20}
```

## config/set

### interval

```json
{
  "sensor_sample_interval_ms": 10000,
  "report_interval_ms": 60000,
  "display_refresh_interval_ms": 50000
}
```

### EnvProfile

```json
{
  "active_env_profile_id": "home"
}
```

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

```json
{
  "env_profile_action": "delete",
  "profile_id": "lab"
}
```

## 说明

- Wi-Fi 密码不通过 MQTT 下发。
- Web API 不返回 Wi-Fi 密码。
- MQTT payload 中不放真实账号或 token。
