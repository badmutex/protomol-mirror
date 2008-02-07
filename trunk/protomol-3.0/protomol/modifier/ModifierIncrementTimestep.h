/*  -*- c++ -*-  */
#ifndef MODIFIERINCREMENTTIMESTEP_H
#define MODIFIERINCREMENTTIMESTEP_H

#include <protomol/modifier/Modifier.h>
#include <protomol/topology/GenericTopology.h>
#include <protomol/integrator/STSIntegrator.h>

namespace ProtoMol {
  class ModifierIncrementTimestep : public Modifier {
    static const std::string keyword;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Constructors, destructors, assignment
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  public:
    ModifierIncrementTimestep(STSIntegrator *i) :
      Modifier(Constant::MAX_INT), myTheIntegrator(i) {}

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // From class Makeable
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    virtual void getParameters(std::vector<Parameter> &parameters) const {}
    virtual std::string getIdNoAlias() const {return "IncrementTimestep";}

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // From class Modifier
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    virtual bool isInternal() const {return true;}

  private:
    virtual void doExecute() {
      myTopology->time += myTheIntegrator->getTimestep();
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // My data members
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  private:
    STSIntegrator *myTheIntegrator;
  };
}
#endif /* MODIFIERINCREMENTTIMESTEP_H */
