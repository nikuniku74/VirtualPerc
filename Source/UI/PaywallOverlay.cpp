#include "UI/PaywallOverlay.h"
#include "UI/MainComponentShared.h"

#include <algorithm>
#include <cmath>

namespace
{
    // ---- Replace before the first submission (docs/APPSTORE.md) -------------
    const char* const kPrivacyUrl = "https://virtualpercussionist.it/privacy/";   // until the DNS points at GitHub Pages, https://nikuniku74.github.io/virtualpercussionist-site/privacy/ answers
    const char* const kTermsUrl   = "https://www.apple.com/legal/internet-services/itunes/dev/stdeula/";

    constexpr int kMaxColumn = 520;

    int textHeight (const juce::String& s, const juce::Font& f, int width)
    {
        if (s.isEmpty() || width < 8)
            return 0;
        juce::AttributedString a;
        a.setJustification (juce::Justification::topLeft);
        a.append (s, f, juce::Colours::white);
        juce::TextLayout l;
        l.createLayout (a, static_cast<float> (width));
        return static_cast<int> (std::ceil (l.getHeight())) + 2;
    }

    void drawWrapped (juce::Graphics& g, const juce::String& s, const juce::Font& f, juce::Colour c,
                      juce::Rectangle<int> r, juce::Justification j = juce::Justification::topLeft)
    {
        if (s.isEmpty() || r.isEmpty())
            return;
        juce::AttributedString a;
        a.setJustification (j);
        a.append (s, f, c);
        a.draw (g, r.toFloat());
    }
}

// ---------------------------------------------------------------------------
// The three kinds of control. Drawn here rather than through the app's look and
// feel so the page is the same wherever it is shown (the preview tool has no
// MainComponent), and each is a real juce::Button so VoiceOver can reach it.

class PaywallOverlay::PillButton final : public juce::Button
{
public:
    PillButton() : juce::Button ({}) {}
    juce::String text;
    bool filled = true;
    bool cross = false;   // the round close button: a cross, no word
    float scale = 1.0f;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (cross)
        {
            const auto d = getLocalBounds().toFloat().reduced (1.0f);
            const float a = isEnabled() ? 1.0f : 0.45f;
            g.setColour (ink().withAlpha (a).withMultipliedBrightness (down ? 0.85f : (over ? 1.08f : 1.0f)));
            g.fillEllipse (d);
            g.setColour (border());
            g.drawEllipse (d, 1.0f);
            const auto c = d.getCentre();
            const float r = d.getWidth() * 0.22f;
            juce::Path x;
            x.startNewSubPath (c.x - r, c.y - r);
            x.lineTo (c.x + r, c.y + r);
            x.startNewSubPath (c.x + r, c.y - r);
            x.lineTo (c.x - r, c.y + r);
            g.setColour (::text().withAlpha (a));
            g.strokePath (x, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        const float press = down ? 0.85f : (over ? 0.93f : 1.0f);
        if (filled)
        {
            g.setColour (fuchsia().withMultipliedBrightness (press).withAlpha (alpha));
            g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 16.0f));
            g.setColour (juce::Colours::white.withAlpha (alpha));
        }
        else
        {
            g.setColour (ink().withAlpha (alpha));
            g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 16.0f));
            g.setColour (border());
            g.drawRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 16.0f), 1.0f);
            g.setColour (::text().withAlpha (alpha));
        }
        g.setFont (fontUi (juce::jmin (r.getHeight() * 0.40f, 20.0f * scale + 2.0f)));
        g.drawFittedText (text, getLocalBounds().reduced (10, 2), juce::Justification::centred, 1);
    }
};

class PaywallOverlay::LinkButton final : public juce::Button
{
public:
    LinkButton() : juce::Button ({}) {}
    juce::String text;
    float scale = 1.0f;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        g.setColour ((down || over ? ::text() : mute()));
        g.setFont (fontUi (12.5f * scale, false));
        g.drawText (text, getLocalBounds(), juce::Justification::centred, true);
        const auto w = juce::GlyphArrangement::getStringWidth (fontUi (12.5f * scale, false), text);
        const float y = getHeight() * 0.5f + 8.0f * scale;
        g.fillRect (getWidth() * 0.5f - w * 0.5f, y, w, 1.0f);
    }
};

class PaywallOverlay::PlanButton final : public juce::Button
{
public:
    PlanButton() : juce::Button ({}) {}
    juce::String title, detail, rightTop, rightBottom, badge;
    bool selected = false;
    float scale = 1.0f;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        if (! badge.isEmpty())
            r.removeFromTop (6.0f * scale);
        const float corner = 14.0f;
        const float alpha = isEnabled() ? 1.0f : 0.5f;

        g.setColour ((selected ? fuchsia().withAlpha (gDarkMode ? 0.12f : 0.08f) : panel())
                         .withMultipliedBrightness (down ? 0.92f : (over ? 1.05f : 1.0f)));
        g.fillRoundedRectangle (r, corner);
        g.setColour (selected ? fuchsia() : border());
        g.drawRoundedRectangle (r, corner, selected ? 2.0f : 1.0f);

