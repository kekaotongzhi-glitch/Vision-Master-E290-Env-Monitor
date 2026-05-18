# 硬件说明

## 主板

当前固件基于 Heltec Vision Master E290。它是一块集成 ESP32-S3、SX1262 LoRa 和 E-Ink 墨水屏的节点硬件。

当前固件主要使用：

- ESP32-S3
- E-Ink 墨水屏
- Wi-Fi
- BLE
- ADC

SX1262 LoRa 芯片当前没有接入主要通信流程。

## 外设连接

| 模块 | 引脚 / 说明 |
| --- | --- |
| DHT11 | `GPIO39` |
| 按键 | `GPIO17` 接按键，另一端接 GND |
| 电池 ADC 输入 | `GPIO7` |
| 电池 ADC 使能 | `GPIO46` |
| 墨水屏 | `EInkDisplay_VisionMasterE290` |

按键代码使用 `INPUT_PULLUP`，按下时 GPIO 为低电平。

## DHT11

DHT11 数据线接 `GPIO39`。固件限制了最小采样间隔，避免 DHT11 被过快读取。

## 电池检测

电池检测使用 `GPIO7` 作为 ADC 输入，`GPIO46` 控制检测电路使能。默认电压校准系数是 `4.9`，可以通过 Web 或 MQTT 修改。

## 墨水屏

墨水屏由 `heltec-eink-modules` 驱动，业务代码使用 `EInkDisplay_VisionMasterE290`。SPI 和屏幕控制引脚由库封装，没有在业务代码里单独列出。

## 按键行为

当前按键用于切换页面：

- 主页面且 Wi-Fi 已连接：显示 Web 后台二维码
- 主页面且 Wi-Fi 未连接：显示 BLE 配网二维码
- 二维码页面：返回主页面

代码中有基本消抖和最小触发间隔限制。
