# 测试记录清单

这份清单用于我每次演示或改动固件后做手动回归。

## 编译和烧录

- [ ] `pio run --environment vision-master-e290` 编译通过
- [ ] `pio run -t upload --environment vision-master-e290` 烧录完成
- [ ] 串口监视器波特率为 `115200`

## 启动

- [ ] 串口出现 `[系统] 正在启动`
- [ ] 墨水屏进入主页面
- [ ] 没有 Wi-Fi 时仍显示本地温湿度

## BLE 配网

- [ ] 按键显示 BLE SETUP 二维码
- [ ] ESP BLE Prov App 能识别设备
- [ ] 输入 Wi-Fi 后设备获取 IP
- [ ] 配网成功后回到主页面
- [ ] Wi-Fi profile 被保存

## Web 后台

- [ ] Wi-Fi 已连接后按键显示 WEB SETUP 二维码
- [ ] 浏览器可以通过 IP URL 打开 Web 首页
- [ ] `/api/status` 返回 JSON
- [ ] 状态看板能显示温度、湿度、电池、RSSI、Wi-Fi/MQTT 状态

## MQTT

- [ ] Web 后台保存 MQTT 配置后设备重启
- [ ] 串口显示 MQTT 连接目标、client_id 和订阅结果
- [ ] MQTTX 或 mosquitto_sub 订阅 `#` 能看到 telemetry/status
- [ ] `{"cmd":"ping"}` 返回 `pong`
- [ ] `{"cmd":"report_status"}` 触发 status
- [ ] `{"cmd":"report_now"}` 触发 telemetry
- [ ] 合法 `config/set` 返回 `config/report`
- [ ] 非法 `config/set` 返回 `ok=false`

## 环境预设和报警

- [ ] Web 能加载环境 profile 列表
- [ ] 可以新增、编辑、应用、删除环境 profile
- [ ] 非法阈值顺序会被拒绝
- [ ] 触发 alarm 时 telemetry 包含 `report_reason="alarm"`
- [ ] 解除 alarm 时 telemetry 包含 `report_reason="alarm_cleared"`
- [ ] 墨水屏环境状态显示 `OK/WARN/ALARM/PAST/SENSOR`

## 报警事件

- [ ] `/api/alarms` 返回事件列表
- [ ] alarm 持续期间 max/min 温湿度更新
- [ ] alarm 解除后 `duration_ms` 有值
- [ ] Web 可以确认单条报警
- [ ] Web 可以确认全部报警
- [ ] MQTT `report_alarms` 返回事件信息

## 历史数据

- [ ] 正常采样后 `/api/history` 的 `count` 增长
- [ ] `/api/history?limit=10` 最多返回 10 条
- [ ] NTP 有效时样本 `has_real_time=true`
- [ ] NTP 无效时样本使用 `sample_ms`
- [ ] MQTT `report_history` 返回最近样本

## 趋势图

- [ ] Web 首页显示历史趋势区域
- [ ] 点击“刷新趋势”后请求 `/api/history?limit=120`
- [ ] Canvas 绘制温度/湿度曲线
- [ ] 有报警事件时显示 ALARM 区间
- [ ] 页面打开时不自动频繁请求历史数据

## 墨水屏页面

- [ ] 主页面温湿度数字清晰
- [ ] BLE QR 页面和 Web QR 页面文案可区分
- [ ] QR payload 正常
- [ ] 二维码页面静态显示，不闪烁

## 安全项

- [ ] 串口日志不打印 Wi-Fi 密码
- [ ] Web API 不返回 Wi-Fi 密码
- [ ] README 和 docs 中没有真实 Wi-Fi 密码或 MQTT token
