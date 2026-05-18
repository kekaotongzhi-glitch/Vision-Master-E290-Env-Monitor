#include "provisioning_manager.h"
#include "ui_manager.h"
#include "web_config_server.h"
#include "wifi_profile_manager.h"
#include <WiFi.h>
#include <WiFiProv.h>

static const char* PROV_POP = "12345678";
static bool prov_running = false;
static bool prov_start_pending = false;
static uint32_t prov_start_due_ms = 0;
static bool wifi_disconnected_logged = false;
static IPAddress last_connected_ip;
static String pending_service_name;
static String pending_wifi_ssid;
static String pending_wifi_password;
static uint8_t prov_uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
                                0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};

static const uint32_t PROV_WIFI_SETTLE_MS = 500;

static String provisioning_service_name() {
    uint64_t mac = ESP.getEfuseMac();
    uint16_t suffix = static_cast<uint16_t>(mac & 0xFFFF);
    char name[24];
    snprintf(name, sizeof(name), "PROV_EINK_%04X", suffix);
    return String(name);
}

String provisioning_get_service_name() {
    return provisioning_service_name();
}

String provisioning_get_pop() {
    return String(PROV_POP);
}

String provisioning_get_qr_payload() {
    String payload = "{\"ver\":\"v1\",\"name\":\"";
    payload += provisioning_service_name();
    payload += "\",\"pop\":\"";
    payload += PROV_POP;
    payload += "\",\"transport\":\"ble\"}";
    return payload;
}

static void on_wifi_event(arduino_event_t *event) {
    switch (event->event_id) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        {
            IPAddress ip(event->event_info.got_ip.ip_info.ip.addr);
            if (wifi_disconnected_logged || ip != last_connected_ip) {
                Serial.print("[BLE] Wi-Fi 已连接，IP=");
                Serial.println(ip);
            }
            last_connected_ip = ip;
            wifi_disconnected_logged = false;
            if (pending_wifi_ssid.length() > 0) {
                if (wifi_profiles_add(pending_wifi_ssid, pending_wifi_password, 50)) {
                    Serial.print("[BLE] 已保存 Wi-Fi：");
                    Serial.println(pending_wifi_ssid);
                } else {
                    Serial.println("[BLE] Wi-Fi 保存失败");
                }
                pending_wifi_ssid = "";
                pending_wifi_password = "";
            }
            ui_set_wifi_status(WifiStatus::CONNECTED);
            web_config_begin();
            provisioning_stop();
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (!wifi_disconnected_logged) {
                Serial.println("[BLE] Wi-Fi 已断开");
                wifi_disconnected_logged = true;
            }
            break;
        case ARDUINO_EVENT_PROV_START:
            Serial.println("[BLE] 配网服务已启动");
            break;
        case ARDUINO_EVENT_PROV_CRED_RECV:
            pending_wifi_ssid = reinterpret_cast<const char*>(event->event_info.prov_cred_recv.ssid);
            pending_wifi_password = reinterpret_cast<const char*>(event->event_info.prov_cred_recv.password);
            Serial.println("[BLE] 已收到 Wi-Fi 凭据");
            break;
        case ARDUINO_EVENT_PROV_CRED_FAIL:
            Serial.println("[BLE] Wi-Fi 凭据验证失败");
            break;
        case ARDUINO_EVENT_PROV_CRED_SUCCESS:
            Serial.println("[BLE] Wi-Fi 凭据验证通过");
            break;
        case ARDUINO_EVENT_PROV_END:
            prov_running = false;
            prov_start_pending = false;
            ui_set_provisioning_status(false);
            Serial.println("[BLE] 配网服务已结束");
            break;
        default:
            break;
    }
}

void provisioning_init() {
    WiFi.onEvent(on_wifi_event);
    ui_set_provisioning_status(false);
}

void provisioning_start_ble() {
    if (prov_running) {
        Serial.println("[BLE] 配网已在运行");
        return;
    }

    prov_running = true;
    prov_start_pending = true;
    prov_start_due_ms = millis() + PROV_WIFI_SETTLE_MS;
    pending_service_name = provisioning_service_name();
    wifi_disconnected_logged = false;
    last_connected_ip = IPAddress();
    ui_set_provisioning_status(true);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);

    Serial.println("[BLE] 已请求启动配网");
}

static void provisioning_begin_ble_now() {
    prov_start_pending = false;

    Serial.print("[BLE] 配网已启动：");
    Serial.println(pending_service_name);
    Serial.print("[BLE] PoP=");
    Serial.println(PROV_POP);
    Serial.println("[BLE] 传输方式：BLE");

    WiFiProv.beginProvision(
        WIFI_PROV_SCHEME_BLE,
        WIFI_PROV_SCHEME_HANDLER_NONE,
        WIFI_PROV_SECURITY_1,
        PROV_POP,
        pending_service_name.c_str(),
        nullptr,
        prov_uuid,
        true
    );
    WiFiProv.printQR(pending_service_name.c_str(), PROV_POP, "ble");
}

void provisioning_stop() {
    if (!prov_running) return;
    prov_running = false;
    prov_start_pending = false;
    ui_set_provisioning_status(false);
    Serial.println("[BLE] 配网已停止");
}

bool provisioning_is_running() {
    return prov_running;
}

void provisioning_loop() {
    if (prov_start_pending && static_cast<int32_t>(millis() - prov_start_due_ms) >= 0) {
        provisioning_begin_ble_now();
    }
}
