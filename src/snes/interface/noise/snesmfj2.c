
#include <petsc/private/snesimpl.h>   /*I  "petscsnes.h"   I*/
/* matimpl.h is needed only for logging of matrix operation */
#include <petsc/private/matimpl.h>

PETSC_INTERN PetscErrorCode SNESUnSetMatrixFreeParameter(SNES);
PETSC_INTERN PetscErrorCode SNESDefaultMatrixFreeCreate2(SNES,Vec,Mat*);
PETSC_INTERN PetscErrorCode SNESDefaultMatrixFreeSetParameters2(Mat,PetscReal,PetscReal,PetscReal);

PETSC_INTERN PetscErrorCode SNESDiffParameterCreate_More(SNES,Vec,void**);
PETSC_INTERN PetscErrorCode SNESDiffParameterCompute_More(SNES,void*,Vec,Vec,PetscReal*,PetscReal*);
PETSC_INTERN PetscErrorCode SNESDiffParameterDestroy_More(void*);

typedef struct {  /* default context for matrix-free SNES */
  SNES         snes;             /* SNES context */
  Vec          w;                /* work vector */
  MatNullSpace sp;               /* null space context */
  PetscReal    error_rel;        /* square root of relative error in computing function */
  PetscReal    umin;             /* minimum allowable u'a value relative to |u|_1 */
  PetscBool    jorge;            /* flag indicating use of Jorge's method for determining the differencing parameter */
  PetscReal    h;                /* differencing parameter */
  PetscBool    need_h;           /* flag indicating whether we must compute h */
  PetscBool    need_err;         /* flag indicating whether we must currently compute error_rel */
  PetscBool    compute_err;      /* flag indicating whether we must ever compute error_rel */
  PetscInt     compute_err_iter; /* last iter where we've computer error_rel */
  PetscInt     compute_err_freq; /* frequency of computing error_rel */
  void         *data;            /* implementation-specific data */
} MFCtx_Private;

PetscErrorCode SNESMatrixFreeDestroy2_Private(Mat mat)
{
  MFCtx_Private  *ctx;

  PetscFunctionBegin;
  CHKERRQ(MatShellGetContext(mat,&ctx));
  CHKERRQ(VecDestroy(&ctx->w));
  CHKERRQ(MatNullSpaceDestroy(&ctx->sp));
  if (ctx->jorge || ctx->compute_err) CHKERRQ(SNESDiffParameterDestroy_More(ctx->data));
  CHKERRQ(PetscFree(ctx));
  PetscFunctionReturn(0);
}

/*
   SNESMatrixFreeView2_Private - Views matrix-free parameters.
 */
PetscErrorCode SNESMatrixFreeView2_Private(Mat J,PetscViewer viewer)
{
  MFCtx_Private  *ctx;
  PetscBool      iascii;

  PetscFunctionBegin;
  CHKERRQ(MatShellGetContext(J,&ctx));
  CHKERRQ(PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii));
  if (iascii) {
    CHKERRQ(PetscViewerASCIIPrintf(viewer,"  SNES matrix-free approximation:\n"));
    if (ctx->jorge) {
      CHKERRQ(PetscViewerASCIIPrintf(viewer,"    using Jorge's method of determining differencing parameter\n"));
    }
    CHKERRQ(PetscViewerASCIIPrintf(viewer,"    err=%g (relative error in function evaluation)\n",(double)ctx->error_rel));
    CHKERRQ(PetscViewerASCIIPrintf(viewer,"    umin=%g (minimum iterate parameter)\n",(double)ctx->umin));
    if (ctx->compute_err) {
      CHKERRQ(PetscViewerASCIIPrintf(viewer,"    freq_err=%D (frequency for computing err)\n",ctx->compute_err_freq));
    }
  }
  PetscFunctionReturn(0);
}

