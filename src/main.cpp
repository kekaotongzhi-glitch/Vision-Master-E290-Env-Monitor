#include <Arduino.h>
#include <DHT.h>
#include <heltec-eink-modules.h>
#include "app_state.h"
#include "alarm_manager.h"
#include "battery_manager.h"
#include "config_manager.h"
#include "environment_monitor.h"
#include "history_manager.h"
#include "lvgl_eink_bridge.h"
#include "network_manager.h"
#include "provisioning_manager.h"
#include "ui_manager.h"
#include "web_config_server.h"

LvglEinkBridge *g_lvgl_eink_bridge = nullptr;

#define DHTPIN 39
#define DHTTYPE DHT11
#define QR_BUTTON_PIN 17
#define BUTTON_DEBOUNCE_MS 80
#define BUTTON_MIN_TOGGLE_INTERVAL_MS 350

DHT dht(DHTPIN, DHTTYPE);
EInkDisplay_VisionMasterE290 display;

static lv_color_t lvgl_buf[296 * 12];
static LvglEinkBridge bridge(display, lvgl_buf, sizeof(lvgl_buf) / sizeof(lvgl_buf[0]));

static float last_temperature = NAN;
static float last_humidity = NAN;
static int last_battery_percent = 0;
static bool last_charging = false;
static float last_battery_voltage = 0.0f;
static uint32_t last_sample_ms = 0;
static uint32_t last_upload_ms = 0;
static uint32_t last_display_refresh_ms = 0;
static uint32_t last_lvgl_tick_ms = 0;
static uint32_t last_time_ui_ms = 0;
static volatile bool button_press_pending = false;
static bool button_debounce_active = false;
static uint32_t button_debounce_started_ms = 0;
static uint32_t last_button_toggle_ms = 0;
static float last_ui_temperature = NAN;
static float last_ui_humidity = NAN;
static int last_ui_battery_percent = -1;
static bool report_pending = false;
static bool display_pending = false;
static bool last_alarm_active = false;
static uint32_t last_dht_error_log_ms = 0;
static uint32_t dht_error_count = 0;

static const char* env_ascii_text(const AppRuntimeState& state) {
    if (state.env_message_en == "Sensor data invalid") return "SENSOR";
    if (state.alarm_active) return "ALARM";
    if (state.has_unack_alarm) return "PAST";
    if (state.warning_active) return "WARN";
    return "OK";
}

void IRAM_ATTR qr_button_isr() {
    button_press_pending = true;
}

static void refresh_main_screen() {
    ui_init();
    String time_str = network_get_time_str();
    ui_update(last_temperature, last_humidity, last_battery_percent, last_charging, network_get_rssi(), time_str.c_str());
    ui_update_battery(last_battery_voltage, last_battery_percent, last_charging);
    ui_set_provisioning_status(provisioning_is_running());
    AppRuntimeState state = app_state_get();
    ui_set_environment_status(env_ascii_text(state));
    last_ui_temperature = last_temperature;
    last_ui_humidity = last_humidity;
    last_ui_battery_percent = last_battery_percent;
    last_display_refresh_ms = millis();
}

static bool sample_sensors() {
    uint32_t now = millis();
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    bool valid = !isnan(temperature) && !isnan(humidity);

    if (!valid) {
        dht_error_count++;
        if (last_dht_error_log_ms == 0 || now - last_dht_error_log_ms >= 60000UL) {
            Serial.print("[传感器] DHT 读取失败，连续次数=");
            Serial.println(dht_error_count);
            last_dht_error_log_ms = now;
        }
    } else {
        dht_error_count = 0;
        last_dht_error_log_ms = 0;
        last_temperature = temperature;
        last_humidity = humidity;
    }
    app_state_set_sensor_result(valid, last_temperature, last_humidity, now);
    EnvStatus env_status = environment_evaluate(last_temperature, last_humidity, valid);
    bool env_changed = app_state_set_environment(env_status, now);
    alarm_manager_on_env_update(env_status.level, env_status.reason, env_status.message_cn,
                                last_temperature, last_humidity, valid, now);
    if (env_changed && ui_current_screen() == UiScreen::MAIN) {
        AppRuntimeState state = app_state_get();
        ui_set_environment_status(env_ascii_text(state));
    }

    last_battery_voltage = get_battery_voltage();
    last_battery_percent = get_battery_percent();
    last_charging = is_battery_charging();
    app_state_set_battery(last_battery_voltage, last_battery_percent, last_charging);
    if (valid) {
        AppRuntimeState state = app_state_get();
        history_manager_add_sample(last_temperature,
                                   last_humidity,
                                   last_battery_voltage,
                                   last_battery_percent,
                                   network_get_rssi(),
                                   environment_level_to_string(state.env_level),
                                   environment_reason_to_string(state.env_reason),
                                   state.alarm_active,
                                   state.warning_active,
                                   state.current_alarm_event_active,
                                   state.has_unack_alarm,
                                   state.current_alarm_event_id,
                                   now);
    }
    last_sample_ms = now;
    return valid;
}

