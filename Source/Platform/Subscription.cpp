#include "Platform/Subscription.h"

#include <cmath>
#include <utility>

namespace vp::store
{

int daysLeft (double expiresAt, double nowSeconds, int cap) noexcept
{
    if (expiresAt <= nowSeconds)
        return 0;
    const int days = static_cast<int> (std::ceil ((expiresAt - nowSeconds) / 86400.0));
    return cap > 0 && days > cap ? cap : days;
}

#if ! defined (VP_STOREKIT)
// Host, tests, probes, native Mac: no store, never locked. The iOS build defines
// VP_STOREKIT and gets Subscription.mm instead (CMakeLists.txt).
void start (std::function<void (const Status&)> onChange)
{
    Status s;
    s.access = Access::subscribed;
    if (onChange)
        onChange (s);
}

void reloadProducts() {}

void purchase (const std::string&, std::function<void (PurchaseResult, const std::string&)> done)
{
    if (done)
        done (PurchaseResult::failed, "No store on this platform");
}

void restore (std::function<void (bool)> done)
{
    if (done)
        done (false);
}

void manage() {}

void redeemCode() {}
#endif

} // namespace vp::store
