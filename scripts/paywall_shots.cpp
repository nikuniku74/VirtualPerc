// Not a measurement: the subscription pages, rendered to PNG for review, plus the
// few facts about them that can be checked without a store (what the button
// says, which page shows, the day count). Host only.
//
//   cmake --build build-host --target VPPaywall
//   ./build-host/VPPaywall_artefacts/Release/VPPaywall /tmp/paywall-shots
//
// The real thing - a purchase, a trial, a restore - needs StoreKit and a device
// or the .storekit file in Xcode; see docs/APPSTORE.md.

#include "UI/PaywallOverlay.h"
#include "UI/MainComponentShared.h"   // vpui::gDarkMode

#include <cstdio>

namespace
{
    int gFailed = 0;

    void expect (bool ok, const char* what)
    {
        std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what);
        if (! ok)
            ++gFailed;
    }

    vp::store::Plan plan (const char* id, const char* price, bool yearly, bool trial)
    {
        vp::store::Plan p;
        p.id = id;
        p.price = price;
        p.yearly = yearly;
        p.trialEligible = trial;
        p.trialDays = trial ? 7 : 0;
        if (yearly)
        {
            p.pricePerMonth = "2,92 \xe2\x82\xac";
            p.savePercent = 42;
        }
        return p;
    }

    vp::store::Status offer (bool trial, bool hadBefore = false)
    {
        vp::store::Status s;
        s.access = vp::store::Access::locked;
        s.productsLoaded = true;
        s.plans = { plan (vp::store::kMonthlyId, "4,99 \xe2\x82\xac", false, trial),
                    plan (vp::store::kYearlyId, "34,99 \xe2\x82\xac", true, trial) };
        s.hadSubscription = hadBefore;
        return s;
    }

    constexpr double kNow = 1792000000.0;   // fixed, so the day count never moves

    vp::store::Status inTrial()
    {
        auto s = offer (false);
        s.access = vp::store::Access::trial;
        s.activePlanId = vp::store::kYearlyId;
        s.expiresAt = kNow + 4.5 * 86400.0;
        s.renewal = vp::store::Status::Renewal::renews;
        return s;
    }

    vp::store::Status subscribed (bool renews)
    {
        auto s = offer (false);
        s.access = vp::store::Access::subscribed;
        s.activePlanId = vp::store::kMonthlyId;
        s.expiresAt = kNow + 20.0 * 86400.0;
        s.renewal = renews ? vp::store::Status::Renewal::renews : vp::store::Status::Renewal::ends;
        return s;
    }

    void shoot (PaywallOverlay& p, const vp::store::Status& st, const juce::File& dir, const juce::String& name,
                int w, int h, juce::BorderSize<int> safe, bool dark)
    {
        vpui::gDarkMode = dark;
        p.setSafeArea (safe);
        p.setSize (w, h);
        p.setStatus (st);
        const auto img = p.createComponentSnapshot (p.getLocalBounds(), true, 2.0f);
        const auto file = dir.getChildFile (name + ".png");
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
        {
            out->setPosition (0);
            out->truncate();
            png.writeImageToStream (img, *out);
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    PaywallOverlay p;
    p.setClock ([] { return kNow; });
    p.setItalian (false);

    std::printf ("paywall logic\n");
    p.setStatus (offer (true));
    expect (p.ctaText() == "Start 7-day free trial", "eligible: the button says 7-day free trial");
    expect (! p.showsStatusPage(), "locked shows the offer");
    p.setStatus (offer (false));
    expect (p.ctaText() == "Subscribe", "not eligible: plain Subscribe");
    p.setStatus (inTrial());
    expect (p.showsStatusPage(), "in trial shows the status page");
    expect (p.ctaText() == "Manage subscription", "status page button manages");
    p.setItalian (true);
    p.setStatus (offer (true));
    expect (p.ctaText() == "Prova gratis per 7 giorni", "italian button");
    p.setItalian (false);
    expect (vp::store::daysLeft (kNow + 4.5 * 86400.0, kNow) == 5, "4.5 days left rounds up to 5");
    expect (vp::store::daysLeft (kNow + 8.3 * 86400.0, kNow, 7) == 7, "a clock a day behind cannot show more than the trial");
    expect (vp::store::daysLeft (kNow + 3.2 * 86400.0, kNow, 7) == 4, "the cap does not touch a smaller count");
    expect (vp::store::daysLeft (kNow - 1.0, kNow) == 0, "past is 0");
    expect (vp::store::daysLeft (0.0, kNow) == 0, "unknown is 0");

    bool purchased = false;
    std::string asked;
    p.onPurchase = [&] (const std::string& id) { purchased = true; asked = id; };
    p.setStatus (offer (true));
    p.setSize (393, 852);
    // The yearly plan is the one on offer first and is selected by default.
    p.setBusy (true);
    expect (p.ctaText() == "Please wait...", "busy: the button waits");
    p.setBusy (false);
    juce::ignoreUnused (purchased, asked);

    if (argc > 1)
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
        dir.createDirectory();
        const juce::BorderSize<int> phoneSafe { 59, 0, 34, 0 };
        const juce::BorderSize<int> phoneLandSafe { 0, 59, 21, 59 };
        const juce::BorderSize<int> padSafe { 24, 0, 20, 0 };

        for (const bool it : { false, true })
        {
            p.setItalian (it);
            const juce::String L = it ? "it-" : "en-";
            shoot (p, offer (true), dir, L + "1-offer-trial-phone", 393, 852, phoneSafe, true);
            shoot (p, offer (true), dir, L + "1-offer-trial-phone-light", 393, 852, phoneSafe, false);
            shoot (p, offer (true), dir, L + "2-offer-trial-ipad", 820, 1180, padSafe, true);
            shoot (p, offer (true), dir, L + "3-offer-trial-phone-landscape", 852, 393, phoneLandSafe, true);
            shoot (p, offer (false, true), dir, L + "4-offer-ended-phone", 393, 852, phoneSafe, true);
            shoot (p, inTrial(), dir, L + "5-status-trial-phone", 393, 852, phoneSafe, true);
            shoot (p, subscribed (true), dir, L + "6-status-subscribed-phone", 393, 852, phoneSafe, true);
            shoot (p, subscribed (false), dir, L + "7-status-cancelled-phone", 393, 852, phoneSafe, true);

            vp::store::Status loading;
            shoot (p, loading, dir, L + "8-loading-phone", 393, 852, phoneSafe, true);
            vp::store::Status failed;
            failed.access = vp::store::Access::locked;
            failed.productsFailed = true;
            shoot (p, failed, dir, L + "9-offline-phone", 393, 852, phoneSafe, true);

            p.setStatus (offer (true));
            p.setNotice (PaywallOverlay::Notice::pending);
            shoot (p, offer (true), dir, L + "10-pending-phone", 393, 852, phoneSafe, true);
            p.setNotice (PaywallOverlay::Notice::none);
            p.setStatus (offer (true));
            p.setSize (393, 852);
            p.showLicenses();
            shoot (p, offer (true), dir, L + "11-licenses-phone", 393, 852, phoneSafe, true);
        }
        std::printf ("screens written to %s\n", dir.getFullPathName().toRawUTF8());
    }

    std::printf (gFailed == 0 ? "paywall: all passed\n" : "paywall: %d FAILED\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
