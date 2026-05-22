#pragma once
namespace mqtt_client {
void begin();
void update();
bool connected();
const char* state_string();
void publish_status();
void publish_lora();
void publish_gps();
void publish_telemetry();
void publish_log(const char* msg);
void publish_event(const char* event_state);
void publish_ack(const char* message_id, const char* command_id, bool ok, const char* info = "");
}
