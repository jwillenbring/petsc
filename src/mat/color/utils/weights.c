#include <petsc/private/matimpl.h>      /*I "petscmat.h"  I*/
#include <../src/mat/impls/aij/seq/aij.h>

PetscErrorCode MatColoringCreateLexicalWeights(MatColoring mc,PetscReal *weights)
{
  PetscInt       i,s,e;
  Mat            G=mc->mat;

  PetscFunctionBegin;
  CHKERRQ(MatGetOwnershipRange(G,&s,&e));
  for (i=s;i<e;i++) {
    weights[i-s] = i;
  }
  PetscFunctionReturn(0);
}

PetscErrorCode MatColoringCreateRandomWeights(MatColoring mc,PetscReal *weights)
{
  PetscInt       i,s,e;
  PetscRandom    rand;
  PetscReal      r;
  Mat            G = mc->mat;

  PetscFunctionBegin;
  /* each weight should be the degree plus a random perturbation */
  CHKERRQ(PetscRandomCreate(PetscObjectComm((PetscObject)mc),&rand));
  CHKERRQ(PetscRandomSetFromOptions(rand));
  CHKERRQ(MatGetOwnershipRange(G,&s,&e));
  for (i=s;i<e;i++) {
    CHKERRQ(PetscRandomGetValueReal(rand,&r));
    weights[i-s] = PetscAbsReal(r);
  }
  CHKERRQ(PetscRandomDestroy(&rand));
  PetscFunctionReturn(0);
}

PetscErrorCode MatColoringGetDegrees(Mat G,PetscInt distance,PetscInt *degrees)
{
  PetscInt       j,i,s,e,n,ln,lm,degree,bidx,idx,dist;
  Mat            lG,*lGs;
  IS             ris;
  PetscInt       *seen;
  const PetscInt *gidx;
  PetscInt       *idxbuf;
  PetscInt       *distbuf;
  PetscInt       ncols;
  const PetscInt *cols;
  PetscBool      isSEQAIJ;
  Mat_SeqAIJ     *aij;
  PetscInt       *Gi,*Gj;

  PetscFunctionBegin;
  CHKERRQ(MatGetOwnershipRange(G,&s,&e));
  n=e-s;
  CHKERRQ(ISCreateStride(PetscObjectComm((PetscObject)G),n,s,1,&ris));
  CHKERRQ(MatIncreaseOverlap(G,1,&ris,distance));
  CHKERRQ(ISSort(ris));
  CHKERRQ(MatCreateSubMatrices(G,1,&ris,&ris,MAT_INITIAL_MATRIX,&lGs));
  lG = lGs[0];
  CHKERRQ(PetscObjectBaseTypeCompare((PetscObject)lG,MATSEQAIJ,&isSEQAIJ));
  PetscCheckFalse(!isSEQAIJ,PetscObjectComm((PetscObject)G),PETSC_ERR_SUP,"Requires an MPI/SEQAIJ Matrix");
  CHKERRQ(MatGetSize(lG,&ln,&lm));
  aij = (Mat_SeqAIJ*)lG->data;
  Gi = aij->i;
  Gj = aij->j;
  CHKERRQ(PetscMalloc3(lm,&seen,lm,&idxbuf,lm,&distbuf));
  for (i=0;i<ln;i++) {
    seen[i]=-1;
  }
  CHKERRQ(ISGetIndices(ris,&gidx));
  for (i=0;i<ln;i++) {
    if (gidx[i] >= e || gidx[i] < s) continue;
    bidx=-1;
    ncols = Gi[i+1]-Gi[i];
    cols = &(Gj[Gi[i]]);
    degree = 0;
    /* place the distance-one neighbors on the queue */
    for (j=0;j<ncols;j++) {
      bidx++;
      seen[cols[j]] = i;
      distbuf[bidx] = 1;
      idxbuf[bidx] = cols[j];
    }
    while (bidx >= 0) {
      /* pop */
      idx = idxbuf[bidx];
      dist = distbuf[bidx];
      bidx--;
      degree++;
      if (dist < distance) {
        ncols = Gi[idx+1]-Gi[idx];
        cols = &(Gj[Gi[idx]]);
        for (j=0;j<ncols;j++) {
          if (seen[cols[j]] != i) {
            bidx++;
            seen[cols[j]] = i;
            idxbuf[bidx] = cols[j];
            distbuf[bidx] = dist+1;
          }
        }
      }
    }
    degrees[gidx[i]-s] = degree;
  }
  CHKERRQ(ISRestoreIndices(ris,&gidx));
  CHKERRQ(ISDestroy(&ris));
  CHKERRQ(PetscFree3(seen,idxbuf,distbuf));
  CHKERRQ(MatDestroyMatrices(1,&lGs));
  PetscFunctionReturn(0);
}

