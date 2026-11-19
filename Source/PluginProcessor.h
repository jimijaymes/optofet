#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
class OptoFETProcessor final : public juce::AudioProcessor {
public:
 OptoFETProcessor();
 void prepareToPlay(double,int) override;
 void releaseResources() override {}
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
 juce::AudioProcessorEditor* createEditor() override;
 bool hasEditor() const override {return true;}
 const juce::String getName() const override {return "OptoFET 0.1";}
 bool acceptsMidi() const override {return false;}
 bool producesMidi() const override {return false;}
 bool isMidiEffect() const override {return false;}
 double getTailLengthSeconds() const override {return 0;}
 int getNumPrograms() override {return 1;}
 int getCurrentProgram() override {return 0;}
 void setCurrentProgram(int) override {}
 const juce::String getProgramName(int) override {return {};}
 void changeProgramName(int,const juce::String&) override {}
 void getStateInformation(juce::MemoryBlock&) override;
 void setStateInformation(const void*,int) override;
 juce::AudioProcessorValueTreeState parameters;
 std::atomic<float> opticalGR{0},fetGR{0};
private:
 static juce::AudioProcessorValueTreeState::ParameterLayout layout();
 struct OpticalState {float bass=0,presence=0,bassEnv=0,presenceEnv=0,enhanced=0,memory=0,exposure=0,t4Fast=0,t4Slow=0,highCut=0,transformerBass=0,transformerFlux=0,transformerTreble=0;};
 struct FETState {float envelope=0,gr=0,transformerLow=0,ampMemory=0,levelInput=0,levelOutput=0,compensation=1;};
 std::array<OpticalState,2> opto;
 std::array<FETState,2> fet;
 std::array<float,2> detectorHPx{},detectorHPy{};
 juce::AudioBuffer<float> stageBuffer;
 double sr=44100;
 int blockSize=512;
 float orderBlend=0,vintageBlend=0;
 void processOptical(juce::AudioBuffer<float>&,int,const std::array<float,2>&,float,float,float,bool,bool,bool);
 void processFET(juce::AudioBuffer<float>&,int,float,float,float,float);
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OptoFETProcessor)
};
