#ifndef COOLFluiD_Numerics_FiniteVolume_MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm_hh
#define COOLFluiD_Numerics_FiniteVolume_MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm_hh

//////////////////////////////////////////////////////////////////////////////

#include "Common/SafePtr.hh"
#include "Framework/State.hh"
#include "FiniteVolume/ComputeSourceTermFVMCC.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {

  namespace Framework {
    class GeometricEntity;
  }

  namespace Physics {
    namespace MHD {
      class MHD3DProjectionPolytropicVarSet;
    }
  }

  namespace Numerics {

    namespace FiniteVolume {

//////////////////////////////////////////////////////////////////////////////

/**
 * This class represents Coriolis and centrifugal source terms necessary for
 * corotating reference frames for 3D conservative variables used with the
 * hyperbolic divergence cleaning method on the polytropic modelling of 
 * the solar wind
 *
 * @author Mehmet Sarp Yalim
 *
 */
class MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm : public ComputeSourceTermFVMCC {

public:

  /**
   * Constructor
   * @see ComputeSourceTermFVMCC
   */
  MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm(const std::string& name);

  /**
   * Default destructor
   */
  ~MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm();

  /**
   * Defines the Config Option's of this class
   * @param options a OptionList where to add the Option's
   */
  static void defineConfigOptions(Config::OptionList& options);

  /**
   * Configure the object
   */
  virtual void configure ( Config::ConfigArgs& args )
  {
    ComputeSourceTermFVMCC::configure(args);
    
    _globalSockets.createSocketSink<Framework::State*>("states");
  }

  /**
   * Set up private data and data of the aggregated classes
   * in this command before processing phase
   */
  void setup();
 
  /**
   * Compute the source term
   */
  void computeSource(Framework::GeometricEntity *const element,
		     RealVector& source,
		     RealMatrix& jacobian);
  
private: // data

  /// corresponding variable set
  Common::SafePtr<Physics::MHD::MHD3DProjectionPolytropicVarSet> _varSet;

  /// x-component of the angular velocity of the external object
  CFreal _OmegaX;

  /// y-component of the angular velocity of the external object
  CFreal _OmegaY;

  /// z-component of the angular velocity of the external object
  CFreal _OmegaZ;

  /// MHD physical data
  RealVector _physicalData;

  /// MHD physical data
  RealVector _dataLeftState;

   /// MHD physical data
  RealVector _dataRightState;

}; // end of class MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm

//////////////////////////////////////////////////////////////////////////////

    } // namespace FiniteVolume

  } // namespace Numerics

} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////

#endif // COOLFluiD_Numerics_FiniteVolume_MHD3DProjectionPolytropicCoriolisCentrifugalForcesSourceTerm_hh