PetscErrorCode MatColoringCreateLargestFirstWeights(MatColoring mc,PetscReal *weights)
{
  PetscInt       i,s,e,n,ncols;
  PetscRandom    rand;
  PetscReal      r;
  PetscInt       *degrees;
  Mat            G = mc->mat;

  PetscFunctionBegin;
  /* each weight should be the degree plus a random perturbation */
  CHKERRQ(PetscRandomCreate(PetscObjectComm((PetscObject)mc),&rand));
  CHKERRQ(PetscRandomSetFromOptions(rand));
  CHKERRQ(MatGetOwnershipRange(G,&s,&e));
  n=e-s;
  CHKERRQ(PetscMalloc1(n,&degrees));
  CHKERRQ(MatColoringGetDegrees(G,mc->dist,degrees));
  for (i=s;i<e;i++) {
    CHKERRQ(MatGetRow(G,i,&ncols,NULL,NULL));
    CHKERRQ(PetscRandomGetValueReal(rand,&r));
    weights[i-s] = ncols + PetscAbsReal(r);
    CHKERRQ(MatRestoreRow(G,i,&ncols,NULL,NULL));
  }
  CHKERRQ(PetscRandomDestroy(&rand));
  CHKERRQ(PetscFree(degrees));
  PetscFunctionReturn(0);
}

