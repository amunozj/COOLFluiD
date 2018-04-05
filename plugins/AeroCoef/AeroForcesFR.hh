#ifndef COOLFluiD_AeroCoef_AeroForcesFR_hh
#define COOLFluiD_AeroCoef_AeroForcesFR_hh

//////////////////////////////////////////////////////////////////////////////

#include "Environment/SingleBehaviorFactory.hh"
#include "Environment/FileHandlerOutput.hh"
#include "MathTools/FunctionParser.hh"
#include "Framework/DataProcessingData.hh"
#include "Framework/DynamicDataSocketSet.hh"
#include "Framework/DataSocketSink.hh"
#include "Framework/FaceTrsGeoBuilder.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {
  
  namespace Framework
  {
    class FaceToCellGEBuilder;
  }
  namespace Physics {
    namespace NavierStokes {
      class EulerVarSet;
    }
  }
  
  namespace FluxReconstructionMethod {
    class FluxReconstructionSolverData;
    class CellToFaceGEBuilder;
  }
  
  namespace AeroCoef {
    
//////////////////////////////////////////////////////////////////////////////

/**
 * This class computes the Wall values and aerodynamic coefficients for the FR method
 *
 * @author Ray Vandenhoeck
 *
 */
class AeroForcesFR : public Framework::DataProcessingCom {
public:

  /**
   * Defines the Config Option's of this class
   * @param options a OptionList where to add the Option's
   */
  static void defineConfigOptions(Config::OptionList& options);

  /**
   * Constructor.
   */
  AeroForcesFR(const std::string& name);

  /**
   * Default destructor
   */
  ~AeroForcesFR();

  /**
   * Configure the command
   */
  virtual void configure ( Config::ConfigArgs& args );

  /**
   * Returns the DataSocket's that this command needs as sinks
   * @return a vector of SafePtr with the DataSockets
   */
  virtual std::vector<Common::SafePtr<Framework::BaseDataSocketSink> > needsSockets();

  /**
   * Returns the DataSocket's that this command provides as sources
   * @return a vector of SafePtr with the DataSockets
   */
  virtual std::vector<Common::SafePtr<Framework::BaseDataSocketSource> > providesSockets();

  /**
   * Set up private data and data of the aggregated classes
   * in this command before processing phase
   */
  virtual void setup();

  /**
   * Unset up private data and data of the aggregated classes
   * in this command
   */
  virtual void unsetup();

protected:
  
  /// Update values to be printed and the corresponding residual
  void updateValuesMatAndResidual(const CFuint iVar, 
				  const CFuint index, 
				  const CFreal value)
  {
    cf_assert(iVar < m_valuesMatRes.nbRows());
    cf_assert(index < m_valuesMatRes.nbCols());
    cf_assert(iVar < m_valuesMat.nbRows());
    cf_assert(index < m_valuesMat.nbCols());
    m_valuesMatRes(iVar, index) = value - m_valuesMat(iVar, index);
    m_valuesMat(iVar, index) = value;
  }
  
  
  /// Initialize the surface residuals
  virtual void initSurfaceResiduals();
  
  /// Compute and output to screen the residuals of surface quantities of interest
  virtual void computeSurfaceResiduals();
  
  /// Reorder the file with the wall data
  virtual void reorderOutputFileWall();
  
  /**
   * Execute on a set of dofs
   */
  virtual void executeOnTrs();
  
  /**
   * Compute the values at the wall and write them to file
   */
  virtual void computeWall();
  
  /**
   * Compute the aerodynamic coefficients and write them to file
   */
  virtual void computeAero();
  
  /**
   * Open the Output File and Write the header
   */
  virtual void prepareOutputFileWall();

  /**
   * Open the Output File and Write the header
   */
  virtual void prepareOutputFileAero();

  /**
   * Write the aerodynamic coefficients to file
   */
  virtual void updateOutputFileAero();
  
  /**
   * Write the aerodynamic coefficients to file
   */
  virtual void updateOutputFileWall();
  
  /**
   * 
   */
  virtual void computeWallStatesGrads();
  
private:

  /**
   * Set the functions for Alpha angle
   * @throw Common::ParserException if the expression is senseless
   * @throw BadValueException if the expression defines more than one functions for alpha
   */
  void setFunction();
  
  /**
   * Compute the wet surface area
   */
  void computeWetSurface();
  
protected:
  
  /// socket for gradients
  Framework::DataSocketSink< std::vector< RealVector > > socket_gradients;
  
  /// socket for size of projection vector in face flux points
  Framework::DataSocketSink<  std::vector< CFreal > > socket_faceJacobVecSizeFaceFlxPnts;

  /// the dynamic sockets in this Command
  Framework::DynamicDataSocketSet<> m_sockets;
  
  /// the socket to the data handle of the state's
  Framework::DataSocketSink < Framework::State* , Framework::GLOBAL > socket_states;
  
  /// Update variable set
  Common::SafePtr<Physics::NavierStokes::EulerVarSet> m_updateVarSet;
  
  // pointer to the data of the cell centered FVM method
  Common::SafePtr<FluxReconstructionMethod::FluxReconstructionSolverData> m_frData;
  
  // mapping between faceIDs and global index
  Common::CFMap<CFuint, CFuint> m_mapTrsFaceToID;
  
  /// physical model data
  RealVector m_dataState;
  
  // temporary coordinates of the cell center
  RealVector m_coord;
  
  /// Fx, Fy, Fz friction forces
  RealVector m_frictionForces;
  
  // moment vector
  RealVector m_xyzMoment;
  
  // temporary vector
  RealVector m_v01;
  
  // temporary vector
  RealVector m_v02;
  
  // temporary vector
  RealVector m_vCross0102;
  
  // temporary vector
  RealVector m_v12;
  
  // gravity center
  RealVector m_xg;
  
  // mid face node
  RealVector m_midFaceNode;
  
  // gravity center
  RealMatrix m_rotMat;
  
  // aerodynamic force in xyz frame
  RealVector m_xyzForce;
    
  // aerodynamic force in xyz frame
  RealVector m_force;
  
  // aerodynamic force in wind frame
  RealVector m_aeroForce;
  
  /// array storing the L2 norms of the values to write
  RealVector m_valuesMatL2;
  
  /// array storing the L2 norms of the values to write
  RealVector m_l2Norm;
  
  /// 2D array storing all values to write to file
  RealMatrix m_valuesMat;
  
  /// 2D array storing the residuals of all values to write
  RealMatrix m_valuesMatRes;
  
  /// list of variable names to write
  std::vector<std::string> m_varNames;
  
  /// Alpha function of time
  MathTools::FunctionParser m_functionAlphaParser;
  
  /// Beta function of time
  MathTools::FunctionParser m_functionBetaParser;
  
  /// a vector of string to hold the functions
  std::string m_vars;
  
  /// Temporary Storage for evaluation of Alpha
  RealVector m_eval;
  
  /// current face
  Framework::GeometricEntity* m_currFace;
    
  /// current local face ID
  CFuint m_iFace;
  
  //// Variables to store the wall values + lift/drag
  /// pressure
  CFreal m_p;

  /// pressure coefficient
  CFreal m_Cp;
  
  /// friction coefficient
  CFreal m_Cf;
  
  /// Mach number
  CFreal m_Mach;
  
  /// total lift coefficient 
  CFreal m_lift;
  
  /// total lateral force coefficient 
  CFreal m_lateral;
  
  /// total drag coefficient 
  CFreal m_drag;
    
  /// wet surface used to adimensionalize coefficients
  CFreal m_wetSurface;
  
  /// Incidence of the TRS in degrees
  CFreal m_alphadeg;

  /// Incidence of the TRS in radians
  CFreal m_alpha;
   
  /// Sideslip of the TRS in degrees
  CFreal m_betadeg;
  
  /// Sideslip of the TRS in radians
  CFreal m_beta;
  
  /// Name of Output File where to write the coeficients.
  std::string m_nameOutputFileWall;

  /// Name of Output File where to write the coeficients.
  std::string m_nameOutputFileAero;
  
  /// Flag that says if outputfile must be re-initialized
  bool m_outputFileAeroPrepared;
  
  /// a string to hold the angle of attack function
  std::string m_functionAlpha;
  
  /// a string to hold the sideslip function
  std::string m_functionBeta;
  
  /// velocity at infinity
  CFreal m_uInf;
  
  /// density at infinity
  CFreal m_rhoInf;
  
  /// pressure at infinity
  CFreal m_pInf;
  
  // freestream temperature
  CFreal m_TInf;
  
  ///flag for appending iteration
  bool m_appendIter;

  ///flag for appending time
  bool m_appendTime;
  
  ///flag for reordering the wall data to produce a structured file
  bool m_reorderWallData;
  
  /// ID of temperature in gradient vars
  CFuint m_TID;

  /// IDs of velocity component Vx in gradient vars
  CFuint m_UID;

  /// IDs of velocity component Vy in gradient vars
  CFuint m_VID;

  /// IDs of velocity component Vz in gradient vars
  CFuint m_WID;
  
  /// Reference length (e.g. chord) for scaling 2D aerodynamic coefficients
  CFreal m_refLength2D;
  
  /// Reference area (e.g. chord) for scaling aerodynamic coefficients
  CFreal m_refArea;
  
  /// gravity center
  std::vector<CFreal> m_gravityCenter;
  
  // name of the surface convergence file
  std::string m_outputFileConv;
  
  /// builder of faces
  Common::SafePtr<Framework::GeometricEntityPool<Framework::FaceToCellGEBuilder> > m_faceBuilder;

  /// builder of cells
  Common::SafePtr< Framework::GeometricEntityPool<FluxReconstructionMethod::CellToFaceGEBuilder> > m_cellBuilder;
  
  /// pointer to booleans telling whether a face is on the boundary
  Common::SafePtr< std::vector< bool > > m_isFaceOnBoundary;

  /// pointer to neighbouring cell side vector
  Common::SafePtr< std::vector< CFuint > > m_nghbrCellSide;

  /// pointer to current cell side vector
  Common::SafePtr< std::vector< CFuint > > m_currCellSide;

  /// pointer to orientation vector
  Common::SafePtr< std::vector< CFuint > > m_faceOrients;

  /// pointer to BC index vector
  Common::SafePtr< std::vector< CFuint > > m_faceBCIdx;
  
  /// variable for current face orientation
  CFuint m_orient;
  
  /// variable for current internal cell
  Framework::GeometricEntity* m_intCell;
  
  /// the states in the neighbouring cell
  std::vector< Framework::State* >* m_cellStates;
  
  /// number of flux pnts on a face
  CFuint m_nbrFaceFlxPnts;
  
  /// face mapped coordinates of the flux points
  Common::SafePtr< std::vector<RealVector> > m_flxLocalCoords;
  
  /// the gradients in the neighbouring cell
  std::vector< std::vector< RealVector >* > m_cellGrads;
  
  /// the corrected gradients in the flux points
  std::vector< std::vector< RealVector* > > m_cellGradFlxPnt;
  
  /// vector containing pointers to the face normals
  Common::SafePtr< std::vector< RealVector > > m_faceNormals;
  
  /// flx pnt - face connectivity
  Common::SafePtr< std::vector< std::vector< CFuint > > > m_faceFlxPntConn;
  
  /// face connectivity per orient
  Common::SafePtr< std::vector< std::vector< CFuint > > > m_faceConnPerOrient;
  
  /// coefficients for integration over a face
  Common::SafePtr< RealVector > m_faceIntegrationCoefs;
  
  /// extrapolated states in the flux points of the cell
  std::vector< Framework::State* > m_cellStatesFlxPnt;
  
  /// unit normal vector in flux points
  std::vector< RealVector > m_unitNormalFlxPnts;
  
  /// coefs to extrapolate the states to the flx pnts
  Common::SafePtr< std::vector< std::vector< CFreal > > > m_solPolyValsAtFlxPnts;
  
  /// number of equations in the physical model
  CFuint m_nbrEqs;
  
  /// number of solution pnts in the cell
  CFuint m_nbrSolPnts;
  
  /// number of dimensions in the physical model
  CFuint m_dim;
  
  /// face Jacobian vector sizes (abs)
  std::vector< CFreal > m_faceJacobVecAbsSizeFlxPnts;
  
  /// local cell face - mapped coordinate direction per orientation
  Common::SafePtr< std::vector< CFint > > m_faceMappedCoordDir;
  
  /// face Jacobian vector sizes
  std::vector< CFreal > m_faceJacobVecSizeFlxPnts;
    
}; /// end of class AeroForcesFR

//////////////////////////////////////////////////////////////////////////////

    } /// namespace AeroCoef

} /// namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////

#endif /// COOLFluiD_AeroCoef_AeroForcesFR_hh
