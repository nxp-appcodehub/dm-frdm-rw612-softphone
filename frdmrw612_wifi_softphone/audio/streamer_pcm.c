/*
 * Copyright 2022-2023, 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_definitions.h"
#include "board.h"
#include "streamer_pcm.h"
#include "fsl_debug_console.h"
#include "fsl_codec_common.h"
#include "main.h"

int8_t DMIC_started        = 0; /* Indicates that the DMIC transfer has already started. */
int8_t I2S_started         = 0; /* Indicates that the I2S transfer has already started. */

#define SAMPLES_PER_FRAME    (DEMO_AUDIO_SAMPLE_RATE * DEMO_MIC_FRAME_SIZE / 1000U)
#define RECORD_BUFFER_SIZE   (SAMPLES_PER_FRAME * DEMO_AUDIO_BYTE_WIDTH)
#define PLAYBACK_BUFFER_SIZE (DEMO_MIC_CHANNEL_NUM * RECORD_BUFFER_SIZE)
#define BUFFER_NUM           (3U)
#define BUFFER_SIZE          (PLAYBACK_BUFFER_SIZE * BUFFER_NUM)

static pcm_rtos_t pcmHandle = {0};

extern codec_handle_t codecHandle;

static uint16_t volatile s_readIndex = 0U;
SDK_ALIGN(dma_descriptor_t s_dmaDescriptorPingpongCh0[BUFFER_NUM], 16);
SDK_ALIGN(static uint8_t s_buffer[BUFFER_SIZE], 16);

static dmic_transfer_t s_receiveXfer[DEMO_MIC_CHANNEL_NUM][BUFFER_NUM];

// Queue for storing data buffer addresses
typedef struct
{
    int num_buffers; /* Number of all required buffers */
    int head;
    int tail;
    int count;
    uint8_t *buff_addr[I2S_NUM_BUFFERS];
} Streamer_buff_addr_T;

static Streamer_buff_addr_T streamer_buff_addr = {
    .num_buffers = I2S_NUM_BUFFERS, // num_buffers must be one greater than AUDIO_SRC_BUFFER_NUM
    .head        = 0,
    .tail        = 0,
    .count       = 0,
};

int Streamer_buff_addr_Push(uint8_t *buff_addr)
{
    if ((streamer_buff_addr.num_buffers <= 0) || (buff_addr == NULL))
    {
        return -1;
    }

    if (streamer_buff_addr.count == streamer_buff_addr.num_buffers)
    {
        return -1; /* Queue is full */
    }

    taskENTER_CRITICAL();
    streamer_buff_addr.buff_addr[streamer_buff_addr.head] = buff_addr;
    streamer_buff_addr.head = (streamer_buff_addr.head + 1) % streamer_buff_addr.num_buffers;
    streamer_buff_addr.count++;
    taskEXIT_CRITICAL();

    return 0;
}

uint8_t *Streamer_buff_addr_Pull(void)
{
    uint8_t *buff_addr = NULL;

    taskENTER_CRITICAL();
    if (streamer_buff_addr.count > 0)
    {
        buff_addr               = streamer_buff_addr.buff_addr[streamer_buff_addr.tail];
        streamer_buff_addr.tail = (streamer_buff_addr.tail + 1) % streamer_buff_addr.num_buffers;
        streamer_buff_addr.count--;
    }
    taskEXIT_CRITICAL();

    return buff_addr;
}

static void TxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    D3_ON();
    pcm_rtos_t *pcm = (pcm_rtos_t *)userData;
    BaseType_t reschedule;

    xSemaphoreGiveFromISR(pcm->semaphoreTX, &reschedule);
    portYIELD_FROM_ISR(reschedule);
    D3_OFF();
}

static void pdmRxCallback(DMIC_Type *base, dmic_dma_handle_t *handle, status_t status, void *userData)
{
    D5_ON();
    pcm_rtos_t *pcm       = (pcm_rtos_t *)userData;
    BaseType_t reschedule = pdFALSE;

    xSemaphoreGiveFromISR(pcm->semaphoreRX, &reschedule);
    portYIELD_FROM_ISR(reschedule);
    D5_OFF();
}

