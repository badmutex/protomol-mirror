#include <protomol/topology/GenericTopology.h>

#include <protomol/debug/Exception.h>

using namespace std;
using namespace ProtoMol;

//____ GenericTopology

const string GenericTopology::scope("Topology");
const string GenericTopology::keyword("Topology");

GenericTopology::GenericTopology() :
  Makeable(), exclude(ExclusionType::ONE4MODIFIED), coulombScalingFactor(1.0),
  time(0.0), min(Vector3D(Constant::MAXREAL, Constant::MAXREAL,
                          Constant
                            ::
                            MAXREAL)),
  max(Vector3D(-Constant::MINREAL, -Constant::MINREAL,
               -
               Constant
                 ::MINREAL)),
  doSCPISM(false), minimalMolecularDistances(false) {}

GenericTopology::GenericTopology(Real c, const ExclusionType &e) :
  Makeable(), exclude(e), coulombScalingFactor(c), time(0.0),
  min(Vector3D(Constant::MAXREAL, Constant::MAXREAL,
               Constant
                 ::
                 MAXREAL)), max(Vector3D(-Constant::MINREAL, -Constant::MINREAL,
                                         -
                                         Constant
                                           ::MINREAL)), doSCPISM(false),
  minimalMolecularDistances(false) {}

GenericTopology *GenericTopology::make(const vector<Value> &values) const {
  assertParameters(values);

  return adjustAlias(doMake(values));
}
