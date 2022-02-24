
static char help[] = "Solves a linear system in parallel with KSP.  Also\n\
illustrates setting a user-defined shell preconditioner and using the\n\
Input parameters include:\n\
  -user_defined_pc : Activate a user-defined preconditioner\n\n";

/*T
   Concepts: KSP^basic parallel example
   Concepts: PC^setting a user-defined shell preconditioner
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

/* Define context for user-provided preconditioner */
typedef struct {
  Vec diag;
} SampleShellPC;

/* Declare routines for user-provided preconditioner */
extern PetscErrorCode SampleShellPCCreate(SampleShellPC**);
extern PetscErrorCode SampleShellPCSetUp(PC,Mat,Vec);
extern PetscErrorCode SampleShellPCApply(PC,Vec x,Vec y);
extern PetscErrorCode SampleShellPCDestroy(PC);

/*
   User-defined routines.  Note that immediately before each routine below,
   If defined, this macro is used in the PETSc error handlers to provide a
   complete traceback of routine names.  All PETSc library routines use this
   macro, and users can optionally employ it as well in their application
   codes.  Note that users can get a traceback of PETSc errors regardless of
   provides the added traceback detail of the application routine names.
*/

int main(int argc,char **args)
{
  Vec            x,b,u;   /* approx solution, RHS, exact solution */
  Mat            A;         /* linear system matrix */
  KSP            ksp;      /* linear solver context */
  PC             pc;        /* preconditioner context */
  PetscReal      norm;      /* norm of solution error */
  SampleShellPC  *shell;    /* user-defined preconditioner context */
  PetscScalar    v,one = 1.0,none = -1.0;
  PetscInt       i,j,Ii,J,Istart,Iend,m = 8,n = 7,its;
  PetscErrorCode ierr;
  PetscBool      user_defined_pc = PETSC_FALSE;

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  CHKERRQ(PetscOptionsGetInt(NULL,NULL,"-m",&m,NULL));
  CHKERRQ(PetscOptionsGetInt(NULL,NULL,"-n",&n,NULL));

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
         Compute the matrix and right-hand-side vector that define
         the linear system, Ax = b.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /*
     Create parallel matrix, specifying only its global dimensions.
     When using MatCreate(), the matrix format can be specified at
     runtime. Also, the parallel partioning of the matrix is
     determined by PETSc at runtime.
  */
  CHKERRQ(MatCreate(PETSC_COMM_WORLD,&A));
  CHKERRQ(MatSetSizes(A,PETSC_DECIDE,PETSC_DECIDE,m*n,m*n));
  CHKERRQ(MatSetFromOptions(A));
  CHKERRQ(MatSetUp(A));

  /*
     Currently, all PETSc parallel matrix formats are partitioned by
     contiguous chunks of rows across the processors.  Determine which
     rows of the matrix are locally owned.
  */
  CHKERRQ(MatGetOwnershipRange(A,&Istart,&Iend));

  /*
     Set matrix elements for the 2-D, five-point stencil in parallel.
      - Each processor needs to insert only elements that it owns
        locally (but any non-local elements will be sent to the
        appropriate processor during matrix assembly).
      - Always specify global rows and columns of matrix entries.
   */
  for (Ii=Istart; Ii<Iend; Ii++) {
    v = -1.0; i = Ii/n; j = Ii - i*n;
    if (i>0)   {J = Ii - n; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES));}
    if (i<m-1) {J = Ii + n; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES));}
    if (j>0)   {J = Ii - 1; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES));}
    if (j<n-1) {J = Ii + 1; CHKERRQ(MatSetValues(A,1,&Ii,1,&J,&v,INSERT_VALUES));}
    v = 4.0; CHKERRQ(MatSetValues(A,1,&Ii,1,&Ii,&v,INSERT_VALUES));
  }

  /*
     Assemble matrix, using the 2-step process:
       MatAssemblyBegin(), MatAssemblyEnd()
     Computations can be done while messages are in transition
     by placing code between these two statements.
  */
  CHKERRQ(MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY));

  /*
     Create parallel vectors.
      - When using VecCreate() VecSetSizes() and VecSetFromOptions(),
        we specify only the vector's global
        dimension; the parallel partitioning is determined at runtime.
      - Note: We form 1 vector from scratch and then duplicate as needed.
  */
  CHKERRQ(VecCreate(PETSC_COMM_WORLD,&u));
  CHKERRQ(VecSetSizes(u,PETSC_DECIDE,m*n));
  CHKERRQ(VecSetFromOptions(u));
  CHKERRQ(VecDuplicate(u,&b));
  CHKERRQ(VecDuplicate(b,&x));

  /*
     Set exact solution; then compute right-hand-side vector.
  */
  CHKERRQ(VecSet(u,one));
  CHKERRQ(MatMult(A,u,b));

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
                Create the linear solver and set various options
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

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
     Set linear solver defaults for this problem (optional).
     - By extracting the KSP and PC contexts from the KSP context,
       we can then directly call any KSP and PC routines
       to set various options.
  */
  CHKERRQ(KSPGetPC(ksp,&pc));
  ierr = KSPSetTolerances(ksp,1.e-7,PETSC_DEFAULT,PETSC_DEFAULT,
                          PETSC_DEFAULT);CHKERRQ(ierr);

  /*
     Set a user-defined "shell" preconditioner if desired
  */
  CHKERRQ(PetscOptionsGetBool(NULL,NULL,"-user_defined_pc",&user_defined_pc,NULL));
  if (user_defined_pc) {
    /* (Required) Indicate to PETSc that we're using a "shell" preconditioner */
    CHKERRQ(PCSetType(pc,PCSHELL));

    /* (Optional) Create a context for the user-defined preconditioner; this
       context can be used to contain any application-specific data. */
    CHKERRQ(SampleShellPCCreate(&shell));

    /* (Required) Set the user-defined routine for applying the preconditioner */
    CHKERRQ(PCShellSetApply(pc,SampleShellPCApply));
    CHKERRQ(PCShellSetContext(pc,shell));

    /* (Optional) Set user-defined function to free objects used by custom preconditioner */
    CHKERRQ(PCShellSetDestroy(pc,SampleShellPCDestroy));

    /* (Optional) Set a name for the preconditioner, used for PCView() */
    CHKERRQ(PCShellSetName(pc,"MyPreconditioner"));

    /* (Optional) Do any setup required for the preconditioner */
    /* Note: This function could be set with PCShellSetSetUp and it would be called when necessary */
    CHKERRQ(SampleShellPCSetUp(pc,A,x));

  } else {
    CHKERRQ(PCSetType(pc,PCJACOBI));
  }

  /*
    Set runtime options, e.g.,
        -ksp_type <type> -pc_type <type> -ksp_monitor -ksp_rtol <rtol>
    These options will override those specified above as long as
    KSPSetFromOptions() is called _after_ any other customization
    routines.
  */
  CHKERRQ(KSPSetFromOptions(ksp));

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
                      Solve the linear system
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  CHKERRQ(KSPSolve(ksp,b,x));

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
                      Check solution and clean up
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

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