PetscErrorCode MatColoringCreateSmallestLastWeights(MatColoring mc,PetscReal *weights)
{
  PetscInt       *degrees,*degb,*llprev,*llnext;
  PetscInt       j,i,s,e,n,nin,ln,lm,degree,maxdegree=0,bidx,idx,dist,distance=mc->dist;
  Mat            lG,*lGs;
  IS             ris;
  PetscInt       *seen;
  const PetscInt *gidx;
  PetscInt       *idxbuf;
  PetscInt       *distbuf;
  PetscInt       ncols,nxt,prv,cur;
  const PetscInt *cols;
  PetscBool      isSEQAIJ;
  Mat_SeqAIJ     *aij;
  PetscInt       *Gi,*Gj,*rperm;
  Mat            G = mc->mat;
  PetscReal      *lweights,r;
  PetscRandom    rand;

  PetscFunctionBegin;
  CHKERRQ(MatGetOwnershipRange(G,&s,&e));
  n=e-s;
  CHKERRQ(ISCreateStride(PetscObjectComm((PetscObject)G),n,s,1,&ris));
  CHKERRQ(MatIncreaseOverlap(G,1,&ris,distance+1));
  CHKERRQ(ISSort(ris));
  CHKERRQ(MatCreateSubMatrices(G,1,&ris,&ris,MAT_INITIAL_MATRIX,&lGs));
  lG = lGs[0];
  CHKERRQ(PetscObjectBaseTypeCompare((PetscObject)lG,MATSEQAIJ,&isSEQAIJ));
  PetscCheckFalse(!isSEQAIJ,PetscObjectComm((PetscObject)G),PETSC_ERR_ARG_WRONGSTATE,"Requires an MPI/SEQAIJ Matrix");
  CHKERRQ(MatGetSize(lG,&ln,&lm));
  aij = (Mat_SeqAIJ*)lG->data;
  Gi = aij->i;
  Gj = aij->j;
  CHKERRQ(PetscMalloc3(lm,&seen,lm,&idxbuf,lm,&distbuf));
  CHKERRQ(PetscMalloc1(lm,&degrees));
  CHKERRQ(PetscMalloc1(lm,&lweights));
  for (i=0;i<ln;i++) {
    seen[i]=-1;
    lweights[i] = 1.;
  }
  CHKERRQ(ISGetIndices(ris,&gidx));
  for (i=0;i<ln;i++) {
    bidx=-1;
    ncols = Gi[i+1]-Gi[i];
    cols = &(Gj[Gi[i]]);
    degree = 0;
    /* place the distance-one neighbors on the queue */
    for (j=0;j<ncols;j++) {
      bidx++;
      seen[cols[j]] = i;
      distbuf[bidx] = 1;
      idxbuf[bidx] = cols[j];
    }
    while (bidx >= 0) {
      /* pop */
      idx = idxbuf[bidx];
      dist = distbuf[bidx];
      bidx--;
      degree++;
      if (dist < distance) {
        ncols = Gi[idx+1]-Gi[idx];
        cols = &(Gj[Gi[idx]]);
        for (j=0;j<ncols;j++) {
          if (seen[cols[j]] != i) {
            bidx++;
            seen[cols[j]] = i;
            idxbuf[bidx] = cols[j];
            distbuf[bidx] = dist+1;
          }
        }
      }
    }
    degrees[i] = degree;
    if (degree > maxdegree) maxdegree = degree;
  }
  /* bucket by degree by some random permutation */
  CHKERRQ(PetscRandomCreate(PetscObjectComm((PetscObject)mc),&rand));
  CHKERRQ(PetscRandomSetFromOptions(rand));
  CHKERRQ(PetscMalloc1(ln,&rperm));
  for (i=0;i<ln;i++) {
      CHKERRQ(PetscRandomGetValueReal(rand,&r));
      lweights[i] = r;
      rperm[i]=i;
  }
  CHKERRQ(PetscSortRealWithPermutation(lm,lweights,rperm));
  CHKERRQ(PetscMalloc1(maxdegree+1,&degb));
  CHKERRQ(PetscMalloc2(ln,&llnext,ln,&llprev));
  for (i=0;i<maxdegree+1;i++) {
    degb[i] = -1;
  }
  for (i=0;i<ln;i++) {
    llnext[i] = -1;
    llprev[i] = -1;
    seen[i] = -1;
  }
  for (i=0;i<ln;i++) {
    idx = rperm[i];
    llnext[idx] = degb[degrees[idx]];
    if (degb[degrees[idx]] > 0) llprev[degb[degrees[idx]]] = idx;
    degb[degrees[idx]] = idx;
  }
  CHKERRQ(PetscFree(rperm));
  /* remove the lowest degree one */
  i=0;
  nin=0;
  while (i != maxdegree+1) {
    for (i=1;i<maxdegree+1; i++) {
      if (degb[i] > 0) {
        cur = degb[i];
        nin++;
        degrees[cur] = 0;
        degb[i] = llnext[cur];
        bidx=-1;
        ncols = Gi[cur+1]-Gi[cur];
        cols = &(Gj[Gi[cur]]);
        /* place the distance-one neighbors on the queue */
        for (j=0;j<ncols;j++) {
          if (cols[j] != cur) {
            bidx++;
            seen[cols[j]] = i;
            distbuf[bidx] = 1;
            idxbuf[bidx] = cols[j];
          }
        }
        while (bidx >= 0) {
          /* pop */
          idx = idxbuf[bidx];
          dist = distbuf[bidx];
          bidx--;
          nxt=llnext[idx];
          prv=llprev[idx];
          if (degrees[idx] > 0) {
            /* change up the degree of the neighbors still in the graph */
            if (lweights[idx] <= lweights[cur]) lweights[idx] = lweights[cur]+1;
            if (nxt > 0) {
              llprev[nxt] = prv;
            }
            if (prv > 0) {
              llnext[prv] = nxt;
            } else {
              degb[degrees[idx]] = nxt;
            }
            degrees[idx]--;
            llnext[idx] = degb[degrees[idx]];
            llprev[idx] = -1;
            if (degb[degrees[idx]] >= 0) {
              llprev[degb[degrees[idx]]] = idx;
            }
            degb[degrees[idx]] = idx;
            if (dist < distance) {
              ncols = Gi[idx+1]-Gi[idx];
              cols = &(Gj[Gi[idx]]);
              for (j=0;j<ncols;j++) {
                if (seen[cols[j]] != i) {
                  bidx++;
                  seen[cols[j]] = i;
                  idxbuf[bidx] = cols[j];
                  distbuf[bidx] = dist+1;
                }
              }
            }
          }
        }
        break;
      }
    }
  }
  for (i=0;i<lm;i++) {
    if (gidx[i] >= s && gidx[i] < e) {
      weights[gidx[i]-s] = lweights[i];
    }
  }
  CHKERRQ(PetscRandomDestroy(&rand));
  CHKERRQ(PetscFree(degb));
  CHKERRQ(PetscFree2(llnext,llprev));
  CHKERRQ(PetscFree(degrees));
  CHKERRQ(PetscFree(lweights));
  CHKERRQ(ISRestoreIndices(ris,&gidx));
  CHKERRQ(ISDestroy(&ris));
  CHKERRQ(PetscFree3(seen,idxbuf,distbuf));
  CHKERRQ(MatDestroyMatrices(1,&lGs));
  PetscFunctionReturn(0);
}

