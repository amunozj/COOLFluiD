#ifndef COOLFluiD_FluxReconstructionMethod_PhysicalityEuler2D_hh
#define COOLFluiD_FluxReconstructionMethod_PhysicalityEuler2D_hh

//////////////////////////////////////////////////////////////////////////////

#include "Framework/DataSocketSink.hh"

#include "FluxReconstructionMethod/FluxReconstructionSolverData.hh"

#include "FluxReconstructionMethod/BasePhysicality.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {
  
  namespace Physics {
    namespace NavierStokes {
      class Euler2DVarSet;
    }
  }

  namespace FluxReconstructionMethod {

//////////////////////////////////////////////////////////////////////////////

/**
 * This class represent a command that checks and enforces the physicality of an Euler 2D state, particularly the positivity of the pressure
 *
 * @author Ray Vandenhoeck
 *
 */
class PhysicalityEuler2D : public BasePhysicality {
public:

  /**
   * Constructor.
   */
  explicit PhysicalityEuler2D(const std::string& name);

  /**
   * Destructor.
   */
  virtual ~PhysicalityEuler2D();

  /**
   * Defines the Config Option's of this class
   * @param options a OptionList where to add the Option's
   */
  static void defineConfigOptions(Config::OptionList& options);

  /**
   * Setup private data and data of the aggregated classes
   * in this command before processing phase
   */
  virtual void setup();

  /**
   * Unsetup private data
   */
  virtual void unsetup();

  /**
   * Configures the command.
   */
  virtual void configure ( Config::ConfigArgs& args );

protected: // functions

  /**
   * apply pressure possitivity check
   */
  virtual void enforcePhysicality();
  
  /**
   * Check if the states are physical
   */
  virtual bool checkPhysicality();

protected: // data

  /// minimum allowable value for density
  CFreal m_minDensity;

  /// minimum allowable value for pressure
  CFreal m_minPressure;
  
  /// boolean telling wether to also check the internal solution for physicality
  bool m_checkInternal;
  
  /// physical model (in conservative variables)
  Common::SafePtr<Physics::NavierStokes::Euler2DVarSet> m_eulerVarSet;

  /// heat capacity ratio minus one
  CFreal m_gammaMinusOne;
  
  /// variable for physical data of sol
  RealVector m_solPhysData;
  
  /// coefficients for the computation of the cell averaged solution
  Common::SafePtr< RealVector > m_cellAvgSolCoefs;
  
  /// cell averaged state
  RealVector m_cellAvgState;

}; // class PhysicalityEuler2D

//////////////////////////////////////////////////////////////////////////////

  } // namespace FluxReconstructionMethod

} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////

#endif // COOLFluiD_FluxReconstructionMethod_PhysicalityEuler2D_hh
