#include <cstdlib>
#include <iostream>

#include <TGResourcePool.h>
#include <TGFrame.h>
#include <TGLabel.h>
#include <TGTextEntry.h>
#include <TGNumberEntry.h>
#include <TGCanvas.h>
#include <TGTableLayout.h>
#include <TGFontDialog.h>
#include <TFrame.h>

#include "colorText.h"
#include "MarkersManager.h"
#include "EntryDialog.h"
#include "MainFrame.h"
#include "HistoManager.h"
#include "ScrollFrame.h"
#include "CommonDefinitions.h"

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
MarkersManager::MarkersManager(const TGWindow * p, MainFrame * aFrame)
 : TGCompositeFrame(p, 10, 10, kVerticalFrame), fParentFrame(aFrame){

   SetCleanup(kDeepCleanup);

   fTopFrame = new TGVerticalFrame(this, 300, 300);
   TGLayoutHints aLayoutHints(kLHintsTop | kLHintsLeft | kLHintsExpandY |
			      kLHintsShrinkX|kLHintsShrinkY |
			      kLHintsFillX|kLHintsFillY, 2, 2, 2, 2);
   AddFrame(fTopFrame, &aLayoutHints);
   
   TGGroupFrame *aHeaderFrame = new TGGroupFrame(fTopFrame, "Segment creation");
   fTopFrame->AddFrame(aHeaderFrame, new TGLayoutHints(kLHintsExpandX, 2, 2, 1, 1));

   addSegmentButton = new TGTextButton(aHeaderFrame,"Add segment", M_ADD_SEGMENT);
   ULong_t aColor;
   gClient->GetColorByName("yellow", aColor);
   addSegmentButton->ChangeBackground(aColor);
   addSegmentButton->Connect("Clicked()","MarkersManager",this,"DoButton()");   
   aHeaderFrame->AddFrame(addSegmentButton, new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));

   /*
   TGLabel *aLabel = new TGLabel(aHeaderFrame,"U");
   aHeaderFrame->AddFrame(aLabel, new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));

   aLabel = new TGLabel(aHeaderFrame,"V");
   aHeaderFrame->AddFrame(aLabel, new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));

   aLabel = new TGLabel(aHeaderFrame,"W");
   aHeaderFrame->AddFrame(aLabel, new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));
   */
   /*
   fMarkerGCanvas = new TGCanvas(fTopFrame, 300, 300);
   TGCompositeFrame *aMarkerContainer = new TGCompositeFrame(fMarkerGCanvas->GetViewPort(), kVerticalFrame);
   fMarkerGCanvas->SetContainer(aMarkerContainer);
   fTopFrame->AddFrame(fMarkerGCanvas, new TGLayoutHints(kLHintsExpandX, 2, 2, 1, 1));
   fTopFrame->Layout();
   */
   /*
   for(int iMarker=0;iMarker<4;++iMarker){
     addMarkerFrame(iMarker);
   }
   */

   initialize();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