PetscErrorCode MatColoringCreateWeights(MatColoring mc,PetscReal **weights,PetscInt **lperm)
{
  PetscInt       i,s,e,n;
  PetscReal      *wts;

  PetscFunctionBegin;
  /* create weights of the specified type */
  CHKERRQ(MatGetOwnershipRange(mc->mat,&s,&e));
  n=e-s;
  CHKERRQ(PetscMalloc1(n,&wts));
  switch(mc->weight_type) {
  case MAT_COLORING_WEIGHT_RANDOM:
    CHKERRQ(MatColoringCreateRandomWeights(mc,wts));
    break;
  case MAT_COLORING_WEIGHT_LEXICAL:
    CHKERRQ(MatColoringCreateLexicalWeights(mc,wts));
    break;
  case MAT_COLORING_WEIGHT_LF:
    CHKERRQ(MatColoringCreateLargestFirstWeights(mc,wts));
    break;
  case MAT_COLORING_WEIGHT_SL:
    CHKERRQ(MatColoringCreateSmallestLastWeights(mc,wts));
    break;
  }
  if (lperm) {
    CHKERRQ(PetscMalloc1(n,lperm));
    for (i=0;i<n;i++) {
      (*lperm)[i] = i;
    }
    CHKERRQ(PetscSortRealWithPermutation(n,wts,*lperm));
    for (i=0;i<n/2;i++) {
      PetscInt swp;
      swp = (*lperm)[i];
      (*lperm)[i] = (*lperm)[n-1-i];
      (*lperm)[n-1-i] = swp;
    }
  }
  if (weights) *weights = wts;
  PetscFunctionReturn(0);
}

PetscErrorCode MatColoringSetWeights(MatColoring mc,PetscReal *weights,PetscInt *lperm)
{
  PetscInt       i,s,e,n;

  PetscFunctionBegin;
  CHKERRQ(MatGetOwnershipRange(mc->mat,&s,&e));
  n=e-s;
  if (weights) {
    CHKERRQ(PetscMalloc2(n,&mc->user_weights,n,&mc->user_lperm));
    for (i=0;i<n;i++) {
      mc->user_weights[i]=weights[i];
    }
    if (!lperm) {
      for (i=0;i<n;i++) {
        mc->user_lperm[i]=i;
      }
      CHKERRQ(PetscSortRealWithPermutation(n,mc->user_weights,mc->user_lperm));
      for (i=0;i<n/2;i++) {
        PetscInt swp;
        swp = mc->user_lperm[i];
        mc->user_lperm[i] = mc->user_lperm[n-1-i];
        mc->user_lperm[n-1-i] = swp;
      }
    } else {
      for (i=0;i<n;i++) {
        mc->user_lperm[i]=lperm[i];
      }
    }
  } else {
    mc->user_weights = NULL;
    mc->user_lperm = NULL;
  }
  PetscFunctionReturn(0);
}
