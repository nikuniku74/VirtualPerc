#pragma once

#include "Platform/Subscription.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

/** The subscription page: the offer (7 days free, then monthly or yearly), the
    "you are in" page for someone who already is, and the states in between
    (reading the store, store unreachable, a purchase in flight).

    It owns a surface and knows nothing about the engine: everything it shows
    comes from a vp::store::Status and everything it does goes out through the
    callbacks. MainComponent decides when it is on screen and what a purchase
    unlocks. Text is English or Italian, by the device language.

    What App Review looks at is all here and deliberately not hidden: the price
    and period next to the button, the trial length, the auto-renew and
    cancellation text, Restore, and links to the Terms and the Privacy Policy. */
class PaywallOverlay final : public juce::Component,
                             private juce::Timer
{
public:
    PaywallOverlay();
    ~PaywallOverlay() override;

    void setStatus (const vp::store::Status&);
    /** A personal build (VP_UNLOCKED): the page only says everything is
        unlocked and keeps the terms, privacy and licences links - no plans,
        no restore, no codes, nothing that would ask the App Store. */
    void setPersonalBuild (bool);
    /** A purchase or a restore is running: the buttons stand still. */
    void setBusy (bool busy);
    /** One line under the button, in the page's language. `detail` is the
        store's own reason for a failed purchase, when it gave one. */
    enum class Notice { none, pending, failed, offline, nothingToRestore };
    void setNotice (Notice, const juce::String& detail = {});
    void setSafeArea (juce::BorderSize<int>);
    /** Defaults to the device language; the preview tool and the tests pin it. */
    void setItalian (bool);
    /** "Now" for the day count, so a test or a screenshot is not a moving target. */
    void setClock (std::function<double()> unixSeconds);

    std::function<void()> onClose;
    std::function<void (const std::string& planId)> onPurchase;
    std::function<void()> onRestore, onManage, onRetry;
    /** Opens the offer-code sheet. The "Redeem code" link is shown only when set. */
    std::function<void()> onRedeem;

    /** What the main button says and does, for tests. */
    juce::String ctaText() const;
    bool showsStatusPage() const noexcept;
    /** The page is about a lifetime purchase (the plan in view, or the one owned). */
    bool lifetimeInView() const noexcept;
    /** Third-party notices (CC BY attribution, MIT text) over the page. */
    void showLicenses();
    /** Selects a plan card by display position (0 yearly, 1 monthly, 2 lifetime). */
    void selectPlan (int displayIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /** Replace before shipping: both are shown on the page and reviewed. The
        Terms are Apple's standard EULA, which is what an app with no EULA of
        its own uses (set it in App Store Connect too). */
    static const char* privacyUrl();
    static const char* termsUrl();

private:
    class PlanButton;
    class PillButton;
    class LinkButton;
    class LicensesPage;

    int selectedPlan = 0;
    vp::store::Status status;
    bool personal = false;
    bool busy = false;
    bool italian = false;
    juce::String notice;
    Notice noticeKind = Notice::none;
    juce::String noticeDetail;
    juce::BorderSize<int> safe { 0, 0, 0, 0 };
    std::function<double()> clock;

    std::unique_ptr<PillButton> closeButton, ctaButton;
    std::unique_ptr<PlanButton> planButtons[3];
    std::unique_ptr<LinkButton> restoreLink, redeemLink, termsLink, privacyLink, licensesLink, retryLink, manageLink;
    std::unique_ptr<LicensesPage> licensesPage;

    struct Geometry
    {
        juce::Rectangle<int> brand, logo, dots, headline, sub, statusCard, price, legal, noticeRow, links;
        juce::Rectangle<int> bullets[3], planRect[3], cta;
        int bulletCount = 0;
        float scale = 1.0f;
    };
    Geometry geo;

    bool statusPage() const noexcept;
    bool offerReady() const noexcept;
    /** Display order: yearly (the one on offer), monthly, lifetime. */
    const vp::store::Plan* plan (int displayIndex) const noexcept;
    int planCount() const noexcept;
    const vp::store::Plan* selected() const noexcept;
    juce::String t (const char* en, const char* it) const;
    juce::String headlineText() const;
    juce::String subText() const;
    juce::String priceNote() const;
    juce::String legalText() const;
    juce::String dateText (double unixSeconds) const;
    juce::String planName (const juce::String& id) const;
    juce::Font headlineFont() const;
    /** The four quarters turning over under the headline, as on the stage. */
    void timerCallback() override;
    void paintDots (juce::Graphics&) const;
    juce::Image logo;
    void refreshControls();
    void updateNoticeText();
    void layoutAll();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaywallOverlay)
};
