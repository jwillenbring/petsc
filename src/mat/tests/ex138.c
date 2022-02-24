
static char help[] = "Tests MatGetColumnNorms()/Sums()/Means() for matrix read from file.";

#include <petscmat.h>

int main(int argc,char **args)
{
  Mat            A;
  PetscErrorCode ierr;
  PetscReal      *reductions_real;
  PetscScalar    *reductions_scalar;
  char           file[PETSC_MAX_PATH_LEN];
  PetscBool      flg;
  PetscViewer    fd;
  PetscInt       n;
  PetscMPIInt    rank;

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  CHKERRMPI(MPI_Comm_rank(PETSC_COMM_WORLD,&rank));
  CHKERRQ(PetscOptionsGetString(NULL,NULL,"-f",file,sizeof(file),&flg));
  PetscCheckFalse(!flg,PETSC_COMM_WORLD,PETSC_ERR_USER,"Must indicate binary file with the -f option");
  CHKERRQ(PetscViewerBinaryOpen(PETSC_COMM_WORLD,file,FILE_MODE_READ,&fd));
  CHKERRQ(MatCreate(PETSC_COMM_WORLD,&A));
  CHKERRQ(MatSetFromOptions(A));
  CHKERRQ(MatLoad(A,fd));
  CHKERRQ(PetscViewerDestroy(&fd));

  CHKERRQ(MatGetSize(A,NULL,&n));
  CHKERRQ(PetscMalloc1(n,&reductions_real));
  CHKERRQ(PetscMalloc1(n,&reductions_scalar));

  CHKERRQ(MatGetColumnNorms(A,NORM_2,reductions_real));
  if (rank == 0) {
    CHKERRQ(PetscPrintf(PETSC_COMM_SELF,"NORM_2:\n"));
    CHKERRQ(PetscRealView(n,reductions_real,PETSC_VIEWER_STDOUT_SELF));
  }

  CHKERRQ(MatGetColumnNorms(A,NORM_1,reductions_real));
  if (rank == 0) {
    CHKERRQ(PetscPrintf(PETSC_COMM_SELF,"NORM_1:\n"));
    CHKERRQ(PetscRealView(n,reductions_real,PETSC_VIEWER_STDOUT_SELF));
  }

  CHKERRQ(MatGetColumnNorms(A,NORM_INFINITY,reductions_real));
  if (rank == 0) {
    CHKERRQ(PetscPrintf(PETSC_COMM_SELF,"NORM_INFINITY:\n"));
    CHKERRQ(PetscRealView(n,reductions_real,PETSC_VIEWER_STDOUT_SELF));
  }

  CHKERRQ(MatGetColumnSums(A,reductions_scalar));
  if (!rank) {
    CHKERRQ(PetscPrintf(PETSC_COMM_SELF,"REDUCTION_SUM:\n"));
    CHKERRQ(PetscScalarView(n,reductions_scalar,PETSC_VIEWER_STDOUT_SELF));
  }

  CHKERRQ(MatGetColumnMeans(A,reductions_scalar));
  if (!rank) {
    CHKERRQ(PetscPrintf(PETSC_COMM_SELF,"REDUCTION_MEAN:\n"));
    CHKERRQ(PetscScalarView(n,reductions_scalar,PETSC_VIEWER_STDOUT_SELF));
  }

  CHKERRQ(PetscFree(reductions_real));
  CHKERRQ(PetscFree(reductions_scalar));
  CHKERRQ(MatDestroy(&A));
  ierr = PetscFinalize();
  return ierr;
}

/*TEST

   test:
      suffix: 1
      nsize: 2
      requires: datafilespath !complex double !defined(PETSC_USE_64BIT_INDICES)
      args: -f ${DATAFILESPATH}/matrices/small -mat_type aij
      output_file: output/ex138.out

   test:
      suffix: 2
      nsize: {{1 2}}
      requires: datafilespath !complex double !defined(PETSC_USE_64BIT_INDICES)
      args: -f ${DATAFILESPATH}/matrices/small -mat_type baij -matload_block_size {{2 3}}
      output_file: output/ex138.out

   test:
      suffix: complex
      nsize: 2
      requires: datafilespath complex double !defined(PETSC_USE_64BIT_INDICES)
      args: -f ${DATAFILESPATH}/matrices/nimrod/small_112905 -mat_type aij
      output_file: output/ex138_complex.out
      filter: grep -E "\ 0:|1340:|1344:"

TEST*/
