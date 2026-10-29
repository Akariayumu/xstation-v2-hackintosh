#ifndef XSHIDD_LOG_HPP
#define XSHIDD_LOG_HPP

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>

class XSHIDDLog : public IOService {
    OSDeclareDefaultStructors(XSHIDDLog)

public:
    bool init(OSDictionary *dictionary = nullptr) override;
    void free() override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn message(UInt32 type, IOService *provider,
                     void *argument = nullptr) override;
    static IOReturn interestHandler(void *target, void *refCon,
                                    UInt32 messageType, IOService *provider,
                                    void *messageArgument, vm_size_t argSize);

private:
    IOReturn recordNotification(UInt32 code, vm_size_t argSize, bool viaInterest);
    IOLock *stateLock {nullptr};
    IONotifier *interestNotifier {nullptr};
    bool active {false};
    UInt64 notifyCount {0};
    UInt64 interestCount {0};
    UInt64 messageCount {0};
};

#endif
