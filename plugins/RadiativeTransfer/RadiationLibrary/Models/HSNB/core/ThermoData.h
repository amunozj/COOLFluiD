#ifndef COOLFluiD_RadiativeTransfer_THERMODATA_H
#define COOLFluiD_RadiativeTransfer_THERMODATA_H

#include "Common/StringOps.hh"
#include "Framework/PhysicalChemicalLibrary.hh"
#include "Common/CFMap.hh"
#include "Environment/CFEnv.hh"
#include <vector>
#include <string>
#include <iostream>
#include <map>
#include "Common/CFLog.hh"
#include "Environment/ObjectProvider.hh"
#include "Common/BadValueException.hh"
#include "RadiativeTransfer/RadiationLibrary/Models/HSNB/core/SpeciesData.h"
#include "RadiativeTransfer/RadiationLibrary/Models/HSNB/core/RadiationFieldData.h"



//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {

namespace RadiativeTransfer {

//////////////////////////////////////////////////////////////////////////////


///Define a state that is not being held by this process but needed temporarily for the
/// computation of absorption of a photon emitted by another process
struct ExternalState{
    ExternalState(CFreal* N):  m_N(N) {
        active=false;
    }

    ExternalState(){
        active=false;
    }

    void activateState(CFreal* N) {
        m_N=N;
        active=true;
    }

    void deactivate() {
        active=false;
    }

    CFreal* m_N;
    bool active;

};

class ThermoData
{
public:
    ThermoData();

    void setup(const CFuint pressureID, const CFuint trID, const CFuint tvID, RealVector* avogadroOvMM, bool m_convertPartialPressure);
  
    void reset() {
        m_species.clear();
    }

    int nSpecies() const {
        return m_species.size();
    }
    
    /**
     * Returns the index of the species with the given name in the global
     * species array.  If the species is not in the array, then the index is -1.
     */
    int speciesIndex(const std::string& name) const
    {
        for (int i = 0; i < m_species.size(); ++i)
            if (m_species[i].name == name) return i;
        return -1;
    }

    CFuint getLocalCellID(CFuint cellID);

    const SpeciesData& operator[] (size_t i) const { return m_species[i]; }
 
