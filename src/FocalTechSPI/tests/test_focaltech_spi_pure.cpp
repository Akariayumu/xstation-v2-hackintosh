// SPDX-License-Identifier: BSD-3-Clause
// Unit tests for FocalTechSPIPure.hpp (AML _CRS parser, TGL-LP GPCL GPIO resolver,
// LPSS_CNL_SSP 100 MHz divider math, and S0ix SessionLease generation rollover).

#include "../FocalTechSPIPure.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Exact ConcatenateResTemplate(BBUF, ConcatenateResTemplate(UBUF, GBUF)) bytes
// from X-STATION V2 DSDT (\_SB.PC00.SPI1.FPNT._CRS) with SPEX=4000000, UPIN=278, GPIN=264.
std::vector<uint8_t> makeXStationFpntCrs(uint16_t resetPin = 278, uint16_t irqPin = 264) {
    // BBUF without trailing EndTag (36 bytes): SpiSerialBusV2
    const uint8_t bbuf[] = {
        0x8e, 0x21, 0x00, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, 0x01, 0x09, 0x00,
        0x00, 0x09, 0x3d, 0x00,  // _SPE = 0x003D0900 (4,000,000 Hz)
        0x08,                    // _LEN = 8 bits
        0x00,                    // _PHA = 0 (ClockPhaseFirst)
        0x00,                    // _POL = 0 (ClockPolarityLow)
        0x00, 0x00,              // _ADR = CS0
        '\\', '_', 'S', 'B', '.', 'P', 'C', '0', '0', '.', 'S', 'P', 'I', '1', 0x00,
    };
    // UBUF without trailing EndTag (35 bytes): GpioIo (OutputOnly)
    uint8_t ubuf[] = {
        0x8c, 0x20, 0x00, 0x01, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x17, 0x00, 0x00, 0x19, 0x00, 0x23, 0x00, 0x00, 0x00,
        static_cast<uint8_t>(resetPin & 0xFFu),
        static_cast<uint8_t>((resetPin >> 8) & 0xFFu),
        '\\', '_', 'S', 'B', '.', 'G', 'P', 'I', '0', 0x00,
    };
    // GBUF without trailing EndTag (35 bytes): GpioInt (Edge, ActiveBoth, ExclusiveAndWake)
    uint8_t gbuf[] = {
        0x8c, 0x20, 0x00, 0x01, 0x00, 0x01, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x17, 0x00, 0x00, 0x19, 0x00, 0x23, 0x00, 0x00, 0x00,
        static_cast<uint8_t>(irqPin & 0xFFu),
        static_cast<uint8_t>((irqPin >> 8) & 0xFFu),
        '\\', '_', 'S', 'B', '.', 'G', 'P', 'I', '0', 0x00,
    };
    const uint8_t endTag[] = {0x79, 0x00};

    std::vector<uint8_t> out;
    out.insert(out.end(), bbuf, bbuf + sizeof(bbuf));
    out.insert(out.end(), ubuf, ubuf + sizeof(ubuf));
    out.insert(out.end(), gbuf, gbuf + sizeof(gbuf));
    out.insert(out.end(), endTag, endTag + sizeof(endTag));
    return out;
}

void testParseFpntCrsValid() {
    const std::vector<uint8_t> crs = makeXStationFpntCrs(278, 264);
    fspi::FpntResources res{};
    assert(fspi::parseFpntCrs(crs.data(), crs.size(), &res));
    assert(res.speedHz == 4000000u);
    assert(res.dataBitLength == 8u);
    assert(res.spiMode == 0u);
    assert(res.csPolarityHigh == false);
    assert(res.deviceSelect == 0u);
    assert(res.resetGnum == 278u);
    assert(res.irqGnum == 264u);
    assert(res.irqEdgeTriggered == true);
    assert(res.irqActiveLow == false);
    assert(res.irqActiveBoth == true);
}

void testParseFpntCrsRejectsMalformed() {
    fspi::FpntResources res{};
    // Zero-length or null buffer
    assert(!fspi::parseFpntCrs(nullptr, 0, &res));
    const uint8_t empty[] = {0x79, 0x00};
    assert(!fspi::parseFpntCrs(empty, sizeof(empty), &res));

    // Truncated mid-descriptor
    std::vector<uint8_t> crs = makeXStationFpntCrs();
    assert(!fspi::parseFpntCrs(crs.data(), crs.size() - 10, &res));

    // Missing GpioInt (only BBUF + UBUF + EndTag)
    std::vector<uint8_t> missingIrq(crs.begin(), crs.begin() + 36 + 35);
    missingIrq.push_back(0x79);
    missingIrq.push_back(0x00);
    assert(!fspi::parseFpntCrs(missingIrq.data(), missingIrq.size(), &res));

    // Unsupported dataBitLength != 8
    std::vector<uint8_t> badBits = makeXStationFpntCrs();
    badBits[16] = 16;  // _LEN = 16
    assert(!fspi::parseFpntCrs(badBits.data(), badBits.size(), &res));
}