        auto inner = r.reduced (14.0f * scale, 8.0f * scale);
        const float d = 20.0f * scale;
        auto radio = inner.removeFromLeft (d).withSizeKeepingCentre (d, d);
        g.setColour (selected ? fuchsia() : mute().withAlpha (0.8f));
        g.drawEllipse (radio, 1.8f);
        if (selected)
            g.fillEllipse (radio.reduced (5.0f * scale));
        inner.removeFromLeft (12.0f * scale);

        auto right = inner.removeFromRight (inner.getWidth() * 0.36f);
        auto itemH = inner.getHeight() * 0.5f;
        g.setColour (::text().withAlpha (alpha));
        g.setFont (fontUi (17.0f * scale));
        g.drawText (title, inner.removeFromTop (itemH).toNearestInt(), juce::Justification::bottomLeft, true);
        g.setColour (mute().withAlpha (alpha));
        g.setFont (fontUi (12.0f * scale, false));
        g.drawFittedText (detail, inner.toNearestInt().withTrimmedTop (2), juce::Justification::topLeft, 2);

        g.setColour (::text().withAlpha (alpha));
        g.setFont (fontUi (17.0f * scale));
        g.drawText (rightTop, right.removeFromTop (itemH).toNearestInt(), juce::Justification::bottomRight, true);
        g.setColour (mute().withAlpha (alpha));
        g.setFont (fontUi (12.0f * scale, false));
        g.drawText (rightBottom, right.toNearestInt().withTrimmedTop (2), juce::Justification::topRight, true);

        if (! badge.isEmpty())
        {
            const auto f = fontUi (10.5f * scale);
            const float w = juce::GlyphArrangement::getStringWidth (f, badge) + 16.0f * scale;
            juce::Rectangle<float> pill (getWidth() - w - 16.0f * scale, 0.0f, w, 16.0f * scale + 2.0f);
            g.setColour (fuchsia());
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (juce::Colours::white);
            g.setFont (f);
            g.drawText (badge, pill.toNearestInt(), juce::Justification::centred, false);
        }
    }
};


// ---------------------------------------------------------------------------
// Third-party notices. Attribution is a condition of CC BY 4.0 (the beat model)
// and of the MIT licence (ONNX Runtime); the rest is courtesy. Keep in step with
// docs/LICENSES.md and Assets/*/ATTRIBUTION.md / SOURCES.md.

class PaywallOverlay::LicensesPage final : public juce::Component
{
public:
    LicensesPage()
    {
        setOpaque (true);
        close = std::make_unique<PillButton>();
        close->cross = true;
        close->setTitle ("Close");
        close->onClick = [this] { setVisible (false); };
        addAndMakeVisible (*close);

        body.setMultiLine (true, true);
        body.setReadOnly (true);
        body.setCaretVisible (false);
        body.setScrollbarsShown (true);
        body.setPopupMenuEnabled (false);
        addAndMakeVisible (body);
        body.setText (notices(), false);
    }

    void setSafeArea (juce::BorderSize<int> b) { safe = b; resized(); }

    void applyColours()
    {
        body.setColour (juce::TextEditor::backgroundColourId, bg());
        body.setColour (juce::TextEditor::textColourId, ::text());
        body.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        body.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        body.applyFontToAllText (fontUi (14.0f, false));
        body.setFont (fontUi (14.0f, false));
    }

    void visibilityChanged() override { if (isVisible()) applyColours(); }
    void paint (juce::Graphics& g) override { g.fillAll (bg()); }

    void resized() override
    {
        auto r = safe.subtractedFrom (getLocalBounds()).reduced (20, 10);
        auto head = r.removeFromTop (38);
        close->setBounds (head.removeFromRight (30).withSizeKeepingCentre (30, 30));
        head.removeFromLeft (4);
        titleArea = head;
        r.removeFromTop (6);
        body.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 640), r.getHeight()));
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        g.setColour (::text());
        g.setFont (fontUi (17.0f));
        g.drawText ("Licenses and credits", titleArea, juce::Justification::centredLeft, true);
    }

