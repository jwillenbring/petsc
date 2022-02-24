
static char help[] = "Reads a PETSc vector from a socket connection, then sends it back within a loop. Works with ex42.m or ex42a.c\n";

#include <petscvec.h>

int main(int argc,char **args)
{
  Vec            b;
  PetscViewer    fd;               /* viewer */
  PetscErrorCode ierr;
  PetscInt       i;

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  fd = PETSC_VIEWER_SOCKET_WORLD;

  for (i=0; i<1000; i++) {
    CHKERRQ(VecCreate(PETSC_COMM_WORLD,&b));
    CHKERRQ(VecLoad(b,fd));
    CHKERRQ(VecView(b,fd));
    CHKERRQ(VecDestroy(&b));
  }
  ierr = PetscFinalize();
  return ierr;
}
