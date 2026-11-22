// SPDX-License-Identifier: BSD-3-Clause
// Host- and kernel-compatible pure helpers for FocalTechSPI:
//   1. ACPI 6.4 AML _CRS parser for SpiSerialBusV2 + GpioIo + GpioInt
//   2. Intel Tiger Lake-LP (INT34C5 / X-STATION V2 GPCL) GNUM -> (barIndex, padOffset)
//   3. LPSS_CNL_SSP 100 MHz clock divider & SSCR0/SSCR1 builder
//   4. S0ix sleep generation lease tracker

#ifndef FOCALTECH_SPI_PURE_HPP
#define FOCALTECH_SPI_PURE_HPP

#include <stddef.h>
#include <stdint.h>

#include "FocalTechSPIProtocol.h"

namespace fspi {

static constexpr uint32_t kTglLpssSpiSrcClockHz = 100000000u;
static constexpr uint32_t kMinSpiSpeedHz        = 1000000u;
static constexpr uint32_t kMaxSpiSpeedHz        = 8000000u;

// LPSS_CNL_SSP MMIO register offsets (PCI BAR0)
static constexpr uint32_t kRegSscr0               = 0x000u;
static constexpr uint32_t kRegSscr1               = 0x004u;
static constexpr uint32_t kRegSssr                = 0x008u;
static constexpr uint32_t kRegSsdr                = 0x010u;
static constexpr uint32_t kRegLpssPrivClockParams = 0x200u;
static constexpr uint32_t kRegLpssPrivResets      = 0x204u;
static constexpr uint32_t kRegLpssCsControl       = 0x224u;
static constexpr uint32_t kRegLpssClockGate       = 0x238u;

// SSSR bit definitions
static constexpr uint32_t kSssrTnf = (1u << 2);  // Transmit FIFO Not Full
static constexpr uint32_t kSssrRne = (1u << 3);  // Receive FIFO Not Empty
static constexpr uint32_t kSssrBsy = (1u << 4);  // SSP Busy
static constexpr uint32_t kSssrRor = (1u << 7);  // Receive FIFO Overrun

// SSCR1 bit definitions
static constexpr uint32_t kSscr1Spo = (1u << 3);  // Motorola SPI SSPSCLK Polarity
static constexpr uint32_t kSscr1Sph = (1u << 4);  // Motorola SPI SSPSCLK Phase

// LPSS private register bit definitions
static constexpr uint32_t kLpssResetRelease    = 0x07u;  // Func + DMA reset released
static constexpr uint32_t kLpssCsSwMode        = (1u << 0);
static constexpr uint32_t kLpssCsHigh          = (1u << 1);
static constexpr uint32_t kLpssClockGateForceOn = 0x03u;

// Intel PCH PAD_CFG_DW0 bits
static constexpr uint32_t kPadCfgDw0GpioTxState   = (1u << 0);
static constexpr uint32_t kPadCfgDw0GpioRxState   = (1u << 1);
static constexpr uint32_t kPadCfgDw0GpioTxDis     = (1u << 8);
static constexpr uint32_t kPadCfgDw0GpioRxDis     = (1u << 9);
static constexpr uint32_t kPadCfgDw0PmodeMask     = (0xFu << 10);
static constexpr uint32_t kPadCfgDw0RoutMask      = (0xFu << 17);
static constexpr uint32_t kPadCfgDw0RoutSci       = (1u << 19);
static constexpr uint32_t kPadCfgDw0RoutIoApic    = (1u << 20);
static constexpr uint32_t kPadCfgDw0RxInv         = (1u << 23);
static constexpr uint32_t kPadCfgDw0RxEvCfgShift  = 25u;
static constexpr uint32_t kPadCfgDw0RxEvCfgMask   = (0x3u << 25);

inline bool isSupportedFpntHid(const char *hid) {
    if (!hid) {
        return false;
    }
    return hid[0] == 'F' && hid[1] == 'T' && hid[2] == 'E' &&
           hid[3] == '3' && hid[4] == '6' && hid[5] == '0' &&
           hid[6] == '0' && hid[7] == '\0';
}

struct FpntResources {
    uint32_t speedHz;
    uint16_t deviceSelect;
    uint16_t resetGnum;
    uint16_t irqGnum;
    uint8_t  dataBitLength;
    uint8_t  spiMode;
    bool     csPolarityHigh;
    bool     irqEdgeTriggered;
    bool     irqActiveLow;
    bool     irqActiveBoth;
};

inline uint16_t readLe16(const uint8_t *p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t readLe32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Parse ACPI _CRS buffer containing SpiSerialBusV2 (0x8E subtype 0x02),
// output GpioIo (0x8C connType 0x01), and GpioInt (0x8C connType 0x00).
inline bool parseFpntCrs(const uint8_t *buf, size_t len, FpntResources *out) {
    if (!buf || len < 2 || !out) {
        return false;
    }

    FpntResources parsed{};
    bool sawSpi = false;
    bool sawReset = false;
    bool sawIrq = false;
    bool sawEndTag = false;

    size_t pos = 0;
    while (pos < len) {
        const uint8_t tag = buf[pos];
        if ((tag & 0x80u) == 0u) {
            // Small resource item: bits [6:3] = name, bits [2:0] = payload byte count
            const size_t itemLen = static_cast<size_t>(tag & 0x07u);
            if (pos + 1u + itemLen > len) {
                return false;
            }
            const uint8_t smallName = static_cast<uint8_t>((tag >> 3) & 0x0Fu);
            if (smallName == 0x0Fu) {  // EndTag (0x79)
                sawEndTag = true;
                break;
            }
            pos += 1u + itemLen;
            continue;
        }

        // Large resource item: 1-byte tag + 2-byte LE payload length
        if (pos + 3u > len) {
            return false;
        }
        const uint16_t payloadLen = readLe16(&buf[pos + 1u]);
        const size_t totalLen = 3u + static_cast<size_t>(payloadLen);
        if (pos + totalLen > len) {
            return false;
        }

        const uint8_t *item = &buf[pos];
        if (tag == 0x8Eu) {
            // Serial Bus Connection Descriptor (ACPI 6.4 s6.4.3.8.2)
            if (totalLen < 21u) {
                return false;
            }
            const uint8_t busType = item[5];
            if (busType == 0x02u) {  // SPI
                const uint16_t typeFlags = readLe16(&item[7]);
                const uint16_t typeDataLen = readLe16(&item[10]);
                if (typeDataLen < 9u || 12u + static_cast<size_t>(typeDataLen) > totalLen) {
                    return false;
                }
                const uint32_t speedHz = readLe32(&item[12]);
                const uint8_t bits = item[16];
                const uint8_t phase = item[17];
                const uint8_t polarity = item[18];
                const uint16_t devSel = readLe16(&item[19]);
                if (bits != 8u || speedHz == 0u || phase > 1u || polarity > 1u) {
                    return false;
                }
                // Three-wire mode (bit 0 == 1) is unsupported; require 4-wire SPI
                if ((typeFlags & 0x01u) != 0u) {
                    return false;
                }
                parsed.speedHz = speedHz;
                parsed.dataBitLength = bits;
                parsed.spiMode = static_cast<uint8_t>((polarity << 1) | phase);
                parsed.csPolarityHigh = ((typeFlags & 0x02u) != 0u);
                parsed.deviceSelect = devSel;
                sawSpi = true;
            }
        } else if (tag == 0x8Cu) {
            // GPIO Connection Descriptor (ACPI 6.4 s6.4.3.8.1)
            if (totalLen < 25u) {
                return false;
            }
            const uint8_t connType = item[4];
            const uint16_t intIoFlags = readLe16(&item[7]);
            const uint16_t pinTableOff = readLe16(&item[14]);
            const uint16_t resSrcOff = readLe16(&item[17]);
            if (pinTableOff < 23u ||
                static_cast<size_t>(pinTableOff) + 2u > totalLen ||
                static_cast<size_t>(pinTableOff) + 2u > static_cast<size_t>(resSrcOff)) {
                return false;
            }
            const uint16_t pin = readLe16(&item[pinTableOff]);
            if (connType == 0x01u) {
                // GpioIo: bits [1:0] == 2 -> IoRestrictionOutputOnly (or 0 = IoRestrictionNone)
                const uint8_t restriction = static_cast<uint8_t>(intIoFlags & 0x03u);
                if (restriction == 0x01u) {
                    // InputOnly cannot be Reset
                    return false;
                }
                parsed.resetGnum = pin;
                sawReset = true;
            } else if (connType == 0x00u) {
                // GpioInt: bit 0 = Mode (1=Edge, 0=Level), bits [2:1] = Polarity (0=High, 1=Low, 2=Both)
                const uint8_t polarity = static_cast<uint8_t>((intIoFlags >> 1) & 0x03u);
                parsed.irqGnum = pin;
                parsed.irqEdgeTriggered = ((intIoFlags & 0x01u) != 0u);
                parsed.irqActiveLow = (polarity == 0x01u);
                parsed.irqActiveBoth = (polarity == 0x02u);
                sawIrq = true;
            }
        }

        pos += totalLen;
    }

    if (!sawEndTag || !sawSpi || !sawReset || !sawIrq) {
        return false;
    }
    *out = parsed;
    return true;
}

struct TglGpioGroup {
    uint8_t  barIndex;
    uint8_t  padCount;
    uint16_t padBar;
    uint16_t gnumBase;
};

// Exact 14 active GPCL groups from X-STATION V2 DSDT (dsdt.dsl:8987-9186).
// GPI0 (INT34C5) exposes 4 IODeviceMemory ranges of 64 KB each:
//   BAR 0: 0xFD6E0000 (port 0x006E0000, COM0)
//   BAR 1: 0xFD6D0000 (port 0x006D0000, COM1)
//   BAR 2: 0xFD6A0000 (port 0x006A0000, COM4)
//   BAR 3: 0xFD690000 (port 0x00690000, COM5)
inline bool resolveTglLpGpioGnum(uint32_t gnum, uint32_t *outBarIndex, uint32_t *outPadOffset) {
    if (!outBarIndex || !outPadOffset || gnum >= 0xFFFFu) {
        return false;
    }

    static const TglGpioGroup kGroups[] = {
        {0u, 0x1Au, 0x0700u, 0x0000u},
        {0u, 0x10u, 0x08A0u, 0x0020u},
        {0u, 0x19u, 0x09A0u, 0x0040u},
        {3u, 0x08u, 0x0700u, 0x0160u},
        {1u, 0x08u, 0x0700u, 0x0060u},
        {1u, 0x18u, 0x0780u, 0x0080u},
        {1u, 0x15u, 0x0900u, 0x00A0u},
        {1u, 0x18u, 0x0A50u, 0x00C0u},
        {1u, 0x1Bu, 0x0BD0u, 0x00E0u},
        {2u, 0x18u, 0x0700u, 0x0100u},
        {2u, 0x19u, 0x0880u, 0x0120u},
        {2u, 0x19u, 0x0A70u, 0x0140u},
    };

    for (size_t i = 0; i < sizeof(kGroups) / sizeof(kGroups[0]); ++i) {
        const TglGpioGroup &g = kGroups[i];
        if (gnum >= g.gnumBase && gnum < static_cast<uint32_t>(g.gnumBase) + g.padCount) {
            const uint32_t padIdx = gnum - g.gnumBase;
            const uint32_t padOffset = static_cast<uint32_t>(g.padBar) + padIdx * 0x10u;
            if (padOffset + 8u > 0x10000u) {
                return false;
            }
            *outBarIndex = g.barIndex;
            *outPadOffset = padOffset;
            return true;
        }
    }
    return false;
}

inline uint32_t clampSpiSpeedHz(uint32_t targetHz) {
    if (targetHz == 0u || targetHz < kMinSpiSpeedHz) {
        return kMinSpiSpeedHz;
    }
    if (targetHz > kMaxSpiSpeedHz) {
        return kMaxSpiSpeedHz;
    }
    return targetHz;
}

// Compute SSCR0 for Motorola 8-bit SPI with SSE enabled:
//   busHz = srcClockHz / (SCR + 1) <= clampedTargetHz
inline uint32_t computeSscr0(uint32_t srcClockHz, uint32_t targetHz) {
    const uint32_t clamped = clampSpiSpeedHz(targetHz);
    uint32_t div = (srcClockHz + clamped - 1u) / clamped;
    if (div < 1u) {
        div = 1u;
    }
    if (div > 4096u) {
        div = 4096u;
    }
    const uint32_t scr = div - 1u;
    // DSS = 0x07 (8-bit data), FRF = 0 (Motorola SPI), SSE = bit 7 (0x80), SCR in bits [19:8]
    return (scr << 8) | 0x87u;
}

inline uint32_t computeSscr1(uint8_t spiMode) {
    uint32_t val = 0u;
    if ((spiMode & 0x02u) != 0u) {
        val |= kSscr1Spo;  // CPOL = 1
    }
    if ((spiMode & 0x01u) != 0u) {
        val |= kSscr1Sph;  // CPHA = 1
    }
    return val;
}

inline uint32_t computeSpiDeadlineMs(uint32_t length, uint32_t speedHz) {
    const uint32_t clampedHz = clampSpiSpeedHz(speedHz);
    const uint64_t wireMarginMs =
        (static_cast<uint64_t>(length) * 10000ull + clampedHz - 1ull) / clampedHz;
    const uint64_t totalMs = 500ull + wireMarginMs;
    return (totalMs > 10000ull) ? 10000u : static_cast<uint32_t>(totalMs);
}

inline uint32_t configureIrqPadDw0(uint32_t dw0, const FpntResources &res) {
    uint32_t val = dw0;
    val &= ~(kPadCfgDw0PmodeMask | kPadCfgDw0GpioRxDis |
             kPadCfgDw0RoutMask | kPadCfgDw0RxInv | kPadCfgDw0RxEvCfgMask);
    val |= kPadCfgDw0GpioTxDis | kPadCfgDw0RoutIoApic;
    if (res.irqEdgeTriggered) {
        if (res.irqActiveBoth) {
            val |= (0x3u << kPadCfgDw0RxEvCfgShift);
        } else {
            val |= (0x1u << kPadCfgDw0RxEvCfgShift);
            if (res.irqActiveLow) {
                val |= kPadCfgDw0RxInv;
            }
        }
    } else if (res.irqActiveLow) {
        val |= kPadCfgDw0RxInv;
    }
    return val;
}

class SessionLease {
public:
    SessionLease() : generation_(1u), irqSeq_(0u) {}

    uint32_t generation() const { return generation_; }
    uint32_t irqSeq() const { return irqSeq_; }

    void recordIrqEdge() { ++irqSeq_; }
    void recordIrqEdges(uint32_t count) { irqSeq_ += count; }

    void onSleep() {
        ++generation_;
        if (generation_ == 0u) {
            generation_ = 1u;
        }
    }

    uint32_t classifyWait(uint32_t lastSeenSeq, uint32_t expectedGen) const {
        if (expectedGen != generation_) {
            return kFocalTechSPIWaitGenMismatch;
        }
        if (irqSeq_ != lastSeenSeq) {
            return kFocalTechSPIWaitEdge;
        }
        return kFocalTechSPIWaitTimeout;
    }

private:
    uint32_t generation_;
    uint32_t irqSeq_;
};

}  // namespace fspi

#endif /* FOCALTECH_SPI_PURE_HPP */
