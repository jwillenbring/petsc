
/*
  This file defines a "solve the problem redundantly on each subgroup of processor" preconditioner.
*/
#include <petsc/private/pcimpl.h>
#include <petscksp.h>           /*I "petscksp.h" I*/

typedef struct {
  KSP                ksp;
  PC                 pc;                   /* actual preconditioner used on each processor */
  Vec                xsub,ysub;            /* vectors of a subcommunicator to hold parallel vectors of PetscObjectComm((PetscObject)pc) */
  Vec                xdup,ydup;            /* parallel vector that congregates xsub or ysub facilitating vector scattering */
  Mat                pmats;                /* matrix and optional preconditioner matrix belong to a subcommunicator */
  VecScatter         scatterin,scatterout; /* scatter used to move all values to each processor group (subcommunicator) */
  PetscBool          useparallelmat;
  PetscSubcomm       psubcomm;
  PetscInt           nsubcomm;             /* num of data structure PetscSubcomm */
  PetscBool          shifttypeset;
  MatFactorShiftType shifttype;
} PC_Redundant;

PetscErrorCode  PCFactorSetShiftType_Redundant(PC pc,MatFactorShiftType shifttype)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  if (red->ksp) {
    PC pc;
    CHKERRQ(KSPGetPC(red->ksp,&pc));
    CHKERRQ(PCFactorSetShiftType(pc,shifttype));
  } else {
    red->shifttypeset = PETSC_TRUE;
    red->shifttype    = shifttype;
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode PCView_Redundant(PC pc,PetscViewer viewer)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;
  PetscBool      iascii,isstring;
  PetscViewer    subviewer;

  PetscFunctionBegin;
  CHKERRQ(PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii));
  CHKERRQ(PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERSTRING,&isstring));
  if (iascii) {
    if (!red->psubcomm) {
      CHKERRQ(PetscViewerASCIIPrintf(viewer,"  Not yet setup\n"));
    } else {
      CHKERRQ(PetscViewerASCIIPrintf(viewer,"  First (color=0) of %D PCs follows\n",red->nsubcomm));
      CHKERRQ(PetscViewerGetSubViewer(viewer,((PetscObject)red->pc)->comm,&subviewer));
      if (!red->psubcomm->color) { /* only view first redundant pc */
        CHKERRQ(PetscViewerASCIIPushTab(subviewer));
        CHKERRQ(KSPView(red->ksp,subviewer));
        CHKERRQ(PetscViewerASCIIPopTab(subviewer));
      }
      CHKERRQ(PetscViewerRestoreSubViewer(viewer,((PetscObject)red->pc)->comm,&subviewer));
    }
  } else if (isstring) {
    CHKERRQ(PetscViewerStringSPrintf(viewer," Redundant solver preconditioner"));
  }
  PetscFunctionReturn(0);
}

