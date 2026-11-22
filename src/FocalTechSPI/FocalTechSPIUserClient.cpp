// SPDX-License-Identifier: BSD-3-Clause
#include "FocalTechSPIUserClient.hpp"

#include <IOKit/IOLib.h>
#include <string.h>

OSDefineMetaClassAndStructors(FocalTechSPIUserClient, IOUserClient)

const IOExternalMethodDispatch FocalTechSPIUserClient::kMethods[kFocalTechSPIMethodCount] = {
    // 0: kFocalTechSPIMethodGetHwInfo () -> structOutput FocalTechSPIHwInfo
    {&FocalTechSPIUserClient::sGetHwInfo, 0, 0, 0, sizeof(FocalTechSPIHwInfo)},
    // 1: kFocalTechSPIMethodSetReset (level) -> void
    {&FocalTechSPIUserClient::sSetReset, 1, 0, 0, 0},
    // 2: kFocalTechSPIMethodSetSpiConfig (speedHz, mode) -> void
    {&FocalTechSPIUserClient::sSetSpiConfig, 2, 0, 0, 0},
    // 3: kFocalTechSPIMethodSpiTransfer (length, optional structIn/Out) -> status
    {&FocalTechSPIUserClient::sSpiTransfer, 1, kIOUCVariableStructureSize, 1, kIOUCVariableStructureSize},
    // 4: kFocalTechSPIMethodWaitIrq (lastSeq, expectedGen, timeoutMs) -> (irqSeq, currentGen, pinLevel, waitStatus)
    {&FocalTechSPIUserClient::sWaitIrq, 3, 0, 4, 0},
};

bool FocalTechSPIUserClient::initWithTask(task_t owningTask,
                                          void *securityToken,
                                          UInt32 type,
                                          OSDictionary *properties) {
    if (clientHasPrivilege(securityToken, kIOClientPrivilegeAdministrator) != kIOReturnSuccess) {
        return false;
    }
    return IOUserClient::initWithTask(owningTask, securityToken, type, properties);
}

bool FocalTechSPIUserClient::start(IOService *provider) {
    FocalTechSPI *spi = OSDynamicCast(FocalTechSPI, provider);
    if (!spi || !IOUserClient::start(provider)) {
        return false;
    }
    if (!spi->acquireClientSession(this)) {
        IOUserClient::stop(provider);
        return false;
    }
    provider_ = spi;
    provider_->retain();
    sessionHeld_ = true;
    return true;
}

void FocalTechSPIUserClient::stop(IOService *provider) {
    if (sessionHeld_ && provider_) {
        provider_->releaseClientSession(this);
        sessionHeld_ = false;
    }
    OSSafeReleaseNULL(provider_);
    IOUserClient::stop(provider);
}

IOReturn FocalTechSPIUserClient::clientClose() {
    if (sessionHeld_ && provider_) {
        provider_->releaseClientSession(this);
        sessionHeld_ = false;
    }
    if (!isInactive()) {
        terminate();
    }
    return kIOReturnSuccess;
}

IOReturn FocalTechSPIUserClient::clientDied() {
    return clientClose();
}

IOReturn FocalTechSPIUserClient::clientMemoryForType(UInt32 type,
                                                     IOOptionBits *options,
                                                     IOMemoryDescriptor **memory) {
    if (type != 0u || !options || !memory || !provider_) {
        return kIOReturnBadArgument;
    }
    IOBufferMemoryDescriptor *buf = provider_->sharedTransferBuffer();
    if (!buf) {
        return kIOReturnNotReady;
    }
    buf->retain();
    *options = 0;
    *memory = buf;
    return kIOReturnSuccess;
}

IOReturn FocalTechSPIUserClient::externalMethod(uint32_t selector,
                                                IOExternalMethodArguments *arguments,
                                                IOExternalMethodDispatch *dispatch,
                                                OSObject *target,
                                                void *reference) {
    if (selector >= kFocalTechSPIMethodCount || !provider_ || isInactive()) {
        return kIOReturnBadArgument;
    }
    dispatch = const_cast<IOExternalMethodDispatch *>(&kMethods[selector]);
    target = this;
    return IOUserClient::externalMethod(selector, arguments, dispatch, target, reference);
}

IOReturn FocalTechSPIUserClient::sGetHwInfo(OSObject *target, void * /*reference*/, IOExternalMethodArguments *args) {
    FocalTechSPIUserClient *self = OSDynamicCast(FocalTechSPIUserClient, target);
    if (!self || !self->provider_ || !args->structureOutput ||
        args->structureOutputSize < sizeof(FocalTechSPIHwInfo)) {
        return kIOReturnBadArgument;
    }
    args->structureOutputSize = sizeof(FocalTechSPIHwInfo);
    return self->provider_->getHwInfo(static_cast<FocalTechSPIHwInfo *>(args->structureOutput));
}

