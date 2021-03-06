// Local
#include "Global.hh"
#include "LumiCalClusterer.h"
#include "SortingFunctions.hh"
#include "Distance2D.hh"
using LCHelper::distance2D;

// Root
#include <TF1.h>
#include <TH1F.h>
#include <TString.h>
// LCIO
#include <IMPL/CalorimeterHitImpl.h>
// stdlib
#include <algorithm>
#include <iomanip>
#include <map>
#include <stdexcept>
#include <vector>



/* =========================================================================
   LumiCalClustererClass :: buildClusters
   ============================================================================
   (1). Description:
   --------------------------------
   - SOME DESCRIPTION ......
   ============================================================================ */

int LumiCalClustererClass::buildClusters( std::map < int , std::vector <IMPL::CalorimeterHitImpl*> > const& calHits,
					  MapIntCalHit & calHitsCellIdGlobal,
					  MapIntVInt & superClusterIdToCellId,
					  MapIntVDouble & superClusterIdToCellEngy,
					  MapIntLCCluster & superClusterCM,
					  const int detectorArm) {

  int   maxEngyLayerN(-1);
  double maxEngyLayer;
  int numSuperClusters;

  std::vector < std::map <int , IMPL::CalorimeterHitImpl* > > calHitsCellId(_maxLayerToAnalyse), calHitsSmallEngyCellId(_maxLayerToAnalyse);

  std::vector < std::map < int , int > >        cellIdToClusterId(_maxLayerToAnalyse+1);

  std::vector < std::map < int , std::vector<int> > >   clusterIdToCellId(_maxLayerToAnalyse+1);

  std::vector < std::map < int , LCCluster > >        clusterCM(_maxLayerToAnalyse+1);
  std::map < int , LCCluster > :: iterator    clusterCMIterator;

  std::map < int , int >                cellIdToSuperClusterId;
  std::map < int , int > :: iterator    cellIdToSuperClusterIdIterator;

  std::map < int , LCCluster > :: iterator    superClusterCMIterator;

  std::vector < int >   initialClusterControlVar(4), isShowerPeakLayer(_maxLayerToAnalyse);

  std::map < int , std::vector < LCCluster > >                 engyPosCMLayer;
  std::map < int , std::vector < LCCluster > > :: iterator     engyPosCMLayerIterator;

  std::map < int , std::vector <int> >  thisLayer;

  std::vector < std::map < int , VirtualCluster > >        virtualClusterCM(_maxLayerToAnalyse);

  std::map < int , TH1F >             xLineFitCM, yLineFitCM;
  std::vector < std::vector<double> >        fitParamX, fitParamY;

  std::map < int , double >                     layerToPosX, layerToPosY, layerToEngy;
  std::map < int , double > :: iterator layerToPosXYIterator;


  /* --------------------------------------------------------------------------
     determine the total energy of the hits in the arm
     -------------------------------------------------------------------------- */
#if _GENERAL_CLUSTERER_DEBUG == 1 || _CLUSTER_BUILD_DEBUG == 1
  std::string detectorArmName;
  if(detectorArm > 0) detectorArmName = "positive detector arm";
  if(detectorArm < 0) detectorArmName = "negative detector arm";
  std::cout << "************************ buildClusters Arm "<< detectorArm <<" *****************************************\n";
  std::cout << "\tTotal " << detectorArmName << " energy =  "
	    << _totEngyArm[detectorArm] << std::endl << std::endl;
#endif


  /* --------------------------------------------------------------------------
    1.  set the <middleEnergyHitBound> minimal energy to take into account in the initial  clustering pass
    2.  determine the <minNumElementsInShowerPeakLayer>  min number of hits that makeup a showerPeak layer and
        flag the layers that make the cut. 
     -------------------------------------------------------------------------- */

  const double middleEnergyHitBound = exp(-1*_logWeightConst) * _totEngyArm[detectorArm] * _middleEnergyHitBoundFrac;
  const int minNumElementsInShowerPeakLayer = int( _numHitsInArm[detectorArm] * _elementsPercentInShowerPeakLayer);

#if _CLUSTER_BUILD_DEBUG == 1
  for (MapIntVCalHit::const_iterator calHitsIt = calHits.begin(); calHitsIt!=calHits.end(); ++calHitsIt) {
    std::cout << "Hits in layer " 
                           << std::setw(3) << calHitsIt->first 
                           << std::setw(6) << calHitsIt->second.size()  << std::endl;
  }

  std::cout  <<  "Shower peak layers:" << std::endl;
  std::cout  << "\t min # of hits, min energy/hit([signal]) : " << minNumElementsInShowerPeakLayer
			  << "\t(" <<  middleEnergyHitBound << " , " 
			  << ")" <<std::endl << "\t layers chosen : \n";
#endif

  // 1. set ShowerPeak tags in isShowerPeakLayer vector
  // 2. fill calHitsCellId with layer tagged calhits, skip hits with energy below <minHitEnergy>
  // 3. for ShowerPeak layers optionally split hits according to value <middleEnergyHitBound>

  for (MapIntVCalHit::const_iterator calHitsIt = calHits.begin(); calHitsIt!=calHits.end(); ++calHitsIt) {
    size_t numHitsInLayer = (int)(calHitsIt->second.size());
    int layerNow = calHitsIt->first;
    isShowerPeakLayer[ layerNow ] = ( (int)numHitsInLayer > minNumElementsInShowerPeakLayer )? 1 : 0;
#if _CLUSTER_BUILD_DEBUG == 1
    if( isShowerPeakLayer[ layerNow ] == 1 )
      std::cout <<"\t"<< layerNow <<"\t nhits("<< numHitsInLayer <<")\n";
#endif
    for(size_t j=0; j<numHitsInLayer; j++){
      int       cellIdHit = (int)calHitsIt->second[j]->getCellID0();
      double    cellEngy = (double)calHitsIt->second[j]->getEnergy();
      if( cellEngy >= _hitMinEnergy ){
#if _CLUSTER_MIDDLE_RANGE_ENGY_HITS == 1
	/* split hits in ShowerPeakLayer into two sets one with hit energy below 
	 * and the other above middleEnergyHitBound
	 */
	if( isShowerPeakLayer[layerNow]) {
	  if(cellEngy <= middleEnergyHitBound ){
	    calHitsSmallEngyCellId[layerNow][cellIdHit] = calHitsIt->second[j];
	  }else{
	    calHitsCellId[layerNow][cellIdHit] = calHitsIt->second[j];
	  }
	}else{
	  calHitsCellId[layerNow][cellIdHit] = calHitsIt->second[j];
	}
#else
	// all hits assigned to one set
	calHitsCellId[layerNow][cellIdHit] = calHitsIt->second[j];
#endif
      }
    }
  }


  /* --------------------------------------------------------------------------
     fill calHitsCellId with the layer tagged cal hits. for showerPeak layers
     separate cal hits with energy above/below the middleEnergyHitBound.
     in any case only choose hits with energy above the _hitMinEnergy cut
     -------------------------------------------------------------------------- */
  /*(BP)
  for (MapIntVCalHit::const_iterator calHitsIt = calHits.begin(); calHitsIt!=calHits.end(); ++calHitsIt) {
    //  for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++) {
    for(size_t j=0; j<calHitsIt->second.size(); j++){
      int       cellIdHit = (int)calHitsIt->second[j]->getCellID0();
      double    cellEngy = (double)calHitsIt->second[j]->getEnergy();
      const int layerNow = calHitsIt->first;
      if(cellEngy < _hitMinEnergy) continue;
      //(BP) Bug ? No matter what is value of the option: _CLUSTER_MIDDLE_RANGE_ENGY_HITS
        -  calHitsCellId contains all hits
      if(cellEngy > middleEnergyHitBound) calHitsCellId[layerNow][cellIdHit] = calHitsIt->second[j];

#if _CLUSTER_MIDDLE_RANGE_ENGY_HITS == 1
      if(isShowerPeakLayer[layerNow] == 1) {
	if(cellEngy <= middleEnergyHitBound) calHitsSmallEngyCellId[layerNow][cellIdHit] = calHitsIt->second[j];
      } else {
	calHitsCellId[layerNow][cellIdHit] = calHitsIt->second[j];
      }
#endif
    }
  }
  */

  /* --------------------------------------------------------------------------
     form initial clusters for the shower-peak layers
     -------------------------------------------------------------------------- */
#if _CLUSTER_BUILD_DEBUG == 1
  std::cout <<  "run initialClusterBuild and initialLowEngyClusterBuild:" << std::endl;
#endif

  // set the control vector for the initialClusterBuild clustering options
  initialClusterControlVar[0] = 1;  // mergeOneHitClusters
  initialClusterControlVar[1] = 1;  // mergeSmallToLargeClusters
  initialClusterControlVar[2] = 1;  // mergeLargeToSmallClusters
  initialClusterControlVar[3] = 1;  // forceMergeSmallToLargeClusters

  for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++) 
    if(isShowerPeakLayer[layerNow] == 1) {
      // run the initial clustering algorithm for the high energy hits
      // 
#if _CLUSTER_BUILD_DEBUG == 1
      std::cout << "\t layer " << layerNow <<std::endl;
      int numCells = calHitsCellId[layerNow].size();
      std::map <int , IMPL::CalorimeterHitImpl* >::iterator calHitsCellIdIterator = calHitsCellId[layerNow].begin();
      for(int cellNow = 0; cellNow < numCells; cellNow++, calHitsCellIdIterator++){
	int cellId = calHitsCellIdIterator->first;
	const IMPL::CalorimeterHitImpl* calHit = calHitsCellIdIterator->second;
	const float* pos = calHit->getPosition();
	std::cout << "\t\t CellId, pos(x,y,z), signal energy [MeV]: "
		  << cellId << "\t ("
		  << pos[0] << ", " 
		  << pos[1] << ", " 
		  << pos[2] << "), " 
		  << std::fixed << std::setprecision(3)
		  << 1000.*(calHit->getEnergy())
		  <<std::endl;
      }
      std::cout <<std::endl;
#endif
      initialClusterBuild( calHitsCellId[layerNow],          // <--
			   cellIdToClusterId[layerNow],      // -->
			   clusterIdToCellId[layerNow],      // -->
			   clusterCM[layerNow],              // -->
			   initialClusterControlVar );       // <--

#if _CLUSTER_MIDDLE_RANGE_ENGY_HITS == 1
      // cluster the low energy hits
      initialLowEngyClusterBuild( calHitsSmallEngyCellId[layerNow],
				  calHitsCellId[layerNow],
				  cellIdToClusterId[layerNow],
				  clusterIdToCellId[layerNow],
				  clusterCM[layerNow]) ;
#endif
      // store max number of hits in ShowerPeakLayer
 
#if _CLUSTER_BUILD_DEBUG == 1
      std::cout << "\t layer " << layerNow <<std::endl;
      int numClusters = clusterCM[layerNow].size();
      clusterCMIterator = clusterCM[layerNow].begin();
      for(int clusterNow1 = 0; clusterNow1 < numClusters; clusterNow1++, clusterCMIterator++){
	int clusterId = (int)(*clusterCMIterator).first;
	std::cout << "\t\t cluster Id, pos(x,y,z), engy(weight): "
		  << clusterId << "\t (" 
		  << clusterCM[layerNow][clusterId].getX() << " , "
		  << clusterCM[layerNow][clusterId].getY() << " , "
		  << clusterCM[layerNow][clusterId].getZ() << ") \t " 
		  << clusterCM[layerNow][clusterId].getE() <<" ( " 
		  << clusterCM[layerNow][clusterId].getWeight() <<" ) " <<std::endl;
      }
      std::cout <<std::endl;
#endif
    }

  /* --------------------------------------------------------------------------
     check how many global clusters there are 
     find the most frequent value of numClusters within ShowerPeakLayer set
     -------------------------------------------------------------------------- */
  MapIntInt numClustersCounter;
  // find the number of clusters in the majority of layers