#include <../src/mat/impls/aij/mpi/mpiaij.h>
static PetscErrorCode PCSetUp_Redundant(PC pc)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;
  PetscInt       mstart,mend,mlocal,M;
  PetscMPIInt    size;
  MPI_Comm       comm,subcomm;
  Vec            x;

  PetscFunctionBegin;
  CHKERRQ(PetscObjectGetComm((PetscObject)pc,&comm));

  /* if pmatrix set by user is sequential then we do not need to gather the parallel matrix */
  CHKERRMPI(MPI_Comm_size(comm,&size));
  if (size == 1) red->useparallelmat = PETSC_FALSE;

  if (!pc->setupcalled) {
    PetscInt mloc_sub;
    if (!red->psubcomm) { /* create red->psubcomm, new ksp and pc over subcomm */
      KSP ksp;
      CHKERRQ(PCRedundantGetKSP(pc,&ksp));
    }
    subcomm = PetscSubcommChild(red->psubcomm);

    if (red->useparallelmat) {
      /* grab the parallel matrix and put it into processors of a subcomminicator */
      CHKERRQ(MatCreateRedundantMatrix(pc->pmat,red->psubcomm->n,subcomm,MAT_INITIAL_MATRIX,&red->pmats));

      CHKERRMPI(MPI_Comm_size(subcomm,&size));
      if (size > 1) {
        PetscBool foundpack,issbaij;
        CHKERRQ(PetscObjectTypeCompare((PetscObject)red->pmats,MATMPISBAIJ,&issbaij));
        if (!issbaij) {
          CHKERRQ(MatGetFactorAvailable(red->pmats,NULL,MAT_FACTOR_LU,&foundpack));
        } else {
          CHKERRQ(MatGetFactorAvailable(red->pmats,NULL,MAT_FACTOR_CHOLESKY,&foundpack));
        }
        if (!foundpack) { /* reset default ksp and pc */
          CHKERRQ(KSPSetType(red->ksp,KSPGMRES));
          CHKERRQ(PCSetType(red->pc,PCBJACOBI));
        } else {
          CHKERRQ(PCFactorSetMatSolverType(red->pc,NULL));
        }
      }

      CHKERRQ(KSPSetOperators(red->ksp,red->pmats,red->pmats));

      /* get working vectors xsub and ysub */
      CHKERRQ(MatCreateVecs(red->pmats,&red->xsub,&red->ysub));

      /* create working vectors xdup and ydup.
       xdup concatenates all xsub's contigously to form a mpi vector over dupcomm  (see PetscSubcommCreate_interlaced())
       ydup concatenates all ysub and has empty local arrays because ysub's arrays will be place into it.
       Note: we use communicator dupcomm, not PetscObjectComm((PetscObject)pc)! */
      CHKERRQ(MatGetLocalSize(red->pmats,&mloc_sub,NULL));
      CHKERRQ(VecCreateMPI(PetscSubcommContiguousParent(red->psubcomm),mloc_sub,PETSC_DECIDE,&red->xdup));
      CHKERRQ(VecCreateMPIWithArray(PetscSubcommContiguousParent(red->psubcomm),1,mloc_sub,PETSC_DECIDE,NULL,&red->ydup));

      /* create vecscatters */
      if (!red->scatterin) { /* efficiency of scatterin is independent from psubcomm_type! */
        IS       is1,is2;
        PetscInt *idx1,*idx2,i,j,k;

        CHKERRQ(MatCreateVecs(pc->pmat,&x,NULL));
        CHKERRQ(VecGetSize(x,&M));
        CHKERRQ(VecGetOwnershipRange(x,&mstart,&mend));
        mlocal = mend - mstart;
        CHKERRQ(PetscMalloc2(red->psubcomm->n*mlocal,&idx1,red->psubcomm->n*mlocal,&idx2));
        j    = 0;
        for (k=0; k<red->psubcomm->n; k++) {
          for (i=mstart; i<mend; i++) {
            idx1[j]   = i;
            idx2[j++] = i + M*k;
          }
        }
        CHKERRQ(ISCreateGeneral(comm,red->psubcomm->n*mlocal,idx1,PETSC_COPY_VALUES,&is1));
        CHKERRQ(ISCreateGeneral(comm,red->psubcomm->n*mlocal,idx2,PETSC_COPY_VALUES,&is2));
        CHKERRQ(VecScatterCreate(x,is1,red->xdup,is2,&red->scatterin));
        CHKERRQ(ISDestroy(&is1));
        CHKERRQ(ISDestroy(&is2));

        /* Impl below is good for PETSC_SUBCOMM_INTERLACED (no inter-process communication) and PETSC_SUBCOMM_CONTIGUOUS (communication within subcomm) */
        CHKERRQ(ISCreateStride(comm,mlocal,mstart+ red->psubcomm->color*M,1,&is1));
        CHKERRQ(ISCreateStride(comm,mlocal,mstart,1,&is2));
        CHKERRQ(VecScatterCreate(red->xdup,is1,x,is2,&red->scatterout));
        CHKERRQ(ISDestroy(&is1));
        CHKERRQ(ISDestroy(&is2));
        CHKERRQ(PetscFree2(idx1,idx2));
        CHKERRQ(VecDestroy(&x));
      }
    } else { /* !red->useparallelmat */
      CHKERRQ(KSPSetOperators(red->ksp,pc->mat,pc->pmat));
    }
  } else { /* pc->setupcalled */
    if (red->useparallelmat) {
      MatReuse       reuse;
      /* grab the parallel matrix and put it into processors of a subcomminicator */
      /*--------------------------------------------------------------------------*/
      if (pc->flag == DIFFERENT_NONZERO_PATTERN) {
        /* destroy old matrices */
        CHKERRQ(MatDestroy(&red->pmats));
        reuse = MAT_INITIAL_MATRIX;
      } else {
        reuse = MAT_REUSE_MATRIX;
      }
      CHKERRQ(MatCreateRedundantMatrix(pc->pmat,red->psubcomm->n,PetscSubcommChild(red->psubcomm),reuse,&red->pmats));
      CHKERRQ(KSPSetOperators(red->ksp,red->pmats,red->pmats));
    } else { /* !red->useparallelmat */
      CHKERRQ(KSPSetOperators(red->ksp,pc->mat,pc->pmat));
    }
  }

  if (pc->setfromoptionscalled) {
    CHKERRQ(KSPSetFromOptions(red->ksp));
  }
  CHKERRQ(KSPSetUp(red->ksp));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCApply_Redundant(PC pc,Vec x,Vec y)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;
  PetscScalar    *array;

  PetscFunctionBegin;
  if (!red->useparallelmat) {
    CHKERRQ(KSPSolve(red->ksp,x,y));
    CHKERRQ(KSPCheckSolve(red->ksp,pc,y));
    PetscFunctionReturn(0);
  }

  /* scatter x to xdup */
  CHKERRQ(VecScatterBegin(red->scatterin,x,red->xdup,INSERT_VALUES,SCATTER_FORWARD));
  CHKERRQ(VecScatterEnd(red->scatterin,x,red->xdup,INSERT_VALUES,SCATTER_FORWARD));

  /* place xdup's local array into xsub */
  CHKERRQ(VecGetArray(red->xdup,&array));
  CHKERRQ(VecPlaceArray(red->xsub,(const PetscScalar*)array));

  /* apply preconditioner on each processor */
  CHKERRQ(KSPSolve(red->ksp,red->xsub,red->ysub));
  CHKERRQ(KSPCheckSolve(red->ksp,pc,red->ysub));
  CHKERRQ(VecResetArray(red->xsub));
  CHKERRQ(VecRestoreArray(red->xdup,&array));

  /* place ysub's local array into ydup */
  CHKERRQ(VecGetArray(red->ysub,&array));
  CHKERRQ(VecPlaceArray(red->ydup,(const PetscScalar*)array));

  /* scatter ydup to y */
  CHKERRQ(VecScatterBegin(red->scatterout,red->ydup,y,INSERT_VALUES,SCATTER_FORWARD));
  CHKERRQ(VecScatterEnd(red->scatterout,red->ydup,y,INSERT_VALUES,SCATTER_FORWARD));
  CHKERRQ(VecResetArray(red->ydup));
  CHKERRQ(VecRestoreArray(red->ysub,&array));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCApplyTranspose_Redundant(PC pc,Vec x,Vec y)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;
  PetscScalar    *array;

  PetscFunctionBegin;
  if (!red->useparallelmat) {
    CHKERRQ(KSPSolveTranspose(red->ksp,x,y));
    CHKERRQ(KSPCheckSolve(red->ksp,pc,y));
    PetscFunctionReturn(0);
  }

  /* scatter x to xdup */
  CHKERRQ(VecScatterBegin(red->scatterin,x,red->xdup,INSERT_VALUES,SCATTER_FORWARD));
  CHKERRQ(VecScatterEnd(red->scatterin,x,red->xdup,INSERT_VALUES,SCATTER_FORWARD));

  /* place xdup's local array into xsub */
  CHKERRQ(VecGetArray(red->xdup,&array));
  CHKERRQ(VecPlaceArray(red->xsub,(const PetscScalar*)array));

  /* apply preconditioner on each processor */
  CHKERRQ(KSPSolveTranspose(red->ksp,red->xsub,red->ysub));
  CHKERRQ(KSPCheckSolve(red->ksp,pc,red->ysub));
  CHKERRQ(VecResetArray(red->xsub));
  CHKERRQ(VecRestoreArray(red->xdup,&array));

  /* place ysub's local array into ydup */
  CHKERRQ(VecGetArray(red->ysub,&array));
  CHKERRQ(VecPlaceArray(red->ydup,(const PetscScalar*)array));

  /* scatter ydup to y */
  CHKERRQ(VecScatterBegin(red->scatterout,red->ydup,y,INSERT_VALUES,SCATTER_FORWARD));
  CHKERRQ(VecScatterEnd(red->scatterout,red->ydup,y,INSERT_VALUES,SCATTER_FORWARD));
  CHKERRQ(VecResetArray(red->ydup));
  CHKERRQ(VecRestoreArray(red->ysub,&array));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCReset_Redundant(PC pc)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  if (red->useparallelmat) {
    CHKERRQ(VecScatterDestroy(&red->scatterin));
    CHKERRQ(VecScatterDestroy(&red->scatterout));
    CHKERRQ(VecDestroy(&red->ysub));
    CHKERRQ(VecDestroy(&red->xsub));
    CHKERRQ(VecDestroy(&red->xdup));
    CHKERRQ(VecDestroy(&red->ydup));
  }
  CHKERRQ(MatDestroy(&red->pmats));
  CHKERRQ(KSPReset(red->ksp));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCDestroy_Redundant(PC pc)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(PCReset_Redundant(pc));
  CHKERRQ(KSPDestroy(&red->ksp));
  CHKERRQ(PetscSubcommDestroy(&red->psubcomm));
  CHKERRQ(PetscFree(pc->data));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCSetFromOptions_Redundant(PetscOptionItems *PetscOptionsObject,PC pc)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(PetscOptionsHead(PetscOptionsObject,"Redundant options"));
  CHKERRQ(PetscOptionsInt("-pc_redundant_number","Number of redundant pc","PCRedundantSetNumber",red->nsubcomm,&red->nsubcomm,NULL));
  CHKERRQ(PetscOptionsTail());
  PetscFunctionReturn(0);
}