private:
    static juce::String notices()
    {
        return juce::String (juce::CharPointer_UTF8 (
            "Virtual Percussionist uses the following third-party work.\n\n"
            "JUCE\n(c) Raw Material Software Limited.\n\n"
            "ONNX Runtime\n(c) Microsoft Corporation. MIT License.\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and "
            "associated documentation files (the \"Software\"), to deal in the Software without restriction, "
            "including without limitation the rights to use, copy, modify, merge, publish, distribute, "
            "sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is "
            "furnished to do so, subject to the following conditions: The above copyright notice and this "
            "permission notice shall be included in all copies or substantial portions of the Software. "
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING "
            "BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
            "NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, "
            "DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n"
            "BeatNet\nBeat and downbeat model by Mojtaba Heydari, Frank Cwitkowitz and Zhiyao Duan "
            "(\"BeatNet: CRNN and Particle Filtering for Online Joint Beat Downbeat and Meter Tracking\", ISMIR 2021). "
            "Model weights licensed under Creative Commons Attribution 4.0 (CC BY 4.0, "
            "https://creativecommons.org/licenses/by/4.0/). The weights were fine-tuned for this app; "
            "this is a modified version.\n\n"
            "Percussion samples\nVersilian Community Sample Library (VCSL) and VS Chamber Orchestra 2: Community "
            "Edition by Versilian Studios LLC, CC0 1.0. Loops by nicpressley, Sycopation and shpira, "
            "CC0 1.0.\n"));
    }

    std::unique_ptr<PillButton> close;
    juce::TextEditor body;
    juce::BorderSize<int> safe { 0, 0, 0, 0 };
    juce::Rectangle<int> titleArea;
};

// ---------------------------------------------------------------------------

PaywallOverlay::PaywallOverlay()
{
    setOpaque (true);
    italian = vpui::italian();
    clock = [] { return juce::Time::currentTimeMillis() / 1000.0; };

    closeButton = std::make_unique<PillButton>();
    ctaButton = std::make_unique<PillButton>();
    closeButton->filled = false;
    closeButton->cross = true;
    for (auto& b : planButtons)
        b = std::make_unique<PlanButton>();
    restoreLink = std::make_unique<LinkButton>();
    redeemLink = std::make_unique<LinkButton>();
    termsLink = std::make_unique<LinkButton>();
    privacyLink = std::make_unique<LinkButton>();
    licensesLink = std::make_unique<LinkButton>();
    licensesPage = std::make_unique<LicensesPage>();
    retryLink = std::make_unique<LinkButton>();
    manageLink = std::make_unique<LinkButton>();

    for (juce::Component* c : { static_cast<juce::Component*> (closeButton.get()),
                                static_cast<juce::Component*> (ctaButton.get()),
                                static_cast<juce::Component*> (planButtons[0].get()),
                                static_cast<juce::Component*> (planButtons[1].get()),
                                static_cast<juce::Component*> (planButtons[2].get()),
                                static_cast<juce::Component*> (restoreLink.get()),
                                static_cast<juce::Component*> (redeemLink.get()),
                                static_cast<juce::Component*> (termsLink.get()),
                                static_cast<juce::Component*> (privacyLink.get()),
                                static_cast<juce::Component*> (licensesLink.get()),
                                static_cast<juce::Component*> (retryLink.get()),
                                static_cast<juce::Component*> (manageLink.get()) })
        addChildComponent (c);
    addChildComponent (*licensesPage);   // above everything else

    closeButton->onClick = [this] { if (onClose) onClose(); };
    ctaButton->onClick = [this]
    {
        if (busy)
            return;
        if (statusPage())
        {
            if (onManage)
                onManage();
        }
        else if (const auto* p = selected(); p != nullptr && onPurchase)
            onPurchase (p->id);
    };
    for (int i = 0; i < 3; ++i)
        planButtons[i]->onClick = [this, i]
        {
            if (! busy)
            {
                selectedPlan = i;
                refreshControls();
                repaint();
            }
        };
    restoreLink->onClick = [this] { if (! busy && onRestore) onRestore(); };
    redeemLink->onClick = [this] { if (! busy && onRedeem) onRedeem(); };
    retryLink->onClick = [this] { if (onRetry) onRetry(); };
    manageLink->onClick = [this] { if (onManage) onManage(); };
    termsLink->onClick = [] { juce::URL (kTermsUrl).launchInDefaultBrowser(); };
    privacyLink->onClick = [] { juce::URL (kPrivacyUrl).launchInDefaultBrowser(); };
    licensesLink->onClick = [this] { showLicenses(); };

    refreshControls();
}

PaywallOverlay::~PaywallOverlay() = default;

const char* PaywallOverlay::privacyUrl() { return kPrivacyUrl; }
const char* PaywallOverlay::termsUrl()   { return kTermsUrl; }

void PaywallOverlay::setStatus (const vp::store::Status& s)
{
    status = s;
    selectedPlan = juce::jlimit (0, juce::jmax (0, planCount() - 1), selectedPlan);
    refreshControls();
    layoutAll();
    repaint();
}

void PaywallOverlay::setBusy (bool b)
{
    busy = b;
    refreshControls();
    repaint();
}

void PaywallOverlay::setNotice (Notice kind, const juce::String& detail)
{
    noticeKind = kind;
    noticeDetail = detail;
    updateNoticeText();
    layoutAll();
    repaint();
}

