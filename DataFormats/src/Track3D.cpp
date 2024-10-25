#include <iostream>
#include <algorithm>

#include "TPCReco/Track3D.h"
#include "TPCReco/GeometryTPC.h"
#include "TPCReco/colorText.h"

#include <Fit/Fitter.h>
#include <Math/Functor.h>
#include <Math/GenVector/VectorUtil.h>

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
Track3D::Track3D(){ };
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::addSegment(const TrackSegment3D & aSegment3D){

  mySegments.push_back(aSegment3D);
  update();

}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::update(){

  myLenght = 0.0;
  for(auto &aSegment: mySegments){
    myLenght += aSegment.getLength();
  }
  updateChi2();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
std::vector<double> Track3D::getSegmentsStartEndXYZ() const{

  std::vector<double> coordinates;
  if(!mySegments.size()) return coordinates;

  std::vector<double> segmentCoordinates = mySegments.front().getStartEndXYZ();
  coordinates.insert(coordinates.end(), segmentCoordinates.begin(), segmentCoordinates.begin()+3);
  
  for(auto &aSegment: mySegments){
    std::vector<double> segmentCoordinates = aSegment.getStartEndXYZ();
    coordinates.insert(coordinates.end(), segmentCoordinates.begin()+3, segmentCoordinates.begin()+6);
  }

  return coordinates;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
std::vector<double> Track3D::getSegmentsBiasTangentCoords() const{

  std::vector<double> coordinates;
  if(!mySegments.size()) return coordinates;

  for(auto &aSegment: mySegments){
    std::vector<double> segmentCoordinates = aSegment.getBiasTangentCoords();
    coordinates.insert(coordinates.end(), segmentCoordinates.begin(), segmentCoordinates.end());
  }

  return coordinates;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
double Track3D::getIntegratedCharge(double lambda) const{
  double charge = 0.0;
  double pastSegmentsLambda = 0.0;
  double segmentLambda = 0.0;
  for(const auto & aSegment: mySegments){
    segmentLambda = lambda - pastSegmentsLambda;
    if(segmentLambda>aSegment.getLength()) segmentLambda = aSegment.getLength();
    charge += aSegment.getIntegratedCharge(segmentLambda);
    pastSegmentsLambda += aSegment.getLength();
    if(segmentLambda<aSegment.getLength()) break;
  }
  return charge;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::enableProjectionForChi2(int iProjection){

  iProjectionForChi2 = iProjection;
  updateChi2();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
double Track3D::getChi2() const{

  double chi2 = 0.0;
  chi2 += getSegmentsChi2();
  return chi2;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
double Track3D::getSegmentsChi2() const{

  double chi2 = 0.0;
  std::for_each(segmentChi2.begin(), segmentChi2.end(), [&](auto aItem){chi2 += aItem;});
  return chi2;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::updateChi2(){

  segmentChi2.clear();
  for(auto aItem: mySegments){
    segmentChi2.push_back(aItem.getRecHitChi2(iProjectionForChi2));
  }
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
std::tuple<double, double> Track3D::getLambdaOnSphere(const TrackSegment3D & aSegment, double r) const{

   double phi = aSegment.getTangent().Phi();
   double theta = aSegment.getTangent().Theta();
   double r0Square = aSegment.getStart().Mag2();
   double x0 = aSegment.getStart().X();
   double y0 = aSegment.getStart().Y();
   double z0 = aSegment.getStart().Z();

   double a = 1;
   double b = 2*(x0*cos(phi)*sin(theta) + y0*sin(phi)*sin(theta) + z0*cos(theta));
   double c = r0Square - r*r;
   double delta = b*b - 4*a*c;
   double lambda1 = (-b - sqrt(delta))/(2*a);
   double lambda2 = (-b + sqrt(delta))/(2*a);
   return std::make_tuple(lambda1, lambda2);
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::extendToChamberRange(double r){

  if(!mySegments.size()) return;
  double lambda1{0.0}, lambda2{0.0};  

  TrackSegment3D & aFirstSegment = mySegments.front();
  std::tie(lambda1, lambda2) = getLambdaOnSphere(aFirstSegment, r);
   std::cout<<"lambda1: "<<lambda1<<" lambda2: "<<lambda2<<std::endl;

   TVector3 aStart = aFirstSegment.getStart() + lambda2*aFirstSegment.getTangent();
   TVector3 aEnd = aFirstSegment.getEnd();
   aFirstSegment.setStartEnd(aStart, aEnd);
   /////       
   TrackSegment3D & aLastSegment = mySegments.back();
   double segmentLength = aLastSegment.getLength();
   std::tie(lambda1, lambda2) = getLambdaOnSphere(aLastSegment, r);
    std::cout<<"lambda1: "<<lambda1<<" lambda2: "<<lambda2<<std::endl;

   aStart = aLastSegment.getStart();
   aEnd = aStart + (lambda1 - segmentLength)*aLastSegment.getTangent();
   aLastSegment.setStartEnd(aStart, aEnd);
   update();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::shrinkToHits(){

  if(getLength()<1.0) return;

  TrackSegment3D & aFirstSegment = mySegments.front();
  TrackSegment3D & aLastSegment = mySegments.back();

  TH1F hChargeProfileStart = aFirstSegment.getChargeProfile();
  TH1F hChargeProfileEnd = aLastSegment.getChargeProfile();
  double chargeCut = 0.0;
  
  int startBin = 0;
  int endBin = 1E6;
  int binTmp = 0;
  chargeCut = 0.05*hChargeProfileStart.GetMaximum();
  binTmp = hChargeProfileStart.FindFirstBinAbove(chargeCut);

  if(binTmp>startBin) startBin = binTmp;
    ///
  chargeCut = 0.05*hChargeProfileEnd.GetMaximum();
  binTmp = hChargeProfileEnd.FindLastBinAbove(chargeCut);
  if(binTmp<endBin) endBin = binTmp;

  if(endBin-startBin<2){
    startBin-=1;
    endBin+=1;
  }

  double lambdaStart = hChargeProfileStart.GetBinCenter(startBin);
  double lambdaEnd = hChargeProfileEnd.GetBinCenter(endBin);

  TVector3 aStart = aFirstSegment.getStart() + getSegmentLambda(lambdaStart, 0)*aFirstSegment.getTangent();
  TVector3 aEnd = aLastSegment.getStart() + getSegmentLambda(lambdaEnd, mySegments.size()-1)*aLastSegment.getTangent();

  aFirstSegment.setStartEnd(aStart, aFirstSegment.getEnd());
  aLastSegment.setStartEnd(aLastSegment.getStart(), aEnd);
  update();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
double Track3D::getSegmentLambda(double lambda, unsigned int iSegment) const{

  double segmentLambda = lambda;
  for(unsigned int index=0;index<iSegment;++index){
    segmentLambda -= mySegments[index].getLength();
  }
  if(segmentLambda<0) segmentLambda = 0.0;
  return segmentLambda;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void Track3D::removeEmptySegments(){

  auto modifiedEnd = std::remove_if(mySegments.begin(), mySegments.end(), [](auto aItem){
      std::vector<double> segmentParameters = aItem.getStartEndXYZ();
      double segmentChi2 = aItem(segmentParameters.data());
      return segmentChi2<1E-5 || segmentChi2>999;
    });

  unsigned int newSize = modifiedEnd - mySegments.begin();
  mySegments.resize(newSize);
  update();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
double Track3D::chi2FromNodesList(const double *par){
  
  for(unsigned int iSegment=0;iSegment<mySegments.size();++iSegment){
    if(myFitMode==FIT_START_STOP){
      double segmentParameters[6];

      segmentParameters[0]  = par[0];
      segmentParameters[1]  = par[1];
      segmentParameters[2]  = par[2];

      segmentParameters[3]  = par[3*iSegment+3];
      segmentParameters[4]  = par[3*iSegment+4];
      segmentParameters[5]  = par[3*iSegment+5];
      
      mySegments.at(iSegment).setStartEnd(segmentParameters);
    }
    if(myFitMode==FIT_BIAS_TANGENT){
      const double *segmentParameters = par+3*iSegment;
      mySegments.at(iSegment).setBiasTangent(segmentParameters);
    }
  }

  update();
  return getChi2();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
std::ostream & operator << (std::ostream &out, const Track3D &aTrack){

  out << "Number of segments: "<<aTrack.getSegments().size()<<std::endl;
  if(!aTrack.getSegments().size()) return out;

  out<<"\t Segments:"<<std::endl;
  out<<KBLU<<"-----------------------------------"<<RST<<std::endl;
  for(auto aSegment: aTrack.getSegments()){
    out<<"\t \t"<<aSegment<<std::endl;
    out<<KBLU<<"-----------------------------------"<<RST<<std::endl;
  }

  auto length = aTrack.getLength();
  auto charge = aTrack.getIntegratedCharge(length);
  auto chargeFromProfile = aTrack.getChargeProfile().Integral("width");
  out<<"\t Total track length [mm]: "<<length<<std::endl;
  out<<"\t Total track charge [arb. units]: "<<charge<<std::endl;
  out<<"\t Total track charge  "<<std::endl;
  out<<"\t from 3D profile [arb. units]: "<<aTrack.getChargeProfile().Integral("width")<<std::endl;
  out<<"\t Hit fit loss func.: "<<aTrack.getChi2()<<std::endl;
  out<<"\t dE/dx fit loss func./charge^2: "<<aTrack.getHypothesisFitChi2()/std::pow(chargeFromProfile+0.01,2)<<std::endl;
  out<<"\t       fitted diffusion [mm]: "<<aTrack.getSegments().front().getDiffusion()<<std::endl;
  out<<KBLU<<"-----------------------------------"<<RST<<std::endl;
  return out;
}
