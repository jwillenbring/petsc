/* XH: todo add cs1f.F90 and asjust makefile */
/*
   Include "petsctao.h" so that we can use TAO solvers.  Note that this
   file automatically includes libraries such as:
     petsc.h       - base PETSc routines   petscvec.h - vectors
     petscsys.h    - system routines        petscmat.h - matrices
     petscis.h     - index sets            petscksp.h - Krylov subspace methods
     petscviewer.h - viewers               petscpc.h  - preconditioners

*/

#include <petsctao.h>

/*
Description:   Compressive sensing test example 1.
               0.5*||Ax-b||^2 + lambda*||D*x||_1
               Xiang Huang: Nov 19, 2018

Reference:     None
*/

static char help[] = "Finds the least-squares solution to the under constraint linear model Ax = b, with L1-norm regularizer. \n\
            A is a M*N real matrix (M<N), x is sparse. \n\
            We find the sparse solution by solving 0.5*||Ax-b||^2 + lambda*||D*x||_1, where lambda (by default 1e-4) is a user specified weight.\n\
            D is the K*N transform matrix so that D*x is sparse. By default D is identity matrix, so that D*x = x.\n";
/*T
   Concepts: TAO^Solving a system of nonlinear equations, nonlinear least squares
   Routines: TaoCreate();
   Routines: TaoSetType();
   Routines: TaoSetSeparableObjectiveRoutine();
   Routines: TaoSetJacobianRoutine();
   Routines: TaoSetSolution();
   Routines: TaoSetFromOptions();
   Routines: TaoSetConvergenceHistory(); TaoGetConvergenceHistory();
   Routines: TaoSolve();
   Routines: TaoView(); TaoDestroy();
   Processors: 1
T*/

#define M 3
#define N 5
#define K 4

/* User-defined application context */
typedef struct {
  /* Working space. linear least square:  f(x) = A*x - b */
  PetscReal A[M][N];    /* array of coefficients */
  PetscReal b[M];       /* array of observations */
  PetscReal xGT[M];     /* array of ground truth object, which can be used to compare the reconstruction result */
  PetscReal D[K][N];    /* array of coefficients for 0.5*||Ax-b||^2 + lambda*||D*x||_1 */
  PetscReal J[M][N];    /* dense jacobian matrix array. For linear least square, J = A. For nonlinear least square, it is different from A */
  PetscInt  idm[M];     /* Matrix row, column indices for jacobian and dictionary */
  PetscInt  idn[N];
  PetscInt  idk[K];
} AppCtx;

/* User provided Routines */
PetscErrorCode InitializeUserData(AppCtx *);
PetscErrorCode FormStartingPoint(Vec);
PetscErrorCode FormDictionaryMatrix(Mat,AppCtx *);
PetscErrorCode EvaluateFunction(Tao,Vec,Vec,void *);
PetscErrorCode EvaluateJacobian(Tao,Vec,Mat,Mat,void *);

