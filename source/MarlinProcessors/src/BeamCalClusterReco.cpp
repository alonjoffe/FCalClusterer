#include "BeamCalClusterReco.hh"
#include "ProcessorUtilities.hh"

#include "BCPCuts.hh"
#include "BCPadEnergies.hh"
#include "BCRecoObject.hh"
#include "BeamCal.hh"
#include "BeamCalCluster.hh"
#include "BCUtilities.hh"
#include "BeamCalGeoCached.hh"
#include "BeamCalBkg.hh"
#include "BeamCalBkgParam.hh"
#include "BeamCalBkgPregen.hh"
#include "BeamCalBkgGauss.hh"
#include "BeamCalBkgAverage.hh"
#include "BeamCalFitShower.hh"

//LCIO
#include <EVENT/LCCollection.h>
#include <EVENT/MCParticle.h>
#include <EVENT/ReconstructedParticle.h>
#include <EVENT/SimCalorimeterHit.h>
#include <UTIL/CellIDDecoder.h>
#include <IMPL/LCCollectionVec.h>
#include <IMPL/ReconstructedParticleImpl.h>
#include <IMPL/ClusterImpl.h>

// ----- include for verbosity dependent logging ---------
#include <streamlog/loglevels.h>
#include <streamlog/streamlog.h>

#include <marlin/ProcessorEventSeeder.h>
#include <marlin/Global.h>
#include <marlin/Exceptions.h>

//ROOT
#include <TCanvas.h>
#include <TChain.h>
#include <TCrown.h>
#include <TEfficiency.h>
#include <TFile.h>
#include <TH2F.h>
#include <TLine.h>
#include <TPaveText.h>
#include <TProfile.h>
#include <TRandom3.h>
#include <TStyle.h>
#include <TMarker.h>

//STDLIB
#include <numeric>
#include <iomanip>
#include <iostream>
#include <utility>
//#include <set>

using namespace lcio ;
using namespace marlin ;
using std::pair;

BeamCalClusterReco aBeamCalClusterReco ;

//Just for formatting /////////////////////////////////////////////////////////////////
#define LONGSTRING "                                                                  "
///////////////////////////////////////////////////////////////////////////////////////

class LackingFilesException :public std::runtime_error {
public:
LackingFilesException(std::string const& error) : std::runtime_error(error) { }
};

class WrongParameterException :public std::runtime_error {
public:
WrongParameterException(std::string const& error) : std::runtime_error(error) { }
};


BeamCalClusterReco::BeamCalClusterReco() : Processor("BeamCalClusterReco"),
                                           m_colNameMC(""),
                                           m_colNameBCal(""),
					   m_bgMethodName(""),
                                           m_files(),
                                           m_nEvt(0),
                                           m_specialEvent(-1),
                                           m_nBXtoOverlay(0),
                                           m_eventSide(-1),
                                           m_minimumTowerSize(0),
                                           m_startLookingInLayer(0),
                                           m_NShowerCountingLayers(0),
                                           m_usePadCuts(true),
					   m_useChi2Selection(false),
                                           m_createEfficienyFile(false),
                                           m_sigmaCut(1.0),
                                           m_TowerChi2ndfLimit(5.0),
                                           m_calibrationFactor(1.0),
                                           m_startingRings(),
                                           m_requiredRemainingEnergy(),
                                           m_requiredClusterEnergy(),
                                           m_BCG(NULL),
                                           m_bcpCuts(NULL),
					   m_BCbackground(NULL),
                                           m_totalEfficiency(NULL),
                                           m_thetaEfficieny(NULL),
                                           m_phiEfficiency(NULL),
                                           m_twoDEfficiency(NULL),
                                           m_phiFake(NULL),
                                           m_thetaFake(NULL),
                                           m_checkPlots(0),
                                           m_originalParticles(0),
                                           m_MCinBeamCal(NULL),
                                           m_BCalClusterColName(""),
                                           m_BCalRPColName(""),
                                           m_EfficiencyFileName(""),
                                           m_usingDD4HEP(false)
{

// modify processor description
  _description = "BeamCalClusterReco reproduces the beamstrahlung background for a given number of " \
    "bunch-crossings NumberOfBX and puts the signal hits from the "	\
    "lcio input file on top of that, and then clustering is attempted." ;



 registerInputCollection( LCIO::MCPARTICLE,
			  "MCParticleCollectionName" ,
			  "MCParticle Collection Name, only needed and used to estimate efficiencies",
			  m_colNameMC ,
			  std::string("MCParticle") ) ;

 registerInputCollection( LCIO::SIMCALORIMETERHIT,
			  "BeamCalCollectionName" ,
			  "Name of BeamCal Collection"  ,
			  m_colNameBCal ,
			  std::string("BeamCalCollection") ) ;

 registerOutputCollection( LCIO::RECONSTRUCTEDPARTICLE,
			   "RecoParticleCollectionname" ,
			   "Name of the Reconstructed Particle collection"  ,
			   m_BCalRPColName,
			   std::string("BCalRecoParticle") ) ;

 registerOutputCollection( LCIO::CLUSTER,
			   "RecoClusterCollectionname" ,
			   "Name of the Reconstructed Cluster collection"  ,
			   m_BCalClusterColName ,
			   std::string("BCalClusters") ) ;

registerProcessorParameter ("BackgroundMethod",
			      "How to estimate background [Gaussian, Parametrised, Pregenerated, Averaged]",
			      m_bgMethodName,
			      std::string("Gaussian") ) ;

std::vector<std::string> defaultFile;
defaultFile.push_back("BeamCal.root");
registerProcessorParameter ("InputFileBackgrounds",
			      "Root Inputfile(s)",
			      m_files,
			      defaultFile ) ;

registerProcessorParameter ("NumberOfBX",
			      "Number of Bunch Crossings of Background",
			      m_nBXtoOverlay,
			      int(1) ) ;

std::vector<float> startingRing, padCut, clusterCut;
startingRing.push_back(0.0);  padCut.push_back(0.5);  clusterCut.push_back(3.0);
startingRing.push_back(1.0);  padCut.push_back(0.3);  clusterCut.push_back(2.0);
startingRing.push_back(2.0);  padCut.push_back(0.2);  clusterCut.push_back(1.0);

registerProcessorParameter ("UseConstPadCuts",
			      "Use the cuts for the pads specified in ETPad. If false, the standard deviation of each pad times the SigmaCut Factor is used, the first entry in ETPad is used as a minimum energy to consider a pad at all",
			      m_usePadCuts,
			      true ) ;

registerProcessorParameter ("SigmaCut",
			      "If not using ConstPadCuts, each pad SigmaCut*standardDeviation is considered for clusters",
			      m_sigmaCut,
			      double(1.0) ) ;

registerProcessorParameter ("StartingRing",
			      "Rings from which onwards the outside Thresholds are used",
			      m_startingRings,
			      startingRing ) ;

registerProcessorParameter ("ETPad",
			      "Energy in a Pad, after subtraction of background required to consider it for signal",
			      m_requiredRemainingEnergy,
			      padCut ) ;

registerProcessorParameter ("ETCluster",
			      "Energy in a Cluster to consider it an electron",
			      m_requiredClusterEnergy,
			      clusterCut) ;

registerProcessorParameter ("MinimumTowerSize",
			      "Minimum number of pads in a single tower to be considered for signal",
			      m_minimumTowerSize,
			      int(4) ) ;

registerProcessorParameter ("StartLookingInLayer",
			      "Layer (inclusive) from which on we start looking for signal clusters",
			      m_startLookingInLayer,
			      int(10) ) ;

registerProcessorParameter ("NShowerCountingLayers",
			      "How many layers are used for shower fitting",
			      m_NShowerCountingLayers,
			      int(3) ) ;

registerProcessorParameter ("LinearCalibrationFactor",
			      "Multiply deposit energy by this factor to account for samplif fraction",
			      m_calibrationFactor,
			      double(1.0) ) ;

registerProcessorParameter ("UseChi2Selection",
			      "Use Chi2 selection criteria to detect high energy electron in the signal.",
			      m_useChi2Selection,
			      false ) ;

registerProcessorParameter ("TowerChi2ndfLimit",
			      "Limit on square norm of tower energy chi2/ndf, where chi2 = (E_dep - E_bg)^2/sig^2. \
			      Reasonable value for pregenerated bkg is 5., for parametrised is 2.",
			      m_TowerChi2ndfLimit,
			      double(5.0) ) ;


registerProcessorParameter ("CreateEfficiencyFile",
			    "Flag to create the TEfficiency for fast tagging library",
			    m_createEfficienyFile,
			    false ) ;

registerProcessorParameter ("EfficiencyFilename",
			    "The name of the rootFile which will contain the TEfficiency objects",
			    m_EfficiencyFileName,
			    std::string("TaggingEfficiency.root") ) ;

registerProcessorParameter ("PrintThisEvent",
			    "Number of Event that should be printed to PDF File",
			    m_specialEvent,
			    int(-1) ) ;


}