static PetscErrorCode PCRedundantSetNumber_Redundant(PC pc,PetscInt nreds)
{
  PC_Redundant *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  red->nsubcomm = nreds;
  PetscFunctionReturn(0);
}

/*@
   PCRedundantSetNumber - Sets the number of redundant preconditioner contexts.

   Logically Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  nredundant - number of redundant preconditioner contexts; for example if you are using 64 MPI processes and
                              use an nredundant of 4 there will be 4 parallel solves each on 16 = 64/4 processes.

   Level: advanced

@*/
PetscErrorCode PCRedundantSetNumber(PC pc,PetscInt nredundant)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  PetscCheckFalse(nredundant <= 0,PetscObjectComm((PetscObject)pc),PETSC_ERR_ARG_WRONG, "num of redundant pc %D must be positive",nredundant);
  CHKERRQ(PetscTryMethod(pc,"PCRedundantSetNumber_C",(PC,PetscInt),(pc,nredundant)));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCRedundantSetScatter_Redundant(PC pc,VecScatter in,VecScatter out)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  CHKERRQ(PetscObjectReference((PetscObject)in));
  CHKERRQ(VecScatterDestroy(&red->scatterin));

  red->scatterin  = in;

  CHKERRQ(PetscObjectReference((PetscObject)out));
  CHKERRQ(VecScatterDestroy(&red->scatterout));
  red->scatterout = out;
  PetscFunctionReturn(0);
}