/*--------------------------------------------------------------------*/
int main(int argc,char **argv)
{
  PetscErrorCode ierr;               /* used to check for functions returning nonzeros */
  Vec            x,f;               /* solution, function f(x) = A*x-b */
  Mat            J,D;               /* Jacobian matrix, Transform matrix */
  Tao            tao;                /* Tao solver context */
  PetscInt       i;                  /* iteration information */
  PetscReal      hist[100],resid[100];
  PetscInt       lits[100];
  AppCtx         user;               /* user-defined work context */

  ierr = PetscInitialize(&argc,&argv,(char *)0,help);if (ierr) return ierr;

  /* Allocate solution and vector function vectors */
  CHKERRQ(VecCreateSeq(PETSC_COMM_SELF,N,&x));
  CHKERRQ(VecCreateSeq(PETSC_COMM_SELF,M,&f));

  /* Allocate Jacobian and Dictionary matrix. */
  CHKERRQ(MatCreateSeqDense(PETSC_COMM_SELF,M,N,NULL,&J));
  CHKERRQ(MatCreateSeqDense(PETSC_COMM_SELF,K,N,NULL,&D)); /* XH: TODO: dense -> sparse/dense/shell etc, do it on fly  */

  for (i=0;i<M;i++) user.idm[i] = i;
  for (i=0;i<N;i++) user.idn[i] = i;
  for (i=0;i<K;i++) user.idk[i] = i;

  /* Create TAO solver and set desired solution method */
  CHKERRQ(TaoCreate(PETSC_COMM_SELF,&tao));
  CHKERRQ(TaoSetType(tao,TAOBRGN));

  /* User set application context: A, D matrice, and b vector. */
  CHKERRQ(InitializeUserData(&user));

  /* Set initial guess */
  CHKERRQ(FormStartingPoint(x));

  /* Fill the content of matrix D from user application Context */
  CHKERRQ(FormDictionaryMatrix(D,&user));

  /* Bind x to tao->solution. */
  CHKERRQ(TaoSetSolution(tao,x));
  /* Bind D to tao->data->D */
  CHKERRQ(TaoBRGNSetDictionaryMatrix(tao,D));

  /* Set the function and Jacobian routines. */
  CHKERRQ(TaoSetResidualRoutine(tao,f,EvaluateFunction,(void*)&user));
  CHKERRQ(TaoSetJacobianResidualRoutine(tao,J,J,EvaluateJacobian,(void*)&user));

  /* Check for any TAO command line arguments */
  CHKERRQ(TaoSetFromOptions(tao));

  CHKERRQ(TaoSetConvergenceHistory(tao,hist,resid,0,lits,100,PETSC_TRUE));

  /* Perform the Solve */
  CHKERRQ(TaoSolve(tao));

  /* XH: Debug: View the result, function and Jacobian.  */
  CHKERRQ(PetscPrintf(PETSC_COMM_SELF, "-------- result x, residual f=A*x-b, and Jacobian=A. -------- \n"));
  CHKERRQ(VecView(x,PETSC_VIEWER_STDOUT_SELF));
  CHKERRQ(VecView(f,PETSC_VIEWER_STDOUT_SELF));
  CHKERRQ(MatView(J,PETSC_VIEWER_STDOUT_SELF));
  CHKERRQ(MatView(D,PETSC_VIEWER_STDOUT_SELF));

  /* Free TAO data structures */
  CHKERRQ(TaoDestroy(&tao));

   /* Free PETSc data structures */
  CHKERRQ(VecDestroy(&x));
  CHKERRQ(VecDestroy(&f));
  CHKERRQ(MatDestroy(&J));
  CHKERRQ(MatDestroy(&D));

  ierr = PetscFinalize();
  return ierr;
}