#if _CLUSTER_BUILD_DEBUG == 1
      std::cout <<  "Searching number of clusters in the majority of layers..." << std::endl; 
#endif
  for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++) {
    if(isShowerPeakLayer[layerNow] == 1) {
      const int numClusters = clusterCM[layerNow].size();
#if _CLUSTER_BUILD_DEBUG == 1
      std::cout <<  "\t -> layer " << layerNow <<"\t global clusters "<< numClusters << std::endl; 
#endif
      numClustersCounter[numClusters]++;
    }
  }

  MapIntInt::iterator maxCluster = 
    std::max_element( numClustersCounter.begin(), numClustersCounter.end(), compareByValue<std::pair<int, int> >);
  int numClustersMajority = maxCluster->first;

#if _CLUSTER_BUILD_DEBUG == 1
   int numLayersWithClustersMajority = maxCluster->second;
  std::cout <<  "\t -> Found that there are "<< numLayersWithClustersMajority <<" ShowerPeakLayers \n"
			 <<  "\t with "<< numClustersMajority
			 << " global clusters" << std::endl <<std::endl;
#endif
  numClustersCounter.clear();


  /* --------------------------------------------------------------------------
     choose only layers which have a numClustersMajority number of clusters,
     and add their CM to engyPosCMLayer[clusterNow] the decision to choose a
     certain clusterNow is made according to the distance of the given
     cluster CM from an averaged CM
     -------------------------------------------------------------------------- */

  // find the layer with the highest energy which has numClustersMajority clusters
  maxEngyLayer = 0.;
  for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++) {
    if(isShowerPeakLayer[layerNow] == 1) {
      clusterCMIterator = clusterCM[layerNow].begin();
      const int numClusters = clusterCM[layerNow].size();

      double    engyLayerNow = 0.;
      if(numClusters != numClustersMajority) continue;
      for(int clusterNow1 = 0; clusterNow1 < numClustersMajority; clusterNow1++, clusterCMIterator++){
	int clusterId = (int)(*clusterCMIterator).first;

	engyLayerNow += clusterCM[layerNow][clusterId].getE();
      }

      if(maxEngyLayer < engyLayerNow) {
	maxEngyLayer  = engyLayerNow;
	maxEngyLayerN = layerNow;
      }
    }
  }
  // for the layer with the most energy which has numClustersMajority clusters,
  // initialize the averageCM vector
  // (BP) FIX: Implicit assumptions is that ShowerPeakLayer is also maxEngyLayer, may not be true
  //      but anyway we do not need loop , frome above loop we know layer number
 
    std::vector < LCCluster >  avrgCM;
    /*
    for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++) {
      if(isShowerPeakLayer[layerNow] == 1) {
    	clusterCMIterator = clusterCM[layerNow].begin();
    	const int numClusters       = clusterCM[layerNow].size();
    	if( (numClusters != numClustersMajority) || (layerNow != maxEngyLayerN)) continue;

	for(int clusterNow = 0; clusterNow < numClustersMajority; clusterNow++, clusterCMIterator++){
	int clusterId = (int)(*clusterCMIterator).first;

	// store the CM energy/position vector for each cluster
	engyPosCMLayer[clusterNow].push_back( clusterCM[layerNow][clusterId] );
	thisLayer[clusterNow].push_back( layerNow );

	// initialize the averaged CM position vector
	avrgCM.push_back( clusterCM[layerNow][clusterId] );
	}
	break; // only do it in isShowerPeakLayer==1
      }// if isShowerPeakLayer
    } //check all Layers
    */       
    if( maxEngyLayerN+1 > 0 ){
      clusterCMIterator = clusterCM[maxEngyLayerN].begin();
      for(int clusterNow = 0; clusterNow < numClustersMajority; clusterNow++, clusterCMIterator++){
	int clusterId = (int)(*clusterCMIterator).first;
	// store the CM energy/position vector for each cluster
	engyPosCMLayer[clusterNow].push_back( clusterCM[maxEngyLayerN][clusterId] );
	thisLayer[clusterNow].push_back( maxEngyLayerN );
	// initialize the averaged CM position vector
	avrgCM.push_back( clusterCM[maxEngyLayerN][clusterId] );
      }
   }
  

  // for all layers in ShowerPeak except the layer with the most energy which has numClustersMajority clusters,
  // update the averageCM vector

  for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++) {
    if(isShowerPeakLayer[layerNow] == 1) {
      clusterCMIterator = clusterCM[layerNow].begin();
      int numClustersNow = int(clusterCM[layerNow].size());

      if( ( numClustersNow != numClustersMajority) || (layerNow == maxEngyLayerN) ) continue;

      for(int clusterNow1 = 0; clusterNow1 < numClustersMajority; clusterNow1++, clusterCMIterator++){
	LCCluster const& thisCluster = clusterCM[layerNow][clusterCMIterator->first];
	const double CM1[2] = { thisCluster.getX(), thisCluster.getY() };

	std::map <int , double> weightedDistanceV;
	// compare the position of the CM to the averaged CM positions
	for(int clusterNow2 = 0; clusterNow2 < numClustersMajority; clusterNow2++){
	    const double distanceCM = distance2D(CM1,avrgCM[clusterNow2].getPosition());
	    weightedDistanceV[clusterNow2] = (distanceCM > 0 ) ? 1./distanceCM : 1e10;
	}

	std::map < int , double > :: iterator closestCluster = 
	  std::max_element(weightedDistanceV.begin(), weightedDistanceV.end(), compareByValue< std::pair<int, double> >);
	// add the CM to the right vector
	engyPosCMLayer[closestCluster->first].push_back(thisCluster);
	thisLayer[closestCluster->first].push_back( layerNow );

	// update the multi-layer CM position
	//APS: BUGFIX This used to have the CM2 from the clusterNow2 loop above, instead of closestCluster
	avrgCM[closestCluster->first].addToEnergy(thisCluster.getE());
#pragma message( "(BP) temporary fix, need modify cluster CM method " )
	// 
	double wt_closest = avrgCM[closestCluster->first].getWeight();
	double wt_this = thisCluster.getWeight();
	double new_wt = wt_closest + wt_this;
	double new_x = CM1[0]*wt_this + (avrgCM[closestCluster->first].getX())*wt_closest;
	double new_y = CM1[1]*wt_this + (avrgCM[closestCluster->first].getY())*wt_closest;
	new_x /= new_wt;
	new_y /= new_wt;
	avrgCM[closestCluster->first].setX( new_x );
	avrgCM[closestCluster->first].setY( new_y );
	avrgCM[closestCluster->first].setWeight( new_wt );

	//
	//	avrgCM[closestCluster->first].setX( (CM1[0]+(avrgCM[closestCluster->first].getX()))/2.);
	//	avrgCM[closestCluster->first].setY( (CM1[1]+(avrgCM[closestCluster->first].getY()))/2.);

      }//for all clusters
    }//if isShowerPeakLayer
  }//for all layers

  /* --------------------------------------------------------------------------
     fit a stright line through each cluster from engyPosCMLayer. results
     (line parametrizations) are stored in fitParamX(Y)
     engyPosCMLayer contains now info for ShowerPeakLayer only (?!) 
     -------------------------------------------------------------------------- */
