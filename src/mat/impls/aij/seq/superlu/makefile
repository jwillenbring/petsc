-include ../../../../../../petscdir.mk
#requirespackage   'PETSC_HAVE_SUPERLU'

CFLAGS   = ${SUPERLU_INCLUDE}
SOURCEC  = superlu.c
SOURCEF  =
SOURCEH  =
LIBBASE  = libpetscmat
DIRS     =
MANSEC   = Mat

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test