void streamer_pcm_init(void)
{
    dmic_channel_config_t dmic_channel_cfg;
    DMA_Init(DEMO_DMA);

    DMA_EnableChannel(DEMO_DMA, DEMO_I2S_TX_CHANNEL);
    DMA_SetChannelPriority(DEMO_DMA, DEMO_I2S_TX_CHANNEL, kDMA_ChannelPriority3);
    DMA_CreateHandle(&pcmHandle.i2sTxDmaHandle, DEMO_DMA, DEMO_I2S_TX_CHANNEL);

    DMA_EnableChannel(DEMO_DMA, DEMO_DMIC_RX_CHANNEL);
    DMA_SetChannelPriority(DEMO_DMA, DEMO_DMIC_RX_CHANNEL, kDMA_ChannelPriority2);
    DMA_CreateHandle(&pcmHandle.dmicRxDmaHandleCh0, DEMO_DMA, DEMO_DMIC_RX_CHANNEL);

    NVIC_SetPriority(DMA0_IRQn, 2U);

    memset(&dmic_channel_cfg, 0U, sizeof(dmic_channel_config_t));

    dmic_channel_cfg.divhfclk            = kDMIC_PdmDiv1;
    dmic_channel_cfg.osr                 = 32U;
    dmic_channel_cfg.gainshft            = 6U;
    dmic_channel_cfg.preac2coef          = kDMIC_CompValueZero;
    dmic_channel_cfg.preac4coef          = kDMIC_CompValueZero;
    dmic_channel_cfg.dc_cut_level        = kDMIC_DcCut155;
    dmic_channel_cfg.post_dc_gain_reduce = 6U;
    dmic_channel_cfg.saturate16bit       = 1U;
    dmic_channel_cfg.sample_rate         = kDMIC_PhyFullSpeed;

    DMIC_Init(DMIC0);
#if DEMO_AUDIO_SAMPLE_RATE == 48000
    DMIC_Use2fs(DMIC0, true);
#else
   DMIC_Use2fs(DMIC0, false);
#endif

    DMIC_EnableChannelDma(DMIC0, DEMO_DMIC_CHANNEL, true);
    DMIC_ConfigChannel(DMIC0, DEMO_DMIC_CHANNEL, kDMIC_Left, &dmic_channel_cfg);
    DMIC_FifoChannel(DMIC0, DEMO_DMIC_CHANNEL, FIFO_DEPTH, true, true);

    DMIC_EnableChannnel(DMIC0, DEMO_DMIC_CHANNEL_ENABLE);
    DMIC_TransferCreateHandleDMA(DMIC0, &pcmHandle.dmicDmaHandleCh0, pdmRxCallback, (void *)&pcmHandle,
                                 &pcmHandle.dmicRxDmaHandleCh0);
    DMIC_InstallDMADescriptorMemory(&pcmHandle.dmicDmaHandleCh0, s_dmaDescriptorPingpongCh0, BUFFER_NUM);

    for (int channel = 0; channel < DEMO_MIC_CHANNEL_NUM; channel++)
    {
        for (int ibuf = 0; ibuf < BUFFER_NUM; ibuf++)
        {
            s_receiveXfer[channel][ibuf].data      = &s_buffer[(BUFFER_NUM * channel) + (ibuf * PLAYBACK_BUFFER_SIZE)];
            s_receiveXfer[channel][ibuf].dataWidth = sizeof(uint16_t);
            s_receiveXfer[channel][ibuf].dataSize  = RECORD_BUFFER_SIZE;
            s_receiveXfer[channel][ibuf].dataAddrInterleaveSize = kDMA_AddressInterleave1xWidth;
            int ibuf_next                                       = ibuf + 1;
            if (ibuf_next == BUFFER_NUM)
                ibuf_next = 0;
            s_receiveXfer[channel][ibuf].linkTransfer = &s_receiveXfer[channel][ibuf_next];
        }
    }
    pcmHandle.isFirstRx      = 1;
    pcmHandle.isFirstTx      = 1;
    DMIC_started             = 0;
    I2S_started              = 0;
    streamer_buff_addr.count = 0;
    streamer_buff_addr.head  = 0;
    streamer_buff_addr.tail  = 0;
    s_readIndex              = 0;
}