/*@
   PCRedundantSetScatter - Sets the scatter used to copy values into the
     redundant local solve and the scatter to move them back into the global
     vector.

   Logically Collective on PC

   Input Parameters:
+  pc - the preconditioner context
.  in - the scatter to move the values in
-  out - the scatter to move them out

   Level: advanced

@*/
PetscErrorCode PCRedundantSetScatter(PC pc,VecScatter in,VecScatter out)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  PetscValidHeaderSpecific(in,PETSCSF_CLASSID,2);
  PetscValidHeaderSpecific(out,PETSCSF_CLASSID,3);
  CHKERRQ(PetscTryMethod(pc,"PCRedundantSetScatter_C",(PC,VecScatter,VecScatter),(pc,in,out)));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCRedundantGetKSP_Redundant(PC pc,KSP *innerksp)
{
  PC_Redundant   *red = (PC_Redundant*)pc->data;
  MPI_Comm       comm,subcomm;
  const char     *prefix;
  PetscBool      issbaij;

  PetscFunctionBegin;
  if (!red->psubcomm) {
    CHKERRQ(PCGetOptionsPrefix(pc,&prefix));

    CHKERRQ(PetscObjectGetComm((PetscObject)pc,&comm));
    CHKERRQ(PetscSubcommCreate(comm,&red->psubcomm));
    CHKERRQ(PetscSubcommSetNumber(red->psubcomm,red->nsubcomm));
    CHKERRQ(PetscSubcommSetType(red->psubcomm,PETSC_SUBCOMM_CONTIGUOUS));

    CHKERRQ(PetscSubcommSetOptionsPrefix(red->psubcomm,prefix));
    CHKERRQ(PetscSubcommSetFromOptions(red->psubcomm));
    CHKERRQ(PetscLogObjectMemory((PetscObject)pc,sizeof(PetscSubcomm)));

    /* create a new PC that processors in each subcomm have copy of */
    subcomm = PetscSubcommChild(red->psubcomm);

    CHKERRQ(KSPCreate(subcomm,&red->ksp));
    CHKERRQ(KSPSetErrorIfNotConverged(red->ksp,pc->erroriffailure));
    CHKERRQ(PetscObjectIncrementTabLevel((PetscObject)red->ksp,(PetscObject)pc,1));
    CHKERRQ(PetscLogObjectParent((PetscObject)pc,(PetscObject)red->ksp));
    CHKERRQ(KSPSetType(red->ksp,KSPPREONLY));
    CHKERRQ(KSPGetPC(red->ksp,&red->pc));
    CHKERRQ(PetscObjectTypeCompare((PetscObject)pc->pmat,MATSEQSBAIJ,&issbaij));
    if (!issbaij) {
      CHKERRQ(PetscObjectTypeCompare((PetscObject)pc->pmat,MATMPISBAIJ,&issbaij));
    }
    if (!issbaij) {
      CHKERRQ(PCSetType(red->pc,PCLU));
    } else {
      CHKERRQ(PCSetType(red->pc,PCCHOLESKY));
    }
    if (red->shifttypeset) {
      CHKERRQ(PCFactorSetShiftType(red->pc,red->shifttype));
      red->shifttypeset = PETSC_FALSE;
    }
    CHKERRQ(KSPSetOptionsPrefix(red->ksp,prefix));
    CHKERRQ(KSPAppendOptionsPrefix(red->ksp,"redundant_"));
  }
  *innerksp = red->ksp;
  PetscFunctionReturn(0);
}

