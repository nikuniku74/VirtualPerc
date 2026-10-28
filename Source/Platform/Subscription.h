#pragma once

// The subscription, as the rest of the app sees it: plain data and four calls.
// On iOS the answers come from StoreKit 2 (StoreKitBridge.swift, reached through
// Subscription.mm); everywhere else Subscription.cpp answers "subscribed", so a
// host build, the tests and the probes are never behind a paywall.
//
// There is no server and no trial counter of our own. The 7 days are an
// *introductory offer* on the subscription in App Store Connect, so Apple tracks
// them per Apple ID: deleting the app, setting the clock back or restoring a
// backup cannot hand out a second trial. docs/APPSTORE.md.

#include <functional>
#include <string>
#include <vector>

namespace vp::store
{

enum class Access
{
    checking,      // nothing answered yet (launch, or StoreKit still reading)
    locked,        // no active access: the console is behind the paywall
    trial,         // inside the free introductory period
    subscribed     // paying
};

/** Product ids. Must match App Store Connect and the .storekit file. Monthly and
    yearly are auto-renewing subscriptions (with the 7-day introductory offer);
    lifetime is a non-consumable: one payment, no renewal, no trial. */
inline constexpr const char* kMonthlyId  = "com.virtualpercussionist.app.pro.monthly";
inline constexpr const char* kYearlyId   = "com.virtualpercussionist.app.pro.yearly";
inline constexpr const char* kLifetimeId = "com.virtualpercussionist.app.pro.lifetime";

struct Plan
{
    std::string id;             // App Store product id
    std::string price;          // "49,99 €", already localised by StoreKit
    bool yearly = false;
    bool lifetime = false;
    int savePercent = 0;        // yearly only, against twelve monthly payments
    bool trialEligible = false; // this Apple ID has not used the intro offer yet
    int trialDays = 0;
};

struct Status
{
    Access access = Access::checking;
    bool productsLoaded = false;
    bool productsFailed = false;      // asked and could not get them (offline...)
    std::vector<Plan> plans;          // whatever the store returned, in no fixed order
    std::string activePlanId;
    double expiresAt = 0.0;           // unix seconds; 0 = unknown
    enum class Renewal { unknown, renews, ends } renewal = Renewal::unknown;
    bool hadSubscription = false;     // a lapsed one: the paywall says "ended"

    bool entitled() const noexcept
    {
        return access == Access::trial || access == Access::subscribed;
    }
    bool ownsLifetime() const noexcept { return access == Access::subscribed && activePlanId == kLifetimeId; }
    bool entitledAt (double nowSeconds) const noexcept
    {
        return entitled() && (ownsLifetime() || expiresAt > nowSeconds);
    }
};

enum class PurchaseResult { success, pending, cancelled, failed };

/** Whole days from `nowSeconds` to `expiresAt`, rounded up; 0 when unknown or
    past. `cap`, when above 0, is the most the period can have (the trial's own
    length): a device clock a day behind the store's - seen in Xcode's StoreKit
    testing, where the Mac is the store - would otherwise show 8 days left on a
    7-day trial. The one bit of arithmetic the screens share. */
int daysLeft (double expiresAt, double nowSeconds, int cap = 0) noexcept;

/** Starts listening. `onChange` is called on the message thread, once at
    start-up and again whenever the answer changes (a purchase, a renewal that
    lapsed, a refund, a family-sharing change). Call it once. */
void start (std::function<void (const Status&)> onChange);

/** Re-reads the products, e.g. after a failure. */
void reloadProducts();

void purchase (const std::string& planId,
               std::function<void (PurchaseResult, const std::string& message)> done);

/** "Restore Purchases": asks the App Store to sync, then re-reads. */
void restore (std::function<void (bool ok)> done);

/** Apple's own Manage Subscriptions sheet (cancel, change plan). */
void manage();

/** Apple's offer-code sheet: where a gift or promotional code is typed in. The
    purchase then arrives like any other, through the transaction updates. */
void redeemCode();

} // namespace vp::store
