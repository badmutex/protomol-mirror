# USING THE NEW STRUCTURE
from Physical import *
from Forces import *
from Propagator import *
from IO import *
from ForceField import *

# PHYSICAL
phys = Physical()
io = IO()
io.readPDBPos(phys, "data/alanine.pdb")
io.readPSF(phys, "data/alanine.psf")
io.readPAR(phys, "data/alanine.par")
phys.bc = "Vacuum"
phys.cellsize = 5
phys.exclude = "scaled1-4"
phys.temperature = 300
phys.seed = 1234
phys.remcom = 1


# FORCES
forces = Forces()
ff = forces.makeForceField(phys, "charmm")

# EXECUTE
prop = Propagator(phys, forces, io)
gamma = prop.propagate("Leapfrog", steps=20, dt=0.5, forcefield=ff)
io.writeXYZPos(phys,'data/COMTest.xyz')
