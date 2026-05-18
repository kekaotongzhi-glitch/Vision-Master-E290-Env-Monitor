# MQTT Payload 示例

以下示例假设：

```text
device_id = eink_a994
topic_prefix = devices/eink_a994
```

## 控制命令

Topic:

```text
devices/eink_a994/control/set
```

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

## 配置命令

Topic:

```text
devices/eink_a994/config/set
```

### 设置采样、上报、显示间隔

```json
{
  "sensor_sample_interval_ms": 10000,
  "report_interval_ms": 60000,
  "display_refresh_interval_ms": 50000
}
```

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

## 本地 Mosquitto 配置记录

电脑端 Mosquitto 测试配置：

```text
listener 1883 0.0.0.0
allow_anonymous true
```

ESP32 Web 后台里的 MQTT 配置：

```text
mqtt_host = 192.168.x.x
mqtt_port = 1883
mqtt_client_id = eink_a994
topic_prefix = devices/eink_a994
```
