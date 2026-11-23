/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FOCALTECH_SPI_PROTOCOL_H
#define FOCALTECH_SPI_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kFocalTechSPIServiceName "FocalTechSPI"
#define kFocalTechSPIAbiVersion  1u
#define kFocalTechSPIMaxTransfer 65536u

enum FocalTechSPISelector {
    kFocalTechSPIMethodGetHwInfo    = 0,
    kFocalTechSPIMethodSetReset     = 1,
    kFocalTechSPIMethodSetSpiConfig = 2,
    kFocalTechSPIMethodSpiTransfer  = 3,
    kFocalTechSPIMethodWaitIrq      = 4,
    kFocalTechSPIMethodCount        = 5
};

enum FocalTechSPIWaitStatus {
    kFocalTechSPIWaitEdge        = 0,
    kFocalTechSPIWaitTimeout     = 1,
    kFocalTechSPIWaitGenMismatch = 2
};

struct FocalTechSPIHwInfo {
    uint32_t abiVersion;
    uint32_t sessionGeneration;
    uint32_t acpiSpeedHz;
    uint32_t resetGnum;
    uint32_t irqGnum;
    uint8_t  spiMode;
    uint8_t  irqActiveLow;
    uint8_t  powerState;
    uint8_t  reserved;
};

#ifdef __cplusplus
}
#endif

#endif /* FOCALTECH_SPI_PROTOCOL_H */
