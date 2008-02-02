#include <protomol/topology/TopologyModule.h>

#include <protomol/topology/Topology.h>
#include <protomol/topology/TopologyFactory.h>
#include <protomol/topology/CubicCellManager.h>
#include <protomol/topology/VacuumBoundaryConditions.h>
#include <protomol/topology/PeriodicBoundaryConditions.h>

#include <protomol/frontend/ProtoMolApp.h>
#include <protomol/topology/ExclusionTable.h>
#include <protomol/topology/GenericTopology.h>
#include <protomol/topology/CoulombSCPISMParameterTable.h>
#include <protomol/topology/LennardJonesParameterTable.h>
#include <protomol/types/PAR.h>
#include <protomol/types/PSF.h>
#include <protomol/util/Report.h>
#include <protomol/util/PMConstants.h>
#include <protomol/util/MathUtilities.h>
#include <protomol/util/StringUtilities.h>
#include <protomol/topology/TopologyUtilities.h>

#include <protomol/frontend/MainModule.h>

#include <protomol/debug/Exception.h>
#include <algorithm>
#include <list>
#include <map>
#include <set>

using namespace std;
using namespace ProtoMol;
using namespace ProtoMol::Report;

defineInputValue(InputBoundaryConditions, "boundaryConditions");
defineInputValue(InputCellManager, "cellManager");
defineInputValue(InputDoSCPISM, "doscpism");
defineInputValue(InputReducedImage, "reducedImage");
defineInputValue(InputTemperature, "temperature");
defineInputValueWithAliasesAndText
(InputRemoveLinearMomentum, "removeLinearMomentum", ("comMotion"),
  "removes linear momentum, where -1 for never, 0 at initialization or at STS "
  "frequency <n>");
defineInputValueWithAliasesAndText
(InputRemoveAngularMomentum, "removeAngularMomentum", ("angularMomentum"),
  "removes angular momentum, where -1 for never, 0 at initialization or at STS "
  "frequency <n>");

void TopologyModule::init(ProtoMolApp *app) {
  Configuration *config = &app->config;
  GenericTopology *topo;
  TopologyFactory *factory = &app->topologyFactory;

  // Vacuum or normal boundary conditions
  topo = new Topology<VacuumBoundaryConditions, CubicCellManager>();
  factory->registerExemplar(topo, Vector<string>("NormalCubic"));

  // Periodic boundary conditions
  topo = new Topology<PeriodicBoundaryConditions, CubicCellManager>();
  factory->registerExemplar(topo);

  // Register input values
  InputBoundaryConditions::registerConfiguration(config);
  InputCellManager::registerConfiguration(config);
  InputDoSCPISM::registerConfiguration(config);
  InputReducedImage::registerConfiguration(config);
  InputTemperature::registerConfiguration(config);
  InputRemoveLinearMomentum::registerConfiguration(config);
  InputRemoveAngularMomentum::registerConfiguration(config);
}

void TopologyModule::configure(ProtoMolApp *app) {
  Configuration &config = app->config;

  // Fix for old topology definition
  if (!config[GenericTopology::keyword].valid())
    config[GenericTopology::keyword] =
      config[InputBoundaryConditions::keyword].getString() +
      config[InputCellManager::keyword].getString();
}

void TopologyModule::buildTopology(ProtoMolApp *app) {
  Configuration &config = app->config;
  Vector3DBlock *velocities = &app->velocities;
  Vector3DBlock *positions = &app->positions;
  GenericTopology *topo = app->topology;

  // Build topology
  if ((bool)config[InputDoSCPISM::keyword])
    topo->doSCPISM = true;

  buildTopology(topo, app->psf, app->par,
    config[InputDihedralMultPSF::keyword]);

  topo->minimalMolecularDistances =
    topo->checkMoleculePairDistances(*positions);

  // Reduce image
  if ((bool)config[InputReducedImage::keyword] &&
      !topo->minimalMolecularDistances) {
    Vector3DBlock tmp(*positions);

    topo->minimalImage(tmp);

    if (topo->checkMoleculePairDistances(tmp)) {
      *positions = tmp;
      report << plain << "Fixed minimal molecule distances." << endr;
      topo->minimalMolecularDistances = true;
    } else {
      report << plain << "Could not fixed minimal molecule distances." << endr;
      topo->minimalMolecularDistances = false;
    }
  }

  // Fix velocities
  if (!config.valid(InputVelocities::keyword)) {
    randomVelocity(config[InputTemperature::keyword],
      topo, velocities, config[InputSeed::keyword]);
    report << plain << "Random temperature : "
           << temperature(topo, velocities) << "K" << endr;
  }

  // Remove Linear Momentum
  if ((int)config[InputRemoveLinearMomentum::keyword] >= 0)
    report
      << plain << "Removed linear momentum: "
      << removeLinearMomentum(velocities, topo) * Constant::INV_TIMEFACTOR
      << endr;

  else
    report << plain << "Linear momentum : "
           << linearMomentum(velocities, topo) * Constant::INV_TIMEFACTOR
           << endr;

  // Remove Angular Momentum
  if ((int)config[InputRemoveAngularMomentum::keyword] >= 0)
    report << plain << "Removed angular momentum : "
           << removeAngularMomentum(positions, velocities, topo) *
    Constant::INV_TIMEFACTOR << endr;

  else
    report << plain << "Angular momentum : "
           << angularMomentum(positions, velocities, topo) *
    Constant::INV_TIMEFACTOR << endr;

  report << plain << "Actual start temperature : "
         << temperature(topo, velocities) << "K" << endr;
}

