
# Copyright 2026 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

mcux_set_variable(gcc_wireless_linker_file RW61x_flash.ld)

mcux_set_variable(iar_wireless_linker_file RW61x_flash.icf)
mcux_add_configuration(
    CC "-DSDK_DEBUGCONSOLE=1"
    CX "-DSDK_DEBUGCONSOLE=1"
)


mcux_add_source(
    SOURCES frdmrw612/board.c
            frdmrw612/board.h
)

mcux_add_include(
    INCLUDES frdmrw612
)

mcux_add_source(
    SOURCES frdmrw612/clock_config.c
            frdmrw612/clock_config.h
)

mcux_add_include(
    INCLUDES frdmrw612
)
