#include <cstdlib>
#include <iostream>
#include <algorithm>

#include <TVector3.h>
#include <TProfile.h>
#include <TObjArray.h>
#include <TF1.h>
#include <TTree.h>
#include <TFile.h>
#include <TFitResult.h>
#include <Math/Functor.h>

#include "TPCReco/GeometryTPC.h"

#include "TPCReco/TrackBuilder.h"
#include "TPCReco/colorText.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif // M_PI
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
TrackBuilder::TrackBuilder() {

  nAccumulatorRhoBins = 50;//FIX ME move to configuarable
  nAccumulatorPhiBins = 2.0*M_PI/0.025;//FIX ME move to configuarable
  nAccumulatorPhiBins = 2.0*M_PI/0.1;//FIX ME move to configuarable

  myHistoInitialized = false;
  myAccumulators.resize(3);
  my2DSeeds.resize(3);
  myRecHits.resize(3);
  myRawHits.resize(3);

  fitter.Config().MinimizerOptions().SetMinimizerType("Minuit2");
  fitter.Config().MinimizerOptions().SetMaxFunctionCalls(2000);
  fitter.Config().MinimizerOptions().Print(std::cout);
  fitter.Config().MinimizerOptions().SetPrintLevel(0);
  
  ///An offset used for filling the Hough transformation.
  ///to avoid having very small rho parameters, as
  ///orignally many tracks traverse close to X=0, Time=0
  ///point.
  aHoughOffest.SetX(50.0);
  aHoughOffest.SetY(50.0);
  aHoughOffest.SetZ(0.0);

  setPressure(myPressure);
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
TrackBuilder::~TrackBuilder() { }
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::setGeometry(std::shared_ptr<GeometryTPC> aGeometryPtr){
  
  myGeometryPtr = aGeometryPtr;
  myRecHitBuilder.setGeometry(aGeometryPtr);
  
  phiPitchDirection.resize(3);
  phiPitchDirection[definitions::projection_type::DIR_U] = myGeometryPtr->GetStripPitchVector(definitions::projection_type::DIR_U).Phi();
  phiPitchDirection[definitions::projection_type::DIR_V] = myGeometryPtr->GetStripPitchVector(definitions::projection_type::DIR_V).Phi();
  phiPitchDirection[definitions::projection_type::DIR_W] = myGeometryPtr->GetStripPitchVector(definitions::projection_type::DIR_W).Phi();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::setPressure(double aPressure) {

  myPressure = aPressure;
  mydEdxFitter.setPressure(myPressure);
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::setEvent(std::shared_ptr<EventTPC> aEvent){

  myEventPtr = aEvent;
  myFittedTrack = Track3D();
  
  std::string hName, hTitle;
  if(!myHistoInitialized){
    for(int iDir=definitions::projection_type::DIR_U;iDir<=definitions::projection_type::DIR_W;++iDir){
      std::shared_ptr<TH2D> hRawHits = myEventPtr->get2DProjection(get2DProjectionType(iDir),
								   filter_type::none,
								   scale_type::mm);
      double minX = hRawHits->GetXaxis()->GetXmin();
      double minY = hRawHits->GetYaxis()->GetXmin();
      double maxX = hRawHits->GetXaxis()->GetXmax();
      double maxY = hRawHits->GetYaxis()->GetXmax();
      double rho1 = sqrt( maxX*maxX + maxY*maxY);
      double rho2 = sqrt( minX*minX + maxY*maxY);
      double rho3 = sqrt( maxX*maxX + minY*minY);
      double rho4 = sqrt( minX*minX + minY*minY);
      double rhoMAX=rho1;
      double rhoMIN=rho1;
      if(rho1>rhoMAX) rhoMAX=rho1;
      if(rho2>rhoMAX) rhoMAX=rho2;
      if(rho3>rhoMAX) rhoMAX=rho3;
      if(rho4>rhoMAX) rhoMAX=rho4;
      if(rho1<rhoMIN) rhoMIN=rho1;
      if(rho2<rhoMIN) rhoMIN=rho2;
      if(rho3>rhoMIN) rhoMIN=rho3;
      if(rho4>rhoMIN) rhoMIN=rho4;
      // for rhoMIN: check if (0,0) is inside the rectangle [minX, maxX] x [minY, maxY]
      //  1 | 2 | 1
      // ---+---+---                   +-----+
      //  3 | 4 | 5                    |     |
      // ---+---+---       (0,0)+      +-----+
      //  1 | 6 | 1       
      if(minX<=0.0 && maxX>=0.0 && minY<=0.0 && maxY>=0.0) {
	rhoMIN=0.0; // case 4
      } else if(minX<0.0 && maxX<0.0 && minY<=0.0 && maxY>=0.0) {
	rhoMIN=fabs(maxX); // case 5
      } else if(minX>0.0 && maxX>0.0 && minY<=0.0 && maxY>=0.0) {
	rhoMIN=fabs(minX); // case 3
      } else if(minX<=0.0 && maxX>=0.0 && minY<0.0 && maxY<0.0) {
	rhoMIN=fabs(maxY); // case 6
      } else if(minX<=0.0 && maxX>=0.0 && minY>0.0 && maxY>0.0) {
	rhoMIN=fabs(minY); // case 2
      }     
      hName = "hAccumulator_"+std::to_string(iDir);
      hTitle = "Hough accumulator for direction: "+std::to_string(iDir)+";#theta;#rho";
      TH2D hAccumulator(hName.c_str(), hTitle.c_str(), nAccumulatorPhiBins,
			-M_PI, M_PI, nAccumulatorRhoBins, rhoMIN, rhoMAX);
      myAccumulators[iDir] = hAccumulator;
      myRawHits[iDir] = *hRawHits;
      if(iDir==definitions::projection_type::DIR_U) hTimeProjection = *hRawHits->ProjectionX();
    }
    myHistoInitialized = true;
  } 
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::reconstruct(){

  hTimeProjection.Reset();  
  for(int iDir=definitions::projection_type::DIR_U;iDir<=definitions::projection_type::DIR_W;++iDir){
    makeRecHits(iDir);
    fillHoughAccumulator(iDir);
    my2DSeeds[iDir] = findSegment2DCollection(iDir);    
  }
  myZRange = getTimeProjectionEdges();
  myTrack3DSeed = buildSegment3D();
  if(myTrack3DSeed.getLength()<1) return;
  
  Track3D aTrackCandidate;
  aTrackCandidate.addSegment(myTrack3DSeed);

  auto xyRange = myGeometryPtr->rangeXY();
  aTrackCandidate.extendToChamberRange(xyRange, myZRange);

  aTrackCandidate = fitTrack3D(aTrackCandidate);
  aTrackCandidate = fitEventHypothesis(aTrackCandidate);
  myFittedTrack = aTrackCandidate;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::makeRecHits(int iDir){

  std::shared_ptr<TH2D> hProj = myEventPtr->get2DProjection(get2DProjectionType(iDir),
							 filter_type::threshold,
							 scale_type::mm);
  myRecHits[iDir] = myRecHitBuilder.makeRecHits(*hProj);
  myRawHits[iDir] = myRecHitBuilder.makeCleanCluster(*hProj);
  hTimeProjection.Add(myRecHits[iDir].ProjectionX("hTimeProjection"));
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
std::tuple<double, double> TrackBuilder::getTimeProjectionEdges() const{
  
  int iBinStart = -1;
  int iBinEnd = -1;
  double histoSum = hTimeProjection.Integral();
  double sum = 0.0;
  double threshold = 0.01;

  for(auto iBin=0;iBin<hTimeProjection.GetNbinsX();++iBin){
    sum += hTimeProjection.GetBinContent(iBin); 
    if(sum/histoSum>threshold && iBinStart<0) iBinStart = iBin;
    else if(sum/histoSum>1.0-threshold && iBinEnd<0) {
      iBinEnd = iBin;
      break;
    }
  }
  
  double start = hTimeProjection.GetXaxis()->GetBinCenter(iBinStart-10);
  double end =  hTimeProjection.GetXaxis()->GetBinCenter(iBinEnd+10);  
  return std::make_tuple(start, end);
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const TH2D & TrackBuilder::getCluster2D(int iDir) const{

  return myRawHits[iDir];  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const TH2D & TrackBuilder::getRecHits2D(int iDir) const{

  return myRecHits[iDir];  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const TH1D & TrackBuilder::getRecHitsTimeProjection() const{

  return hTimeProjection;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const TH2D & TrackBuilder::getHoughtTransform(int iDir) const{

  return myAccumulators[iDir];  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const TrackSegment2D & TrackBuilder::getSegment2D(int iDir, unsigned int iTrack) const{

  if(my2DSeeds[iDir].size()<iTrack) return dummySegment2D;  
  return my2DSeeds[iDir].at(iTrack);  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const TrackSegment3D & TrackBuilder::getSegment3DSeed() const{

  return myTrack3DSeed; 
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
const Track3D & TrackBuilder::getTrack3D(unsigned int iSegment) const{

  return myFittedTrack;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::fillHoughAccumulator(int iDir){

  myAccumulators[iDir].Reset();
  
  const TH2D & hRecHits  = getRecHits2D(iDir);
  double maxCharge = hRecHits.GetMaximum();
  double theta = 0.0, rho = 0.0;
  double x = 0.0, y=0.0;
  int charge = 0;
  for(int iBinX=1;iBinX<hRecHits.GetNbinsX();++iBinX){
    for(int iBinY=1;iBinY<hRecHits.GetNbinsY();++iBinY){
      x = hRecHits.GetXaxis()->GetBinCenter(iBinX) + aHoughOffest.X();
      y = hRecHits.GetYaxis()->GetBinCenter(iBinY) + aHoughOffest.Y();
      charge = hRecHits.GetBinContent(iBinX, iBinY);
      if(charge<0.05*maxCharge) continue;
      for(int iBinTheta=1;iBinTheta<myAccumulators[iDir].GetNbinsX();++iBinTheta){
	theta = myAccumulators[iDir].GetXaxis()->GetBinCenter(iBinTheta);
	rho = x*cos(theta) + y*sin(theta);
	myAccumulators[iDir].Fill(theta, rho, charge);
      }
    }
  }
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
TrackSegment2DCollection TrackBuilder::findSegment2DCollection(int iDir){

  TrackSegment2DCollection aTrackCollection;
  int nPeaks = 1;
  for(int iPeak=0;iPeak<nPeaks;++iPeak){
    TrackSegment2D aTrackSegment = findSegment2D(iDir, iPeak);
    aTrackCollection.push_back(aTrackSegment);
  }
  return aTrackCollection;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
TrackSegment2D TrackBuilder::findSegment2D(int iDir, int iPeak) const{
  
  int iBinX = 0, iBinY = 0, iBinZ=0;
  int margin = 5;
  const TH2D & hAccumulator = myAccumulators[iDir];

  TH2D *hAccumulator_Clone = (TH2D*)hAccumulator.Clone("hAccumulator_clone");
  myAccumulators[iDir].GetMaximumBin(iBinX, iBinY, iBinZ);  
  for(int aPeak=0;aPeak<iPeak;++aPeak){
    for(int iDeltaX=-margin;iDeltaX<=margin;++iDeltaX){
      for(int iDeltaY=-margin;iDeltaY<=margin;++iDeltaY){
	hAccumulator_Clone->SetBinContent(iBinX+iDeltaX, iBinY+iDeltaY, 0,0);
      }
    }    
    hAccumulator_Clone->GetMaximumBin(iBinX, iBinY, iBinZ);
  }
  delete hAccumulator_Clone;
  
  TVector3 aTangent, aBias;
  int nHits = hAccumulator.GetBinContent(iBinX, iBinY);
  double theta = hAccumulator.GetXaxis()->GetBinCenter(iBinX);
  double rho = hAccumulator.GetYaxis()->GetBinCenter(iBinY);
  double aX = rho*cos(theta);
  double aY = rho*sin(theta);
  aBias.SetXYZ(aX, aY, 0.0);
  aBias -= aHoughOffest.Dot(aBias.Unit())*aBias.Unit();
  
  aX = -rho*sin(theta);
  aY = rho*cos(theta);
  aTangent.SetXYZ(aX, aY, 0.0);

  TrackSegment2D aSegment2D(iDir, myGeometryPtr);
  aSegment2D.setBiasTangent(aBias, aTangent);
  aSegment2D.setNAccumulatorHits(nHits);
  return aSegment2D;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void TrackBuilder::getSegment2DCollectionFromGUI(const std::vector<double> & segmentsXY){
  
  if(segmentsXY.size()%3 || (segmentsXY.size()/3)%4){
    std::cout<<KRED<<__FUNCTION__<<RST<<"Wrong number on segment endpoints: "
	     <<segmentsXY.size()<<std::endl;      
  }
  std::for_each(my2DSeeds.begin(), my2DSeeds.end(),
		[](TrackSegment2DCollection &item){item.resize(0);});

  Track3D aTrackCandidate;
  TVector3 aStart, aEnd;
  double x=0.0, y=0.0;
  int nSegments = segmentsXY.size()/3/4;
  for(int iSegment=0;iSegment<nSegments;++iSegment){
    for(int iDir = definitions::projection_type::DIR_U; iDir<=definitions::projection_type::DIR_W;++iDir){
      TrackSegment2D aSegment2D(iDir, myGeometryPtr);
      x = segmentsXY.at(iDir*4 + iSegment*12);
      y = segmentsXY.at(iDir*4 + iSegment*12 + 1);
      aStart.SetXYZ(x,y,0.0);
      x = segmentsXY.at(iDir*4 + iSegment*12 + 2);
      y = segmentsXY.at(iDir*4 + iSegment*12 + 3);
      aEnd.SetXYZ(x,y,0.0);
      aSegment2D.setStartEnd(aStart, aEnd);
      aSegment2D.setNAccumulatorHits(1);
      my2DSeeds[iDir].push_back(aSegment2D);
    }
    TrackSegment3D a3DSeed = buildSegment3D(iSegment);
    double startTime = my2DSeeds.at(definitions::projection_type::DIR_U).at(iSegment).getStart().X();    
    double endTime = my2DSeeds.at(definitions::projection_type::DIR_U).at(iSegment).getEnd().X();
    double lambdaStartTime = a3DSeed.getLambdaAtZ(startTime);
    double lambdaEndTime = a3DSeed.getLambdaAtZ(endTime);
    TVector3 start =  a3DSeed.getStart() + lambdaStartTime*a3DSeed.getTangent();
    TVector3 end =  a3DSeed.getStart() + lambdaEndTime*a3DSeed.getTangent();
    a3DSeed.setStartEnd(start, end);
    a3DSeed.setRecHits(myRawHits);
    aTrackCandidate.addSegment(a3DSeed);
  }
  std::cout<<KBLU<<"Hand cliked track: "<<RST<<std::endl;
  std::cout<<aTrackCandidate<<std::endl;
  
  myFittedTrack = aTrackCandidate;
  myFittedTrack.setHypothesisFitChi2(0);
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
TrackSegment3D TrackBuilder::buildSegment3D(int iTrack2DSeed) const{

  TrackSegment3D a3DSeed;
  a3DSeed.setGeometry(myGeometryPtr);  
	     
  const TrackSegment2D & segmentU = my2DSeeds[definitions::projection_type::DIR_U][iTrack2DSeed];
  const TrackSegment2D & segmentV = my2DSeeds[definitions::projection_type::DIR_V][iTrack2DSeed];
  const TrackSegment2D & segmentW = my2DSeeds[definitions::projection_type::DIR_W][iTrack2DSeed];

  int nHits_U = segmentU.getNAccumulatorHits();
  int nHits_V = segmentV.getNAccumulatorHits();
  int nHits_W = segmentW.getNAccumulatorHits();

  if(nHits_U*nHits_V*nHits_W==0) return a3DSeed;

  double bias_time = (segmentU.getMinBias().X()*(nHits_U>0) +
		      segmentV.getMinBias().X()*(nHits_V>0) +
		      segmentW.getMinBias().X()*(nHits_W>0))/((nHits_U>0) + (nHits_V>0) + (nHits_W>0));
  bias_time =  segmentV.getMinBias().X();
  TVector3 bias_U = segmentU.getBiasAtT(bias_time);
  TVector3 bias_V = segmentV.getBiasAtT(bias_time);
  TVector3 bias_W = segmentW.getBiasAtT(bias_time);
 
  TVector2 biasXY_fromUV;
  bool res1=myGeometryPtr->GetUVWCrossPointInMM(segmentU.getStripDir(), bias_U.Y(),
						segmentV.getStripDir(), bias_V.Y(),
						biasXY_fromUV);
  TVector2 biasXY_fromVW;
  bool res2=myGeometryPtr->GetUVWCrossPointInMM(segmentV.getStripDir(), bias_V.Y(),
						segmentW.getStripDir(), bias_W.Y(),
						biasXY_fromVW);
  TVector2 biasXY_fromWU;
  bool res3=myGeometryPtr->GetUVWCrossPointInMM(segmentW.getStripDir(), bias_W.Y(),
						segmentU.getStripDir(), bias_U.Y(),
						biasXY_fromWU);
  assert(res1|res2|res3);

  int weight_fromUV = res1*(nHits_U + nHits_V)*(nHits_U>0)*(nHits_W>0);
  int weight_fromVW = res2*(nHits_V + nHits_W)*(nHits_V>0)*(nHits_W>0);
  int weight_fromWU = res3*(nHits_W + nHits_U)*(nHits_W>0)*(nHits_U>0);
  int weight_sum = weight_fromUV+ weight_fromVW+weight_fromWU;
  const TVector2 biasXY=(weight_fromUV*biasXY_fromUV + weight_fromVW*biasXY_fromVW + weight_fromWU*biasXY_fromWU)/weight_sum;

  int weight_fromU = res1*nHits_U;
  int weight_fromV = res2*nHits_V;
  int weight_fromW = res3*nHits_W;
  weight_sum = weight_fromU+ weight_fromV+weight_fromW;

  double biasZ = (bias_U.X()*weight_fromU +  bias_V.X()*weight_fromV +  bias_W.X()*weight_fromW)/weight_sum;  
  TVector3 aBias( biasXY.X(), biasXY.Y(), biasZ);

  TVector3 tangent_U = segmentU.getNormalisedTangent();
  TVector3 tangent_V = segmentV.getNormalisedTangent();
  TVector3 tangent_W = segmentW.getNormalisedTangent();
  
  if(std::abs(tangent_U.Y())>1E-3 && 
     tangent_U.Y()*tangent_V.Y()>0 &&
     tangent_U.Y()*tangent_W.Y()>0){
    tangent_U.SetY(-tangent_U.Y());
    }
  
  TVector2 tangentXY_fromUV;
  bool res4=myGeometryPtr->GetUVWCrossPointInMM(segmentU.getStripDir(), tangent_U.Y(),
						segmentV.getStripDir(), tangent_V.Y(),
						tangentXY_fromUV);
  TVector2 tangentXY_fromVW;
  bool res5=myGeometryPtr->GetUVWCrossPointInMM(segmentV.getStripDir(), tangent_V.Y(),
						segmentW.getStripDir(), tangent_W.Y(),
						tangentXY_fromVW);
  TVector2 tangentXY_fromWU;
  bool res6=myGeometryPtr->GetUVWCrossPointInMM(segmentW.getStripDir(), tangent_W.Y(),
						segmentU.getStripDir(), tangent_U.Y(),
						tangentXY_fromWU);
  assert(res4|res5|res6);

  weight_fromUV = res4*(nHits_U + nHits_V)*(nHits_U>0)*(nHits_V>0);
  weight_fromVW = res5*(nHits_V + nHits_W)*(nHits_V>0)*(nHits_W>0);
  weight_fromWU = res6*(nHits_W + nHits_U)*(nHits_W>0)*(nHits_U>0);
  
  TVector2 tangentXY=(weight_fromUV*tangentXY_fromUV + weight_fromVW*tangentXY_fromVW + weight_fromWU*tangentXY_fromWU)/(weight_fromUV+weight_fromVW+weight_fromWU);
  double tangentZ_fromU = tangent_U.X();
  double tangentZ_fromV = tangent_V.X();
  double tangentZ_fromW = tangent_W.X();
  double tangentZ = (tangentZ_fromU*nHits_U + tangentZ_fromV*nHits_V + tangentZ_fromW*nHits_W)/(nHits_U+nHits_V+nHits_W);
  TVector3 aTangent(tangentXY.X(), tangentXY.Y(), tangentZ);

   // TEST
  /*
  std::cout<<KRED<<__FUNCTION__<<RST
	   <<" nHits_fromU: "<<nHits_U
    	   <<" nHits_fromV: "<<nHits_V
    	   <<" nHits_fromW: "<<nHits_W
	   <<std::endl;
  
  std::cout<<KRED<<" tangent U: "<<RST;
  tangent_U.Print();
  std::cout<<KRED<<" tangent V: "<<RST;
  tangent_V.Print();
  std::cout<<KRED<<" tangent W: "<<RST;
  tangent_W.Print();
  std::cout<<KRED<<" from UV: "<<RST;
  tangentXY_fromUV.Print();
  std::cout<<KRED<<" from VW: "<<RST;
  tangentXY_fromVW.Print();
  std::cout<<KRED<<" from WU: "<<RST;
  tangentXY_fromWU.Print();
  std::cout<<KRED<<" from average: "<<RST;
  tangentXY.Print();
  std::cout<<KRED<<" bias U: "<<RST;
  bias_U.Print();
  std::cout<<KRED<<" bias V: "<<RST;
  bias_V.Print();
  std::cout<<KRED<<" bias W: "<<RST;
  bias_W.Print();
  std::cout<<KRED<<"bias from average: "<<RST;
  aBias.Print();
  ////////
  //aTangent.SetMagThetaPhi(1, 1.52218, 1.34371);
  //aTangent.SetXYZ(0.318140,-0.948024,0.006087);
  */

  a3DSeed.setBiasTangent(aBias, aTangent);
  a3DSeed.setRecHits(myRecHits);
  /*
  std::cout<<KRED<<"tagent from track: "<<RST;
  a3DSeed.getTangent().Print();
  
  std::cout<<KRED<<"tagent from track, U proj.: "<<RST;
  a3DSeed.get2DProjection(definitions::projection_type::DIR_U, 0, 1).getTangent().Print();

  std::cout<<KRED<<"tagent from track, V proj.: "<<RST;
  a3DSeed.get2DProjection(definitions::projection_type::DIR_V, 0, 1).getTangent().Print();

  std::cout<<KRED<<"tagent from track, W proj.: "<<RST;
  a3DSeed.get2DProjection(definitions::projection_type::DIR_W, 0, 1).getTangent().Print();

  std::cout<<KRED<<"bias from track: "<<RST;
  a3DSeed.getBias().Print();
  */
  
  return a3DSeed;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
Track3D TrackBuilder::fitTrack3D(const Track3D & aTrackCandidate){

  Track3D aFittedTrack = aTrackCandidate;  
  std::cout<<KBLU<<"Pre-fit: "<<RST<<std::endl; 
  std::cout<<aFittedTrack<<std::endl;
  
  int nOffsets = 3;
  double offset = 0.0;
  double candidateChi2 = aTrackCandidate.getChi2();
  ROOT::Fit::FitResult bestFitResult;
  for(int iOffset=-nOffsets;iOffset<=nOffsets;++iOffset){
    if(iOffset==0) offset = aTrackCandidate.getSegments().front().getTangent().Phi();
    else offset = iOffset*M_PI/6.0;    
    auto fitResult = fitTrackNodesBiasTangent(aTrackCandidate, offset);
    if(fitResult.IsValid() &&
       (fitResult.MinFcnValue()<bestFitResult.MinFcnValue() || bestFitResult.IsEmpty()) ){
      bestFitResult = fitResult;
    }
  }
  if(bestFitResult.IsValid() && bestFitResult.MinFcnValue()<candidateChi2){
    aFittedTrack.setFitMode(Track3D::FIT_BIAS_TANGENT);
    aFittedTrack.chi2FromNodesList(bestFitResult.GetParams());
  }
  aFittedTrack.getSegments().front().setRecHits(myRawHits);

  //auto xyRange = myGeometryPtr->rangeXY(); TH2Poly axis ranges returns too small values
  auto xyRange = std::make_tuple(-99, 99, -165, 165);
  aFittedTrack.extendToChamberRange(xyRange, myZRange);
  aFittedTrack.shrinkToHits();
  
  std::cout<<KBLU<<"Post-fit: "<<RST<<std::endl;
  std::cout<<aFittedTrack<<std::endl;
  return aFittedTrack;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
ROOT::Fit::FitResult TrackBuilder::fitTrackNodesBiasTangent(const Track3D & aTrack, double offset) const{

  Track3D aTrackCandidate = aTrack;
  aTrackCandidate.setFitMode(Track3D::FIT_BIAS_TANGENT);
  std::vector<double> params = aTrackCandidate.getSegmentsBiasTangentCoords();
  int nParams = params.size();
  params[4] = offset;
  if(params[4]>M_PI) params[4] -= 2*M_PI;
  if(params[4]<-M_PI) params[4] += 2*M_PI;
  
  ROOT::Math::Functor fcn(&aTrackCandidate, &Track3D::chi2FromNodesList, nParams);
  fitter.SetFCN(fcn, params.data());

  for (int iPar = 0; iPar < nParams; ++iPar){
    fitter.Config().ParSettings(iPar).SetValue(params[iPar]);
    if(iPar<3){//bias coordinates
      fitter.Config().ParSettings(iPar).SetStepSize(1);
      fitter.Config().ParSettings(iPar).SetLimits(-300,300);
    }
    if(iPar==3){ //tangent polar angle
      fitter.Config().ParSettings(iPar).SetStepSize(1);
      fitter.Config().ParSettings(iPar).SetLimits(0, M_PI);
    }
    if(iPar==4){ //tangent azimuthal angle 
      fitter.Config().ParSettings(iPar).SetStepSize(1);
      fitter.Config().ParSettings(iPar).SetLimits(-M_PI, M_PI);
    }
  }
  for(int iIter=0;iIter<4;++iIter){
    if(iIter%2==0){
      fitter.Config().ParSettings(0).Release();
      fitter.Config().ParSettings(1).Release();
      fitter.Config().ParSettings(2).Release();
      fitter.Config().ParSettings(3).Fix();
      fitter.Config().ParSettings(4).Fix();
    }
    else if(iIter%2==1){
      fitter.Config().ParSettings(0).Fix();
      fitter.Config().ParSettings(1).Fix();
      fitter.Config().ParSettings(2).Fix();
      fitter.Config().ParSettings(3).Release();
      fitter.Config().ParSettings(4).Release();
    }
    
    bool fitStatus = fitter.FitFCN();
    if (!fitStatus) {
      Error(__FUNCTION__, "Track3D Fit failed");
      std::cout<<KRED<<"Track3D Fit failed."<<RST
	       <<" iteration "<<iIter
	       <<" phi offset "<<offset
	       <<RST<<std::endl;
      //fitter.Result().Print(std::cout);
      return fitter.Result();
    }
  }
  return fitter.Result();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
Track3D TrackBuilder::fitTrackNodesStartEnd(const Track3D & aTrack) const{

  Track3D aTrackCandidate = aTrack;
  aTrackCandidate.setFitMode(Track3D::FIT_START_STOP);
  std::vector<double> params = aTrackCandidate.getSegmentsStartEndXYZ();
  int nParams = params.size();
  
  ROOT::Math::Functor fcn(&aTrackCandidate, &Track3D::chi2FromNodesList, nParams);
  fitter.SetFCN(fcn, params.data());
  
  //double paramWindowWidth = 10.0;
  for (int iPar = 0; iPar < nParams; ++iPar){
    fitter.Config().ParSettings(iPar).Release();
    fitter.Config().ParSettings(iPar).SetValue(params[iPar]);
    //fitter.Config().ParSettings(iPar).SetLimits(params[iPar]-paramWindowWidth,
    //						params[iPar]+paramWindowWidth);
  }      
  bool fitStatus = fitter.FitFCN();
  if (!fitStatus) {
    Error(__FUNCTION__, "Track3D Fit failed");
    std::cout<<KRED<<"Track3D nodes fit failed"<<RST<<std::endl;
    fitter.Result().Print(std::cout);
    //return aTrack;
  }
  const ROOT::Fit::FitResult & result = fitter.Result();
  aTrackCandidate.chi2FromNodesList(result.GetParams());

  std::cout<<KBLU<<"Post-refit: "<<RST<<std::endl;
  std::cout<<aTrackCandidate<<std::endl;
  
  return aTrackCandidate;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
Track3D TrackBuilder::fitEventHypothesis(const Track3D & aTrackCandidate){

  /*
  return aTrackCandidate;
  ////TEST
  TVector3 a(0,0,0); 
  //TVector3 b(0, 0 , 60); //theta = 0
  //TVector3 b(60, 0, 3.67394e-15); //theta = M_PI/2, phi = 0
  TVector3 b(42.4264, 42.4264, 3.67394e-15); //theta = M_PI/4, phi = M_PI/4

  Track3D aCandidate1;
  TrackSegment3D aSegment1;
  aSegment1.setGeometry(myGeometryPtr);  
  aSegment1.setStartEnd(a, b);
  aSegment1.setRecHits(myRawHits);
  aSegment1.setPID(pid_type::ALPHA);
  aCandidate1.addSegment(aSegment1);
  return aCandidate1;
  ////////
  */
  if(aTrackCandidate.getLength()<1) return aTrackCandidate;

  const TrackSegment3D & aSegment = aTrackCandidate.getSegments().front(); 
  TH1F hChargeProfile = aSegment.getChargeProfile(); 
  mydEdxFitter.fitHisto(hChargeProfile);

  pid_type eventType = mydEdxFitter.getBestFitEventType();
  bool isReflected = mydEdxFitter.getIsReflected();
  double vertexOffset = mydEdxFitter.getVertexOffset();
  double alphaRange = mydEdxFitter.getAlphaRange();
  double carbonRange = mydEdxFitter.getCarbonRange();

  TVector3 tangent =  aSegment.getTangent();
  TVector3 start =  aSegment.getStart();
  
  if(isReflected){
    tangent *=-1;
    start = aSegment.getEnd();
  }

  TVector3 vertexPos =  start + vertexOffset*tangent;
  TVector3 alphaEnd =  vertexPos + alphaRange*tangent;
  TVector3 carbonEnd =  vertexPos - carbonRange*tangent;

  Track3D aSplitTrackCandidate;
  aSplitTrackCandidate.setChargeProfile(mydEdxFitter.getFittedHisto());
  aSplitTrackCandidate.setHypothesisFitChi2(mydEdxFitter.getChi2());
  TrackSegment3D alphaSegment;
  alphaSegment.setGeometry(myGeometryPtr);  
  alphaSegment.setStartEnd(vertexPos, alphaEnd);
  alphaSegment.setRecHits(myRawHits);
  alphaSegment.setPID(pid_type::ALPHA);
  aSplitTrackCandidate.addSegment(alphaSegment);
  
  if(eventType==C12_ALPHA){   
    TrackSegment3D carbonSegment;
    carbonSegment.setGeometry(myGeometryPtr);  
    carbonSegment.setStartEnd(vertexPos, carbonEnd);
    carbonSegment.setRecHits(myRawHits);
    carbonSegment.setPID(pid_type::CARBON_12);
    aSplitTrackCandidate.addSegment(carbonSegment);
  }

  std::cout<<KBLU<<"Post-split: "<<RST<<std::endl;
  std::cout<<aSplitTrackCandidate<<std::endl;
  return aSplitTrackCandidate;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