void BeamCalClusterReco::init() {

  Global::EVENTSEEDER->registerProcessor(this);

  // usually a good idea to
  if( streamlog::out.write< MESSAGE3 >() ) {
    printParameters() ;
  }

  m_nEvt = 0;

#pragma message "FIXME: Allow for possibility to use precomputed sigma file"
#pragma message "FIXME: Need energy calibration"

  if ( (m_startingRings.size() != m_requiredClusterEnergy.size() ) ||
       (m_requiredClusterEnergy.size() != m_requiredRemainingEnergy.size())){
    throw WrongParameterException("== Error From BeamCalClusterReco == The number of starting rings and required" \
				  "cluster energy or pad energy are not the same!");
  }
  if( m_startingRings[0] != 0 ) {
    throw WrongParameterException("== Error From BeamCalClusterReco == startingRings must always start with 0");
  }

  m_BCG = ProcessorUtilities::getBeamCalGeo(m_usingDD4HEP);

  // select which background we have
  if(      string("Pregenerated") == m_bgMethodName ) {
    m_BCbackground = new BeamCalBkgPregen(m_bgMethodName, m_BCG);
  } else if( string("Gaussian") == m_bgMethodName ) {
    m_BCbackground = new BeamCalBkgGauss(m_bgMethodName, m_BCG);
  } else if( string("Parametrised") == m_bgMethodName ) {
    m_BCbackground = new BeamCalBkgParam(m_bgMethodName, m_BCG);
  } else if( string("Averaged")     == m_bgMethodName) {
    m_BCbackground = new BeamCalBkgAverage(m_bgMethodName, m_BCG);
  } else {
    streamlog_out(ERROR) <<"== Error From BeamCalBackground == "
			 << "Unknown BeamCal method of background esitmation \""
			 << m_bgMethodName << "\"" << std::endl;
    throw std::runtime_error("Unknown BeamCal background method");
  }

  //Fill BCPCuts object with cuts from the processor parameters
  m_bcpCuts = new BCPCuts(m_startingRings,
			  m_requiredRemainingEnergy, m_requiredClusterEnergy,
			  m_minimumTowerSize,
			  m_startLookingInLayer,
			  m_NShowerCountingLayers,
			  m_usePadCuts,
			  m_sigmaCut);

  m_BCbackground->setBCPCuts(m_bcpCuts);
  m_BCbackground->init(m_files, m_nBXtoOverlay);

  //Create Efficiency Objects if required
  if(m_createEfficienyFile) {
    const double //angles in mrad
      minAngle(0.9*m_BCG->getBCInnerRadius()/m_BCG->getBCZDistanceToIP()*1000), 
      maxAngle(1.1*m_BCG->getBCOuterRadius()/m_BCG->getBCZDistanceToIP()*1000); 
    const int bins = 50;
    m_totalEfficiency = new TEfficiency("totalEff","Total detector efficiency", 1, 
      minAngle/0.9, maxAngle/1.1);
    m_thetaEfficieny = new TEfficiency("thetaEff","Efficiency vs. #Theta", bins, minAngle, maxAngle);
    m_phiEfficiency  = new TEfficiency("phiEff","Efficiency vs. #Phi", 72, 0, 360);
    m_twoDEfficiency = new TEfficiency("TwoDEff","Efficiency vs. #Theta and #Phi", bins, minAngle, maxAngle, 72, 0, 360);
    m_phiFake  = new TEfficiency("phiFake","Fake Rate vs. #Phi", 72, 0, 360);
    m_thetaFake = new TEfficiency("thetaFake","Fake Rate vs. #Theta", bins, minAngle, maxAngle);

    /* 0*/  m_checkPlots.push_back( new TH1D("energyReal","Energy;Energy [GeV];N",100, 0, 30) );
    /* 1*/  m_checkPlots.push_back( new TH1D("energyFake","Energy;Energy [GeV];N",100, 0, 30) );
    /* 2*/  m_checkPlots.push_back( new TH1D("clusterReal","Cluster; Cluster; N",40, 0, 40) );
    /* 3*/  m_checkPlots.push_back( new TH1D("clusterFake","Cluster; Cluster; N",40, 0, 40) );
    /* 4*/  m_checkPlots.push_back( new TH2D("EvClusterReal","Energy vs. N_{Pads};Energy [GeV]; N_{Pads}",100, 0, 20, 40, 0, 40) );
    /* 5*/  m_checkPlots.push_back( new TH2D("EvClusterFake","Energy vs. N_{Pads};Energy [GeV]; N_{Pads}",100, 0, 20, 40, 0, 40) );
    /* 6*/  m_checkPlots.push_back( new TH2D("EnergyRing","Energy vs. Theta;Energy [GeV]; #theta [mrad",100, 0, 20, 60, 0, 60) );
    /* 7*/  m_checkPlots.push_back( new TH1D("thetaReal","Theta;#theta [mrad];N",200, 0, 40) );
    /* 8*/  m_checkPlots.push_back( new TH1D("phiReal","Phi;#phi [deg];N",200, 0, 360) );
    /* 9*/  m_checkPlots.push_back( new TH1D("thetaDiff","Delta Theta;#Delta#theta [mrad];N",100, -5, 5) );
    /*10*/  m_checkPlots.push_back( new TH1D("phiDiff","Phi;#Delta#phi [deg];N",100, -20, 20) );
    /*11*/  m_checkPlots.push_back( new TH2D("spatialRes","Spatial resolution;#Delta(#phi*R) [mm];#Delta R [mm]",60, -30, 30, 60, -30, 30) );
    /*12*/  m_checkPlots.push_back( new TH2D("dRvsR","dR vs R;R [mm];#Delta R [mm]", 65, 20, 150, 60, -30, 30) );
    /*13*/  m_checkPlots.push_back( new TH2D("dphiRvsR","d(phi*R) vs R;R [mm];#Delta(#phi*R) [mm]",65, 20, 150, 60, -30, 30) );
    /*14*/  m_checkPlots.push_back( new TH2D("dphivsR","d(phi) vs R;R [mm];#Delta(#phi) [deg]",65, 20, 150, 100, -20, 20) );
    /*15*/  m_checkPlots.push_back( new TProfile("EvsTheta_profile", "E vs Theta", bins, minAngle, maxAngle, 0., 30.));

  }//Creating Efficiency objects



}//init

