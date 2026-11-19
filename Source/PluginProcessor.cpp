#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
namespace {
float db(float x){return juce::Decibels::decibelsToGain(x);}
float pole(float hz,double sr){return std::exp(-2.0f*juce::MathConstants<float>::pi*hz/(float)sr);}
float timeCoeff(float sec,double sr){return std::exp(-1.0f/juce::jmax(1.0f,sec*(float)sr));}
float soft(float x){return std::tanh(x);}
float softKnee(float above,float ratio,float knee){
 const float slope=1.0f-1.0f/ratio;
 if(above<=-knee*0.5f)return 0;
 if(above>=knee*0.5f)return above*slope;
 float v=above+knee*0.5f;return slope*v*v/(2.0f*knee);
}
}
juce::AudioProcessorValueTreeState::ParameterLayout OptoFETProcessor::layout(){
 using F=juce::AudioParameterFloat;using C=juce::AudioParameterChoice;using B=juce::AudioParameterBool;
 juce::AudioProcessorValueTreeState::ParameterLayout p;
 p.add(std::make_unique<F>(juce::ParameterID{"input",1},"Global Input (dB)",juce::NormalisableRange<float>(-24,24,0.1f),0));
 p.add(std::make_unique<F>(juce::ParameterID{"output",1},"Global Output (dB)",juce::NormalisableRange<float>(-24,24,0.1f),0));
 p.add(std::make_unique<F>(juce::ParameterID{"peak",1},"Optical Peak Reduction",juce::NormalisableRange<float>(0,50,0.1f),18));
 p.add(std::make_unique<F>(juce::ParameterID{"optoGain",1},"Optical Makeup (dB)",juce::NormalisableRange<float>(-12,24,0.1f),0));
 p.add(std::make_unique<C>(juce::ParameterID{"optoRatio",1},"Optical Mode",juce::StringArray{"Compress","Limit"},0));
 p.add(std::make_unique<C>(juce::ParameterID{"response",1},"Optical Response",juce::StringArray{"Enhanced","Classic T4"},1));
 p.add(std::make_unique<C>(juce::ParameterID{"release",1},"Optical Release",juce::StringArray{"LA-2A Slow","LA-3A Fast"},0));
 p.add(std::make_unique<B>(juce::ParameterID{"vintage",1},"Vintage Bandwidth",false));
 p.add(std::make_unique<F>(juce::ParameterID{"drive",1},"1176 Drive / Auto Level",juce::NormalisableRange<float>(0,100,0.1f),25));
 p.add(std::make_unique<C>(juce::ParameterID{"fetRatio",1},"1176 Ratio",juce::StringArray{"4:1","8:1","12:1","20:1"},0));
 p.add(std::make_unique<F>(juce::ParameterID{"attack",1},"1176 Attack",juce::NormalisableRange<float>(0,100,0.1f),50));
 p.add(std::make_unique<F>(juce::ParameterID{"fetRelease",1},"1176 Release",juce::NormalisableRange<float>(0,100,0.1f),55));
 p.add(std::make_unique<B>(juce::ParameterID{"reverse",1},"1176 First",false));
 return p;
}
OptoFETProcessor::OptoFETProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"STATE",layout()){}
void OptoFETProcessor::prepareToPlay(double sampleRate,int maximumExpectedSamplesPerBlock){
 sr=sampleRate;blockSize=juce::jmax(1,maximumExpectedSamplesPerBlock);
 opto={};fet={};detectorHPx={};detectorHPy={};orderBlend=vintageBlend=0;
 stageBuffer.setSize(2,blockSize);setLatencySamples(0);
}
void OptoFETProcessor::processOptical(juce::AudioBuffer<float>& b,int n,const std::array<float,2>& unused,float peak,float gain,float ratio,bool classic,bool fast,bool vintage){
 juce::ignoreUnused(unused);
 const float split=pole(320,sr),lowPole=pole(180,sr),fluxPole=pole(65,sr),treblePole=pole(6500,sr);
 const float t4Attack=timeCoeff(.010f,sr),t4FastDecay=timeCoeff(.028f,sr);
 const float bassAttack=timeCoeff(.035f,sr),presenceAttack=timeCoeff(.010f,sr);
 const float bassRelease=timeCoeff(fast?.22f:.55f,sr),presenceRelease=timeCoeff(fast?.08f:.16f,sr);
 const float envRelease=timeCoeff(fast?.09f:.16f,sr);
 const float exposureUp=timeCoeff(.7f,sr),exposureDown=timeCoeff(2.0f,sr);
 const float vintagePole=pole(12000,sr),vintageSwitch=timeCoeff(.015f,sr);
 float avgGR=0;
 for(int i=0;i<n;++i){
  vintageBlend=vintageSwitch*vintageBlend+(1-vintageSwitch)*(vintage?1.0f:0.0f);
  for(int c=0;c<b.getNumChannels();++c){
   auto& s=opto[(size_t)c];float x=b.getSample(c,i);
   s.transformerBass=lowPole*s.transformerBass+(1-lowPole)*x;
   s.transformerFlux=fluxPole*s.transformerFlux+(1-fluxPole)*s.transformerBass;
   float d=1.375f*(1.0f+.135f*juce::jmin(2.0f,std::abs(s.transformerFlux)));
   float magnetic=x+.135f*(soft(d*s.transformerBass)/d-s.transformerBass);
   s.transformerTreble=treblePole*s.transformerTreble+(1-treblePole)*magnetic;
   magnetic-=.018f*juce::jlimit(0.0f,1.0f,std::abs(s.transformerFlux)*1.5f)*(magnetic-s.transformerTreble);
   const float hpA=1.0f/(1.0f+2.0f*juce::MathConstants<float>::pi*90.0f/(float)sr);
   float detected=hpA*(detectorHPy[(size_t)c]+magnetic-detectorHPx[(size_t)c]);
   detectorHPx[(size_t)c]=magnetic;detectorHPy[(size_t)c]=detected;
   s.bass=split*s.bass+(1-split)*detected;
   float bassLevel=std::abs(s.bass),highLevel=std::abs(detected-s.bass);
   float bc=bassLevel>s.bassEnv?bassAttack:bassRelease,pc=highLevel>s.presenceEnv?presenceAttack:presenceRelease;
   s.bassEnv=bc*s.bassEnv+(1-bc)*bassLevel;
   s.presenceEnv=pc*s.presenceEnv+(1-pc)*highLevel;
   const float enhancedLevel=std::sqrt(.36f*s.bassEnv*s.bassEnv+s.presenceEnv*s.presenceEnv);
   const float threshold=-peak;
   float enhancedTarget=softKnee(juce::Decibels::gainToDecibels(juce::jmax(enhancedLevel,1e-8f),-160.0f)-threshold,ratio,10.0f);
   const float ec=enhancedTarget>s.enhanced?presenceAttack:envRelease;
   s.enhanced=ec*s.enhanced+(1-ec)*enhancedTarget;
   const float classicTarget=softKnee(juce::Decibels::gainToDecibels(juce::jmax(std::abs(detected),1e-8f),-160.0f)-threshold,ratio,10.0f);
   float illumination=juce::jlimit(0.0f,1.0f,classicTarget/18.0f);
   float xc=illumination>s.exposure?exposureUp:exposureDown;
   s.exposure=xc*s.exposure+(1-xc)*illumination;
   float slowDecay=timeCoeff(1.0f+11.0f*s.exposure,sr);
   float fc=classicTarget>s.t4Fast?t4Attack:t4FastDecay;
   float sc=classicTarget>s.t4Slow?t4Attack:slowDecay;
   s.t4Fast=fc*s.t4Fast+(1-fc)*classicTarget;
   s.t4Slow=sc*s.t4Slow+(1-sc)*classicTarget;
   float classicGR=(.9f-.42f*s.exposure)*s.t4Fast+(.1f+.42f*s.exposure)*s.t4Slow;
   float gr=classic?classicGR:s.enhanced;avgGR+=gr;
   float wet=magnetic*db(-gr);
   float drive=1.0f+1.5f*.15f,bias=.08f*.15f;
   wet=(soft(wet*drive+bias)-soft(bias))/(drive*(1-soft(bias)*soft(bias)));
   s.highCut=vintagePole*s.highCut+(1-vintagePole)*wet;
   wet+=vintageBlend*(s.highCut-wet);
   b.setSample(c,i,wet*db(gain));
  }
 }
 opticalGR.store(avgGR/juce::jmax(1,n*b.getNumChannels()),std::memory_order_relaxed);
}
void OptoFETProcessor::processFET(juce::AudioBuffer<float>& b,int n,float drive,float ratio,float attack,float release){
 float inputDb=drive*.24f,pre=db(inputDb),post=db(-inputDb);
 float attackSec=.0008f*std::pow(.025f,attack);
 float releaseSec=.05f*std::pow(22.0f,1.0f-release);
 float ac=timeCoeff(attackSec,sr),rc=timeCoeff(releaseSec,sr);
 float levelCoef=timeCoeff(.35f,sr),servoCoef=timeCoeff(.65f,sr);
 float transformerPole=pole(110,sr),ampPole=pole(8500,sr);
 float sum=0;
 for(int i=0;i<n;++i)for(int c=0;c<b.getNumChannels();++c){
  auto& s=fet[(size_t)c];float x=b.getSample(c,i),hot=x*pre;
  s.transformerLow=transformerPole*s.transformerLow+(1-transformerPole)*hot;
  float magnetic=hot+.10f*(soft(1.6f*s.transformerLow)/1.6f-s.transformerLow);
  float a=std::abs(magnetic),ec=a>s.envelope?ac:rc;
  s.envelope=ec*s.envelope+(1-ec)*a;
  float levelDb=juce::Decibels::gainToDecibels(juce::jmax(s.envelope,1e-8f),-160.0f);
  float target=softKnee(levelDb-(-18.0f),ratio,5.0f);
  float gc=target>s.gr?ac:rc;
  s.gr=gc*s.gr+(1-gc)*target;
  float attenuated=magnetic*db(-s.gr);
  float nonlinear=soft(attenuated*(1.0f+.12f*drive))/(1.0f+.12f*drive);
  s.ampMemory=ampPole*s.ampMemory+(1-ampPole)*nonlinear;
  float coloured=nonlinear-.05f*drive*(nonlinear-s.ampMemory);
  s.levelInput=levelCoef*s.levelInput+(1-levelCoef)*x*x;
  float base=coloured*post;
  s.levelOutput=levelCoef*s.levelOutput+(1-levelCoef)*base*base;
  float wanted=std::sqrt((s.levelInput+1e-8f)/(s.levelOutput+1e-8f));
  wanted=juce::jlimit(.35f,3.0f,wanted);
  s.compensation=servoCoef*s.compensation+(1-servoCoef)*wanted;
  b.setSample(c,i,base*s.compensation);sum+=s.gr;
 }
 fetGR.store(sum/juce::jmax(1,n*b.getNumChannels()),std::memory_order_relaxed);
}
void OptoFETProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
 juce::ScopedNoDenormals noDenormals;
 const int channels=juce::jmin(2,buffer.getNumChannels()),n=buffer.getNumSamples();
 if(channels==0||n==0)return;
 for(int c=channels;c<buffer.getNumChannels();++c)buffer.clear(c,0,n);
 float in=db(*parameters.getRawParameterValue("input")),out=db(*parameters.getRawParameterValue("output"));
 float peak=*parameters.getRawParameterValue("peak"),optoMakeup=*parameters.getRawParameterValue("optoGain");
 bool classic=*parameters.getRawParameterValue("response")>.5f,fast=*parameters.getRawParameterValue("release")>.5f,vintage=*parameters.getRawParameterValue("vintage")>.5f;
 float opticalRatio=*parameters.getRawParameterValue("optoRatio")>.5f?20.0f:3.0f;
 float drive=*parameters.getRawParameterValue("drive")*.01f;
 const float ratios[]={4,8,12,20};int ri=juce::jlimit(0,3,(int)*parameters.getRawParameterValue("fetRatio"));
 float attack=*parameters.getRawParameterValue("attack")*.01f,release=*parameters.getRawParameterValue("fetRelease")*.01f;
 bool reverse=*parameters.getRawParameterValue("reverse")>.5f;
 for(int offset=0;offset<n;offset+=blockSize){
  int count=juce::jmin(blockSize,n-offset);
  for(int c=0;c<channels;++c){auto* dst=stageBuffer.getWritePointer(c);auto* src=buffer.getReadPointer(c,offset);for(int i=0;i<count;++i)dst[i]=src[i]*in;}
  juce::AudioBuffer<float> chunk(stageBuffer.getArrayOfWritePointers(),channels,count);
  if(reverse){processFET(chunk,count,drive,ratios[ri],attack,release);processOptical(chunk,count,{},peak,optoMakeup,opticalRatio,classic,fast,vintage);}
  else{processOptical(chunk,count,{},peak,optoMakeup,opticalRatio,classic,fast,vintage);processFET(chunk,count,drive,ratios[ri],attack,release);}
  for(int c=0;c<channels;++c){auto* dst=buffer.getWritePointer(c,offset);auto* src=chunk.getReadPointer(c);for(int i=0;i<count;++i)dst[i]=src[i]*out;}
 }
}
juce::AudioProcessorEditor* OptoFETProcessor::createEditor(){return new OptoFETEditor(*this);}
void OptoFETProcessor::getStateInformation(juce::MemoryBlock& b){auto xml=parameters.copyState().createXml();copyXmlToBinary(*xml,b);}
void OptoFETProcessor::setStateInformation(const void* data,int bytes){if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(parameters.state.getType()))parameters.replaceState(juce::ValueTree::fromXml(*xml));}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new OptoFETProcessor();}