/***********************************************************************/
/*          Routines for a user-defined shell preconditioner           */
/***********************************************************************/

/*
   SampleShellPCCreate - This routine creates a user-defined
   preconditioner context.

   Output Parameter:
.  shell - user-defined preconditioner context
*/
PetscErrorCode SampleShellPCCreate(SampleShellPC **shell)
{
  SampleShellPC  *newctx;

  CHKERRQ(PetscNew(&newctx));
  newctx->diag = 0;
  *shell       = newctx;
  return 0;
}
/* ------------------------------------------------------------------- */
/*
   SampleShellPCSetUp - This routine sets up a user-defined
   preconditioner context.

   Input Parameters:
.  pc    - preconditioner object
.  pmat  - preconditioner matrix
.  x     - vector

   Output Parameter:
.  shell - fully set up user-defined preconditioner context

   Notes:
   In this example, we define the shell preconditioner to be Jacobi's
   method.  Thus, here we create a work vector for storing the reciprocal
   of the diagonal of the preconditioner matrix; this vector is then
   used within the routine SampleShellPCApply().
*/
PetscErrorCode SampleShellPCSetUp(PC pc,Mat pmat,Vec x)
{
  SampleShellPC  *shell;
  Vec            diag;

  CHKERRQ(PCShellGetContext(pc,&shell));
  CHKERRQ(VecDuplicate(x,&diag));
  CHKERRQ(MatGetDiagonal(pmat,diag));
  CHKERRQ(VecReciprocal(diag));

  shell->diag = diag;
  return 0;
}
/* ------------------------------------------------------------------- */
/*
   SampleShellPCApply - This routine demonstrates the use of a
   user-provided preconditioner.

   Input Parameters:
+  pc - preconditioner object
-  x - input vector

   Output Parameter:
.  y - preconditioned vector

   Notes:
   This code implements the Jacobi preconditioner, merely as an
   example of working with a PCSHELL.  Note that the Jacobi method
   is already provided within PETSc.
*/
PetscErrorCode SampleShellPCApply(PC pc,Vec x,Vec y)
{
  SampleShellPC  *shell;

  CHKERRQ(PCShellGetContext(pc,&shell));
  CHKERRQ(VecPointwiseMult(y,x,shell->diag));

  return 0;
}
/* ------------------------------------------------------------------- */
/*
   SampleShellPCDestroy - This routine destroys a user-defined
   preconditioner context.

   Input Parameter:
.  shell - user-defined preconditioner context
*/
PetscErrorCode SampleShellPCDestroy(PC pc)
{
  SampleShellPC  *shell;

  CHKERRQ(PCShellGetContext(pc,&shell));
  CHKERRQ(VecDestroy(&shell->diag));
  CHKERRQ(PetscFree(shell));

  return 0;
}

/*TEST

   build:
      requires: !complex !single

   test:
      nsize: 2
      args: -ksp_view -user_defined_pc -ksp_gmres_cgs_refinement_type refine_always

   test:
      suffix: tsirm
      args: -m 60 -n 60 -ksp_type tsirm -pc_type ksp -ksp_monitor_short -ksp_ksp_type fgmres -ksp_ksp_rtol 1e-10 -ksp_pc_type mg -ksp_ksp_max_it 30
      timeoutfactor: 4

TEST*/