#if _CLUSTER_BUILD_DEBUG == 1
  std::cout <<  "Fit lines through the averaged CM" << std::endl <<std::endl;
  std::cout <<  "Fit Param should be this size: " <<  engyPosCMLayer.size()  << std::endl;
#endif

  TF1 fitFunc("fitFunc","[0]+[1]*x",-3000,-2000);

  //  for(size_t clusterNow=0; clusterNow < engyPosCMLayer.size(); clusterNow++, engyPosCMLayerIterator++) {
  for(engyPosCMLayerIterator = engyPosCMLayer.begin(); engyPosCMLayerIterator != engyPosCMLayer.end(); engyPosCMLayerIterator++) {
    int clusterId = (int)(*engyPosCMLayerIterator).first;
    int clusterNow = engyPosCMLayerIterator->first;

    if( engyPosCMLayer[clusterId].size() < 3) {
#if _CLUSTER_BUILD_DEBUG == 1
      std::cout << "\t decrease the global cluster number by 1"
		<< std::endl <<std::endl;
#endif
      numClustersMajority--;
      //clusterNow--;//APS
      continue;
    }

#if _CLUSTER_BUILD_DEBUG == 1
    std::cout << "clusterId " << clusterId << std::endl;
#endif

    std::string hisName = "_xLineFitCM_Cluster";
    std::stringstream clusterNum;
    clusterNum << clusterNow;
    hisName += clusterNum.str();
    xLineFitCM[clusterNow] = TH1F(  hisName.c_str(),hisName.c_str(),_maxLayerToAnalyse*10,0,_maxLayerToAnalyse);

    hisName = "_yLineFitCM_Cluster"; hisName += clusterNum.str();
    yLineFitCM[clusterNow] = TH1F(  hisName.c_str(),hisName.c_str(),_maxLayerToAnalyse*10,0,_maxLayerToAnalyse);


    /* --------------------------------------------------------------------------
       since more than on cluster may have choosen the same averagedCM in a
       given layer, some layers may have more than one entry in the engyPosCMLayer
       map. therefore an averaging is performed for each layer
       -------------------------------------------------------------------------- */

    // initialize the layer-averaging position/energy maps
    for(size_t layerN = 0; layerN < engyPosCMLayer[clusterId].size(); layerN++) {
      const int layerNow = thisLayer[clusterId][layerN];
      layerToPosX[layerNow] = layerToPosY[layerNow] = layerToEngy[layerNow] = 0.;
    }

    // fill the maps with energy-weighted positions
    for(size_t layerN = 0; layerN < engyPosCMLayer[clusterId].size(); layerN++) {
      const int layerNow = thisLayer[clusterId][layerN];
      /*(BP) FIX:
       *  it should be weighted with WeightingMethod
       */
      double weightNow =  engyPosCMLayer[clusterNow][layerN].getWeight();
      layerToPosX[layerNow] += engyPosCMLayer[clusterNow][layerN].getX() * weightNow;
      layerToPosY[layerNow] += engyPosCMLayer[clusterNow][layerN].getY() * weightNow;
      layerToEngy[layerNow] += weightNow;
      /*
      layerToPosX[layerNow] += engyPosCMLayer[clusterNow][layerN].getX() * engyPosCMLayer[clusterNow][layerN].getE();
      layerToPosY[layerNow] += engyPosCMLayer[clusterNow][layerN].getY() * engyPosCMLayer[clusterNow][layerN].getE();
      layerToEngy[layerNow] += engyPosCMLayer[clusterNow][layerN].getE();
      */
    }

    // fill histograms of x(z) and y(z) of the CM positions
    layerToPosXYIterator = layerToPosX.begin();
    for(size_t layerN = 0; layerN < layerToPosX.size(); layerN++, layerToPosXYIterator++) {
      const int layerNow = (int)(*layerToPosXYIterator).first;

      // get back to units of position
      layerToPosX[layerNow] /= layerToEngy[layerNow];
      layerToPosY[layerNow] /= layerToEngy[layerNow];

      xLineFitCM[clusterNow] . Fill(layerNow , layerToPosX[layerNow]);
      yLineFitCM[clusterNow] . Fill(layerNow , layerToPosY[layerNow]);

#if _CLUSTER_BUILD_DEBUG == 1
      std::cout     << "\tlayer , avPos(x,y) : " << layerNow << " \t (" << layerToPosX[layerNow]
		    << " , " << layerToPosY[layerNow] << ")" <<std::endl;
#endif
    }

    // fit a straight line for each histogram, and store the fit results
    xLineFitCM[clusterNow].Fit("fitFunc","+CQ0");
    fitParamX.push_back(std::vector<double>(2,0.0));
    fitParamX.back()[0] = fitFunc.GetParameter(0);
    fitParamX.back()[1] = fitFunc.GetParameter(1);

#if _CLUSTER_BUILD_DEBUG == 1
    std::cout   << "\t -> xFitPar 0,1:  "
		<< fitFunc.GetParameter(0) << " (+-) " << fitFunc.GetParError(0)
		<< " \t,\t " << fitFunc.GetParameter(1) << " (+-) " << fitFunc.GetParError(1) <<std::endl;
#endif

    yLineFitCM[clusterNow] . Fit("fitFunc","+CQ0");
    fitParamY.push_back(std::vector<double>(2,0.0));
    fitParamY.back()[0] = fitFunc.GetParameter(0);
    fitParamY.back()[1] = fitFunc.GetParameter(1);

#if _CLUSTER_BUILD_DEBUG == 1
    std::cout       << "\t -> yFitPar 0,1:  "
		    << fitFunc.GetParameter(0) << " (+-) " << fitFunc.GetParError(0)
		    << " \t,\t " << fitFunc.GetParameter(1) << " (+-) " << fitFunc.GetParError(1) <<std::endl <<std::endl;
#endif

    // cleanUp
    layerToPosX.clear();  layerToPosY.clear();  layerToEngy.clear();
  }
  // cleanUp
  xLineFitCM.clear(); yLineFitCM.clear();


  /* --------------------------------------------------------------------------
     extrapolate the CM positions in all layers (and form clusters in the
     non shower-peak layers)
     -------------------------------------------------------------------------- */
