// SPDX-License-Identifier: BSD-3-Clause
#include "FocalTechSPI.hpp"
#include "FocalTechSPIUserClient.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/IOPMpowerState.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <string.h>

#define FSPI_LOG(fmt, ...) IOLog("FocalTechSPI: " fmt "\n", ##__VA_ARGS__)

namespace {

enum {
    kPowerStateSleep = 0,
    kPowerStateAwake = 1,
    kPowerStateCount = 2
};

static IOPMPowerState gPowerStates[kPowerStateCount] = {
    {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {kIOPMPowerStateVersion1, kIOPMPowerOn | kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0},
};

}  // namespace

OSDefineMetaClassAndStructors(FocalTechSPI, IOService)

bool FocalTechSPI::init(OSDictionary *dictionary) {
    if (!IOService::init(dictionary)) {
        return false;
    }
    hwLock_ = IOLockAlloc();
    clientLock_ = IOLockAlloc();
    return (hwLock_ != nullptr && clientLock_ != nullptr);
}

bool FocalTechSPI::verifyFpntHid() {
    if (!fpntDevice_) {
        return false;
    }
    OSObject *hidObj = nullptr;
    if (fpntDevice_->evaluateObject("_HID", &hidObj) == kIOReturnSuccess && hidObj) {
        bool match = false;
        if (OSString *hidStr = OSDynamicCast(OSString, hidObj)) {
            match = fspi::isSupportedFpntHid(hidStr->getCStringNoCopy());
        }
        hidObj->release();
        return match;
    }
    if (OSString *propStr = OSDynamicCast(OSString, fpntDevice_->getProperty("name"))) {
        return fspi::isSupportedFpntHid(propStr->getCStringNoCopy());
    }
    if (OSData *propData = OSDynamicCast(OSData, fpntDevice_->getProperty("name"))) {
        const char *bytes = static_cast<const char *>(propData->getBytesNoCopy());
        if (bytes && propData->getLength() == 8u && bytes[7] == '\0') {
            return fspi::isSupportedFpntHid(bytes);
        }
    }
    return false;
}

bool FocalTechSPI::locateAndParseAcpiFpnt(IOPCIDevice *pci) {
    const IORegistryPlane *acpiPlane = IORegistryEntry::getPlane("IOACPIPlane");
    if (!acpiPlane) {
        FSPI_LOG("IOACPIPlane unavailable");
        return false;
    }

    // First try PCI device's linked acpi-device ("SPI1")
    if (IOACPIPlatformDevice *spiAcpi = OSDynamicCast(IOACPIPlatformDevice, pci->getProperty("acpi-device"))) {
        OSIterator *children = spiAcpi->getChildIterator(acpiPlane);
        if (children) {
            while (OSObject *obj = children->getNextObject()) {
                IOACPIPlatformDevice *cand = OSDynamicCast(IOACPIPlatformDevice, obj);
                if (cand && cand->getName() && strcmp(cand->getName(), "FPNT") == 0) {
                    cand->retain();
                    fpntDevice_ = cand;
                    break;
                }
            }
            children->release();
        }
    }

    // Fallback recursive scan on IOACPIPlane for SPI1 -> FPNT and GPI0
    IORegistryIterator *it = IORegistryIterator::iterateOver(acpiPlane, kIORegistryIterateRecursively);
    if (it) {
        while (IORegistryEntry *entry = it->getNextObject()) {
            IOACPIPlatformDevice *adev = OSDynamicCast(IOACPIPlatformDevice, entry);
            if (!adev || !adev->getName()) {
                continue;
            }
            if (!fpntDevice_ && strcmp(adev->getName(), "FPNT") == 0) {
                IORegistryEntry *parent = adev->getParentEntry(acpiPlane);
                if (parent && parent->getName() && strcmp(parent->getName(), "SPI1") == 0) {
                    adev->retain();
                    fpntDevice_ = adev;
                }
            } else if (!gpi0Device_ && strcmp(adev->getName(), "GPI0") == 0) {
                adev->retain();
                gpi0Device_ = adev;
            }
        }
        it->release();
    }

    if (!fpntDevice_) {
        FSPI_LOG("ACPI child FPNT under SPI1 not found");
        return false;
    }
    if (!gpi0Device_) {
        FSPI_LOG("ACPI GPI0 (INT34C5) device not found");
        return false;
    }

    UInt32 sta = 0;
    if (fpntDevice_->evaluateInteger("_STA", &sta) == kIOReturnSuccess && (sta & 0x0Fu) != 0x0Fu) {
        FSPI_LOG("FPNT _STA=0x%x not active; refusing to attach", static_cast<unsigned>(sta));
        return false;
    }

    if (!verifyFpntHid()) {
        FSPI_LOG("FPNT _HID is not FTE3600; refusing to attach");
        return false;
    }

    OSObject *crsObj = nullptr;
    if (fpntDevice_->evaluateObject("_CRS", &crsObj) != kIOReturnSuccess || !crsObj) {
        FSPI_LOG("Failed to evaluate FPNT._CRS");
        return false;
    }
    OSData *crsData = OSDynamicCast(OSData, crsObj);
    bool ok = false;
    if (crsData && crsData->getBytesNoCopy() && crsData->getLength() > 0) {
        ok = fspi::parseFpntCrs(
            static_cast<const uint8_t *>(crsData->getBytesNoCopy()),
            crsData->getLength(),
            &res_);
    }
    crsObj->release();
    if (!ok) {
        FSPI_LOG("Malformed or unsupported FPNT._CRS buffer");
        return false;
    }

    if (!fspi::resolveTglLpGpioGnum(res_.resetGnum, &resetBarIdx_, &resetPadOffset_) ||
        !fspi::resolveTglLpGpioIrqRegs(res_.irqGnum, &irqBarIdx_, &irqPadOffset_, &irqGpiIsOffset_, &irqPinMask_)) {
        FSPI_LOG("GPIO GNUM out of range (reset=%u irq=%u)", res_.resetGnum, res_.irqGnum);
        return false;
    }

    currentSpeedHz_ = fspi::clampSpiSpeedHz(res_.speedHz);
    currentMode_ = res_.spiMode;
    return true;
}

bool FocalTechSPI::mapGpioCommunities() {
    // Map COM0 (BAR 0, GPP_B GSPI1 pads) for read-only pinmux telemetry plus Reset & IRQ BARs.
    const uint32_t neededBars[3] = {0u, resetBarIdx_, irqBarIdx_};
    for (size_t i = 0; i < 3; ++i) {
        const uint32_t barIdx = neededBars[i];
        if (barIdx >= 4u) {
            return false;
        }
        if (gpioBarMaps_[barIdx] != nullptr) {
            continue;
        }
        IODeviceMemory *mem = gpi0Device_->getDeviceMemoryWithIndex(barIdx);
        if (!mem) {
            FSPI_LOG("GPI0 missing IODeviceMemory index %u", barIdx);
            return false;
        }
        IOMemoryMap *map = mem->map();
        if (!map || map->getVirtualAddress() == 0) {
            OSSafeReleaseNULL(map);
            FSPI_LOG("Failed to map GPI0 BAR %u", barIdx);
            return false;
        }
        gpioBarMaps_[barIdx] = map;
    }

    if (gpioBarMaps_[0]->getLength() < 0x08A0u ||
        gpioBarMaps_[resetBarIdx_]->getLength() < resetPadOffset_ + 8u ||
        gpioBarMaps_[irqBarIdx_]->getLength() < irqPadOffset_ + 8u ||
        gpioBarMaps_[irqBarIdx_]->getLength() < irqGpiIsOffset_ + 4u) {
        FSPI_LOG("GPI0 BAR length smaller than pad offset");
        return false;
    }
    irqGpiIsReg_ = reinterpret_cast<volatile uint32_t *>(
        reinterpret_cast<volatile uint8_t *>(gpioBarMaps_[irqBarIdx_]->getVirtualAddress()) + irqGpiIsOffset_);
    return true;
}

void FocalTechSPI::configureIrqPadRoutingLocked(bool enabled) {
    IOMemoryMap *map = (irqBarIdx_ < 4u) ? gpioBarMaps_[irqBarIdx_] : nullptr;
    if (!map || map->getVirtualAddress() == 0) {
        return;
    }
    volatile uint32_t *dw0Ptr = reinterpret_cast<volatile uint32_t *>(
        reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) + irqPadOffset_);
    const uint32_t orig = *dw0Ptr;
    if ((orig & fspi::kPadCfgDw0RoutSci) != 0u) {
        FSPI_LOG("IRQ pad GNUM %u routed to ACPI SCI (DW0=0x%08x); preserving pad routing", res_.irqGnum, orig);
        return;
    }
    if (!irqPadModified_) {
        savedIrqPadDw0_ = orig;
        irqPadModified_ = true;
    }
    const uint32_t next = fspi::configureIrqPadDw0(orig, res_, enabled);
    if (next != orig) {
        *dw0Ptr = next;
        (void)*dw0Ptr;
    }
    if (irqGpiIsReg_) {
        // RW1C: clear only our own pin's latched edge status bit in GPI_IS.
        *irqGpiIsReg_ = irqPinMask_;
        (void)*irqGpiIsReg_;
    }
}