static void report_alarm_transition_if_needed(bool sample_valid) {
    if (!sample_valid) return;
    uint32_t now = millis();
    AppRuntimeState state = app_state_get();
    if (!last_alarm_active && state.alarm_active) {
        Serial.print("[报警] 进入报警：");
        Serial.println(environment_reason_to_string(state.env_reason));
        if (network_is_wifi_connected() && network_is_mqtt_connected() &&
            network_report(last_temperature, last_humidity, last_battery_percent, last_battery_voltage, "alarm")) {
            last_upload_ms = now;
            report_pending = false;
            app_state_set_last_mqtt_upload(now);
        }
    } else if (last_alarm_active && !state.alarm_active) {
        Serial.println("[报警] 报警解除");
        if (network_is_wifi_connected() && network_is_mqtt_connected() &&
            network_report(last_temperature, last_humidity, last_battery_percent, last_battery_voltage, "alarm_cleared")) {
            last_upload_ms = now;
            report_pending = false;
            app_state_set_last_mqtt_upload(now);
        }
    }
    last_alarm_active = state.alarm_active;
}

static void update_main_data_display_if_needed(bool force) {
    if (ui_current_screen() == UiScreen::MAIN) {
        if (isnan(last_temperature) || isnan(last_humidity)) return;

        AppConfig cfg = config_get();
        bool changed = force;
        changed |= isnan(last_ui_temperature) || fabsf(last_temperature - last_ui_temperature) >= 0.2f;
        changed |= isnan(last_ui_humidity) || fabsf(last_humidity - last_ui_humidity) >= 1.0f;
        changed |= last_ui_battery_percent < 0 || abs(last_battery_percent - last_ui_battery_percent) >= 1;
        bool interval_elapsed = millis() - last_display_refresh_ms >= cfg.display_refresh_interval_ms;

        if (!changed && !interval_elapsed) return;

        String time_str = network_get_time_str();
        ui_update(last_temperature, last_humidity, last_battery_percent, last_charging, network_get_rssi(), time_str.c_str());
        ui_update_battery(last_battery_voltage, last_battery_percent, last_charging);
        AppRuntimeState state = app_state_get();
        ui_set_environment_status(env_ascii_text(state));
        last_ui_temperature = last_temperature;
        last_ui_humidity = last_humidity;
        last_ui_battery_percent = last_battery_percent;
        last_display_refresh_ms = millis();
    }
}

static void handle_report_now_if_requested() {
    if (!network_report_now_requested()) return;
    network_clear_report_now_request();

    uint32_t now = millis();
    AppRuntimeState state = app_state_get();
    bool safe_to_sample = state.last_sensor_sample_ms == 0 || (now - state.last_sensor_sample_ms) >= DHT11_SAFE_MIN_SAMPLE_MS;
    bool sampled_now = false;

    if (safe_to_sample) {
        bool valid = sample_sensors();
        sampled_now = valid;
        report_alarm_transition_if_needed(valid);
        if (valid) {
            update_main_data_display_if_needed(true);
        }
    }

    bool ok = network_report_cached("manual", !sampled_now);
    if (ok) {
        app_state_set_last_mqtt_upload(millis());
    }
    network_finish_report_now(ok);
}

static void update_time_display_if_needed() {
    uint32_t now = millis();
    if (now - last_time_ui_ms < 5000) return;
    last_time_ui_ms = now;

    if (ui_current_screen() == UiScreen::MAIN) {
        String time_str = network_get_time_str();
        ui_update_time(time_str.c_str());
    }
}

static void handle_short_press_action() {
    UiScreen screen = ui_current_screen();
    if (screen == UiScreen::PROVISIONING_QR || screen == UiScreen::WEB_CONSOLE_QR) {
        refresh_main_screen();
        Serial.println("[按键] 返回主界面");
        return;
    }

    if (network_is_wifi_connected() && web_config_is_running()) {
        String url = web_config_get_url();
        ui_show_web_console_qr(url.c_str());
        Serial.print("[按键] 显示 Web 配置二维码：");
        Serial.println(url);
        return;
    }

    if (!provisioning_is_running()) {
        provisioning_start_ble();
    }
    String payload = provisioning_get_qr_payload();
    ui_show_ble_provisioning_qr(payload.c_str());
    Serial.println("[按键] 显示 BLE 配网二维码");
}

