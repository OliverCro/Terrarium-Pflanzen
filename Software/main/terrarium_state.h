#pragma once

#include <stdbool.h>

/* Terrarium state shared by the web UI and the firmware.
 *
 * The state is transmitted as a single JSON object, for example:
 *
 *   {"heater":40,"fan":20,"pump":1,"lamp":75,
 *    "temperature":22.5,"humidity":58.0}
 *
 * Clients may send a partial object containing only the fields they want to
 * change. The server merges it into the current state and broadcasts the
 * complete object to every connected client. */
void terrarium_state_init(void);

/* Return the complete state as a newly allocated JSON object.
 * The caller owns and must free() the returned string. */
char *terrarium_state_json(void);

/* Merge a client JSON object (partial or complete) into the state.
 * Returns the new complete state as a newly allocated JSON string when
 * anything changed, or NULL for invalid input / unchanged state.
 * The caller owns and must free() the returned string. */
char *terrarium_state_handle_client_json(const char *json);

/* Update the sensor readings. Intended to be called from future sensor code;
 * returns true when the stored values changed. The caller is responsible for
 * broadcasting terrarium_state_json() afterwards. */
bool terrarium_state_set_sensors(float temperature, float humidity);
