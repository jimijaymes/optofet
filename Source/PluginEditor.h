#pragma once
#include "PluginProcessor.h"
class OptoFETEditor final:public juce::AudioProcessorEditor,private juce::Timer{
public:
 explicit OptoFETEditor(OptoFETProcessor&);
 void paint(juce::Graphics&) override;
    void paintPanel(juce::Graphics&);
    struct ZoomPanel : juce::Component { std::function<void(juce::Graphics&)> draw; void paint(juce::Graphics& g) override { if(draw)draw(g); } } zoomPanel;
    juce::ComboBox zoomChoice;
    bool zoomReady=false;
 void resized() override;
private:
 struct Dial{juce::Slider slider;juce::Label label;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;};
 struct Choice{juce::ComboBox combo;juce::Label label;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;};
 OptoFETProcessor& processor;
 std::array<Dial,9> dials;
 std::array<Choice,6> choices;
 juce::ToggleButton vintage,reverse,optoEnabled,fetEnabled;
 std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> vintageAttachment,reverseAttachment,optoEnabledAttachment,fetEnabledAttachment;
 float optical=0,fet=0;
 void timerCallback() override;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OptoFETEditor)
};