int streamer_pcm_tx_open(uint32_t num_buffers)
{
    pcmHandle.semaphoreTX = xSemaphoreCreateBinary();
    return 0;
}

int streamer_pcm_rx_open(uint32_t num_buffers)
{
    pcmHandle.semaphoreRX = xSemaphoreCreateBinary();

    /* Clear old samples from FIFOs */
    DMIC_DoFifoReset(DMIC0, DEMO_DMIC_CHANNEL);
    return 0;
}

void streamer_pcm_tx_close(void)
{
    /* Stop playback.  This will flush the SAI transmit buffers. */
    if (pcmHandle.i2sTxHandle.state != 0)
    {
        I2S_TransferAbortDMA(DEMO_I2S_TX, &pcmHandle.i2sTxHandle);
    }
    vSemaphoreDelete(pcmHandle.semaphoreTX);
}

void streamer_pcm_rx_close(void)
{
    /* Abort the DMIC transfer */
    DMIC_TransferAbortReceiveDMA(DMIC0, &pcmHandle.dmicDmaHandleCh0);
    vSemaphoreDelete(pcmHandle.semaphoreRX);
}

int streamer_pcm_write(uint8_t *data, uint32_t size)
{
    /* Split the first transfer into two to ensure the continuity */
    if (pcmHandle.isFirstTx)
    {
        pcmHandle.isFirstTx = 0;
        size /= 2;
    }

    /* Ensure write size is a multiple of 32, otherwise EDMA will assert
     * failure.  Round down for the last chunk of a file/stream. */
    pcmHandle.i2sTxTransfer.dataSize = size - (size % 32);
    pcmHandle.i2sTxTransfer.data     = data;

    if (I2S_started == 0)
    {
        /* need to queue two transmit buffers so when the first one
         * finishes transfer, the other immediatelly starts */
        I2S_TxTransferSendDMA(DEMO_I2S_TX, &pcmHandle.i2sTxHandle, pcmHandle.i2sTxTransfer);
        pcmHandle.i2sTxTransfer.data = data + pcmHandle.i2sTxTransfer.dataSize;
        I2S_TxTransferSendDMA(DEMO_I2S_TX, &pcmHandle.i2sTxHandle, pcmHandle.i2sTxTransfer);
        I2S_started = 1;
        return 0;
    }

    /* Try to queue transfer, only block if busy */
    status_t status = I2S_TxTransferSendDMA(DEMO_I2S_TX, &pcmHandle.i2sTxHandle, pcmHandle.i2sTxTransfer);

    while (status == kStatus_I2S_Busy)
    {
        /* Queue is full, wait for a slot to open */
        if (xSemaphoreTake(pcmHandle.semaphoreTX, portMAX_DELAY) != pdTRUE)
            return -1;

        /* Try again */
        status = I2S_TxTransferSendDMA(DEMO_I2S_TX, &pcmHandle.i2sTxHandle, pcmHandle.i2sTxTransfer);
    }

    if (status != kStatus_Success)
        return -1;

    return 0;
}

int streamer_pcm_read(uint8_t *data, uint32_t size)
{
    uint8_t *buff_addr = NULL;

    /* Handle first call */
    if (pcmHandle.isFirstRx)
    {
        pcmHandle.isFirstRx = 0;
        if (Streamer_buff_addr_Push(data) != 0)
            return -1;
        return 1; // Not ready yet
    }

    /* Start DMIC on second call */
    if (DMIC_started == 0)
    {
        DMIC_TransferReceiveDMA(DMIC0, &pcmHandle.dmicDmaHandleCh0, s_receiveXfer[0], DEMO_DMIC_CHANNEL);
        DMIC_started = 1;
        if (Streamer_buff_addr_Push(data) != 0)
            return -1;
        return 1; // Not ready yet
    }

    /* Wait for data to be ready using semaphore */
    if (xSemaphoreTake(pcmHandle.semaphoreRX, 0) == pdTRUE)
    {
        /* Pull the oldest queued application buffer */
        buff_addr = Streamer_buff_addr_Pull();
        if (buff_addr == NULL)
            return -1;

        /* Copy DMA buffer data to application buffer */
        memcpy(buff_addr, &s_buffer[s_readIndex], size);
        s_readIndex += size;
        if (s_readIndex >= BUFFER_SIZE)
            s_readIndex -= BUFFER_SIZE;

        /* Queue the current buffer for future use (after we freed a slot) */
        if (Streamer_buff_addr_Push(data) != 0)
        {
            /* Buffer push failed - not critical, will retry next time */
        }

        return 0; // Data is ready
    }
    else
    {
        /* Data not ready - try to queue buffer for future use if queue has space */
        Streamer_buff_addr_Push(data); // Ignore return value - queue might be full

        return 1; // Not ready yet
    }
}

