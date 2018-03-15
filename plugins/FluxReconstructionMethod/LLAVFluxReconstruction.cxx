// Copyright (C) 2012 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "Framework/MethodCommandProvider.hh"

#include "Framework/CFSide.hh"
#include "Framework/MeshData.hh"
#include "Framework/BaseTerm.hh"

#include "MathTools/MathFunctions.hh"

#include "FluxReconstructionMethod/LLAVFluxReconstruction.hh"
#include "FluxReconstructionMethod/FluxReconstruction.hh"
#include "FluxReconstructionMethod/FluxReconstructionElementData.hh"

//////////////////////////////////////////////////////////////////////////////

using namespace std;
using namespace COOLFluiD::Common;
using namespace COOLFluiD::Framework;
using namespace COOLFluiD::MathTools;
using namespace COOLFluiD::Common;

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {
  namespace FluxReconstructionMethod {
    
//////////////////////////////////////////////////////////////////////////////

MethodCommandProvider< LLAVFluxReconstruction,
		       FluxReconstructionSolverData,
		       FluxReconstructionModule >
LLAVFluxReconstructionFluxReconstructionProvider("LLAV");

//////////////////////////////////////////////////////////////////////////////
  
LLAVFluxReconstruction::LLAVFluxReconstruction(const std::string& name) :
  DiffRHSFluxReconstruction(name),
  m_updateVarSet(CFNULL),
  m_cellNodesConn(CFNULL),
  m_cellBuilder(CFNULL),
  m_isFaceOnBoundaryCell(CFNULL),
  m_nghbrCellSideCell(CFNULL),
  m_currCellSideCell(CFNULL),
  m_faceOrientsCell(CFNULL),
  m_faceBCIdxCell(CFNULL),
  m_faces(),
  m_order(),
  m_transformationMatrix(),
  m_statesPMinOne(),
  m_epsilon(),
  m_s0(),
  m_s(),
  m_epsilon0(),
  m_kappa(),
  m_peclet(),
  m_cellNodes(),
  m_faceNodes(),
  m_nbrCornerNodes(),
  m_nodeEpsilons(),
  m_nbNodeNeighbors(),
  m_cellEpsilons(),
  m_epsilonLR(),
  m_flagComputeNbNghb(),
  m_nodePolyValsAtFlxPnts(),
  m_nodePolyValsAtSolPnts(),
  m_solEpsilons(),
  m_elemIdx(),
  m_flxPntGhostGrads(),
  m_bcStateComputers(CFNULL),
  m_freezeLimiterRes(),
  m_freezeLimiterIter(),
  m_useMax(),
  m_totalEps(),
  m_jacob(false)
  {
    addConfigOptionsTo(this);
    
    m_kappa = 5.0;
    setParameter( "Kappa", &m_kappa);
    
    m_peclet = 2.0;
    setParameter( "Peclet", &m_peclet);
    
    m_freezeLimiterRes = -20.0;
    setParameter( "FreezeLimiterRes", &m_freezeLimiterRes);
  
    m_freezeLimiterIter = MathTools::MathConsts::CFuintMax();
    setParameter( "FreezeLimiterIter", &m_freezeLimiterIter);
  }
  
  
//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::defineConfigOptions(Config::OptionList& options)
{
  options.addConfigOption< CFreal >("Kappa","Kappa factor of artificial viscosity.");
  
  options.addConfigOption< CFreal >("Peclet","Peclet number to be used for artificial viscosity.");
  
  options.addConfigOption< CFreal >("FreezeLimiterRes","Residual after which to freeze the residual.");
  
  options.addConfigOption< CFuint >("FreezeLimiterIter","Iteration after which to freeze the residual.");
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::configure ( Config::ConfigArgs& args )
{
  FluxReconstructionSolverCom::configure(args);
}  

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::execute()
{
  CFAUTOTRACE;
  
  CFLog(VERBOSE, "LLAVFluxReconstruction::execute()\n");
  
  // get the elementTypeData
  SafePtr< vector<ElementTypeData> > elemType = MeshDataStack::getActive()->getElementTypeData();

  // get InnerCells TopologicalRegionSet
  SafePtr<TopologicalRegionSet> cells = MeshDataStack::getActive()->getTrs("InnerCells");

  // get the geodata of the geometric entity builder and set the TRS
  CellToFaceGEBuilder::GeoData& geoDataCell = m_cellBuilder->getDataGE();
  geoDataCell.trs = cells;
  
  // get InnerFaces TopologicalRegionSet
  SafePtr<TopologicalRegionSet> faces = MeshDataStack::getActive()->getTrs("InnerFaces");

  // get the face start indexes
  vector< CFuint >& innerFacesStartIdxs = getMethodData().getInnerFacesStartIdxs();

  // get number of face orientations
  const CFuint nbrFaceOrients = innerFacesStartIdxs.size()-1;

  // get the geodata of the face builder and set the TRSs
  FaceToCellGEBuilder::GeoData& geoDataFace = m_faceBuilder->getDataGE();
  geoDataFace.cellsTRS = cells;
  geoDataFace.facesTRS = faces;
  geoDataFace.isBoundary = false;
  
  const CFreal residual = SubSystemStatusStack::getActive()->getResidual();
  
  const CFuint iter = SubSystemStatusStack::getActive()->getNbIter();
  
  m_useMax = residual < m_freezeLimiterRes || iter > m_freezeLimiterIter;
  m_totalEps = 0.0;
  
  m_nodeEpsilons = 0.0;
  
  //// Loop over the elements to compute the artificial viscosities
  
  // loop over element types, for the moment there should only be one
  const CFuint nbrElemTypes = elemType->size();
  cf_assert(nbrElemTypes == 1);
  for (m_iElemType = 0; m_iElemType < nbrElemTypes; ++m_iElemType)
  {
    // get start and end indexes for this type of element
    const CFuint startIdx = (*elemType)[m_iElemType].getStartIdx();
    const CFuint endIdx   = (*elemType)[m_iElemType].getEndIdx();

    // loop over cells
    for (CFuint elemIdx = startIdx; elemIdx < endIdx; ++elemIdx)
    {
      // build the GeometricEntity
      geoDataCell.idx = elemIdx;
      m_elemIdx = elemIdx;
      m_cell = m_cellBuilder->buildGE();

      // get the states in this cell
      m_cellStates = m_cell->getStates();
      
      // get the nodes in this cell
      m_cellNodes  = m_cell->getNodes();
      
      // if the states in the cell are parallel updatable, compute the resUpdates (-divFC)
      if ((*m_cellStates)[0]->isParUpdatable())
      {
	// compute the states projected on order P-1
	computeProjStates(m_statesPMinOne);
	
	// compute the artificial viscosity
	computeEpsilon();
	
	// store epsilon
	storeEpsilon();
      } 
      
      //release the GeometricEntity
      m_cellBuilder->releaseGE();
    }
  }
  
  CFLog(INFO, "total eps: " << m_totalEps << "\n");
  
  m_flagComputeNbNghb = false;
  
  //// Loop over faces to calculate fluxes and interface fluxes in the flux points
  
  // loop over different orientations
  for (m_orient = 0; m_orient < nbrFaceOrients; ++m_orient)
  {
    CFLog(VERBOSE, "Orient = " << m_orient << "\n");
    // start and stop index of the faces with this orientation
    const CFuint faceStartIdx = innerFacesStartIdxs[m_orient  ];
    const CFuint faceStopIdx  = innerFacesStartIdxs[m_orient+1];

    // loop over faces with this orientation
    for (CFuint faceID = faceStartIdx; faceID < faceStopIdx; ++faceID)
    {
      // build the face GeometricEntity
      geoDataFace.idx = faceID;
      m_face = m_faceBuilder->buildGE();
      
      // get the neighbouring cells
      m_cells[LEFT ] = m_face->getNeighborGeo(LEFT );
      m_cells[RIGHT] = m_face->getNeighborGeo(RIGHT);

      // get the states in the neighbouring cells
      m_states[LEFT ] = m_cells[LEFT ]->getStates();
      m_states[RIGHT] = m_cells[RIGHT]->getStates();
      
      // compute volume
      m_cellVolume[LEFT] = m_cells[LEFT]->computeVolume();
      m_cellVolume[RIGHT] = m_cells[RIGHT]->computeVolume();
      
      // if one of the neighbouring cells is parallel updatable, compute the correction flux
      if ((*m_states[LEFT ])[0]->isParUpdatable() || (*m_states[RIGHT])[0]->isParUpdatable())
      {
	// set the face data
	setFaceData(m_face->getID());//faceID
	
	// compute the left and right states and gradients in the flx pnts
	computeFlxPntStatesAndGrads();
	
	// compute the common interface flux
	computeInterfaceFlxCorrection();

	// compute the wave speed updates
        computeWaveSpeedUpdates(m_waveSpeedUpd);

        // update the wave speed
        updateWaveSpeed();

	// compute the correction for the left neighbour
	computeCorrection(LEFT, m_divContFlx);

	// update RHS
	updateRHS();
	
	// compute the correction for the right neighbour
	computeCorrection(RIGHT, m_divContFlx);
	
	// update RHS
	updateRHS();
      }

      // release the GeometricEntity
      m_faceBuilder->releaseGE();
    }
  }

  //// Loop over the elements to calculate the divergence of the continuous flux
  
  // loop over element types, for the moment there should only be one
  for (m_iElemType = 0; m_iElemType < nbrElemTypes; ++m_iElemType)
  {
    // get start and end indexes for this type of element
    const CFuint startIdx = (*elemType)[m_iElemType].getStartIdx();
    const CFuint endIdx   = (*elemType)[m_iElemType].getEndIdx();

    // loop over cells
    for (m_elemIdx = startIdx; m_elemIdx < endIdx; ++m_elemIdx)
    {
      // build the GeometricEntity
      geoDataCell.idx = m_elemIdx;
      m_cell = m_cellBuilder->buildGE();

      // get the states in this cell
      m_cellStates = m_cell->getStates();
      
      // if the states in the cell are parallel updatable, compute the resUpdates (-divFC)
      if ((*m_cellStates)[0]->isParUpdatable())
      {
	// get the neighbouring faces
        m_faces = m_cell->getNeighborGeos();
      
	// set the cell data
	setCellData();

	// compute the divergence of the discontinuous flux (-divFD+divhFD)
	computeDivDiscontFlx(m_divContFlx);
      
	// update RHS
        updateRHS();
      } 
      
      // divide by the Jacobian to transform the residuals back to the physical domain
      //divideByJacobDet();
      
//       for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
//       {
//         (*((*m_cellStates)[iSol]))[0] = m_solEpsilons[iSol];
//       }
      
      // print out the residual updates for debugging
      if(m_cell->getID() == 1988) //
      {
	CFLog(VERBOSE, "ID  = " << (*m_cellStates)[0]->getLocalID() << "\n");
        CFLog(VERBOSE, "TotalUpdate = \n");
        // get the datahandle of the rhs
        DataHandle< CFreal > rhs = socket_rhs.getDataHandle();
        for (CFuint iState = 0; iState < m_nbrSolPnts; ++iState)
        {
          CFuint resID = m_nbrEqs*( (*m_cellStates)[iState]->getLocalID() );
          for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
          {
            CFLog(VERBOSE, "" << rhs[resID+iVar] << " ");
          }
          CFLog(VERBOSE,"\n");
          DataHandle<CFreal> updateCoeff = socket_updateCoeff.getDataHandle();
          CFLog(VERBOSE, "UpdateCoeff: " << updateCoeff[(*m_cellStates)[iState]->getLocalID()] << "\n");
        }
      }
      
      //release the GeometricEntity
      m_cellBuilder->releaseGE();
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeInterfaceFlxCorrection()
{ 
  // Loop over the flux points to calculate FI
  for (CFuint iFlxPnt = 0; iFlxPnt < m_nbrFaceFlxPnts; ++iFlxPnt)
  { 
    const CFreal epsilon = 0.5*(m_epsilonLR[LEFT][iFlxPnt]+m_epsilonLR[RIGHT][iFlxPnt]);
    
    // compute the average sol and grad to use the BR2 scheme
    for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
    {
      *(m_avgGrad[iVar]) = (*(m_cellGradFlxPnt[LEFT][iFlxPnt][iVar]) + *(m_cellGradFlxPnt[RIGHT][iFlxPnt][iVar]))/2.0;
    }
    
    m_flxPntRiemannFlux[iFlxPnt] = 0.0;
    
    for (CFuint iDim = 0; iDim < m_dim; ++iDim)
    {
      for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
      {
        m_flxPntRiemannFlux[iFlxPnt][iVar] += epsilon*((*(m_avgGrad[iVar]))[iDim])*m_unitNormalFlxPnts[iFlxPnt][iDim];
      }
    }
     
    // compute FI in the mapped coord frame
    m_cellFlx[LEFT][iFlxPnt] = (m_flxPntRiemannFlux[iFlxPnt])*m_faceJacobVecSizeFlxPnts[iFlxPnt][LEFT];
    m_cellFlx[RIGHT][iFlxPnt] = (m_flxPntRiemannFlux[iFlxPnt])*m_faceJacobVecSizeFlxPnts[iFlxPnt][RIGHT];
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::setFaceData(CFuint faceID)
{
  DiffRHSFluxReconstruction::setFaceData(faceID);
  
  m_faceNodes = m_face->getNodes();
  
  // loop over flx pnts to extrapolate the states to the flux points
  for (CFuint iFlxPnt = 0; iFlxPnt < m_nbrFaceFlxPnts; ++iFlxPnt)
  {   
    //m_epsilonLR[LEFT][iFlxPnt] = m_cellEpsilons[m_cells[LEFT]->getID()];
    //m_epsilonLR[RIGHT][iFlxPnt] = m_cellEpsilons[m_cells[RIGHT]->getID()];
    
    // local flux point indices in the left and right cell
    const CFuint flxPntIdxL = (*m_faceFlxPntConnPerOrient)[m_orient][LEFT][iFlxPnt];
    const CFuint flxPntIdxR = (*m_faceFlxPntConnPerOrient)[m_orient][RIGHT][iFlxPnt]; 
    
    // reset the states in the flx pnts
    m_epsilonLR[LEFT][iFlxPnt] = 0.0;
    m_epsilonLR[RIGHT][iFlxPnt] = 0.0;
    
    for (CFuint iSide = 0; iSide < 2; ++iSide)
    {
      m_cellNodes = m_cells[iSide]->getNodes();
      CFuint flxIdx;
      iSide == LEFT ? flxIdx = flxPntIdxL : flxIdx = flxPntIdxR;

      // loop over the sol pnts to compute the states and grads in the flx pnts
      for (CFuint iNode = 0; iNode < m_faceNodes->size(); ++iNode)
      {
	for (CFuint iNodeCell = 0; iNodeCell < m_nbrCornerNodes; ++iNodeCell)
        {
	  if ((*m_faceNodes)[iNode]->getLocalID() == (*m_cellNodes)[iNodeCell]->getLocalID())
	  {
            //const CFuint nodeIdx = (*m_faceNodes)[iNode]->getLocalID();
	    // get node local index
            const CFuint nodeIdx = (*m_cellNodesConn)(m_cells[iSide]->getID(),iNodeCell);
	    
            m_epsilonLR[iSide][iFlxPnt] += m_nodePolyValsAtFlxPnts[flxIdx][iNodeCell]*m_nodeEpsilons[nodeIdx]/m_nbNodeNeighbors[nodeIdx];
	  }
	}
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeWaveSpeedUpdates(vector< CFreal >& waveSpeedUpd)
{
  // compute the wave speed updates for the neighbouring cells
  cf_assert(waveSpeedUpd.size() == 2);
  CFreal visc = 1.0;
  
  for (CFuint iSide = 0; iSide < 2; ++iSide)
  {
    waveSpeedUpd[iSide] = 0.0;
    for (CFuint iFlx = 0; iFlx < m_cellFlx[iSide].size(); ++iFlx)
    {
      const CFreal jacobXJacobXIntCoef = m_faceJacobVecAbsSizeFlxPnts[iFlx]*
                                         m_faceJacobVecAbsSizeFlxPnts[iFlx]*
                                         (*m_faceIntegrationCoefs)[iFlx]*
                                         m_cflConvDiffRatio;
      const CFreal rho = (*(m_cellStatesFlxPnt[iSide][iFlx]))[0];
      const CFreal epsilon = 0.5*(m_epsilonLR[LEFT][iFlx]+m_epsilonLR[RIGHT][iFlx]);
      visc = epsilon/rho;
      
      // transform update states to physical data to calculate eigenvalues
      waveSpeedUpd[iSide] += visc*jacobXJacobXIntCoef/m_cellVolume[iSide];
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeDivDiscontFlx(vector< RealVector >& residuals)
{
  // reset the extrapolated fluxes
  for (CFuint iFlxPnt = 0; iFlxPnt < m_flxPntsLocalCoords->size(); ++iFlxPnt)
  {
    m_extrapolatedFluxes[iFlxPnt] = 0.0;
  }
  
  // Loop over solution points to calculate the discontinuous flux.
  for (CFuint iSolPnt = 0; iSolPnt < m_nbrSolPnts; ++iSolPnt)
  { 
    // dereference the state
    State& stateSolPnt = *(*m_cellStates)[iSolPnt];

    vector< RealVector > temp = *(m_cellGrads[0][iSolPnt]);
    vector< RealVector* > grad;
    grad.resize(m_nbrEqs);

    for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
    {
      cf_assert(temp.size() == m_nbrEqs);
      grad[iVar] = & (temp[iVar]);
    }
    
    // calculate the discontinuous flux projected on x, y, z-directions
    for (CFuint iDim = 0; iDim < m_dim; ++iDim)
    { 
      m_contFlx[iSolPnt][iDim] = 0.0;
    
      for (CFuint iDim2 = 0; iDim2 < m_dim; ++iDim2)
      {
        for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
        {
          m_contFlx[iSolPnt][iDim][iVar] += m_solEpsilons[iSolPnt]*((*(grad[iVar]))[iDim2])*m_cellFluxProjVects[iDim][iSolPnt][iDim2];
        }
      }
    }
    
    for (CFuint iFlxPnt = 0; iFlxPnt < m_flxPntsLocalCoords->size(); ++iFlxPnt)
    {
      CFuint dim = (*m_flxPntFlxDim)[iFlxPnt];
      m_extrapolatedFluxes[iFlxPnt] += (*m_solPolyValsAtFlxPnts)[iFlxPnt][iSolPnt]*(m_contFlx[iSolPnt][dim]);
    }
  }

  // Loop over solution pnts to calculate the divergence of the discontinuous flux
  for (CFuint iSolPnt = 0; iSolPnt < m_nbrSolPnts; ++iSolPnt)
  {
    // reset the divergence of FC
    residuals[iSolPnt] = 0.0;
    // Loop over solution pnt to count factor of all sol pnt polys
    for (CFuint jSolPnt = 0; jSolPnt < m_nbrSolPnts; ++jSolPnt)
    {
      // Loop over deriv directions and sum them to compute divergence
      for (CFuint iDir = 0; iDir < m_dim; ++iDir)
      {
        // Loop over conservative fluxes 
        for (CFuint iEq = 0; iEq < m_nbrEqs; ++iEq)
        {
          // Store divFD in the vector that will be divFC
          residuals[iSolPnt][iEq] += (*m_solPolyDerivAtSolPnts)[iSolPnt][iDir][jSolPnt]*(m_contFlx[jSolPnt][iDir][iEq]);

	  if (fabs(residuals[iSolPnt][iEq]) < MathTools::MathConsts::CFrealEps())
          {
            residuals[iSolPnt][iEq] = 0.0;
	  }
	}
      }
    }
  }
    
  const CFuint nbrFaces = m_cell->nbNeighborGeos();
  for (CFuint iFace = 0; iFace < nbrFaces; ++iFace)
  {
    if (!((*m_isFaceOnBoundaryCell)[iFace]))
    {
      for (CFuint iSolPnt = 0; iSolPnt < m_nbrSolPnts; ++iSolPnt)
      {
        for (CFuint iFlxPnt = 0; iFlxPnt < m_nbrFaceFlxPnts; ++iFlxPnt)
        {
          const CFuint currFlxIdx = (*m_faceFlxPntConn)[iFace][iFlxPnt];
          const CFreal divh = m_corrFctDiv[iSolPnt][currFlxIdx];

          if (fabs(divh) > MathTools::MathConsts::CFrealEps())
          {   
            // Fill in the corrections
            for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
            {
              residuals[iSolPnt][iVar] += -m_extrapolatedFluxes[currFlxIdx][iVar] * divh; 
            }
          }
        }
      }
    }
    else
    {
      m_faceNodes = (*m_faces)[iFace]->getNodes();
      m_face = (*m_faces)[iFace];
      m_cellNodes = m_cell->getNodes();
	
      vector< RealVector > unitNormalFlxPnts;
  
      vector< CFreal > faceJacobVecSizeFlxPnts;
      faceJacobVecSizeFlxPnts.resize(m_nbrFaceFlxPnts);
	
      // get the local FR data
      vector< FluxReconstructionElementData* >& frLocalData = getMethodData().getFRLocalData();
      
      // get the datahandle of the update coefficients
      DataHandle<CFreal> updateCoeff = socket_updateCoeff.getDataHandle();
    
      // compute flux point coordinates
      SafePtr< vector<RealVector> > flxLocalCoords = frLocalData[0]->getFaceFlxPntsFaceLocalCoords();
  
      // compute flux point coordinates
      for (CFuint iFlx = 0; iFlx < m_nbrFaceFlxPnts; ++iFlx)
      {
        m_flxPntCoords[iFlx] = m_face->computeCoordFromMappedCoord((*flxLocalCoords)[iFlx]);	
      }
          
      // compute face Jacobian vectors
      vector< RealVector > faceJacobVecs = m_face->computeFaceJacobDetVectorAtMappedCoords(*flxLocalCoords);
  
      // get face Jacobian vector sizes in the flux points
      DataHandle< vector< CFreal > > faceJacobVecSizeFaceFlxPnts = socket_faceJacobVecSizeFaceFlxPnts.getDataHandle();
  
      // Loop over flux points to compute the unit normals
      for (CFuint iFlxPnt = 0; iFlxPnt < m_nbrFaceFlxPnts; ++iFlxPnt)
      {
        // get face Jacobian vector size
        CFreal faceJacobVecAbsSizeFlxPnts = faceJacobVecSizeFaceFlxPnts[m_face->getID()][iFlxPnt];
	
	// set face Jacobian vector size with sign depending on mapped coordinate direction
        faceJacobVecSizeFlxPnts[iFlxPnt] = faceJacobVecAbsSizeFlxPnts*((*m_faceLocalDir)[iFace]);
 
	// set unit normal vector
        unitNormalFlxPnts.push_back(faceJacobVecs[iFlxPnt]/faceJacobVecAbsSizeFlxPnts);
      }
	
      for (CFuint iFlxPnt = 0; iFlxPnt < m_nbrFaceFlxPnts; ++iFlxPnt)
      {
        const CFuint currFlxIdx = (*m_faceFlxPntConn)[iFace][iFlxPnt];
    
        // reset the grads in the flx pnts
        for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
        {
          *(m_cellGradFlxPnt[0][iFlxPnt][iVar]) = 0.0;
        }
        
        *(m_cellStatesFlxPnt[0][iFlxPnt]) = 0.0;

        // loop over the sol pnts to compute the states and grads in the flx pnts
        for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
        {
	  *(m_cellStatesFlxPnt[0][iFlxPnt]) += (*m_solPolyValsAtFlxPnts)[currFlxIdx][iSol]*(*((*(m_cellStates))[iSol]));
	  
          for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
          {
            *(m_cellGradFlxPnt[0][iFlxPnt][iVar]) += (*m_solPolyValsAtFlxPnts)[currFlxIdx][iSol]*((*(m_cellGrads[0][iSol]))[iVar]);
          }
        }
      }
	
      // compute ghost gradients
      (*m_bcStateComputers)[(*m_faceBCIdxCell)[iFace]]->computeGhostGradients(m_cellGradFlxPnt[0],m_flxPntGhostGrads,unitNormalFlxPnts,m_flxPntCoords);
	
      for (CFuint iFlxPnt = 0; iFlxPnt < m_nbrFaceFlxPnts; ++iFlxPnt)
      {
	const CFuint currFlxIdx = (*m_faceFlxPntConn)[iFace][iFlxPnt];
	
	CFreal epsilon = 0.0;
	
	// loop over the sol pnts to compute the states and grads in the flx pnts
        for (CFuint iNode = 0; iNode < m_faceNodes->size(); ++iNode)
        {
	  for (CFuint iNodeCell = 0; iNodeCell < m_nbrCornerNodes; ++iNodeCell)
          {
	    if ((*m_faceNodes)[iNode]->getLocalID() == (*m_cellNodes)[iNodeCell]->getLocalID())
	    {
              //const CFuint nodeIdx = (*m_faceNodes)[iNode]->getLocalID();
	      // get node local index
              const CFuint nodeIdx = (*m_cellNodesConn)(m_cell->getID(),iNodeCell);
	  
              epsilon += m_nodePolyValsAtFlxPnts[currFlxIdx][iNodeCell]*m_nodeEpsilons[nodeIdx]/m_nbNodeNeighbors[nodeIdx];
	    }
	  }
        }
	
	if (!m_jacob)
	{
	  // adding updateCoeff
	  CFreal visc = 1.0;
  
          m_waveSpeedUpd[0] = 0.0;

          const CFreal jacobXJacobXIntCoef = faceJacobVecSizeFlxPnts[iFlxPnt]*
                                             faceJacobVecSizeFlxPnts[iFlxPnt]*
                                             (*m_faceIntegrationCoefs)[iFlxPnt]*
                                             m_cflConvDiffRatio;
          const CFreal rho = (*(m_cellStatesFlxPnt[0][iFlxPnt]))[0];
          visc = epsilon/rho;
      
          // transform update states to physical data to calculate eigenvalues
          m_waveSpeedUpd[0] += visc*jacobXJacobXIntCoef/m_cell->computeVolume();

          // loop over the sol pnts of both sides to update the wave speeds
          for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
          {
            const CFuint solID = (*m_cellStates)[iSol]->getLocalID();
            updateCoeff[solID] += m_waveSpeedUpd[0];
          }
	}
	
        // compute the average sol and grad to use the BR2 scheme
        for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
        {
	  if (m_cell->getID() == 1092) CFLog(VERBOSE, "var: " << iVar << ", grad: " << *(m_cellGradFlxPnt[0][iFlxPnt][iVar]) << ", ghost: " << *(m_flxPntGhostGrads[iFlxPnt][iVar]) << "\n");
          *(m_avgGrad[iVar]) = (*(m_cellGradFlxPnt[0][iFlxPnt][iVar]) + *(m_flxPntGhostGrads[iFlxPnt][iVar]))/2.0;
        }
              
        m_flxPntRiemannFlux[iFlxPnt] = 0.0;
	      
        for (CFuint iDim = 0; iDim < m_dim; ++iDim)
        {
          for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
          {
            m_flxPntRiemannFlux[iFlxPnt][iVar] += epsilon*((*(m_avgGrad[iVar]))[iDim])*unitNormalFlxPnts[iFlxPnt][iDim];
	    if (m_cell->getID() == 1092) CFLog(VERBOSE, "avgrad: " << (*(m_avgGrad[iVar]))[iDim] << "\n");
          }
        }
     
        // compute FI in the mapped coord frame
        m_cellFlx[0][iFlxPnt] = (m_flxPntRiemannFlux[iFlxPnt])*faceJacobVecSizeFlxPnts[iFlxPnt]; 
	if (m_cell->getID() == 1092) CFLog(VERBOSE, "riemannunit: " << m_flxPntRiemannFlux[iFlxPnt] << "jacob: " << faceJacobVecSizeFlxPnts[iFlxPnt] << "\n");
	
	for (CFuint iSolPnt = 0; iSolPnt < m_nbrSolPnts; ++iSolPnt)
        {  
          const CFreal divh = m_corrFctDiv[iSolPnt][currFlxIdx];

          if (fabs(divh) > MathTools::MathConsts::CFrealEps())
          {   
            // Fill in the corrections
            for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
            {
              residuals[iSolPnt][iVar] += (m_cellFlx[0][iFlxPnt][iVar] - m_extrapolatedFluxes[currFlxIdx][iVar]) * divh; 
	      if (m_cell->getID() == 1092) CFLog(VERBOSE, "riemann: " << m_cellFlx[0][iFlxPnt][iVar] << ", extr: " << m_extrapolatedFluxes[currFlxIdx][iVar] << "\n");
            }
          }
        }
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::setCellData()
{
  DiffRHSFluxReconstruction::setCellData();
  
  m_cellNodes = m_cell->getNodes();
  
  // loop over flx pnts to extrapolate the states to the flux points
  for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
  {   
    //m_solEpsilons[iSol] = m_cellEpsilons[m_cell->getID()];
    
    // reset the states in the flx pnts
    m_solEpsilons[iSol] = 0.0;

    // loop over the sol pnts to compute the states and grads in the flx pnts
    for (CFuint iNode = 0; iNode < m_nbrCornerNodes; ++iNode)
    {
      // get node local index
      //const CFuint nodeIdx = (*m_cellNodesConn)(m_elemIdx,iNode);
      
      const CFuint nodeIdx = (*m_cellNodes)[iNode]->getLocalID();
      
      m_solEpsilons[iSol] += m_nodePolyValsAtSolPnts[iSol][iNode]*m_nodeEpsilons[nodeIdx]/m_nbNodeNeighbors[nodeIdx];
      //CFLog(VERBOSE, "solEps: " << m_solEpsilons[iSol] << ", nodeEps: " << m_nodeEpsilons[nodeIdx] << ", nghb: " << m_nbNodeNeighbors[nodeIdx] << "\n");
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeProjStates(std::vector< RealVector >& projStates)
{
  if (m_order != 1)
  {
    for (CFuint iEq = 0; iEq < m_nbrEqs; ++iEq)
    {
      RealVector temp(projStates.size());
    
      for (CFuint iSol = 0; iSol < projStates.size(); ++iSol)
      {
        temp[iSol] = (*((*m_cellStates)[iSol]))[iEq];
      }

      RealVector tempProj(projStates.size());

      tempProj = m_transformationMatrix*temp;

      for (CFuint iSol = 0; iSol < projStates.size(); ++iSol)
      {
        projStates[iSol][iEq] = tempProj[iSol];
      }
    }
  }
  else
  {
    for (CFuint iEq = 0; iEq < m_nbrEqs; ++iEq)
    {
      CFreal stateSum = 0.0;
      
      for (CFuint iSol = 0; iSol < projStates.size(); ++iSol)
      {
        stateSum += (*((*m_cellStates)[iSol]))[iEq];
      }

      stateSum /= projStates.size();

      for (CFuint iSol = 0; iSol < projStates.size(); ++iSol)
      {
        projStates[iSol][iEq] = stateSum;
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeEpsilon()
{
  computeEpsilon0();
  
  computeSmoothness();
  
  if (m_s < m_s0 - m_kappa)
  {
    m_epsilon = 0.0;
  }
  else if (m_s > m_s0 + m_kappa)
  {
    m_epsilon = m_epsilon0;
  }
  else
  {
    m_epsilon = m_epsilon0*0.5*(1.0 + sin(0.5*MathTools::MathConsts::CFrealPi()*(m_s-m_s0)/m_kappa));
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeEpsilon0()
{ 
  // get the datahandle of the update coefficients
  DataHandle<CFreal> updateCoeff = socket_updateCoeff.getDataHandle();
  
  const CFreal wavespeed = updateCoeff[(*m_cellStates)[0]->getLocalID()];
  
  const CFreal deltaKsi = 1.0/(m_order+2.0);
  
  m_epsilon0 = wavespeed*(2.0/m_peclet - deltaKsi/m_peclet);

}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::computeSmoothness()
{ 
  CFreal sNum = 0.0;
  
  CFreal sDenom = 0.0;
  
  for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
  {
    CFreal stateP = (*((*m_cellStates)[iSol]))[0];
    CFreal diffStatesPPMinOne = stateP - m_statesPMinOne[iSol][0];
    sNum += diffStatesPPMinOne*diffStatesPPMinOne;
    sDenom += stateP*stateP;
  }
  if (sNum <= MathTools::MathConsts::CFrealEps() || sDenom <= MathTools::MathConsts::CFrealEps())
  {
    m_s = -100.0;
  }
  else
  {
    m_s = log10(sNum/sDenom);
  }
  CFLog(VERBOSE, "S = " << m_s << ", num = " << sNum << ", denom = " << sDenom << "\n");
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::storeEpsilon()
{ 
  for (CFuint iNode = 0; iNode < m_nbrCornerNodes; ++iNode)
  {
    // get node ID
    const CFuint nodeID = (*m_cellNodes)[iNode]->getLocalID();

    if (!m_useMax) 
    {
      m_nodeEpsilons[nodeID] += m_epsilon;
      m_cellEpsilons[m_cell->getID()] = m_epsilon;
      m_totalEps += m_epsilon;
    }
    else
    {
      const CFreal maxEps = max(m_epsilon, m_cellEpsilons[m_cell->getID()]);
      m_nodeEpsilons[nodeID] += maxEps;
      m_cellEpsilons[m_cell->getID()] = maxEps;
      m_totalEps += maxEps;
    }
    
    if (m_flagComputeNbNghb)
    {
      m_nbNodeNeighbors[nodeID] += 1.0;
    }
  }
  
  if (m_epsilon > 0.5*m_epsilon0) CFLog(VERBOSE, "cellID eps: " << m_cell->getID() << "\n");
  CFLog(VERBOSE, "eps0 = " << m_epsilon0 << ", eps = " << m_epsilon << ", S = " << m_s << ", S0 = " << m_s0 << "\n");
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::setup()
{
  CFAUTOTRACE;
  DiffRHSFluxReconstruction::setup();

  // get the update varset
  m_updateVarSet = getMethodData().getUpdateVar();
  
  // get CellToFaceGeBuilder
  m_cellBuilder = getMethodData().getCellBuilder();
  m_isFaceOnBoundaryCell = m_cellBuilder->getGeoBuilder()->getIsFaceOnBoundary();
  m_nghbrCellSideCell    = m_cellBuilder->getGeoBuilder()->getNeighbrCellSide ();
  m_currCellSideCell     = m_cellBuilder->getGeoBuilder()->getCurrentCellSide ();
  m_faceOrientsCell      = m_cellBuilder->getGeoBuilder()->getFaceOrient      ();
  m_faceBCIdxCell        = m_cellBuilder->getGeoBuilder()->getFaceBCIdx       ();
  
  // get BC state computers
  m_bcStateComputers = getMethodData().getBCStateComputers();
  
  // get cell-node connectivity
  m_cellNodesConn = MeshDataStack::getActive()->getConnectivity("cellNodes_InnerCells");
  
  // get the local spectral FD data
  vector< FluxReconstructionElementData* >& frLocalData = getMethodData().getFRLocalData();
  cf_assert(frLocalData.size() > 0);
  // for now, there should be only one type of element
  cf_assert(frLocalData.size() == 1);
  
  const CFPolyOrder::Type order = frLocalData[0]->getPolyOrder();
  
  // get the coefs for extrapolation of the node artificial viscosities to the flx pnts
  m_nodePolyValsAtFlxPnts = frLocalData[0]->getNodePolyValsAtPnt(*(frLocalData[0]->getFlxPntsLocalCoords()));
  
  // get the coefs for extrapolation of the node artificial viscosities to the sol pnts
  m_nodePolyValsAtSolPnts = frLocalData[0]->getNodePolyValsAtPnt(*(frLocalData[0]->getSolPntsLocalCoords()));
  
  m_order = static_cast<CFuint>(order);

  // number of cell corner nodes
  /// @note in the future, hanging nodes should be taken into account here
  m_nbrCornerNodes = frLocalData[0]->getNbrCornerNodes();
  
  // get the number of nodes in the mesh
  const CFuint nbrNodes = MeshDataStack::getActive()->getNbNodes();
  
  // get the elementTypeData
  SafePtr< vector<ElementTypeData> > elemType = MeshDataStack::getActive()->getElementTypeData();
  
  // get the number of cells in the mesh
  const CFuint nbrCells = (*elemType)[0].getEndIdx();
  
  m_nodeEpsilons.resize(nbrNodes);
  m_nbNodeNeighbors.resize(nbrNodes);
  m_cellEpsilons.resize(nbrCells);
  m_solEpsilons.resize(m_nbrSolPnts);
  m_epsilonLR.resize(2);
  m_epsilonLR[LEFT].resize(m_nbrFaceFlxPnts);
  m_epsilonLR[RIGHT].resize(m_nbrFaceFlxPnts);
  
  for (CFuint iSol = 0; iSol < m_nbrSolPnts; ++iSol)
  {
    RealVector temp(m_nbrEqs);
    temp = 0.0;
    m_statesPMinOne.push_back(temp);
  }
  
  SafePtr<RealMatrix> vdm = frLocalData[0]->getVandermondeMatrix();
  
  SafePtr<RealMatrix> vdmInv = frLocalData[0]->getVandermondeMatrixInv();
  
  RealMatrix temp(m_nbrSolPnts,m_nbrSolPnts);
  temp = 0.0;
  for (CFuint idx = 0; idx < (m_order)*(m_order); ++idx)
  {
    temp(idx,idx) = 1.0;
  }
  
  m_transformationMatrix.resize(m_nbrSolPnts,m_nbrSolPnts);
  
  m_transformationMatrix = (*vdm)*temp*(*vdmInv);
  
  m_s0 = -3.0*log10(static_cast<CFreal>(m_order));
  
  m_nbNodeNeighbors = 0.0;
  
  m_flagComputeNbNghb = true;
  
  m_flxPntGhostGrads.resize(m_nbrFaceFlxPnts);
  
  for (CFuint iFlx = 0; iFlx < m_nbrFaceFlxPnts; ++iFlx)
  {
    for (CFuint iVar = 0; iVar < m_nbrEqs; ++iVar)
    {
      m_flxPntGhostGrads[iFlx].push_back(new RealVector(m_dim));
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void LLAVFluxReconstruction::unsetup()
{
  CFAUTOTRACE;
  
  for (CFuint iFlx = 0; iFlx < m_nbrFaceFlxPnts; ++iFlx)
  {
    for (CFuint iGrad = 0; iGrad < m_nbrEqs; ++iGrad)
    {
      deletePtr(m_flxPntGhostGrads[iFlx][iGrad]); 
    }
    m_flxPntGhostGrads[iFlx].clear();
  }
  m_flxPntGhostGrads.clear();
  
  DiffRHSFluxReconstruction::unsetup();
}


//////////////////////////////////////////////////////////////////////////////

  }  // namespace FluxReconstructionMethod
}  // namespace COOLFluiD
