/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_power.h"
#include "fsl_codec_common.h"
#include "fsl_codec_adapter.h"
#include "fsl_dialog7212.h"
#include "app.h"
#include "fsl_debug_console.h"
/*${header:end}*/

#include "fsl_inputmux.h"
#include "fsl_dma.h"
#include "fsl_gpio.h"
#include "display_support.h"



/*${variable:start}*/
codec_handle_t codecHandle = {0};

da7212_config_t da7212Config = {
    .i2cConfig    = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE, .codecI2CSourceClock = 16000000},
    .dacSource    = kDA7212_DACSourceInputStream,
    .slaveAddress = DA7212_ADDRESS,
    .protocol     = kDA7212_BusI2S,
    .format       = {.mclk_HZ = 12288000U, .sampleRate = DEMO_AUDIO_SAMPLE_RATE, .bitWidth = DEMO_AUDIO_BIT_WIDTH},
// #if 1 == USE_DA7212_INTERNAL_PLL_MCLK
//     .pll          = &pllConfig,
// 	.sysClkSource = kDA7212_SysClkSourcePLL,
// #else
	.sysClkSource = kDA7212_SysClkSourceMCLK,
//#endif
    .isMaster     = false,
};
codec_config_t boardCodecConfig = {.codecDevType = kCODEC_DA7212, .codecDevConfig = &da7212Config};
/*${variable:end}*/

/*${function:start}*/
static void i2c_release_bus_delay(void)
{
    uint32_t i = 0;
    for (i = 0; i < 100; i++)
    {
        __NOP();
    }
}

void BOARD_I2C_ReleaseBus(void)
{
    uint8_t i = 0;

    GPIO_PortInit(GPIO, BOARD_CODEC_I2C_SDA_PORT);
    GPIO_PortInit(GPIO, BOARD_CODEC_I2C_SCL_PORT);

    BOARD_InitI2CPinsAsGPIO();

    /* Drive SDA low first to simulate a start */
    GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SDA_PORT, BOARD_CODEC_I2C_SDA_PIN, 0U);
    i2c_release_bus_delay();

    /* Send 9 pulses on SCL */
    for (i = 0; i < 9; i++)
    {
        GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SCL_PORT, BOARD_CODEC_I2C_SCL_PIN, 0U);
        i2c_release_bus_delay();

        GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SDA_PORT, BOARD_CODEC_I2C_SDA_PIN, 1U);
        i2c_release_bus_delay();

        GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SCL_PORT, BOARD_CODEC_I2C_SCL_PIN, 1U);
        i2c_release_bus_delay();
        i2c_release_bus_delay();
    }

    /* Send stop */
    GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SCL_PORT, BOARD_CODEC_I2C_SCL_PIN, 0U);
    i2c_release_bus_delay();

    GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SDA_PORT, BOARD_CODEC_I2C_SDA_PIN, 0U);
    i2c_release_bus_delay();

    GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SCL_PORT, BOARD_CODEC_I2C_SCL_PIN, 1U);
    i2c_release_bus_delay();

    GPIO_PinWrite(GPIO, BOARD_CODEC_I2C_SDA_PORT, BOARD_CODEC_I2C_SDA_PIN, 1U);
    i2c_release_bus_delay();
}

int BOARD_CODEC_Init(void)
{
    status_t err = 0;
    err = CODEC_Init(&codecHandle, &boardCodecConfig);
    if (err != kStatus_Success)
    {
        PRINTF("[CODEC] DA7212 init Failed: %d\r\n", err);
        return -1;
    }
    PRINTF("[CODEC] DA7212 init status: %d\r\n", err);

    // /* Invert the DAC data in order to output signal with correct polarity - set DACL_DATINV and DACR_DATINV = 1 */
    // if (WM8904_WriteRegister((wm8904_handle_t *)codecHandle.codecDevHandle, WM8904_AUDIO_IF_0, 0x1850) !=
    //     kStatus_WM8904_Success)
    // {
    //     return -1;
    // }

    /* Invert the DAC data in order to output signal with correct polarity - set DACL_DATINV and DACR_DATINV = 1 */
    if (DA7212_WriteRegister((da7212_handle_t *)codecHandle.codecDevHandle, DIALOG7212_DIG_CTRL, 0x88) !=
        kStatus_Success)
    {
        return -1;
    }

    /* Initial volume kept low for hearing safety. */
    if (CODEC_SetVolume(&codecHandle, ALL_CHANNELS, DEMO_VOLUME) != kStatus_Success)
    {
        return -1;
    }

    return 0;
}

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    if (BOARD_IS_XIP())
    {
        // BOARD_BootClockLPR();
        BOARD_BootClockRUN();
        CLOCK_EnableClock(kCLOCK_Otp);
        CLOCK_EnableClock(kCLOCK_Els);
        CLOCK_EnableClock(kCLOCK_ElsApb);
        RESET_PeripheralReset(kOTP_RST_SHIFT_RSTn);
        RESET_PeripheralReset(kELS_APB_RST_SHIFT_RSTn);
    }
    else
    {
        BOARD_InitBootClocks();
    }
    BOARD_InitDebugConsole();
    BOARD_I2C_ReleaseBus();
    BOARD_InitI2CPins();

    CLOCK_EnableClock(kCLOCK_InputMux);

    /* attach SFRO clock to I2C2 */
    CLOCK_AttachClk(kSFRO_to_FLEXCOMM2);

