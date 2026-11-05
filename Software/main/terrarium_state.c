#include "terrarium_state.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "terrarium_state"

#define LEVEL_MAX 100

typedef struct {
    uint8_t heater; /* 0 - 100 % */
    uint8_t fan;    /* 0 - 100 % */
    uint8_t pump;   /* 0 = off, 1 = on */
    uint8_t lamp;   /* 0 - 100 % */
    float temperature;
    float humidity;
} terrarium_state_t;

static terrarium_state_t s_state;
static SemaphoreHandle_t s_lock;

static uint8_t clamp_level(double value)
{
    if (value <= 0.0) {
        return 0;
    }
    if (value >= LEVEL_MAX) {
        return LEVEL_MAX;
    }
    return (uint8_t)(value + 0.5);
}

static uint8_t clamp_pump(double value)
{
    return value >= 0.5 ? 1 : 0;
}

static float clamp_temperature(double value)
{
    if (value < -40.0) {
        return -40.0f;
    }
    if (value > 125.0) {
        return 125.0f;
    }
    return (float)value;
}

static float clamp_humidity(double value)
{
    if (value < 0.0) {
        return 0.0f;
    }
    if (value > 100.0) {
        return 100.0f;
    }
    return (float)value;
}

/* Must be called with s_lock held. */
static char *state_to_json_locked(void)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(object, "heater", s_state.heater);
    cJSON_AddNumberToObject(object, "fan", s_state.fan);
    cJSON_AddNumberToObject(object, "pump", s_state.pump);
    cJSON_AddNumberToObject(object, "lamp", s_state.lamp);

    /* Omit sensor fields until a real reading exists. */
    if (isfinite(s_state.temperature)) {
        cJSON_AddNumberToObject(object, "temperature", s_state.temperature);
    }
    if (isfinite(s_state.humidity)) {
        cJSON_AddNumberToObject(object, "humidity", s_state.humidity);
    }

    char *json = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);

    return json;
}

void terrarium_state_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }

    if (s_lock == NULL) {
        ESP_LOGE(TAG, "failed to create state lock");
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_state, 0, sizeof(s_state));
    /* No sensor readings until the firmware (or a client) provides them. */
    s_state.temperature = NAN;
    s_state.humidity = NAN;
    xSemaphoreGive(s_lock);
}

char *terrarium_state_json(void)
{
    if (s_lock == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    char *json = state_to_json_locked();
    xSemaphoreGive(s_lock);

    return json;
}

char *terrarium_state_handle_client_json(const char *json)
{
    if (json == NULL || s_lock == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGW(TAG, "invalid JSON payload");
        return NULL;
    }

    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "payload is not a JSON object");
        cJSON_Delete(root);
        return NULL;
    }

    bool changed = false;
    const cJSON *item = NULL;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    item = cJSON_GetObjectItemCaseSensitive(root, "heater");
    if (cJSON_IsNumber(item)) {
        uint8_t value = clamp_level(item->valuedouble);
        if (value != s_state.heater) {
            s_state.heater = value;
            changed = true;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "fan");
    if (cJSON_IsNumber(item)) {
        uint8_t value = clamp_level(item->valuedouble);
        if (value != s_state.fan) {
            s_state.fan = value;
            changed = true;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "pump");
    if (cJSON_IsNumber(item)) {
        uint8_t value = clamp_pump(item->valuedouble);
        if (value != s_state.pump) {
            s_state.pump = value;
            changed = true;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "lamp");
    if (cJSON_IsNumber(item)) {
        uint8_t value = clamp_level(item->valuedouble);
        if (value != s_state.lamp) {
            s_state.lamp = value;
            changed = true;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "temperature");
    if (cJSON_IsNumber(item) && isfinite(item->valuedouble)) {
        float value = clamp_temperature(item->valuedouble);
        if (value != s_state.temperature) {
            s_state.temperature = value;
            changed = true;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "humidity");
    if (cJSON_IsNumber(item) && isfinite(item->valuedouble)) {
        float value = clamp_humidity(item->valuedouble);
        if (value != s_state.humidity) {
            s_state.humidity = value;
            changed = true;
        }
    }

    char *result = changed ? state_to_json_locked() : NULL;

    xSemaphoreGive(s_lock);
    cJSON_Delete(root);

    return result;
}

bool terrarium_state_set_sensors(float temperature, float humidity)
{
    if (s_lock == NULL || !isfinite(temperature) || !isfinite(humidity)) {
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    float new_temperature = clamp_temperature(temperature);
    float new_humidity = clamp_humidity(humidity);
    bool changed = (new_temperature != s_state.temperature) ||
                   (new_humidity != s_state.humidity);

    s_state.temperature = new_temperature;
    s_state.humidity = new_humidity;

    xSemaphoreGive(s_lock);

    return changed;
}