void FocalTechSPI::registerGpioInterrupt() {
    if (!gpi0Device_) {
        return;
    }
    OSIterator *children = gpi0Device_->getChildIterator(gIOServicePlane);
    if (!children) {
        return;
    }
    while (OSObject *obj = children->getNextObject()) {
        IOService *svc = OSDynamicCast(IOService, obj);
        if (!svc || !svc->getMetaClass() || !svc->getMetaClass()->getClassName()) {
            continue;
        }
        const char *clsName = svc->getMetaClass()->getClassName();
        if (strncmp(clsName, "VoodooGPIO", 10) == 0) {
            svc->retain();
            gpioIrqProvider_ = svc;
            break;
        }
    }
    children->release();

    if (!gpioIrqProvider_) {
        FSPI_LOG("VoodooGPIO child not found on GPI0; using gated PAD_CFG_DW0 edge sampling");
        return;
    }

    const int pin = static_cast<int>(res_.irqGnum);
    IOReturn rc = gpioIrqProvider_->registerInterrupt(pin, this, &FocalTechSPI::interruptOccurred, nullptr);
    if (rc == kIOReturnSuccess) {
        // Unmask pin in VoodooGPIO GPI_IE once and keep GPI0 shared IOAPIC IRQ enabled;
        // gate idle vs active strictly at our own pad's PAD_CFG_DW0.RXEVCFG!
        (void)gpioIrqProvider_->enableInterrupt(pin);
        IOLockLock(hwLock_);
        configureIrqPadRoutingLocked(false);
        IOLockUnlock(hwLock_);
        irqRegistered_ = true;
        irqEnabled_ = false;
        FSPI_LOG("Registered GPIO IRQ pin %d via %s (per-pad RXEVCFG gated)", pin, gpioIrqProvider_->getMetaClass()->getClassName());
    } else {
        FSPI_LOG("VoodooGPIO registerInterrupt(%d) returned 0x%x; fallback edge sampling active", pin, rc);
        OSSafeReleaseNULL(gpioIrqProvider_);
    }
}

void FocalTechSPI::setGpioInterruptEnabled(bool enabled) {
    if (!irqRegistered_ || !hwLock_) {
        return;
    }
    IOLockLock(hwLock_);
    irqEnabled_ = enabled;
    if (powerState_ != 0u) {
        configureIrqPadRoutingLocked(enabled);
    }
    IOLockUnlock(hwLock_);
}

void FocalTechSPI::unregisterGpioInterrupt() {
    if (irqRegistered_ && gpioIrqProvider_) {
        setGpioInterruptEnabled(false);
        const int pin = static_cast<int>(res_.irqGnum);
        (void)gpioIrqProvider_->unregisterInterrupt(pin);
        irqRegistered_ = false;
    }
    if (irqPadModified_) {
        IOMemoryMap *map = (irqBarIdx_ < 4u) ? gpioBarMaps_[irqBarIdx_] : nullptr;
        if (map && map->getVirtualAddress() != 0 && hwLock_) {
            IOLockLock(hwLock_);
            volatile uint32_t *dw0Ptr = reinterpret_cast<volatile uint32_t *>(
                reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) + irqPadOffset_);
            *dw0Ptr = savedIrqPadDw0_;
            (void)*dw0Ptr;
            IOLockUnlock(hwLock_);
        }
        irqPadModified_ = false;
    }
    OSSafeReleaseNULL(gpioIrqProvider_);
}

