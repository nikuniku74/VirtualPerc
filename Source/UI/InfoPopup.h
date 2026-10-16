#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

/** A card of text over a dimmed page, closed by its cross or by a tap outside.
    Used for the longer explanations that do not belong on the settings page
    itself (the input modes and why MIXER is the one for live). It fills its
    parent; the parent decides when it is shown. */
class InfoPopup final : public juce::Component
{
public:
    InfoPopup();
    ~InfoPopup() override;

    void setContent (const juce::String& title, const juce::String& text);
    /** A filled button under the text ("Got it"); empty = only the cross. */
    void setConfirmText (const juce::String&);
    void open();
    void close();
    /** Called whenever the popup closes, by whichever control. */
    std::function<void()> onClosed;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    class CrossButton;
    class ConfirmButton;

    juce::String title;
    juce::TextEditor body;
    std::unique_ptr<CrossButton> closeButton;
    std::unique_ptr<ConfirmButton> confirmButton;
    juce::Rectangle<int> card, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPopup)
};
