/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
//
// Analysis class for tuning reconstructed track lengths and/or energies
// by applying corrections on top of default IonRangeCalculator (i.e. initialized with length corrections:
// scale_length=1, offset_length=0). The following 3 correction types are implemented:
//
// 1. Linear re-scaling of track lengths per PID:
//       LEN_TRUE(LEN_MEAS) = LSCALE * LEN_MEAS + LOFFSET[mm]
// or:   LEN_MEAS(LEN_TRUE) = (LEN_TRUE - LOFFSET[mm]) / LSCALE
//
// 2. Linear re-scaling of track energies in CMS per PID:
//       EKIN_TRUE(EKIN_MEAS) = ESCALE * EKIN_MEAS + EOFFSET[MeV]
// or:   EKIN_MEAS(EKIN_TRUE) = (EKIN_TRUE - EOFFSET[MeV]) / ESCALE
//
// 3. Re-scaling both track energies and lengths in LAB per PID using Zenek's formula:
//      EKIN_TRUE( LEN_MEAS ) = ESCALE * EKIN0_SRIM( R0(LEN_MEAS) + LOFFSET[mm] ) [MeV]
// or:  EKIN_TRUE( EKIN_MEAS ) = ESCALE * ENERGY( LEN_MEAS + LOFFSET * T/T0 * p0/p )
//      EKIN_MEAS( EKIN_TRUE ) = ENERGY( RANGE( EKIN_TRUE/ESCALE ) - LOFFSET * p/p0 * T0/T )
//
// where:
//      LEN_MEAS = measured range at (p,T)
//      R0(LEN_MEAS) = LEN_MEAS * T0/T * p/p0 = reduced measured range corresponding to reference (p0,T0)
//      RANGE0_SRIM( EKIN_MEAS(LEN_MEAS) ) = R0
//      ENERGY(LENGTH) = EKIN0_SRIM( LENGTH * T0/T * p/p0 ) = kinetic energy curve at (p,T)
//      RANGE(ENERGY) = p0/p * T/T0 * RANGE0_SRIM(ENERGY) = range curve at (p,T)
// 
// Several collections of tracks corresponding to different 2-body decay
// hypotheses can be supplied as input.
// The class provides operator() for use with external minimization fitter, such as: MINUIT2/MIGRAD2, MINUIT2/FUMULI2.
//
// Fitting options:
// - Type of correction to be applied: length, kin.energy in LAB, kin.energy in CMS, Zenek's formula.
// - Use nominal gamma beam energy for LAB-to-CMS boost instead of reconstructed event-by-event one
// - Use only the leading track (e.g. PID=ALPHA) for caclulating properties in the CMS frame.
//   The second component will be deduced from energy and momentum conservation.
//   In this case the correction factors will be tuned only for PID corresponding to the leading track
//   (NOTE: this option is ignored for three-alpha "democratic" decay hypothesis)
// - Use only multiplicative scaling factors instead of scale + offset (i.e. fix offset=0).
//
//
// Mikolaj Cwiok (UW) - 9 June 2023
//
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////

#define USE_SHAPE_ASYMMETRY true // TRUE = bifurcated asymmetric gaussian shape
                                 // FALSE = standard symmetric gaussian shape
#define USE_EXPECTED_WIDTH true  // TRUE = include expected width (sigma) of the excitation energy in CMS in partial CHI2 calculations
                                 // FALSE = use only expected detector resolution and fit error in partial CHI2 calculations
#define USE_STAT_WEIGHT false    // TRUE = apply weigths to partial CHI2 contributions for different Qvalue peaks/selections
                                 //        according to event statistics
                                 // FALSE = treat all partial CHI2 contributions for different Qvalue peaks/selections as equally important
#define USE_SHAPE_CHI2 false     // TRUE = use CHI2/NDF from fitting TF1 template shape, where NDF=number of bins in the fitting range
                                 // FALSE = use formula CHI2 = ( Qvalue_fit - expected_Qvalue )^2 / expected_resolution^2
                                 //         where: expected_resolution^2 = detector_resolution^2 + expected_peak_sigma^2

#define TOY_MC_TESTSCALE false // TRUE = re-scale input data using scaling factors given below:
#define TOY_MC_TESTSCALE_TRUE_LOFFSET_ALPHA   0.00 // [mm]
#define TOY_MC_TESTSCALE_TRUE_LSCALE_ALPHA    1.00 // unitless
#define TOY_MC_TESTSCALE_TRUE_LOFFSET_CARBON  0.00 // [mm]
#define TOY_MC_TESTSCALE_TRUE_LSCALE_CARBON   1.00 // unitless
#define TOY_MC_TESTSCALE_TRUE_EOFFSET_ALPHA   0.00 // [MeV]
#define TOY_MC_TESTSCALE_TRUE_ESCALE_ALPHA    1.00 // unitless
#define TOY_MC_TESTSCALE_TRUE_EOFFSET_CARBON  0.00 // [MeV]
#define TOY_MC_TESTSCALE_TRUE_ESCALE_CARBON   1.00 // unitless

#define DEBUG_ENERGY false

#include <cstdlib>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <tuple>
#include <string>
#include <boost/bimap.hpp> // TEMPORARY FOR enumDict ENERGY_SCALE - TO BE DELETED
#include <boost/assign.hpp> // TEMPORARY FOR enumDict ENERGY_SCALE - TO BE DELETED
#include <boost/algorithm/string.hpp>

#include <TVector3.h>
#include <TLorentzVector.h>
#include <TH1D.h>
#include <TF1.h>
#include <TList.h>
#include <TStyle.h>
#include <TCanvas.h>
#include <TLine.h>
#include <TObject.h>
#include <TFile.h>
#include <TFitResultPtr.h>
#include <TFitResult.h>
#include <Fit/Fitter.h>

