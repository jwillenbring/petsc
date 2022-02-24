
/*
   Support for the parallel dense matrix vector multiply
*/
#include <../src/mat/impls/dense/mpi/mpidense.h>
#include <petscblaslapack.h>

PetscErrorCode MatSetUpMultiply_MPIDense(Mat mat)
{
  Mat_MPIDense   *mdn = (Mat_MPIDense*)mat->data;

  PetscFunctionBegin;
  /* Create local vector that is used to scatter into */
  CHKERRQ(VecDestroy(&mdn->lvec));
  if (mdn->A) {
    CHKERRQ(MatCreateVecs(mdn->A,&mdn->lvec,NULL));
    CHKERRQ(PetscLogObjectParent((PetscObject)mat,(PetscObject)mdn->lvec));
  }
  if (!mdn->Mvctx) {
    CHKERRQ(PetscLayoutSetUp(mat->cmap));
    CHKERRQ(PetscSFCreate(PetscObjectComm((PetscObject)mat),&mdn->Mvctx));
    CHKERRQ(PetscSFSetGraphWithPattern(mdn->Mvctx,mat->cmap,PETSCSF_PATTERN_ALLGATHER));
    CHKERRQ(PetscLogObjectParent((PetscObject)mat,(PetscObject)mdn->Mvctx));
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode MatCreateSubMatrices_MPIDense_Local(Mat,PetscInt,const IS[],const IS[],MatReuse,Mat*);

PetscErrorCode MatCreateSubMatrices_MPIDense(Mat C,PetscInt ismax,const IS isrow[],const IS iscol[],MatReuse scall,Mat *submat[])
{
  PetscInt       nmax,nstages_local,nstages,i,pos,max_no;

  PetscFunctionBegin;
  /* Allocate memory to hold all the submatrices */
  if (scall != MAT_REUSE_MATRIX) {
    CHKERRQ(PetscCalloc1(ismax+1,submat));
  }
  /* Determine the number of stages through which submatrices are done */
  nmax = 20*1000000 / (C->cmap->N * sizeof(PetscInt));
  if (!nmax) nmax = 1;
  nstages_local = ismax/nmax + ((ismax % nmax) ? 1 : 0);

  /* Make sure every processor loops through the nstages */
  CHKERRMPI(MPIU_Allreduce(&nstages_local,&nstages,1,MPIU_INT,MPI_MAX,PetscObjectComm((PetscObject)C)));

  for (i=0,pos=0; i<nstages; i++) {
    if (pos+nmax <= ismax) max_no = nmax;
    else if (pos == ismax) max_no = 0;
    else                   max_no = ismax-pos;
    CHKERRQ(MatCreateSubMatrices_MPIDense_Local(C,max_no,isrow+pos,iscol+pos,scall,*submat+pos));
    pos += max_no;
  }
  PetscFunctionReturn(0);
}
/* -------------------------------------------------------------------------*/
PetscErrorCode MatCreateSubMatrices_MPIDense_Local(Mat C,PetscInt ismax,const IS isrow[],const IS iscol[],MatReuse scall,Mat *submats)
{
  Mat_MPIDense   *c = (Mat_MPIDense*)C->data;
  Mat            A  = c->A;
  Mat_SeqDense   *a = (Mat_SeqDense*)A->data,*mat;
  PetscMPIInt    rank,size,tag0,tag1,idex,end,i;
  PetscInt       N = C->cmap->N,rstart = C->rmap->rstart,count;
  const PetscInt **irow,**icol,*irow_i;
  PetscInt       *nrow,*ncol,*w1,*w3,*w4,*rtable,start;
  PetscInt       **sbuf1,m,j,k,l,ct1,**rbuf1,row,proc;
  PetscInt       nrqs,msz,**ptr,*ctr,*pa,*tmp,bsz,nrqr;
  PetscInt       is_no,jmax,**rmap,*rmap_i;
  PetscInt       ctr_j,*sbuf1_j,*rbuf1_i;
  MPI_Request    *s_waits1,*r_waits1,*s_waits2,*r_waits2;
  MPI_Status     *r_status1,*r_status2,*s_status1,*s_status2;
  MPI_Comm       comm;
  PetscScalar    **rbuf2,**sbuf2;
  PetscBool      sorted;

  PetscFunctionBegin;
  CHKERRQ(PetscObjectGetComm((PetscObject)C,&comm));
  tag0 = ((PetscObject)C)->tag;
  CHKERRMPI(MPI_Comm_rank(comm,&rank));
  CHKERRMPI(MPI_Comm_size(comm,&size));
  m    = C->rmap->N;

  /* Get some new tags to keep the communication clean */
  CHKERRQ(PetscObjectGetNewTag((PetscObject)C,&tag1));

  /* Check if the col indices are sorted */
  for (i=0; i<ismax; i++) {
    CHKERRQ(ISSorted(isrow[i],&sorted));
    PetscCheckFalse(!sorted,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"ISrow is not sorted");
    CHKERRQ(ISSorted(iscol[i],&sorted));
    PetscCheckFalse(!sorted,PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"IScol is not sorted");
  }

  CHKERRQ(PetscMalloc5(ismax,(PetscInt***)&irow,ismax,(PetscInt***)&icol,ismax,&nrow,ismax,&ncol,m,&rtable));
  for (i=0; i<ismax; i++) {
    CHKERRQ(ISGetIndices(isrow[i],&irow[i]));
    CHKERRQ(ISGetIndices(iscol[i],&icol[i]));
    CHKERRQ(ISGetLocalSize(isrow[i],&nrow[i]));
    CHKERRQ(ISGetLocalSize(iscol[i],&ncol[i]));
  }

  /* Create hash table for the mapping :row -> proc*/
  for (i=0,j=0; i<size; i++) {
    jmax = C->rmap->range[i+1];
    for (; j<jmax; j++) rtable[j] = i;
  }

  /* evaluate communication - mesg to who,length of mesg, and buffer space
     required. Based on this, buffers are allocated, and data copied into them*/
  CHKERRQ(PetscMalloc3(2*size,&w1,size,&w3,size,&w4));
  CHKERRQ(PetscArrayzero(w1,size*2)); /* initialize work vector*/
  CHKERRQ(PetscArrayzero(w3,size)); /* initialize work vector*/
  for (i=0; i<ismax; i++) {
    CHKERRQ(PetscArrayzero(w4,size)); /* initialize work vector*/
    jmax   = nrow[i];
    irow_i = irow[i];
    for (j=0; j<jmax; j++) {
      row  = irow_i[j];
      proc = rtable[row];
      w4[proc]++;
    }
    for (j=0; j<size; j++) {
      if (w4[j]) { w1[2*j] += w4[j];  w3[j]++;}
    }
  }

  nrqs       = 0;              /* no of outgoing messages */
  msz        = 0;              /* total mesg length (for all procs) */
  w1[2*rank] = 0;              /* no mesg sent to self */
  w3[rank]   = 0;
  for (i=0; i<size; i++) {
    if (w1[2*i])  { w1[2*i+1] = 1; nrqs++;} /* there exists a message to proc i */
  }
  CHKERRQ(PetscMalloc1(nrqs+1,&pa)); /*(proc -array)*/
  for (i=0,j=0; i<size; i++) {
    if (w1[2*i]) { pa[j] = i; j++; }
  }

  /* Each message would have a header = 1 + 2*(no of IS) + data */
  for (i=0; i<nrqs; i++) {
    j        = pa[i];
    w1[2*j] += w1[2*j+1] + 2* w3[j];
    msz     += w1[2*j];
  }
  /* Do a global reduction to determine how many messages to expect*/
  CHKERRQ(PetscMaxSum(comm,w1,&bsz,&nrqr));

  /* Allocate memory for recv buffers . Make sure rbuf1[0] exists by adding 1 to the buffer length */
  CHKERRQ(PetscMalloc1(nrqr+1,&rbuf1));
  CHKERRQ(PetscMalloc1(nrqr*bsz,&rbuf1[0]));
  for (i=1; i<nrqr; ++i) rbuf1[i] = rbuf1[i-1] + bsz;

  /* Post the receives */
  CHKERRQ(PetscMalloc1(nrqr+1,&r_waits1));
  for (i=0; i<nrqr; ++i) {
    CHKERRMPI(MPI_Irecv(rbuf1[i],bsz,MPIU_INT,MPI_ANY_SOURCE,tag0,comm,r_waits1+i));
  }

  /* Allocate Memory for outgoing messages */
  CHKERRQ(PetscMalloc4(size,&sbuf1,size,&ptr,2*msz,&tmp,size,&ctr));
  CHKERRQ(PetscArrayzero(sbuf1,size));
  CHKERRQ(PetscArrayzero(ptr,size));
  {
    PetscInt *iptr = tmp,ict = 0;
    for (i=0; i<nrqs; i++) {
      j        = pa[i];
      iptr    += ict;
      sbuf1[j] = iptr;
      ict      = w1[2*j];
    }
  }

  /* Form the outgoing messages */
  /* Initialize the header space */
  for (i=0; i<nrqs; i++) {
    j           = pa[i];
    sbuf1[j][0] = 0;
    CHKERRQ(PetscArrayzero(sbuf1[j]+1,2*w3[j]));
    ptr[j]      = sbuf1[j] + 2*w3[j] + 1;
  }

  /* Parse the isrow and copy data into outbuf */
  for (i=0; i<ismax; i++) {
    CHKERRQ(PetscArrayzero(ctr,size));
    irow_i = irow[i];
    jmax   = nrow[i];
    for (j=0; j<jmax; j++) {  /* parse the indices of each IS */
      row  = irow_i[j];
      proc = rtable[row];
      if (proc != rank) { /* copy to the outgoing buf*/
        ctr[proc]++;
        *ptr[proc] = row;
        ptr[proc]++;
      }
    }
    /* Update the headers for the current IS */
    for (j=0; j<size; j++) { /* Can Optimise this loop too */
      if ((ctr_j = ctr[j])) {
        sbuf1_j        = sbuf1[j];
        k              = ++sbuf1_j[0];
        sbuf1_j[2*k]   = ctr_j;
        sbuf1_j[2*k-1] = i;
      }
    }
  }

  /*  Now  post the sends */
  CHKERRQ(PetscMalloc1(nrqs+1,&s_waits1));
  for (i=0; i<nrqs; ++i) {
    j    = pa[i];
    CHKERRMPI(MPI_Isend(sbuf1[j],w1[2*j],MPIU_INT,j,tag0,comm,s_waits1+i));
  }

  /* Post receives to capture the row_data from other procs */
  CHKERRQ(PetscMalloc1(nrqs+1,&r_waits2));
  CHKERRQ(PetscMalloc1(nrqs+1,&rbuf2));
  for (i=0; i<nrqs; i++) {
    j     = pa[i];
    count = (w1[2*j] - (2*sbuf1[j][0] + 1))*N;
    CHKERRQ(PetscMalloc1(count+1,&rbuf2[i]));
    CHKERRMPI(MPI_Irecv(rbuf2[i],count,MPIU_SCALAR,j,tag1,comm,r_waits2+i));
  }

  /* Receive messages(row_nos) and then, pack and send off the rowvalues
     to the correct processors */

  CHKERRQ(PetscMalloc1(nrqr+1,&s_waits2));
  CHKERRQ(PetscMalloc1(nrqr+1,&r_status1));
  CHKERRQ(PetscMalloc1(nrqr+1,&sbuf2));

  {
    PetscScalar *sbuf2_i,*v_start;
    PetscInt    s_proc;
    for (i=0; i<nrqr; ++i) {
      CHKERRMPI(MPI_Waitany(nrqr,r_waits1,&idex,r_status1+i));
      s_proc  = r_status1[i].MPI_SOURCE;         /* send processor */
      rbuf1_i = rbuf1[idex];         /* Actual message from s_proc */
      /* no of rows = end - start; since start is array idex[], 0idex, whel end
         is length of the buffer - which is 1idex */
      start = 2*rbuf1_i[0] + 1;
      CHKERRMPI(MPI_Get_count(r_status1+i,MPIU_INT,&end));
      /* allocate memory sufficinet to hold all the row values */
      CHKERRQ(PetscMalloc1((end-start)*N,&sbuf2[idex]));
      sbuf2_i = sbuf2[idex];
      /* Now pack the data */
      for (j=start; j<end; j++) {
        row     = rbuf1_i[j] - rstart;
        v_start = a->v + row;
        for (k=0; k<N; k++) {
          sbuf2_i[0] = v_start[0];
          sbuf2_i++;
          v_start += a->lda;
        }
      }
      /* Now send off the data */
      CHKERRMPI(MPI_Isend(sbuf2[idex],(end-start)*N,MPIU_SCALAR,s_proc,tag1,comm,s_waits2+i));
    }
  }
  /* End Send-Recv of IS + row_numbers */
  CHKERRQ(PetscFree(r_status1));
  CHKERRQ(PetscFree(r_waits1));
  CHKERRQ(PetscMalloc1(nrqs+1,&s_status1));
  if (nrqs) CHKERRMPI(MPI_Waitall(nrqs,s_waits1,s_status1));
  CHKERRQ(PetscFree(s_status1));
  CHKERRQ(PetscFree(s_waits1));

  /* Create the submatrices */
  if (scall == MAT_REUSE_MATRIX) {
    for (i=0; i<ismax; i++) {
      mat = (Mat_SeqDense*)(submats[i]->data);
      PetscCheckFalse((submats[i]->rmap->n != nrow[i]) || (submats[i]->cmap->n != ncol[i]),PETSC_COMM_SELF,PETSC_ERR_ARG_SIZ,"Cannot reuse matrix. wrong size");
      CHKERRQ(PetscArrayzero(mat->v,submats[i]->rmap->n*submats[i]->cmap->n));

      submats[i]->factortype = C->factortype;
    }
  } else {
    for (i=0; i<ismax; i++) {
      CHKERRQ(MatCreate(PETSC_COMM_SELF,submats+i));
      CHKERRQ(MatSetSizes(submats[i],nrow[i],ncol[i],nrow[i],ncol[i]));
      CHKERRQ(MatSetType(submats[i],((PetscObject)A)->type_name));
      CHKERRQ(MatSeqDenseSetPreallocation(submats[i],NULL));
    }
  }

  /* Assemble the matrices */
  {
    PetscInt    col;
    PetscScalar *imat_v,*mat_v,*imat_vi,*mat_vi;

    for (i=0; i<ismax; i++) {
      mat    = (Mat_SeqDense*)submats[i]->data;
      mat_v  = a->v;
      imat_v = mat->v;
      irow_i = irow[i];
      m      = nrow[i];
      for (j=0; j<m; j++) {
        row  = irow_i[j];
        proc = rtable[row];
        if (proc == rank) {
          row     = row - rstart;
          mat_vi  = mat_v + row;
          imat_vi = imat_v + j;
          for (k=0; k<ncol[i]; k++) {
            col          = icol[i][k];
            imat_vi[k*m] = mat_vi[col*a->lda];
          }
        }
      }
    }
  }

  /* Create row map-> This maps c->row to submat->row for each submat*/
  /* this is a very expensive operation wrt memory usage */
  CHKERRQ(PetscMalloc1(ismax,&rmap));
  CHKERRQ(PetscCalloc1(ismax*C->rmap->N,&rmap[0]));
  for (i=1; i<ismax; i++) rmap[i] = rmap[i-1] + C->rmap->N;
  for (i=0; i<ismax; i++) {
    rmap_i = rmap[i];
    irow_i = irow[i];
    jmax   = nrow[i];
    for (j=0; j<jmax; j++) {
      rmap_i[irow_i[j]] = j;
    }
  }

  /* Now Receive the row_values and assemble the rest of the matrix */
  CHKERRQ(PetscMalloc1(nrqs+1,&r_status2));
  {
    PetscInt    is_max,tmp1,col,*sbuf1_i,is_sz;
    PetscScalar *rbuf2_i,*imat_v,*imat_vi;

    for (tmp1=0; tmp1<nrqs; tmp1++) { /* For each message */
      CHKERRMPI(MPI_Waitany(nrqs,r_waits2,&i,r_status2+tmp1));
      /* Now dig out the corresponding sbuf1, which contains the IS data_structure */
      sbuf1_i = sbuf1[pa[i]];
      is_max  = sbuf1_i[0];
      ct1     = 2*is_max+1;
      rbuf2_i = rbuf2[i];
      for (j=1; j<=is_max; j++) { /* For each IS belonging to the message */
        is_no  = sbuf1_i[2*j-1];
        is_sz  = sbuf1_i[2*j];
        mat    = (Mat_SeqDense*)submats[is_no]->data;
        imat_v = mat->v;
        rmap_i = rmap[is_no];
        m      = nrow[is_no];
        for (k=0; k<is_sz; k++,rbuf2_i+=N) {  /* For each row */
          row     = sbuf1_i[ct1]; ct1++;
          row     = rmap_i[row];
          imat_vi = imat_v + row;
          for (l=0; l<ncol[is_no]; l++) { /* For each col */
            col          = icol[is_no][l];
            imat_vi[l*m] = rbuf2_i[col];
          }
        }
      }
    }
  }
  /* End Send-Recv of row_values */
  CHKERRQ(PetscFree(r_status2));
  CHKERRQ(PetscFree(r_waits2));
  CHKERRQ(PetscMalloc1(nrqr+1,&s_status2));
  if (nrqr) CHKERRMPI(MPI_Waitall(nrqr,s_waits2,s_status2));
  CHKERRQ(PetscFree(s_status2));
  CHKERRQ(PetscFree(s_waits2));

  /* Restore the indices */
  for (i=0; i<ismax; i++) {
    CHKERRQ(ISRestoreIndices(isrow[i],irow+i));
    CHKERRQ(ISRestoreIndices(iscol[i],icol+i));
  }

  CHKERRQ(PetscFree5(*(PetscInt***)&irow,*(PetscInt***)&icol,nrow,ncol,rtable));
  CHKERRQ(PetscFree3(w1,w3,w4));
  CHKERRQ(PetscFree(pa));

  for (i=0; i<nrqs; ++i) {
    CHKERRQ(PetscFree(rbuf2[i]));
  }
  CHKERRQ(PetscFree(rbuf2));
  CHKERRQ(PetscFree4(sbuf1,ptr,tmp,ctr));
  CHKERRQ(PetscFree(rbuf1[0]));
  CHKERRQ(PetscFree(rbuf1));

  for (i=0; i<nrqr; ++i) {
    CHKERRQ(PetscFree(sbuf2[i]));
  }

  CHKERRQ(PetscFree(sbuf2));
  CHKERRQ(PetscFree(rmap[0]));
  CHKERRQ(PetscFree(rmap));

  for (i=0; i<ismax; i++) {
    CHKERRQ(MatAssemblyBegin(submats[i],MAT_FINAL_ASSEMBLY));
    CHKERRQ(MatAssemblyEnd(submats[i],MAT_FINAL_ASSEMBLY));
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode MatScale_MPIDense(Mat inA,PetscScalar alpha)
{
  Mat_MPIDense   *A = (Mat_MPIDense*)inA->data;

  PetscFunctionBegin;
  CHKERRQ(MatScale(A->A,alpha));
  PetscFunctionReturn(0);
}
