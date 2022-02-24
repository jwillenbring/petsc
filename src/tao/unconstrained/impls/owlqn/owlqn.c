#include <petsctaolinesearch.h>
#include <../src/tao/unconstrained/impls/owlqn/owlqn.h>

#define OWLQN_BFGS                0
#define OWLQN_SCALED_GRADIENT     1
#define OWLQN_GRADIENT            2

static PetscErrorCode ProjDirect_OWLQN(Vec d, Vec g)
{
  const PetscReal *gptr;
  PetscReal       *dptr;
  PetscInt        low,high,low1,high1,i;

  PetscFunctionBegin;
  CHKERRQ(VecGetOwnershipRange(d,&low,&high));
  CHKERRQ(VecGetOwnershipRange(g,&low1,&high1));

  CHKERRQ(VecGetArrayRead(g,&gptr));
  CHKERRQ(VecGetArray(d,&dptr));
  for (i = 0; i < high-low; i++) {
    if (dptr[i] * gptr[i] <= 0.0) {
      dptr[i] = 0.0;
    }
  }
  CHKERRQ(VecRestoreArray(d,&dptr));
  CHKERRQ(VecRestoreArrayRead(g,&gptr));
  PetscFunctionReturn(0);
}

static PetscErrorCode ComputePseudoGrad_OWLQN(Vec x, Vec gv, PetscReal lambda)
{
  const PetscReal *xptr;
  PetscReal       *gptr;
  PetscInt        low,high,low1,high1,i;

  PetscFunctionBegin;
  CHKERRQ(VecGetOwnershipRange(x,&low,&high));
  CHKERRQ(VecGetOwnershipRange(gv,&low1,&high1));

  CHKERRQ(VecGetArrayRead(x,&xptr));
  CHKERRQ(VecGetArray(gv,&gptr));
  for (i = 0; i < high-low; i++) {
    if (xptr[i] < 0.0)               gptr[i] = gptr[i] - lambda;
    else if (xptr[i] > 0.0)          gptr[i] = gptr[i] + lambda;
    else if (gptr[i] + lambda < 0.0) gptr[i] = gptr[i] + lambda;
    else if (gptr[i] - lambda > 0.0) gptr[i] = gptr[i] - lambda;
    else                             gptr[i] = 0.0;
  }
  CHKERRQ(VecRestoreArray(gv,&gptr));
  CHKERRQ(VecRestoreArrayRead(x,&xptr));
  PetscFunctionReturn(0);
}

