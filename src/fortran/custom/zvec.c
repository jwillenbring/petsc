#ifndef lint
static char vcid[] = "$Id: zvec.c,v 1.9 1996/01/30 12:21:52 curfman Exp bsmith $";
#endif

#include "zpetsc.h"
#include "vec.h"
#ifdef HAVE_FORTRAN_CAPS
#define veccreateseq_         VECCREATESEQ
#define veccreate_            VECCREATE
#define vecduplicate_         VECDUPLICATE
#define veccreatempi_         VECCREATEMPI
#define vecscattercreate_     VECSCATTERCREATE
#define vecscattercopy_       VECSCATTERCOPY
#define vecdestroy_           VECDESTROY
#define vecscatterdestroy_    VECSCATTERDESTROY
#define vecrestorearray_      VECRESTOREARRAY
#define vecgetarray_          VECGETARRAY
#define vecload_              VECLOAD
#define vecgettype_           VECGETTYPE
#elif !defined(HAVE_FORTRAN_UNDERSCORE)
#define veccreateseq_         veccreateseq
#define veccreate_            veccreate
#define vecduplicate_         vecduplicate
#define veccreatempi_         veccreatempi
#define vecscattercreate_     vecscattercreate
#define vecscattercopy_       vecscattercopy
#define vecdestroy_           vecdestroy
#define vecscatterdestroy_    vecscatterdestroy
#define vecrestorearray_      vecrestorearray
#define vecgetarray_          vecgetarray
#define vecload_              vecload
#define vecgettype_           vecgettype
#endif

#if defined(__cplusplus)
extern "C" {
#endif

void vecgettype_(Vec vv,VecType *type,char *name,int *__ierr,int len)
{
  char *tname;
  if (type == PETSC_NULL_Fortran) type = PETSC_NULL;
  *__ierr = VecGetType((Vec)MPIR_ToPointer(*(int*)vv),type,&tname);
  if (name != PETSC_NULL_Fortran) PetscStrncpy(name,tname,len);
}

void vecload_(Viewer bview,Vec *newvec, int *__ierr )
{ 
  Vec vv;
  *__ierr = VecLoad((Viewer)MPIR_ToPointer( *(int*)(bview) ),&vv);
  *(int *) newvec = MPIR_FromPointer(vv);
}

/* Be to keep vec/examples/ex21.F and snes/examples/ex12.F up to date */
void vecrestorearray_(Vec x,Scalar *fa,int *ia,int *__ierr)
{
  Vec    xin = (Vec)MPIR_ToPointer( *(int*)(x) );
  Scalar *lx = PetscScalarAddressFromFortran(fa,*ia);

  *__ierr = VecRestoreArray(xin,&lx);
}

void vecgetarray_(Vec x,Scalar *fa,int *ia,int *__ierr)
{
  Vec    xin = (Vec)MPIR_ToPointer( *(int*)(x) );
  Scalar *lx;

  *__ierr = VecGetArray(xin,&lx); if (*__ierr) return;
  *ia      = PetscScalarAddressToFortran(fa,lx);
}

void vecscatterdestroy_(VecScatter ctx, int *__ierr )
{
  *__ierr = VecScatterDestroy((VecScatter)MPIR_ToPointer( *(int*)(ctx) ));
   MPIR_RmPointer(*(int*)(ctx)); 
}

void vecdestroy_(Vec v, int *__ierr )
{
  *__ierr = VecDestroy((Vec)MPIR_ToPointer( *(int*)(v) ));
   MPIR_RmPointer(*(int*)(v)); 
}

void vecscattercreate_(Vec xin,IS ix,Vec yin,IS iy,VecScatter *newctx, int *__ierr )
{
  VecScatter lV;
  *__ierr = VecScatterCreate(
	(Vec)MPIR_ToPointer( *(int*)(xin) ),
	(IS)MPIR_ToPointer( *(int*)(ix) ),
	(Vec)MPIR_ToPointer( *(int*)(yin) ),
	(IS)MPIR_ToPointer( *(int*)(iy) ),&lV);
  *(int*) newctx = MPIR_FromPointer(lV);
}
void vecscattercopy_(VecScatter sctx,VecScatter *ctx, int *__ierr )
{
  VecScatter lV;
  *__ierr = VecScatterCopy((VecScatter)MPIR_ToPointer( *(int*)(sctx) ),&lV);
   *(int*) ctx = MPIR_FromPointer(lV); 
}


void veccreatempi_(MPI_Comm comm,int *n,int *N,Vec *vv, int *__ierr )
{
  Vec lV;
  *__ierr = VecCreateMPI((MPI_Comm)MPIR_ToPointer_Comm( *(int*)(comm) ),*n,*N,&lV);
  *(int*)vv = MPIR_FromPointer(lV);
}

void veccreateseq_(MPI_Comm comm,int *n,Vec *V, int *__ierr )
{
  Vec lV;
  *__ierr = VecCreateSeq((MPI_Comm)MPIR_ToPointer_Comm( *(int*)(comm)),*n,&lV);
  *(int*)V = MPIR_FromPointer(lV);
}

void veccreate_(MPI_Comm comm,int *n,Vec *V, int *__ierr ){
  Vec lV;
  *__ierr = VecCreate((MPI_Comm)MPIR_ToPointer_Comm( *(int*)(comm) ),*n,&lV);
  *(int*)V = MPIR_FromPointer(lV);
}

void vecduplicate_(Vec v,Vec *newv, int *__ierr )
{
  Vec lV;
  *__ierr = VecDuplicate((Vec)MPIR_ToPointer( *(int*)(v) ),&lV);
  *(int*)newv = MPIR_FromPointer(lV);
}

#if defined(__cplusplus)
}
#endif