void BeamCalClusterReco::processRunHeader( LCRunHeader*) {
  //  streamlog_out (DEBUG) << "Runnumber "<< _nRun << std::endl;
  //   if(_nRun % 4 == 0) {
}

void BeamCalClusterReco::processEvent( LCEvent * evt ) {

  LCCollection  *colBCal;

  try {
    colBCal = evt->getCollection( m_colNameBCal ) ;
  } catch (Exception &e) {
    colBCal = NULL;
  }

  m_BCbackground->setRandom3Seed(m_nEvt+Global::EVENTSEEDER->getSeed(this));

  BCPadEnergies padEnergiesLeft(m_BCG, BCPadEnergies::kLeft);
  BCPadEnergies padEnergiesRight(m_BCG, BCPadEnergies::kRight);
  BCPadEnergies padAveragesLeft(m_BCG, BCPadEnergies::kLeft);
  BCPadEnergies padAveragesRight(m_BCG, BCPadEnergies::kRight);
  BCPadEnergies padErrorsLeft(m_BCG, BCPadEnergies::kLeft);
  BCPadEnergies padErrorsRight(m_BCG, BCPadEnergies::kRight);

  m_BCbackground->getEventBG(padEnergiesLeft, padEnergiesRight);
  m_BCbackground->getAverageBG(padAveragesLeft, padAveragesRight);
  m_BCbackground->getErrorsBG(padErrorsLeft, padErrorsRight);

  streamlog_out(DEBUG4) << "*************** Event " << std::setw(6) << m_nEvt << " ***************" << std::endl;

  // Some event classification variables
  double depositedEnergy(0);
  double maxDeposit(0.0);
  int maxLayer(0);

  // add the energy in the event to the background/average energy
  if(colBCal) {
    CellIDDecoder<SimCalorimeterHit> mydecoder(colBCal);
    int nHits = colBCal->getNumberOfElements();
    for(int i=0; i < nHits; i++) {
      SimCalorimeterHit *bcalhit = static_cast<SimCalorimeterHit*>(colBCal->getElementAt(i));
      int side, layer, ring, sector;
      BCUtil::DecodeCellID(mydecoder, bcalhit, side, layer, ring, sector, m_usingDD4HEP);
      const float energy = bcalhit->getEnergy();
      depositedEnergy += energy;

      if(maxDeposit < energy) {
	maxDeposit = energy;
	maxLayer = layer;
      }

      try{
	if(side == BCPadEnergies::kLeft) {
	  padEnergiesLeft.addEnergy(layer, ring, sector, energy);
	} else if(side == BCPadEnergies::kRight) {
	  padEnergiesRight.addEnergy(layer, ring, sector, energy);
	}
      } catch (std::out_of_range &e){

	streamlog_out(DEBUG1) << "Filling from signal: " << e.what()
			      << std::setw(10) << layer
			      << std::setw(10) << ring
			      << std::setw(10) << sector
			      << std::endl;
      }

    }//for all entries in the collection
  }//if there were hits from the signal

  // Run the clustering
  std::vector<BCRecoObject*> LeftSide,  RightSide;

  if ( ! m_useChi2Selection ) {
    LeftSide = FindClusters(padEnergiesLeft,  padAveragesLeft,  padErrorsLeft,  "Sig 6 L");
    RightSide= FindClusters(padEnergiesRight, padAveragesRight, padErrorsRight, "Sig 6 R");
  } else {
    LeftSide = FindClustersChi2(padEnergiesLeft,  padAveragesLeft,  padErrorsLeft,  "Chi2 6 L");
    RightSide= FindClustersChi2(padEnergiesRight, padAveragesRight, padErrorsRight, "Chi2 6 R");
  }

  //merge the two list of clusters so that we can run in one loop
  LeftSide.insert( LeftSide.end(), RightSide.begin(), RightSide.end() );

  if(m_createEfficienyFile) {
    findOriginalMCParticles(evt);
  }

  if(m_createEfficienyFile) {
    fillEfficiencyObjects(LeftSide);
  }

  if( (streamlog::out.write< DEBUG3 >() && m_nEvt == m_specialEvent ) ) {
    printBeamCalEventDisplay(padEnergiesLeft, padEnergiesRight, maxLayer, maxDeposit, depositedEnergy, LeftSide);
  }//DEBUG

  /////////////////////////////////////////////////////////
  // Add the found objects to the RecoParticleCollection //
  /////////////////////////////////////////////////////////

  LCCollectionVec* BCalClusterCol = new LCCollectionVec(LCIO::CLUSTER);
  LCCollectionVec* BCalRPCol = new LCCollectionVec(LCIO::RECONSTRUCTEDPARTICLE);

  for (std::vector<BCRecoObject*>::iterator it = LeftSide.begin(); it != LeftSide.end(); ++it) {

    // Create Reconstructed Particles and Clusters from the BCRecoObjects" )
    const float energyCluster(m_calibrationFactor * (*it)->getEnergy());
    const float thetaCluster((*it)->getThetaRad());
    const float phiCluster((*it)->getPhi()*TMath::DegToRad());

    const float mass = 0.0;
    const float charge = 1e+19;
    const float mmBeamCalDistance( m_BCG->getLayerZDistanceToIP(m_startLookingInLayer) );

#pragma message "make sure rotation to labframe works on both sides and we are not rotating something weird"
    //which side the Cluster is on
    double sideFactor = ((*it)->getSide()==BCPadEnergies::kLeft) ? +1.0 : -1.0;

    float tempPos[3] = { float(mmBeamCalDistance * sin ( thetaCluster ) * cos ( phiCluster )),
			 float(mmBeamCalDistance * sin ( thetaCluster ) * sin ( phiCluster )),
			 float(mmBeamCalDistance * cos ( thetaCluster ) * sideFactor) };

    float tempMom[3] = { float(energyCluster * sin ( thetaCluster ) * cos ( phiCluster )),
			 float(energyCluster * sin ( thetaCluster ) * sin ( phiCluster )),
			 float(energyCluster * cos ( thetaCluster ) * sideFactor) };

    const double halfCrossingAngleMrad(m_BCG->getCrossingAngle()*0.5);
    float position[3] = {0.0, 0.0, 0.0};
    float momentumCluster[3] = {0.0, 0.0, 0.0};

    BCUtil::RotateToLabFrame(tempPos, position, halfCrossingAngleMrad);
    BCUtil::RotateToLabFrame(tempMom, momentumCluster, halfCrossingAngleMrad);

    ClusterImpl* cluster = new ClusterImpl;
    cluster->setEnergy( energyCluster );
    cluster->setPosition( position );

    ReconstructedParticleImpl* particle = new ReconstructedParticleImpl;
    particle->setMass( mass ) ;
    particle->setCharge( charge ) ;
    particle->setMomentum ( momentumCluster ) ;
    particle->setEnergy ( energyCluster ) ;
    particle->addCluster( cluster ) ;

    BCalClusterCol->addElement(cluster);
    BCalRPCol->addElement(particle);

    //CleanUp
    delete *it;
  }//for all found clusters

  ///////////////////////////////////////
  // Done with Running Reco Clustering //
  ///////////////////////////////////////
  
  if( BCalClusterCol->getNumberOfElements() != 0 ) {
    evt->addCollection(BCalClusterCol, m_BCalClusterColName);
    evt->addCollection(BCalRPCol, m_BCalRPColName);
  } else {
    delete BCalClusterCol;
    delete BCalRPCol;
  }

  m_nEvt++ ;

  /*
  if (m_nEvt > 1000) this->end();

  throw RewindDataFilesException(this);
  */


}//processEvent