static PetscErrorCode TaoSolve_OWLQN(Tao tao)
{
  TAO_OWLQN                    *lmP = (TAO_OWLQN *)tao->data;
  PetscReal                    f, fold, gdx, gnorm;
  PetscReal                    step = 1.0;
  PetscReal                    delta;
  PetscInt                     stepType;
  PetscInt                     iter = 0;
  TaoLineSearchConvergedReason ls_status = TAOLINESEARCH_CONTINUE_ITERATING;

  PetscFunctionBegin;
  if (tao->XL || tao->XU || tao->ops->computebounds) {
    CHKERRQ(PetscInfo(tao,"WARNING: Variable bounds have been set but will be ignored by owlqn algorithm\n"));
  }

  /* Check convergence criteria */
  CHKERRQ(TaoComputeObjectiveAndGradient(tao, tao->solution, &f, tao->gradient));
  CHKERRQ(VecCopy(tao->gradient, lmP->GV));
  CHKERRQ(ComputePseudoGrad_OWLQN(tao->solution,lmP->GV,lmP->lambda));
  CHKERRQ(VecNorm(lmP->GV,NORM_2,&gnorm));
  PetscCheck(!PetscIsInfOrNanReal(f) && !PetscIsInfOrNanReal(gnorm),PetscObjectComm((PetscObject)tao),PETSC_ERR_USER, "User provided compute function generated Inf or NaN");

  tao->reason = TAO_CONTINUE_ITERATING;
  CHKERRQ(TaoLogConvergenceHistory(tao,f,gnorm,0.0,tao->ksp_its));
  CHKERRQ(TaoMonitor(tao,iter,f,gnorm,0.0,step));
  CHKERRQ((*tao->ops->convergencetest)(tao,tao->cnvP));
  if (tao->reason != TAO_CONTINUE_ITERATING) PetscFunctionReturn(0);

  /* Set initial scaling for the function */
  delta = 2.0 * PetscMax(1.0, PetscAbsScalar(f)) / (gnorm*gnorm);
  CHKERRQ(MatLMVMSetJ0Scale(lmP->M, delta));

  /* Set counter for gradient/reset steps */
  lmP->bfgs = 0;
  lmP->sgrad = 0;
  lmP->grad = 0;

  /* Have not converged; continue with Newton method */
  while (tao->reason == TAO_CONTINUE_ITERATING) {
    /* Call general purpose update function */
    if (tao->ops->update) {
      CHKERRQ((*tao->ops->update)(tao, tao->niter, tao->user_update));
    }

    /* Compute direction */
    CHKERRQ(MatLMVMUpdate(lmP->M,tao->solution,tao->gradient));
    CHKERRQ(MatSolve(lmP->M, lmP->GV, lmP->D));

    CHKERRQ(ProjDirect_OWLQN(lmP->D,lmP->GV));

    ++lmP->bfgs;

    /* Check for success (descent direction) */
    CHKERRQ(VecDot(lmP->D, lmP->GV , &gdx));
    if ((gdx <= 0.0) || PetscIsInfOrNanReal(gdx)) {

      /* Step is not descent or direction produced not a number
         We can assert bfgsUpdates > 1 in this case because
         the first solve produces the scaled gradient direction,
         which is guaranteed to be descent

         Use steepest descent direction (scaled) */
      ++lmP->grad;

      delta = 2.0 * PetscMax(1.0, PetscAbsScalar(f)) / (gnorm*gnorm);
      CHKERRQ(MatLMVMSetJ0Scale(lmP->M, delta));
      CHKERRQ(MatLMVMReset(lmP->M, PETSC_FALSE));
      CHKERRQ(MatLMVMUpdate(lmP->M, tao->solution, tao->gradient));
      CHKERRQ(MatSolve(lmP->M,lmP->GV, lmP->D));

      CHKERRQ(ProjDirect_OWLQN(lmP->D,lmP->GV));

      lmP->bfgs = 1;
      ++lmP->sgrad;
      stepType = OWLQN_SCALED_GRADIENT;
    } else {
      if (1 == lmP->bfgs) {
        /* The first BFGS direction is always the scaled gradient */
        ++lmP->sgrad;
        stepType = OWLQN_SCALED_GRADIENT;
      } else {
        ++lmP->bfgs;
        stepType = OWLQN_BFGS;
      }
    }

    CHKERRQ(VecScale(lmP->D, -1.0));

    /* Perform the linesearch */
    fold = f;
    CHKERRQ(VecCopy(tao->solution, lmP->Xold));
    CHKERRQ(VecCopy(tao->gradient, lmP->Gold));

    CHKERRQ(TaoLineSearchApply(tao->linesearch, tao->solution, &f, lmP->GV, lmP->D, &step,&ls_status));
    CHKERRQ(TaoAddLineSearchCounts(tao));

    while (((int)ls_status < 0) && (stepType != OWLQN_GRADIENT)) {

      /* Reset factors and use scaled gradient step */
      f = fold;
      CHKERRQ(VecCopy(lmP->Xold, tao->solution));
      CHKERRQ(VecCopy(lmP->Gold, tao->gradient));
      CHKERRQ(VecCopy(tao->gradient, lmP->GV));

      CHKERRQ(ComputePseudoGrad_OWLQN(tao->solution,lmP->GV,lmP->lambda));

      switch(stepType) {
      case OWLQN_BFGS:
        /* Failed to obtain acceptable iterate with BFGS step
           Attempt to use the scaled gradient direction */

        delta = 2.0 * PetscMax(1.0, PetscAbsScalar(f)) / (gnorm*gnorm);
        CHKERRQ(MatLMVMSetJ0Scale(lmP->M, delta));
        CHKERRQ(MatLMVMReset(lmP->M, PETSC_FALSE));
        CHKERRQ(MatLMVMUpdate(lmP->M, tao->solution, tao->gradient));
        CHKERRQ(MatSolve(lmP->M, lmP->GV, lmP->D));

        CHKERRQ(ProjDirect_OWLQN(lmP->D,lmP->GV));

        lmP->bfgs = 1;
        ++lmP->sgrad;
        stepType = OWLQN_SCALED_GRADIENT;
        break;

      case OWLQN_SCALED_GRADIENT:
        /* The scaled gradient step did not produce a new iterate;
           attempt to use the gradient direction.
           Need to make sure we are not using a different diagonal scaling */
        CHKERRQ(MatLMVMSetJ0Scale(lmP->M, 1.0));
        CHKERRQ(MatLMVMReset(lmP->M, PETSC_FALSE));
        CHKERRQ(MatLMVMUpdate(lmP->M, tao->solution, tao->gradient));
        CHKERRQ(MatSolve(lmP->M, lmP->GV, lmP->D));

        CHKERRQ(ProjDirect_OWLQN(lmP->D,lmP->GV));

        lmP->bfgs = 1;
        ++lmP->grad;
        stepType = OWLQN_GRADIENT;
        break;
      }
      CHKERRQ(VecScale(lmP->D, -1.0));

      /* Perform the linesearch */
      CHKERRQ(TaoLineSearchApply(tao->linesearch, tao->solution, &f, lmP->GV, lmP->D, &step, &ls_status));
      CHKERRQ(TaoAddLineSearchCounts(tao));
    }

    if ((int)ls_status < 0) {
      /* Failed to find an improving point*/
      f = fold;
      CHKERRQ(VecCopy(lmP->Xold, tao->solution));
      CHKERRQ(VecCopy(lmP->Gold, tao->gradient));
      CHKERRQ(VecCopy(tao->gradient, lmP->GV));
      step = 0.0;
    } else {
      /* a little hack here, because that gv is used to store g */
      CHKERRQ(VecCopy(lmP->GV, tao->gradient));
    }

    CHKERRQ(ComputePseudoGrad_OWLQN(tao->solution,lmP->GV,lmP->lambda));

    /* Check for termination */

    CHKERRQ(VecNorm(lmP->GV,NORM_2,&gnorm));

    iter++;
    CHKERRQ(TaoLogConvergenceHistory(tao,f,gnorm,0.0,tao->ksp_its));
    CHKERRQ(TaoMonitor(tao,iter,f,gnorm,0.0,step));
    CHKERRQ((*tao->ops->convergencetest)(tao,tao->cnvP));

    if ((int)ls_status < 0) break;
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode TaoSetUp_OWLQN(Tao tao)
{
  TAO_OWLQN      *lmP = (TAO_OWLQN *)tao->data;
  PetscInt       n,N;

  PetscFunctionBegin;
  /* Existence of tao->solution checked in TaoSetUp() */
  if (!tao->gradient) CHKERRQ(VecDuplicate(tao->solution,&tao->gradient));
  if (!tao->stepdirection) CHKERRQ(VecDuplicate(tao->solution,&tao->stepdirection));
  if (!lmP->D) CHKERRQ(VecDuplicate(tao->solution,&lmP->D));
  if (!lmP->GV) CHKERRQ(VecDuplicate(tao->solution,&lmP->GV));
  if (!lmP->Xold) CHKERRQ(VecDuplicate(tao->solution,&lmP->Xold));
  if (!lmP->Gold) CHKERRQ(VecDuplicate(tao->solution,&lmP->Gold));

  /* Create matrix for the limited memory approximation */
  CHKERRQ(VecGetLocalSize(tao->solution,&n));
  CHKERRQ(VecGetSize(tao->solution,&N));
  CHKERRQ(MatCreateLMVMBFGS(((PetscObject)tao)->comm,n,N,&lmP->M));
  CHKERRQ(MatLMVMAllocate(lmP->M,tao->solution,tao->gradient));
  PetscFunctionReturn(0);
}

/* ---------------------------------------------------------- */
static PetscErrorCode TaoDestroy_OWLQN(Tao tao)
{
  TAO_OWLQN      *lmP = (TAO_OWLQN *)tao->data;

  PetscFunctionBegin;
  if (tao->setupcalled) {
    CHKERRQ(VecDestroy(&lmP->Xold));
    CHKERRQ(VecDestroy(&lmP->Gold));
    CHKERRQ(VecDestroy(&lmP->D));
    CHKERRQ(MatDestroy(&lmP->M));
    CHKERRQ(VecDestroy(&lmP->GV));
  }
  CHKERRQ(PetscFree(tao->data));
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/
static PetscErrorCode TaoSetFromOptions_OWLQN(PetscOptionItems *PetscOptionsObject,Tao tao)
{
  TAO_OWLQN      *lmP = (TAO_OWLQN *)tao->data;

  PetscFunctionBegin;
  CHKERRQ(PetscOptionsHead(PetscOptionsObject,"Orthant-Wise Limited-memory method for Quasi-Newton unconstrained optimization"));
  CHKERRQ(PetscOptionsReal("-tao_owlqn_lambda", "regulariser weight","", 100,&lmP->lambda,NULL));
  CHKERRQ(PetscOptionsTail());
  CHKERRQ(TaoLineSearchSetFromOptions(tao->linesearch));
  PetscFunctionReturn(0);
}

/*------------------------------------------------------------*/
static PetscErrorCode TaoView_OWLQN(Tao tao, PetscViewer viewer)
{
  TAO_OWLQN      *lm = (TAO_OWLQN *)tao->data;
  PetscBool      isascii;

  PetscFunctionBegin;
  CHKERRQ(PetscObjectTypeCompare((PetscObject)viewer, PETSCVIEWERASCII, &isascii));
  if (isascii) {
    CHKERRQ(PetscViewerASCIIPushTab(viewer));
    CHKERRQ(PetscViewerASCIIPrintf(viewer, "BFGS steps: %D\n", lm->bfgs));
    CHKERRQ(PetscViewerASCIIPrintf(viewer, "Scaled gradient steps: %D\n", lm->sgrad));
    CHKERRQ(PetscViewerASCIIPrintf(viewer, "Gradient steps: %D\n", lm->grad));
    CHKERRQ(PetscViewerASCIIPopTab(viewer));
  }
  PetscFunctionReturn(0);
}

/* ---------------------------------------------------------- */
/*MC
  TAOOWLQN - orthant-wise limited memory quasi-newton algorithm

. - tao_owlqn_lambda - regulariser weight

  Level: beginner
M*/

PETSC_EXTERN PetscErrorCode TaoCreate_OWLQN(Tao tao)
{
  TAO_OWLQN      *lmP;
  const char     *owarmijo_type = TAOLINESEARCHOWARMIJO;

  PetscFunctionBegin;
  tao->ops->setup = TaoSetUp_OWLQN;
  tao->ops->solve = TaoSolve_OWLQN;
  tao->ops->view = TaoView_OWLQN;
  tao->ops->setfromoptions = TaoSetFromOptions_OWLQN;
  tao->ops->destroy = TaoDestroy_OWLQN;

  CHKERRQ(PetscNewLog(tao,&lmP));
  lmP->D = NULL;
  lmP->M = NULL;
  lmP->GV = NULL;
  lmP->Xold = NULL;
  lmP->Gold = NULL;
  lmP->lambda = 1.0;

  tao->data = (void*)lmP;
  /* Override default settings (unless already changed) */
  if (!tao->max_it_changed) tao->max_it = 2000;
  if (!tao->max_funcs_changed) tao->max_funcs = 4000;

  CHKERRQ(TaoLineSearchCreate(((PetscObject)tao)->comm,&tao->linesearch));
  CHKERRQ(PetscObjectIncrementTabLevel((PetscObject)tao->linesearch, (PetscObject)tao, 1));
  CHKERRQ(TaoLineSearchSetType(tao->linesearch,owarmijo_type));
  CHKERRQ(TaoLineSearchUseTaoRoutines(tao->linesearch,tao));
  CHKERRQ(TaoLineSearchSetOptionsPrefix(tao->linesearch,tao->hdr.prefix));
  PetscFunctionReturn(0);
}
