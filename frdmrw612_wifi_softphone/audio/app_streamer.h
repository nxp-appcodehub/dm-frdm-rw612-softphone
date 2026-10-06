/*
 * Copyright 2020-2021, 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _APP_STREAMER_H_
#define _APP_STREAMER_H_

/* streamer library includes. */
#include "streamer_api.h"
#include "streamer_element_properties.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * API
 ******************************************************************************/

#if defined(__cplusplus)
extern "C" {
#endif

/* Baresip callback interface */
typedef void (*baresip_ausrc_callback_h)(const int16_t *samples, uint32_t num_samples, void *arg);
typedef void (*baresip_auplay_callback_h)(int16_t *samples, uint32_t num_samples, void *arg);

/* Register baresip callbacks */
void streamer_register_baresip_ausrc(baresip_ausrc_callback_h callback, void *arg);
void streamer_register_baresip_auplay(baresip_auplay_callback_h callback, void *arg);

/*!
 * @brief Initialize the Maestro streamer interface
 *
 * This function initializes the Maestro streamer library, and initializes the PCM
 * output interface.  This function should be called before creating a streamer
 * handle.
 */
void STREAMER_Init(void);

/*!
 * @brief Create a Maestro streamer interface handle
 *
 * This function creates a Maestro streamer interface and starts a task for
 * handling media playback, and a task for sending status and error messages
 * back to the application.
 *
 * @param handle Pointer to input handle
 * @return kStatus_Success on success, otherwise an error.
 */
status_t STREAMER_Create();

/*!
 * @brief Create a Maestro streamer interface handle
 *
 * This function creates a Maestro streamer interface and starts a task for
 * handling mic
 *
 * @param handle Pointer to input handle
 * @param out sink type
 * @param file_name The specified file name for saving samples. If not used, use NULL.
 * @return kStatus_Success on success, otherwise an error.
 */

status_t STREAMER_mic_Create();

/*!
 * @brief Create a Maestro streamer interface handle
 *
 * This function creates a Maestro streamer interface and starts a task for
 * handling playback from memory
 *
 * @param handle Pointer to input handle
 * @return kStatus_Success on success, otherwise an error.
 */

status_t STREAMER_play_Create();

/*!
 * @brief Create a Maestro streamer interface handle
 *
 * This function creates a Maestro streamer interface and starts a task for
 * handling playback from stream
 *
 * @param handle Pointer to input handle
 * @return kStatus_Success on success, otherwise an error.
 */

status_t STREAMER_stream_Create();
status_t STREAMER_send_mic_Create();

/*!
 * @brief Destroy an Maestro streamer interface handle
 *
 * This function destroys an Maestro streamer interface and frees associated memory.
 *
 * @param handle Pointer to input handle
 */
void STREAMER_Destroy();

/*!
 * @brief Start audio playback for the streamer interface
 *
 * This function puts the streamer in a playing state, and begins pulling data
 * from the internal ring buffer, filled with calls to STREAMER_Start.
 *
 * @param handle Pointer to input handle
 */
void STREAMER_Start();

/*!
 * @brief Stop audio playback for the streamer interface
 *
 * This function puts the streamer in a stopped state, and ends playback from
 * the audio buffer.  The internal audio buffer is cleared of any data.
 *
 * @param handle Pointer to input handle
 */
void STREAMER_Stop();

/*!
 * @brief Check if streamer interface is playing
 *
 * This function returns true/false of the playing state for the interface
 *
 * @param handle Pointer to input handle
 * @return true if playing, false if not
 */
bool STREAMER_IsPlaying();

#if defined(__cplusplus)
}
#endif

#endif