/*@
   PCRedundantGetKSP - Gets the less parallel KSP created by the redundant PC.

   Not Collective

   Input Parameter:
.  pc - the preconditioner context

   Output Parameter:
.  innerksp - the KSP on the smaller set of processes

   Level: advanced

@*/
PetscErrorCode PCRedundantGetKSP(PC pc,KSP *innerksp)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  PetscValidPointer(innerksp,2);
  CHKERRQ(PetscUseMethod(pc,"PCRedundantGetKSP_C",(PC,KSP*),(pc,innerksp)));
  PetscFunctionReturn(0);
}

static PetscErrorCode PCRedundantGetOperators_Redundant(PC pc,Mat *mat,Mat *pmat)
{
  PC_Redundant *red = (PC_Redundant*)pc->data;

  PetscFunctionBegin;
  if (mat)  *mat  = red->pmats;
  if (pmat) *pmat = red->pmats;
  PetscFunctionReturn(0);
}

/*@
   PCRedundantGetOperators - gets the sequential matrix and preconditioner matrix

   Not Collective

   Input Parameter:
.  pc - the preconditioner context

   Output Parameters:
+  mat - the matrix
-  pmat - the (possibly different) preconditioner matrix

   Level: advanced

@*/
PetscErrorCode PCRedundantGetOperators(PC pc,Mat *mat,Mat *pmat)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  if (mat)  PetscValidPointer(mat,2);
  if (pmat) PetscValidPointer(pmat,3);
  CHKERRQ(PetscUseMethod(pc,"PCRedundantGetOperators_C",(PC,Mat*,Mat*),(pc,mat,pmat)));
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------------------*/
/*MC
     PCREDUNDANT - Runs a KSP solver with preconditioner for the entire problem on subgroups of processors

     Options for the redundant preconditioners can be set with -redundant_pc_xxx for the redundant KSP with -redundant_ksp_xxx

  Options Database:
.  -pc_redundant_number <n> - number of redundant solves, for example if you are using 64 MPI processes and
                              use an n of 4 there will be 4 parallel solves each on 16 = 64/4 processes.

   Level: intermediate

   Notes:
    The default KSP is preonly and the default PC is LU or CHOLESKY if Pmat is of type MATSBAIJ.

   PCFactorSetShiftType() applied to this PC will convey they shift type into the inner PC if it is factorization based.

   Developer Notes:
    Note that PCSetInitialGuessNonzero()  is not used by this class but likely should be.

.seealso:  PCCreate(), PCSetType(), PCType (for list of available types), PCRedundantSetScatter(),
           PCRedundantGetKSP(), PCRedundantGetOperators(), PCRedundantSetNumber()
M*/

