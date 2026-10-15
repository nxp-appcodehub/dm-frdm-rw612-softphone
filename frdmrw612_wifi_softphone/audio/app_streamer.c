/*
 * Copyright 2020-2023, 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_common.h"
#include "fsl_debug_console.h"

#include "app_streamer.h"
#include "streamer_pcm.h"
#include "maestro_logging.h"
#include "app_definitions.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_STREAMER_MSG_QUEUE     "app_queue"
#define STREAMER_TASK_NAME         "Streamer"
#define STREAMER_MESSAGE_TASK_NAME "StreamerMessage"

#define STREAMER_TASK_STACK_SIZE         32 * 1024
#define STREAMER_MESSAGE_TASK_STACK_SIZE 2 * 1024

#define AUDIO_RING_BUFFER_SIZE (8000 * 2) // 1 second of 16-bit PCM at 8kHz

/*******************************************************************************
 * Variables
 ******************************************************************************/
static OSA_TASK_HANDLE_DEFINE(msg_thread);

static STREAMER_T *streamer;

typedef struct
{
    int16_t buffer[AUDIO_RING_BUFFER_SIZE / 2]; // Store as 16-bit samples
    uint32_t write_index;                       // Index in samples
    uint32_t read_index;                        // Index in samples
    uint32_t available_samples;
    SemaphoreHandle_t mutex;
} audio_ring_buffer_t;

/* Baresip callbacks */
static struct {
    baresip_ausrc_callback_h ausrc_cb;
    void *ausrc_arg;
    baresip_auplay_callback_h auplay_cb;
    void *auplay_arg;
} g_baresip_callbacks = {0};

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Streamer task for communicating messages
 *
 * This function is the entry point of a task that is manually created by
 * STREAMER_Create.  It listens on a message queue and receives status updates
 * about errors, audio playback state and position.  The application can make
 * use of this data.
 *
 * @param arg Data to be passed to the task
 */
static void STREAMER_MessageTask(void *arg)
{
    STREAMER_MSG_T msg;
    bool exit_thread = false;
    osa_status_t ret;

    (void)arg;

    while (streamer->message_channel_out.is_mq_created == false)
    {
        OSA_TimeDelay(1);
    }

    do
    {
        ret = OSA_MsgQGet(streamer->message_channel_out.mq, (void *)&msg, osaWaitForever_c);
        if (ret != KOSA_StatusSuccess)
        {
            PRINTF("OSA_MsgQGet error: %d\r\n", ret);
            continue;
        }

        switch (msg.id)
        {
            case STREAM_MSG_ERROR:
                PRINTF("STREAM_MSG_ERROR\r\n");
                exit_thread = true;
                STREAMER_Stop();
                break;
            case STREAM_MSG_EOS:
                PRINTF("\nSTREAM_MSG_EOS\r\n");
                exit_thread = true;
                /* Indicate to other software layers that playing has ended. */
                STREAMER_Stop();
                break;
            case STREAM_MSG_UPDATE_POSITION:
                PRINTF("STREAM_MSG_UPDATE_POSITION..");
                PRINTF(" position: %d ms\r", msg.event_data);
                break;
            case STREAM_MSG_CLOSE_TASK:
                PRINTF("STREAM_MSG_CLOSE_TASK\r\n");
                exit_thread = true;
                break;
            default:
                break;
        }

    } while (!exit_thread);

    OSA_MsgQDestroy(streamer->message_channel_out.mq);
    streamer->message_channel_out.is_mq_created = false;

    OSA_TaskDestroy(msg_thread);
}

void streamer_register_baresip_ausrc(baresip_ausrc_callback_h callback, void *arg)
{
    g_baresip_callbacks.ausrc_cb = callback;
    g_baresip_callbacks.ausrc_arg = arg;
}

void streamer_register_baresip_auplay(baresip_auplay_callback_h callback, void *arg)
{
    g_baresip_callbacks.auplay_cb = callback;
    g_baresip_callbacks.auplay_arg = arg;
}

static int streamer_pcm_network_read(uint8_t *buffer, uint32_t length)
{
    D4_ON();

    /* Direct passthrough from baresip - no buffering */
    if (g_baresip_callbacks.auplay_cb) {
        g_baresip_callbacks.auplay_cb((int16_t *)buffer, length / 2, g_baresip_callbacks.auplay_arg);
    } else {
        /* No callback registered yet - fill with silence */
        memset(buffer, 0, length);
    }

    D4_OFF();
    return 0;
}

static int streamer_pcm_network_write(uint8_t *buffer, uint32_t length)
{
    D6_ON();

    /* Direct passthrough to baresip - no buffering */
    if (g_baresip_callbacks.ausrc_cb) {
        g_baresip_callbacks.ausrc_cb((int16_t *)buffer, length / 2, g_baresip_callbacks.ausrc_arg);
    }

    D6_OFF();
    return 0;
}