#if _CLUSTER_BUILD_DEBUG == 1
  std::cout <<  "Extrapolate virtual cluster CMs" << std::endl <<std::endl;
#endif

  // fill virtual cluster CM vectors for all the layers
  for(int layerNow = 0; layerNow < _maxLayerToAnalyse; layerNow++ ){

    if( calHitsCellId[layerNow].empty() ) continue;

    for(int clusterNow=0; clusterNow<numClustersMajority; clusterNow++){
      int       maxLayerToRaiseVirtualClusterSize = int(0.75*_maxLayerToAnalyse);
      double    fitPar0, fitPar1, hitLayerRatio;

      if ( fitParamX[clusterNow].empty() ) continue; //APS

      VirtualCluster virtualClusterCMV;

      // extrapolated x/y positions
      virtualClusterCMV.setX( fitParamX[clusterNow][0] + fitParamX[clusterNow][1] * layerNow ); // x position
      virtualClusterCMV.setY( fitParamY[clusterNow][0] + fitParamY[clusterNow][1] * layerNow ); // y position

      // ???????? DECIDE/FIX - incorparate the parameters given here better in the code ????????
      // ???????? DECIDE/FIX - consider a different middle layer for the else condition ????????
      // extrapolated cluster radius around CM position
#pragma message ("WARNING: Fix these parameters")
      if(avrgCM[clusterNow].getE() > 1) { fitPar0 = 236.7; fitPar1 = 9.11; hitLayerRatio = 22/2618.; }
      else                              { fitPar0 = 226.5; fitPar1 = 10.3; hitLayerRatio = 22/2570.; }

      if(layerNow < maxLayerToRaiseVirtualClusterSize)
	virtualClusterCMV.setZ( exp( (fitPar0 + fitPar1 * layerNow) * hitLayerRatio ));
      else
	virtualClusterCMV.setZ( exp( (fitPar0 + fitPar1 * maxLayerToRaiseVirtualClusterSize) * hitLayerRatio ));

      // The numbers above for fitPar0/1 were derived for a detector with moliereRadius=18.2
      // they must, therefore, they must be corrected for according to the _moliereRadius used now
      virtualClusterCM[layerNow][clusterNow] = virtualClusterCMV;
    }

    // form clusters for the non shower-peak layers in the non shower-peak layers only.
    if(isShowerPeakLayer[layerNow] == 0) {
      try {
      virtualCMClusterBuild( calHitsCellId[layerNow],
			     cellIdToClusterId[layerNow],
			     clusterIdToCellId[layerNow],
			     clusterCM[layerNow],
			     virtualClusterCM[layerNow] );
      } catch (std::out_of_range &e) {
	std::cout << "exception"  << std::endl;
	std::cout << e.what()  << std::endl;
	throw;
      }
#if _CLUSTER_BUILD_DEBUG == 1
      std::cout << "\tbuild cluster around a virtual CM in layer " << layerNow
			   << std::endl;
#endif
    }

    // only do something if there are less real clusters than virtual clusters in the layer
    else {
      int numRealClusters    = clusterIdToCellId[layerNow].size();
      int numVirtualClusters = virtualClusterCM[layerNow].size();
      if(numRealClusters < numVirtualClusters) {
#if _VIRTUALCLUSTER_BUILD_DEBUG == 1
	std::cout<< "\tin layer " << layerNow << " there are real/virtual clusters: "
		<< numRealClusters << "  ,  " << numVirtualClusters <<  std::endl;
#endif

	virtualCMPeakLayersFix( calHitsCellId[layerNow],
				cellIdToClusterId[layerNow],
				clusterIdToCellId[layerNow],
				clusterCM[layerNow],
				virtualClusterCM[layerNow] );

#if _CLUSTER_BUILD_DEBUG == 1
	std::cout << "\tre-cluster in layer " << layerNow <<  std::endl;
#endif
      }
    }
  }
  // cleanUp
  avrgCM.clear(); fitParamX.clear(); fitParamY.clear();


  /* --------------------------------------------------------------------------
     merge all the clusters from the different layers into superClusters.
     the number of final clusters will be numClustersMajority (the majority
     found in the shower-peak layers), and each cluster will be merged into
     a superCluster according to its distance from it (stored as virtual clusters
     in virtualClusterCM).
     -------------------------------------------------------------------------- */