void testResolveTglLpGpioGnum() {
    uint32_t barIdx = 99;
    uint32_t padOffset = 0;

    // IRQ pin 264 (0x0108) -> Community BAR 2 (0x006A0000), padOffset 0x0780
    assert(fspi::resolveTglLpGpioGnum(264u, &barIdx, &padOffset));
    assert(barIdx == 2u);
    assert(padOffset == 0x0780u);

    // Reset pin 278 (0x0116) -> Community BAR 2 (0x006A0000), padOffset 0x0860
    assert(fspi::resolveTglLpGpioGnum(278u, &barIdx, &padOffset));
    assert(barIdx == 2u);
    assert(padOffset == 0x0860u);

    // Cross-check TSC1 touch IRQ pin 337 (0x0151) -> BAR 2, padOffset 0x0B80
    assert(fspi::resolveTglLpGpioGnum(337u, &barIdx, &padOffset));
    assert(barIdx == 2u);
    assert(padOffset == 0x0B80u);

    // Boundary pins: 0 -> BAR 0, 0x0700; 359 (0x0167) -> BAR 3, 0x0770
    assert(fspi::resolveTglLpGpioGnum(0u, &barIdx, &padOffset));
    assert(barIdx == 0u && padOffset == 0x0700u);
    assert(fspi::resolveTglLpGpioGnum(359u, &barIdx, &padOffset));
    assert(barIdx == 3u && padOffset == 0x0770u);

    // Out-of-range 360 and unmapped hole 30 (between 0..25 and 32..47) rejected
    assert(!fspi::resolveTglLpGpioGnum(360u, &barIdx, &padOffset));
    assert(!fspi::resolveTglLpGpioGnum(30u, &barIdx, &padOffset));
}

void testComputeSscr0() {
    // 8086:a0ab uses cnl_spi_info (120 MHz): 120 MHz / 4 MHz -> SCR = 29, DSS = 7 (8-bit), SSE = 0x80
    assert(fspi::kTglLpssSpiSrcClockHz == 120000000u);
    const uint32_t sscr0_4m = fspi::computeSscr0(fspi::kTglLpssSpiSrcClockHz, 4000000u);
    assert(sscr0_4m == ((29u << 8) | 0x87u));

    // Clamp > 8 MHz to 8 MHz: 120,000,000 / 8,000,000 = 15 -> SCR = 14 (8.00 MHz <= 8 MHz)
    const uint32_t sscr0_clamp_hi = fspi::computeSscr0(fspi::kTglLpssSpiSrcClockHz, 20000000u);
    const uint32_t sscr0_8m = fspi::computeSscr0(fspi::kTglLpssSpiSrcClockHz, 8000000u);
    assert(sscr0_clamp_hi == sscr0_8m);
    const uint32_t scr_hi = (sscr0_clamp_hi >> 8) & 0xFFFu;
    assert((fspi::kTglLpssSpiSrcClockHz / (scr_hi + 1u)) <= 8000000u);

    // Clamp 0 Hz to 1 MHz -> 120 MHz / 1 MHz = 120 -> SCR = 119
    const uint32_t sscr0_zero = fspi::computeSscr0(fspi::kTglLpssSpiSrcClockHz, 0u);
    assert(sscr0_zero == ((119u << 8) | 0x87u));
}

void testSessionLeaseSleepRollover() {
    fspi::SessionLease lease;
    const uint32_t gen0 = lease.generation();
    assert(gen0 == 1u);
    assert(lease.irqSeq() == 0u);

    lease.recordIrqEdge();
    assert(lease.irqSeq() == 1u);
    assert(lease.classifyWait(0u, gen0) == kFocalTechSPIWaitEdge);
    assert(lease.classifyWait(1u, gen0) == kFocalTechSPIWaitTimeout);

    // S0ix sleep increments generation without mutating irqSeq
    lease.onSleep();
    assert(lease.generation() == gen0 + 1u);
    assert(lease.irqSeq() == 1u);
    assert(lease.classifyWait(0u, gen0) == kFocalTechSPIWaitGenMismatch);
    assert(lease.classifyWait(1u, gen0) == kFocalTechSPIWaitGenMismatch);
    assert(kFocalTechSPIWaitGenMismatch == 2u);
}