void BeamCalClusterReco::fillEfficiencyObjects(const std::vector<BCRecoObject*>& RecoedObjects) {

  if( not m_createEfficienyFile ) return;

  //Try to match all clusters and particles, only then can we fill our efficiencies,
  //this should allow one in principle to estimate efficiencies when there are
  //multiple particles or cluster, but should be checked!!

  //See if we have a cluster matching one of the MCParticles
  for (std::vector<BCRecoObject*>::const_iterator it = RecoedObjects.begin(); it != RecoedObjects.end(); ++it) {
    BCRecoObject* bco = *it;

    bool hasRightCluster = false;
    const double theta(bco->getThetaMrad());
    const double phi(bco->getPhi());

    for (std::vector<OriginalMC>::iterator mcIt = m_originalParticles.begin(); mcIt != m_originalParticles.end(); ++mcIt) {
      if (BCUtil::areCloseTogether(theta, phi, (*mcIt).m_theta, (*mcIt).m_phi )) {
	hasRightCluster = true;
	bco->setOMC(mcIt-m_originalParticles.begin());
	(*mcIt).m_wasFound = true;
	break;
      }
    }

    bco->setHasRightCluster(hasRightCluster);
    streamlog_out(MESSAGE2) << "Have we found a cluster matching a particle? "
			    << std::boolalpha << hasRightCluster 
			    << std::endl;
  }


  //Here we fill the efficiency for reconstructing MCParticles
  for (std::vector<OriginalMC>::iterator mcIt = m_originalParticles.begin(); mcIt != m_originalParticles.end(); ++mcIt) {
    OriginalMC const& omc = (*mcIt);
    if (m_MCinBeamCal) {
      m_totalEfficiency->Fill( omc.m_wasFound, omc.m_theta);
    }
    m_thetaEfficieny->Fill( omc.m_wasFound, omc.m_theta);
    m_phiEfficiency->Fill ( omc.m_wasFound, omc.m_phi);
    m_twoDEfficiency->Fill( omc.m_wasFound, omc.m_theta, omc.m_phi);
    streamlog_out(MESSAGE2) << "Particle was found? " << std::boolalpha << omc.m_wasFound 
			    << std::setw(13) << omc.m_theta
			    << std::setw(13) << omc.m_phi
			    << std::setw(13) << omc.m_energy
			    << std::endl;
  }

  //Here we fill the fake rate, for reconstructed clusters that do not have an MCParticle
  bool foundFake(false);
  for (std::vector<BCRecoObject*>::const_iterator it = RecoedObjects.begin(); it != RecoedObjects.end(); ++it) {
    BCRecoObject* bco = *it;
    const bool hasRightCluster(bco->hasRightCluster());
    const double theta(bco->getThetaMrad());
    const double phi(bco->getPhi());
    if (not hasRightCluster) {
      m_thetaFake->Fill(true, theta);
      m_phiFake->Fill(true, phi);
      foundFake = true;
    }
  }

  if (not foundFake) {
    const int nbins   = m_thetaFake->GetTotalHistogram()->GetNbinsX();
    const double low  = m_thetaFake->GetTotalHistogram()->GetBinLowEdge(1);
    const double high = m_thetaFake->GetTotalHistogram()->GetBinLowEdge(nbins+1);
    const double step = (high-low)/double(nbins);
    for (int i = 1; i <= nbins ;++i) {
      m_thetaFake->Fill( false, i * step + step/2.0 + low );
    }
  }


  for (std::vector<BCRecoObject*>::const_iterator it = RecoedObjects.begin(); it != RecoedObjects.end(); ++it) {
    BCRecoObject* bco = *it;

    if( bco->hasRightCluster() ) {
      m_checkPlots[0]->Fill(bco->getEnergy() );
      m_checkPlots[2]->Fill(bco->getNPads() );
      m_checkPlots[4]->Fill(bco->getEnergy(), bco->getNPads() );

      //Angles
      m_checkPlots[7] ->Fill(bco->getThetaMrad());
      m_checkPlots[8] ->Fill(bco->getPhi());
      OriginalMC const& omc = m_originalParticles[bco->getOMC()];
      m_checkPlots[9] ->Fill(omc.m_theta - bco->getThetaMrad());
      m_checkPlots[10]->Fill(omc.m_phi   - bco->getPhi());

      double omcR = omc.m_theta*m_BCG->getBCZDistanceToIP()/1000;
      double R = bco->getThetaMrad()*m_BCG->getBCZDistanceToIP()/1000;
      m_checkPlots[11]->Fill((TMath::DegToRad())*(omc.m_phi - bco->getPhi())*omcR, omcR-R);
      m_checkPlots[12]->Fill(omcR, omcR-R);
      m_checkPlots[13]->Fill(omcR, (TMath::DegToRad())*(omc.m_phi - bco->getPhi())*omcR);
      m_checkPlots[14]->Fill(omcR, (TMath::DegToRad())*(omc.m_phi - bco->getPhi()));
      m_checkPlots[15]->Fill(bco->getThetaMrad(), bco->getEnergy());

    } else if( bco->hasWrongCluster() )  {
      m_checkPlots[1]->Fill(bco->getEnergy() );
      m_checkPlots[3]->Fill(bco->getNPads() );
      m_checkPlots[5]->Fill(bco->getEnergy(), bco->getNPads() );
      m_checkPlots[6]->Fill(bco->getEnergy(), bco->getThetaMrad() );
    }

  }

}//fillEfficiencyObjects




