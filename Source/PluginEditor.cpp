#include "PluginEditor.h"
namespace{const juce::Colour bg(0xff171c22),cream(0xfff0e7d7),gold(0xffcba86a),blue(0xff70b5c9);}
OptoFETEditor::OptoFETEditor(OptoFETProcessor& p):AudioProcessorEditor(p),processor(p){
 const char* ids[]={"input","output","peak","optoGain","drive","attack","fetRelease",""};
 const char* names[]={"GLOBAL INPUT","GLOBAL OUTPUT","OPTICAL REDUCTION","OPTICAL GAIN","1176 DRIVE / LEVEL","1176 ATTACK","1176 RELEASE",""};
 for(size_t i=0;i<7;++i){auto& k=dials[i];k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,85,21);
  k.slider.setColour(juce::Slider::rotarySliderFillColourId,i<4?gold:blue);k.slider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff3b4651));
  k.slider.setColour(juce::Slider::thumbColourId,cream);k.slider.setColour(juce::Slider::textBoxTextColourId,cream);
  k.slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);k.slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
  addAndMakeVisible(k.slider);k.label.setText(names[i],juce::dontSendNotification);k.label.setJustificationType(juce::Justification::centred);k.label.setColour(juce::Label::textColourId,cream);addAndMakeVisible(k.label);
  k.attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],k.slider);}
 const char* choiceIds[]={"optoRatio","response","release","fetRatio","oversampling"};
 const char* choiceNames[]={"OPTICAL MODE","T4 RESPONSE","OPTICAL RELEASE","1176 RATIO","OVERSAMPLING"};
 const char* options[][4]={{"COMPRESS","LIMIT",nullptr,nullptr},{"ENHANCED","CLASSIC T4",nullptr,nullptr},{"LA-2A SLOW","LA-3A FAST",nullptr,nullptr},{"4:1","8:1","12:1","20:1"},{"OFF (1X)","2X","4X","8X"}};
 for(size_t i=0;i<choices.size();++i){auto& k=choices[i];for(int j=0;j<4;++j)if(options[i][j])k.combo.addItem(options[i][j],j+1);
  k.combo.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff2b3540));k.combo.setColour(juce::ComboBox::textColourId,cream);k.combo.setColour(juce::ComboBox::outlineColourId,i==3?blue:gold);
  addAndMakeVisible(k.combo);k.label.setText(choiceNames[i],juce::dontSendNotification);k.label.setJustificationType(juce::Justification::centred);k.label.setColour(juce::Label::textColourId,cream);addAndMakeVisible(k.label);
  k.attachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,choiceIds[i],k.combo);}
 vintage.setButtonText("VINTAGE BANDWIDTH");reverse.setButtonText("1176 FIRST / REVERSE ORDER");
 for(auto* b:{&vintage,&reverse}){b->setColour(juce::ToggleButton::textColourId,cream);b->setColour(juce::ToggleButton::tickColourId,gold);addAndMakeVisible(*b);}
 vintageAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"vintage",vintage);
 reverseAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"reverse",reverse);
 setSize(1120,620);startTimerHz(24);
}
void OptoFETEditor::paint(juce::Graphics& g){
 g.fillAll(bg);g.setColour(juce::Colour(0xff242d35));g.fillRoundedRectangle(12,12,1096,596,13);
 g.setColour(gold);g.setFont(juce::Font(juce::FontOptions(32,juce::Font::bold)));g.drawText("OPTO / FET   0.3",36,22,620,44,juce::Justification::centredLeft);
 g.setColour(cream);g.setFont(juce::Font(juce::FontOptions(13)));g.drawText("T4-INSPIRED OPTICAL LEVELING  +  REV D-INSPIRED FET LIMITING",38,68,900,25,juce::Justification::centredLeft);
 g.setColour(gold);g.drawRoundedRectangle(26,116,520,405,9,1.5f);g.setColour(blue);g.drawRoundedRectangle(558,116,534,405,9,1.5f);
 g.setFont(juce::Font(juce::FontOptions(19,juce::Font::bold)));g.setColour(gold);g.drawText("OPTICAL / LA-2A",42,126,430,32,juce::Justification::centredLeft);
 g.setColour(blue);g.drawText("FET / 1176 REV D",575,126,420,32,juce::Justification::centredLeft);
 g.setColour(cream);g.setFont(juce::Font(juce::FontOptions(12)));
 g.drawText("Transformer stages + valve colour  |  20 kHz interstage LP",42,490,485,20,juce::Justification::centredLeft);
 g.drawText("Drive raises detector input; linked output trims gain",574,490,500,20,juce::Justification::centredLeft);
 g.drawText("Optical GR: "+juce::String(optical,1)+" dB",41,537,255,25,juce::Justification::centredLeft);
 g.drawText("FET GR: "+juce::String(fet,1)+" dB",303,537,240,25,juce::Justification::centredLeft);
 g.drawText("ORDER: "+juce::String(reverse.getToggleState()?"1176 -> LA-2A":"LA-2A -> 1176"),568,537,480,25,juce::Justification::centredLeft);
}
void OptoFETEditor::resized(){
 const int positions[][2]={{38,174},{174,174},{38,294},{174,294},{580,174},{730,174},{880,174}};
 for(int i=0;i<7;++i){auto& k=dials[(size_t)i];k.slider.setBounds(positions[i][0],positions[i][1],125,108);k.label.setBounds(positions[i][0]-10,positions[i][1]+106,145,25);}
 const int choicePos[][2]={{326,184},{326,252},{326,320},{622,368},{622,438}};
 for(int i=0;i<5;++i){auto& k=choices[(size_t)i];k.label.setBounds(choicePos[i][0],choicePos[i][1],175,22);k.combo.setBounds(choicePos[i][0]+5,choicePos[i][1]+24,165,28);}
 vintage.setBounds(332,416,205,34);reverse.setBounds(806,415,268,35);
}
void OptoFETEditor::timerCallback(){optical=.8f*optical+.2f*processor.opticalGR.load();fet=.8f*fet+.2f*processor.fetGR.load();repaint();}