uint32_t FocalTechSPI::mmioRead32(uint32_t offset) const {
    return *reinterpret_cast<volatile const uint32_t *>(bar0_ + offset);
}

void FocalTechSPI::mmioWrite32(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(bar0_ + offset) = value;
}

void FocalTechSPI::setCsActiveLocked(bool active) {
    const uint32_t curCs = mmioRead32(fspi::kRegLpssCsControl);
    const uint32_t nextCs = fspi::computeLpssCsControl(
        curCs, res_.deviceSelect, res_.csPolarityHigh, active);
    mmioWrite32(fspi::kRegLpssCsControl, nextCs);
    // Tiger Lake LPSS cs_clk_stays_gated quirk: force clock gate ON so CS state
    // propagates immediately to the physical pad; keep ON while CS is active.
    mmioWrite32(fspi::kRegLpssClockGate, fspi::kLpssClockGateForceOn);
    (void)mmioRead32(fspi::kRegLpssCsControl);
    IODelay(1);
    if (!active) {
        mmioWrite32(fspi::kRegLpssClockGate, 0u);
    }
}

void FocalTechSPI::drainRxFifoLocked() {
    for (uint32_t i = 0; i < 256u; ++i) {
        if ((mmioRead32(fspi::kRegSssr) & fspi::kSssrRne) == 0u) {
            break;
        }
        (void)mmioRead32(fspi::kRegSsdr);
    }
    mmioWrite32(fspi::kRegSssr, 0x002C0080u);  // PINT | TINT | TUR | ROR
}

void FocalTechSPI::restorePciD0Locked() {
    if (!pciDevice_) {
        return;
    }
    if (pmCapOffset_ != 0u) {
        const UInt16 pmcsr = pciDevice_->configRead16(pmCapOffset_ + 4u);
        if ((pmcsr & 0x03u) != 0u) {
            pciDevice_->configWrite16(pmCapOffset_ + 4u, static_cast<UInt16>(pmcsr & ~0x03u));
            IODelay(10000);  // 10 ms PCI D3hot -> D0 settle
        }
    }
    const uint32_t curBar0 = pciDevice_->configRead32(kIOPCIConfigBaseAddress0);
    if ((curBar0 & ~0xFu) == 0u && (savedBar0Low_ & ~0xFu) != 0u) {
        pciDevice_->configWrite32(kIOPCIConfigBaseAddress0, savedBar0Low_);
        if (bar0Is64Bit_) {
            pciDevice_->configWrite32(kIOPCIConfigBaseAddress1, savedBar0High_);
        }
    }
    pciDevice_->setMemoryEnable(true);
    pciDevice_->setBusMasterEnable(true);
}

void FocalTechSPI::hwInitD0Locked() {
    if (!bar0_) {
        return;
    }
    // 1. Pulse LPSS reset and release FUNC + IDMA (intel_lpss_init_dev)
    mmioWrite32(fspi::kRegLpssPrivResets, 0u);
    (void)mmioRead32(fspi::kRegLpssPrivResets);
    IODelay(5);
    mmioWrite32(fspi::kRegLpssPrivResets, fspi::kLpssResetRelease);
    (void)mmioRead32(fspi::kRegLpssPrivResets);
    IODelay(10);

    // 2. Program LPSS_PRIV_REMAP_ADDR (0x240/0x244)
    if (bar0PhysAddr_ != 0ull) {
        mmioWrite32(fspi::kRegLpssRemapAddrLo, static_cast<uint32_t>(bar0PhysAddr_ & 0xFFFFFFFFull));
        mmioWrite32(fspi::kRegLpssRemapAddrHi, static_cast<uint32_t>(bar0PhysAddr_ >> 32));
    }

    // 3. Ensure LPSS_PRIV_CLOCK_PARAMS (0x200) M/N fractional divider & gate are enabled
    const uint32_t hwClk = mmioRead32(fspi::kRegLpssPrivClockParams);
    const uint32_t targetClk = fspi::ensureLpssClockParams(savedClockParams_ != 0u ? savedClockParams_ : hwClk);
    savedClockParams_ = targetClk;
    mmioWrite32(fspi::kRegLpssPrivClockParams, targetClk & ~0x80000000u);
    (void)mmioRead32(fspi::kRegLpssPrivClockParams);
    IODelay(1);
    mmioWrite32(fspi::kRegLpssPrivClockParams, targetClk);
    (void)mmioRead32(fspi::kRegLpssPrivClockParams);
    IODelay(5);

    // 4. Force clock gate ON while configuring SSP registers
    mmioWrite32(fspi::kRegLpssClockGate, fspi::kLpssClockGateForceOn);
    (void)mmioRead32(fspi::kRegLpssClockGate);

    // 5. Configure LPSS_CNL_SSP thresholds & two-step SSCR0 enable (spi-pxa2xx.c + coreboot gspi.c)
    mmioWrite32(fspi::kRegSscr0, 0u);
    (void)mmioRead32(fspi::kRegSscr0);
    mmioWrite32(fspi::kRegSsirf, 0u);                        // SSIRF_RxThresh(1)
    mmioWrite32(fspi::kRegSsitf, ((32u - 1u) << 8) | 55u);   // TxLo=32, TxHi=56
    mmioWrite32(fspi::kRegSsto, 0u);
    mmioWrite32(fspi::kRegSspsp, 0u);
    drainRxFifoLocked();

    const uint32_t origCs = mmioRead32(fspi::kRegLpssCsControl);
    const uint32_t idleCs = fspi::computeLpssCsControl(
        origCs, res_.deviceSelect, res_.csPolarityHigh, false);
    mmioWrite32(fspi::kRegLpssCsControl, idleCs);
    (void)mmioRead32(fspi::kRegLpssCsControl);

    const uint32_t sscr1 = fspi::computeSscr1(currentMode_) |
                           (res_.csPolarityHigh ? fspi::kSscr1Ifs : 0u);
    const uint32_t sscr0 = fspi::computeSscr0(fspi::kTglLpssSpiSrcClockHz, currentSpeedHz_);
    mmioWrite32(fspi::kRegSscr1, sscr1);
    mmioWrite32(fspi::kRegSscr0, sscr0 & ~0x80u);  // Configure divider/format with SSE=0
    (void)mmioRead32(fspi::kRegSscr0);
    mmioWrite32(fspi::kRegSscr0, sscr0);           // Enable SSE=1
    (void)mmioRead32(fspi::kRegSscr0);

    // Return clock gate to dynamic mode while CS is idle
    mmioWrite32(fspi::kRegLpssClockGate, 0u);
}

