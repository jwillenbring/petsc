#ifdef PETSC_RCS_HEADER
"$Id: petscconf.h,v 1.9 2000/11/28 17:26:31 bsmith Exp $"
"Defines the configuration for this machine"
#endif

#if !defined(INCLUDED_PETSCCONF_H)
#define INCLUDED_PETSCCONF_H

#define PARCH_linux
#define PETSC_ARCH_NAME "linux"

#define PETSC_HAVE_POPEN
#define PETSC_HAVE_LIMITS_H
#define PETSC_HAVE_PWD_H 
#define PETSC_HAVE_MALLOC_H 
#define PETSC_HAVE_STRING_H 
#define PETSC_HAVE_GETDOMAINNAME
#define PETSC_HAVE_DRAND48 
#define PETSC_HAVE_UNAME 
#define PETSC_HAVE_UNISTD_H 
#define PETSC_HAVE_SYS_TIME_H 
#define PETSC_HAVE_STDLIB_H
#define PETSC_HAVE_UNISTD_H

#define PETSC_HAVE_FORTRAN_CAPS
#define PETSC_HAVE_TEMPLATED_COMPLEX
#define PETSC_HAVE_READLINK
#define PETSC_HAVE_MEMMOVE
#define PETSC_HAVE_SYS_UTSNAME_H

#define PETSC_HAVE_DOUBLE_ALIGN_MALLOC
#define HAVE_MEMALIGN
#define PETSC_HAVE_SYS_RESOURCE_H
#define SIZEOF_VOID_P 4
#define SIZEOF_INT 4
#define SIZEOF_DOUBLE 8

#if defined(fixedsobug)
#define PETSC_USE_DYNAMIC_LIBRARIES 1
#define PETSC_HAVE_RTLD_GLOBAL 1
#endif

#define PETSC_HAVE_F90_H "f90impl/f90_absoft.h"
#define PETSC_HAVE_F90_C "src/sys/src/f90/f90_absoft.c"
#define PETSC_MISSING_SIGSYS

#endif