/*
  SNESMatrixFreeMult2_Private - Default matrix-free form for Jacobian-vector
  product, y = F'(u)*a:
        y = (F(u + ha) - F(u)) /h,
  where F = nonlinear function, as set by SNESSetFunction()
        u = current iterate
        h = difference interval
*/
PetscErrorCode SNESMatrixFreeMult2_Private(Mat mat,Vec a,Vec y)
{
  MFCtx_Private  *ctx;
  SNES           snes;
  PetscReal      h,norm,sum,umin,noise;
  PetscScalar    hs,dot;
  Vec            w,U,F;
  MPI_Comm       comm;
  PetscInt       iter;
  PetscErrorCode (*eval_fct)(SNES,Vec,Vec);

  PetscFunctionBegin;
  /* We log matrix-free matrix-vector products separately, so that we can
     separate the performance monitoring from the cases that use conventional
     storage.  We may eventually modify event logging to associate events
     with particular objects, hence alleviating the more general problem. */
  CHKERRQ(PetscLogEventBegin(MATMFFD_Mult,a,y,0,0));

  CHKERRQ(PetscObjectGetComm((PetscObject)mat,&comm));
  CHKERRQ(MatShellGetContext(mat,&ctx));
  snes = ctx->snes;
  w    = ctx->w;
  umin = ctx->umin;

  CHKERRQ(SNESGetSolution(snes,&U));
  eval_fct = SNESComputeFunction;
  CHKERRQ(SNESGetFunction(snes,&F,NULL,NULL));

  /* Determine a "good" step size, h */
  if (ctx->need_h) {

    /* Use Jorge's method to compute h */
    if (ctx->jorge) {
      CHKERRQ(SNESDiffParameterCompute_More(snes,ctx->data,U,a,&noise,&h));

      /* Use the Brown/Saad method to compute h */
    } else {
      /* Compute error if desired */
      CHKERRQ(SNESGetIterationNumber(snes,&iter));
      if ((ctx->need_err) || ((ctx->compute_err_freq) && (ctx->compute_err_iter != iter) && (!((iter-1)%ctx->compute_err_freq)))) {
        /* Use Jorge's method to compute noise */
        CHKERRQ(SNESDiffParameterCompute_More(snes,ctx->data,U,a,&noise,&h));

        ctx->error_rel = PetscSqrtReal(noise);

        CHKERRQ(PetscInfo(snes,"Using Jorge's noise: noise=%g, sqrt(noise)=%g, h_more=%g\n",(double)noise,(double)ctx->error_rel,(double)h));

        ctx->compute_err_iter = iter;
        ctx->need_err         = PETSC_FALSE;
      }

      CHKERRQ(VecDotBegin(U,a,&dot));
      CHKERRQ(VecNormBegin(a,NORM_1,&sum));
      CHKERRQ(VecNormBegin(a,NORM_2,&norm));
      CHKERRQ(VecDotEnd(U,a,&dot));
      CHKERRQ(VecNormEnd(a,NORM_1,&sum));
      CHKERRQ(VecNormEnd(a,NORM_2,&norm));

      /* Safeguard for step sizes too small */
      if (sum == 0.0) {
        dot  = 1.0;
        norm = 1.0;
      } else if (PetscAbsScalar(dot) < umin*sum && PetscRealPart(dot) >= 0.0) dot = umin*sum;
      else if (PetscAbsScalar(dot) < 0.0 && PetscRealPart(dot) > -umin*sum) dot = -umin*sum;
      h = PetscRealPart(ctx->error_rel*dot/(norm*norm));
    }
  } else h = ctx->h;

  if (!ctx->jorge || !ctx->need_h) CHKERRQ(PetscInfo(snes,"h = %g\n",(double)h));

  /* Evaluate function at F(u + ha) */
  hs   = h;
  CHKERRQ(VecWAXPY(w,hs,a,U));
  CHKERRQ(eval_fct(snes,w,y));
  CHKERRQ(VecAXPY(y,-1.0,F));
  CHKERRQ(VecScale(y,1.0/hs));
  if (mat->nullsp) CHKERRQ(MatNullSpaceRemove(mat->nullsp,y));

  CHKERRQ(PetscLogEventEnd(MATMFFD_Mult,a,y,0,0));
  PetscFunctionReturn(0);
}