uint32_t FocalTechSPI::runLoopbackSelfTestLocked() {
    if (!bar0_) {
        return 0u;
    }
    // Keep external CS deasserted (HIGH) while testing internal SSP TX->RX shift register (SSCR1_LBM)
    mmioWrite32(fspi::kRegLpssClockGate, fspi::kLpssClockGateForceOn);
    (void)mmioRead32(fspi::kRegLpssClockGate);
    setCsActiveLocked(false);
    mmioWrite32(fspi::kRegLpssClockGate, fspi::kLpssClockGateForceOn);

    const uint32_t sscr0 = fspi::computeSscr0(fspi::kTglLpssSpiSrcClockHz, currentSpeedHz_);
    const uint32_t sscr1Lbm = fspi::computeSscr1(currentMode_) |
                              (res_.csPolarityHigh ? fspi::kSscr1Ifs : 0u) |
                              fspi::kSscr1Lbm;
    mmioWrite32(fspi::kRegSscr0, sscr0 & ~0x80u);
    (void)mmioRead32(fspi::kRegSscr0);
    drainRxFifoLocked();
    mmioWrite32(fspi::kRegSscr1, sscr1Lbm);
    mmioWrite32(fspi::kRegSscr0, sscr0);
    (void)mmioRead32(fspi::kRegSscr0);

    static const uint8_t kPat[4] = {0xA5u, 0x5Au, 0xC6u, 0x93u};
    uint8_t rx[4] = {0u, 0u, 0u, 0u};
    for (uint32_t i = 0; i < 4u; ++i) {
        mmioWrite32(fspi::kRegSsdr, kPat[i]);
    }
    for (uint32_t idx = 0, spin = 0; idx < 4u && spin < 2000u; ++spin) {
        if ((mmioRead32(fspi::kRegSssr) & fspi::kSssrRne) != 0u) {
            rx[idx++] = static_cast<uint8_t>(mmioRead32(fspi::kRegSsdr) & 0xFFu);
        } else {
            IODelay(1);
        }
    }

    // Restore clean non-loopback D0 controller state
    hwInitD0Locked();
    return (static_cast<uint32_t>(rx[0]) << 24) |
           (static_cast<uint32_t>(rx[1]) << 16) |
           (static_cast<uint32_t>(rx[2]) << 8) |
           static_cast<uint32_t>(rx[3]);
}

void FocalTechSPI::hwEnterD3Locked() {
    if (bar0_) {
        const uint32_t clk = mmioRead32(fspi::kRegLpssPrivClockParams);
        if ((clk & 0x01u) != 0u) {
            savedClockParams_ = clk;
        }
        setCsActiveLocked(false);
        mmioWrite32(fspi::kRegSscr0, 0u);
        mmioWrite32(fspi::kRegLpssPrivResets, 0u);
        (void)mmioRead32(fspi::kRegLpssPrivResets);
    }
    if (pciDevice_ && pmCapOffset_ != 0u) {
        const UInt16 pmcsr = pciDevice_->configRead16(pmCapOffset_ + 4u);
        if ((pmcsr & 0x03u) != 0x03u) {
            pciDevice_->configWrite16(pmCapOffset_ + 4u, static_cast<UInt16>((pmcsr & ~0x03u) | 0x03u));
        }
    }
}

uint32_t FocalTechSPI::readIrqPinLevel() const {
    IOMemoryMap *map = (irqBarIdx_ < 4u) ? gpioBarMaps_[irqBarIdx_] : nullptr;
    if (!map || map->getVirtualAddress() == 0) {
        return 0u;
    }
    const volatile uint32_t *dw0 = reinterpret_cast<const volatile uint32_t *>(
        reinterpret_cast<const volatile uint8_t *>(map->getVirtualAddress()) + irqPadOffset_);
    return ((*dw0 & fspi::kPadCfgDw0GpioRxState) != 0u) ? 1u : 0u;
}