void PaywallOverlay::updateNoticeText()
{
    switch (noticeKind)
    {
        case Notice::none:    notice = {}; break;
        case Notice::pending: notice = t ("Waiting for approval (Ask to Buy).", "In attesa di approvazione (Chiedi di acquistare)."); break;
        case Notice::offline: notice = t ("Could not reach the App Store. Try again.", "App Store non raggiungibile. Riprova."); break;
        case Notice::nothingToRestore:
            notice = t ("No active subscription found for this Apple ID.", "Nessun abbonamento attivo per questo ID Apple.");
            break;
        case Notice::failed:
            notice = noticeDetail.isNotEmpty() ? noticeDetail
                                               : t ("The purchase did not go through.", "L'acquisto non è andato a buon fine.");
            break;
    }
}

void PaywallOverlay::setSafeArea (juce::BorderSize<int> b)
{
    if (safe == b)
        return;
    safe = b;
    licensesPage->setSafeArea (b);
    layoutAll();
}

void PaywallOverlay::setItalian (bool it)
{
    italian = it;
    updateNoticeText();
    refreshControls();
    layoutAll();
    repaint();
}

void PaywallOverlay::setClock (std::function<double()> c)
{
    clock = std::move (c);
}

juce::String PaywallOverlay::t (const char* en, const char* it) const
{
    return juce::String (juce::CharPointer_UTF8 (italian ? it : en));
}

void PaywallOverlay::showLicenses()
{
    licensesPage->setBounds (getLocalBounds());
    licensesPage->setVisible (true);
    licensesPage->toFront (false);
}

bool PaywallOverlay::statusPage() const noexcept { return status.entitled(); }
bool PaywallOverlay::showsStatusPage() const noexcept { return statusPage(); }

int PaywallOverlay::planCount() const noexcept
{
    return juce::jmin (3, static_cast<int> (status.plans.size()));
}

const vp::store::Plan* PaywallOverlay::plan (int i) const noexcept
{
    if (i < 0 || i >= planCount())
        return nullptr;
    // Yearly first (it is the one on offer), then monthly, then lifetime.
    std::vector<const vp::store::Plan*> order;
    for (const auto& p : status.plans)
        if (p.yearly)
            order.push_back (&p);
    for (const auto& p : status.plans)
        if (! p.yearly && ! p.lifetime)
            order.push_back (&p);
    for (const auto& p : status.plans)
        if (p.lifetime)
            order.push_back (&p);
    return order[static_cast<size_t> (i)];
}

const vp::store::Plan* PaywallOverlay::selected() const noexcept { return plan (selectedPlan); }

void PaywallOverlay::selectPlan (int displayIndex)
{
    selectedPlan = juce::jlimit (0, juce::jmax (0, planCount() - 1), displayIndex);
    refreshControls();
    layoutAll();
    repaint();
}

bool PaywallOverlay::lifetimeInView() const noexcept
{
    if (statusPage())
        return status.activePlanId == vp::store::kLifetimeId;
    const auto* p = selected();
    return p != nullptr && p->lifetime;
}

bool PaywallOverlay::offerReady() const noexcept
{
    return ! statusPage() && status.productsLoaded && planCount() > 0;
}

// ---- words ----------------------------------------------------------------

namespace
{
    juce::String dayWord (bool italian, int n)
    {
        return juce::String (n) + (italian ? (n == 1 ? " giorno" : " giorni")
                                           : (n == 1 ? " day" : " days"));
    }
}