#if _CLUSTER_BUILD_DEBUG == 1
  std::cout <<std::endl <<  "Build superClusters" << std::endl <<std::endl;
#endif

  int   buildSuperClustersFlag = buildSuperClusters(    calHitsCellIdGlobal,
							calHitsCellId,
							clusterIdToCellId,
							clusterCM,
							virtualClusterCM,
							cellIdToSuperClusterId,
							superClusterIdToCellId,
							superClusterCM );

  if(buildSuperClustersFlag == 0)
    return 0;

  // cleanUp
  virtualClusterCM.clear();




#if _MOLIERE_RADIUS_CORRECTIONS == 1
#if _CLUSTER_BUILD_DEBUG == 1 || _MOL_RAD_CORRECT_DEBUG == 1
  std::cout <<std::endl <<  "RUN engyInMoliereCorrections() ..." << std::endl <<std::endl;
#endif

  int engyInMoliereFlag = engyInMoliereCorrections( calHitsCellIdGlobal,
						    calHits,
						    calHitsCellId,
						    (clusterIdToCellId),
						    (clusterCM),
						    (cellIdToClusterId),
						    (cellIdToSuperClusterId),
						    (superClusterIdToCellId),
						    superClusterCM,
						    middleEnergyHitBound,
						    detectorArm );

  if(engyInMoliereFlag == 0)
    return 0;

