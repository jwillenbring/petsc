-include ../../../../../petscdir.mk
#requirespackage  'PETSC_HAVE_CGNS'

CFLAGS   = ${CGNS_INCLUDE}
SOURCEC  = plexcgns2.c
SOURCEF  =
SOURCEH  =
LIBBASE  = libpetscdm
MANSEC   = DM
SUBMANSEC= DMPlex

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test