MarkersManager::~MarkersManager(){

  delete fMarkerGCanvas;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::initialize(){

  fMarkersContainer.resize(3);
  fHelperLinesContainer.resize(3);
  fSegmentsContainer.resize(3);
  
  firstMarker = 0;
  acceptPoints = false;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::updateSegments(int strip_dir){

  std::vector<TLine> &aSegmentsContainer = fSegmentsContainer.at(strip_dir);

  if(aSegmentsContainer.size() && !isLastSegmentComplete(strip_dir)){    
    TLine &aLine = aSegmentsContainer.back();
    double x = fMarkersContainer.at(strip_dir)->GetX();
    double y = fMarkersContainer.at(strip_dir)->GetY();
    aLine.SetX2(x);
    aLine.SetY2(y);
  }
  else{
    double x = fMarkersContainer.at(strip_dir)->GetX();
    double y = fMarkersContainer.at(strip_dir)->GetY();
    TLine aLine(x, y, x, y);
    aLine.SetLineWidth(3);
    aLine.SetLineColor(2+aSegmentsContainer.size());
    aSegmentsContainer.push_back(aLine);
  }

  std::string padName = "Histograms_"+std::to_string(strip_dir+1);
  TPad *aPad = (TPad*)gROOT->FindObject(padName.c_str());
  if(!aPad) return;
  aPad->cd();
  for(auto &item:aSegmentsContainer){
    item.Draw();
    TMarker aMarker(item.GetX1(), item.GetY1(), 21);
    aMarker.SetMarkerColor(item.GetLineColor());
    aMarker.DrawMarker(item.GetX1(), item.GetY1());
    aMarker.DrawMarker(item.GetX2(), item.GetY2());
  }
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::resetMarkers(){

  std::for_each(fMarkersContainer.begin(), fMarkersContainer.end(),
		[](TMarker *&item){if(item){delete item; item = 0;}});
  firstMarker = 0;

  clearHelperLines();

  int strip_dir = 0;//TEST
  if(isLastSegmentComplete(strip_dir)){
    acceptPoints = false;
    if(addSegmentButton) addSegmentButton->SetState(kButtonUp);
  }
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::clearHelperLines(){

std::for_each(fHelperLinesContainer.begin(), fHelperLinesContainer.end(),
	      [](TLine *&item){if(item){delete item; item = 0;}});  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
/*
void MarkersManager::addHeaderFrame(){
TGHorizontalFrame *aHorizontalFrame = new TGHorizontalFrame(fMarkerGCanvas->GetContainer(), 200, 30);
  
}
*/
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::addMarkerFrame(int iMarker){

  TGHorizontalFrame *aHorizontalFrame = new TGHorizontalFrame(fMarkerGCanvas->GetContainer(), 200, 30);
  TGLayoutHints *aLayoutHints = new TGLayoutHints(kLHintsLeft, 2, 2, 2, 2);
  TGCompositeFrame *aCompositeFrame = (TGCompositeFrame*)fMarkerGCanvas->GetContainer();
  aCompositeFrame->AddFrame(aHorizontalFrame, aLayoutHints);

  float value = 1.0;
  TGNumberEntry *aNumberEntry = new TGNumberEntry(aHorizontalFrame, value, 5, 0,
						  TGNumberFormat::EStyle::kNESRealTwo);
  aNumberEntry->Connect("ValueSet(Long_t)","MainFrame",fParentFrame,"ProcessMessage(Long_t)");
  aNumberEntry->Associate(this);
  aHorizontalFrame->AddFrame(aNumberEntry, aLayoutHints);

  aNumberEntry = new TGNumberEntry(aHorizontalFrame, value, 5, 0,
				   TGNumberFormat::EStyle::kNESRealTwo);
  aNumberEntry->Connect("ValueSet(Long_t)","MainFrame",fParentFrame,"ProcessMessage(Long_t)");
  aNumberEntry->Associate(this);
  aHorizontalFrame->AddFrame(aNumberEntry, aLayoutHints);

  aNumberEntry = new TGNumberEntry(aHorizontalFrame, value, 5, 0,
				   TGNumberFormat::EStyle::kNESRealTwo);
  aNumberEntry->Connect("ValueSet(Long_t)","MainFrame",fParentFrame,"ProcessMessage(Long_t)");
  aNumberEntry->Associate(this);
  aHorizontalFrame->AddFrame(aNumberEntry, aLayoutHints);

  aNumberEntry = new TGNumberEntry(aHorizontalFrame, value, 5, 0,
				   TGNumberFormat::EStyle::kNESRealTwo);
  aNumberEntry->Connect("ValueSet(Long_t)","MainFrame",fParentFrame,"ProcessMessage(Long_t)");
  aNumberEntry->Associate(this);
  aHorizontalFrame->AddFrame(aNumberEntry, aLayoutHints);
 
  fMarkerGCanvas->Layout();
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::processClickCoordinates(int strip_dir, float x, float y){
  
  if(strip_dir<0 || strip_dir>=(int)fMarkersContainer.size() || fMarkersContainer.at(strip_dir)) return;  
  if(firstMarker){ x = firstMarker->GetX(); }

  int iMarkerColor = 2;
  int iMarkerStyle = 8;
  int iMarkerSize = 1;
  TMarker *aMarker = new TMarker(x, y, iMarkerStyle);
  aMarker->SetMarkerColor(iMarkerColor);
  aMarker->SetMarkerSize(iMarkerSize);
  aMarker->Draw();
  fMarkersContainer.at(strip_dir) = aMarker;
  if(!firstMarker){
    firstMarker = aMarker;
    drawFixedTimeLines(strip_dir, x);
  }
  else{ 
    int missingMarkerDir = findMissingMarkerDir();
    y = getMissingYCoordinate(missingMarkerDir);
    aMarker = new TMarker(x, y, iMarkerStyle);
    aMarker->SetMarkerColor(iMarkerColor);
    aMarker->SetMarkerSize(iMarkerSize);
    fMarkersContainer.at(missingMarkerDir) = aMarker;
    std::string padName = "Histograms_"+std::to_string(missingMarkerDir+1);
    TPad *aPad = (TPad*)gROOT->FindObject(padName.c_str());
    aPad->cd();
    aMarker->Draw();
    updateSegments(0);//TEST
    updateSegments(1);//TEST
    updateSegments(2);//TEST
    resetMarkers();
  }
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::drawFixedTimeLines(int strip_dir, double time){

  clearHelperLines();
  int aColor = 1;
  TLine aLine(time, 0, time, 0);
  aLine.SetLineColor(aColor);
  aLine.SetLineWidth(2);
  
  for(int strip_dirTmp=0;strip_dirTmp<3;++strip_dirTmp){
    std::string padName = "Histograms_"+std::to_string(strip_dirTmp+1);
    TPad *aPad = (TPad*)gROOT->FindObject(padName.c_str());
    if(!aPad) continue;
    aPad->cd();
    TFrame *hFrame = (TFrame*)aPad->GetListOfPrimitives()->At(0);
    if(!hFrame) continue;
    double minY = hFrame->GetY1();
    double maxY = hFrame->GetY2();
    fHelperLinesContainer[strip_dirTmp] = aLine.DrawLine(time, minY, time, maxY);
  }  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
int MarkersManager::findMissingMarkerDir(){

 for(int strip_dirTmp=0;strip_dirTmp<3;++strip_dirTmp){
    TMarker *item = fMarkersContainer.at(strip_dirTmp);
    if(!item) return strip_dirTmp;
    }
    return -1;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
double MarkersManager::getMissingYCoordinate(unsigned int missingMarkerDir){

  ///Not implemented yet.
  return 30.0;
  
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::HandleMarkerPosition(Int_t event, Int_t x, Int_t y, TObject *sel){
  
  if(!acceptPoints) return;
  TObject *select = gPad->GetSelected();
  std::string objName = "";
  if(select) objName = std::string(select->GetName());
  if(event == kButton1 && objName.find("vs_time")!=std::string::npos){
    int strip_dir = (objName.find("U")!=std::string::npos) +
      2*(objName.find("V")!=std::string::npos) +
      3*(objName.find("W")!=std::string::npos) - 1;

    TVirtualPad *aCurrentPad = gPad->GetSelectedPad();
    aCurrentPad->cd();
    float localX = aCurrentPad->AbsPixeltoX(x);
    float localY = aCurrentPad->AbsPixeltoY(y);
    processClickCoordinates(strip_dir, localX, localY);
  }
  return;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
Bool_t MarkersManager::HandleButton(Int_t id){
   switch (id) {
   case M_ADD_SEGMENT:
    {
      acceptPoints = true;
      addSegmentButton->SetState(kButtonDown);
    }
    break;
   }
   return kTRUE;
}
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void MarkersManager::DoButton(){
 TGButton* button = (TGButton*)gTQSender;
   UInt_t button_id = button->WidgetId();
   HandleButton(button_id);
 }
////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
bool MarkersManager::isLastSegmentComplete(int strip_dir){

  std::vector<TLine> &aSegmentsContainer = fSegmentsContainer.at(strip_dir);

  return aSegmentsContainer.size() &&
         std::abs(aSegmentsContainer.back().GetX1() - aSegmentsContainer.back().GetX2())>1E-3 &&
	 std::abs(aSegmentsContainer.back().GetY1() - aSegmentsContainer.back().GetY2())>1E-3;
}
////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
