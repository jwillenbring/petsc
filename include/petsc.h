/* $Id: snes.h,v 1.17 1995/06/02 21:05:19 bsmith Exp $ */

#if !defined(__PETSC_PACKAGE)
#define __PETSC_PACKAGE

#define PETSC_VERSION_NUMBER "PETSc Version 2.0.Beta.3 Released May 29, 1995."

#include <stdio.h>
#if defined(PARCH_sun4)
int fprintf(FILE*,char*,...);
int printf(char*,...);
int fflush(FILE*);
int fclose(FILE*);
#endif

/* MPI interface */
#include "mpi.h"
#include "mpiu.h"

#if defined(PETSC_COMPLEX)
/* work around for bug in alpha g++ compiler */
#if defined(PARCH_alpha) 
#define hypot(a,b) (double) sqrt((a)*(a)+(b)*(b)) 
/* extern double hypot(double,double); */
#endif
#include <complex.h>
#define PETSCREAL(a) real(a)
#define Scalar       complex
#else
#define PETSCREAL(a) a
#define Scalar       double
#endif

extern void *(*PetscMalloc)(unsigned int,int,char*);
extern int  (*PetscFree)(void *,int,char*);
#define MALLOC(a)       (*PetscMalloc)(a,__LINE__,__FILE__)
#define FREE(a)         (*PetscFree)(a,__LINE__,__FILE__)
extern int  PetscSetMalloc(void *(*)(unsigned int,int,char*),
                           int (*)(void *,int,char*));
extern int  Trdump(FILE *);

#define NEW(a)          (a *) MALLOC(sizeof(a))
#define MEMCPY(a,b,n)   memcpy((char*)(a),(char*)(b),n)
#define MEMSET(a,b,n)   memset((char*)(a),(int)(b),n)
#include <memory.h>

/*  Macros for error checking */
#if !defined(__DIR__)
#define __DIR__ 0
#endif
#if defined(PETSC_DEBUG)
#define SETERR(n,s)     {return PetscError(__LINE__,__DIR__,__FILE__,s,n);}
#define SETERRA(n,s)    \
                {int _ierr = PetscError(__LINE__,__DIR__,__FILE__,s,n);\
                 MPI_Abort(MPI_COMM_WORLD,_ierr);}
#define CHKERR(n)       {if (n) SETERR(n,(char *)0);}
#define CHKERRA(n)      {if (n) SETERRA(n,(char *)0);}
#define CHKPTR(p)       if (!p) SETERR(1,"No memory");
#define CHKPTRA(p)      if (!p) SETERRA(1,"No memory");
#else
#define SETERR(n,s)     {return PetscError(__LINE__,__DIR__,__FILE__,s,n);}
#define SETERRA(n,s)    \
                {int _ierr = PetscError(__LINE__,__DIR__,__FILE__,s,n);\
                 MPI_Abort(MPI_COMM_WORLD,_ierr);}
#define CHKERR(n)       {if (n) SETERR(n,(char *)0);}
#define CHKERRA(n)      {if (n) SETERRA(n,(char *)0);}
#define CHKPTR(p)       if (!p) SETERR(1,"No memory");
#define CHKPTRA(p)      if (!p) SETERRA(1,"No memory");
#endif

typedef struct _PetscObject* PetscObject;
#define PETSC_COOKIE         0x12121212
#define PETSC_DECIDE         -1

typedef enum { PETSC_FALSE, PETSC_TRUE } PetscTruth;

#include "viewer.h"
#include "options.h"

/* useful Petsc routines (used often) */
extern int  PetscInitialize(int*,char***,char*,char*);
extern int  PetscFinalize();

extern int  PetscDestroy(PetscObject);
extern int  PetscObjectGetComm(PetscObject,MPI_Comm *comm);
extern int  PetscObjectSetName(PetscObject,char*);
extern int  PetscObjectGetName(PetscObject,char**);

extern int  PetscDefaultErrorHandler(int,char*,char*,char*,int,void*);
extern int  PetscAbortErrorHandler(int,char*,char*,char*,int,void* );
extern int  PetscAttachDebuggerErrorHandler(int,char*,char*,char*,int,void*); 
extern int  PetscError(int,char*,char*,char*,int);
extern int  PetscPushErrorHandler(int 
                         (*handler)(int,char*,char*,char*,int,void*),void* );
extern int  PetscPopErrorHandler();

extern int  PetscSetDebugger(char *,int,char *);
extern int  PetscAttachDebugger();

extern int PetscDefaultSignalHandler(int,void*);
extern int PetscPushSignalHandler(int (*)(int,void *),void*);
extern int PetscPopSignalHandler();
extern int PetscSetFPTrap(int);
#define FP_TRAP_OFF    0
#define FP_TRAP_ON     1
#define FP_TRAP_ALWAYS 2


#if defined(PARCH_cray) || defined(PARCH_NCUBE)
#define FORTRANCAPS
#elif !defined(PARCH_rs6000) && !defined(PARCH_NeXT) && !defined(PARCH_hpux)
#define FORTRANUNDERSCORE
#endif

#include <stdio.h> /* I don't like this, but? */

/* Global flop counter */
extern double _TotalFlops;
#if defined(PETSC_LOG)
#define PLogFlops(n) {_TotalFlops += n;}
#else
#define PLogFlops(n)
#endif 

#endif
