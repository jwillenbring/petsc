-include ../../../../../petscdir.mk

SOURCEC  = pbvec.c pdvec.c pvec2.c vmpicr.c commonmpvec.c
SOURCEF  =
SOURCEH  = pvecimpl.h
LIBBASE  = libpetscvec
MANSEC   = Vec
DIRS     = mpiviennacl mpiviennaclcuda kokkos cupm

include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test