bool FocalTechSPI::start(IOService *provider) {
    if (!IOService::start(provider)) {
        return false;
    }

    int offArg = 0;
    if (PE_parse_boot_argn("-fspioff", &offArg, sizeof(offArg))) {
        FSPI_LOG("Disabled by -fspioff boot-arg");
        IOService::stop(provider);
        return false;
    }

    pciDevice_ = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice_) {
        FSPI_LOG("Provider is not IOPCIDevice");
        IOService::stop(provider);
        return false;
    }
    pciDevice_->retain();

    if (!pciDevice_->open(this)) {
        FSPI_LOG("Failed to open IOPCIDevice provider");
        stop(provider);
        return false;
    }
    pciOpened_ = true;

    if (!locateAndParseAcpiFpnt(pciDevice_)) {
        stop(provider);
        return false;
    }
    if (!mapGpioCommunities()) {
        stop(provider);
        return false;
    }

    pciDevice_->findPCICapability(kIOPCIPowerManagementCapability, &pmCapOffset_);
    (void)pciDevice_->enablePCIPowerManagement(kPCIPMCSPowerStateD3);

    IOLockLock(hwLock_);
    restorePciD0Locked();
    savedBar0Low_ = pciDevice_->configRead32(kIOPCIConfigBaseAddress0);
    bar0Is64Bit_ = ((savedBar0Low_ & 0x06u) == 0x04u);
    if (bar0Is64Bit_) {
        savedBar0High_ = pciDevice_->configRead32(kIOPCIConfigBaseAddress1);
    }
    IOLockUnlock(hwLock_);

    bar0Map_ = pciDevice_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!bar0Map_ || bar0Map_->getVirtualAddress() == 0 || bar0Map_->getLength() < 0x300u) {
        FSPI_LOG("Failed to map PCI BAR0 MMIO");
        stop(provider);
        return false;
    }
    bar0_ = reinterpret_cast<volatile uint8_t *>(bar0Map_->getVirtualAddress());
    bar0PhysAddr_ = static_cast<uint64_t>(bar0Map_->getPhysicalAddress());

    sharedBuf_ = IOBufferMemoryDescriptor::inTaskWithOptions(
        kernel_task,
        kIOMemoryKernelUserShared | kIODirectionInOut,
        kFocalTechSPIMaxTransfer,
        page_size);
    if (!sharedBuf_ || !sharedBuf_->getBytesNoCopy()) {
        FSPI_LOG("Failed to allocate 64 KB shared transfer buffer");
        stop(provider);
        return false;
    }
    sharedBufBytes_ = static_cast<uint8_t *>(sharedBuf_->getBytesNoCopy());
    bzero(sharedBufBytes_, kFocalTechSPIMaxTransfer);

    workLoop_ = IOWorkLoop::workLoop();
    if (!workLoop_) {
        FSPI_LOG("Failed to create IOWorkLoop");
        stop(provider);
        return false;
    }
    commandGate_ = IOCommandGate::commandGate(this);
    if (!commandGate_ || workLoop_->addEventSource(commandGate_) != kIOReturnSuccess) {
        FSPI_LOG("Failed to attach IOCommandGate");
        stop(provider);
        return false;
    }

    uint32_t lpssCaps = 0;
    uint32_t activeClkParams = 0;
    uint32_t activeCsCtrl = 0;
    uint32_t loopbackRx = 0;
    uint32_t initSssr = 0;
    IOLockLock(hwLock_);
    mmioWrite32(fspi::kRegLpssPrivResets, fspi::kLpssResetRelease);
    (void)mmioRead32(fspi::kRegLpssPrivResets);
    bootClockParams_ = mmioRead32(fspi::kRegLpssPrivClockParams);
    bootCsControl_ = mmioRead32(fspi::kRegLpssCsControl);
    savedClockParams_ = bootClockParams_;
    hwInitD0Locked();
    loopbackRx = runLoopbackSelfTestLocked();
    lpssCaps = mmioRead32(fspi::kRegLpssCaps);
    activeClkParams = mmioRead32(fspi::kRegLpssPrivClockParams);
    activeCsCtrl = mmioRead32(fspi::kRegLpssCsControl);
    initSssr = mmioRead32(fspi::kRegSssr);
    powerState_ = 1u;
    IOLockUnlock(hwLock_);

    lastSampledIrqLevel_ = readIrqPinLevel();
    registerGpioInterrupt();

    PMinit();
    provider->joinPMtree(this);
    pmInitialized_ = true;
    registerPowerDriver(this, gPowerStates, kPowerStateCount);

    const volatile uint8_t *com0Base = reinterpret_cast<const volatile uint8_t *>(
        gpioBarMaps_[0]->getVirtualAddress());
    const uint32_t gppB19Dw0 = *reinterpret_cast<const volatile uint32_t *>(com0Base + 0x0830u);
    const uint32_t gppB20Dw0 = *reinterpret_cast<const volatile uint32_t *>(com0Base + 0x0840u);
    const uint32_t gppB21Dw0 = *reinterpret_cast<const volatile uint32_t *>(com0Base + 0x0850u);
    const uint32_t gppB22Dw0 = *reinterpret_cast<const volatile uint32_t *>(com0Base + 0x0860u);
    const uint32_t gppB25Dw0 = *reinterpret_cast<const volatile uint32_t *>(com0Base + 0x0890u);
    const uint32_t resetDw0 = *reinterpret_cast<const volatile uint32_t *>(
        reinterpret_cast<const volatile uint8_t *>(gpioBarMaps_[resetBarIdx_]->getVirtualAddress()) + resetPadOffset_);
    const uint32_t irqDw0 = *reinterpret_cast<const volatile uint32_t *>(
        reinterpret_cast<const volatile uint8_t *>(gpioBarMaps_[irqBarIdx_]->getVirtualAddress()) + irqPadOffset_);

    setProperty("ACPI HID", "FTE3600");
    setProperty("SPI Speed Hz", static_cast<unsigned long long>(currentSpeedHz_), 32);
    setProperty("Reset GNUM", static_cast<unsigned long long>(res_.resetGnum), 32);
    setProperty("IRQ GNUM", static_cast<unsigned long long>(res_.irqGnum), 32);
    setProperty("IRQ Hardware Registered", irqRegistered_);
    setProperty("Boot LPSS Clock Params", static_cast<unsigned long long>(bootClockParams_), 32);
    setProperty("Active LPSS Clock Params", static_cast<unsigned long long>(activeClkParams), 32);
    setProperty("Boot CS Control", static_cast<unsigned long long>(bootCsControl_), 32);
    setProperty("Active CS Control", static_cast<unsigned long long>(activeCsCtrl), 32);
    setProperty("SSP Loopback Rx", static_cast<unsigned long long>(loopbackRx), 32);
    setProperty("LPSS Caps", static_cast<unsigned long long>(lpssCaps), 32);
    setProperty("Initial SSSR", static_cast<unsigned long long>(initSssr), 32);
    setProperty("GPP_B19 DW0", static_cast<unsigned long long>(gppB19Dw0), 32);
    setProperty("GPP_B20 DW0", static_cast<unsigned long long>(gppB20Dw0), 32);
    setProperty("GPP_B21 DW0", static_cast<unsigned long long>(gppB21Dw0), 32);
    setProperty("GPP_B22 DW0", static_cast<unsigned long long>(gppB22Dw0), 32);
    setProperty("GPP_B25 DW0", static_cast<unsigned long long>(gppB25Dw0), 32);
    setProperty("Reset DW0", static_cast<unsigned long long>(resetDw0), 32);
    setProperty("IRQ DW0", static_cast<unsigned long long>(irqDw0), 32);

    registerService();
    FSPI_LOG("Attached to 8086:a0ab (FPNT FTE3600 speed=%u Hz reset=%u irq=%u)",
             currentSpeedHz_, res_.resetGnum, res_.irqGnum);
    return true;
}

void FocalTechSPI::stop(IOService *provider) {
    if (pmInitialized_) {
        PMstop();
        pmInitialized_ = false;
    }

    unregisterGpioInterrupt();

    if (commandGate_) {
        (void)commandGate_->runAction(&FocalTechSPI::gatedStopDrainAction);
        commandGate_->disable();
        if (workLoop_) {
            workLoop_->removeEventSource(commandGate_);
        }
        OSSafeReleaseNULL(commandGate_);
    }
    OSSafeReleaseNULL(workLoop_);

    if (hwLock_) {
        IOLockLock(hwLock_);
        powerState_ = 0u;
        hwEnterD3Locked();
        bar0_ = nullptr;
        IOLockUnlock(hwLock_);
    }

    sharedBufBytes_ = nullptr;
    irqGpiIsReg_ = nullptr;
    OSSafeReleaseNULL(sharedBuf_);
    OSSafeReleaseNULL(bar0Map_);
    for (size_t i = 0; i < 4; ++i) {
        OSSafeReleaseNULL(gpioBarMaps_[i]);
    }
    OSSafeReleaseNULL(fpntDevice_);
    OSSafeReleaseNULL(gpi0Device_);
    if (pciDevice_) {
        if (pciOpened_) {
            pciDevice_->close(this);
            pciOpened_ = false;
        }
        OSSafeReleaseNULL(pciDevice_);
    }

    IOService::stop(provider);
}

