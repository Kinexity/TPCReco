/**
 * @file GELIPrimaryGeneratorAction.hh
 * @author     Piotr Podlaski
 * @brief      Definition of GELIPrimaryGeneratorAction class
 */

#ifndef GELIPrimaryGeneratorAction_h
#define GELIPrimaryGeneratorAction_h 1

#include <G4VPrimaryGenerator.hh>
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4VUserPrimaryParticleInformation.hh>

/// \cond
#include <string>
/// \endcond
#include "CentralConfig.hh"
#include "DataBuffer.h"
#include "CommonDefinitions.h"


class G4ParticleGun;

class G4Event;

class G4DataVector;

class G4ParticleDefinition;


class GELIPrimaryParticleInfo : public G4VUserPrimaryParticleInformation {
public:
    explicit GELIPrimaryParticleInfo(size_t id) : primId{id} {}

    void Print() const override;


    size_t primId;
};


class GELIPrimaryGenerator : public G4VPrimaryGenerator {
public:
    explicit GELIPrimaryGenerator(DataBuffer &buf) : buffer{buf} {}

    void GeneratePrimaryVertex(G4Event *evt) override;

private:
    static const G4ParticleDefinition *GetGeantParticleDefinition(const pid_type particleID);

    DataBuffer &buffer;

};


class GELIPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
public:
    explicit GELIPrimaryGeneratorAction(DataBuffer &buffer) :
            primaryGenerator(buffer) {}

    void GeneratePrimaries(G4Event *anEvent) override { primaryGenerator.GeneratePrimaryVertex(anEvent); }

private:
    GELIPrimaryGenerator primaryGenerator;
};


#endif