//____findNextNeighbor
void findNextNeighbor(int a, vector<int> &v, vector<PairInt> &p,
                      vector<char> &unused, const vector<vector<int> > &graph,
                      set<PairInt> &pairs) {
  if (unused[a] > 0) {
    v.push_back(a);
    unused[a] = 0;
    for (unsigned int i = 0; i < graph[a].size(); i++) {
      set<PairInt>::iterator itr =
        pairs.find(PairInt(min(a, graph[a][i]), max(a, graph[a][i])));

      if (itr != pairs.end()) {
        p.push_back(PairInt(a, graph[a][i]));
        pairs.erase(itr);
      }
      findNextNeighbor(graph[a][i], v, p, unused, graph, pairs);
    }
  }
}

void TopologyModule::buildTopology(GenericTopology *topo, const PSF &psf,
                                   const PAR &par,
                                   bool dihedralMultPSF) {
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // First, generate the array of atomtypes
  // Each time a new atom comes up, we need to check if it is
  // already in the vector....
  // NOTE:  this may take a while for large systems; however, it will cut
  // down on the size of the atomTypes vector, and therefore, the amount
  // access time in the back end.
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  topo->atoms.clear();
  topo->atomTypes.clear();
  topo->bonds.clear();
  topo->angles.clear();
  topo->dihedrals.clear();
  topo->impropers.clear();

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // CoulombSCPISMParameters
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  CoulombSCPISMParameterTable *mySCPISM = 0;
  if (topo->doSCPISM) {
    mySCPISM = new CoulombSCPISMParameterTable();
    mySCPISM->populateTable();
    mySCPISM->displayTable();
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Get the atoms
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  map<string, int> atomLookUpTable;

  // loop over all atoms in the PSF object
  for (vector<PSF::Atom>::const_iterator atom = psf.atoms.begin();
       atom != psf.atoms.end(); ++atom) {
    // Two data members for AtomType, name and mass
    Atom tempatom;
    AtomType tempatomtype;
    tempatomtype.name = atom->atom_type;
    tempatomtype.mass = atom->mass;
    tempatomtype.symbolName = atomTypeToSymbolName(atom->atom_type);
    tempatomtype.charge = atom->charge;

    // SCPISM
    if (topo->doSCPISM && mySCPISM->myData.find(tempatomtype.name) !=
        mySCPISM->myData.end()) {
      tempatomtype.mySCPISM = new SCPISMAtomTypeParameters();
      tempatom.mySCPISM = new SCPISMAtomParameters();

      // atom type exists in SCPISM parameters
      tempatomtype.mySCPISM->sqrt_alpha =
        mySCPISM->myData[tempatomtype.name].sqrt_alpha_i;
      tempatomtype.mySCPISM->g_i =
        mySCPISM->myData[tempatomtype.name].hbond_factor;
      tempatomtype.mySCPISM->isHbonded =
        mySCPISM->myData[tempatomtype.name].isHbonded;
      tempatomtype.mySCPISM->A_i = mySCPISM->myData[tempatomtype.name].A_i;
      tempatomtype.mySCPISM->B_i = mySCPISM->myData[tempatomtype.name].B_i;
      tempatomtype.mySCPISM->C_i = mySCPISM->myData[tempatomtype.name].C_i;
      Real R_vdw = mySCPISM->myData[tempatomtype.name].R_vdw + 1.40;
      tempatom.mySCPISM->dR_vdw2 = 1.0 / (4.0 * M_PI * R_vdw * R_vdw);
      tempatom.mySCPISM->r_cov = mySCPISM->myData[tempatomtype.name].r_cov;
      tempatom.mySCPISM->R_iw = mySCPISM->myData[tempatomtype.name].R_iw;
    }

    // Now check if this already exists (same name)
    if (atomLookUpTable.find(tempatomtype.name) == atomLookUpTable.end()) {
      atomLookUpTable[tempatomtype.name] = topo->atomTypes.size();
      topo->atomTypes.push_back(tempatomtype);
    }

    // First, we need to find the index. (an integer corresponding
    // to the type of the atom
    tempatom.name = atom->atom_name;
    tempatom.type = atomLookUpTable[tempatomtype.name];
    tempatom.residue_name = atom->residue_name;
    tempatom.residue_seq = atom->residue_sequence;
    // Now, the scaled charge.  This is straightforward.
    tempatom.scaledCharge = (atom->charge) * Constant::SQRTCOULOMBCONSTANT;
    tempatom.scaledMass = atom->mass;
    if (topo->doSCPISM && tempatom.mySCPISM) {
      tempatom.mySCPISM->R_w =
        tempatom.mySCPISM->r_cov + (atom->charge > 0 ? 0.85 : 0.35);
      tempatom.mySCPISM->R_p =
        tempatom.mySCPISM->R_iw + tempatom.mySCPISM->R_w;
      tempatom.mySCPISM->sqrtalphaSCPISM = tempatomtype.mySCPISM->sqrt_alpha;
      tempatom.mySCPISM->sasaFrac = 0.0;
      tempatom.mySCPISM->polarFrac = 0.0;
      tempatom.mySCPISM->bornRadius = 0.0;
    }
    // Now we need the size of the group for heavy atom ordering
    // We need to parse the name for any H's then any numbers following
    // First, if the atom is an H then this is 0
    if (atom->atom_type == "H") tempatom.hvyAtom = 0;
    else {
      // Otherwise, we need to parse..
      // Initialize to 1
      tempatom.hvyAtom = 1;
      for (unsigned int pos = 0; pos < atom->atom_type.size(); ++pos)
        if (atom->atom_type[pos] == 'H') {
          string number = "";
          while (isdigit(atom->atom_type[++pos]))
            number += atom->atom_type[pos];

          if (number == "")  // never entered loop, default is 1
            number = "1";
          tempatom.hvyAtom += atoi(number.c_str());
        }
    }
    // C/C++ starts at 0, where PSF/PDB at 1
    tempatom.atomNum = atom->number - 1;
    // Also the molecule - using residue sequence for now
    topo->atoms.push_back(tempatom);
  }

  // calculate the # of degrees of freedom, if there are any bond constraints
  // they will be subtracted later by ModifierShake
  topo->degreesOfFreedom = 3 * topo->atoms.size() - 3;

  if (topo->doSCPISM) delete mySCPISM;

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Get the bonds
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // First create look-up-table
  map<string, vector<PAR::Bond>::const_iterator> bondLookUpTable;
  for (vector<PAR::Bond>::const_iterator bond = par.bonds.begin();
       bond != par.bonds.end();
       ++bond)
    bondLookUpTable[bond->atom1 + "," + bond->atom2] = bond;

  // Find the parameters from PAR
  int ignoredBonds = 0;
  for (vector<PSF::Bond>::const_iterator bond = psf.bonds.begin();
       bond != psf.bonds.end(); ++bond) {
    // store the ID numbers of the bonded atoms
    int atom1 = bond->atom1 - 1;
    int atom2 = bond->atom2 - 1;

    // store the type names of the bonded atoms
    string bond1(topo->atomTypes[topo->atoms[atom1].type].name);
    string bond2(topo->atomTypes[topo->atoms[atom2].type].name);

    map<string,
        vector<PAR::Bond>::const_iterator>::const_iterator currentbond =
      bondLookUpTable.find(bond1 + "," + bond2);

    if (currentbond == bondLookUpTable.end())
      currentbond = bondLookUpTable.find(bond2 + "," + bond1);

    // if we still have not found this bond type in the PAR object, report an
    // error
    if (currentbond == bondLookUpTable.end()) {
      ostringstream err;

      err << "Could not find bond '" << bond1 << "'-'" << bond2 << "' ("
          << bond->atom1 << "," << bond->atom2 << ")" << endl;

      for (map<string, vector<PAR::Bond>::const_iterator>::const_iterator i =
             bondLookUpTable.begin(); i != bondLookUpTable.end(); i++)
        err << i->first << endl;

      THROW(err.str());
    }

    // if we have found this bond type then copy the bond parameters
    // into the topology
    Bond tempbond;
    tempbond.springConstant = currentbond->second->forceConstant;
    tempbond.restLength = currentbond->second->distance;
    tempbond.atom1 = atom1;
    tempbond.atom2 = atom2;
    topo->bonds.push_back(tempbond);

    // populate the vector of bonds maintained at each atom
    topo->atoms[atom1].mybonds.push_back((topo->bonds.size()) - 1);
    topo->atoms[atom2].mybonds.push_back((topo->bonds.size()) - 1);

    if (tempbond.springConstant == 0.0) ignoredBonds++;
  }

  if (ignoredBonds > 0)
    report << hint << "Systems contains " << ignoredBonds
           << " bonds with zero force constants." << endr;

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Get the angles
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // First create look-up-table
  map<string, vector<PAR::Angle>::const_iterator> angleLookUpTable;
  for (vector<PAR::Angle>::const_iterator angle = par.angles.begin();
       angle != par.angles.end(); ++angle)
    angleLookUpTable[angle->atom1 + "," + angle->atom2 + "," +
                     angle->atom3] = angle;

  // Find the parameters from PAR
  int ignoredAngles = 0;

  // loop over the angle list in the PSF object
  for (vector<PSF::Angle>::const_iterator angle = psf.angles.begin();
       angle != psf.angles.end(); ++angle) {
    // store the ID numbers of the atoms in this angle
    int atom1 = angle->atom1 - 1;
    int atom2 = angle->atom2 - 1;
    int atom3 = angle->atom3 - 1;

    // store the type names of the atoms in this angle
    string angle1(topo->atomTypes[topo->atoms[atom1].type].name);
    string angle2(topo->atomTypes[topo->atoms[atom2].type].name);
    string angle3(topo->atomTypes[topo->atoms[atom3].type].name);

    map<string, vector<PAR::Angle>::const_iterator>::const_iterator
    currentangle =
      angleLookUpTable.find(angle1 + "," + angle2 + "," + angle3);

    if (currentangle == angleLookUpTable.end())
      currentangle = angleLookUpTable.find(
        angle3 + "," + angle2 + "," + angle1);

    // if we still have not found this angle type in the PAR object, report an
    // error
    if (currentangle == angleLookUpTable.end())
      THROWS("Could not find angle '" << angle1 << "'-'"
                                      << angle2 << "'-'" << angle3 << "'.");

    // if we have found this angle type then copy the angle parameters
    // into the topology
    Angle tempangle;
    tempangle.atom1 = atom1;
    tempangle.atom2 = atom2;
    tempangle.atom3 = atom3;
    tempangle.forceConstant = currentangle->second->forceConstant;
    tempangle.restAngle = dtor(currentangle->second->angleval);
    if (currentangle->second->ub_flag) {       // do we want defaults for these
      tempangle.ureyBradleyConstant = currentangle->second->k_ub;
      tempangle.ureyBradleyRestLength = currentangle->second->r_ub;
    } else { // no Urey-Bradley term specified
      tempangle.ureyBradleyConstant = 0.0;
      tempangle.ureyBradleyRestLength = 0.0;
    }
    topo->angles.push_back(tempangle);
    if (tempangle.forceConstant == 0.0)
      ignoredAngles++;
  }

  if (ignoredAngles > 0)
    report << hint << "Systems contains " << ignoredAngles <<
    " angles with zero force constants." << endr;

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Get the dihedrals
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  //
  // One change I made was to assume that a dihedral will only appear
  // once in the .psf file regardless of it's multiplicity.  The
  // multiplicity should be handled in the .par file.
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // First create look-up-table
  map<string, vector<PAR::Dihedral>::const_iterator> dihedralLookUpTable;
  for (vector<PAR::Dihedral>::const_iterator dihedral = par.dihedrals.begin();
       dihedral != par.dihedrals.end(); ++dihedral)
    dihedralLookUpTable[dihedral->atom1 + "," + dihedral->atom2 + "," +
                        dihedral->atom3 + "," + dihedral->atom4] = dihedral;

  // Find the parameters from PAR
  // loop over the dihedral list in the PSF object
  for (vector<PSF::Dihedral>::const_iterator dihedral = psf.dihedrals.begin();
       dihedral != psf.dihedrals.end(); ++dihedral) {
    // store the ID numbers of the atoms in this dihedral
    int atom1 = dihedral->atom1 - 1;
    int atom2 = dihedral->atom2 - 1;
    int atom3 = dihedral->atom3 - 1;
    int atom4 = dihedral->atom4 - 1;

    // store the type names of the atoms in this dihedral
    string dihedral1 = topo->atomTypes[topo->atoms[atom1].type].name;
    string dihedral2 = topo->atomTypes[topo->atoms[atom2].type].name;
    string dihedral3 = topo->atomTypes[topo->atoms[atom3].type].name;
    string dihedral4 = topo->atomTypes[topo->atoms[atom4].type].name;

    map<string,
        vector<PAR::Dihedral>::const_iterator>::const_iterator
    currentdihedral =
      dihedralLookUpTable.find(
        dihedral1 + "," + dihedral2 + "," + dihedral3 + "," + dihedral4);

    // if this dihedral type has not been found, try reversing the order of
    // the atom types
    if (currentdihedral == dihedralLookUpTable.end())
      currentdihedral = dihedralLookUpTable.find(
        dihedral4 + "," + dihedral3 + "," + dihedral2 + "," + dihedral1);

    // Try wildcards if necessary
    if (currentdihedral == dihedralLookUpTable.end()) {
      currentdihedral = dihedralLookUpTable.find(
        "X," + dihedral2 + "," + dihedral3 + ",X");
      if (currentdihedral == dihedralLookUpTable.end())
        currentdihedral = dihedralLookUpTable.find(
          "X," + dihedral3 + "," + dihedral2 + ",X");
    }

    // if we still have not found this dihedral type in the PAR object, report
    // an error
    if (currentdihedral == dihedralLookUpTable.end())
      THROWS(
        "Could not find dihedral '" << dihedral1 << "'-'" << dihedral2
                                    << "'-'" << dihedral3 << "'-'" <<
        dihedral4 << "'.");

    // if we have found this dihedral type then copy the
    // dihedral parameters into the topology
    Torsion torsion;
    torsion.atom1 = atom1;
    torsion.atom2 = atom2;
    torsion.atom3 = atom3;
    torsion.atom4 = atom4;

    torsion.periodicity = currentdihedral->second->periodicity;
    torsion.forceConstant = currentdihedral->second->forceConstant;
    torsion.phaseShift = dtor(currentdihedral->second->phaseShift);
    torsion.multiplicity = currentdihedral->second->multiplicity;
    if (topo->dihedrals.empty() ||
        topo->dihedrals[topo->dihedrals.size() - 1].atom1 != atom1 ||
        topo->dihedrals[topo->dihedrals.size() - 1].atom2 != atom2 ||
        topo->dihedrals[topo->dihedrals.size() - 1].atom3 != atom3 ||
        topo->dihedrals[topo->dihedrals.size() - 1].atom4 != atom4) {
      if (dihedralMultPSF) {
        torsion.periodicity.resize(1);
        torsion.forceConstant.resize(1);
        torsion.phaseShift.resize(1);
        torsion.multiplicity = 1;
      }
      topo->dihedrals.push_back(torsion);
    } else if (dihedralMultPSF) {
      Torsion &tmp = topo->dihedrals[topo->dihedrals.size() - 1];
      if (tmp.multiplicity > torsion.multiplicity)
        THROWS(
          "PSF multiplicity definition of dihedral (" << dihedral1 << ","
                                                      << dihedral2 <<
          "," << dihedral3 << "," << dihedral4
                                                      <<
          ") exceeded PAR definition.")

        tmp.periodicity.push_back(torsion.periodicity[tmp.multiplicity]);
      tmp.forceConstant.push_back(torsion.forceConstant[tmp.multiplicity]);
      tmp.phaseShift.push_back(torsion.phaseShift[tmp.multiplicity]);
      tmp.multiplicity++;
    } else
      THROWS("Unexpected PSF multiplicity definition of dihedral ("
             << dihedral1 << "," << dihedral2 << "," << dihedral3 << ","
             << dihedral4 << ") occurred, use dihedral multiplicity PSF.");
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Get the impropers
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  //
  // One change I made was to assume that a improper will only appear
  // once in the .psf file regardless of it's multiplicity.  The
  // multiplicity should be handled in the .par file.
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  //         No wildcard usage is allowed for bonds and angles. For dihedrals,
  // two types are allowed; A - B - C - D (all four atoms specified) and
  // X - A - B - X (only middle two atoms specified). Double dihedral
  // specifications may be specified for the four atom type by listing a
  // given set twice. When specifying this type in the topology file, specify
  // a dihedral twice (with nothing intervening) and both forms will be used.
  //
  //         There are five choices for wildcard usage for improper dihedrals;
  // 1) A - B - C - D  (all four atoms, double specification allowed)
  // 2) A - X - X - B
  // 3) X - A - B - C
  // 4) X - A - B - X
  // 5) X - X - A - B
  // When classifying an improper dihedral, the first acceptable match (from
  // the above order) is chosen. The match may be made in either direction
  // ( A - B - C - D = D - C - B - A).
  //
  //         The periodicity value for dihedrals and improper dihedral terms
  // must be an integer. If it is positive, then a cosine functional form is
  // used. Only positive values of 1,2,3,4,5 and 6 are allowed for the vector,
  // parallel vector and cray routines. Slow and scalar routines can use any
  // positive integer and thus dihedral constrains can be of any periodicity.
  // Reference angle 0.0 and 180.0 degree correspond to minimum in staggered
  // and eclipsed respectively. Any reference angle is allowed. The value
  // 180 should be prefered over -180 since it is parsed faster and more
  // accuratly. When the periodicity is given as zero, for OTHER THAN THE
  // FIRST dihdral in a multiple dihedral set, then a the amplitude is a
  // constant added to the energy. This is needed to effect the
  // Ryckaert-Bellemans potential for hydrocarbons (see below).

  // First create look-up-table
  map<string, vector<PAR::Improper>::const_iterator> improperLookUpTable;
  for (vector<PAR::Improper>::const_iterator improper = par.impropers.begin();
       improper != par.impropers.end(); improper++)
    improperLookUpTable[improper->atom1 + "," + improper->atom2 + "," +
                        improper->atom3 + "," + improper->atom4] = improper;

  // Find the parameters from PAR
  // loop over the improper list in the PSF object
  for (vector<PSF::Improper>::const_iterator improper = psf.impropers.begin();
       improper != psf.impropers.end(); improper++) {
    // store the ID numbers of the atoms in this improper
    int atom1 = improper->atom1 - 1;
    int atom2 = improper->atom2 - 1;
    int atom3 = improper->atom3 - 1;
    int atom4 = improper->atom4 - 1;

    // store the type names of the atoms in this improper
    string improper1 = topo->atomTypes[topo->atoms[atom1].type].name;
    string improper2 = topo->atomTypes[topo->atoms[atom2].type].name;
    string improper3 = topo->atomTypes[topo->atoms[atom3].type].name;
    string improper4 = topo->atomTypes[topo->atoms[atom4].type].name;

    map<string, vector<PAR::Improper>::const_iterator>::const_iterator
    currentimproper =
      improperLookUpTable.find(improper1 + "," + improper2 + "," + improper3 +
        "," + improper4);
    if (currentimproper == improperLookUpTable.end())
      currentimproper = improperLookUpTable.find(
        improper4 + "," + improper3 + "," + improper2 + "," + improper1);

    // Try wildcards if necessary
    // 2) A - X - X - B
    if (currentimproper == improperLookUpTable.end()) {
      currentimproper = improperLookUpTable.find(
        improper1 + ",X,X," + improper4);
      if (currentimproper == improperLookUpTable.end())
        currentimproper = improperLookUpTable.find(
          improper4 + ",X,X," + improper1);
    }
    // 3) X - A - B - C
    if (currentimproper == improperLookUpTable.end()) {
      currentimproper = improperLookUpTable.find(
        "X," + improper2 + "," + improper3 + "," + improper4);
      if (currentimproper == improperLookUpTable.end())
        currentimproper = improperLookUpTable.find(
          improper4 + "," + improper3 + "," + improper2 + ",X");
    }

    // 4) X - A - B - X
    if (currentimproper == improperLookUpTable.end()) {
      currentimproper = improperLookUpTable.find(
        "X," + improper2 + "," + improper3 + ",X");
      if (currentimproper == improperLookUpTable.end())
        currentimproper = improperLookUpTable.find(
          "X," + improper3 + "," + improper2 + ",X");
    }

    // 5) X - X - A - B
    if (currentimproper == improperLookUpTable.end()) {
      currentimproper = improperLookUpTable.find(
        "X,X," + improper3 + "," + improper4);
      if (currentimproper == improperLookUpTable.end())
        currentimproper = improperLookUpTable.find(
          improper4 + "," + improper3 + ",X,X");
    }

    // if we still have not found this improper type in the PAR object, report
    // an error
    if (currentimproper == improperLookUpTable.end())
      THROW("Could not find improper.");

    // if we have found this improper type then copy the
    // improper parameters into the topology
    Torsion torsion;
    torsion.atom1 = atom1;
    torsion.atom2 = atom2;
    torsion.atom3 = atom3;
    torsion.atom4 = atom4;
    torsion.periodicity.push_back(currentimproper->second->periodicity);
    torsion.forceConstant.push_back(currentimproper->second->forceConstant);
    torsion.phaseShift.push_back(dtor(currentimproper->second->phaseShift));
    torsion.multiplicity = 1;
    topo->impropers.push_back(torsion);
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // LennardJonesParameters
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  // get some array sizes
  unsigned int sizeAtomTypes = topo->atomTypes.size();
  unsigned int sizeNonbondeds = par.nonbondeds.size();
  unsigned int sizeNbfixs = par.nbfixs.size();

  topo->lennardJonesParameters.resize(sizeAtomTypes);

  // Nonbonded
  for (unsigned int i = 0; i < sizeAtomTypes; i++) {
    int ti = 0;
    unsigned int bi = sizeNonbondeds;

    for (unsigned int k = 0; k < sizeNonbondeds; ++k) {
      int ok = equalWildcard(par.nonbondeds[k].atom, topo->atomTypes[i].name);
      if (ok > ti) {
        bi = k;
        ti = ok;
      }

      if (ti > 1) break;
    }

    if (ti <= 0)
      THROWS("Could not find matching parameter nonbonded of atom '"
            << topo->atomTypes[i].name << "'.");

    for (unsigned int j = i; j < sizeAtomTypes; j++) {
      int tj = 0;
      unsigned int bj = sizeNonbondeds;
      for (unsigned int k = 0; k < sizeNonbondeds; ++k) {
        int ok = equalWildcard(par.nonbondeds[k].atom,
          topo->atomTypes[j].name);
        if (ok > tj) {
          bj = k;
          tj = ok;
        }

        if (tj > 1) break;
      }

      if (tj <= 0)
        THROWS("Could not find matching parameter nonbonded of atom '"
               << topo->atomTypes[j].name << "'.");

      LennardJonesParameters paramsij;

      // Charmm28
      Real sigma_i = par.nonbondeds[bi].sigma;
      Real sigma_j = par.nonbondeds[bj].sigma;
      Real sigma14_i = par.nonbondeds[bi].sigma14;
      Real sigma14_j = par.nonbondeds[bj].sigma14;

      Real epsilon_i = par.nonbondeds[bi].epsilon;
      Real epsilon_j = par.nonbondeds[bj].epsilon;
      Real epsilon14_i = par.nonbondeds[bi].epsilon14;
      Real epsilon14_j = par.nonbondeds[bj].epsilon14;

      Real r_ij = sigma_i + sigma_j;
      Real e_ij = sqrt(epsilon_i * epsilon_j);
      Real r14_ij = sigma14_i + sigma14_j;
      Real e14_ij = sqrt(epsilon14_i * epsilon14_j);

      paramsij.A = power<12>(r_ij) * e_ij;
      paramsij.B = 2 * power<6>(r_ij) * e_ij;
      paramsij.A14 = power<12>(r14_ij) * e14_ij;
      paramsij.B14 = 2 * power<6>(r14_ij) * e14_ij;

      topo->lennardJonesParameters.set(i, j, paramsij);
    }
  }

  // NbFix
  for (unsigned int k = 0; k < sizeNbfixs; ++k) {
    int ti = 0;
    int tj = 0;
    unsigned int bi = sizeNbfixs;
    unsigned int bj = sizeNbfixs;

    for (unsigned int i = 0; i < sizeAtomTypes; i++) {
      int ok = equalWildcard(par.nbfixs[k].atom1, topo->atomTypes[i].name);
      if (ok > ti) {
        bi = i;
        ti = ok;
      }

      if (ti > 1) break;
    }

    if (ti <= 0) continue;

    for (unsigned int j = 0; j < sizeAtomTypes; j++) {
      int ok = equalWildcard(par.nbfixs[k].atom2, topo->atomTypes[j].name);
      if (ok > tj) {
        bj = j;
        tj = ok;
      }

      if (tj > 1) break;
    }

    if (tj <= 0)
      THROWS("Could not find matching parameter nbfix of atoms '"
             << par.nbfixs[k].atom1 << "' - '" << par.nbfixs[k].atom2 << "'.");

    LennardJonesParameters paramsij;

    paramsij.A = par.nbfixs[k].a;
    paramsij.B = par.nbfixs[k].b;
    paramsij.A14 = par.nbfixs[k].a14;
    paramsij.B14 = par.nbfixs[k].b14;
    topo->lennardJonesParameters.set(bi, bj, paramsij);
  }

  // end loop over NbFix types

  // store the molecule information
  buildMoleculeTable(topo);
  buildExclusionTable(topo, topo->exclude);
}

//____~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//____
//____  buildMoleculeTable
//____
//____~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void TopologyModule::buildMoleculeTable(GenericTopology *topo) {
  // *** First we clear all molecules ***
  topo->molecules.clear();

  const unsigned int numAtoms = topo->atoms.size();

  // *** Collecting all possible connections, building the graph ***
  vector<vector<int> > graph(numAtoms, vector<int>());
  set<pair<int, int> > pairs;
  // *** Bonds ***
  for (unsigned int i = 0; i < topo->bonds.size(); i++) {
    int a1 = topo->bonds[i].atom1;
    int a2 = topo->bonds[i].atom2;
    graph[a1].push_back(a2);
    graph[a2].push_back(a1);
    pairs.insert(pair<int, int>(min(a1, a2), max(a1, a2)));
  }

  unsigned int count = pairs.size();

  // *** Angles ***
  for (unsigned int i = 0; i < topo->angles.size(); i++) {
    int a1 = topo->angles[i].atom1;
    int a2 = topo->angles[i].atom2;
    int a3 = topo->angles[i].atom3;
    graph[a1].push_back(a2);
    graph[a1].push_back(a3);
    graph[a2].push_back(a1);
    graph[a2].push_back(a3);
    graph[a3].push_back(a1);
    graph[a3].push_back(a2);
    pairs.insert(pair<int, int>(min(a1, a2), max(a1, a2)));
    pairs.insert(pair<int, int>(min(a3, a2), max(a3, a2)));
  }

  if (count < pairs.size())
    report << hint << "Angles added " << pairs.size() - count <<
    " new bond(s)." << endr;
  count = pairs.size();

  // *** Dihedrals ***
  for (unsigned int i = 0; i < topo->dihedrals.size(); i++) {
    int a1 = topo->dihedrals[i].atom1;
    int a2 = topo->dihedrals[i].atom2;
    int a3 = topo->dihedrals[i].atom3;
    int a4 = topo->dihedrals[i].atom4;
    graph[a1].push_back(a2);
    graph[a1].push_back(a3);
    graph[a1].push_back(a4);
    graph[a2].push_back(a1);
    graph[a2].push_back(a3);
    graph[a2].push_back(a4);
    graph[a3].push_back(a1);
    graph[a3].push_back(a2);
    graph[a3].push_back(a4);
    graph[a4].push_back(a1);
    graph[a4].push_back(a2);
    graph[a4].push_back(a3);
    pairs.insert(pair<int, int>(min(a1, a2), max(a1, a2)));
    pairs.insert(pair<int, int>(min(a3, a2), max(a3, a2)));
    pairs.insert(pair<int, int>(min(a3, a4), max(a3, a4)));
  }

  if (count < pairs.size())
    report << hint << "Dihedrals added " << pairs.size() - count <<
    " new bond(s)." << endr;
  count = pairs.size();

  // *** Impropers ***
  set<pair<int, int> > pairsAddImpropers;
  // Impropers are defined over the bonds 1-2,1-3,1-4 or 4-1,4-3,4-2
  // but MTorsionSystemForce computes distances betweeen 1-2,2-3,3-4
  // we have to take care about these differences ...
  for (unsigned int i = 0; i < topo->impropers.size(); i++) {
    int a1 = topo->impropers[i].atom1;
    int a2 = topo->impropers[i].atom2;
    int a3 = topo->impropers[i].atom3;
    int a4 = topo->impropers[i].atom4;
    graph[a1].push_back(a2);
    graph[a1].push_back(a3);
    graph[a1].push_back(a4);
    graph[a2].push_back(a1);
    graph[a2].push_back(a3);
    graph[a2].push_back(a4);
    graph[a3].push_back(a1);
    graph[a3].push_back(a2);
    graph[a3].push_back(a4);
    graph[a4].push_back(a1);
    graph[a4].push_back(a2);
    graph[a4].push_back(a3);
    pair<int, int> p0(min(a1, a2), max(a1, a2));
    pair<int, int> p1(min(a1, a3), max(a1, a3));
    pair<int, int> p2(min(a1, a4), max(a1, a4));
    pair<int, int> p3(min(a2, a3), max(a2, a3));
    pair<int, int> p4(min(a2, a4), max(a2, a4));
    pair<int, int> p5(min(a3, a4), max(a3, a4));
    int j0 = 0;
    int j1 = 0;
    int j2 = 0;
    int j3 = 0;
    int j4 = 0;
    int j5 = 0;
    if (pairs.find(p0) != pairs.end()) j0++;
    if (pairs.find(p1) != pairs.end()) j1++;
    if (pairs.find(p2) != pairs.end()) j2++;
    if (pairs.find(p3) != pairs.end()) j3++;
    if (pairs.find(p4) != pairs.end()) j4++;
    if (pairs.find(p5) != pairs.end()) j5++;
    if (j0 + j1 + j2 + j3 + j4 + j5 < 3) {
      pairs.insert(p0);
      pairs.insert(p1);
      pairs.insert(p2);
    }
    pairsAddImpropers.insert(p0);
    pairsAddImpropers.insert(p3);
    pairsAddImpropers.insert(p5);
  }

  if (count < pairs.size())
    report << hint << "Impropers added " << pairs.size() - count
           << " new bond(s)." << endr;

  // Now add the improper pairs
  for (set<pair<int, int> >::const_iterator i = pairsAddImpropers.begin();
       i != pairsAddImpropers.end(); i++)
    pairs.insert(*i);

  count = pairs.size();
  // To keep track which atoms already have been added
  // to molecules.
  vector<char> unused(numAtoms, 1);

  // Recursively finding the atoms beloning to a molecule
  for (unsigned int i = 0; i < numAtoms; i++) {
    vector<int> v;
    vector<PairInt> p;
    findNextNeighbor(i, v, p, unused, graph, pairs);

    if (!v.empty()) {
      sort(v.begin(), v.end());
      // add this atom list to the molecules array
      Molecule mol;
      mol.atoms = v;
      for (unsigned int j = 0; j < p.size(); j++)
        if (p[j].first > p[j].second)
          swap(p[j].first, p[j].second);

      sort(p.begin(), p.end());
      mol.pairs = p;
      topo->molecules.push_back(mol);
    }
  }

  // Uncomment to sort descending after size()
  // sort(topo->molecules.begin(),topo->molecules.end(),cmpSize);

  // Look up table for atoms
  const string h("H");
  const string o("O");
  for (unsigned int i = 0; i < topo->molecules.size(); i++) {
    Real mass = 0.0;
    const vector<int> &mol = topo->molecules[i].atoms;
    for (unsigned int j = 0; j < mol.size(); j++) {
      int k = mol[j];
      topo->atoms[k].molecule = i;
      mass += topo->atoms[k].scaledMass;
    }

    topo->molecules[i].mass = mass;
    topo->molecules[i].water =
      (mol.size() == 3 &&
       ((topo->atomTypes[topo->atoms[mol[0]].type].symbolName == h &&
         topo->atomTypes[topo->atoms[mol[1]].type].symbolName == h &&
         topo->atomTypes[topo->atoms[mol[2]].type].symbolName == o) ||
        (topo->atomTypes[topo->atoms[mol[0]].type].symbolName == h &&
         topo->atomTypes[topo->atoms[mol[1]].type].symbolName == o &&
         topo->atomTypes[topo->atoms[mol[2]].type].symbolName == h) ||
        (topo->atomTypes[topo->atoms[mol[0]].type].symbolName == o &&
         topo->atomTypes[topo->atoms[mol[1]].type].symbolName == h &&
         topo->atomTypes[topo->atoms[mol[2]].type].symbolName == h)));
  }

#if defined (DEBUG_PRINT_MOLECULETABLE)
  report << plain << endl
         << "[buildMoleculeTable]: molecule table printout:" << endl;

  for (int i = 0; i < topo->molecules.size(); i++) {
    for (int j = 0; j < topo->molecules[i].size(); j++)
      report << topo->molecules[i][j] << " ";

    report << endl;
  }

  report << endr;
#endif
}

//____~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//____
//____  buildExclusionTable
//____
//____~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void TopologyModule::buildExclusionTable(GenericTopology *topo,
                                         const ExclusionType &exclusionType) {
  if (!exclusionType.valid())
    THROW("[buildExclusionTable()] Exclusion type not defined/valid.");

  topo->exclude = exclusionType;

  //  Resize array.
  topo->exclusions.resize(topo->atoms.size());

  //  If exclusionType is equal to NONE, return.
  if (exclusionType == ExclusionType::NONE) return;

  const int numBonds = topo->bonds.size(),
            numAngles = topo->angles.size(),
            numDihedrals = topo->dihedrals.size();

  //  Add excluded bonds.
  for (int i = 0; i < numBonds; i++)
    topo->exclusions.add(topo->bonds[i].atom1, topo->bonds[i].atom2,
      EXCLUSION_FULL);

  if (exclusionType != ExclusionType::ONE2) {
    //  Add excluded angles.
    for (int i = 0; i < numAngles; i++)
      topo->exclusions.add(topo->angles[i].atom1,
        topo->angles[i].atom3, EXCLUSION_FULL);

    if (exclusionType != ExclusionType::ONE3)

      //  Add excluded dihedrals.
      for (int i = 0; i < numDihedrals; i++)

        if (exclusionType == ExclusionType::ONE4)
          topo->exclusions.add(topo->dihedrals[i].atom1,
            topo->dihedrals[i].atom4, EXCLUSION_FULL);
        else
          topo->exclusions.add(topo->dihedrals[i].atom1,
            topo->dihedrals[i].atom4, EXCLUSION_MODIFIED);

  }

  topo->exclusions.optimize();
}