void BeamCalClusterReco::check( LCEvent * ) {
  // nothing to check here - could be used to fill checkplots in reconstruction processor
}


void BeamCalClusterReco::end(){

  streamlog_out ( MESSAGE4 ) << __PRETTY_FUNCTION__ << " " << name()
			     << " processed " << m_nEvt << " events."
			     << std::endl ;


  if(m_createEfficienyFile) {
    TFile *effFile = TFile::Open(m_EfficiencyFileName.c_str(),"RECREATE");
    m_totalEfficiency->Write();
    m_thetaEfficieny->Write();
    m_phiEfficiency->Write();
    m_twoDEfficiency->Write();
    m_phiFake->Write();
    m_thetaFake->Write();

    for (size_t j = 0; j < m_checkPlots.size() ;++j) {
      m_checkPlots[j]->Write();
      delete m_checkPlots[j];
    }//all plots

    effFile->Close();
    delete effFile;

    delete m_totalEfficiency;
    delete m_thetaEfficieny;
    delete m_phiEfficiency;
    delete m_twoDEfficiency;
    delete m_phiFake;
    delete m_thetaFake;

  }

  m_checkPlots.clear();

  delete m_BCG;
  delete m_BCbackground;
  delete m_bcpCuts;

}



std::vector<BCRecoObject*> BeamCalClusterReco::FindClusters(const BCPadEnergies& signalPads,
							    const BCPadEnergies& backgroundPads,
							    const BCPadEnergies& backgroundSigma,
							    const TString& title) {

  std::vector<BCRecoObject*> recoVec;

  //////////////////////////////////////////
  // This calls the clustering function!
  //////////////////////////////////////////
  const std::vector<BeamCalCluster> &bccs =
    signalPads.lookForNeighbouringClustersOverWithVetoAndCheck(backgroundPads, backgroundSigma, *m_bcpCuts);
  const bool isRealParticle = false; //always false here, decide later

  for (std::vector<BeamCalCluster>::const_iterator it = bccs.begin(); it != bccs.end(); ++it) {

    streamlog_out(MESSAGE2) << title;
    if(signalPads.getSide() == BCPadEnergies::kRight) streamlog_out(MESSAGE2) << LONGSTRING;
    streamlog_out(MESSAGE2) << " " << (*it);
    if(signalPads.getSide() == BCPadEnergies::kLeft) streamlog_out(MESSAGE2) << LONGSTRING;

    //Apply cuts on the reconstructed clusters, then calculate angles
    if ( ( it->getNPads() > 2 ) && m_bcpCuts->isClusterAboveThreshold( (*it) ) ) {

      double theta(it->getTheta());
      double phi  (it->getPhi());

      streamlog_out(MESSAGE2) << " found something "
			      << std::setw(10) << theta
			      << std::setw(10) << phi
	;//ending the streamlog!

      recoVec.push_back( new BCRecoObject(isRealParticle, true, theta, phi, it->getEnergy(), it->getNPads(), signalPads.getSide() ) );

    }//if we have enough pads and energy in the clusters

    //Finish the output line
    streamlog_out(MESSAGE2) << std::endl;

  }//clusterloop

  return recoVec;

}//tryReco6