/*@C
   SNESMatrixFreeCreate2 - Creates a matrix-free matrix
   context for use with a SNES solver.  This matrix can be used as
   the Jacobian argument for the routine SNESSetJacobian().

   Input Parameters:
+  snes - the SNES context
-  x - vector where SNES solution is to be stored.

   Output Parameter:
.  J - the matrix-free matrix

   Level: advanced

   Notes:
   The matrix-free matrix context merely contains the function pointers
   and work space for performing finite difference approximations of
   Jacobian-vector products, J(u)*a, via

$       J(u)*a = [J(u+h*a) - J(u)]/h,
$   where by default
$        h = error_rel*u'a/||a||^2                        if  |u'a| > umin*||a||_{1}
$          = error_rel*umin*sign(u'a)*||a||_{1}/||a||^2   otherwise
$   where
$        error_rel = square root of relative error in
$                    function evaluation
$        umin = minimum iterate parameter
$   Alternatively, the differencing parameter, h, can be set using
$   Jorge's nifty new strategy if one specifies the option
$          -snes_mf_jorge

   The user can set these parameters via MatMFFDSetFunctionError().
   See Users-Manual: ch_snes for details

   The user should call MatDestroy() when finished with the matrix-free
   matrix context.

   Options Database Keys:
$  -snes_mf_err <error_rel>
$  -snes_mf_unim <umin>
$  -snes_mf_compute_err
$  -snes_mf_freq_err <freq>
$  -snes_mf_jorge

.seealso: MatDestroy(), MatMFFDSetFunctionError()
@*/
PetscErrorCode  SNESDefaultMatrixFreeCreate2(SNES snes,Vec x,Mat *J)
{
  MPI_Comm       comm;
  MFCtx_Private  *mfctx;
  PetscInt       n,nloc;
  PetscBool      flg;
  char           p[64];

  PetscFunctionBegin;
  CHKERRQ(PetscNewLog(snes,&mfctx));
  mfctx->sp               = NULL;
  mfctx->snes             = snes;
  mfctx->error_rel        = PETSC_SQRT_MACHINE_EPSILON;
  mfctx->umin             = 1.e-6;
  mfctx->h                = 0.0;
  mfctx->need_h           = PETSC_TRUE;
  mfctx->need_err         = PETSC_FALSE;
  mfctx->compute_err      = PETSC_FALSE;
  mfctx->compute_err_freq = 0;
  mfctx->compute_err_iter = -1;
  mfctx->compute_err      = PETSC_FALSE;
  mfctx->jorge            = PETSC_FALSE;

  CHKERRQ(PetscOptionsGetReal(((PetscObject)snes)->options,((PetscObject)snes)->prefix,"-snes_mf_err",&mfctx->error_rel,NULL));
  CHKERRQ(PetscOptionsGetReal(((PetscObject)snes)->options,((PetscObject)snes)->prefix,"-snes_mf_umin",&mfctx->umin,NULL));
  CHKERRQ(PetscOptionsGetBool(((PetscObject)snes)->options,((PetscObject)snes)->prefix,"-snes_mf_jorge",&mfctx->jorge,NULL));
  CHKERRQ(PetscOptionsGetBool(((PetscObject)snes)->options,((PetscObject)snes)->prefix,"-snes_mf_compute_err",&mfctx->compute_err,NULL));
  CHKERRQ(PetscOptionsGetInt(((PetscObject)snes)->options,((PetscObject)snes)->prefix,"-snes_mf_freq_err",&mfctx->compute_err_freq,&flg));
  if (flg) {
    if (mfctx->compute_err_freq < 0) mfctx->compute_err_freq = 0;
    mfctx->compute_err = PETSC_TRUE;
  }
  if (mfctx->compute_err) mfctx->need_err = PETSC_TRUE;
  if (mfctx->jorge || mfctx->compute_err) {
    CHKERRQ(SNESDiffParameterCreate_More(snes,x,&mfctx->data));
  } else mfctx->data = NULL;

  CHKERRQ(PetscOptionsHasHelp(((PetscObject)snes)->options,&flg));
  CHKERRQ(PetscStrncpy(p,"-",sizeof(p)));
  if (((PetscObject)snes)->prefix) CHKERRQ(PetscStrlcat(p,((PetscObject)snes)->prefix,sizeof(p)));
  if (flg) {
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes)," Matrix-free Options (via SNES):\n"));
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes),"   %ssnes_mf_err <err>: set sqrt of relative error in function (default %g)\n",p,(double)mfctx->error_rel));
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes),"   %ssnes_mf_umin <umin>: see users manual (default %g)\n",p,(double)mfctx->umin));
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes),"   %ssnes_mf_jorge: use Jorge More's method\n",p));
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes),"   %ssnes_mf_compute_err: compute sqrt or relative error in function\n",p));
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes),"   %ssnes_mf_freq_err <freq>: frequency to recompute this (default only once)\n",p));
    CHKERRQ(PetscPrintf(PetscObjectComm((PetscObject)snes),"   %ssnes_mf_noise_file <file>: set file for printing noise info\n",p));
  }
  CHKERRQ(VecDuplicate(x,&mfctx->w));
  CHKERRQ(PetscObjectGetComm((PetscObject)x,&comm));
  CHKERRQ(VecGetSize(x,&n));
  CHKERRQ(VecGetLocalSize(x,&nloc));
  CHKERRQ(MatCreate(comm,J));
  CHKERRQ(MatSetSizes(*J,nloc,n,n,n));
  CHKERRQ(MatSetType(*J,MATSHELL));
  CHKERRQ(MatShellSetContext(*J,mfctx));
  CHKERRQ(MatShellSetOperation(*J,MATOP_MULT,(void (*)(void))SNESMatrixFreeMult2_Private));
  CHKERRQ(MatShellSetOperation(*J,MATOP_DESTROY,(void (*)(void))SNESMatrixFreeDestroy2_Private));
  CHKERRQ(MatShellSetOperation(*J,MATOP_VIEW,(void (*)(void))SNESMatrixFreeView2_Private));
  CHKERRQ(MatSetUp(*J));

  CHKERRQ(PetscLogObjectParent((PetscObject)*J,(PetscObject)mfctx->w));
  CHKERRQ(PetscLogObjectParent((PetscObject)snes,(PetscObject)*J));
  PetscFunctionReturn(0);
}

