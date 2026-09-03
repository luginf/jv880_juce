#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A thin, full-width clickable bar that collapses/expands whatever drawer sits below it
// (currently just VirtualKeyboard) - Alan's request, so the keyboard can be tucked away when
// it's not needed instead of always eating space at the bottom of the window. Deliberately
// simple (no drag-to-resize, no animation) - just a click to toggle.
class CollapseHandle final : public juce::Component
{
public:
    void paint(juce::Graphics &g) override
    {
        auto bounds = getLocalBounds();
        g.setColour(juce::Colour(0xff1a1a1e));
        g.fillRect(bounds);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRect(bounds, 1);

        g.setColour(juce::Colour(0xff8a8a94));
        g.setFont(juce::FontOptions(12.0f));
        // U+25BC/25B2 (filled down/up triangle) rather than an image asset - a single glyph
        // is enough to show which way clicking will go.
        const juce::String arrow = expanded ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc"))
                                             : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb2"));
        g.drawText(arrow + " " + label, bounds, juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        if (onClick) onClick();
    }

    void setLabel(const juce::String &l) { label = l; repaint(); }
    void setExpanded(bool e) { expanded = e; repaint(); }

    std::function<void()> onClick;

private:
    juce::String label { "Keyboard" };
    bool expanded = true;
};