    /**
     * Adds a new species to the species list.
     */
    void addSpecies(const std::string& name)
    {
        m_nbSpecies++;
        // BT: TODO: put species data into file to allow for extension?
        // Hardcoded species for now
        if (name=="e-") {
          emIndex=m_species.size();
          m_species.push_back(SpeciesData(name, -1.0, 0.00055e-3));
        } else if (name=="Ar") {
          m_species.push_back(SpeciesData(name, 0.0, 39.9480e-3));
        } else if (name=="Ar+") {
          m_species.push_back(SpeciesData(name, 1.0, 39.94745e-3));
        } else if (name=="Ar++") {
          m_species.push_back(SpeciesData(name, 2.0, 39.9469e-3));
        } else if (name=="Ar+++") {
          m_species.push_back(SpeciesData(name, 3.0, 39.94635e-3));
        } else if (name=="N-") {
          m_species.push_back(SpeciesData(name, -1.0, 14.00725e-3));
        } else if (name=="N") {
          m_species.push_back(SpeciesData(name, 0, 14.0067e-3));
        } else if (name=="N+") {
          m_species.push_back(SpeciesData(name, 1.0, 14.00615e-3));
        } else if (name=="N++") {
          m_species.push_back(SpeciesData(name, 2.0, 14.0056e-3));
        } else if (name=="N+++") {
          m_species.push_back(SpeciesData(name, 3.0, 14.00505e-3));
        } else if (name=="O-") {
          m_species.push_back(SpeciesData(name, -1.0, 15.99995e-3));
        } else if (name=="O") {
          m_species.push_back(SpeciesData(name, 0, 15.9994e-3));
        } else if (name=="O+") {
          m_species.push_back(SpeciesData(name, 1.0, 15.99885e-3));
        } else if (name=="O++") {
          m_species.push_back(SpeciesData(name, 2.0, 15.9983e-3));
        } else if (name=="O+++") {
          m_species.push_back(SpeciesData(name,  3.0, 15.99775e-3));
        } else if (name=="N2") {
          m_species.push_back(SpeciesData(name, 0, 28.0134e-3));
        } else if (name=="N2+") {
          m_species.push_back(SpeciesData(name, 1.0, 28.01285e-3));
        } else if (name=="O2") {
          m_species.push_back(SpeciesData(name, 0, 31.9988e-3));
        } else if (name=="O2+") {
          m_species.push_back(SpeciesData(name, 1.0, 31.99825e-3));
        } else if (name=="NO") {
          m_species.push_back(SpeciesData(name, 0, 30.0061e-3));
        } else if (name=="NO+") {
          m_species.push_back(SpeciesData(name, 1.0, 30.00555e-3));
        } else if (name=="C-") {
            m_species.push_back(SpeciesData(name, -1.0, 12.01155e-3));
        } else if (name=="C") {
          m_species.push_back(SpeciesData(name, 0, 12.011e-3));
        } else if (name=="C+") {
            m_species.push_back(SpeciesData(name, 1.0, 12.01045e-3));
        } else if (name=="C2") {
          m_species.push_back(SpeciesData(name, 0, 24.022e-3));
        } else if (name=="C3") {
          m_species.push_back(SpeciesData(name, 0, 36.033e-3));
        } else if (name=="CN") {
          m_species.push_back(SpeciesData(name, 0, 26.0177e-3));
        } else if (name=="CO") {
          m_species.push_back(SpeciesData(name, 0, 28.0104e-3));
        } else if (name=="CO+") {
          m_species.push_back(SpeciesData(name, 1.0, 28.00985e-3));
        } else if (name=="CO2") {
          m_species.push_back(SpeciesData(name, 0, 44.0098e-3));
        } else if (name=="H") {
          m_species.push_back(SpeciesData(name, 0, 1.007947e-3));
        } else if (name=="H+") {
          m_species.push_back(SpeciesData(name, 1.0, 1.007397e-3));
        } else if (name=="H2") {
          m_species.push_back(SpeciesData(name, 0, 2.015894e-3));
        } else if (name=="H2+") {
          m_species.push_back(SpeciesData(name, 0, 2.015344e-3));
        } else if (name=="NH") {
          m_species.push_back(SpeciesData(name, 0, 15.014647e-3));
        } else if (name=="CH") {
          m_species.push_back(SpeciesData(name, 0, 13.018947e-3));
        } else if (name=="CH2") {
          m_species.push_back(SpeciesData(name, 0, 14.026894e-3));
        } else if (name=="CH3") {
          m_species.push_back(SpeciesData(name, 0, 15.034841e-3));
        } else if (name=="CH4") {
          m_species.push_back(SpeciesData(name, 0, 16.042788e-3));
        } else if (name=="HCN") {
          m_species.push_back(SpeciesData(name, 0, 27.025647e-3));
        } else if (name=="C2H") {
          m_species.push_back(SpeciesData(name, 0, 25.029947e-3));
        } else {
          std::cout << "Species '" << name << "' is not supported!" << std::endl;
        }

    }
    
    /**
     * Returns the species name witht he given index.
     */
    const std::string& speciesName(int index) const {
        return m_species[index].name;
    }

    /// Might be no need to implement actually?
    CFreal * X() const {
        while (true) {

        }
        return NULL; }
    CFreal X(int i) const {
        while (true) {

        }
        return 1.0; }

    double N(int i) const;
    CFreal *N();
    // TH=Tr?
    double Th() const {
        return m_tr;
    }
    double Tr() const { return m_tr; }
    double Tv() const { return m_tv; }
    double Tel() const { return m_tv; }
    double Te() const { return m_tv; }
    double P() const { return m_p; }

    CFuint nCells() const;

    void setState(CFuint stateIndex);

    void setExternalState(CFreal newTr, CFreal newTv, CFreal newP, CFreal* N);

    void deactivateExternalState();


    void addState(RealVector* stateVector, CFuint localStateID);

    void printState(CFuint stateID);

    CFuint getCurrentLocalCellID() const {
        return m_currentStateID;
    }

private:
    bool isValidLocalID(std::map<CFuint, CFuint>::iterator stateMapIt) const;

    ExternalState m_externalState;
    
    std::vector<SpeciesData> m_species;

    std::vector<RadiationFieldData> m_states;

    RadiationFieldData externalState;

    RealVector* m_avogadroOvMM;

    CFuint m_currentStateID;

    double  m_p;
    double  m_tr;
    double  m_tv;

    bool m_convertPartialPressure=false;

    CFuint m_pressureID;
    CFuint m_trID;
    CFuint m_tvID;
    CFuint m_nbSpecies;

    CFuint emIndex;

    //Map the partition state Id as specified in the
    //radiator m_stateIDs to the local position in m_states
    std::map<CFuint, CFuint> stateIDMap;
    std::map<CFuint, CFuint>::iterator stateMapIt;
};
}
}

#endif // THERMODATA_H
