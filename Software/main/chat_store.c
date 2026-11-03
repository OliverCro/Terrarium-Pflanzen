#include "chat_store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "chat_store"

typedef struct {
    char id[CHAT_MAX_ID_LEN];
    char name[CHAT_MAX_NAME_LEN];
} chat_user_t;

typedef struct {
    int user_index;
    char id[CHAT_MAX_ID_LEN];
    char text[CHAT_MAX_TEXT_LEN];
} chat_message_t;

static chat_user_t s_users[CHAT_MAX_USERS];
static size_t s_user_count;

static chat_message_t s_messages[CHAT_MAX_MESSAGES];
static size_t s_head;  /* next slot to write */
static size_t s_count; /* number of valid messages, <= CHAT_MAX_MESSAGES */

static SemaphoreHandle_t s_lock;

static void trim_inplace(char *text)
{
    char *start = text;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

void chat_store_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }

    s_user_count = 0;
    s_head = 0;
    s_count = 0;
    memset(s_users, 0, sizeof(s_users));
    memset(s_messages, 0, sizeof(s_messages));
}

/* Must be called with s_lock held. */
static int get_or_create_user_locked(const char *id)
{
    for (size_t i = 0; i < s_user_count; i++) {
        if (strcmp(s_users[i].id, id) == 0) {
            return (int)i;
        }
    }

    if (s_user_count >= CHAT_MAX_USERS) {
        return -1;
    }

    chat_user_t *user = &s_users[s_user_count];
    strlcpy(user->id, id, sizeof(user->id));
    snprintf(user->name, sizeof(user->name), "User%d",
             100 + (int)(esp_random() % 900));

    return (int)s_user_count++;
}

static char *chat_add_message(const char *id, const char *text)
{
    if (id == NULL || text == NULL || s_lock == NULL) {
        return NULL;
    }

    size_t id_len = strlen(id);
    size_t text_len = strlen(text);

    if (id_len >= CHAT_MAX_ID_LEN || text_len >= CHAT_MAX_TEXT_LEN) {
        ESP_LOGW(TAG, "rejecting oversized message (id=%u, text=%u)",
                 (unsigned)id_len, (unsigned)text_len);
        return NULL;
    }

    char trimmed[CHAT_MAX_TEXT_LEN];
    strlcpy(trimmed, text, sizeof(trimmed));
    trim_inplace(trimmed);

    if (trimmed[0] == '\0') {
        return NULL;
    }

    cJSON *object = NULL;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    int user_index = get_or_create_user_locked(id);
    if (user_index >= 0) {
        chat_message_t *message = &s_messages[s_head];
        message->user_index = user_index;
        strlcpy(message->id, id, sizeof(message->id));
        strlcpy(message->text, trimmed, sizeof(message->text));

        s_head = (s_head + 1) % CHAT_MAX_MESSAGES;
        if (s_count < CHAT_MAX_MESSAGES) {
            s_count++;
        }

        object = cJSON_CreateObject();
        if (object != NULL) {
            cJSON_AddStringToObject(object, "user", s_users[user_index].name);
            cJSON_AddStringToObject(object, "text", message->text);
            cJSON_AddStringToObject(object, "id", message->id);
        }
    } else {
        ESP_LOGW(TAG, "user limit reached, dropping message");
    }

    xSemaphoreGive(s_lock);

    if (object == NULL) {
        return NULL;
    }

    char *json = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);

    return json;
}

char *chat_handle_client_json(const char *json)
{
    if (json == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGW(TAG, "invalid JSON payload");
        return NULL;
    }

    char *result = NULL;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");

    if (cJSON_IsString(id) && id->valuestring != NULL &&
        cJSON_IsString(text) && text->valuestring != NULL) {
        result = chat_add_message(id->valuestring, text->valuestring);
    }

    cJSON_Delete(root);
    return result;
}

char *chat_history_json(void)
{
    if (s_lock == NULL) {
        return NULL;
    }

    cJSON *array = cJSON_CreateArray();
    if (array == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    size_t start = (s_head + CHAT_MAX_MESSAGES - s_count) % CHAT_MAX_MESSAGES;

    for (size_t i = 0; i < s_count; i++) {
        const chat_message_t *message =
            &s_messages[(start + i) % CHAT_MAX_MESSAGES];

        if (message->user_index < 0 ||
            message->user_index >= (int)s_user_count) {
            continue;
        }

        cJSON *object = cJSON_CreateObject();
        if (object == NULL) {
            continue;
        }

        cJSON_AddStringToObject(object, "user",
                                s_users[message->user_index].name);
        cJSON_AddStringToObject(object, "text", message->text);
        cJSON_AddStringToObject(object, "id", message->id);
        cJSON_AddItemToArray(array, object);
    }

    xSemaphoreGive(s_lock);

    char *json = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);

    return json;
}
