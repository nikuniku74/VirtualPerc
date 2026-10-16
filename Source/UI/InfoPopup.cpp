#include "UI/InfoPopup.h"
#include "UI/MainComponentShared.h"

#include <cmath>

class InfoPopup::CrossButton final : public juce::Button
{
public:
    CrossButton() : juce::Button ({}) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto d = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (ink().withMultipliedBrightness (down ? 0.85f : (over ? 1.08f : 1.0f)));
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
        g.setColour (::text());
        g.strokePath (x, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

class InfoPopup::ConfirmButton final : public juce::Button
{
public:
    ConfirmButton() : juce::Button ({}) {}
    juce::String text;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (fuchsia().withMultipliedBrightness (down ? 0.85f : (over ? 0.93f : 1.0f)));
        g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 16.0f));
        g.setColour (juce::Colours::white);
        g.setFont (fontUi (17.0f));
        g.drawText (text, getLocalBounds(), juce::Justification::centred, true);
    }
};

InfoPopup::InfoPopup()
{
    setOpaque (false);
    setVisible (false);
    setInterceptsMouseClicks (true, true);

    closeButton = std::make_unique<CrossButton>();
    closeButton->setTitle ("Close");
    closeButton->onClick = [this] { close(); };
    addAndMakeVisible (*closeButton);

    confirmButton = std::make_unique<ConfirmButton>();
    confirmButton->onClick = [this] { close(); };
    addChildComponent (*confirmButton);

    body.setMultiLine (true, true);
    body.setReadOnly (true);
    body.setCaretVisible (false);
    body.setPopupMenuEnabled (false);
    body.setScrollbarsShown (true);
    addAndMakeVisible (body);
}

InfoPopup::~InfoPopup() = default;

void InfoPopup::setContent (const juce::String& t, const juce::String& text)
{
    title = t;
    body.setText (text, false);
    resized();
    repaint();
}

void InfoPopup::setConfirmText (const juce::String& text)
{
    confirmButton->text = text;
    confirmButton->setTitle (text);
    confirmButton->setVisible (text.isNotEmpty());
    resized();
}

void InfoPopup::open()
{
    body.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::textColourId, ::text());
    body.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    body.applyFontToAllText (fontUi (15.0f, false));
    body.setFont (fontUi (15.0f, false));
    body.moveCaretToTop (false);
    resized();
    setVisible (true);
    toFront (false);
    repaint();
}

void InfoPopup::close()
{
    const bool wasShowing = isVisible();
    setVisible (false);
    if (wasShowing && onClosed)
        onClosed();
}

void InfoPopup::mouseDown (const juce::MouseEvent& e)
{
    // A tap outside the card dismisses it, the way any sheet does.
    if (! card.contains (e.getPosition()))
        close();
}

void InfoPopup::resized()
{
    const auto area = getLocalBounds().reduced (20, 60);
    if (area.isEmpty())
        return;

    const int width = juce::jmin (area.getWidth(), 600);
    const int textW = width - 40 - 8;
    juce::AttributedString a;
    a.setJustification (juce::Justification::topLeft);
    a.append (body.getText(), fontUi (15.0f, false), juce::Colours::white);
    juce::TextLayout layout;
    layout.createLayout (a, static_cast<float> (juce::jmax (40, textW)));
    const int textH = static_cast<int> (std::ceil (layout.getHeight())) + 24;
    const int head = 56;
    const int foot = confirmButton->isVisible() ? 64 : 0;
    const int height = juce::jmin (area.getHeight(), head + textH + 16 + foot);

    card = juce::Rectangle<int> (width, height).withCentre (area.getCentre());
    auto inner = card.reduced (20, 14);
    auto top = inner.removeFromTop (head - 14);
    closeButton->setBounds (top.removeFromRight (30).withSizeKeepingCentre (30, 30));
    titleArea = top;
    if (foot > 0)
        confirmButton->setBounds (inner.removeFromBottom (48).reduced (0, 0));
    body.setBounds (inner.withTrimmedTop (4).withTrimmedBottom (foot > 0 ? 12 : 0));
}

void InfoPopup::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.62f));
    g.setColour (panel());
    g.fillRoundedRectangle (card.toFloat(), 18.0f);
    g.setColour (border());
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 18.0f, 1.0f);
    g.setColour (fuchsia());
    g.fillRoundedRectangle (static_cast<float> (titleArea.getX()), titleArea.getCentreY() - 4.0f, 8.0f, 8.0f, 1.8f);
    g.setColour (::text());
    g.setFont (fontUi (17.0f));
    g.drawText (title, titleArea.withTrimmedLeft (20), juce::Justification::centredLeft, true);
}
