-include ../../../../../petscdir.mk
#requirespackage 'PETSC_HAVE_HPDDM'

CXXFLAGS  = ${HPDDM_INCLUDE}
SOURCECXX = hpddm.cxx
SOURCEF   =
SOURCEH   =
LIBBASE   = libpetscksp
DIRS      = cuda
MANSEC    = KSP

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test
