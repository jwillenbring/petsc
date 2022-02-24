static char help[] = "Test Mat products \n\n";

#include <petscmat.h>
int main(int argc,char **args)
{
  Mat             A=NULL,B=NULL,C=NULL,D=NULL,E=NULL;
  PetscErrorCode  ierr;
  PetscInt        k;
  const PetscInt  M = 18,N = 18;
  PetscMPIInt     rank;

  /* A, B are 18 x 18 nonsymmetric matrices and have the same sparsity pattern but different values.
     Big enough to have complex communication patterns but still small enough for debugging.
  */
  PetscInt Ai[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5,  6, 6, 7, 7, 8, 8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17};
  PetscInt Aj[] = {0, 1, 2, 7, 3, 8, 4, 9, 5, 8, 2, 6, 11, 0, 7, 1, 6, 2, 4, 10, 16, 11, 15, 12, 17, 12, 13, 14, 15, 17, 11, 13,  3, 16,  9, 15, 11, 13};
  PetscInt Bi[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5,  6, 6, 7, 7, 8, 8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17};
  PetscInt Bj[] = {0, 1, 2, 7, 3, 8, 4, 9, 5, 8, 2, 6, 11, 0, 7, 1, 6, 2, 4, 10, 16, 11, 15, 12, 17, 12, 13, 14, 15, 17, 11, 13,  3, 16,  9, 15, 11, 13};

  PetscInt Annz = sizeof(Ai)/sizeof(PetscInt);
  PetscInt Bnnz = sizeof(Bi)/sizeof(PetscInt);

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  CHKERRMPI(MPI_Comm_rank(PETSC_COMM_WORLD,&rank));

  CHKERRQ(MatCreate(PETSC_COMM_WORLD,&A));
  ierr = MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,M,N);
  CHKERRQ(MatSetFromOptions(A));
  ierr = MatSeqAIJSetPreallocation(A,2,NULL);
  CHKERRQ(MatMPIAIJSetPreallocation(A,2,NULL,2,NULL));
  CHKERRQ(MatSetOption(A,MAT_NEW_NONZERO_ALLOCATION_ERR,PETSC_FALSE));

  if (rank == 0) {
    for (k=0; k<Annz; k++) CHKERRQ(MatSetValue(A,Ai[k],Aj[k],Ai[k]+Aj[k]+1.0,INSERT_VALUES));
  }

  CHKERRQ(MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY));

  CHKERRQ(MatCreate(PETSC_COMM_WORLD,&B));
  ierr = MatSetSizes(B,PETSC_DECIDE,PETSC_DECIDE,M,N);
  CHKERRQ(MatSetFromOptions(B));
  ierr = MatSeqAIJSetPreallocation(B,2,NULL);
  CHKERRQ(MatMPIAIJSetPreallocation(B,2,NULL,2,NULL));
  CHKERRQ(MatSetOption(B,MAT_NEW_NONZERO_ALLOCATION_ERR,PETSC_FALSE));

  if (rank == 0) {
    for (k=0; k<Bnnz; k++) CHKERRQ(MatSetValue(B,Bi[k],Bj[k],Bi[k]+Bj[k]+2.0,INSERT_VALUES));
  }
  CHKERRQ(MatAssemblyBegin(B,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatAssemblyEnd(B,MAT_FINAL_ASSEMBLY));

  CHKERRQ(MatMatMult(A,B,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&C));
  CHKERRQ(MatView(C,PETSC_VIEWER_STDOUT_WORLD));

  /* B, A have the same nonzero pattern, so it is legitimate to do so */
  CHKERRQ(MatMatMult(B,A,MAT_REUSE_MATRIX,PETSC_DEFAULT,&C));
  CHKERRQ(MatView(C,PETSC_VIEWER_STDOUT_WORLD));

  CHKERRQ(MatTransposeMatMult(A,B,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&D));
  CHKERRQ(MatView(D, PETSC_VIEWER_STDOUT_WORLD));

  CHKERRQ(MatPtAP(A,B,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&E));
  CHKERRQ(MatView(E,PETSC_VIEWER_STDOUT_WORLD));

  CHKERRQ(MatDestroy(&A));
  CHKERRQ(MatDestroy(&B));
  CHKERRQ(MatDestroy(&C));
  CHKERRQ(MatDestroy(&D));
  CHKERRQ(MatDestroy(&E));

  ierr = PetscFinalize();
  return ierr;
}

/*TEST
  testset:
    filter: grep -ve type -ve "Mat Object"
    output_file: output/ex250_1.out

    test:
      suffix: 1
      nsize: {{1 3}}
      args: -mat_type aij

    test:
      suffix: 2
      nsize: {{3 4}}
      args: -mat_type aij -matmatmult_via backend -matptap_via backend -mattransposematmult_via backend

    test:
      suffix: cuda
      requires: cuda
      nsize: {{1 3 4}}
      args: -mat_type aijcusparse

    test:
      suffix: kok
      requires: !sycl kokkos_kernels
      nsize: {{1 3 4}}
      args: -mat_type aijkokkos

TEST*/