void testHidFilterBatchIrqEdgesAndTransferDeadline() {
    // I1: Only FTE3600 ACPI _HID is accepted (reject GXFP3200 / ELAN8D02 / CSFP3511)
    assert(fspi::isSupportedFpntHid("FTE3600"));
    assert(!fspi::isSupportedFpntHid("GXFP3200"));
    assert(!fspi::isSupportedFpntHid("ELAN8D02"));
    assert(!fspi::isSupportedFpntHid(""));
    assert(!fspi::isSupportedFpntHid(nullptr));

    // C1: Batch atomic IRQ edge drain in gated context
    fspi::SessionLease lease;
    lease.recordIrqEdges(3u);
    assert(lease.irqSeq() == 3u);
    assert(lease.classifyWait(0u, lease.generation()) == kFocalTechSPIWaitEdge);

    // I2: 64 KB transfer at 1 MHz (~524 ms wire time) must have > 900 ms deadline
    const uint32_t deadline1m = fspi::computeSpiDeadlineMs(65536u, 1000000u);
    assert(deadline1m >= 1000u);
    const uint32_t deadline4m = fspi::computeSpiDeadlineMs(65536u, 4000000u);
    assert(deadline4m >= 600u && deadline4m < deadline1m);

    // I1 + M1: IRQ PAD_CFG_DW0 configures EdgeBoth + PMODE=0 WITHOUT GPIROUTIOXAPIC (uses GPI_IS/GPI_IE)
    fspi::FpntResources resBoth{};
    resBoth.irqEdgeTriggered = true;
    resBoth.irqActiveBoth = true;
    const uint32_t dw0Both = fspi::configureIrqPadDw0(0x04100C00u, resBoth);
    assert((dw0Both & (0x3u << 25)) == (0x3u << 25));  // RXEVCFG = 3 (Both edges)
    assert((dw0Both & (0xFu << 17)) == 0u);            // GPIROUT* = 0 for VoodooGPIO GPI_IS
    assert((dw0Both & (0xFu << 10)) == 0u);            // PMODE = 0 (GPIO)

    // M1: LPSS_PRIV_CLOCK_PARAMS (0x200) initializes 0 -> unity 1:1 (M=1, N=1, EN=1, UPDATE=1)
    assert(fspi::ensureLpssClockParams(0u) == 0x80010003u);
    assert(fspi::ensureLpssClockParams(0x00020004u) == 0x80020005u);

    // M1: Reset PAD_CFG_DW0 clears PMODE & GPIOTXDIS and drives GPIOTXSTATE
    const uint32_t rstHi = fspi::configureResetPadDw0(0x04000D00u, true);
    assert((rstHi & (0xFu << 10)) == 0u);
    assert((rstHi & (1u << 8)) == 0u);
    assert((rstHi & 1u) == 1u);
    const uint32_t rstLo = fspi::configureResetPadDw0(rstHi, false);
    assert((rstLo & 1u) == 0u);
}

void testComputeLpssCsControlV2() {
    // Tiger Lake-LP (LPSS_CNL_SSP / GSPI_VERSION_2):
    // SPI_CS_CONTROL (0x224) bit 12+cs is INACTIVE polarity (1 = inactive HIGH for active-low CS),
    // bit 1 is CS_STATE (1 = DEASSERT, 0 = ASSERT), bits [9:8] = CS_SEL, bit 0 = CS_MODE_SW.
    const uint32_t idleCs0 = fspi::computeLpssCsControl(0x00003000u, 0u, false, false);
    assert(idleCs0 == 0x00003003u);
    const uint32_t activeCs0 = fspi::computeLpssCsControl(idleCs0, 0u, false, true);
    assert(activeCs0 == 0x00003001u);

    // Even if SPI_CS_CONTROL was previously zeroed (0x0), active-low CS0 must set bit 12 (0x1000)
    assert(fspi::computeLpssCsControl(0u, 0u, false, false) == 0x00001003u);
    assert(fspi::computeLpssCsControl(0u, 0u, false, true) == 0x00001001u);

    // Active-low CS1 sets CS_SEL=1 (0x0100) and CS_1_POL=1 (0x2000) while preserving CS_0_POL
    assert(fspi::computeLpssCsControl(0x00001003u, 1u, false, false) == 0x00003103u);
    assert(fspi::computeLpssCsControl(0x00003103u, 1u, false, true) == 0x00003101u);

    // Active-high CS0 clears CS_0_POL (inactive LOW = 0)
    assert(fspi::computeLpssCsControl(0x00003003u, 0u, true, false) == 0x00002003u);
    assert(fspi::computeLpssCsControl(0x00002003u, 0u, true, true) == 0x00002001u);
}

}  // namespace

int main() {
    testParseFpntCrsValid();
    testParseFpntCrsRejectsMalformed();
    testResolveTglLpGpioGnum();
    testComputeSscr0();
    testSessionLeaseSleepRollover();
    testHidFilterBatchIrqEdgesAndTransferDeadline();
    testComputeLpssCsControlV2();
    std::printf("All FocalTechSPIPure tests passed\n");
    return 0;
}
