#pragma once

#include <stddef.h>

#define CHAT_MAX_MESSAGES 100
#define CHAT_MAX_USERS 20
#define CHAT_MAX_ID_LEN 40
#define CHAT_MAX_TEXT_LEN 200
#define CHAT_MAX_NAME_LEN 16

/* Initialise the user/message store. Call once before the HTTP server starts. */
void chat_store_init(void);

/* Parse a client JSON payload ({"id":"...","text":"..."}), store the message
 * and return a newly allocated JSON object for broadcast.
 * Returns NULL if the payload is invalid, empty or the store is full.
 * The caller owns and must free() the returned string. */
char *chat_handle_client_json(const char *json);

/* Return the full message history as a newly allocated JSON array.
 * The caller owns and must free() the returned string. */
char *chat_history_json(void);
