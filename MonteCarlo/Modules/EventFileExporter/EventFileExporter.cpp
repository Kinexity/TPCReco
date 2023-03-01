#include "EventFileExporter.h"
#include "SaveCurrentTDirectory.h"

EventFileExporter::EventFileExporter()
        : file{nullptr}, tpcDataTree{nullptr}, currSimEvent{nullptr}, currPEventTPC{nullptr}, currTrack3D{nullptr},
          currEventInfo{nullptr} {}

fwk::VModule::EResultFlag EventFileExporter::Init(boost::property_tree::ptree config) {
    //create file and ttree
    auto fname = config.get<std::string>("FileName");
    utl::SaveCurrentTDirectory s;
    file = new TFile(fname.c_str(), "RECREATE");
    tpcDataTree = new TTree("TPCData", "");
    tpcRecoDataTree = new TTree("TPCRecoData", "");

    //setup branches:
    for (const auto &br: config.get_child("EnabledBranches")) {
        auto branchName = std::string(br.second.data());
        if (branchName == "SimEvent")
            tpcDataTree->Branch("SimEvent", &currSimEvent);
        if (branchName == "PEventTPC")
            tpcDataTree->Branch("Event", &currPEventTPC);
        if (branchName == "Track3D") {
            tpcRecoDataTree->Branch("RecoEvent", &currTrack3D);
            tpcRecoDataTree->Branch("EventInfo", &currEventInfo);
        }

    }
    return fwk::VModule::eSuccess;
}

fwk::VModule::EResultFlag EventFileExporter::Process(ModuleExchangeSpace &event) {
    currSimEvent = &(event.simEvt);
    currPEventTPC = &(event.tpcPEvt);
    currEventInfo = &(event.eventInfo);
    currTrack3D = &(event.track3D);
    tpcDataTree->Fill();
    tpcRecoDataTree->Fill();
    return fwk::VModule::eSuccess;
}

fwk::VModule::EResultFlag EventFileExporter::Finish() {
    utl::SaveCurrentTDirectory s;
    file->cd();
    tpcDataTree->Write("", TObject::kWriteDelete);
    tpcRecoDataTree->Write("", TObject::kWriteDelete);
    file->Close();
    delete file;
    return fwk::VModule::eSuccess;
}