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
    void open();
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    class CrossButton;

    juce::String title;
    juce::TextEditor body;
    std::unique_ptr<CrossButton> closeButton;
    juce::Rectangle<int> card, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPopup)
};
