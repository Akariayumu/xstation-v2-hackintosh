// SPDX-License-Identifier: BSD-3-Clause
#ifndef FOCALTECH_SPI_USER_CLIENT_HPP
#define FOCALTECH_SPI_USER_CLIENT_HPP

#include <IOKit/IOUserClient.h>

#include "FocalTechSPI.hpp"

class FocalTechSPIUserClient : public IOUserClient {
    OSDeclareDefaultStructors(FocalTechSPIUserClient)

public:
    bool initWithTask(task_t owningTask,
                      void *securityToken,
                      UInt32 type,
                      OSDictionary *properties) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose() APPLE_KEXT_OVERRIDE;
    IOReturn clientDied() APPLE_KEXT_OVERRIDE;

    IOReturn clientMemoryForType(UInt32 type,
                                 IOOptionBits *options,
                                 IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;

    IOReturn externalMethod(uint32_t selector,
                            IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch,
                            OSObject *target,
                            void *reference) APPLE_KEXT_OVERRIDE;

private:
    static const IOExternalMethodDispatch kMethods[kFocalTechSPIMethodCount];

    static IOReturn sGetHwInfo(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sSetReset(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sSetSpiConfig(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sSpiTransfer(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sWaitIrq(OSObject *target, void *reference, IOExternalMethodArguments *args);

    FocalTechSPI *provider_{nullptr};
    bool sessionHeld_{false};
};

#endif /* FOCALTECH_SPI_USER_CLIENT_HPP */
