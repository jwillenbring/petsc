#ifdef PETSC_RCS_HEADER
"$Id: petscconf.h,v 1.21 2001/03/02 23:02:20 balay Exp $"
"Defines the configuration for this machine"
#endif

#if !defined(INCLUDED_PETSCCONF_H)
#define INCLUDED_PETSCCONF_H

#define PARCH_alpha
#define PETSC_ARCH_NAME "alpha"
#define PETSC_USE_GETCLOCK

#define PETSC_USE_KBYTES_FOR_SIZE
#define PETSC_HAVE_POPEN
#define PETSC_HAVE_LIMITS_H
#define PETSC_HAVE_PWD_H 
#define PETSC_HAVE_STRING_H 
#define PETSC_HAVE_MALLOC_H 
#define PETSC_HAVE_STDLIB_H 
#define PETSC_HAVE_DRAND48  
#define PETSC_HAVE_GETDOMAINNAME  
#define PETSC_HAVE_UNISTD_H 
#define PETSC_HAVE_SYS_TIME_H 
#define PETSC_HAVE_UNAME  

#define SIZEOF_VOID_P 8
#define SIZEOF_INT 4
#define SIZEOF_DOUBLE 8

#define PETSC_HAVE_FORTRAN_UNDERSCORE

#define PETSC_HAVE_READLINK
#define PETSC_HAVE_MEMMOVE
#define PETSC_HAVE_SYS_UTSNAME_H
#define PETSC_HAVE_STRINGS_H
#define PETSC_USE_DBX_DEBUGGER
#define PETSC_HAVE_SYS_RESOURCE_H

#define PETSC_USE_DYNAMIC_LIBRARIES 1
#define PETSC_USE_NONEXECUTABLE_SO 1

#define PETSC_NEED_SOCKET_PROTO
#define PETSC_HAVE_MACHINE_ENDIAN_H

#define PETSC_NEED_KILL_FOR_DEBUGGER
#define PETSC_USE_PID_FOR_DEBUGGER
#endif
