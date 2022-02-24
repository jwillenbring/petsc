
static char help[] = "Tests PetscSynchronizedPrintf() and PetscSynchronizedFPrintf().\n\n";

#include <petscsys.h>

int main(int argc,char **argv)
{
  PetscMPIInt    rank;
  PetscErrorCode ierr;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);if (ierr) return ierr;
  CHKERRMPI(MPI_Comm_rank(PETSC_COMM_WORLD,&rank));

  CHKERRQ(PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Greetings from %d\n",rank));
  CHKERRQ(PetscSynchronizedFlush(PETSC_COMM_WORLD,PETSC_STDOUT));

  CHKERRQ(PetscSynchronizedFPrintf(PETSC_COMM_WORLD,PETSC_STDOUT,"Greetings again from %d\n",rank));
  CHKERRQ(PetscSynchronizedFlush(PETSC_COMM_WORLD,PETSC_STDOUT));

  ierr = PetscFinalize();
  return ierr;
}

/*TEST

   test:
      nsize: 3

TEST*/