void FocalTechSPI::free() {
    if (hwLock_) {
        IOLockFree(hwLock_);
        hwLock_ = nullptr;
    }
    if (clientLock_) {
        IOLockFree(clientLock_);
        clientLock_ = nullptr;
    }
    IOService::free();
}

IOReturn FocalTechSPI::setPowerState(unsigned long powerStateOrdinal, IOService * /*whatDevice*/) {
    if (!hwLock_) {
        return IOPMAckImplied;
    }

    if (powerStateOrdinal == kPowerStateSleep) {
        if (commandGate_) {
            (void)commandGate_->runAction(&FocalTechSPI::gatedSleepRolloverAction);
        }
        IOLockLock(hwLock_);
        if (powerState_ != 0u) {
            powerState_ = 0u;
            hwEnterD3Locked();
        }
        IOLockUnlock(hwLock_);
    } else {
        bool wokeFromSleep = false;
        IOLockLock(hwLock_);
        if (powerState_ == 0u) {
            restorePciD0Locked();
            hwInitD0Locked();
            powerState_ = 1u;
            wokeFromSleep = true;
        }
        IOLockUnlock(hwLock_);
        if (wokeFromSleep && irqRegistered_) {
            if (gpioIrqProvider_) {
                (void)gpioIrqProvider_->enableInterrupt(static_cast<int>(res_.irqGnum));
            }
            IOLockLock(hwLock_);
            configureIrqPadRoutingLocked(irqEnabled_);
            IOLockUnlock(hwLock_);
        }
        if (commandGate_) {
            (void)commandGate_->runAction(&FocalTechSPI::gatedWakeRefreshAction);
        }
    }
    return IOPMAckImplied;
}

IOReturn FocalTechSPI::newUserClient(task_t owningTask,
                                     void *securityID,
                                     UInt32 type,
                                     OSDictionary *properties,
                                     IOUserClient **handler) {
    if (!handler) {
        return kIOReturnBadArgument;
    }
    *handler = nullptr;

    if (IOUserClient::clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator) != kIOReturnSuccess) {
        return kIOReturnNotPrivileged;
    }

    FocalTechSPIUserClient *client = OSTypeAlloc(FocalTechSPIUserClient);
    if (!client) {
        return kIOReturnNoMemory;
    }
    if (!client->initWithTask(owningTask, securityID, type, properties)) {
        client->release();
        return kIOReturnBadArgument;
    }
    if (!client->attach(this)) {
        client->release();
        return kIOReturnInternalError;
    }
    if (!client->start(this)) {
        client->detach(this);
        client->release();
        return kIOReturnExclusiveAccess;
    }

    *handler = client;
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::newUserClient(task_t owningTask,
                                     void *securityID,
                                     UInt32 type,
                                     IOUserClient **handler) {
    return newUserClient(owningTask, securityID, type, nullptr, handler);
}

bool FocalTechSPI::acquireClientSession(FocalTechSPIUserClient *client) {
    if (!client || !clientLock_ || isInactive()) {
        return false;
    }
    bool acquired = false;
    IOLockLock(clientLock_);
    if (activeClient_ == nullptr) {
        activeClient_ = client;
        acquired = true;
    }
    IOLockUnlock(clientLock_);

    if (acquired) {
        setGpioInterruptEnabled(true);
        if (commandGate_) {
            bool active = true;
            (void)commandGate_->runAction(&FocalTechSPI::gatedSetClientStateAction, &active);
        }
    }
    return acquired;
}

void FocalTechSPI::releaseClientSession(FocalTechSPIUserClient *client) {
    if (!client || !clientLock_) {
        return;
    }
    bool released = false;
    IOLockLock(clientLock_);
    if (activeClient_ == client) {
        activeClient_ = nullptr;
        released = true;
    }
    IOLockUnlock(clientLock_);

    if (released) {
        setGpioInterruptEnabled(false);
        if (commandGate_) {
            bool active = false;
            (void)commandGate_->runAction(&FocalTechSPI::gatedSetClientStateAction, &active);
        }
    }
}

void FocalTechSPI::interruptOccurred(OSObject *owner, void * /*refCon*/, IOService * /*nub*/, int /*source*/) {
    FocalTechSPI *self = static_cast<FocalTechSPI *>(owner);
    if (!self) {
        return;
    }
    // Primary interrupt context: clear our own pin's RW1C GPI_IS edge latch, then atomic counters + wakeup.
    if (volatile uint32_t *isReg = self->irqGpiIsReg_) {
        *isReg = self->irqPinMask_;
    }
    OSIncrementAtomic(&self->hwIrqDeliveredCount_);
    OSIncrementAtomic(&self->pendingHwIrqEdges_);
    if (IOCommandGate *gate = self->commandGate_) {
        gate->commandWakeup(&self->irqWaitToken_, false);
    }
}

void FocalTechSPI::drainPendingIrqEdgesGated() {
    const SInt32 pending = pendingHwIrqEdges_;
    if (pending > 0) {
        OSAddAtomic(-pending, &pendingHwIrqEdges_);
        lease_.recordIrqEdges(static_cast<uint32_t>(pending));
        lastSampledIrqLevel_ = readIrqPinLevel();
    }
}