/*--------------------------------------------------------------------*/
PetscErrorCode EvaluateFunction(Tao tao, Vec X, Vec F, void *ptr)
{
  AppCtx         *user = (AppCtx *)ptr;
  PetscInt       m,n;
  const PetscReal *x;
  PetscReal      *b=user->b,*f;

  PetscFunctionBegin;
  CHKERRQ(VecGetArrayRead(X,&x));
  CHKERRQ(VecGetArray(F,&f));

  /* Even for linear least square, we do not direct use matrix operation f = A*x - b now, just for future modification and compatibility for nonlinear least square */
  for (m=0;m<M;m++) {
    f[m] = -b[m];
    for (n=0;n<N;n++) {
      f[m] += user->A[m][n]*x[n];
    }
  }
  CHKERRQ(VecRestoreArrayRead(X,&x));
  CHKERRQ(VecRestoreArray(F,&f));
  PetscLogFlops(2.0*M*N);
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/
/* J[m][n] = df[m]/dx[n] */
PetscErrorCode EvaluateJacobian(Tao tao, Vec X, Mat J, Mat Jpre, void *ptr)
{
  AppCtx         *user = (AppCtx *)ptr;
  PetscInt       m,n;
  const PetscReal *x;

  PetscFunctionBegin;
  CHKERRQ(VecGetArrayRead(X,&x)); /* not used for linear least square, but keep for future nonlinear least square) */
  /* XH: TODO:  For linear least square, we can just set J=A fixed once, instead of keep update it! Maybe just create a function getFixedJacobian?
    For nonlinear least square, we require x to compute J, keep codes here for future nonlinear least square*/
  for (m=0; m<M; ++m) {
    for (n=0; n<N; ++n) {
      user->J[m][n] = user->A[m][n];
    }
  }

  CHKERRQ(MatSetValues(J,M,user->idm,N,user->idn,(PetscReal *)user->J,INSERT_VALUES));
  CHKERRQ(MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY));

  CHKERRQ(VecRestoreArrayRead(X,&x));/* not used for linear least square, but keep for future nonlinear least square) */
  PetscLogFlops(0);  /* 0 for linear least square, >0 for nonlinear least square */
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------ */
/* Currently fixed matrix, in future may be dynamic for D(x)? */
PetscErrorCode FormDictionaryMatrix(Mat D,AppCtx *user)
{
  PetscFunctionBegin;
  CHKERRQ(MatSetValues(D,K,user->idk,N,user->idn,(PetscReal *)user->D,INSERT_VALUES));
  CHKERRQ(MatAssemblyBegin(D,MAT_FINAL_ASSEMBLY));
  CHKERRQ(MatAssemblyEnd(D,MAT_FINAL_ASSEMBLY));

  PetscLogFlops(0); /* 0 for fixed dictionary matrix, >0 for varying dictionary matrix */
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------ */
PetscErrorCode FormStartingPoint(Vec X)
{
  PetscFunctionBegin;
  CHKERRQ(VecSet(X,0.0));
  PetscFunctionReturn(0);
}

/* ---------------------------------------------------------------------- */
PetscErrorCode InitializeUserData(AppCtx *user)
{
  PetscReal *b=user->b; /* **A=user->A, but we don't kown the dimension of A in this way, how to fix? */
  PetscInt  m,n,k; /* loop index for M,N,K dimension. */

  PetscFunctionBegin;
  /* b = A*x while x = [0;0;1;0;0] here*/
  m = 0;
  b[m++] = 0.28;
  b[m++] = 0.55;
  b[m++] = 0.96;

  /* matlab generated random matrix, uniformly distributed in [0,1] with 2 digits accuracy. rng(0); A = rand(M, N); A = round(A*100)/100;
  A = [0.81  0.91  0.28  0.96  0.96
       0.91  0.63  0.55  0.16  0.49
       0.13  0.10  0.96  0.97  0.80]
  */
  m=0; n=0; user->A[m][n++] = 0.81; user->A[m][n++] = 0.91; user->A[m][n++] = 0.28; user->A[m][n++] = 0.96; user->A[m][n++] = 0.96;
  ++m; n=0; user->A[m][n++] = 0.91; user->A[m][n++] = 0.63; user->A[m][n++] = 0.55; user->A[m][n++] = 0.16; user->A[m][n++] = 0.49;
  ++m; n=0; user->A[m][n++] = 0.13; user->A[m][n++] = 0.10; user->A[m][n++] = 0.96; user->A[m][n++] = 0.97; user->A[m][n++] = 0.80;

  /* initialize to 0 */
  for (k=0; k<K; k++) {
    for (n=0; n<N; n++) {
      user->D[k][n] = 0.0;
    }
  }
  /* Choice I: set D to identity matrix of size N*N for testing */
  /* for (k=0; k<K; k++) user->D[k][k] = 1.0; */
  /* Choice II: set D to Backward difference matrix of size (N-1)*N, with zero extended boundary assumption */
  for (k=0;k<K;k++) {
      user->D[k][k]   = -1.0;
      user->D[k][k+1] = 1.0;
  }

  PetscFunctionReturn(0);
}

/*TEST

   build:
      requires: !complex !single !quad !defined(PETSC_USE_64BIT_INDICES)

   test:
      localrunfiles: cs1Data_A_b_xGT
      args: -tao_smonitor -tao_max_it 100 -tao_type pounders -tao_gatol 1.e-6

   test:
      suffix: 2
      localrunfiles: cs1Data_A_b_xGT
      args: -tao_monitor -tao_max_it 100 -tao_type brgn -tao_brgn_regularization_type l2prox -tao_brgn_regularizer_weight 1e-8 -tao_gatol 1.e-6 -tao_brgn_subsolver_tao_bnk_ksp_converged_reason

   test:
      suffix: 3
      localrunfiles: cs1Data_A_b_xGT
      args: -tao_monitor -tao_max_it 100 -tao_type brgn -tao_brgn_regularization_type l1dict -tao_brgn_regularizer_weight 1e-8 -tao_brgn_l1_smooth_epsilon 1e-6 -tao_gatol 1.e-6

   test:
      suffix: 4
      localrunfiles: cs1Data_A_b_xGT
      args: -tao_monitor -tao_max_it 100 -tao_type brgn -tao_brgn_regularization_type l2pure -tao_brgn_regularizer_weight 1e-8 -tao_gatol 1.e-6

   test:
      suffix: 5
      localrunfiles: cs1Data_A_b_xGT
      args: -tao_monitor -tao_max_it 100 -tao_type brgn -tao_brgn_regularization_type lm -tao_gatol 1.e-6 -tao_brgn_subsolver_tao_type bnls

TEST*/
