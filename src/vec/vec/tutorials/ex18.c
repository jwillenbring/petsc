
static char help[] = "Computes the integral of 2*x/(1+x^2) from x=0..1 \nThis is equal to the ln(2).\n\n";

/*T
   Concepts: vectors^assembling vectors;
   Processors: n

   Contributed by Mike McCourt <mccomic@iit.edu> and Nathan Johnston <johnnat@iit.edu>
T*/

/*
  Include "petscvec.h" so that we can use vectors.  Note that this file
  automatically includes:
     petscsys.h       - base PETSc routines   petscis.h     - index sets
     petscviewer.h - viewers
*/
#include <petscvec.h>

PetscScalar func(PetscScalar a)
{
  return (PetscScalar)2.*a/((PetscScalar)1.+a*a);
}

int main(int argc,char **argv)
{
  PetscErrorCode ierr;
  PetscMPIInt    rank,size;
  PetscInt       rstart,rend,i,k,N,numPoints=1000000;
  PetscScalar    dummy,result=0,h=1.0/numPoints,*xarray;
  Vec            x,xend;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);if (ierr) return ierr;
  CHKERRMPI(MPI_Comm_rank(PETSC_COMM_WORLD,&rank));
  CHKERRMPI(MPI_Comm_size(PETSC_COMM_WORLD,&size));

  /*
     Create a parallel vector.
       Here we set up our x vector which will be given values below.
       The xend vector is a dummy vector to find the value of the
         elements at the endpoints for use in the trapezoid rule.
  */
  CHKERRQ(VecCreate(PETSC_COMM_WORLD,&x));
  CHKERRQ(VecSetSizes(x,PETSC_DECIDE,numPoints));
  CHKERRQ(VecSetFromOptions(x));
  CHKERRQ(VecGetSize(x,&N));
  CHKERRQ(VecSet(x,result));
  CHKERRQ(VecDuplicate(x,&xend));
  result = 0.5;
  if (rank == 0) {
    i    = 0;
    CHKERRQ(VecSetValues(xend,1,&i,&result,INSERT_VALUES));
  }
  if (rank == size-1) {
    i    = N-1;
    CHKERRQ(VecSetValues(xend,1,&i,&result,INSERT_VALUES));
  }
  /*
     Assemble vector, using the 2-step process:
       VecAssemblyBegin(), VecAssemblyEnd()
     Computations can be done while messages are in transition
     by placing code between these two statements.
  */
  CHKERRQ(VecAssemblyBegin(xend));
  CHKERRQ(VecAssemblyEnd(xend));

  /*
     Set the x vector elements.
      i*h will return 0 for i=0 and 1 for i=N-1.
      The function evaluated (2x/(1+x^2)) is defined above.
      Each evaluation is put into the local array of the vector without message passing.
  */
  CHKERRQ(VecGetOwnershipRange(x,&rstart,&rend));
  CHKERRQ(VecGetArray(x,&xarray));
  k    = 0;
  for (i=rstart; i<rend; i++) {
    xarray[k] = (PetscScalar)i*h;
    xarray[k] = func(xarray[k]);
    k++;
  }
  CHKERRQ(VecRestoreArray(x,&xarray));

  /*
     Evaluates the integral.  First the sum of all the points is taken.
     That result is multiplied by the step size for the trapezoid rule.
     Then half the value at each endpoint is subtracted,
     this is part of the composite trapezoid rule.
  */
  CHKERRQ(VecSum(x,&result));
  result = result*h;
  CHKERRQ(VecDot(x,xend,&dummy));
  result = result-h*dummy;

  /*
      Return the value of the integral.
  */
  CHKERRQ(PetscPrintf(PETSC_COMM_WORLD,"ln(2) is %g\n",(double)PetscRealPart(result)));
  CHKERRQ(VecDestroy(&x));
  CHKERRQ(VecDestroy(&xend));

  ierr = PetscFinalize();
  return ierr;
}

/*TEST

     test:
       nsize: 1

     test:
       nsize: 2
       suffix: 2
       output_file: output/ex18_1.out

TEST*/