/**
* @brief Method of cluster searching by the chi2 criteria
*
*
* @param signalPads Signal energy depositions
* @param backgroundPads Background energy depositions
* @param backgroundSigma Standard deviation of the background
* @param title Some title?
*
* @return A pointer to vector of BeamCal reconstruction objects.
*/
std::vector<BCRecoObject*> BeamCalClusterReco::FindClustersChi2(const BCPadEnergies& signalPads,
							    const BCPadEnergies& backgroundPads,
							    const BCPadEnergies& backgroundSigma,
							    const TString& title) 
{
//  using BeamCalFitShower::EdepProfile_t;

  std::vector<BCRecoObject*> recoVec;
  const bool isRealParticle = false; //always false here, decide later

  vector<EdepProfile_t*> edep_prof; // energy profile for the calorimeter

  vector<double> te_signal, te_bg, te_sigma;
  int ndf(m_BCG->getBCLayers());
  // loop over towers
  for (int it = 0; it < m_BCG->getPadsPerLayer(); it++){
    // get tower energies, average, sigma
    signalPads.getTowerEnergies(it, te_signal);
    backgroundPads.getTowerEnergies(it, te_bg);
    backgroundSigma.getTowerEnergies(it, te_sigma);
    ndf = m_BCG->getBCLayers() - m_startLookingInLayer;

    // sums of signal and background along the tower
    double te_signal_sum(0.), te_bg_sum(0.);
    double tot_te_sigma(0.); // st.dev. for sum of the energies in the tower
    m_BCbackground->getTowerErrorsBG(it, signalPads.getSide(), tot_te_sigma);

    // calculate chi2 for this tower in all layers starting from defined
    double chi2(0.);
    for (int il = m_startLookingInLayer; il< m_BCG->getBCLayers(); il++){
      te_signal_sum += te_signal[il];
      te_bg_sum += te_bg[il];
      chi2+= pow((te_signal[il] - te_bg[il])/te_sigma[il],2);
    }

    // calculate sums for this tower in counting layers
    te_signal_sum = 0.;
    te_bg_sum = 0.;
    for (int il = m_startLookingInLayer; il< m_startLookingInLayer+m_NShowerCountingLayers; il++){
      te_signal_sum += te_signal[il];
      te_bg_sum += te_bg[il];
    }

    // create element of energy deposition profile
    EdepProfile_t *ep = new EdepProfile_t;
    ep->id = it;
    ep->towerChi2 = chi2;
    ep->totalEdep = te_signal_sum;
    ep->bkgEdep = te_bg_sum;
    ep->bkgSigma = tot_te_sigma;
    //std::cout << it<< "\t" <<chi2 << "\t" <<te_signal_sum-te_bg_sum<< std::endl;

    edep_prof.push_back(ep);
  }

  // create shower fitter for this profile
  BeamCalFitShower shower_fitter(edep_prof, signalPads.getSide());
  shower_fitter.setGeometry(m_BCG);
  shower_fitter.setBackground(m_BCbackground);
  shower_fitter.setStartLayer(m_startLookingInLayer);
  shower_fitter.setCountingLayers(m_NShowerCountingLayers);
  shower_fitter.setTowerChi2Limit(m_TowerChi2ndfLimit*ndf);

  shower_fitter.setEshwrLimit(m_requiredClusterEnergy.at(0));

  // Extract fitted showers untill nothing left above some threshold
  while(1){
    double theta(0.), phi(0.), en_shwr(0.), chi2_shwr(0.);
    double shwr_prob = shower_fitter.fitShower(theta, phi, en_shwr, chi2_shwr);
    if (shwr_prob < 0. ) break;
    // if the shower energy is above threshold, create reco object
    if (en_shwr > m_requiredClusterEnergy.at(0) ) {
      // fill the recoVec entry
      recoVec.push_back( new BCRecoObject(isRealParticle, true, 
        theta, phi, en_shwr, m_NShowerCountingLayers, 
	signalPads.getSide() ) );

      // print the log message
      streamlog_out(MESSAGE2) << title;
      if(signalPads.getSide() == BCPadEnergies::kRight) streamlog_out(MESSAGE2) << LONGSTRING;
      if(signalPads.getSide() == BCPadEnergies::kLeft) streamlog_out(MESSAGE2) << LONGSTRING;
//      streamlog_out(MESSAGE2) << "\nParticle candidate(s) found with shower energy above threshold: \n";
      streamlog_out(MESSAGE2) << "\nFound BeamCal particle: \t\t    " 
                              << std::setw(13) << theta 
			      << std::setw(13) << phi
                              << std::setw(13) << en_shwr << std::endl;
			      /*
                              << ";\twith chi2/ndf, p-value: " << std::setw(10) << chi2_shwr 
                              << "/" << te_signal.size() - m_startLookingInLayer << std::setw(14) << shwr_prob << std::endl;*/
    }
  }

  // clean
  while (edep_prof.size() != 0 ){
    delete edep_prof.back();
    edep_prof.pop_back();
  }

  return recoVec;
}