status_t STREAMER_stream_Create()
{
    STREAMER_CREATE_PARAM params;
    osa_task_def_t thread_attr;
    ELEMENT_PROPERTY_T prop;
    int ret;
    /* Reuse ELEMENT_USB_SRC_INDEX for network source */
    ElementIndex element_ids[]        = {ELEMENT_USB_SRC_INDEX, ELEMENT_SPEAKER_INDEX};
    PipelineElements pipelineElements = {element_ids, sizeof(element_ids) / sizeof(ElementIndex)};

    /* Create streamer */
    strcpy(params.out_mq_name, APP_STREAMER_MSG_QUEUE);
    params.stack_size   = STREAMER_TASK_STACK_SIZE;
    params.task_name    = STREAMER_TASK_NAME;
    params.in_dev_name  = "network_src";
    params.out_dev_name = "speaker";
    params.elements     = pipelineElements;

    streamer = streamer_create(&params);
    if (!streamer)
    {
        return kStatus_Fail;
    }

    /* Add another pipeline */
    params.in_dev_name            = "microphone";
    params.out_dev_name           = "network_sink";
    element_ids[0]                = ELEMENT_MICROPHONE_INDEX;
    element_ids[1]                = ELEMENT_USB_SINK_INDEX;
    pipelineElements.element_ids  = element_ids;
    pipelineElements.num_elements = 2;

    if (streamer_create_pipeline(streamer, 1, params.in_dev_name, params.out_dev_name, pipelineElements, false))
    {
        return kStatus_Fail;
    }

    /* Create message process thread */
    thread_attr.tpriority = OSA_PRIORITY_HIGH;
    thread_attr.tname     = (uint8_t *)STREAMER_MESSAGE_TASK_NAME;
    thread_attr.pthread   = &STREAMER_MessageTask;
    thread_attr.stacksize = STREAMER_MESSAGE_TASK_STACK_SIZE;
    ret                   = OSA_TaskCreate(&msg_thread, &thread_attr, NULL);
    if (KOSA_StatusSuccess != ret)
    {
        return kStatus_Fail;
    }

    /* Pipeline 0: USB_SRC (NETWORK) -> SPEAKER */
    /* Configure Audio Source (Network) */
    // Set sample rate (G.711 is 8kHz)
    prop.prop = PROP_USB_SRC_SET_SAMPLE_RATE;
    prop.val  = DEMO_AUDIO_SAMPLE_RATE;
    streamer_set_property(streamer, 0, prop, true);

    // Set channels (G.711 is mono)
    prop.prop = PROP_USB_SRC_SET_NUM_CHANNELS;
    prop.val  = 1;
    streamer_set_property(streamer, 0, prop, true);

    // Set bit width (PCM output is 16-bit)
    prop.prop = PROP_USB_SRC_SET_BITS_PER_SAMPLE;
    prop.val  = 16;
    streamer_set_property(streamer, 0, prop, true);

    // Set frame size
    prop.prop = PROP_USB_SRC_SET_FRAME_MS;
    prop.val  = DEMO_MIC_FRAME_SIZE;
    streamer_set_property(streamer, 0, prop, true);

    // Set the network read callback
    EXT_AUDIOELEMENT_DESC_T appFunctions = {0};
    appFunctions.open_func               = NULL;
    appFunctions.close_func              = NULL;
    appFunctions.start_func              = NULL;
    appFunctions.process_func            = streamer_pcm_network_read;
    appFunctions.set_param_func          = streamer_pcm_setparams;
    appFunctions.get_param_func          = streamer_pcm_getparams;
    appFunctions.mute_func               = NULL;
    appFunctions.volume_func             = NULL;

    prop.prop = PROP_USB_SRC_SET_APP_FUNCTIONS;
    prop.val  = (uintptr_t)&appFunctions;
    streamer_set_property(streamer, 0, prop, true);

    /* Configure Speaker */
    appFunctions.open_func      = streamer_pcm_tx_open;
    appFunctions.close_func     = streamer_pcm_tx_close;
    appFunctions.start_func     = NULL;
    appFunctions.process_func   = streamer_pcm_write;
    appFunctions.set_param_func = streamer_pcm_setparams;
    appFunctions.get_param_func = streamer_pcm_getparams;
    appFunctions.mute_func      = streamer_pcm_mute;
    appFunctions.volume_func    = streamer_pcm_set_volume;

    prop.prop = PROP_SPEAKER_SET_APP_FUNCTIONS;
    prop.val  = (uintptr_t)&appFunctions;
    streamer_set_property(streamer, 0, prop, true);

    /* Pipeline 1: MICROPHONE -> USB_SINK (NETWORK) */
    prop.prop = PROP_MICROPHONE_SET_NUM_CHANNELS;
    prop.val  = DEMO_MIC_CHANNEL_NUM;
    ret = streamer_set_property(streamer, 1, prop, true);
    if(0 != ret){
        PRINTF("Error in pipeline 1 setting number of channels: %d \r\n", ret);
    };

    prop.prop = PROP_MICROPHONE_SET_BITS_PER_SAMPLE;
    prop.val  = DEMO_AUDIO_BIT_WIDTH;
    ret = streamer_set_property(streamer, 1, prop, true);
    if(0 != ret){
        PRINTF("Error in pipeline 1 setting bits per sample: %d \r\n", ret);
    };

    prop.prop = PROP_MICROPHONE_SET_FRAME_MS;
    prop.val  = DEMO_MIC_FRAME_SIZE;
    ret = streamer_set_property(streamer, 1, prop, true);
    if(0 != ret){
        PRINTF("Error in pipeline 1 setting frame: %d \r\n", ret);
    };

    prop.prop = PROP_MICROPHONE_SET_SAMPLE_RATE;
    prop.val  = DEMO_AUDIO_SAMPLE_RATE;
    ret = streamer_set_property(streamer, 1, prop, true);
    if(0 != ret){
        PRINTF("Error in pipeline 1 setting sample rate: %d \r\n", ret);
    };

    appFunctions.open_func      = streamer_pcm_rx_open;
    appFunctions.close_func     = streamer_pcm_rx_close;
    appFunctions.start_func     = NULL;
    appFunctions.process_func   = streamer_pcm_read;
    appFunctions.set_param_func = streamer_pcm_setparams;
    appFunctions.get_param_func = streamer_pcm_getparams;
    appFunctions.mute_func      = streamer_pcm_mute;
    appFunctions.volume_func    = streamer_pcm_set_volume;
    prop.prop                   = PROP_MICROPHONE_SET_APP_FUNCTIONS;
    prop.val                    = (uintptr_t)&appFunctions;
    ret = streamer_set_property(streamer, 1, prop, true);
    if(0 != ret){
        PRINTF("Error in pipeline 1 setting app functions pt1: %d \r\n", ret);
    };

    /* Configure Audio Sink (Network) */
    appFunctions.open_func      = NULL;
    appFunctions.close_func     = NULL;
    appFunctions.start_func     = NULL;
    appFunctions.process_func   = streamer_pcm_network_write;
    appFunctions.set_param_func = streamer_pcm_setparams;
    appFunctions.get_param_func = streamer_pcm_getparams;
    appFunctions.mute_func      = NULL;
    appFunctions.volume_func    = NULL;

    prop.prop = PROP_USB_SINK_SET_APP_FUNCTIONS;
    prop.val  = (uintptr_t)&appFunctions;
    ret = streamer_set_property(streamer, 1, prop, true);
    if(0 != ret){
        PRINTF("Error in pipeline 1 setting app functions pt2: %d \r\n", ret);
    };

    return kStatus_Success;
}