IOReturn FocalTechSPIUserClient::sSetReset(OSObject *target, void * /*reference*/, IOExternalMethodArguments *args) {
    FocalTechSPIUserClient *self = OSDynamicCast(FocalTechSPIUserClient, target);
    if (!self || !self->provider_) {
        return kIOReturnBadArgument;
    }
    return self->provider_->setResetPin(args->scalarInput[0] ? 1u : 0u);
}

IOReturn FocalTechSPIUserClient::sSetSpiConfig(OSObject *target, void * /*reference*/, IOExternalMethodArguments *args) {
    FocalTechSPIUserClient *self = OSDynamicCast(FocalTechSPIUserClient, target);
    if (!self || !self->provider_) {
        return kIOReturnBadArgument;
    }
    return self->provider_->setSpiConfig(
        static_cast<uint32_t>(args->scalarInput[0]),
        static_cast<uint8_t>(args->scalarInput[1] & 0xFFu));
}

IOReturn FocalTechSPIUserClient::sSpiTransfer(OSObject *target, void * /*reference*/, IOExternalMethodArguments *args) {
    FocalTechSPIUserClient *self = OSDynamicCast(FocalTechSPIUserClient, target);
    if (!self || !self->provider_ || !args->scalarOutput) {
        return kIOReturnBadArgument;
    }
    const uint64_t reqLen64 = args->scalarInput[0];
    if (reqLen64 == 0u || reqLen64 > kFocalTechSPIMaxTransfer) {
        return kIOReturnBadArgument;
    }
    const uint32_t len = static_cast<uint32_t>(reqLen64);
    void *sharedBytes = self->provider_->sharedTransferBytes();
    if (!sharedBytes) {
        return kIOReturnNotReady;
    }

    // Copy optional inline or out-of-line input struct into shared kernel buffer
    if (args->structureInputDescriptor) {
        if (args->structureInputDescriptor->getLength() < len) {
            return kIOReturnBadArgument;
        }
        if (args->structureInputDescriptor->prepare(kIODirectionOut) != kIOReturnSuccess) {
            return kIOReturnVMError;
        }
        const IOByteCount copied = args->structureInputDescriptor->readBytes(0, sharedBytes, len);
        args->structureInputDescriptor->complete(kIODirectionOut);
        if (copied != len) {
            return kIOReturnVMError;
        }
    } else if (args->structureInput && args->structureInputSize > 0u) {
        if (args->structureInputSize < len) {
            return kIOReturnBadArgument;
        }
        memcpy(sharedBytes, args->structureInput, len);
    }

    const IOReturn rc = self->provider_->spiTransferInSharedBuffer(len);
    args->scalarOutput[0] = static_cast<uint64_t>(static_cast<uint32_t>(rc));
    if (rc != kIOReturnSuccess) {
        return rc;
    }

    // Copy RX bytes back if caller passed inline or out-of-line output struct
    if (args->structureOutputDescriptor) {
        if (args->structureOutputDescriptor->getLength() < len) {
            return kIOReturnBadArgument;
        }
        if (args->structureOutputDescriptor->prepare(kIODirectionIn) != kIOReturnSuccess) {
            return kIOReturnVMError;
        }
        const IOByteCount written = args->structureOutputDescriptor->writeBytes(0, sharedBytes, len);
        args->structureOutputDescriptor->complete(kIODirectionIn);
        if (written != len) {
            return kIOReturnVMError;
        }
        args->structureOutputDescriptorSize = len;
    } else if (args->structureOutput && args->structureOutputSize >= len) {
        memcpy(args->structureOutput, sharedBytes, len);
        args->structureOutputSize = len;
    }

    return kIOReturnSuccess;
}

IOReturn FocalTechSPIUserClient::sWaitIrq(OSObject *target, void * /*reference*/, IOExternalMethodArguments *args) {
    FocalTechSPIUserClient *self = OSDynamicCast(FocalTechSPIUserClient, target);
    if (!self || !self->provider_ || !args->scalarOutput) {
        return kIOReturnBadArgument;
    }
    uint32_t irqSeq = 0;
    uint32_t curGen = 0;
    uint32_t pinLevel = 0;
    uint32_t waitStatus = 0;
    const IOReturn rc = self->provider_->waitIrq(
        static_cast<uint32_t>(args->scalarInput[0]),
        static_cast<uint32_t>(args->scalarInput[1]),
        static_cast<uint32_t>(args->scalarInput[2]),
        &irqSeq,
        &curGen,
        &pinLevel,
        &waitStatus);
    if (rc == kIOReturnSuccess) {
        args->scalarOutput[0] = irqSeq;
        args->scalarOutput[1] = curGen;
        args->scalarOutput[2] = pinLevel;
        args->scalarOutput[3] = waitStatus;
    }
    return rc;
}
