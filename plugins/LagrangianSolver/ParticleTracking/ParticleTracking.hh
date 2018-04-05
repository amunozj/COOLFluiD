#ifndef COOLFluiD_RadiativeTransfer_ParticleTracking_hh
#define COOLFluiD_RadiativeTransfer_ParticleTracking_hh

//////////////////////////////////////////////////////////////////////////////

#include "Framework/SocketBundleSetter.hh"
#include "LagrangianSolver/ParticleData.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {

namespace LagrangianSolver {

//////////////////////////////////////////////////////////////////////////////

class ParticleTracking : public Framework::SocketBundleSetter
{
public:

  //typedef Environment::ConcreteProvider<ParticleTracking,1> PROVIDER;
  //typedef const std::string& ARG1;

  /**
   * Constructor
   */
  ParticleTracking(const std::string& name);
  
  /**
   * Default destructor
   */
  ~ParticleTracking();

  enum FaceType {WALL_FACE=1, INTERNAL_FACE = 2, COMP_DOMAIN_FACE=3, BOUNDARY_FACE=4};

  virtual void newParticle( CommonData particleCommonData ){
    m_particleCommonData = particleCommonData;

    m_particleCommonData.currentPoint[0]=particleCommonData.currentPoint[0];
    m_particleCommonData.currentPoint[1]=particleCommonData.currentPoint[1];
    m_particleCommonData.currentPoint[2]=particleCommonData.currentPoint[2];

    m_particleCommonData.direction[0]= particleCommonData.direction[0];
    m_particleCommonData.direction[1]= particleCommonData.direction[1];
    m_particleCommonData.direction[2]= particleCommonData.direction[2];
  }
  
  virtual void setupAlgorithm();
  
  virtual void getCommonData(CommonData &data)=0;

  virtual void newDirection(RealVector &direction)=0;

  virtual void trackingStep()=0;

  virtual void getExitPoint(RealVector &exitPoint) = 0;

  virtual CFreal getStepDistance() = 0;

  virtual void getNormals(CFuint faceID, RealVector &CartPosition, RealVector &faceNormal) = 0;

  CFuint getExitCellID(){return m_exitCellID;}

  CFuint getExitFaceID(){return m_exitFaceID;}

  void setFaceTypes(MathTools::CFMat<CFint> & wallTypes, 
		    std::vector<std::string>& wallNames,
                    std::vector<std::string>& boundaryNames);
  
protected: // functions
  void getAxiNormals(CFuint faceID, RealVector &CartPosition, RealVector &faceNormal);
  
  void getCartNormals(CFuint faceID, RealVector &CartPosition, RealVector &faceNormal);
  
protected: // data
  
  MPI_Datatype m_particleDataType;
  CommonData m_particleCommonData;
  
  CFuint m_exitFaceID;
  CFuint m_entryFaceID;
  CFuint m_exitCellID;
  CFuint m_entryCellID;
  CFuint m_cellIdx;
  CFuint m_faceIdx;
  CFuint m_dim; 
  
  Framework::DataHandle<CFreal> m_normals;
  RealVector m_cartNormal;
  
};

//////////////////////////////////////////////////////////////////////////////

} // namespace RadiativeTransfer

} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////

#endif // COOLFluiD_RadiativeTransfer_ParticleTracking_hh