#endif  // #if _MOLIERE_RADIUS_CORRECTIONS == 1






	/* --------------------------------------------------------------------------
	   --------------------------------------------------------------------------
	   NOTE:
	   --------------------------------------------------------------------------
	   FROM THIS POINT ON ENERGY OF HITS BELONGING TO A SUPERCLUSTER
	   MUST BE ACCESSED BY: superClusterIdToCellEngy
	   AND NOT BY:          calHitsCellIdGlobal[cellIdHit]->getEnergy()
	   AS THE ENERGY OF SINGLE HITS MAY BE shared BY SEVERAL CLUSTERS !
	   --------------------------------------------------------------------------
	   -------------------------------------------------------------------------- */


	/* --------------------------------------------------------------------------
	   fill the superClusterIdToCellEngy container
	   -------------------------------------------------------------------------- */
  superClusterCMIterator = superClusterCM.begin();
  numSuperClusters       = superClusterCM.size();
  for(int superClusterNow = 0; superClusterNow < numSuperClusters; superClusterNow++, superClusterCMIterator++) {
    const int superClusterId = (int)(*superClusterCMIterator).first;

    const int numElementsInSuperCluster = superClusterIdToCellId[superClusterId].size();
    for(int cellNow = 0; cellNow < numElementsInSuperCluster; cellNow++) {
      int cellIdHit = superClusterIdToCellId[superClusterId][cellNow];

      double engyHit = calHitsCellIdGlobal.at(cellIdHit) -> getEnergy();

      superClusterIdToCellEngy[superClusterId].push_back(engyHit);
    }
  }

  /* --------------------------------------------------------------------------
     verbosity
     -------------------------------------------------------------------------- */
#if _GENERAL_CLUSTERER_DEBUG == 1
  std::cout << "\tClusters:" << std::endl;

  superClusterCMIterator = superClusterCM.begin();
  numSuperClusters       = superClusterCM.size();
  for(int superClusterNow = 0; superClusterNow < numSuperClusters; superClusterNow++, superClusterCMIterator++) {
    const int superClusterId = (int)(*superClusterCMIterator).first;

    std::cout << "\t Id "  << superClusterId
	      << "  \t energy " << superClusterCM[superClusterId].getEnergy()
	      << "     \t pos(x,y) =  ( " << superClusterCM[superClusterId].getX()
	      << " , " << superClusterCM[superClusterId].getY() << " )"
	      << "     \t pos(theta,phi) =  ( " << superClusterCM[superClusterId].getTheta()
	      << " , " << superClusterCM[superClusterId].getPhi() << " )"
	      << std::endl;
  }

  std::cout <<  std::endl;
#endif

  return 1;
}