#if defined(USE_AUDIO_CODEC_AND_DMICS) && (USE_AUDIO_CODEC_AND_DMICS == 1)
    /* attach AUDIO PLL clock to FLEXCOMM1 (I2S1) */
    //CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM1);

    /* attach AUDIO PLL clock to FLEXCOMM1 (I2S0) */
    CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM0);

    /* attach AUDIO PLL clock to MCLK */
    CLOCK_AttachClk(kAUDIO_PLL_to_MCLK_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivMclkClk, 1);
    SYSCTL1->MCLKPINDIR = SYSCTL1_MCLKPINDIR_MCLKPINDIR_MASK;

    /* DMIC source from audio pll at 12.288MHz */
    CLOCK_AttachClk(kAUDIO_PLL_to_DMIC_CLK);
#if (DEMO_AUDIO_SAMPLE_RATE == 48000) || (DEMO_AUDIO_SAMPLE_RATE == 24000)
    CLOCK_SetClkDiv(kCLOCK_DivDmicClk, 4); /* DMIC clock = 12.288MHz / 4 = 3.072MHz */
#elif (DEMO_AUDIO_SAMPLE_RATE == 16000)
    CLOCK_SetClkDiv(kCLOCK_DivDmicClk, 6); /* DMIC clock = 12.288MHz / 6 = 2.048MHz */
#elif (DEMO_AUDIO_SAMPLE_RATE == 12000)
    CLOCK_SetClkDiv(kCLOCK_DivDmicClk, 8); /* DMIC clock = 12.288MHz / 8 = 1.536MHz */
#elif (DEMO_AUDIO_SAMPLE_RATE == 8000)
    CLOCK_SetClkDiv(kCLOCK_DivDmicClk, 12); /* DMIC clock = 12.288MHz / 12 = 1.024MHz */
#else
#error "Unsupported DEMO_AUDIO_SAMPLE_RATE value"
#endif
#endif /* USE_AUDIO_CODEC_AND_DMICS */

    /* Reset GMDA */
    RESET_PeripheralReset(kGDMA_RST_SHIFT_RSTn);
    /* Keep CAU sleep clock here. */
    /* CPU1 uses Internal clock when in low power mode. */
    POWER_ConfigCauInSleep(false);
    BOARD_InitSleepPinConfig();

#if defined(USE_AUDIO_CODEC_AND_DMICS) && (USE_AUDIO_CODEC_AND_DMICS == 1)
    /* Initialize the audio codec (DA7212) so that codecHandle is valid
     * before any streamer or PCM calls attempt to use it. */
    BOARD_CODEC_Init();
#endif /* USE_AUDIO_CODEC_AND_DMICS */

#if (DEMO_PANEL == DEMO_PANEL_LCD_PAR_S035)
    BOARD_InitLcd1Pins();
    CLOCK_AttachClk(kMAIN_CLK_to_LCD_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivLcdClk, 13);
    RESET_PeripheralReset(kLCDIC_RST_SHIFT_RSTn);
#else
    BOARD_InitLcd0Pins();

    /* SPI clock. */
    CLOCK_AttachClk(kSFRO_to_FLEXCOMM1);
    RESET_PeripheralReset(kFC1_RST_SHIFT_RSTn);
#endif

    /* Use 16 MHz clock for the FLEXCOMM2 */
    CLOCK_AttachClk(kSFRO_to_FLEXCOMM2);

    /* DMA */
    DMA_Init(DMA1);
    /* GPIO. */
    GPIO_PortInit(GPIO, 0);
    GPIO_PortInit(GPIO, 1);

    /* inputmux */
    INPUTMUX_Init(INPUTMUX);
    RESET_PeripheralReset(kINPUTMUX_RST_SHIFT_RSTn);
}
/*${function:end}*/
