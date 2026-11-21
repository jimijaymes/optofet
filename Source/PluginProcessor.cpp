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
 p.add(std::make_unique<B>(juce::ParameterID{"optoEnabled",1},"Optical Enabled",true));
 p.add(std::make_unique<B>(juce::ParameterID{"fetEnabled",1},"FET Enabled",true));
 p.add(std::make_unique<F>(juce::ParameterID{"sidechainHPF",1},"Master Sidechain HPF (Hz)",juce::NormalisableRange<float>(20.f,400.f,0.f,0.35f),90.f));
 p.add(std::make_unique<F>(juce::ParameterID{"mix",1},"Wet Mix (%)",juce::NormalisableRange<float>(0,100,0.1f),100));
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
 p.add(std::make_unique<C>(juce::ParameterID{"fetTiming",1},"FET Timing Mode",juce::StringArray{"1176 Classic","Extended"},0));
 p.add(std::make_unique<B>(juce::ParameterID{"reverse",1},"1176 First",false));
 p.add(std::make_unique<C>(juce::ParameterID{"oversampling",1},"Oversampling",juce::StringArray{"Off (1x)","2x","4x","8x"},2));
 return p;
}
OptoFETProcessor::OptoFETProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"STATE",layout()){}
void OptoFETProcessor::prepareToPlay(double sampleRate,int maximumExpectedSamplesPerBlock){
 sr=sampleRate;blockSize=juce::jmax(1,maximumExpectedSamplesPerBlock);
 opto={};fet={};detectorHPx={};detectorHPy={};orderBlend=vintageBlend=0;
 stageBuffer.setSize(2,blockSize);
 for(auto& ring:dryDelay)ring.assign(16384,0.f);
 dryWrite=0;
 interstageLP1={};interstageLP2={};activeOversampling=-1;
 for(int i=0;i<3;++i){
  oversamplers[(size_t)i]=std::make_unique<juce::dsp::Oversampling<float>>(
   2,(size_t)(i+1),juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,true,true);
  oversamplers[(size_t)i]->initProcessing((size_t)blockSize);
  oversamplers[(size_t)i]->reset();
 }
 setLatencySamples(0);
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
   const float hpA=1.0f/(1.0f+2.0f*juce::MathConstants<float>::pi*sidechainHz/(float)sr);
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
   // LA-2A-style valve line amplifier: cathode recovery and asymmetric
   // grid bias, separate from the input transformer's LF nonlinearity.
   const float cathodePole=pole(40,sr),biasPole=pole(12,sr);
   s.tubeCathode=cathodePole*s.tubeCathode+(1-cathodePole)*std::abs(wet);
   s.tubeBias=biasPole*s.tubeBias+(1-biasPole)*juce::jmax(0.f,wet-.35f);
   const float drive=1.23f,bias=.012f+.025f*s.tubeCathode-.018f*s.tubeBias;
   const float z=wet*drive+bias;
   const float nonlinear=soft(z)-.008f*soft(z*z);
   const float baseline=soft(bias)-.008f*soft(bias*bias);
   wet=(nonlinear-baseline)/(drive*juce::jmax(.2f,1-soft(bias)*soft(bias)));
   // LA-2A-style output iron after the valve amplifier.
   const float outBassPole=pole(125,sr),outFluxPole=pole(28,sr),outTreblePole=pole(10500,sr);
   s.outputBass=outBassPole*s.outputBass+(1-outBassPole)*wet;
   s.outputFlux=outFluxPole*s.outputFlux+(1-outFluxPole)*s.outputBass;
   const float outDrive=1.45f*(1+.3f*juce::jmin(2.f,std::abs(s.outputFlux)));
   wet+=.16f*(soft(outDrive*s.outputBass)/outDrive-s.outputBass);
   s.outputTreble=outTreblePole*s.outputTreble+(1-outTreblePole)*wet;
   wet-=.018f*juce::jmin(1.f,std::abs(s.outputFlux))*(wet-s.outputTreble);
   s.highCut=vintagePole*s.highCut+(1-vintagePole)*wet;
   wet+=vintageBlend*(s.highCut-wet);
   b.setSample(c,i,wet*db(gain));
  }
 }
 opticalGR.store(avgGR/juce::jmax(1,n*b.getNumChannels()),std::memory_order_relaxed);
}
void OptoFETProcessor::processFET(juce::AudioBuffer<float>& b,int n,float drive,float ratio,float attack,float release){
 // One-knob 1176-style input: drive is ahead of the detector and FET gain
 // cell. Linked output trim cancels the added input gain, NOT the gain
 // reduction itself; a compressor should still audibly compress harder.
 const float inputDb=drive*36.0f;
 const float pre=db(inputDb),post=db(-inputDb);
 const bool extended=*parameters.getRawParameterValue("fetTiming")>.5f;
 // Classic: 20-800 us attack and 50-1100 ms release.
 // Extended: 20 us-30 ms attack and 25-1650 ms release.
 // Clockwise remains faster for both knobs in either mode.
 const float slowAttack=extended?.030f:.0008f;
 const float fastRelease=extended?.025f:.050f;
 const float slowRelease=extended?1.650f:1.100f;
 float attackSec=slowAttack*std::pow(.000020f/slowAttack,attack);
 float releaseSec=fastRelease*std::pow(slowRelease/fastRelease,1.0f-release);
 float ac=timeCoeff(attackSec,sr),rc=timeCoeff(releaseSec,sr);
 // Slow, bounded automatic output makeup tracks a portion of gain reduction.
 // Preserve transient compression; avoid following the fast FET envelope.
 const float makeupRise=timeCoeff(.250f,sr),makeupFall=timeCoeff(.650f,sr);
 // No signal-following RMS servo: it previously restored the gain
 // reduction and made increasing input seem ineffective.
 float transformerPole=pole(110,sr),ampPole=pole(8500,sr);
 float sum=0;
 for(int i=0;i<n;++i)for(int c=0;c<b.getNumChannels();++c){
  auto& s=fet[(size_t)c];float x=b.getSample(c,i),hot=x*pre;
  s.transformerLow=transformerPole*s.transformerLow+(1-transformerPole)*hot;
  // Early 1176 UTC O-12 input transformer approximation: flux-dependent
  // LF limiting and a separate loading-dependent HF response.
  const float fluxPole=pole(35,sr),hfPole=pole(12000,sr);
  s.transformerFlux=fluxPole*s.transformerFlux+(1-fluxPole)*s.transformerLow;
  const float coreDrive=1.6f*(1+.25f*juce::jmin(2.f,std::abs(s.transformerFlux)));
  float magnetic=hot+.10f*(soft(coreDrive*s.transformerLow)/coreDrive-s.transformerLow);
  s.transformerTreble=hfPole*s.transformerTreble+(1-hfPole)*magnetic;
  magnetic-=.015f*juce::jmin(1.f,std::abs(s.transformerFlux))*(magnetic-s.transformerTreble);
  // A single rectified peak detector controls the gain cell. The previous
  // version applied the full attack/release envelope twice, slowing transients.
  // The control maps 800->20 us attack and 1100->50 ms release (CW faster).
  // Independent detector-only high-pass: never filters the audible FET path.
  const float hpA=1.f/(1.f+2.f*juce::MathConstants<float>::pi*sidechainHz/(float)sr);
  const float sc=hpA*(s.scHPy+magnetic-s.scHPx);
  s.scHPx=magnetic;s.scHPy=sc;
  const float a=std::abs(sc);
  const float ec=a>s.envelope?ac:rc;
  s.envelope=ec*s.envelope+(1.0f-ec)*a;
  const float levelDb=juce::Decibels::gainToDecibels(juce::jmax(s.envelope,1e-8f),-160.0f);
  // Fixed threshold: input Drive increases the detector level and gain reduction.
  const float target=softKnee(levelDb+18.0f,ratio,5.0f);
  s.gr=target;
  const float makeupTarget=juce::jlimit(0.f,12.f,target*.60f);
  const float makeupCoeff=makeupTarget>s.makeupDb?makeupRise:makeupFall;
  s.makeupDb=makeupCoeff*s.makeupDb+(1.f-makeupCoeff)*makeupTarget;
  const float attenuated=magnetic*db(-s.gr);
  // Subtle Rev D-inspired amplifier saturation, stronger with drive.
  const float ampDrive=1.0f+0.65f*drive;
  // Rev-D-inspired Class A output amplifier: asymmetric headroom,
  // LF-dependent bias recovery and transformer-related HF damping.
  const float biasPole=pole(24,sr);
  s.ampBias=biasPole*s.ampBias+(1-biasPole)*std::abs(attenuated);
  const float bias=.055f*drive*s.ampBias;
  const float z=attenuated*ampDrive+bias;
  const float nonlinear=(soft(z)-soft(bias))/
      (ampDrive*juce::jmax(.2f,1-soft(bias)*soft(bias)));
  s.ampMemory=ampPole*s.ampMemory+(1-ampPole)*nonlinear;
  float coloured=nonlinear-.05f*drive*(nonlinear-s.ampMemory);
  // Early 1176 Class A output transformer, after the amplifier;
  // deliberately distinct from the UTC input transformer's behaviour.
  const float outBassPole=pole(155,sr),outFluxPole=pole(43,sr);
  const float outTreblePole=pole(12500,sr);
  s.outputBass=outBassPole*s.outputBass+(1-outBassPole)*coloured;
  s.outputFlux=outFluxPole*s.outputFlux+(1-outFluxPole)*s.outputBass;
  const float outDrive=1.55f*(1+.35f*juce::jmin(2.f,std::abs(s.outputFlux)));
  coloured+=.12f*(soft(outDrive*s.outputBass)/outDrive-s.outputBass);
  s.outputTreble=outTreblePole*s.outputTreble+(1-outTreblePole)*coloured;
  coloured-=.02f*juce::jmin(1.f,std::abs(s.outputFlux))*(coloured-s.outputTreble);
  // Static linked output trim maintains the nominal uncompressed level;
  // actual loudness drops as gain reduction increases, as on hardware.
  b.setSample(c,i,coloured*post*db(s.makeupDb));sum+=s.gr;
 }
 fetGR.store(sum/juce::jmax(1,n*b.getNumChannels()),std::memory_order_relaxed);
}
void OptoFETProcessor::filterInterstage(juce::AudioBuffer<float>& b,int n){
 // Two cascaded one-pole low-pass sections, nominal 20 kHz.
 // Clamp below Nyquist in 1x mode. The filter reduces interstage HF,
 // but is NOT a substitute for oversampling's anti-alias decimator.
 const float cutoff=juce::jmin(20000.0f,0.45f*(float)sr);
 const float a=pole(cutoff,sr);
 for(int c=0;c<b.getNumChannels();++c){
  auto& s1=interstageLP1[(size_t)c];
  auto& s2=interstageLP2[(size_t)c];
  float* x=b.getWritePointer(c);
  for(int i=0;i<n;++i){
   s1=a*s1+(1.0f-a)*x[i];
   s2=a*s2+(1.0f-a)*s1;
   x[i]=s2;
  }
 }
}
void OptoFETProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){
 juce::ScopedNoDenormals noDenormals;
 const int channels=juce::jmin(2,buffer.getNumChannels()),n=buffer.getNumSamples();
 if(channels==0||n==0)return;
 for(int c=channels;c<buffer.getNumChannels();++c)buffer.clear(c,0,n);
 float in=db(*parameters.getRawParameterValue("input")),out=db(*parameters.getRawParameterValue("output"));
 const float mix=juce::jlimit(0.f,1.f,*parameters.getRawParameterValue("mix")*.01f);
 const bool optoOn=*parameters.getRawParameterValue("optoEnabled")>.5f;
 const bool fetOn=*parameters.getRawParameterValue("fetEnabled")>.5f;
 sidechainHz=*parameters.getRawParameterValue("sidechainHPF");
 float peak=*parameters.getRawParameterValue("peak"),optoMakeup=*parameters.getRawParameterValue("optoGain");
 bool classic=*parameters.getRawParameterValue("response")>.5f,fast=*parameters.getRawParameterValue("release")>.5f,vintage=*parameters.getRawParameterValue("vintage")>.5f;
 float opticalRatio=*parameters.getRawParameterValue("optoRatio")>.5f?20.0f:3.0f;
 float drive=*parameters.getRawParameterValue("drive")*.01f;
 const float ratios[]={4,8,12,20};int ri=juce::jlimit(0,3,(int)*parameters.getRawParameterValue("fetRatio"));
 float attack=*parameters.getRawParameterValue("attack")*.01f,release=*parameters.getRawParameterValue("fetRelease")*.01f;
 bool reverse=*parameters.getRawParameterValue("reverse")>.5f;
 const int osChoice=juce::jlimit(0,3,(int)*parameters.getRawParameterValue("oversampling"));
 // Mode changes can change latency; the host is notified when necessary.
 if(osChoice!=activeOversampling){
  activeOversampling=osChoice;
  setLatencySamples(osChoice==0?0:
   (int)std::round(oversamplers[(size_t)(osChoice-1)]->getLatencyInSamples()));
 }
 const double hostRate=sr;
 const int dryLatency=osChoice==0?0:(int)std::round(oversamplers[(size_t)(osChoice-1)]->getLatencyInSamples());
 const int ringSize=(int)dryDelay[0].size();
 for(int offset=0;offset<n;offset+=blockSize){
  const int count=juce::jmin(blockSize,n-offset);
  for(int c=0;c<channels;++c){
   auto* dst=stageBuffer.getWritePointer(c);
   const auto* src=buffer.getReadPointer(c,offset);
   for(int i=0;i<count;++i)dst[i]=src[i]*in;
  }
  juce::dsp::AudioBlock<float> base(stageBuffer);
  auto slice=base.getSubBlock(0,(size_t)count);
  const int factor=1<<osChoice;
  juce::dsp::AudioBlock<float> up=slice;
  if(osChoice>0)up=oversamplers[(size_t)(osChoice-1)]->processSamplesUp(slice);
  const int upCount=(int)up.getNumSamples();
  // AudioBlock exposes per-channel pointers, not getArrayOfChannelPointers().
  float* upChannels[2] = { up.getChannelPointer(0), channels > 1 ? up.getChannelPointer(1) : nullptr };
  juce::AudioBuffer<float> chunk(upChannels,channels,upCount);
  sr=hostRate*(double)factor;
  if(reverse){
   if(fetOn)processFET(chunk,upCount,drive,ratios[ri],attack,release);
   if(optoOn&&fetOn)filterInterstage(chunk,upCount);
   if(optoOn)processOptical(chunk,upCount,{},peak,optoMakeup,opticalRatio,classic,fast,vintage);
  }else{
   if(optoOn)processOptical(chunk,upCount,{},peak,optoMakeup,opticalRatio,classic,fast,vintage);
   if(optoOn&&fetOn)filterInterstage(chunk,upCount);
   if(fetOn)processFET(chunk,upCount,drive,ratios[ri],attack,release);
  }
  if(!optoOn)opticalGR.store(0.f,std::memory_order_relaxed);
  if(!fetOn)fetGR.store(0.f,std::memory_order_relaxed);
  sr=hostRate;
  if(osChoice>0)oversamplers[(size_t)(osChoice-1)]->processSamplesDown(slice);
  for(int c=0;c<channels;++c){
   auto* dst=buffer.getWritePointer(c,offset);
   const auto* src=stageBuffer.getReadPointer(c);
   for(int i=0;i<count;++i){
    const float dryInput=buffer.getReadPointer(c,offset)[i]*in;
    auto& ring=dryDelay[(size_t)c];
    const int write=(dryWrite+i)%ringSize;
    const int read=(write+ringSize-juce::jmin(dryLatency,ringSize-1))%ringSize;
    const float dry=dryLatency==0?dryInput:ring[(size_t)read];
    ring[(size_t)write]=dryInput;
    dst[i]=(src[i]*mix+dry*(1.f-mix))*out;
   }
  }
  dryWrite=(dryWrite+count)%ringSize;
 }
}
juce::AudioProcessorEditor* OptoFETProcessor::createEditor(){return new OptoFETEditor(*this);}
void OptoFETProcessor::getStateInformation(juce::MemoryBlock& b){auto xml=parameters.copyState().createXml();copyXmlToBinary(*xml,b);}
void OptoFETProcessor::setStateInformation(const void* data,int bytes){if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(parameters.state.getType()))parameters.replaceState(juce::ValueTree::fromXml(*xml));}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new OptoFETProcessor();}