void BeamCalClusterReco::printBeamCalEventDisplay(BCPadEnergies& padEnergiesLeft, BCPadEnergies& padEnergiesRight,
						  int maxLayer, double maxDeposit, double depositedEnergy,
						  const std::vector<BCRecoObject*> & RecoedObjects) const {

  BCPadEnergies *padEnergies, *padErrors, *padAverages;

  BCPadEnergies padAveragesLeft(m_BCG, BCPadEnergies::kLeft);
  BCPadEnergies padAveragesRight(m_BCG, BCPadEnergies::kRight);
  BCPadEnergies padErrorsLeft(m_BCG, BCPadEnergies::kLeft);
  BCPadEnergies padErrorsRight(m_BCG, BCPadEnergies::kRight);

  m_BCbackground->getAverageBG(padAveragesLeft, padAveragesRight);
  m_BCbackground->getErrorsBG(padErrorsLeft, padErrorsRight);

  if( m_eventSide == BCPadEnergies::kLeft ) {
    padEnergies = &padEnergiesLeft;
    padAverages = &padAveragesLeft;
    padErrors   = &padErrorsLeft;
  } else {
    padEnergies = &padEnergiesRight;
    padAverages = &padAveragesRight;
    padErrors   = &padErrorsRight;
  }

  ///////////////////////////////////////
  // Deal with the Canvas and its pads //
  ///////////////////////////////////////
  TCanvas canv("canv","canv", 3200, 1600);
  gStyle->SetOptStat(0.0);
  TPad *pads[9];
  Double_t padSizeX = canv.GetWindowWidth()/4.0;
  Double_t padSizeY = (canv.GetWindowHeight()-200)/2.0;
  for (int i = 0; i < 8 ;++i) {
    Double_t lowX, lowY, highX, highY;

    lowX  = padSizeX *	double(int(i%4))    / 3200.;
    highX = padSizeX *	double(int(i%4)+1)  / 3200.;

    lowY  = (padSizeY *	double(int(i/4))   + 200.) / 1600.;
    highY = (padSizeY *	double(int(i/4)+1) + 200.) / 1600.;

    pads[i] = new TPad(Form("Pad%i",i+1), Form("Pad%i",i+1), lowX, lowY, highX, highY,
		       kWhite, short(0), short(0));
    pads[i]->SetNumber( ( i+1 < 5) ? i+1 + 4 : i+1 - 4 );//to start at the top... DONT TOUCH THIS EVER!
    pads[i]->Draw();
  }
  pads[8] = new TPad("TextPad","Text", 0, 0., 1., 1./9., kWhite, 0, 0);
  pads[8]->SetNumber(9);
  pads[8]->Draw();

  //Draw one before and two after maxLayer
  if(maxDeposit < 0.01) maxLayer = 10;
  const int startLayer = (maxLayer - 1 <= 37) ? maxLayer - 1 : 37 ;
  ////////////////////////
  // Deal with the data //
  ////////////////////////

  BeamCal bc(*m_BCG);
  bc.SetLogz(1);
  bc.SetBeamCalHisto(padEnergies,"tempLeft");

  Double_t ymax = 0.5*double(m_nBXtoOverlay);


  for (int layer = startLayer; layer < startLayer + 4; ++layer) {

    TH2F frame("frame",Form("BeamCal Layer %i", layer), 160, -160, 160, 160, -160, 160);

    const int pad1 = ( layer - startLayer ) + 1;
    const int pad2 = ( layer - startLayer ) + 5;

    //------------------------------
    // Pad 1
    //------------------------------
    canv.cd(pad1);
    canv.cd(pad1)->SetRightMargin(0.18);
    canv.cd(pad1)->SetLeftMargin (0.18);
    bc.BeamCalDraw((TPad*)canv.GetPad(pad1), &frame, layer);
    gPad->Update();
    DrawElectronMarkers( RecoedObjects );

    {
      TCanvas tCanv("temp1",Form("BeamCal Layer %i", layer));
      tCanv.cd();
      bc.BeamCalDraw((TPad*)tCanv.GetPad(0), &frame, layer);
      tCanv.SaveAs(Form("SpecialEvent_Pad%i.eps", pad1));
    }

    //------------------------------
    // Pad 2
    //------------------------------
    canv.cd(pad2);
    bc.SetBeamCalHisto(padAverages, padErrors);
    bc.DrawPhiDistributions((TPad*)canv.GetPad(pad2), layer, "dotted,errors");

    bc.SetBeamCalHisto(padEnergies,"padLeft");
    bc.DrawPhiDistributions((TPad*)canv.GetPad(pad2), layer, "same");
    static_cast<TH1*>(((TPad*)canv.GetPad(pad2))->GetListOfPrimitives()->At(0))->SetMaximum(ymax);
    DrawLineMarkers( RecoedObjects );

    {
      TCanvas tCanv("temp1",Form("PhiDistribution Layer %i", layer));
      tCanv.cd();
      bc.SetBeamCalHisto(padAverages, padErrors);
      bc.DrawPhiDistributions(&tCanv, layer, "dotted,errors");
      bc.SetBeamCalHisto(padEnergies,"padLeft");
      bc.DrawPhiDistributions(&tCanv, layer, "same");
      static_cast<TH1*>(tCanv.GetListOfPrimitives()->At(0))->SetMaximum(ymax);
      //DrawLineMarkers( RecoedObjects );
      tCanv.SaveAs(Form("SpecialEvent_Pad%i.eps", pad2));
    }

  }//run over layers

  //Write some information
  canv.cd(9);
  TPaveText text(0.0, 0.0, 1.0, 1.0);
  text.SetFillColor(kWhite);
  text.AddText(Form("ImpactAngle #theta: %2.1f, Energy Deposit: %2.1f GeV", -1.0, depositedEnergy));
  text.AddText(Form("Max Deposit: %2.1f GeV in Layer %i", maxDeposit, maxLayer));
  text.Draw();

  canv.SaveAs(Form("Event%i.eps", m_nEvt));

}


