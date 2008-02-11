/*  -*- c++ -*-  */
#ifndef OUTPUTSCREEN_H
#define OUTPUTSCREEN_H

#include <protomol/output/Output.h>

namespace ProtoMol {
  class Configuration;

  //____ OutputScreen
  class OutputScreen : public Output {
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Constructors, destructors, assignment
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  public:
    OutputScreen();
    OutputScreen(int freq);

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //  From class Output
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  private:
    virtual Output *doMake(const std::vector<Value> &values) const;
    virtual void doInitialize();
    virtual void doRun(int step);
    virtual void doFinalize(int) {}
    virtual bool isIdDefined(const Configuration *config) const;
    virtual bool addDoKeyword() const {return false;}

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // From class Makeable
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  public:
    virtual std::string getIdNoAlias() const {return keyword;}
    virtual void getParameters(std::vector<Parameter> &) const;
    virtual bool adjustWithDefaultParameters(std::vector<Value> &values,
                                             const Configuration *config) const;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // My data members
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  public:
    static const std::string keyword;
  private:
    std::string myUnit;
    Real myFactor;
  };
}
#endif