#include "TPCReco/colorText.h"
#include "TPCReco/CommonDefinitions.h"
#include "TPCReco/IonRangeCalculator.h"
#include "TPCReco/EnergyScale_analysis.h"

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
EnergyScale_analysis::EnergyScale_analysis(const FitOptionType &aOption,
					   std::vector<EventCollection> &aSelection)
  : myOptions(aOption), mySelection(aSelection) {
  
  reset();
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void EnergyScale_analysis::reset() {
  
  // sanity checks
  myNparams = myOptions.tuned_pid_map.size() * 2; // scale + offset
  if(!myNparams) {
    throw std::runtime_error("Empty list of PIDs to be tuned");
  }
  if(!mySelection.size()) {
    throw std::runtime_error("Empty list of reaction hypotheses to be fitted");
  }
  for(auto &coll: mySelection) {
    if(coll.reaction != reaction_type::C12_ALPHA &&
       coll.reaction != reaction_type::C13_ALPHA &&
       coll.reaction != reaction_type::C14_ALPHA &&
       coll.reaction != reaction_type::THREE_ALPHA_DEMOCRATIC &&
       coll.reaction != reaction_type::THREE_ALPHA_BE) {
      throw std::runtime_error("Unsupported reaction type to be fitted");
    }
    if(!coll.events.size()) {
      throw std::runtime_error("Empty list of events to be fitted");
    }
    if(coll.expectedExcitationEnergySigmaInMeV_CMS<=0.0 || coll.expectedExcitationEnergyPeakInMeV_CMS<=0.0) {
      throw std::runtime_error("Wrong parameters of the expected gamma energy peak");
    }
  }
  
  // reset fitted parameters
  myCorrectionMap.clear();
  for(auto &category: myOptions.tuned_pid_map) {
    if(category.second.size()==0) {
      throw std::runtime_error("Category with empty list of PIDs to be tuned");
    }
    double offset=0.0; // [mm] or [MeV]
    double scale=1.0;
    initialPar.push_back(offset);
    isFixedPar.push_back(myOptions.use_scale_only);
    initialPar.push_back(scale);
    isFixedPar.push_back(false);
    for(auto &pid: category.second) { // PIDs within same category have identical energy scale corrections
      if(pid==pid_type::UNKNOWN) {
	throw std::runtime_error("Wrong PID in the list of PIDs to be tuned");
      }
      if(myCorrectionMap.find(pid)!=myCorrectionMap.end()) {
	throw std::runtime_error("Duplicate PID in the list of PIDs to be tuned");
      }
      myCorrectionMap[pid]=std::make_tuple(offset, scale);
    }
  }
  lastPar=initialPar;

  // clear global chi2 scaling factor
  myFactorChi2 = 1.0;

  // clear debug histograms
  myQvalueFitMap.clear();
  for(auto &it: mySelection) {
    myQvalueFitMap.insert( std::make_pair( it.description, std::make_tuple(TH1D(), TF1(), 0.0 ) ) );
  }
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// helper function to be called before each fit iteration
void EnergyScale_analysis::initializeCorrections(const size_t npar, const double *par) {

  // sanity check
  if (npar != myNparams || par==NULL) {
    throw std::runtime_error("Wrong input NPAR or PAR array");
  }

  // set current corrections for track energy or length
  myCorrectionMap.clear();
  size_t ipar=0;
  for(auto &category: myOptions.tuned_pid_map) {
    double offset = par[ipar];
    double scale = par[ipar+1];
    for(auto &pid: category.second) { // PIDs within same category have identical energy scale corrections
      myCorrectionMap[pid]=std::make_tuple(offset, scale);
    }
    lastPar[ipar] = offset;
    lastPar[ipar+1] = scale;
    ipar += 2;
  }
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// dimensionless speed vector (c=1) of gamma-nucleus CMS reference frame wrt LAB reference frame in BEAM coordinate system
TVector3 EnergyScale_analysis::getBetaVectorOfCMS_BEAM(double nucleusMassInMeV, double photonEnergyInMeV_LAB) const {
  return getBetaOfCMS(nucleusMassInMeV, photonEnergyInMeV_LAB)*TVector3(0,0,1);
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// dimensionless speed (c=1) of gamma-nucleus CMS reference frame wrt LAB reference frame
double EnergyScale_analysis::getBetaOfCMS(double nucleusMassInMeV, double photonEnergyInMeV_LAB) const {
  return photonEnergyInMeV_LAB/(photonEnergyInMeV_LAB+nucleusMassInMeV);
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
size_t EnergyScale_analysis::getNparams() const { return myNparams; }

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////

////// DEBUG
#if(TOY_MC_TESTSCALE)
// artificially rescale track energy in CMS
static double TESTrescaleEnergy(pid_type pid, double EkinMeV) {
  double scale;
  double offset;
  switch (pid) {
  case pid_type::ALPHA:
    scale  = TOY_MC_TESTSCALE_TRUE_ESCALE_ALPHA;
    offset = TOY_MC_TESTSCALE_TRUE_EOFFSET_ALPHA;
    break;
  case pid_type::CARBON_12:
  case pid_type::CARBON_13:
  case pid_type::CARBON_14:
    scale  = TOY_MC_TESTSCALE_TRUE_ESCALE_CARBON;
    offset = TOY_MC_TESTSCALE_TRUE_EOFFSET_CARBON;
    break;
  default:
    scale  = 1.0;
    offset = 0.0;
  };
  return std::max((EkinMeV - offset) / scale, 0.0);
}
// artificially rescale track length
static double TESTrescaleLength(pid_type pid, double length) {
  double scale;
  double offset;
  switch (pid) {
  case pid_type::ALPHA:
    scale  = TOY_MC_TESTSCALE_TRUE_LSCALE_ALPHA;
    offset = TOY_MC_TESTSCALE_TRUE_LOFFSET_ALPHA;
    break;
  case pid_type::CARBON_12:
  case pid_type::CARBON_13:
  case pid_type::CARBON_14:
    scale  = TOY_MC_TESTSCALE_TRUE_LSCALE_CARBON;
    offset = TOY_MC_TESTSCALE_TRUE_LOFFSET_CARBON;
    break;
  default:
    scale  = 1.0;
    offset = 0.0;
  };
  return std::max((length - offset) / scale, 0.0);
}
// get artificial scale/offset corrections per track PID
// ipar=0 --> offset
// ipar=1 --> scale
static double TESTgetCorrectionPerPID(pid_type pid, int ipar, escale_type type=escale_type::ZENEK) {
  double result=0.0;
  double offset;
  double scale;
  switch (pid) {
  case pid_type::ALPHA:
    switch(type) {
    case escale_type::LENGTH:
      scale  = TOY_MC_TESTSCALE_TRUE_LSCALE_ALPHA;
      offset = TOY_MC_TESTSCALE_TRUE_LOFFSET_ALPHA;
      break;
    case escale_type::ZENEK:
      scale  = TOY_MC_TESTSCALE_TRUE_ESCALE_ALPHA;
      offset = TOY_MC_TESTSCALE_TRUE_LOFFSET_ALPHA;
      break;
    case escale_type::ENERGY_CMS:
    case escale_type::ENERGY_LAB:
      scale  = TOY_MC_TESTSCALE_TRUE_ESCALE_ALPHA;
      offset = TOY_MC_TESTSCALE_TRUE_EOFFSET_ALPHA;
      break;
    default:;
    };
    break;
  case pid_type::CARBON_12:
  case pid_type::CARBON_13:
  case pid_type::CARBON_14:
    switch(type) {
    case escale_type::LENGTH:
      scale  = TOY_MC_TESTSCALE_TRUE_LSCALE_CARBON;
      offset = TOY_MC_TESTSCALE_TRUE_LOFFSET_CARBON;
      break;
    case escale_type::ZENEK:
      scale  = TOY_MC_TESTSCALE_TRUE_ESCALE_CARBON;
      offset = TOY_MC_TESTSCALE_TRUE_LOFFSET_CARBON;
      break;
    case escale_type::ENERGY_CMS:
    case escale_type::ENERGY_LAB:
      scale  = TOY_MC_TESTSCALE_TRUE_ESCALE_CARBON;
      offset = TOY_MC_TESTSCALE_TRUE_EOFFSET_CARBON;
      break;
    default:;
    };
    break;
  default:
    scale  = 1.0;
    offset = 0.0;
  };
  switch(ipar) {
  case 0:
    result = offset;
    break;
  case 1:
    result = scale;
    break;
  default:
    throw std::runtime_error("Ivalid PID correction index");
  };
  return result;
}
#endif
////// DEBUG

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// helper function for applying linear correction (track length and kinetic energy)
double EnergyScale_analysis::applyLinearCorrectionPerPID(pid_type pid, double observable) const {
  auto it = myCorrectionMap.find(pid);
  if(it==myCorrectionMap.end()) {
    return observable;
  }
  auto offset = std::get<0>(it->second);
  auto scale = std::get<1>(it->second);
  return std::max( observable * scale + offset, 0.0); // return non-negative corrected value
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// helper function to extract current correction value for a given PID
// ipar=0 --> offset
// ipar=1 --> scale
double EnergyScale_analysis::getCorrectionPerPID(pid_type pid, int ipar) const {
  double result = 0.0;
  double offset = 0.0;
  double scale = 1.0;
  auto it = myCorrectionMap.find(pid);
  if(it!=myCorrectionMap.end()) {
    offset = std::get<0>(it->second);
    scale = std::get<1>(it->second);
  }
  switch(ipar) {
  case 0:
    result = offset;
    break;
  case 1:
    result = scale;
    break;
  default:
    throw std::runtime_error("Ivalid PID correction index");
  };
  return result;
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// Reconstructs single 2-body or 3-body decay event.
// Returns the following triplet:
// * Qvalue_CMS [MeV] - calculated for THIS event
// * expected Qvalue_CMS [MeV] - calculated from expected peak in the excitation energy in CMS
// * error - flag indicating invalid input and results
std::tuple<double, double, bool> EnergyScale_analysis::getEventQvalue_CMS(double nominalBeamEnergyInMeV_LAB,
									  double expectedExcitationEnergyPeakInMeV_CMS,
									  reaction_type reaction,
									  double excitedMassDiffInMeV,
									  std::shared_ptr<IonRangeCalculator> rangeCalc,
									  TrackCollection &list) const {
  ////// DEBUG
  static Long64_t counter=0;
  counter++;
  TLorentzVector totalP4_BEAM_CMS{0,0,0,0};
  TLorentzVector totalP4_BEAM_LAB{0,0,0,0};

  if(DEBUG_ENERGY && counter%10000==0) {
    std::cout << __FUNCTION__ << ": input reaction type=" << enumDict::GetReactionName(reaction) << std::endl;
    std::cout << __FUNCTION__ << ": input track list size=" << list.size() << " :";
    for(auto &it: list) {
      std::cout << " " << enumDict::GetPidName(it.pid);
    }
    std::cout << std::endl;
  }
  ////// DEBUG

  // determine parent nucleus
  auto parentPID=pid_type::UNKNOWN; // parent particle to be decayed
  auto leadingPID=pid_type::UNKNOWN; // leading particle from 2-body decay
  auto trailingPID=pid_type::UNKNOWN; // next-to-leading particle from 2-body decay
  const int ntracks = list.size(); // number of observed final tracks
  switch(ntracks) {
  case 2:
    if(list.front().pid==pid_type::ALPHA &&
       list.back().pid==pid_type::CARBON_12 &&
       reaction==reaction_type::C12_ALPHA) {
      parentPID=pid_type::OXYGEN_16; // O-16 breakup
      leadingPID=list.front().pid;
      trailingPID=list.back().pid;
      break;
    }
    if(list.front().pid==pid_type::ALPHA &&
       list.back().pid==pid_type::CARBON_14 &&
       reaction==reaction_type::C14_ALPHA) {
      parentPID=pid_type::OXYGEN_18; // O-18 breakup
      leadingPID=list.front().pid;
      trailingPID=list.back().pid;
      break;
    }
  case 3:
    if(list.at(0).pid==pid_type::ALPHA &&
       list.at(1).pid==pid_type::ALPHA &&
       list.at(2).pid==pid_type::ALPHA &&
       reaction==reaction_type::THREE_ALPHA_BE) {
      parentPID=pid_type::CARBON_12;
      leadingPID=list.front().pid;
      trailingPID=pid_type::BERYLLIUM_8; // C-12 breakup via intermediate Be-8 ground/excited state
      break;
    }
    if(list.at(0).pid==pid_type::ALPHA &&
       list.at(1).pid==pid_type::ALPHA &&
       list.at(2).pid==pid_type::ALPHA &&
       reaction==reaction_type::THREE_ALPHA_DEMOCRATIC) {
      parentPID=pid_type::CARBON_12;
      leadingPID=list.front().pid;
      trailingPID=pid_type::UNKNOWN; // not a 2-body decay
      break;
    }
  default:
    ////// DEBUG
    std::cout << __FUNCTION__ << ": WARNING: Incompatible reaction type=" << enumDict::GetReactionName(reaction)
	      << " with PID list:";
      for(auto &it: list) {
	std::cout << " " << enumDict::GetPidName(it.pid);
      }
    std::cout << std::endl;
    ////// DEBUG
    return std::make_tuple(0, 0, true); // ERROR
  };

  // get momentum and energy of the leading particle from 2- or 3-body decay in BEAM/LAB frame
  auto leadingLength = list.front().length_uncorrected;

  /////////////////// <--- length corrections in the LAB reference frame
  //
  ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
  if(myOptions.correction_type==escale_type::LENGTH) {
    leadingLength = TESTrescaleLength(leadingPID, leadingLength);
  } else if(myOptions.correction_type==escale_type::ZENEK) {
    auto energy_rescaled = rangeCalc->getIonEnergyMeV(leadingPID, leadingLength) / TESTgetCorrectionPerPID(leadingPID, 1);
    leadingLength =
      std::max(0.0, rangeCalc->getIonRangeMM(leadingPID, energy_rescaled) - TESTgetCorrectionPerPID(leadingPID, 0)
	       * rangeCalc->getGasRangeReferencePressure(leadingPID)/rangeCalc->getGasPressure()
	       * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(leadingPID));
    // auto energy_true = rangeCalc->getIonEnergyMeV(leadingPID, leadingLength);
    // std::cout << "#######  reverse corrections:  PID=" << enumDict::GetPidName(leadingPID)
    // 	      << "  E_true=" << energy_true
    // 	      << "  E_scaled=E_true*(1/f_true)=" << energy_rescaled
    // 	      <<  " R_scaled=RANGE(E_scaled)=" << rangeCalc->getIonRangeMM(leadingPID, energy_rescaled)
    // 	      <<  " R_fake=R_scaled - s_true*(p0/p)*(T/T0)="
    // 	      << rangeCalc->getIonRangeMM(leadingPID, energy_rescaled) - TESTgetCorrectionPerPID(leadingPID, 0)
    //   * rangeCalc->getGasRangeReferencePressure(leadingPID)/rangeCalc->getGasPressure()
    //   * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(leadingPID) << std::endl;
    // auto energy_meas = rangeCalc->getIonEnergyMeV(leadingPID, leadingLength);
    // std::cout << "#######  ion_range:  PID=" << enumDict::GetPidName(leadingPID)
    // 	      << "  E_meas=" << energy_meas
    // 	      << "  E_meas-E_true=" << energy_meas-energy_true << std::endl;
    // auto length_rescaled =
    //   std::max(0.0, leadingLength + TESTgetCorrectionPerPID(leadingPID, 0)
    // 	       * rangeCalc->getGasRangeReferencePressure(leadingPID)/rangeCalc->getGasPressure()
    // 	       * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(leadingPID));
    // auto energy_corr = TESTgetCorrectionPerPID(leadingPID, 1) * rangeCalc->getIonEnergyMeV(leadingPID, length_rescaled);
    // std::cout << "#######  corrected:  PID=" << enumDict::GetPidName(leadingPID)
    // 	      << "  R_corr=" << length_rescaled
    // 	      << "  E_corr=f_true*RANGE(R_corr)=" << energy_corr
    // 	      << "  E_corr-E_true=" << energy_corr-energy_true << std::endl;
  }
#endif
  ////// DEBUG
  //
  // on demand, apply current corrections to the leading track length
  if(myOptions.correction_type==escale_type::LENGTH) {
    leadingLength = applyLinearCorrectionPerPID(leadingPID, leadingLength);
  }
  //
  /////////////////// <--- length corrections in the LAB reference frame

  auto leading_T_LAB = rangeCalc->getIonEnergyMeV(leadingPID, leadingLength); // kinetic energy in LAB frame

  /////////////////// <--- energy corrections in the LAB reference frame
  //
  ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
  if(myOptions.correction_type==escale_type::ENERGY_LAB) {
    leading_T_LAB = TESTrescaleEnergy(leadingPID, leading_T_LAB);
  }
#endif
  ////// DEBUG
  //
  // on demand, apply current corrections to track energies in LAB
  if(myOptions.correction_type==escale_type::ENERGY_LAB) {
    leading_T_LAB = applyLinearCorrectionPerPID(leadingPID, leading_T_LAB); // corrected Ekin
  } else if(myOptions.correction_type==escale_type::ZENEK) {
    auto length_rescaled =
      std::max(0.0, leadingLength + getCorrectionPerPID(leadingPID, 0)
	       * rangeCalc->getGasRangeReferencePressure(leadingPID)/rangeCalc->getGasPressure()
	       * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(leadingPID));
    leading_T_LAB =  getCorrectionPerPID(leadingPID, 1) * rangeCalc->getIonEnergyMeV(leadingPID, length_rescaled);
  }
  //
  /////////////////// <--- energy corrections in the LAB reference frame

  auto leading_mass = rangeCalc->getIonMassMeV(leadingPID); // mass
  auto leading_p_LAB = sqrt(leading_T_LAB*(leading_T_LAB+2*leading_mass)); // scalar momentum
  TVector3 leadingP3_BEAM_LAB; // momentum in LAB frame in BEAM coordinate system
  leadingP3_BEAM_LAB.SetMagThetaPhi(leading_p_LAB, list.front().theta_BEAM_LAB, list.front().phi_BEAM_LAB);
  auto leadingP4_BEAM_LAB=TLorentzVector(leadingP3_BEAM_LAB, leading_mass+leading_T_LAB);

  // get invariant mass, sum of momenta, sum of kinetic energies of all but the leading particle in this event
  double trailing_mass=0;
  double trailing_T_LAB=0;
  double trailing_p_LAB=0;
  TVector3 trailingP3_BEAM_LAB{0,0,0};
  TLorentzVector trailingP4_BEAM_LAB{0,0,0,0};
  if(ntracks==2 || (ntracks==3 && trailingPID==pid_type::BERYLLIUM_8)) {
    trailing_mass = rangeCalc->getIonMassMeV(trailingPID) // [MeV/c^2] - ground state mass
                  + excitedMassDiffInMeV;                   // addition for excited state mass (e.g. Be-8)
  }
  if(ntracks==3) { // 3-body decay
    auto isFirst=true;
    for(auto &track: list) {
      if(isFirst) { isFirst=false; continue; } // skip the rest for the leading particle

      auto length = track.length_uncorrected;

      /////////////////// <--- length corrections in the LAB reference frame
      //
      ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
      if(myOptions.correction_type==escale_type::LENGTH) {
	length = TESTrescaleLength(track.pid, length);
      } else if(myOptions.correction_type==escale_type::ZENEK) {
	auto energy_rescaled = rangeCalc->getIonEnergyMeV(track.pid, length) / TESTgetCorrectionPerPID(track.pid, 1);
	length =
	  std::max(0.0, rangeCalc->getIonRangeMM(track.pid, energy_rescaled) - TESTgetCorrectionPerPID(track.pid, 0)
		   * rangeCalc->getGasRangeReferencePressure(track.pid)/rangeCalc->getGasPressure()
		   * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(track.pid));
      }
#endif
      ////// DEBUG
      //
      // on demand, apply current corrections to all remaining track lengths
      if(myOptions.correction_type==escale_type::LENGTH) {
	length = applyLinearCorrectionPerPID(track.pid, length);
      }
      //
      /////////////////// <--- length corrections in the LAB reference frame

      auto T_LAB = rangeCalc->getIonEnergyMeV(track.pid, length); // [MeV] - kinetic energy in LAB

      /////////////////// <--- energy corrections in the LAB reference frame
      //
      ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
      if(myOptions.correction_type==escale_type::ENERGY_LAB) {
	T_LAB = TESTrescaleEnergy(track.pid, T_LAB);
      }
#endif
      ////// DEBUG
      //
      // on demand, apply current corrections to track energies in LAB
      if(myOptions.correction_type==escale_type::ENERGY_LAB) {
	T_LAB = applyLinearCorrectionPerPID(track.pid, T_LAB); // corrected Ekin
      } else if(myOptions.correction_type==escale_type::ZENEK) {
	auto length_rescaled =
	  std::max(0.0, length + getCorrectionPerPID(track.pid, 0)
		   * rangeCalc->getGasRangeReferencePressure(track.pid)/rangeCalc->getGasPressure()
		   * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(track.pid));
	T_LAB =  getCorrectionPerPID(track.pid, 1) * rangeCalc->getIonEnergyMeV(track.pid, length_rescaled);
      }
      //
      /////////////////// <--- energy corrections in the LAB reference frame

      auto mass = rangeCalc->getIonMassMeV(track.pid); // [MeV/c^2] - particle mass
      auto p_LAB = sqrt(T_LAB*(T_LAB+2*mass)); // [MeV/c] - scalar momentum in LAB
      TVector3 p3_BEAM_LAB{0,0,0};
      p3_BEAM_LAB.SetMagThetaPhi(p_LAB, track.theta_BEAM_LAB, track.phi_BEAM_LAB);
      TLorentzVector p4_BEAM_LAB{0,0,0,0};
      p4_BEAM_LAB.SetVectM(p3_BEAM_LAB, mass);
      trailingP4_BEAM_LAB += p4_BEAM_LAB;
      trailingP3_BEAM_LAB += p3_BEAM_LAB;
      trailing_T_LAB += T_LAB;
    }
    trailing_p_LAB = trailingP3_BEAM_LAB.Mag();
    if(reaction==reaction_type::THREE_ALPHA_DEMOCRATIC) trailing_mass = trailingP4_BEAM_LAB.M(); // get inv. mass from two alphas
  } else { // 2-body decay

    auto trailingLength = list.back().length_uncorrected;

    /////////////////// <--- length corrections in the LAB reference frame
    //
    ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
    if(myOptions.correction_type==escale_type::LENGTH) {
      trailingLength = TESTrescaleLength(trailingPID, trailingLength);
    } else if(myOptions.correction_type==escale_type::ZENEK) {
      auto energy_rescaled = rangeCalc->getIonEnergyMeV(trailingPID, trailingLength) / TESTgetCorrectionPerPID(trailingPID, 1);
      trailingLength =
	std::max(0.0, rangeCalc->getIonRangeMM(trailingPID, energy_rescaled) - TESTgetCorrectionPerPID(trailingPID, 0)
		 * rangeCalc->getGasRangeReferencePressure(trailingPID)/rangeCalc->getGasPressure()
		 * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(trailingPID));
    }
#endif
    ////// DEBUG
    //
    // on demand, apply current corrections to the next-to-leading track length
    if(myOptions.correction_type==escale_type::LENGTH) {
      trailingLength = applyLinearCorrectionPerPID(trailingPID, trailingLength);
    }
    //
    /////////////////// <--- length corrections in the LAB reference frame

    trailing_T_LAB = rangeCalc->getIonEnergyMeV(trailingPID, trailingLength); // [MeV] - kinetic energy in LAB

    /////////////////// <--- energy corrections in the LAB reference frame
    //
    ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
    if(myOptions.correction_type==escale_type::ENERGY_LAB) {
      trailing_T_LAB = TESTrescaleEnergy(leadingPID, trailing_T_LAB);
    }
#endif
    ////// DEBUG
    //
    // on demand, apply current corrections to track energies in LAB
    if(myOptions.correction_type==escale_type::ENERGY_LAB) {
      trailing_T_LAB = applyLinearCorrectionPerPID(trailingPID, trailing_T_LAB); // corrected Ekin
    } else if(myOptions.correction_type==escale_type::ZENEK) {
      auto length_rescaled =
	std::max(0.0, trailingLength + getCorrectionPerPID(trailingPID, 0)
		 * rangeCalc->getGasRangeReferencePressure(trailingPID)/rangeCalc->getGasPressure()
		 * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(trailingPID));
      trailing_T_LAB =  getCorrectionPerPID(trailingPID, 1) * rangeCalc->getIonEnergyMeV(trailingPID, length_rescaled);
    }
    //
    /////////////////// <--- energy corrections in the LAB reference frame

    trailing_p_LAB = sqrt(trailing_T_LAB*(trailing_T_LAB+2*trailing_mass)); // [MeV/c] - scalar momentum in LAB
    trailingP3_BEAM_LAB.SetMagThetaPhi(trailing_p_LAB, list.back().theta_BEAM_LAB, list.back().phi_BEAM_LAB);
    trailingP4_BEAM_LAB.SetVectM(trailingP3_BEAM_LAB, trailing_mass);
  }
    
  // boost P4 from BEAM/LAB frame to BEAM/CMS frame (see TLorentzVector::Boost() convention!)
  auto parentMassGroundState=rangeCalc->getIonMassMeV(parentPID); // MeV/c^2, isotopic mass
  TVector3 beta_BEAM_LAB;
  if(myOptions.use_nominal_beam_energy) {
    beta_BEAM_LAB=getBetaVectorOfCMS_BEAM(parentMassGroundState, nominalBeamEnergyInMeV_LAB); // assume nominal direction and nominal gamma beam energy
  } else {
    auto photon_E_LAB=(leadingP4_BEAM_LAB+trailingP4_BEAM_LAB).E()-parentMassGroundState; // reconstructed gamma beam energy in LAB
    beta_BEAM_LAB=getBetaVectorOfCMS_BEAM(parentMassGroundState, photon_E_LAB);
  }
  auto leadingP4_BEAM_CMS(leadingP4_BEAM_LAB);
  auto trailingP4_BEAM_CMS(trailingP4_BEAM_LAB);
  leadingP4_BEAM_CMS.Boost(-1.0*beta_BEAM_LAB);
  trailingP4_BEAM_CMS.Boost(-1.0*beta_BEAM_LAB);

  // get momentum and energy in BEAM/CMS frame
  auto leading_p_CMS = leadingP4_BEAM_CMS.Vect().Mag(); // [MeV/c] - scalar momentum in CMS
  auto leading_T_CMS = leadingP4_BEAM_CMS.E()-leading_mass; // [MeV] - kinetic energy in CMS
  auto trailing_p_CMS = trailingP4_BEAM_CMS.Vect().Mag(); // [MeV/c] - scalar momentum in CMS
  auto trailing_T_CMS = trailingP4_BEAM_CMS.E()-trailing_mass; // [MeV] - kinetic energy in CMS

  /////////////////// <--- energy corrections in the CMS reference frame
  //
  ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
  if(myOptions.correction_type==escale_type::ENERGY_CMS) {
    leading_T_CMS = TESTrescaleEnergy(leadingPID, leading_T_CMS); // TEST- altered Ekin
    leading_p_CMS = sqrt(leading_T_CMS*(leading_T_CMS+2*leading_mass)); // TEST - altered momentum
    leadingP4_BEAM_CMS.SetVectM(leading_p_CMS*leadingP4_BEAM_CMS.Vect().Unit(), // TEST - altered P3
				leading_mass);
    trailing_T_CMS = TESTrescaleEnergy(trailingPID, trailing_T_CMS); // TEST- altered Ekin
    trailing_p_CMS = sqrt(trailing_T_CMS*(trailing_T_CMS+2*trailing_mass)); // TEST - altered momentum
    trailingP4_BEAM_CMS.SetVectM(trailing_p_CMS*trailingP4_BEAM_CMS.Vect().Unit(), // TEST - altered P3
				 trailing_mass);
  }
#endif
  if(DEBUG_ENERGY && counter%10000==0) {
    std::cout << "X-CHECK BEFORE CORRECTION: Ekin_CMS(" << enumDict::GetPidName(leadingPID) << ")=" << leading_T_CMS << std::endl;
    std::cout << "X-CHECK BEFORE CORRECTION: Ekin_CMS(" << enumDict::GetPidName(trailingPID) << ")=" << trailing_T_CMS << std::endl;
  }
  ////// DEBUG
  //
  // on demand, apply current corrections to track energies in CMS
  if(myOptions.correction_type==escale_type::ENERGY_CMS) {
    leading_T_CMS = applyLinearCorrectionPerPID(leadingPID, leading_T_CMS); // corrected Ekin
    leading_p_CMS = sqrt(leading_T_CMS*(leading_T_CMS+2*leading_mass)); // corrected momentum
    leadingP4_BEAM_CMS.SetVectM(leading_p_CMS*leadingP4_BEAM_CMS.Vect().Unit(), // corrected P3
				leading_mass);
    trailing_T_CMS = applyLinearCorrectionPerPID(trailingPID, trailing_T_CMS); // corrected Ekin
    trailing_p_CMS = sqrt(trailing_T_CMS*(trailing_T_CMS+2*trailing_mass)); // corrected momentum
    trailingP4_BEAM_CMS.SetVectM(trailing_p_CMS*trailingP4_BEAM_CMS.Vect().Unit(), // corrected P3
				 trailing_mass);
  }
  //
  /////////////////// <--- energy corrections in the CMS reference frame

  // on demand, correct next-to-leading particle properties in CMS from momentum conservation principle
  if(myOptions.use_leading_track_only) {
    trailing_p_CMS = leading_p_CMS; // corrected momentum in CMS
    trailingP4_BEAM_CMS.SetVectM(-leadingP4_BEAM_CMS.Vect(), trailing_mass); // corrected P4 in CMS
    trailing_T_CMS = trailingP4_BEAM_CMS.E()-trailing_mass; // [MeV] - corrected kinetic energy in CMS
  }

  ////// DEBUG
  if(DEBUG_ENERGY && counter%10000==0) {
    std::cout << "X-CHECK AFTER CORRECTION: Ekin_CMS(" << enumDict::GetPidName(leadingPID) << ")=" << leading_T_CMS << std::endl;
    std::cout << "X-CHECK AFTER CORRECTION: Ekin_CMS(" << enumDict::GetPidName(trailingPID) << ")=" << trailing_T_CMS << std::endl;
  }
  ////// DEBUG

  ////// DEBUG
  if(DEBUG_ENERGY && counter%10000==0) {
    for(auto &track: list) {

      auto length = track.length_uncorrected;

      /////////////////// <--- length corrections in the LAB reference frame
      //
      ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
      if(myOptions.correction_type==escale_type::LENGTH) {
        length = TESTrescaleLength(track.pid, length);
      } else if(myOptions.correction_type==escale_type::ZENEK) {
	auto energy_rescaled = rangeCalc->getIonEnergyMeV(track.pid, length) / TESTgetCorrectionPerPID(track.pid, 1);
	length =
	  std::max(0.0, rangeCalc->getIonRangeMM(track.pid, energy_rescaled) - TESTgetCorrectionPerPID(track.pid, 0)
		   * rangeCalc->getGasRangeReferencePressure(track.pid)/rangeCalc->getGasPressure()
		   * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(track.pid));
      }
#endif
      ////// DEBUG
      //
      // on demand, apply current corrections to all remaining track lengths
      if(myOptions.correction_type==escale_type::LENGTH) {
	length = applyLinearCorrectionPerPID(track.pid, length);
      }
      //
      /////////////////// <--- length corrections in the LAB reference frame

      auto T_LAB = rangeCalc->getIonEnergyMeV(track.pid, length); // [MeV] - kinetic energy in LAB

      /////////////////// <--- energy corrections in the LAB reference frame
      //
      ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
      if(myOptions.correction_type==escale_type::ENERGY_LAB) {
	T_LAB = TESTrescaleEnergy(track.pid, T_LAB);
      } else if(myOptions.correction_type==escale_type::ZENEK) {
	auto energy_rescaled = rangeCalc->getIonEnergyMeV(track.pid, length) / TESTgetCorrectionPerPID(track.pid, 1);
	length =
	  std::max(0.0, rangeCalc->getIonRangeMM(track.pid, energy_rescaled) - TESTgetCorrectionPerPID(track.pid, 0)
		   * rangeCalc->getGasRangeReferencePressure(track.pid)/rangeCalc->getGasPressure()
		   * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(track.pid));
      }
#endif
      ////// DEBUG
      //
      // on demand, apply current corrections to track energies in LAB
      if(myOptions.correction_type==escale_type::ENERGY_LAB) {
	T_LAB = applyLinearCorrectionPerPID(track.pid, T_LAB); // corrected Ekin
      } else if(myOptions.correction_type==escale_type::ZENEK) {
	auto length_rescaled =
	  std::max(0.0, length + getCorrectionPerPID(track.pid, 0)
		   * rangeCalc->getGasRangeReferencePressure(track.pid)/rangeCalc->getGasPressure()
		   * rangeCalc->getGasTemperature()/rangeCalc->getGasRangeReferencePressure(track.pid));
	T_LAB =  getCorrectionPerPID(track.pid, 1) * rangeCalc->getIonEnergyMeV(track.pid, length_rescaled);
      }
      //
      /////////////////// <--- energy corrections in the LAB reference frame

      auto mass = rangeCalc->getIonMassMeV(track.pid); // [MeV/c^2] - particle mass
      auto p_LAB = sqrt(T_LAB*(T_LAB+2*mass)); // [MeV/c] - scalar momentum in LAB
      TVector3 p3_BEAM_LAB{0,0,0};
      p3_BEAM_LAB.SetMagThetaPhi(p_LAB, track.theta_BEAM_LAB, track.phi_BEAM_LAB);
      TLorentzVector p4_BEAM_LAB{0,0,0,0};
      p4_BEAM_LAB.SetVectM(p3_BEAM_LAB, mass);
      totalP4_BEAM_LAB += p4_BEAM_LAB;
      auto p4_BEAM_CMS(p4_BEAM_LAB);
      p4_BEAM_CMS.Boost(-1.0*beta_BEAM_LAB);
      auto T_CMS = p4_BEAM_CMS.E()-mass;

      /////////////////// <--- energy corrections in the CMS reference frame
      //
      ////// DEBUG
#if(TOY_MC_TESTSCALE) // reverse corrections
      if(myOptions.correction_type==escale_type::ENERGY_CMS) {
	T_CMS = TESTrescaleEnergy(track.pid, T_CMS);
      }
#endif
      ////// DEBUG
      //
      // on demand, apply current corrections to track energies in CMS
      if(myOptions.correction_type==escale_type::ENERGY_CMS) {
	T_CMS = applyLinearCorrectionPerPID(track.pid, T_CMS); // corrected Ekin
      }
      //
      /////////////////// <--- energy corrections in the CMS reference frame

      auto p_CMS = sqrt(T_CMS*(T_CMS+2*mass));
      p4_BEAM_CMS.SetVectM(p_CMS*p4_BEAM_CMS.Vect().Unit(), // new P3
			   mass);
      totalP4_BEAM_CMS += p4_BEAM_CMS;
    }

    std::cout << "##### TOTAL ENERGY XCHECK: parent=" << enumDict::GetPidName(parentPID)
	      << " (leadingP4_LAB+trailingP4_LAB).E()=" << (leadingP4_BEAM_LAB+trailingP4_BEAM_LAB).E()
	      << " totalP4_LAB.E()=" << totalP4_BEAM_LAB.E()
	      << " (diff=" << totalP4_BEAM_LAB.E()-(leadingP4_BEAM_LAB+trailingP4_BEAM_LAB).E() << ")"
	      << " (leadingP4_CMS+trailingP4_CMS).E()=" << (leadingP4_BEAM_CMS+trailingP4_BEAM_CMS).E()
	      << " totalP4_CMS.E()=" << totalP4_BEAM_CMS.E()
	      << " (diff=" << totalP4_BEAM_CMS.E()-(leadingP4_BEAM_LAB+trailingP4_BEAM_CMS).E() << ")" << std::endl;
    std::cout << "##### INVARIANT MASS XCHECK: parent=" << enumDict::GetPidName(parentPID)
	      << " parentMassGroundState=" << parentMassGroundState
	      << " totalP4_LAB.M()=" << totalP4_BEAM_LAB.M()
	      << " totalP4_CMS.M()=" << totalP4_BEAM_CMS.M()
	      << " (diff1=" << (totalP4_BEAM_LAB.M()-parentMassGroundState)
	      << " diff2=" << (totalP4_BEAM_CMS.M()-parentMassGroundState) << ")" << std::endl;
    std::cout << "##### MOMENTUM XCHECK: parent="<< enumDict::GetPidName(parentPID)
	      << " leadingP_CMS=" << leading_p_CMS << " trailingP_CMS=" <<  trailing_p_CMS
	      << " (diff=" << leading_p_CMS-trailing_p_CMS << ")" << std::endl;
    std::cout << "##### MOMENTUM BALANCE XCHECK: parent="<< enumDict::GetPidName(parentPID)
	      << " (leadingP4_LAB+trailingP4_LAB).P()=" <<  (leadingP4_BEAM_LAB+trailingP4_BEAM_LAB).P()
	      << " totalP4_LAB.P()=" << totalP4_BEAM_LAB.P()
	      << " (leadingP4_CMS+trailingP4_CMS).P()=" << (leadingP4_BEAM_CMS+trailingP4_BEAM_CMS).P()
	      << " totalP4_CMS.P()=" << totalP4_BEAM_CMS.P() << std::endl;
  }
  ////// DEBUG

  // calculate expected / average excitation energy above g.s. of the parent's particle in CMS
  auto average_excitation_E_CMS=sqrt(parentMassGroundState*(2*expectedExcitationEnergyPeakInMeV_CMS+parentMassGroundState))-parentMassGroundState;
  auto average_Qvalue_CMS=sqrt(parentMassGroundState*(2*expectedExcitationEnergyPeakInMeV_CMS+parentMassGroundState))-leading_mass-trailing_mass;

  // calculate excitation energy in CMS
  double total_E_CMS=(leadingP4_BEAM_CMS+trailingP4_BEAM_CMS).E(); // [MeV] - mass of parent's stationary excited state
  double excitation_E_CMS=total_E_CMS-parentMassGroundState; // [MeV] - parent's excitation energy above g.s. in CMS
  double Qvalue_CMS=trailing_T_CMS+leading_T_CMS;

  // on demand, use only leading track information
  // NOTE: this option is not valid for THREE_ALPHA_DEMOCRATIC case
  // NOTE: non-relativistic approach
  if(myOptions.use_leading_track_only && reaction!=reaction_type::THREE_ALPHA_DEMOCRATIC) {
    Qvalue_CMS=leading_T_CMS*(1+leading_mass/trailing_mass);
    excitation_E_CMS=Qvalue_CMS+leading_mass+trailing_mass-parentMassGroundState;
  }
  
  ////// DEBUG
  if(DEBUG_ENERGY && counter%10000==0) {
    std::cout << __FUNCTION__ << ": X-CHECK parentMassGroundState=" << parentMassGroundState
	      << " total_P4.M()=" << (leadingP4_BEAM_CMS+trailingP4_BEAM_CMS).M()
	      << " (diff=" << (leadingP4_BEAM_CMS+trailingP4_BEAM_CMS).M()-parentMassGroundState << ")" << std::endl;
    std::cout << __FUNCTION__ << ": E_excitation_CMS=" << excitation_E_CMS
	      << ": (average=" << average_excitation_E_CMS
	      << ", diff=" << excitation_E_CMS-average_excitation_E_CMS << ")" << std::endl;
    std::cout << __FUNCTION__ << ": Qvalue_CMS=" << Qvalue_CMS
	      << ": (average=" << average_Qvalue_CMS
	      << ", diff=" << Qvalue_CMS-average_Qvalue_CMS << ")" << std::endl;
  }
  ////// DEBUG

  return std::make_tuple(Qvalue_CMS, average_Qvalue_CMS, false); // [MeV]
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// helper function to calculate optimal bin size [MeV] for Qvalue_CMS histogram
// rounded off to the nearest: 1keV (for bin<10keV) or 5 keV (for 10keV<bin<50keV) or 10 keV (otherwise)
double EnergyScale_analysis::getOptimalBinWidthInMeV(double histogramRMS, double histogramIntegral) const {
  const auto resolution = 0.001; // [MeV]
  auto binMin = std::max( resolution, histogramRMS/5 ); // at least 10 bins  in [-sigma, sigma] range
  auto binOpt = 4 * histogramRMS / log(1+histogramIntegral);
  auto bin=std::min( binMin, binOpt );
  std::cout << __FUNCTION__ << ":  binmin=" << binMin << "  binOpt=" << binOpt << "  binFinal=" << bin << std::endl;
  if (bin<0.01) return resolution * (long)( bin / resolution); // rounded off to 1 keV
  if (bin<0.05) return 5 * resolution * (long)( bin / resolution / 5); // rounded off to 5 keV
  return 10 * resolution * (long)( bin / resolution / 10); // rounded off to 10 keV
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// - IMPLEMENTATION #7
// helper function to calculate global chi^2
std::tuple<double, size_t> EnergyScale_analysis::getSelectionChi2(EventCollection &selection, bool debug_histos_flag) {

  const bool use_fixed_peak = USE_SHAPE_CHI2;
  const double height_fraction = 0.6; // 0.8; // In the 2nd pass limit fit range to the vicinity of the peak at a certain level
  const double fitSigmaFraction = sqrt(-log(height_fraction)*2);
  const double energyBinSize = getOptimalBinWidthInMeV(sqrt(pow(selection.expectedExcitationEnergySigmaInMeV_CMS,2)+
							    pow(myOptions.detectorQvalueResolutionInMeV_CMS, 2)),
						       selection.events.size()); // [MeV]
  const double xmin = 0.0; // [MeV]
  const double xmax = selection.expectedExcitationEnergyPeakInMeV_CMS + 5.0; // [MeV]
  const long nbins = (long)((xmax-xmin)/energyBinSize);
  TH1D hQvalue("hQvalue", "", nbins, xmin, xmax);
  TH1D hQvalueAve("hQvalueAve", "", nbins, xmin, xmax);
  hQvalue.StatOverflows(true);    // for proper mean value calculations
  hQvalueAve.StatOverflows(true);

  size_t npassed = 0U;
  for(auto &event: selection.events) {
    auto result = getEventQvalue_CMS(selection.photonEnergyInMeV_LAB,
				     selection.expectedExcitationEnergyPeakInMeV_CMS,
				     selection.reaction,
				     selection.excitedMassDiffInMeV, // mass difference for excited states (e.g. Be-8)
				     selection.rangeCalc,
				     event);
    auto error = std::get<2>(result);
    if(error) continue;

    hQvalue.Fill( std::get<0>(result) ); // [MeV] - Qvalue in CMS
    hQvalueAve.Fill( std::get<1>(result) ); // [MeV] - average/expected Qvalue in CMS (=const)
    npassed++;
  }
  auto Qvalue_mean = hQvalue.GetMean();
  auto Qvalue_mean_err = hQvalue.GetMeanError();
  auto Qvalue_expected = hQvalueAve.GetMean();
  auto Qvalue_expected_err = hQvalueAve.GetMeanError();
  auto Qvalue_RMS = hQvalue.GetRMS();

  // post-fill fitting using bifurcated gaussian shape
  TF1 energyShapeGaussBifurcated("f_energyShapeGaussBifurcated", [](double *x, double *p) {
      const double xx = x[0] - p[1]; // [MeV]
      const double sigmaLeft = p[2]; // [MeV]
      const double sigmaRight = p[3]; // [MeV]
      if(xx<0) return p[0] * exp( - 0.5 * xx * xx / sigmaLeft / sigmaLeft );
      return p[0] * exp( - 0.5 * xx * xx / sigmaRight / sigmaRight );
    }, xmin, xmax, 4, 1); // 4 parameters, 1 dimension
  TF1 energyShapeGauss("f_energyShapeGauss", [](double *x, double *p) {
      const double xx = x[0] - p[1]; // [MeV]
      const double sigma = p[2]; // [MeV]
      return p[0] * exp( - 0.5 * xx * xx / sigma / sigma );
    }, xmin, xmax, 3, 1); // 3 parameters, 1 dimension
  TF1* funcPtr = NULL;
  if(USE_SHAPE_ASYMMETRY) {
    funcPtr = &energyShapeGaussBifurcated;
  } else {
    funcPtr = &energyShapeGauss;
  }
  // set parameter constraints depending on CHI2 calculation strategy
  funcPtr->SetParLimits(0, 0.5*hQvalue.GetMaximum(), 2.0*hQvalue.GetMaximum());
  funcPtr->SetParLimits(2, 0.1*Qvalue_RMS, 10*Qvalue_RMS); // [MeV]
  if(USE_SHAPE_ASYMMETRY) {
    funcPtr->SetParameters(hQvalue.GetMaximum(), Qvalue_mean, Qvalue_RMS, Qvalue_RMS);
    funcPtr->SetParLimits(3, 0.1*Qvalue_RMS, 10*Qvalue_RMS); // [MeV]
  } else {
    funcPtr->SetParameters(hQvalue.GetMaximum(), Qvalue_mean, Qvalue_RMS);
  }
  if(use_fixed_peak) {
    funcPtr->FixParameter(1, Qvalue_expected);
  } else {
    funcPtr->SetParLimits(1, xmin, xmax); // [MeV]
  }
  funcPtr->SetNpx(1000);

  //////////////////
  //
  // Default approximate CHI2 from MEAN and RMS
  //
  double chi2 = pow((Qvalue_mean - Qvalue_expected)/Qvalue_RMS, 2);

  //////////////////
  //
  // 1st pass with fit range corresponding to entire hQvalue histogram range
  //
  TFitResultPtr result1 = hQvalue.Fit(funcPtr->GetName(), "I Q WW 0 S", "", // 0=store, but do not draw
				      xmin, xmax); // do not restrict fit range during 1st pass
  std::cout << "################### PASS-1 ------> FitResultPtr=" << result1.Get() << std::endl;
  auto Qvalue_fit = funcPtr->GetParameter(1);
  auto Qvalue_fit_err = funcPtr->GetParError(1);
  if(result1.Get()) {
    auto factor = pow( Qvalue_fit_err, 2) + pow( Qvalue_expected_err, 2) +
      ( USE_EXPECTED_WIDTH ? pow(selection.expectedExcitationEnergySigmaInMeV_CMS, 2) : 0.0 ) +
      pow(myOptions.detectorQvalueResolutionInMeV_CMS, 2);
    auto chi2_formula = pow( Qvalue_fit - Qvalue_expected, 2) / factor;

    auto ndf_fit = result1->Ndf();
    auto chi2_fit = result1->Chi2() / ndf_fit;

    if(use_fixed_peak) {
      chi2 = chi2_fit;
    } else {
      chi2 = chi2_formula;
    }

    ////// DEBUG
    std::cout << __FUNCTION__ << ": 1st fit pass | method1 (FitResult)  chi2/ndf=" << chi2_fit << "  ndf=" << ndf_fit
	      << "  | method2 (formula)  chi2=" << chi2_formula << std::endl;
    ////// DEBUG
  }

  ////// DEBUG
  if(myOptions.debug) {
    std::cout << __FUNCTION__ << ": 1st fit pass " << (result1.Get() && result1->IsValid() ? "OK" : "FAILED")
	      << "  Qvalue_fit=" << Qvalue_fit
	      << "  Qvalue_mean=" << Qvalue_mean
	      << "  Qvalue_expected=" << Qvalue_expected
	      << "  (diff1=" << Qvalue_fit - Qvalue_expected << " +/- "
	      << sqrt(Qvalue_mean_err*Qvalue_fit_err+Qvalue_expected_err*Qvalue_expected_err)
	      << "  diff2=" << Qvalue_mean - Qvalue_expected << " +/- "
	      << sqrt(Qvalue_mean_err*Qvalue_mean_err+Qvalue_expected_err*Qvalue_expected_err) << ")" << std::endl;
  }
  ////// DEBUG

  auto sigma_left = funcPtr->GetParameter(2);
  auto sigma_right = ( USE_SHAPE_ASYMMETRY ? funcPtr->GetParameter(3) : sigma_left );
  auto sigma_ave = 0.5*( sigma_left + sigma_right );

  //////////////////
  //
  // 2nd pass with fit range restricted to a range corresponding to certain fraction of peak height (e.g. 50%, 80%, etc).
  //
  // set parameter constraints depending on CHI2 calculation strategy
  funcPtr->SetParLimits(0, 0.8*funcPtr->GetParameter(0), 1.2*funcPtr->GetParameter(0));
  funcPtr->SetParLimits(2, 0.5*Qvalue_RMS, 2*Qvalue_RMS); // [MeV]
  if(USE_SHAPE_ASYMMETRY) {
    funcPtr->SetParameters(funcPtr->GetParameter(0), funcPtr->GetParameter(1), sigma_left, sigma_right);
    funcPtr->SetParLimits(3, 0.5*Qvalue_RMS, 2*Qvalue_RMS); // [MeV]
  } else {
    funcPtr->SetParameters(funcPtr->GetParameter(0), funcPtr->GetParameter(1), sigma_ave);
  }
  if(use_fixed_peak) {
    funcPtr->FixParameter(1, Qvalue_expected);
  } else {
    funcPtr->SetParLimits(1, 0.8*funcPtr->GetParameter(1), 1.2*funcPtr->GetParameter(1));
  }

  if(result1.Get()) {
    auto xmin_fit = Qvalue_fit - fitSigmaFraction * sigma_left;
    auto xmax_fit = Qvalue_fit + fitSigmaFraction * sigma_right;
    funcPtr->SetRange(xmin_fit, xmax_fit);
    TFitResultPtr result2 = hQvalue.Fit("f_energyShapeGauss", "I Q M E 0 S", "", // 0=store, but do not draw
					xmin_fit, xmax_fit);
    std::cout << "################### PASS-2 ------> FitResultPtr=" << result2.Get() << std::endl;
    if(result2.Get()) {
      Qvalue_fit = funcPtr->GetParameter(1);
      Qvalue_fit_err = funcPtr->GetParError(1);
      auto factor = pow( Qvalue_fit_err, 2) + pow( Qvalue_expected_err, 2) +
	( USE_EXPECTED_WIDTH ? pow(selection.expectedExcitationEnergySigmaInMeV_CMS, 2) : 0.0 ) +
	pow(myOptions.detectorQvalueResolutionInMeV_CMS, 2);
      auto chi2_formula = pow( Qvalue_fit - Qvalue_expected, 2) / factor;

      auto ndf_fit = result2->Ndf();
      auto chi2_fit = result2->Chi2() / ndf_fit;

      if(use_fixed_peak) {
	chi2 = chi2_fit;
      } else {
	chi2 = chi2_formula;
      }

      ////// DEBUG
      std::cout << __FUNCTION__ << ": 2nd fit pass | method1 (FitResult)  chi2/ndf=" << chi2_fit << "  ndf=" << ndf_fit
		<< "  | method2 (formula)  chi2=" << chi2_formula << std::endl;
      ////// DEBUG
    }

    ////// DEBUG
    if(myOptions.debug) {
      std::cout << __FUNCTION__ << ": 2nd fit pass " << (result2.Get() && result2->IsValid() ? "OK" : "FAILED")
		<< "  Qvalue_fit=" << Qvalue_fit
		<< "  Qvalue_mean=" << Qvalue_mean
		<< "  Qvalue_expected=" << Qvalue_expected
		<< "  (diff1=" << Qvalue_fit - Qvalue_expected << " +/- "
		<< sqrt(Qvalue_mean_err*Qvalue_fit_err+Qvalue_expected_err*Qvalue_expected_err)
		<< "  diff2=" << Qvalue_mean - Qvalue_expected << " +/- "
		  << sqrt(Qvalue_mean_err*Qvalue_mean_err+Qvalue_expected_err*Qvalue_expected_err) << ")" << std::endl;
    }
    ////// DEBUG
  }

  // update sigmas
  sigma_left = funcPtr->GetParameter(2);
  sigma_right = ( USE_SHAPE_ASYMMETRY ? funcPtr->GetParameter(3) : sigma_left );

  ////// DEBUG
  if(myOptions.debug) std::cout << __FUNCTION__ << "  BEFORE PENALTIES  chi2=" << chi2
				<< "  npassed=" << npassed << std::endl;
  ////// DEBUG

  // add input histogram mean value out-of-range penalty term, where: penalty=(anomaly/resolution)^2
  chi2 += (Qvalue_mean>xmax ? pow((Qvalue_mean - xmax)/myOptions.detectorQvalueResolutionInMeV_CMS, 2) : 0.0);
  chi2 += (Qvalue_mean<xmin ? pow((Qvalue_mean - xmin)/myOptions.detectorQvalueResolutionInMeV_CMS, 2) : 0.0);

  // add fitted left-right sigma asymmetry penalty term, where: penalty=(asymmetry/THR-1)^2 for asymmetry>THR
  auto asymmetry = fabs(sigma_left - sigma_right) / (sigma_left + sigma_right); // range [0, 1]
  chi2 += (asymmetry>0.2  ? pow(asymmetry/0.2-1, 2) : 0.0); // asymmetry by factor up to 1.5 is acceptable

  // add fitted too-wide sigma penalty terms, where: penalty=(sigma/THR-1)^2 for sigma>THR
  auto ratio_left = sigma_left/Qvalue_RMS;
  auto ratio_right = sigma_right/Qvalue_RMS;
  chi2 += (ratio_left>1  ? pow(ratio_left-1, 2) : 0.0); // expected fitted sigma is smaller than histogram RMS
  chi2 += (ratio_right>1  ? pow(ratio_right-1, 2) : 0.0);

  ////// DEBUG
  if(debug_histos_flag) {
    auto it = myQvalueFitMap.find(selection.description);
    if(it!=myQvalueFitMap.end()) {
      hQvalue.SetTitle(it->first.c_str());
      hQvalue.SetXTitle("Q-value in CMS [MeV]");
      hQvalue.SetYTitle(Form("Events / %lg MeV", energyBinSize));
      it->second = std::make_tuple(hQvalue, *funcPtr, Qvalue_expected);
    }
  }
  ////// DEBUG

  ////// DEBUG
  if(myOptions.debug) std::cout << __FUNCTION__ << "  chi2=" << chi2
				<< "  npassed=" << npassed << std::endl;
  ////// DEBUG

  return std::make_tuple(chi2, npassed);
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// global chi^2 from all event collections corresponding
// to different reaction channels
double EnergyScale_analysis::getChi2(const bool debug_histos_flag) {

  const bool stat_weights_flag = USE_STAT_WEIGHT;
  double sumChi2=0.0;
  double sumW=0.0;
  int sumN=0;

  for(auto &selection: mySelection) {
    size_t npoints = 0U;
    double chi2 = 0.0;
    double W = 1.0;
    std::tie(chi2, npoints) = getSelectionChi2(selection, debug_histos_flag);
    assert(npoints>0);

    if(stat_weights_flag) {
      W = npoints;
    }
    sumChi2 += chi2 * W;
    sumW += W;
    sumN++;

    ////// DEBUG
    if(myOptions.debug) {
      std::cout<<__FUNCTION__<<": selection=\"" << enumDict::GetReactionName(selection.reaction)
	       << "\"  chi2/npoints=" << chi2/npoints << "  weight=" << W << std::endl;
    }
    ////// DEBUG
  }

  sumChi2 /= sumW; // weight partial chi2 contributions according to event counts
  sumChi2 *= sumN;

  ////// DEBUG
  if(myOptions.debug) {
    std::cout<<__FUNCTION__<<": Sum of weights=" << sumW << std::endl;
  }
  ////// DEBUG

  ////// DEBUG
  if(myOptions.debug) {
    std::cout<<__FUNCTION__<<": Sum of (weighted) chi2 from "<<mySelection.size()<<" reaction channels = "<<sumChi2<<std::endl;
  }
  ////// DEBUG

  return sumChi2 * myFactorChi2; // NOTE: for FUMILI minimizer the returned function should be: 0.5 * chi^2
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// helper function to set division factor for global CHI2
void EnergyScale_analysis::setMinimizerAlgorithm(const std::string &algorithm) {
  const std::string name(boost::algorithm::to_upper_copy(algorithm));
  myFactorChi2 = 1.0;
  if(name=="FUMILI2" || name=="FUMILI") myFactorChi2 = 0.5;
  std::cout << __FUNCTION__ << ": Setting global CHI2 scaling factor to " << myFactorChi2 << std::endl;
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// get parameter value from the last call of operator()
double EnergyScale_analysis::getParameter(size_t ipar) const {
  return lastPar.at(ipar);
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// indicator flag for external fitter that this parameter shall be fixed
bool EnergyScale_analysis::isParameterFixed(size_t ipar) const {
  return isFixedPar.at(ipar);
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
// actual implementation of the function to be minimized
double EnergyScale_analysis::operator() (const double *par) {

  initializeCorrections(myNparams, par);
  return getChi2();
}

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
void EnergyScale_analysis::plotQvalueFits(const std::string &filePrefix) {
  // sanity checks
  if(myQvalueFitMap.size()==0) {
    std::cout << __FUNCTION__ << ": Drawing fitted Q-value histograms requires non-empty map with histogram pointers" << std::endl;
    return;
  }
  getChi2(true);

  TFile f((filePrefix+".root").c_str(), "RECREATE");
  f.cd();
  TCanvas c("c", "", 800, 600);
  c.Print((filePrefix+".pdf[").c_str());
  int isel=0;
  for(auto &it: myQvalueFitMap) {
    auto h = std::get<0>(it.second); // TH1D
    auto f = std::get<1>(it.second); // TF1
    auto expected = std::get<2>(it.second); // expected Qvalue_CMS

    std::cout << "htitle=" << h.GetTitle() << " Qexp=" << expected << std::endl;

    if(h.GetEntries()==0 || f.GetNdim()!=1) continue;
    h.SetStats(true);
    gStyle->SetOptFit(1111); // pcev
    auto list=h.GetListOfFunctions();
    list->Clear();
    list->Add(&f); // associate f with histogram h
    auto xmin=h.GetMean()-4*h.GetRMS();
    auto xmax=h.GetMean()+4*h.GetRMS();
    auto ymin=h.GetMinimum();
    auto ymax=h.GetMaximum();
    auto ypeak=f.GetParameter(0);
    f.SetNpx((f.GetXmax()-f.GetXmin())/(xmax-xmin)*1000); // 1K points in the drawing window
    h.Draw();
    h.GetXaxis()->SetRangeUser(xmin, xmax);
    TLine l1(expected, ymin, expected, ymax);
    TLine l2(xmin, ypeak/2, xmax, ypeak/2);
    l1.SetLineColor(kBlue);
    l2.SetLineColor(kBlue);
    l1.SetLineStyle(kDashed);
    l2.SetLineStyle(kDashed);
    l1.Draw();
    l2.Draw();
    f.SetLineColor(kRed);
    h.SetDirectory(0);
    c.Print((filePrefix+".pdf").c_str());
    isel++;
    c.Write(Form("point_%d", isel), TObject::kOverwrite);
  }
  c.Print((filePrefix+".pdf]").c_str());
  f.Close();
}

///////////////////////////////////////////////////////////////////// - TO BE MOVED TO CommonDefinitions
///////////////////////////////////////////////////////////////////// - TO BE MOVED TO CommonDefinitions

namespace enumDict {
//Keep type definition and dictionary in unnamed namespace not to expose them
    namespace{
        typedef boost::bimap<::escale_type, std::string> EscaleDictionary;

        const EscaleDictionary gEScales =
                boost::assign::list_of<EscaleDictionary::relation>
	        (escale_type::UNKNOWN,                     "UNKNOWN")
	        (escale_type::NONE,                        "NONE")
                (escale_type::LENGTH,                      "LENGTH")
                (escale_type::ENERGY_LAB,                  "ENERGY_LAB")
                (escale_type::ENERGY_CMS,                  "ENERGY_CMS")
                (escale_type::ZENEK,                       "ZENEK")
                ;
    }

    escale_type GetEnergyScaleType(const std::string &scaleName) {
        auto it = gEScales.right.find(scaleName);
        return it == gEScales.right.end() ? escale_type::UNKNOWN : it->second;
    }

    std::string GetEnergyScaleName(escale_type type) {
        auto it = gEScales.left.find(type);
        return it == gEScales.left.end() ? "UNKNOWN" : it->second;
    }
}
///////////////////////////////////////////////////////////////////// - TO BE MOVED TO CommonDefinitions
///////////////////////////////////////////////////////////////////// - TO BE MOVED TO CommonDefinitions