void BeamCalClusterReco::DrawElectronMarkers ( const std::vector<BCRecoObject*> & RecoedObjects ) const {

  const double BeamCalDist = m_BCG->getBCZDistanceToIP();

  for( std::vector<BCRecoObject*>::const_iterator it = RecoedObjects.begin();
       it != RecoedObjects.end(); ++it) {

    double radius = BeamCalDist*tan((*it)->getThetaRad() );
    double circX = radius*cos((*it)->getPhi()*TMath::DegToRad());
    double circY = radius*sin((*it)->getPhi()*TMath::DegToRad());
    // electron.SetNextPoint(radius*cos(m_impactAnglePhi*TMath::DegToRad()),
    //			    radius*sin(m_impactAnglePhi*TMath::DegToRad()));

    TMarker* electron = new TMarker(circX, circY, 29);
    // electron->SetLineColor(kRed);
    // electron->SetFillColor(kRed);
    // electron->SetFillStyle(4000);
    // electron->SetMarkerStyle(kOpenCircle);
    // electron->SetMarkerSize(5);
    electron->SetMarkerColor(kRed);
    //      Double_t ymin = 0, ymax = 35;
    electron->Draw();

  }

  return;
}

void BeamCalClusterReco::DrawLineMarkers( const std::vector<BCRecoObject*> & RecoedObjects ) const {

  Double_t ymin = 0, ymax = 5;

  for( std::vector<BCRecoObject*>::const_iterator it = RecoedObjects.begin();
       it != RecoedObjects.end(); ++it) {
    TLine* line = new TLine((*it)->getPhi(),ymin,(*it)->getPhi(),ymax);
    line->SetLineStyle(kDashed);
    line->SetLineColor(kRed);
    line->SetLineWidth(0);
    line->Draw();//on gPad whatever active
  }

  return;
}


void BeamCalClusterReco::findOriginalMCParticles(LCEvent *evt) {
  m_originalParticles.clear();
  const double halfCrossingAngleMrad(m_BCG->getCrossingAngle()*0.5);
  const double maxAngle(1.1*m_BCG->getBCOuterRadius()/m_BCG->getBCZDistanceToIP()*1000); 
  try {
    LCCollection* colMC = evt->getCollection ( m_colNameMC );
    for (int i =0; i < colMC->getNumberOfElements();++i) {
      MCParticle *tempmc = static_cast<MCParticle*>(colMC->getElementAt(i));

      //only use particles generated by generator
      //this does not work, if we used the particle gun, in which case everything was created in simulation
      // in this case we take only the first
      if (not ( tempmc->getGeneratorStatus() == 1 || (tempmc->getGeneratorStatus() == 0 && i == 0 ) ) ) continue;

      const double *momentum = tempmc->getMomentum();
      double momentum2[3];
    
      //Check if it inside the BeamCalAcceptance, and has sufficientmomentum(10GeV);
      const double absMom = sqrt(momentum[0]*momentum[0]+momentum[1]*momentum[1]+momentum[2]*momentum[2]);
      if ( absMom < 10 ) continue;
      //Rotate to appropriate beamCal System
#pragma message "Fix the rotation thing"
      BCUtil::RotateToBCFrame(momentum, momentum2, halfCrossingAngleMrad);
      m_eventSide = (momentum[2] > 0) ? BCPadEnergies::kLeft  : BCPadEnergies::kRight;

      //double radius = sqrt(momentum2[0]*momentum2[0]+momentum2[1]*momentum2[1]);
      double impactTheta = BCUtil::AngleToBeamCal(momentum, halfCrossingAngleMrad)*1000;//mrad
      if ( impactTheta > maxAngle ) continue; //only particles that are inside the BeamCal acceptance

      double impactPhi   = TMath::ATan2(momentum2[1], momentum2[0]) * TMath::RadToDeg();
      if(impactPhi < 0) impactPhi += 360;
      
      OriginalMC omc(impactTheta, impactPhi, absMom);
      m_originalParticles.push_back(omc);
      streamlog_out(MESSAGE2) << "Found MCParticle: (Theta, Phi, Eng) "
			      << std::setw(13) << impactTheta
			      << std::setw(13) << impactPhi
			      << std::setw(13) << absMom
			      << std::endl;
    }
  } catch (Exception &e) {
  }

  m_MCinBeamCal = false;
  try {
    LCCollection* colBC = evt->getCollection ( m_colNameBCal );
    if ( colBC->getNumberOfElements() > 1000 ) { m_MCinBeamCal = true; }
  } catch (Exception &e) {
  }


  return;
}//FindOriginalMCParticle
