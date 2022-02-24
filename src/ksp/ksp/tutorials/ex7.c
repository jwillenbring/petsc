static char help[] = "Block Jacobi preconditioner for solving a linear system in parallel with KSP.\n\
The code indicates the\n\
procedures for setting the particular block sizes and for using different\n\
linear solvers on the individual blocks.\n\n";

/*
   Note:  This example focuses on ways to customize the block Jacobi
   preconditioner. See ex1.c and ex2.c for more detailed comments on
   the basic usage of KSP (including working with matrices and vectors).

   Recall: The block Jacobi method is equivalent to the ASM preconditioner
   with zero overlap.
 */

/*T
   Concepts: KSP^customizing the block Jacobi preconditioner
   Processors: n
T*/

/*
  Include "petscksp.h" so that we can use KSP solvers.  Note that this file
  automatically includes:
     petscsys.h       - base PETSc routines   petscvec.h - vectors
     petscmat.h - matrices
     petscis.h     - index sets            petscksp.h - Krylov subspace methods
     petscviewer.h - viewers               petscpc.h  - preconditioners
*/
#include <petscksp.h>

int main(int argc,char **args)
{
  Vec            x,b,u;      /* approx solution, RHS, exact solution */
  Mat            A;            /* linear system matrix */
  KSP            ksp;         /* KSP context */
  KSP            *subksp;     /* array of local KSP contexts on this processor */
  PC             pc;           /* PC context */
  PC             subpc;        /* PC context for subdomain */
  PetscReal      norm;         /* norm of solution error */
  PetscErrorCode ierr;
  PetscInt       i,j,Ii,J,*blks,m = 4,n;
  PetscMPIInt    rank,size;
  PetscInt       its,nlocal,first,Istart,Iend;
  PetscScalar    v,one = 1.0,none = -1.0;
  PetscBool      isbjacobi;

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  CHKERRQ(PetscOptionsGetInt(NULL,NULL,"-m",&m,NULL));
  CHKERRMPI(MPI_Comm_rank(PETSC_COMM_WORLD,&rank));
  CHKERRMPI(MPI_Comm_size(PETSC_COMM_WORLD,&size));
  n    = m+2;

  /* -------------------------------------------------------------------
         Compute the matrix and right-hand-side vector that define
         the linear system, Ax = b.
     ------------------------------------------------------------------- */

  /*
     Create and assemble parallel matrix
  */
  CHKERRQ(MatCreate(PETSC_COMM_WORLD,&A));
  CHKERRQ(MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,m*n,m*n));
  CHKERRQ(MatSetFromOptions(A));
  CHKERRQ(MatMPIAIJSetPreallocation(A,5,NULL,5,NULL));
  CHKERRQ(MatSeqAIJSetPreallocation(A,5,NULL));
  CHKERRQ(MatGetOwnershipRange(A,&Istart,&Iend));
  for (Ii=Istart; Ii<Iend; Ii++) {
    v = -1.0; i = Ii/n; j = Ii - i*n;
    if (i>0)   {J = Ii - n; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,ADD_VALUES));}
    if (i<m-1) {J = Ii + n; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,ADD_VALUES));}
    if (j>0)   {J = Ii - 1; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,ADD_VALUES));}
    if (j<n-1) {J = Ii + 1; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,ADD_VALUES));}
    v = 4.0; CHKERRQ(MatSetValues(A,1,&Ii,1,&Ii,&v,ADD_VALUES));
  }
  CHKERRQ(MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatSetOption(A,MAT_SYMMETRIC,PETSC_TRUE));

  /*
     Create parallel vectors
  */
  CHKERRQ(MatCreateVecs(A,&u,&b));
  CHKERRQ(VecDuplicate(u,&x));

  /*
     Set exact solution; then compute right-hand-side vector.
  */
  CHKERRQ(VecSet(u,one));
  CHKERRQ(MatMult(A,u,b));

  /*
     Create linear solver context
  */
  CHKERRQ(KSPCreate(PETSC_COMM_WORLD,&ksp));

  /*
     Set operators. Here the matrix that defines the linear system
     also serves as the preconditioning matrix.
  */
  CHKERRQ(KSPSetOperators(ksp,A,A));

  /*
     Set default preconditioner for this program to be block Jacobi.
     This choice can be overridden at runtime with the option
        -pc_type <type>

     IMPORTANT NOTE: Since the inners solves below are constructed to use
     iterative methods (such as KSPGMRES) the outer Krylov method should
     be set to use KSPFGMRES since it is the only Krylov method (plus KSPFCG)
     that allows the preconditioners to be nonlinear (that is have iterative methods
     inside them). The reason these examples work is because the number of
     iterations on the inner solves is left at the default (which is 10,000)
     and the tolerance on the inner solves is set to be a tight value of around 10^-6.
  */
  CHKERRQ(KSPGetPC(ksp,&pc));
  CHKERRQ(PCSetType(pc,PCBJACOBI));

  /* -------------------------------------------------------------------
                   Define the problem decomposition
     ------------------------------------------------------------------- */

  /*
     Call PCBJacobiSetTotalBlocks() to set individually the size of
     each block in the preconditioner.  This could also be done with
     the runtime option
         -pc_bjacobi_blocks <blocks>
     Also, see the command PCBJacobiSetLocalBlocks() to set the
     local blocks.

      Note: The default decomposition is 1 block per processor.
  */
  CHKERRQ(PetscMalloc1(m,&blks));
  for (i=0; i<m; i++) blks[i] = n;
  CHKERRQ(PCBJacobiSetTotalBlocks(pc,m,blks));
  CHKERRQ(PetscFree(blks));

  /*
    Set runtime options
  */
  CHKERRQ(KSPSetFromOptions(ksp));

  /* -------------------------------------------------------------------
               Set the linear solvers for the subblocks
     ------------------------------------------------------------------- */

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
       Basic method, should be sufficient for the needs of most users.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

     By default, the block Jacobi method uses the same solver on each
     block of the problem.  To set the same solver options on all blocks,
     use the prefix -sub before the usual PC and KSP options, e.g.,
          -sub_pc_type <pc> -sub_ksp_type <ksp> -sub_ksp_rtol 1.e-4
  */

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        Advanced method, setting different solvers for various blocks.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

     Note that each block's KSP context is completely independent of
     the others, and the full range of uniprocessor KSP options is
     available for each block. The following section of code is intended
     to be a simple illustration of setting different linear solvers for
     the individual blocks.  These choices are obviously not recommended
     for solving this particular problem.
  */
  CHKERRQ(PetscObjectTypeCompare((PetscObject)pc,PCBJACOBI,&isbjacobi));
  if (isbjacobi) {
    /*
       Call KSPSetUp() to set the block Jacobi data structures (including
       creation of an internal KSP context for each block).

       Note: KSPSetUp() MUST be called before PCBJacobiGetSubKSP().
    */
    CHKERRQ(KSPSetUp(ksp));

    /*
       Extract the array of KSP contexts for the local blocks
    */
    CHKERRQ(PCBJacobiGetSubKSP(pc,&nlocal,&first,&subksp));

    /*
       Loop over the local blocks, setting various KSP options
       for each block.
    */
    for (i=0; i<nlocal; i++) {
      CHKERRQ(KSPGetPC(subksp[i],&subpc));
      if (rank == 0) {
        if (i%2) {
          CHKERRQ(PCSetType(subpc,PCILU));
        } else {
          CHKERRQ(PCSetType(subpc,PCNONE));
          CHKERRQ(KSPSetType(subksp[i],KSPBCGS));
          CHKERRQ(KSPSetTolerances(subksp[i],1.e-6,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT));
        }
      } else {
        CHKERRQ(PCSetType(subpc,PCJACOBI));
        CHKERRQ(KSPSetType(subksp[i],KSPGMRES));
        CHKERRQ(KSPSetTolerances(subksp[i],1.e-6,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT));
      }
    }
  }

  /* -------------------------------------------------------------------
                      Solve the linear system
     ------------------------------------------------------------------- */

  /*
     Solve the linear system
  */
  CHKERRQ(KSPSolve(ksp,b,x));

  /* -------------------------------------------------------------------
                      Check solution and clean up
     ------------------------------------------------------------------- */

  /*
     Check the error
  */
  CHKERRQ(VecAXPY(x,none,u));
  CHKERRQ(VecNorm(x,NORM_2,&norm));
  CHKERRQ(KSPGetIterationNumber(ksp,&its));
  CHKERRQ(PetscPrintf(PETSC_COMM_WORLD,"Norm of error %g iterations %D\n",(double)norm,its));

  /*
     Free work space.  All PETSc objects should be destroyed when they
     are no longer needed.
  */
  CHKERRQ(KSPDestroy(&ksp));
  CHKERRQ(VecDestroy(&u));  CHKERRQ(VecDestroy(&x));
  CHKERRQ(VecDestroy(&b));  CHKERRQ(MatDestroy(&A));
  ierr = PetscFinalize();
  return ierr;
}

