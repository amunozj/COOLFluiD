#ifndef COOLFluiD_FluxReconstructionMethod_MLPLimiterEuler3D_hh
#define COOLFluiD_FluxReconstructionMethod_MLPLimiterEuler3D_hh

//////////////////////////////////////////////////////////////////////////////

#include "Framework/DataSocketSink.hh"

#include "FluxReconstructionMethod/FluxReconstructionSolverData.hh"
#include "FluxReconstructionMethod/MLPLimiter.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {
  
  namespace Physics {
    namespace NavierStokes {
      class Euler3DVarSet;
    }
  }

  namespace FluxReconstructionMethod {

//////////////////////////////////////////////////////////////////////////////

/**
 * This class represent a command that applies an elementwise MLP limiter to the solution,
 * taking into account the requirement of pressure positivty
 *
 * @author Ray Vandenhoeck
 *
 */
class MLPLimiterEuler3D : public MLPLimiter {
public:

  /**
   * Constructor.
   */
  explicit MLPLimiterEuler3D(const std::string& name);

  /**
   * Destructor.
   */
  virtual ~MLPLimiterEuler3D();

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
  virtual void applyChecks(CFreal phi);
  
  /**
   * Check if the states are physical
   */
  virtual bool checkPhysicality();
  
  /**
   * Compute the physical value that should be used to limit the solution in order to make it physical
   */
  virtual CFreal computeLimitingValue(RealVector state);
  
  /**
   * Limit the average cell state to make it physical
   */
  virtual void limitAvgState();
  
  /**
   * check for special physics-dependent conditions if limiting is necessary (for Euler: check whether we are in a supersonic region)
   */
  virtual bool checkSpecialLimConditions();

protected: // data

  /// minimum allowable value for density
  CFreal m_minDensity;

  /// minimum allowable value for pressure
  CFreal m_minPressure;
  
  /// physical model (in conservative variables)
  Common::SafePtr<Physics::NavierStokes::Euler3DVarSet> m_eulerVarSet;

  /// heat capacity ratio minus one
  CFreal m_gammaMinusOne;
  
  /// variable for physical data of sol
  RealVector m_solPhysData;

}; // class MLPLimiterEuler3D

//////////////////////////////////////////////////////////////////////////////

  } // namespace FluxReconstructionMethod

} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////

#endif // COOLFluiD_FluxReconstructionMethod_MLPLimiterEuler3D_hh
