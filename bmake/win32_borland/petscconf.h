#ifdef PETSC_RCS_HEADER
"$Id: petscconf.h,v 1.4 2001/03/26 19:11:22 balay Exp $"
"Defines the configuration for this machine"
#endif

#if !defined(INCLUDED_PETSCCONF_H)
#define INCLUDED_PETSCCONF_H

#define PARCH_win32 
#define PETSC_ARCH_NAME "win32"
#define PETSC_HAVE_LIMITS_H
#define PETSC_HAVE_STDLIB_H 
#define PETSC_HAVE_STRING_H 
#define PETSC_HAVE_SEARCH_H
#define PETSC_HAVE_IO_H

#define PETSC_HAVE_TEMPLATED_COMPLEX
#define PETSC_HAVE_FORTRAN_CAPS 

#define PETSC_HAVE_READLINK
#define PETSC_HAVE_MEMMOVE

#define PETSC_HAVE_RAND
#define PETSC_CANNOT_START_DEBUGGER
#define PETSC_HAVE_CLOCK

#define PETSC_HAVE_GET_USER_NAME
#define SIZEOF_VOID_P 4
#define SIZEOF_INT 4
#define SIZEOF_DOUBLE 8

#define PETSC_USE_NT_TIME
#define PETSC_HAVE_NO_GETRUSAGE

#define PETSC_MISSING_SIGBUS
#define PETSC_MISSING_SIGQUIT
#define PETSC_MISSING_SIGSYS

#define PETSC_HAVE_DOS_H
#define PETSC_HAVE_ACCESS

#endif