juce::String PaywallOverlay::dateText (double unixSeconds) const
{
    static const char* const en[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char* const it[] = { "gen", "feb", "mar", "apr", "mag", "giu",
                                      "lug", "ago", "set", "ott", "nov", "dic" };
    const auto time = juce::Time (static_cast<juce::int64> (unixSeconds * 1000.0));
    return juce::String (time.getDayOfMonth()) + " " + (italian ? it : en)[time.getMonth()]
           + " " + juce::String (time.getYear());
}

juce::String PaywallOverlay::planName (const juce::String& id) const
{
    return id == vp::store::kLifetimeId ? t ("Lifetime", "Lifetime")
         : id == vp::store::kYearlyId   ? t ("Yearly", "Annuale") : t ("Monthly", "Mensile");
}

juce::String PaywallOverlay::headlineText() const
{
    if (statusPage())
        return t ("You're all set", "Tutto pronto");
    if (const auto* p = selected(); p != nullptr && p->trialEligible && p->trialDays > 0)
        return t ("Try it free for ", "Provalo gratis per ") + dayWord (italian, p->trialDays);
    if (status.hadSubscription)
        return t ("Pick up where you left off", "Riprendi da dove avevi lasciato");
    return t ("Play with a percussionist that listens", "Suona con un percussionista che ti ascolta");
}

juce::String PaywallOverlay::subText() const
{
    if (status.access == vp::store::Access::trial)
        return t ("Your free trial is active.", "La prova gratuita è attiva.");
    if (status.ownsLifetime())
        return t ("Thank you for your purchase.", "Grazie per il tuo acquisto.");
    if (status.access == vp::store::Access::subscribed)
        return t ("Thank you for subscribing.", "Grazie per il tuo abbonamento.");
    return t ("It follows your band's tempo and plays along, in time.",
              "Segue il tempo della tua band e suona insieme a lei, a tempo.");
}

juce::String PaywallOverlay::priceNote() const
{
    const auto* p = selected();
    if (status.productsFailed || p == nullptr || statusPage())
        return {};
    if (p->lifetime)
        return juce::String (p->price) + t (" one-time payment. No subscription, no renewal.",
                                            " una tantum. Nessun abbonamento, nessun rinnovo.");
    const auto per = p->yearly ? t ("per year", "all'anno") : t ("per month", "al mese");
    const auto cancel = t ("Cancel anytime.", "Disdici quando vuoi.");
    if (p->trialEligible && p->trialDays > 0)
        return dayWord (italian, p->trialDays) + t (" free, then ", " gratis, poi ")
               + juce::String (p->price) + " " + per + ". " + cancel;
    return juce::String (p->price) + " " + per + ". " + cancel;
}

juce::String PaywallOverlay::legalText() const
{
    if (lifetimeInView())
        return t ("Payment is charged to your Apple ID at confirmation of purchase. This is a one-time purchase, "
                  "not a subscription: it does not renew. You can restore it on your other devices with "
                  "Restore purchases.",
                  "Il pagamento viene addebitato sul tuo ID Apple alla conferma dell'acquisto. E' un acquisto "
                  "singolo, non un abbonamento: non si rinnova. Puoi ripristinarlo sugli altri tuoi dispositivi "
                  "con Ripristina acquisti.");
    return t ("Payment is charged to your Apple ID at confirmation of purchase, or when the free trial ends. "
              "The subscription renews automatically unless it is cancelled at least 24 hours before the end of "
              "the current period. Your account is charged for renewal within 24 hours before the period ends. "
              "Manage or cancel anytime in Settings > Apple ID > Subscriptions. "
              "Any unused part of a free trial is forfeited when you subscribe.",
              "Il pagamento viene addebitato sul tuo ID Apple alla conferma dell'acquisto, o al termine della prova gratuita. "
              "L'abbonamento si rinnova automaticamente, a meno che non venga annullato almeno 24 ore prima della fine "
              "del periodo in corso. Il rinnovo viene addebitato nelle 24 ore precedenti la fine del periodo. "
              "Puoi gestire o annullare l'abbonamento in qualsiasi momento da Impostazioni > ID Apple > Abbonamenti. "
              "La parte inutilizzata della prova gratuita va persa quando ti abboni.");
}

juce::String PaywallOverlay::ctaText() const
{
    if (busy)
        return t ("Please wait...", "Attendi...");
    if (statusPage())
        return t ("Manage subscription", "Gestisci abbonamento");
    if (const auto* p = selected(); p != nullptr && p->lifetime)
        return t ("Buy lifetime access", "Acquista l'accesso lifetime");
    if (const auto* p = selected(); p != nullptr && p->trialEligible && p->trialDays > 0)
        return t ("Start ", "Prova gratis per ") + (italian ? dayWord (true, p->trialDays)
                                                           : juce::String (p->trialDays) + "-day free trial");
    return t ("Subscribe", "Abbonati");
}

juce::Font PaywallOverlay::headlineFont() const
{
    return fontDisplay (30.0f * geo.scale);
}

// ---- controls ---------------------------------------------------------------

void PaywallOverlay::refreshControls()
{
    const bool onStatus = statusPage();
    const bool ready = offerReady();
    const float s = geo.scale;

    closeButton->text = t ("Close", "Chiudi");   // read by VoiceOver; the button draws a cross
    closeButton->scale = s;
    closeButton->setEnabled (! busy);
    closeButton->setTitle (closeButton->text);

    ctaButton->text = ctaText();
    ctaButton->scale = s;
    ctaButton->filled = ! onStatus;
    ctaButton->setEnabled (! busy && (onStatus || ready));
    ctaButton->setTitle (ctaButton->text);

    for (int i = 0; i < 3; ++i)
    {
        auto& b = *planButtons[i];
        const auto* p = plan (i);
        b.scale = s;
        b.setVisible (ready && p != nullptr);
        b.setEnabled (! busy);
        if (p == nullptr)
            continue;
        b.selected = i == selectedPlan;
        b.title = p->lifetime ? t ("Lifetime", "Lifetime")
                : p->yearly   ? t ("Yearly", "Annuale") : t ("Monthly", "Mensile");
        // What is charged is the big figure on the right; the line on the left
        // only says how it is billed. (A per-month equivalent bigger than the
        // real charge is what App Review rejects.)
        const bool trial = p->trialEligible && p->trialDays > 0;
        const auto billed = p->yearly ? t ("billed yearly", "addebito annuale") : t ("billed monthly", "addebito mensile");
        b.detail = p->lifetime ? t ("One payment, yours for good", "Un solo pagamento, tuo per sempre")
                 : trial       ? dayWord (italian, p->trialDays) + t (" free, then ", " gratis, poi ") + billed
                               : billed.substring (0, 1).toUpperCase() + billed.substring (1);
        b.rightTop = juce::String (p->price);
        b.rightBottom = p->lifetime ? t ("one-time", "una tantum")
                      : p->yearly   ? t ("per year", "all'anno") : t ("per month", "al mese");
        b.badge = p->yearly && p->savePercent > 0
                      ? t ("BEST VALUE  -", "CONVIENE  -") + juce::String (p->savePercent) + "%" : juce::String();
        b.setTitle (b.title + ", " + b.detail);
        b.setTooltip (b.getTitle());
    }

    restoreLink->text = t ("Restore purchases", "Ripristina acquisti");
    redeemLink->text = t ("Redeem code", "Riscatta codice");
    termsLink->text = t ("Terms of Use", "Condizioni d'uso");
    privacyLink->text = t ("Privacy Policy", "Privacy");
    licensesLink->text = t ("Licenses", "Licenze");
    retryLink->text = t ("Try again", "Riprova");
    manageLink->text = {};
    for (auto* l : { restoreLink.get(), redeemLink.get(), termsLink.get(), privacyLink.get(), licensesLink.get(), retryLink.get() })
    {
        l->scale = s;
        l->setTitle (l->text);
    }
    restoreLink->setEnabled (! busy);
    restoreLink->setVisible (true);
    redeemLink->setVisible (static_cast<bool> (onRedeem));
    redeemLink->setEnabled (! busy);
    termsLink->setVisible (true);
    privacyLink->setVisible (true);
    licensesLink->setVisible (true);
    retryLink->setVisible (status.productsFailed && ! onStatus);
    manageLink->setVisible (false);
    // A lifetime owner has no subscription to manage: the page is just the receipt.
    ctaButton->setVisible (! (onStatus && lifetimeInView()));
    closeButton->setVisible (true);
    ctaButton->repaint();
}

// ---- layout -----------------------------------------------------------------

void PaywallOverlay::resized()
{
    licensesPage->setBounds (getLocalBounds());
    layoutAll();
}

void PaywallOverlay::layoutAll()
{
    if (getWidth() < 40 || getHeight() < 40)
        return;

    const bool onStatus = statusPage();
    const bool ready = offerReady();
    auto area = safe.subtractedFrom (getLocalBounds()).reduced (20, 10);

    geo = {};
    const auto closeRect = area.removeFromTop (38).removeFromRight (30).withSizeKeepingCentre (30, 30);
    area.removeFromTop (6);

    const bool twoCol = area.getWidth() >= 640 && area.getWidth() > area.getHeight() * 1.15f;
    // A tablet in portrait: the same page, larger, instead of a phone-sized
    // column in the middle of a big screen.
    const float zoom = (! twoCol && area.getWidth() >= 800 && area.getHeight() >= 900) ? 1.4f : 1.0f;
    const int colW = twoCol ? juce::jmin (kMaxColumn - 100, (area.getWidth() - 40) / 2)
                            : juce::jmin (static_cast<int> (kMaxColumn * zoom), area.getWidth());

    // One pass at scale 1 to see how tall the page wants to be, then the same
    // pass again at the scale that makes it fit. The settings page does the
    // same thing with its cards.
    struct Item { juce::Rectangle<int>* out; int h; };
    auto pass = [&] (float s, juce::Array<Item>& left, juce::Array<Item>& right, Geometry& g,
                     int& leftH, int& rightH)
    {
        auto F = [&] (float h, bool bold) { return fontUi (h * s, bold); };
        const int bulletW = colW - static_cast<int> (26 * s);
        left.clear();
        right.clear();
        g.scale = s;

        left.add ({ &g.brand, static_cast<int> (20 * s) });
        left.add ({ nullptr, static_cast<int> (6 * s) });
        left.add ({ &g.headline, textHeight (headlineText(), fontDisplay (30.0f * s), colW) });
        left.add ({ nullptr, static_cast<int> (6 * s) });
        left.add ({ &g.sub, textHeight (subText(), F (15.0f, false), colW) });
        left.add ({ nullptr, static_cast<int> (14 * s) });
        if (! onStatus)
        {
            const char* const en[] = { "Follows the tempo of the band, live",
                                       "Shaker, congas, clap and more, in the right groove",
                                       "Most precise on a mixer feed; also the mic or a loaded track" };
            const char* const it[] = { "Segue il tempo della band, dal vivo",
                                       "Shaker, conga, clap e altro, nel groove giusto",
                                       "Massima precisione dal mixer; anche microfono o brano caricato" };
            g.bulletCount = 3;
            for (int i = 0; i < 3; ++i)
                left.add ({ &g.bullets[i], juce::jmax (static_cast<int> (26 * s),
                                                       textHeight (t (en[i], it[i]), F (14.0f, false), bulletW) + 6) });
        }

        if (onStatus)
            right.add ({ &g.statusCard, static_cast<int> (112 * s) });
        else if (ready)
        {
            for (int i = 0; i < planCount(); ++i)
            {
                if (i > 0)
                    right.add ({ nullptr, static_cast<int> (8 * s) });
                right.add ({ &g.planRect[i], static_cast<int> ((i == 0 ? 76 : 64) * s) });
            }
        }
        else
            right.add ({ &g.statusCard, static_cast<int> (112 * s) });   // "reading the store" / "unreachable"
        right.add ({ nullptr, static_cast<int> (14 * s) });
        if (! (onStatus && lifetimeInView()))
            right.add ({ &g.cta, static_cast<int> (54 * s) });
        right.add ({ nullptr, static_cast<int> (8 * s) });
        if (priceNote().isNotEmpty())
            right.add ({ &g.price, textHeight (priceNote(), F (13.0f, true), colW) });
        if (notice.isNotEmpty())
        {
            right.add ({ nullptr, static_cast<int> (4 * s) });
            right.add ({ &g.noticeRow, textHeight (notice, F (12.5f, true), colW) });
        }
        right.add ({ nullptr, static_cast<int> (8 * s) });
        right.add ({ &g.legal, textHeight (legalText(), F (10.5f, false), colW) });
        right.add ({ nullptr, static_cast<int> (6 * s) });
        right.add ({ &g.links, static_cast<int> (56 * s) });   // two rows

        auto sum = [] (const juce::Array<Item>& v) { int n = 0; for (auto& i : v) n += i.h; return n; };
        leftH = sum (left);
        rightH = sum (right);
    };

    juce::Array<Item> left, right;
    Geometry g;
    int leftH = 0, rightH = 0;
    pass (zoom, left, right, g, leftH, rightH);
    const int want = twoCol ? juce::jmax (leftH, rightH) : leftH + 14 + rightH;
    const float s = juce::jlimit (0.55f, zoom, zoom * static_cast<float> (area.getHeight()) / static_cast<float> (juce::jmax (1, want)));
    pass (s, left, right, g, leftH, rightH);

    auto place = [] (juce::Rectangle<int> col, juce::Array<Item>& items)
    {
        for (auto& it : items)
        {
            auto r = col.removeFromTop (it.h);
            if (it.out != nullptr)
                *it.out = r;
        }
        return col.getY();
    };

    const int gap = static_cast<int> (14 * s);
    if (twoCol)
    {
        auto l = area.removeFromLeft (area.getWidth() / 2).withSizeKeepingCentre (colW, area.getHeight());
        auto r = area.withSizeKeepingCentre (colW, area.getHeight());
        place (l.withTrimmedTop (juce::jmax (0, (l.getHeight() - leftH) / 2)), left);
        place (r.withTrimmedTop (juce::jmax (0, (r.getHeight() - rightH) / 2)), right);
    }
    else
    {
        auto col = area.withSizeKeepingCentre (colW, area.getHeight());
        col.removeFromTop (juce::jmax (0, (col.getHeight() - (leftH + gap + rightH)) / 3));
        const int y = place (col, left) + gap;
        place (juce::Rectangle<int> (col.getX(), y, colW, juce::jmax (0, col.getBottom() - y)), right);
    }

    geo = g;
    closeButton->setBounds (closeRect);
    ctaButton->setBounds (geo.cta);
    for (int i = 0; i < 3; ++i)
        planButtons[i]->setBounds (geo.planRect[i]);

    // Two rows: the page's own actions, then the legal links.
    auto links = geo.links;
    auto actions = links.removeFromTop (links.getHeight() / 2);
    const int half = colW / 2;
    for (auto* l : { restoreLink.get(), redeemLink.get() })
        l->setBounds (actions.removeFromLeft (half));
    const int third = colW / 3;
    for (auto* l : { termsLink.get(), privacyLink.get(), licensesLink.get() })
        l->setBounds (links.removeFromLeft (third));
    retryLink->setBounds (geo.statusCard.withSizeKeepingCentre (150, 30).translated (0, 28));

    refreshControls();
}

// ---- paint ------------------------------------------------------------------

void PaywallOverlay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.fillAll (bg());
    juce::ColourGradient floor (gDarkMode ? juce::Colour (0xff0a0a0c) : juce::Colour (0xffffffff),
                                full.getCentreX(), full.getY(), bg(), full.getCentreX(), full.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (full);
    paintRadial (g, { full.getCentreX(), full.getY() + 40.0f }, full.getWidth() * 0.6f, fuchsia(), 0.14f);

    const float s = geo.scale;
    auto F = [&] (float h, bool bold) { return fontUi (h * s, bold); };

    // brand
    {
        auto b = geo.brand;
        g.setColour (fuchsia());
        g.fillRoundedRectangle (b.removeFromLeft (static_cast<int> (20 * s)).withSizeKeepingCentre (static_cast<int> (8 * s), static_cast<int> (8 * s)).toFloat(), 1.8f);
        g.setColour (::text());
        g.setFont (F (13.0f, true));
        g.drawText ("VIRTUAL PERCUSSIONIST", b, juce::Justification::centredLeft, true);
    }

    drawWrapped (g, headlineText(), headlineFont(), ::text(), geo.headline);
    drawWrapped (g, subText(), F (15.0f, false), mute(), geo.sub);

    if (! statusPage())
    {
        const char* const en[] = { "Follows the tempo of the band, live",
                                   "Shaker, congas, clap and more, in the right groove",
                                   "Most precise on a mixer feed; also the mic or a loaded track" };
        const char* const it[] = { "Segue il tempo della band, dal vivo",
                                   "Shaker, conga, clap e altro, nel groove giusto",
                                   "Massima precisione dal mixer; anche microfono o brano caricato" };
        for (int i = 0; i < geo.bulletCount; ++i)
        {
            auto r = geo.bullets[i];
            g.setColour (fuchsia());
            g.fillEllipse (static_cast<float> (r.getX()), r.getY() + 5.0f * s, 7.0f * s, 7.0f * s);
            drawWrapped (g, t (en[i], it[i]), F (14.0f, false), ::text(), r.withTrimmedLeft (static_cast<int> (20 * s)));
        }
    }

    // The card where the plans would be, when they are not: someone already in,
    // or the store has not answered.
    if (statusPage() || ! offerReady())
    {
        const auto card = geo.statusCard.toFloat();
        g.setColour (panel());
        g.fillRoundedRectangle (card, 14.0f);
        g.setColour (border());
        g.drawRoundedRectangle (card, 14.0f, 1.0f);
        auto in = geo.statusCard.reduced (static_cast<int> (16 * s), static_cast<int> (10 * s));

        if (statusPage())
        {
            const auto* active = [&]() -> const vp::store::Plan*
            {
                for (const auto& p : status.plans)
                    if (p.id == status.activePlanId)
                        return &p;
                return nullptr;
            }();
            g.setColour (mute());
            g.setFont (F (12.0f, true));
            g.drawText (t ("YOUR PLAN", "IL TUO PIANO"), in.removeFromTop (static_cast<int> (18 * s)), juce::Justification::centredLeft, true);
            g.setColour (::text());
            g.setFont (F (20.0f, true));
            g.drawText (planName (status.activePlanId), in.removeFromTop (static_cast<int> (28 * s)), juce::Justification::centredLeft, true);

            juce::String line, detail;
            if (status.ownsLifetime())
            {
                line = t ("Lifetime access", "Accesso lifetime");
                detail = t ("One payment, no renewals. It stays with your Apple ID.",
                            "Un solo pagamento, nessun rinnovo. Resta legato al tuo ID Apple.");
            }
            else if (status.access == vp::store::Access::trial)
            {
                const int n = vp::store::daysLeft (status.expiresAt, clock(), active != nullptr ? active->trialDays : 0);
                line = t ("Free trial: ", "Prova gratuita: ") + dayWord (italian, n) + t (" left", " rimasti");
                detail = t ("Ends on ", "Termina il ") + dateText (status.expiresAt);
                if (active != nullptr && status.renewal != vp::store::Status::Renewal::ends)
                    detail += t (". Then ", ". Poi ") + juce::String (active->price) + (active->yearly ? t (" per year", " all'anno") : t (" per month", " al mese"))
                              + t (", unless you cancel first.", ", salvo disdetta prima.");
                else if (status.renewal == vp::store::Status::Renewal::ends)
                    detail += t (". It will not renew.", ". Non si rinnoverà.");
            }
            else
            {
                line = status.renewal == vp::store::Status::Renewal::renews ? t ("Renews on ", "Si rinnova il ") + dateText (status.expiresAt)
                     : status.renewal == vp::store::Status::Renewal::ends   ? t ("Ends on ", "Termina il ") + dateText (status.expiresAt)
                                                                          : t ("Active until ", "Attivo fino al ") + dateText (status.expiresAt);
            }
            g.setColour (status.access == vp::store::Access::trial ? stateLocked() : ::text());
            g.setFont (F (14.0f, true));
            g.drawText (line, in.removeFromTop (static_cast<int> (20 * s)), juce::Justification::centredLeft, true);
            drawWrapped (g, detail, F (12.0f, false), mute(), in);
        }
        else
        {
            g.setColour (::text());
            g.setFont (F (15.0f, true));
            g.drawFittedText (status.productsFailed
                                  ? t ("Can't reach the App Store.\nCheck your connection and try again.",
                                       "App Store non raggiungibile.\nControlla la connessione e riprova.")
                                  : t ("Contacting the App Store...", "Contatto l'App Store..."),
                              in.withTrimmedBottom (status.productsFailed ? static_cast<int> (34 * s) : 0),
                              juce::Justification::centred, 3);
        }
    }

    drawWrapped (g, priceNote(), F (13.0f, true), ::text(), geo.price, juce::Justification::topLeft);
    drawWrapped (g, notice, F (12.5f, true), stateSearching(), geo.noticeRow);
    drawWrapped (g, legalText(), F (10.5f, false), mute().withAlpha (0.9f), geo.legal);
}
