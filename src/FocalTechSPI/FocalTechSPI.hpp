// SPDX-License-Identifier: BSD-3-Clause
#ifndef FOCALTECH_SPI_HPP
#define FOCALTECH_SPI_HPP

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "FocalTechSPIProtocol.h"
#include "FocalTechSPIPure.hpp"

class FocalTechSPIUserClient;

class FocalTechSPI : public IOService {
    OSDeclareDefaultStructors(FocalTechSPI)

public:
    bool init(OSDictionary *dictionary = nullptr) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;

    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) APPLE_KEXT_OVERRIDE;

    IOReturn newUserClient(task_t owningTask,
                           void *securityID,
                           UInt32 type,
                           OSDictionary *properties,
                           LIBKERN_RETURNS_RETAINED IOUserClient **handler) APPLE_KEXT_OVERRIDE;

    IOReturn newUserClient(task_t owningTask,
                           void *securityID,
                           UInt32 type,
                           LIBKERN_RETURNS_RETAINED IOUserClient **handler) APPLE_KEXT_OVERRIDE;

    bool acquireClientSession(FocalTechSPIUserClient *client);
    void releaseClientSession(FocalTechSPIUserClient *client);

    IOBufferMemoryDescriptor *sharedTransferBuffer() const { return sharedBuf_; }

    IOReturn getHwInfo(FocalTechSPIHwInfo *outInfo);
    IOReturn setResetPin(uint32_t level);
    IOReturn setSpiConfig(uint32_t speedHz, uint8_t spiMode);
    IOReturn spiTransfer(uint32_t length,
                         IOMemoryDescriptor *inDesc,
                         const void *inInline,
                         IOMemoryDescriptor *outDesc,
                         void *outInline);
    IOReturn waitIrq(uint32_t lastSeq,
                     uint32_t expectedGen,
                     uint32_t timeoutMs,
                     uint32_t *outIrqSeq,
                     uint32_t *outCurrentGen,
                     uint32_t *outPinLevel,
                     uint32_t *outWaitStatus);

private:
    struct WaitIrqArgs {
        uint32_t lastSeq;
        uint32_t expectedGen;
        uint32_t timeoutMs;
        uint32_t outIrqSeq;
        uint32_t outCurrentGen;
        uint32_t outPinLevel;
        uint32_t outWaitStatus;
    };

    bool locateAndParseAcpiFpnt(IOPCIDevice *pci);
    bool verifyFpntHid();
    bool mapGpioCommunities();
    void configureIrqPadRoutingLocked(bool enabled);
    void registerGpioInterrupt();
    void unregisterGpioInterrupt();
    void setGpioInterruptEnabled(bool enabled);

    uint32_t mmioRead32(uint32_t offset) const;
    void mmioWrite32(uint32_t offset, uint32_t value);
    void setCsActiveLocked(bool active);
    void drainRxFifoLocked();
    void hwInitD0Locked();
    uint32_t runLoopbackSelfTestLocked();
    void hwEnterD3Locked();
    void restorePciD0Locked();
    IOReturn spiTransferLocked(uint32_t length);

    uint32_t readIrqPinLevel() const;
    void drainPendingIrqEdgesGated();

    static void interruptOccurred(OSObject *owner, void *refCon, IOService *nub, int source);
    static IOReturn gatedReadLeaseAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn gatedSleepRolloverAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn gatedWakeRefreshAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn gatedSetClientStateAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn gatedStopDrainAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn gatedWaitIrqAction(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    IOReturn waitIrqGated(WaitIrqArgs *args);

    IOPCIDevice *pciDevice_{nullptr};
    bool pciOpened_{false};
    IOMemoryMap *bar0Map_{nullptr};
    volatile uint8_t *bar0_{nullptr};
    UInt8 pmCapOffset_{0};
    uint32_t savedBar0Low_{0};
    uint32_t savedBar0High_{0};
    uint64_t bar0PhysAddr_{0};
    uint32_t bootClockParams_{0};
    uint32_t savedClockParams_{0};
    bool bar0Is64Bit_{false};

    IOACPIPlatformDevice *fpntDevice_{nullptr};
    IOACPIPlatformDevice *gpi0Device_{nullptr};
    IOMemoryMap *gpioBarMaps_[4]{nullptr, nullptr, nullptr, nullptr};
    uint32_t resetBarIdx_{0};
    uint32_t resetPadOffset_{0};
    uint32_t irqBarIdx_{0};
    uint32_t irqPadOffset_{0};
    uint32_t irqGpiIsOffset_{0};
    uint32_t irqPinMask_{0};
    volatile uint32_t *irqGpiIsReg_{nullptr};
    uint32_t savedIrqPadDw0_{0};
    bool irqPadModified_{false};

    IOService *gpioIrqProvider_{nullptr};
    bool irqRegistered_{false};
    bool irqEnabled_{false};
    volatile SInt32 hwIrqDeliveredCount_{0};
    volatile SInt32 pendingHwIrqEdges_{0};

    IOWorkLoop *workLoop_{nullptr};
    IOCommandGate *commandGate_{nullptr};
    IOLock *hwLock_{nullptr};
    IOLock *clientLock_{nullptr};
    FocalTechSPIUserClient *activeClient_{nullptr};
    bool hasActiveClientGated_{false};
    bool shuttingDown_{false};
    uint32_t inFlightWaiters_{0};

    IOBufferMemoryDescriptor *sharedBuf_{nullptr};
    uint8_t *sharedBufBytes_{nullptr};

    fspi::FpntResources res_{};
    fspi::SessionLease lease_{};
    uint32_t lastSampledIrqLevel_{0};
    uint8_t irqWaitToken_{0};
    uint8_t drainToken_{0};

    uint32_t currentSpeedHz_{4000000u};
    uint8_t currentMode_{0u};
    uint32_t bootCsControl_{0u};
    uint8_t powerState_{0u};
    bool pmInitialized_{false};
};

#endif /* FOCALTECH_SPI_HPP */