IOReturn FocalTechSPI::gatedReadLeaseAction(OSObject *owner, void *arg0, void * /*arg1*/, void * /*arg2*/, void * /*arg3*/) {
    FocalTechSPI *self = OSDynamicCast(FocalTechSPI, owner);
    uint32_t *outGen = static_cast<uint32_t *>(arg0);
    if (self && outGen) {
        self->drainPendingIrqEdgesGated();
        *outGen = self->lease_.generation();
    }
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::gatedSleepRolloverAction(OSObject *owner, void * /*arg0*/, void * /*arg1*/, void * /*arg2*/, void * /*arg3*/) {
    FocalTechSPI *self = OSDynamicCast(FocalTechSPI, owner);
    if (self) {
        self->lease_.onSleep();
        if (self->commandGate_) {
            self->commandGate_->commandWakeup(&self->irqWaitToken_, false);
        }
    }
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::gatedWakeRefreshAction(OSObject *owner, void * /*arg0*/, void * /*arg1*/, void * /*arg2*/, void * /*arg3*/) {
    FocalTechSPI *self = OSDynamicCast(FocalTechSPI, owner);
    if (self) {
        self->drainPendingIrqEdgesGated();
        self->lastSampledIrqLevel_ = self->readIrqPinLevel();
        if (self->commandGate_) {
            self->commandGate_->commandWakeup(&self->irqWaitToken_, false);
        }
    }
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::gatedSetClientStateAction(OSObject *owner, void *arg0, void * /*arg1*/, void * /*arg2*/, void * /*arg3*/) {
    FocalTechSPI *self = OSDynamicCast(FocalTechSPI, owner);
    const bool *activePtr = static_cast<const bool *>(arg0);
    if (self && activePtr) {
        self->hasActiveClientGated_ = *activePtr;
        if (!*activePtr && self->commandGate_) {
            self->commandGate_->commandWakeup(&self->irqWaitToken_, false);
        }
    }
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::gatedStopDrainAction(OSObject *owner, void * /*arg0*/, void * /*arg1*/, void * /*arg2*/, void * /*arg3*/) {
    FocalTechSPI *self = OSDynamicCast(FocalTechSPI, owner);
    if (!self || !self->commandGate_) {
        return kIOReturnSuccess;
    }
    self->shuttingDown_ = true;
    self->hasActiveClientGated_ = false;
    self->commandGate_->commandWakeup(&self->irqWaitToken_, false);
    while (self->inFlightWaiters_ > 0u) {
        (void)self->commandGate_->commandSleep(&self->drainToken_, THREAD_UNINT);
    }
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::getHwInfo(FocalTechSPIHwInfo *outInfo) {
    if (!outInfo) {
        return kIOReturnBadArgument;
    }
    uint32_t gen = 0;
    if (commandGate_) {
        (void)commandGate_->runAction(&FocalTechSPI::gatedReadLeaseAction, &gen);
    }
    IOLockLock(hwLock_);
    outInfo->abiVersion = kFocalTechSPIAbiVersion;
    outInfo->sessionGeneration = gen;
    outInfo->acpiSpeedHz = currentSpeedHz_;
    outInfo->resetGnum = res_.resetGnum;
    outInfo->irqGnum = res_.irqGnum;
    outInfo->spiMode = currentMode_;
    outInfo->irqActiveLow = res_.irqActiveLow ? 1u : 0u;
    outInfo->powerState = powerState_;
    outInfo->reserved = 0u;
    IOLockUnlock(hwLock_);
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::setResetPin(uint32_t level) {
    IOLockLock(hwLock_);
    IOMemoryMap *map = (resetBarIdx_ < 4u) ? gpioBarMaps_[resetBarIdx_] : nullptr;
    if (powerState_ == 0u || !map || map->getVirtualAddress() == 0) {
        IOLockUnlock(hwLock_);
        return kIOReturnNotReady;
    }
    volatile uint32_t *dw0 = reinterpret_cast<volatile uint32_t *>(
        reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) + resetPadOffset_);
    *dw0 = fspi::configureResetPadDw0(*dw0, level != 0u);
    (void)*dw0;
    IOLockUnlock(hwLock_);
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::setSpiConfig(uint32_t speedHz, uint8_t spiMode) {
    if (spiMode > 3u) {
        return kIOReturnBadArgument;
    }
    IOLockLock(hwLock_);
    currentSpeedHz_ = fspi::clampSpiSpeedHz(speedHz);
    currentMode_ = spiMode;
    if (powerState_ != 0u && bar0_ != nullptr) {
        hwInitD0Locked();
    }
    IOLockUnlock(hwLock_);
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::spiTransferLocked(uint32_t length) {
    if (powerState_ == 0u || !bar0_ || !sharedBufBytes_) {
        return kIOReturnNotReady;
    }

    const uint32_t deadlineMs = fspi::computeSpiDeadlineMs(length, currentSpeedHz_);
    uint64_t deadline = 0;
    clock_interval_to_deadline(deadlineMs, kMillisecondScale, &deadline);

    mmioWrite32(fspi::kRegLpssClockGate, fspi::kLpssClockGateForceOn);
    (void)mmioRead32(fspi::kRegLpssClockGate);
    drainRxFifoLocked();
    setCsActiveLocked(true);

    uint32_t txIdx = 0;
    uint32_t rxIdx = 0;
    IOReturn status = kIOReturnSuccess;

    while (rxIdx < length) {
        bool progressed = false;

        while (txIdx < length && (txIdx - rxIdx) < 32u &&
               (mmioRead32(fspi::kRegSssr) & fspi::kSssrTnf) != 0u) {
            mmioWrite32(fspi::kRegSsdr, sharedBufBytes_[txIdx++]);
            progressed = true;
        }

        while (rxIdx < txIdx && (mmioRead32(fspi::kRegSssr) & fspi::kSssrRne) != 0u) {
            sharedBufBytes_[rxIdx++] = static_cast<uint8_t>(mmioRead32(fspi::kRegSsdr) & 0xFFu);
            progressed = true;
        }

        if (!progressed) {
            if (mach_absolute_time() > deadline) {
                status = kIOReturnTimeout;
                break;
            }
            IODelay(1);
        }
    }

    if (status == kIOReturnSuccess) {
        while ((mmioRead32(fspi::kRegSssr) & fspi::kSssrBsy) != 0u) {
            if (mach_absolute_time() > deadline) {
                status = kIOReturnTimeout;
                break;
            }
            IODelay(1);
        }
    }

    setCsActiveLocked(false);
    if (status != kIOReturnSuccess) {
        hwInitD0Locked();
    }
    return status;
}

IOReturn FocalTechSPI::spiTransfer(uint32_t length,
                                   IOMemoryDescriptor *inDesc,
                                   const void *inInline,
                                   IOMemoryDescriptor *outDesc,
                                   void *outInline) {
    if (length == 0u || length > kFocalTechSPIMaxTransfer || !hwLock_) {
        return kIOReturnBadArgument;
    }

    bool inPrepared = false;
    bool outPrepared = false;
    if (inDesc) {
        if (inDesc->prepare(kIODirectionOut) != kIOReturnSuccess) {
            return kIOReturnVMError;
        }
        inPrepared = true;
    }
    if (outDesc) {
        if (outDesc->prepare(kIODirectionIn) != kIOReturnSuccess) {
            if (inPrepared) {
                inDesc->complete(kIODirectionOut);
            }
            return kIOReturnVMError;
        }
        outPrepared = true;
    }

    IOLockLock(hwLock_);
    IOReturn status = kIOReturnSuccess;
    if (!sharedBufBytes_) {
        status = kIOReturnNotReady;
    } else if (inDesc) {
        if (inDesc->readBytes(0, sharedBufBytes_, length) != length) {
            status = kIOReturnVMError;
        }
    } else if (inInline) {
        memcpy(sharedBufBytes_, inInline, length);
    }

    if (status == kIOReturnSuccess) {
        status = spiTransferLocked(length);
    }

    if (status == kIOReturnSuccess) {
        if (outDesc) {
            if (outDesc->writeBytes(0, sharedBufBytes_, length) != length) {
                status = kIOReturnVMError;
            }
        } else if (outInline) {
            memcpy(outInline, sharedBufBytes_, length);
        }
    }
    IOLockUnlock(hwLock_);

    if (outPrepared) {
        outDesc->complete(kIODirectionIn);
    }
    if (inPrepared) {
        inDesc->complete(kIODirectionOut);
    }
    return status;
}

IOReturn FocalTechSPI::gatedWaitIrqAction(OSObject *owner, void *arg0, void * /*arg1*/, void * /*arg2*/, void * /*arg3*/) {
    FocalTechSPI *self = OSDynamicCast(FocalTechSPI, owner);
    WaitIrqArgs *args = static_cast<WaitIrqArgs *>(arg0);
    if (!self || !args) {
        return kIOReturnBadArgument;
    }
    return self->waitIrqGated(args);
}

IOReturn FocalTechSPI::waitIrqGated(WaitIrqArgs *args) {
    if (shuttingDown_ || isInactive() || !hasActiveClientGated_) {
        return kIOReturnAborted;
    }

    drainPendingIrqEdgesGated();
    uint32_t curPin = readIrqPinLevel();
    if (curPin != lastSampledIrqLevel_) {
        lastSampledIrqLevel_ = curPin;
        lease_.recordIrqEdge();
    }

    uint32_t status = lease_.classifyWait(args->lastSeq, args->expectedGen);
    if (status != kFocalTechSPIWaitTimeout || args->timeoutMs == 0u) {
        args->outIrqSeq = lease_.irqSeq();
        args->outCurrentGen = lease_.generation();
        args->outPinLevel = curPin;
        args->outWaitStatus = status;
        return kIOReturnSuccess;
    }

    const uint32_t cappedTimeoutMs = (args->timeoutMs > 60000u) ? 60000u : args->timeoutMs;
    uint64_t finalDeadline = 0;
    clock_interval_to_deadline(cappedTimeoutMs, kMillisecondScale, &finalDeadline);

    ++inFlightWaiters_;
    while (!isInactive() && !shuttingDown_ && hasActiveClientGated_) {
        const uint64_t now = mach_absolute_time();
        if (now >= finalDeadline) {
            break;
        }

        const uint32_t sliceMs = (hwIrqDeliveredCount_ > 0) ? 50u : 4u;
        AbsoluteTime sliceDeadline{};
        clock_interval_to_deadline(sliceMs, kMillisecondScale, reinterpret_cast<uint64_t *>(&sliceDeadline));
        if (*reinterpret_cast<uint64_t *>(&sliceDeadline) > finalDeadline) {
            *reinterpret_cast<uint64_t *>(&sliceDeadline) = finalDeadline;
        }

        const IOReturn sleepRc = commandGate_->commandSleep(&irqWaitToken_, sliceDeadline, THREAD_ABORTSAFE);
        if (shuttingDown_ || isInactive() || !hasActiveClientGated_) {
            break;
        }

        drainPendingIrqEdgesGated();
        curPin = readIrqPinLevel();
        if (curPin != lastSampledIrqLevel_) {
            lastSampledIrqLevel_ = curPin;
            lease_.recordIrqEdge();
        }

        status = lease_.classifyWait(args->lastSeq, args->expectedGen);
        if (status != kFocalTechSPIWaitTimeout) {
            break;
        }
        if (sleepRc == THREAD_INTERRUPTED) {
            break;
        }
    }
    --inFlightWaiters_;
    if (shuttingDown_ && inFlightWaiters_ == 0u) {
        commandGate_->commandWakeup(&drainToken_, false);
    }

    if (shuttingDown_ || isInactive() || !hasActiveClientGated_) {
        return kIOReturnAborted;
    }

    args->outIrqSeq = lease_.irqSeq();
    args->outCurrentGen = lease_.generation();
    args->outPinLevel = readIrqPinLevel();
    args->outWaitStatus = lease_.classifyWait(args->lastSeq, args->expectedGen);
    return kIOReturnSuccess;
}

IOReturn FocalTechSPI::waitIrq(uint32_t lastSeq,
                               uint32_t expectedGen,
                               uint32_t timeoutMs,
                               uint32_t *outIrqSeq,
                               uint32_t *outCurrentGen,
                               uint32_t *outPinLevel,
                               uint32_t *outWaitStatus) {
    if (!commandGate_ || !outIrqSeq || !outCurrentGen || !outPinLevel || !outWaitStatus) {
        return kIOReturnBadArgument;
    }
    WaitIrqArgs args{};
    args.lastSeq = lastSeq;
    args.expectedGen = expectedGen;
    args.timeoutMs = timeoutMs;
    const IOReturn rc = commandGate_->runAction(&FocalTechSPI::gatedWaitIrqAction, &args);
    if (rc == kIOReturnSuccess) {
        *outIrqSeq = args.outIrqSeq;
        *outCurrentGen = args.outCurrentGen;
        *outPinLevel = args.outPinLevel;
        *outWaitStatus = args.outWaitStatus;
    }
    return rc;
}