PETSC_EXTERN PetscErrorCode PCCreate_Redundant(PC pc)
{
  PC_Redundant   *red;
  PetscMPIInt    size;

  PetscFunctionBegin;
  CHKERRQ(PetscNewLog(pc,&red));
  CHKERRMPI(MPI_Comm_size(PetscObjectComm((PetscObject)pc),&size));

  red->nsubcomm       = size;
  red->useparallelmat = PETSC_TRUE;
  pc->data            = (void*)red;

  pc->ops->apply          = PCApply_Redundant;
  pc->ops->applytranspose = PCApplyTranspose_Redundant;
  pc->ops->setup          = PCSetUp_Redundant;
  pc->ops->destroy        = PCDestroy_Redundant;
  pc->ops->reset          = PCReset_Redundant;
  pc->ops->setfromoptions = PCSetFromOptions_Redundant;
  pc->ops->view           = PCView_Redundant;

  CHKERRQ(PetscObjectComposeFunction((PetscObject)pc,"PCRedundantSetScatter_C",PCRedundantSetScatter_Redundant));
  CHKERRQ(PetscObjectComposeFunction((PetscObject)pc,"PCRedundantSetNumber_C",PCRedundantSetNumber_Redundant));
  CHKERRQ(PetscObjectComposeFunction((PetscObject)pc,"PCRedundantGetKSP_C",PCRedundantGetKSP_Redundant));
  CHKERRQ(PetscObjectComposeFunction((PetscObject)pc,"PCRedundantGetOperators_C",PCRedundantGetOperators_Redundant));
  CHKERRQ(PetscObjectComposeFunction((PetscObject)pc,"PCFactorSetShiftType_C",PCFactorSetShiftType_Redundant));
  PetscFunctionReturn(0);
}
