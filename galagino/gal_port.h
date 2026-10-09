/*
 * Build glue shared by the Galagino core. The core is plain C with no Pico
 * dependencies, so the same sources build for the board and for the host
 * harness in hosttest/. On the board the hot paths go to SRAM.
 */
#ifndef GAL_PORT_H
#define GAL_PORT_H

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define GAL_HOT(f) __not_in_flash_func(f)
#else
#define GAL_HOT(f) f
#endif

#endif
