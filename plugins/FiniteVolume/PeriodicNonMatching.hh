#ifndef COOLFluiD_Numerics_FiniteVolume_PeriodicX2D_hh
#define COOLFluiD_Numerics_FiniteVolume_PeriodicX2D_hh

//////////////////////////////////////////////////////////////////////////////

#include "Framework/State.hh"
#include "FiniteVolume/FVMCC_BC.hh"
#include "Common/CFMap.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {

  namespace Numerics {

    namespace FiniteVolume {

//////////////////////////////////////////////////////////////////////////////

  /**
   * This class represents a periodic boundary condition command for two
   * topological regions in 2D parallel to whatever direction you want
   * for CellCenterFVM schemes (for serial simulations ??)
   *
   * @author Alessandro Sanna
   *
   */
class PeriodicFace {
public:

  //default constructor
  PeriodicFace();

  //default destructor
  ~PeriodicFace();

  /*
  *** setup private data
  */
  //set first node
  void setupFirstNode(Node& N1)
  {
    vertices[1] = N1;
  };
  
  //set second node
  void setupSecondNode(Node& N2)
  {
    vertices[2] = N2;
  };
 
  //set identification
  void setup(CFuint ID)
  {
    FaceID = ID;
  };

  /*
  *** get private data
  */
  //get first vertice
  Node& getFirstNode()
  {
    return vertices[1];
  };

  //get second vertice
  Node& getSecondNode()
  {
    return vertices[2];
  };

private: //data

  ///vector of nodes belonging to the face
  std::vector<Node&> vertices;

  ///identifier of the faces
  CFuint FaceID;

}; // end of class PeriodicFace


/*
* ______________________________________________________________________________________________
*/

class PeriodicNonMatching : public FVMCC_BC {
public:

  /**
   * Defines the Config Option's of this class
   * @param options a OptionList where to add the Option's
   */
  static void defineConfigOptions(Config::OptionList& options);

  /**
   * Constructor
   */
  PeriodicNonMatching(const std::string& name);

  /**
   * Default destructor
   */
  ~PeriodicNonMatching();

  /**
   * Set up private data
   */
  void setup ();

  /**
   * Apply boundary condition on the given face
   */
  void setGhostState(Framework::GeometricEntity *const face);

private: //data

  /// vector of new common periodic boundary
  std::vector<PeriodicFace&> PeriodicFaceNew;
  
  /// vector of old bottom periodic boundary
  std::vector<PeriodicFace&> PeriodicFaceBottom;
  
  /// vector of old top periodic boundary
  std::vector<PeriodicFace&> PeriodicFaceTop;

  /// map that maps global topological region face ID to a local one in the topological region set
  Common::CFMap<CFuint,CFuint> _globalToLocalTRSFaceID;
  
}; // end of class PeriodicNonMatching

//////////////////////////////////////////////////////////////////////////////

 } // namespace FiniteVolume

  } // namespace Numerics

} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////

#endif // COOLFluiD_Numerics_FiniteVolume_PeriodicX2D_hh