/*@C
   SNESDefaultMatrixFreeSetParameters2 - Sets the parameters for the approximation of
   matrix-vector products using finite differences.

$       J(u)*a = [J(u+h*a) - J(u)]/h where

   either the user sets h directly here, or this parameter is computed via

$        h = error_rel*u'a/||a||^2                        if  |u'a| > umin*||a||_{1}
$          = error_rel*umin*sign(u'a)*||a||_{1}/||a||^2   else
$

   Input Parameters:
+  mat - the matrix
.  error_rel - relative error (should be set to the square root of
     the relative error in the function evaluations)
.  umin - minimum allowable u-value
-  h - differencing parameter

   Level: advanced

   Notes:
   If the user sets the parameter h directly, then this value will be used
   instead of the default computation indicated above.

.seealso: MatCreateSNESMF()
@*/
PetscErrorCode  SNESDefaultMatrixFreeSetParameters2(Mat mat,PetscReal error,PetscReal umin,PetscReal h)
{
  MFCtx_Private  *ctx;

  PetscFunctionBegin;
  CHKERRQ(MatShellGetContext(mat,&ctx));
  if (ctx) {
    if (error != PETSC_DEFAULT) ctx->error_rel = error;
    if (umin  != PETSC_DEFAULT) ctx->umin = umin;
    if (h     != PETSC_DEFAULT) {
      ctx->h      = h;
      ctx->need_h = PETSC_FALSE;
    }
  }
  PetscFunctionReturn(0);
}

PetscErrorCode  SNESUnSetMatrixFreeParameter(SNES snes)
{
  MFCtx_Private  *ctx;
  Mat            mat;

  PetscFunctionBegin;
  CHKERRQ(SNESGetJacobian(snes,&mat,NULL,NULL,NULL));
  CHKERRQ(MatShellGetContext(mat,&ctx));
  if (ctx) ctx->need_h = PETSC_TRUE;
  PetscFunctionReturn(0);
}
