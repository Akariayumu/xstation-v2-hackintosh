#include "XSHIDDLog.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/acpi/IOACPITypes.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <stdint.h>

#define super IOService
OSDefineMetaClassAndStructors(XSHIDDLog, IOService)

namespace {
bool disabledByBootArgument()
{
    int value = 0;
    // Accept both spellings so the safety switch does not depend on the
    // boot-argument parser's treatment of the leading dash.
    return PE_parse_boot_argn("xshiddoff", &value, sizeof(value)) ||
           PE_parse_boot_argn("-xshiddoff", &value, sizeof(value));
}

UInt32 notifyCodeFromArgument(void *argument, vm_size_t argSize)
{
    const uintptr_t raw = reinterpret_cast<uintptr_t>(argument);
    // IOACPITypes.h documents a pointer to a UInt32.  A few IOKit message
    // paths pass small ordinals by value, so tolerate those without ever
    // dereferencing a low address.
    if (argSize == 0 || raw < 0x10000)
        return static_cast<UInt32>(raw);
    return *static_cast<const UInt32 *>(argument);
}
}

bool XSHIDDLog::init(OSDictionary *dictionary)
{
    if (!super::init(dictionary))
        return false;
    stateLock = IOLockAlloc();
    return stateLock != nullptr;
}

void XSHIDDLog::free()
{
    // Retain the lock until object destruction, including after stop().
    if (stateLock) {
        IOLockFree(stateLock);
        stateLock = nullptr;
    }
    super::free();
}

IOService *XSHIDDLog::probe(IOService *provider, SInt32 *score)
{
    if (disabledByBootArgument())
        return nullptr;
    return super::probe(provider, score);
}

bool XSHIDDLog::start(IOService *provider)
{
    if (disabledByBootArgument() || !super::start(provider))
        return false;

    IOLockLock(stateLock);
    notifyCount = 0;
    interestCount = 0;
    messageCount = 0;
    const bool ready =
        setProperty("S1NotifyCount", static_cast<UInt64>(0), 64) &&
        setProperty("S1InterestCount", static_cast<UInt64>(0), 64) &&
        setProperty("S1MessageCount", static_cast<UInt64>(0), 64) &&
        setProperty("S1LastNotify", static_cast<UInt64>(0), 32) &&
        setProperty("S1LastNotifyTime", static_cast<UInt64>(0), 64) &&
        setProperty("S1LastArgSize", static_cast<UInt64>(0), 64) &&
        setProperty("S1ProviderName", provider->getName());
    active = ready;
    IOLockUnlock(stateLock);

    if (!ready) {
        super::stop(provider);
        return false;
    }

    // ACPI notifications are delivered through gIOGeneralInterest, not by
    // messageClients() to ordinary children (see IOService::messageClients).
    IONotifier *notifier = provider->registerInterest(
        gIOGeneralInterest, &XSHIDDLog::interestHandler, this, nullptr);
    if (!notifier) {
        IOLockLock(stateLock);
        active = false;
        IOLockUnlock(stateLock);
        super::stop(provider);
        IOLog("XSHIDDLog: failed to register ACPI interest\n");
        return false;
    }

    IOLockLock(stateLock);
    interestNotifier = notifier;
    IOLockUnlock(stateLock);
    IOLog("XSHIDDLog: S1 started on %s (log only, interest registered)\n",
          provider->getName());
    return true;
}

void XSHIDDLog::stop(IOService *provider)
{
    IONotifier *notifier = nullptr;

    IOLockLock(stateLock);
    active = false;
    notifier = interestNotifier;
    interestNotifier = nullptr;
    IOLockUnlock(stateLock);

    if (notifier)
        notifier->remove();
    super::stop(provider);
}

IOReturn XSHIDDLog::interestHandler(void *target, void *refCon,
                                    UInt32 messageType, IOService *provider,
                                    void *messageArgument, vm_size_t argSize)
{
    (void)refCon;
    XSHIDDLog *self = static_cast<XSHIDDLog *>(target);
    if (!self || messageType != kIOACPIMessageDeviceNotification)
        return kIOReturnSuccess;
    if (provider != self->getProvider())
        return kIOReturnSuccess;
    if (!messageArgument)
        return kIOReturnBadArgument;

    return self->recordNotification(
        notifyCodeFromArgument(messageArgument, argSize), argSize, true);
}

IOReturn XSHIDDLog::message(UInt32 type, IOService *provider, void *argument)
{
    if (type != kIOACPIMessageDeviceNotification || provider != getProvider())
        return super::message(type, provider, argument);
    if (!argument)
        return kIOReturnBadArgument;

    // IOACPITypes.h documents a pointer to a UInt32.  Older or direct-message
    // paths may still pass a small ordinal by value; the helper tolerates both.
    return recordNotification(
        notifyCodeFromArgument(argument, sizeof(UInt32)), 0, false);
}

IOReturn XSHIDDLog::recordNotification(UInt32 code, vm_size_t argSize,
                                       bool viaInterest)
{
    IOLockLock(stateLock);
    if (!active) {
        IOLockUnlock(stateLock);
        return kIOReturnNotReady;
    }

    uint64_t uptime = 0;
    uint64_t nanoseconds = 0;
    clock_get_uptime(&uptime);
    absolutetime_to_nanoseconds(uptime, &nanoseconds);
    ++notifyCount;
    if (viaInterest)
        ++interestCount;
    else
        ++messageCount;

    // Publish count last. These properties are not one atomic tuple.
    const bool recordedCode = setProperty("S1LastNotify", code, 32);
    const bool recordedTime = setProperty("S1LastNotifyTime", nanoseconds, 64);
    const bool recordedArgSize =
        setProperty("S1LastArgSize", static_cast<UInt64>(argSize), 64);
    const bool recordedInterest =
        setProperty("S1InterestCount", interestCount, 64);
    const bool recordedMessage =
        setProperty("S1MessageCount", messageCount, 64);
    const bool recordedCount = setProperty("S1NotifyCount", notifyCount, 64);

    // Bound text log volume; IORegistry continues to update on every notify.
    if (notifyCount <= 16 || (notifyCount % 256) == 0)
        IOLog("XSHIDDLog: notify=0x%08x source=%s arg_size=%llu "
              "count=%llu uptime_ns=%llu\n",
              static_cast<unsigned int>(code),
              viaInterest ? "interest" : "message",
              static_cast<unsigned long long>(argSize),
              static_cast<unsigned long long>(notifyCount),
              static_cast<unsigned long long>(nanoseconds));

    IOLockUnlock(stateLock);
    return recordedCode && recordedTime && recordedArgSize &&
                   recordedInterest && recordedMessage && recordedCount
        ? kIOReturnSuccess : kIOReturnNoMemory;
}