/*TEST

   test:
      suffix: 1
      nsize: 2
      args: -ksp_monitor_short -ksp_gmres_cgs_refinement_type refine_always

   test:
      suffix: 2
      nsize: 2
      args: -ksp_view ::ascii_info_detail

   test:
      suffix: viennacl
      requires: viennacl
      args: -ksp_monitor_short -mat_type aijviennacl
      output_file: output/ex7_mpiaijcusparse.out

   test:
      suffix: viennacl_2
      nsize: 2
      requires: viennacl
      args: -ksp_monitor_short -mat_type aijviennacl
      output_file: output/ex7_mpiaijcusparse_2.out

   test:
      suffix: mpiaijcusparse
      requires: cuda
      args: -ksp_monitor_short -mat_type aijcusparse

   test:
      suffix: mpiaijcusparse_2
      nsize: 2
      requires: cuda
      args: -ksp_monitor_short -mat_type aijcusparse

   test:
      suffix: mpiaijcusparse_simple
      requires: cuda
      args: -ksp_monitor_short -mat_type aijcusparse -sub_pc_factor_mat_solver_type cusparse -sub_ksp_type preonly -sub_pc_type ilu

   test:
      suffix: mpiaijcusparse_simple_2
      nsize: 2
      requires: cuda
      args: -ksp_monitor_short -mat_type aijcusparse -sub_pc_factor_mat_solver_type cusparse -sub_ksp_type preonly -sub_pc_type ilu

   test:
      suffix: mpiaijcusparse_3
      requires: cuda
      args: -ksp_monitor_short -mat_type aijcusparse -sub_pc_factor_mat_solver_type cusparse

   test:
      suffix: mpiaijcusparse_4
      nsize: 2
      requires: cuda
      args: -ksp_monitor_short -mat_type aijcusparse -sub_pc_factor_mat_solver_type cusparse

   testset:
      args: -ksp_monitor_short -pc_type gamg -ksp_view -pc_gamg_esteig_ksp_type cg -pc_gamg_esteig_ksp_max_it 10
      test:
        suffix: gamg_cuda
        nsize: {{1 2}separate output}
        requires: cuda
        args: -mat_type aijcusparse
        # triggers cusparse MatTransposeMat operation when squaring the graph
        args: -pc_gamg_sym_graph 0 -pc_gamg_threshold -1 -pc_gamg_square_graph 1
      test:
        suffix: gamg_kokkos
        nsize: {{1 2}separate output}
        requires: !sycl kokkos_kernels
        args: -mat_type aijkokkos

TEST*/