int streamer_pcm_setparams(
    uint32_t sample_rate, uint32_t bit_width, uint8_t num_channels, bool tx, bool dummy_tx, int volume)
{
    int ret = 0;

    pcmHandle.sample_rate  = sample_rate;
    pcmHandle.bit_width    = bit_width;
    pcmHandle.num_channels = num_channels;
    pcmHandle.dummy_tx_enable |= dummy_tx;

    /*
     * masterSlave = kI2S_MasterSlaveNormalMaster;
     * mode = kI2S_ModeI2sClassic;
     * rightLow = false;
     * leftJust = false;
     * pdmData = false;
     * sckPol = false;
     * wsPol = false;
     * divider = 1;
     * oneChannel = false;
     * dataLength = 16;
     * frameLength = 32;
     * position = 0;
     * fifoLevel = 4;
     */

    if (tx)
    {
        I2S_TxGetDefaultConfig(&pcmHandle.tx_config);
        pcmHandle.tx_config.divider     = DEMO_I2S_CLOCK_DIVIDER;
        pcmHandle.tx_config.masterSlave = DEMO_I2S_TX_MODE;
        if (pcmHandle.num_channels == 1)
        {
            pcmHandle.tx_config.oneChannel = true;
        }
        I2S_TxInit(DEMO_I2S_TX, &pcmHandle.tx_config);
        I2S_TxTransferCreateHandleDMA(DEMO_I2S_TX, &pcmHandle.i2sTxHandle, &pcmHandle.i2sTxDmaHandle, TxCallback,
                                      (void *)&pcmHandle);
    }
    else
    {
        if (dummy_tx)
        {
            I2S_TxGetDefaultConfig(&pcmHandle.tx_config);
            pcmHandle.tx_config.divider     = DEMO_I2S_CLOCK_DIVIDER;
            pcmHandle.tx_config.masterSlave = DEMO_I2S_TX_MODE;
            I2S_TxInit(DEMO_I2S_TX, &pcmHandle.tx_config);
        }
    }

    PRINTF("[PCM] setparams called, codecHandle valid: %d\r\n", codecHandle.codecDevHandle);

    ret = CODEC_SetMute(&codecHandle, ALL_CHANNELS, true);
    ret = streamer_pcm_set_volume(DEMO_VOLUME);
    if (ret != kStatus_Success)
    {
        return 1;
    }

    return 0;
}

void streamer_pcm_getparams(uint32_t *sample_rate, uint32_t *bit_width, uint8_t *num_channels)
{
    *sample_rate  = pcmHandle.sample_rate;
    *bit_width    = pcmHandle.bit_width;
    *num_channels = pcmHandle.num_channels;
}

int streamer_pcm_mute(bool mute)
{
    status_t ret;
    ret = CODEC_SetMute(&codecHandle, ALL_CHANNELS, mute);
    if (ret != kStatus_Success)
    {
        return 1;
    }

    return 0;
}

int streamer_pcm_set_volume(int volume)
{
    int channel;
    channel =
        (pcmHandle.num_channels == 1) ? kCODEC_PlayChannelHeadphoneLeft | kCODEC_PlayChannelSpeakerLeft : ALL_CHANNELS;

    if (volume <= 0)
        CODEC_SetMute(&codecHandle, ALL_CHANNELS, true);
    else
        CODEC_SetVolume(&codecHandle, channel, volume > CODEC_VOLUME_MAX_VALUE ? CODEC_VOLUME_MAX_VALUE : volume);

    return 0;
}