static void handle_qr_button() {
    uint32_t now = millis();

    if (button_press_pending) {
        button_press_pending = false;
        if (!button_debounce_active) {
            button_debounce_active = true;
            button_debounce_started_ms = now;
        }
    }

    if (!button_debounce_active || (now - button_debounce_started_ms) < BUTTON_DEBOUNCE_MS) {
        return;
    }

    button_debounce_active = false;

    if (digitalRead(QR_BUTTON_PIN) == LOW && (now - last_button_toggle_ms) >= BUTTON_MIN_TOGGLE_INTERVAL_MS) {
        last_button_toggle_ms = now;
        handle_short_press_action();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[系统] 正在启动");

    config_init();
    AppConfig cfg = config_get();

    battery_init();
    battery_set_voltage_factor(cfg.battery_voltage_factor);
    dht.begin();
    pinMode(QR_BUTTON_PIN, INPUT_PULLUP);
    Serial.print("[按键] GPIO17 初始电平=");
    Serial.println(digitalRead(QR_BUTTON_PIN) == LOW ? "LOW" : "HIGH");
    attachInterrupt(digitalPinToInterrupt(QR_BUTTON_PIN), qr_button_isr, FALLING);

    lv_init();
    display.setRotation(USB_LEFT);
    bridge.begin(true, true);
    ui_init();

    web_config_init();
    provisioning_init();
    environment_monitor_init();
    alarm_manager_init();
    history_manager_init();

    bool online = network_init();
    if (!online) {
        Serial.println("[系统] 网络暂未就绪，本地显示继续运行");
    }

    bool initial_sample_valid = sample_sensors();
    report_alarm_transition_if_needed(initial_sample_valid);
    update_main_data_display_if_needed(true);
    Serial.println("[系统] 启动完成");
}

void loop() {
    uint32_t now = millis();
    uint32_t elapsed = now - last_lvgl_tick_ms;
    if (elapsed >= 5) {
        lv_tick_inc(elapsed);
        last_lvgl_tick_ms = now;
    }

    lv_timer_handler();
    ui_commit_refresh_if_needed();
    handle_qr_button();
    network_process();
    handle_report_now_if_requested();
    web_config_loop();
    provisioning_loop();
    update_time_display_if_needed();

    if (ui_current_screen() == UiScreen::PROVISIONING_QR && network_is_wifi_connected() && !provisioning_is_running()) {
        refresh_main_screen();
        Serial.println("[BLE] 配网完成，已返回主界面");
    }

    AppConfig cfg = config_get();
    battery_set_voltage_factor(cfg.battery_voltage_factor);

    if (!cfg.report_enabled) report_pending = false;
    if (!cfg.display_enabled) display_pending = false;

    if (cfg.report_enabled && now - last_upload_ms >= cfg.report_interval_ms) {
        report_pending = true;
    }
    if (cfg.display_enabled && now - last_display_refresh_ms >= cfg.display_refresh_interval_ms) {
        display_pending = true;
    }
    app_state_set_pending(report_pending, display_pending);

    AppRuntimeState runtime_state = app_state_get();
    bool sensor_interval_due = runtime_state.last_sensor_sample_ms == 0 ||
                               (now - runtime_state.last_sensor_sample_ms) >= cfg.sensor_sample_interval_ms;
    bool report_can_output = report_pending && network_is_wifi_connected() && network_is_mqtt_connected();
    bool output_pending = display_pending || report_can_output;
    bool safe_to_sample = runtime_state.last_sensor_sample_ms == 0 ||
                          (now - runtime_state.last_sensor_sample_ms) >= DHT11_SAFE_MIN_SAMPLE_MS;
    bool should_sample = sensor_interval_due || output_pending;

    if (should_sample && safe_to_sample) {
        bool valid = sample_sensors();
        report_alarm_transition_if_needed(valid);
        if (valid && display_pending && cfg.display_enabled) {
            update_main_data_display_if_needed(true);
            last_display_refresh_ms = now;
            display_pending = false;
        } else if (valid && !display_pending && cfg.display_enabled) {
            update_main_data_display_if_needed(false);
        }

        if (valid && report_can_output) {
            if (network_report(last_temperature, last_humidity, last_battery_percent, last_battery_voltage, "interval")) {
                app_state_set_last_mqtt_upload(now);
                last_upload_ms = now;
                report_pending = false;
            }
        }
        app_state_set_pending(report_pending, display_pending);
    }

    yield();
}