void STREAMER_Start()
{
    PRINTF("[STREAMER] start \r\n");

    for (size_t i = 0; i < MAX_PIPELINES; i++)
    {
        if (streamer->pipes[i] != (uintptr_t)NULL)
        {
            PRINTF("[STREAMER] streamer ptr: %p, pipe[0]: %p, pipe[1]: %p\r\n", streamer, (void*)streamer->pipes[0], (void*)streamer->pipes[1]);
            if (streamer_set_state(streamer, i, STATE_PLAYING, true) != 0)
            {
                PRINTF("[STREAMER] Playback start failed\r\n");
            }
        }
    }
}

void STREAMER_Stop()
{
    PRINTF("[STREAMER] stop \r\n");

    for (size_t i = 0; i < MAX_PIPELINES; i++)
    {
        if (streamer->pipes[i] != (uintptr_t)NULL)
        {
            streamer_set_state(streamer, i, STATE_NULL, true);
        }
    }
}

void STREAMER_Destroy()
{
    streamer_destroy(streamer);
    deinit_logging();
}

void STREAMER_Init(void)
{
    /* Initialize logging */
    init_logging();

    /* Uncomment below to turn on full debug logging for the streamer. */
    set_debug_module(0xffffffff);
    //     set_debug_level(LOGLVL_DEBUG);
    set_debug_level(0xF);
    set_debug_out(LOGPUT_CONSOLE);
    //    get_debug_state();

    /* Initialize streamer PCM management library. */
    streamer_pcm_init();
}
