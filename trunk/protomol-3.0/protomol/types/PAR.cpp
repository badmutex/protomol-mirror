#include <protomol/types/PAR.h>

#include <protomol/util/MathUtilities.h>

using namespace ProtoMol;
//____ PAR
const Real PAR::Nonbonded::SIGMA_CHARMM19_TO_CHARMM28(pow(2.0, -5.0 / 6.0));
const Real PAR::Nonbonded::SIGMA_CHARMM28_TO_CHARMM19(pow(2.0, 5.0 / 6.0));

void PAR::clear() {
  bonds.clear();
  angles.clear();
  dihedrals.clear();
  impropers.clear();
  nonbondeds.clear();
  nbfixs.clear();
  hbonds.clear();
}
