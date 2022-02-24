
#include <../src/ksp/pc/impls/factor/icc/icc.h>   /*I "petscpc.h" I*/

static PetscErrorCode PCSetUp_ICC(PC pc)
{
  PC_ICC                 *icc = (PC_ICC*)pc->data;
  IS                     perm = NULL,cperm = NULL;
  MatInfo                info;
  MatSolverType          stype;
  MatFactorError         err;
  PetscBool              canuseordering;

  PetscFunctionBegin;
  pc->failedreason = PC_NOERROR;

  CHKERRQ(MatSetErrorIfFailure(pc->pmat,pc->erroriffailure));
  if (!pc->setupcalled) {
    if (!((PC_Factor*)icc)->fact) {
      CHKERRQ(MatGetFactor(pc->pmat,((PC_Factor*)icc)->solvertype,MAT_FACTOR_ICC,&((PC_Factor*)icc)->fact));
    }
    CHKERRQ(MatFactorGetCanUseOrdering(((PC_Factor*)icc)->fact,&canuseordering));
    if (canuseordering) {
      CHKERRQ(PCFactorSetDefaultOrdering_Factor(pc));
      CHKERRQ(MatGetOrdering(pc->pmat, ((PC_Factor*)icc)->ordering,&perm,&cperm));
    }
    CHKERRQ(MatICCFactorSymbolic(((PC_Factor*)icc)->fact,pc->pmat,perm,&((PC_Factor*)icc)->info));
  } else if (pc->flag != SAME_NONZERO_PATTERN) {
    PetscBool canuseordering;
    CHKERRQ(MatDestroy(&((PC_Factor*)icc)->fact));
    CHKERRQ(MatGetFactor(pc->pmat,((PC_Factor*)icc)->solvertype,MAT_FACTOR_ICC,&((PC_Factor*)icc)->fact));
    CHKERRQ(MatFactorGetCanUseOrdering(((PC_Factor*)icc)->fact,&canuseordering));
    if (canuseordering) {
      CHKERRQ(PCFactorSetDefaultOrdering_Factor(pc));
      CHKERRQ(MatGetOrdering(pc->pmat, ((PC_Factor*)icc)->ordering,&perm,&cperm));
    }
    CHKERRQ(MatICCFactorSymbolic(((PC_Factor*)icc)->fact,pc->pmat,perm,&((PC_Factor*)icc)->info));
  }
  CHKERRQ(MatGetInfo(((PC_Factor*)icc)->fact,MAT_LOCAL,&info));
  icc->hdr.actualfill = info.fill_ratio_needed;

  CHKERRQ(ISDestroy(&cperm));
  CHKERRQ(ISDestroy(&perm));

  CHKERRQ(MatFactorGetError(((PC_Factor*)icc)->fact,&err));
  if (err) { /* FactorSymbolic() fails */
    pc->failedreason = (PCFailedReason)err;
    PetscFunctionReturn(0);
  }

  CHKERRQ(MatCholeskyFactorNumeric(((PC_Factor*)icc)->fact,pc->pmat,&((PC_Factor*)icc)->info));
  CHKERRQ(MatFactorGetError(((PC_Factor*)icc)->fact,&err));
  if (err) { /* FactorNumeric() fails */
    pc->failedreason = (PCFailedReason)err;
  }

  CHKERRQ(PCFactorGetMatSolverType(pc,&stype));
  if (!stype) {
    MatSolverType solverpackage;
    CHKERRQ(MatFactorGetSolverType(((PC_Factor*)icc)->fact,&solverpackage));
    CHKERRQ(PCFactorSetMatSolverType(pc,solverpackage));
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode PCReset_ICC(PC pc)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(MatDestroy(&((PC_Factor*)icc)->fact));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCDestroy_ICC(PC pc)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(PCReset_ICC(pc));
  CHKERRQ(PetscFree(((PC_Factor*)icc)->ordering));
  CHKERRQ(PetscFree(((PC_Factor*)icc)->solvertype));
  CHKERRQ(PetscFree(pc->data));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCApply_ICC(PC pc,Vec x,Vec y)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(MatSolve(((PC_Factor*)icc)->fact,x,y));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCMatApply_ICC(PC pc,Mat X,Mat Y)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(MatMatSolve(((PC_Factor*)icc)->fact,X,Y));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCApplySymmetricLeft_ICC(PC pc,Vec x,Vec y)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(MatForwardSolve(((PC_Factor*)icc)->fact,x,y));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCApplySymmetricRight_ICC(PC pc,Vec x,Vec y)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(MatBackwardSolve(((PC_Factor*)icc)->fact,x,y));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCSetFromOptions_ICC(PetscOptionItems *PetscOptionsObject,PC pc)
{
  PC_ICC         *icc = (PC_ICC*)pc->data;
  PetscBool      flg;
  /* PetscReal      dt[3];*/

  PetscFunctionBegin;
  CHKERRQ(PetscOptionsHead(PetscOptionsObject,"ICC Options"));
  CHKERRQ(PCSetFromOptions_Factor(PetscOptionsObject,pc));

  CHKERRQ(PetscOptionsReal("-pc_factor_levels","levels of fill","PCFactorSetLevels",((PC_Factor*)icc)->info.levels,&((PC_Factor*)icc)->info.levels,&flg));
  /*dt[0] = ((PC_Factor*)icc)->info.dt;
  dt[1] = ((PC_Factor*)icc)->info.dtcol;
  dt[2] = ((PC_Factor*)icc)->info.dtcount;
  PetscInt       dtmax = 3;
  CHKERRQ(PetscOptionsRealArray("-pc_factor_drop_tolerance","<dt,dtcol,maxrowcount>","PCFactorSetDropTolerance",dt,&dtmax,&flg));
  if (flg) {
    CHKERRQ(PCFactorSetDropTolerance(pc,dt[0],dt[1],(PetscInt)dt[2]));
  }
  */
  CHKERRQ(PetscOptionsTail());
  PetscFunctionReturn(0);
}

extern PetscErrorCode  PCFactorSetDropTolerance_ILU(PC,PetscReal,PetscReal,PetscInt);

/*MC
     PCICC - Incomplete Cholesky factorization preconditioners.

   Options Database Keys:
+  -pc_factor_levels <k> - number of levels of fill for ICC(k)
.  -pc_factor_in_place - only for ICC(0) with natural ordering, reuses the space of the matrix for
                      its factorization (overwrites original matrix)
.  -pc_factor_fill <nfill> - expected amount of fill in factored matrix compared to original matrix, nfill > 1
-  -pc_factor_mat_ordering_type <natural,nd,1wd,rcm,qmd> - set the row/column ordering of the factored matrix

   Level: beginner

   Notes:
    Only implemented for some matrix formats. Not implemented in parallel.

          For BAIJ matrices this implements a point block ICC.

          The Manteuffel shift is only implemented for matrices with block size 1

          By default, the Manteuffel is applied (for matrices with block size 1). Call PCFactorSetShiftType(pc,MAT_SHIFT_POSITIVE_DEFINITE);
          to turn off the shift.

   References:
.  * - TONY F. CHAN AND HENK A. VAN DER VORST, Review article: APPROXIMATE AND INCOMPLETE FACTORIZATIONS,
      Chapter in Parallel Numerical Algorithms, edited by D. Keyes, A. Semah, V. Venkatakrishnan, ICASE/LaRC Interdisciplinary Series in
      Science and Engineering, Kluwer.

.seealso:  PCCreate(), PCSetType(), PCType (for list of available types), PC, PCSOR, MatOrderingType,
           PCFactorSetZeroPivot(), PCFactorSetShiftType(), PCFactorSetShiftAmount(),
           PCFactorSetFill(), PCFactorSetMatOrderingType(), PCFactorSetReuseOrdering(),
           PCFactorSetLevels()

M*/

PETSC_EXTERN PetscErrorCode PCCreate_ICC(PC pc)
{
  PC_ICC         *icc;

  PetscFunctionBegin;
  CHKERRQ(PetscNewLog(pc,&icc));
  pc->data = (void*)icc;
  CHKERRQ(PCFactorInitialize(pc, MAT_FACTOR_ICC));

  ((PC_Factor*)icc)->info.fill      = 1.0;
  ((PC_Factor*)icc)->info.dtcol     = PETSC_DEFAULT;
  ((PC_Factor*)icc)->info.shifttype = (PetscReal) MAT_SHIFT_POSITIVE_DEFINITE;

  pc->ops->apply               = PCApply_ICC;
  pc->ops->matapply            = PCMatApply_ICC;
  pc->ops->applytranspose      = PCApply_ICC;
  pc->ops->setup               = PCSetUp_ICC;
  pc->ops->reset               = PCReset_ICC;
  pc->ops->destroy             = PCDestroy_ICC;
  pc->ops->setfromoptions      = PCSetFromOptions_ICC;
  pc->ops->view                = PCView_Factor;
  pc->ops->applysymmetricleft  = PCApplySymmetricLeft_ICC;
  pc->ops->applysymmetricright = PCApplySymmetricRight_ICC;
  PetscFunctionReturn(0);
}
