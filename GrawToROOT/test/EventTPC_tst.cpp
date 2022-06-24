#include <iostream>
#include <string>
#include <memory>

#include "GeometryTPC.h"
#include "EventTPC.h"
#include "PEventTPC.h"
#include "EventSourceMultiGRAW.h"

#include "TFile.h"

#include "colorText.h"

void testHits(std::shared_ptr<EventTPC> aEventPtr, filter_type filterType){
  
  std::cout<<KBLU<<"1D projection on strips: U, V, W [raw]"<<RST<<std::endl;
  std::cout<<aEventPtr->get1DProjection(projection_type::DIR_V, filterType, scale_type::raw)->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get1DProjection(projection_type::DIR_V, filterType, scale_type::raw)->GetXaxis()->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get1DProjection(projection_type::DIR_V, filterType, scale_type::raw)->GetYaxis()->GetTitle()<<std::endl;

  std::cout<<aEventPtr->get1DProjection(projection_type::DIR_TIME, filterType, scale_type::raw)->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get1DProjection(projection_type::DIR_TIME, filterType, scale_type::raw)->GetXaxis()->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get1DProjection(projection_type::DIR_TIME, filterType, scale_type::raw)->GetYaxis()->GetTitle()<<std::endl;
  
  std::cout<<aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::raw)->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::raw)->GetXaxis()->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::raw)->GetYaxis()->GetTitle()<<std::endl;
  
  std::cout<<aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::mm)->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::mm)->GetXaxis()->GetTitle()<<std::endl;
  std::cout<<aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::mm)->GetYaxis()->GetTitle()<<std::endl;

  aEventPtr->get1DProjection(projection_type::DIR_U, filterType, scale_type::raw)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_V, filterType, scale_type::raw)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_W, filterType, scale_type::raw)->Print();
  std::cout<<KBLU<<"1D projection on strips U, V, W [mm]"<<RST<<std::endl;
  aEventPtr->get1DProjection(projection_type::DIR_U, filterType, scale_type::mm)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_V, filterType, scale_type::mm)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_W, filterType, scale_type::mm)->Print();   
  std::cout<<KBLU<<"1D projection on time : global, U, V, W [raw]"<<RST<<std::endl;
  aEventPtr->get1DProjection(projection_type::DIR_TIME, filterType, scale_type::raw)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_TIME_U, filterType, scale_type::raw)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_TIME_V, filterType, scale_type::raw)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_TIME_W, filterType, scale_type::raw)->Print();
  std::cout<<KBLU<<"1D projection on time : global, U, V, W [mm]"<<RST<<std::endl;
  aEventPtr->get1DProjection(projection_type::DIR_TIME, filterType, scale_type::mm)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_TIME_U, filterType, scale_type::mm)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_TIME_V, filterType, scale_type::mm)->Print();
  aEventPtr->get1DProjection(projection_type::DIR_TIME_W, filterType, scale_type::mm)->Print();
  std::cout<<KBLU<<"2D projection time vs strips: U, V, W [raw]"<<RST<<std::endl;
  aEventPtr->get2DProjection(projection_type::DIR_TIME_U, filterType, scale_type::raw)->Print();
  aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::raw)->Print();
  aEventPtr->get2DProjection(projection_type::DIR_TIME_W, filterType, scale_type::raw)->Print();
  std::cout<<KBLU<<"2D projection on time : global, U, V, W [mm]"<<RST<<std::endl;
  aEventPtr->get2DProjection(projection_type::DIR_TIME_U, filterType, scale_type::mm)->Print();
  aEventPtr->get2DProjection(projection_type::DIR_TIME_V, filterType, scale_type::mm)->Print();
  aEventPtr->get2DProjection(projection_type::DIR_TIME_W, filterType, scale_type::mm)->Print();

  if(filterType==filter_type::none){
    std::cout<<KBLU<<"2D projection on time cells vs AGET channels"<<RST<<std::endl;
    aEventPtr->GetChannels(0,0)->Print();
    aEventPtr->GetChannels_raw(0,0)->Print();
  }

  std::cout<<"total charge: "<< aEventPtr->GetTotalCharge(-1, -1, -1, -1, filterType)<<std::endl;
  std::cout<<"total charge DIR_U: "<< aEventPtr->GetTotalCharge(DIR_U, -1, -1, -1, filterType)<<std::endl;  
  std::cout<<"total charge DIR_U, strip 1: "<< aEventPtr->GetTotalCharge(DIR_U, -1, 1, -1, filterType)<<std::endl;
  std::cout<<"total charge DIR_U, sec. 1, strip 58: "<< aEventPtr->GetTotalCharge(DIR_U, 1, 58, -1, filterType)<<std::endl;
  std::cout<<"total charge time cell 128: "<< aEventPtr->GetTotalCharge(-1, -1, -1, 128, filterType)<<std::endl;
  std::cout<<"total charge DIR_U, time cell 128: "<< aEventPtr->GetTotalCharge(DIR_U, -1, -1, 128, filterType)<<std::endl;
  std::cout<<"total charge DIR_U, sec. 1, time cell 128: "<< aEventPtr->GetTotalCharge(DIR_U, 1,  -1, 128, filterType)<<std::endl;

  std::cout<<"max charge: "<< aEventPtr->GetMaxCharge(-1,-1,-1,filterType)<<std::endl;
  std::cout<<"max charge DIR_U: "<< aEventPtr->GetMaxCharge(DIR_U,-1,-1,filterType)<<std::endl;
  std::cout<<"max charge DIR_U, strip 1: "<< aEventPtr->GetMaxCharge(DIR_U, -1, 1, filterType)<<std::endl;
  std::cout<<"max charge DIR_U, sec. 1, strip 58: "<< aEventPtr->GetMaxCharge(DIR_U, 1, 58,filterType)<<std::endl;

  int maxTime = 0, maxStrip = 0;
  std::tie(maxTime, maxStrip) = aEventPtr->GetMaxChargePos(-1,filterType);
  std::cout<<"max charge time: "<<maxTime<<std::endl;
  std::cout<<"max charge channel: "<<0<<std::endl;
  std::tie(maxTime, maxStrip) = aEventPtr->GetMaxChargePos(DIR_U, filterType);
  std::cout<<"max charge time DIR_U: "<<maxTime<<std::endl;
  std::cout<<"max charge strip DIR_U: "<<maxStrip<<std::endl;
  /*
  std::cout<<"max time: "<<std::endl;//aEventPtr->GetMaxTime(-1, -1, -1, filterType)<<std::endl;
  std::cout<<"min time: "<<std::endl;//aEventPtr->GetMinTime(filterType)<<std::endl;    
  std::cout<<"multiplicity(total): "<<aEventPtr->GetMultiplicity(-1, -1, filterType)<<std::endl;

  std::cout<<"multiplicity(DIR_U): "<<aEventPtr->GetMultiplicity(DIR_U, -1, filterType)<<std::endl;
  std::cout<<"multiplicity(DIR_V): "<<aEventPtr->GetMultiplicity(DIR_V, -1, filterType)<<std::endl;
  std::cout<<"multiplicity(DIR_W): "<<aEventPtr->GetMultiplicity(DIR_W, -1, filterType)<<std::endl;
  
  std::cout<<"multiplicity(DIR_U, 0): "<<aEventPtr->GetMultiplicity(DIR_U, 0, filterType)<<std::endl;
  std::cout<<"multiplicity(DIR_V, 0): "<<aEventPtr->GetMultiplicity(DIR_V, 0, filterType)<<std::endl;
  std::cout<<"multiplicity(DIR_W, 0): "<<aEventPtr->GetMultiplicity(DIR_W, 0, filterType)<<std::endl; 
  */ 
}
/////////////////////////////////////
/////////////////////////////////////
int main(int argc, char *argv[]) {

  std::string geometryFileName = "geometry_ELITPC_190mbar_3332Vdrift_25MHz.dat";
  std::string dataFileName = "/scratch_cmsse/akalinow/ELITPC/data/HIgS_2022/20220412_extTrg_CO2_190mbar_DT1470ET/11.5MeV/GRAW/CoBo0_AsAd0_2022-04-12T08:03:44.531_0000.graw,/scratch_cmsse/akalinow/ELITPC/data/HIgS_2022/20220412_extTrg_CO2_190mbar_DT1470ET/11.5MeV/GRAW/CoBo0_AsAd1_2022-04-12T08:03:44.533_0000.graw,/scratch_cmsse/akalinow/ELITPC/data/HIgS_2022/20220412_extTrg_CO2_190mbar_DT1470ET/11.5MeV/GRAW/CoBo0_AsAd2_2022-04-12T08:03:44.536_0000.graw,/scratch_cmsse/akalinow/ELITPC/data/HIgS_2022/20220412_extTrg_CO2_190mbar_DT1470ET/11.5MeV/GRAW/CoBo0_AsAd3_2022-04-12T08:03:44.540_0000.graw";
  std::string referenceDataFileName = "";

  std::shared_ptr<EventSourceBase> myEventSource = std::make_shared<EventSourceMultiGRAW>(geometryFileName);
  myEventSource->loadDataFile(dataFileName);
  std::cout << "File with " << myEventSource->numberOfEntries() << " frames opened." << std::endl;

  auto myEventPtr = myEventSource->getCurrentEvent();
  for(int i=0;i<100;++i){
    myEventSource->loadFileEntry(i);
  
    std::cout<<myEventPtr->GetEventInfo()<<std::endl;
    testHits(myEventPtr, filter_type::none);
    testHits(myEventPtr, filter_type::threshold);    
  }

  return 0;
}
/////////////////////////////////////
/////////////////////////////////////
