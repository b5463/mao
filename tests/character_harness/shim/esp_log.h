#pragma once
void harness_log(const char *tag, const char *fmt, ...);
#define ESP_LOGI(tag, ...) harness_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) harness_log(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) harness_log(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ((void)0)
