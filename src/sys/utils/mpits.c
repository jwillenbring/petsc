#include <petscsys.h>        /*I  "petscsys.h"  I*/
#include <petsc/private/petscimpl.h>

PetscLogEvent PETSC_BuildTwoSided;
PetscLogEvent PETSC_BuildTwoSidedF;

const char *const PetscBuildTwoSidedTypes[] = {
  "ALLREDUCE",
  "IBARRIER",
  "REDSCATTER",
  "PetscBuildTwoSidedType",
  "PETSC_BUILDTWOSIDED_",
  NULL
};

static PetscBuildTwoSidedType _twosided_type = PETSC_BUILDTWOSIDED_NOTSET;

/*@
   PetscCommBuildTwoSidedSetType - set algorithm to use when building two-sided communication

   Logically Collective

   Input Parameters:
+  comm - PETSC_COMM_WORLD
-  twosided - algorithm to use in subsequent calls to PetscCommBuildTwoSided()

   Level: developer

   Note:
   This option is currently global, but could be made per-communicator.

.seealso: PetscCommBuildTwoSided(), PetscCommBuildTwoSidedGetType()
@*/
PetscErrorCode PetscCommBuildTwoSidedSetType(MPI_Comm comm,PetscBuildTwoSidedType twosided)
{
  PetscFunctionBegin;
  if (PetscDefined(USE_DEBUG)) {                             /* We don't have a PetscObject so can't use PetscValidLogicalCollectiveEnum */
    PetscMPIInt b1[2],b2[2];
    b1[0] = -(PetscMPIInt)twosided;
    b1[1] = (PetscMPIInt)twosided;
    CHKERRMPI(MPIU_Allreduce(b1,b2,2,MPI_INT,MPI_MAX,comm));
    PetscCheckFalse(-b2[0] != b2[1],comm,PETSC_ERR_ARG_WRONG,"Enum value must be same on all processes");
  }
  _twosided_type = twosided;
  PetscFunctionReturn(0);
}

/*@
   PetscCommBuildTwoSidedGetType - set algorithm to use when building two-sided communication

   Logically Collective

   Output Parameters:
+  comm - communicator on which to query algorithm
-  twosided - algorithm to use for PetscCommBuildTwoSided()

   Level: developer

.seealso: PetscCommBuildTwoSided(), PetscCommBuildTwoSidedSetType()
@*/
PetscErrorCode PetscCommBuildTwoSidedGetType(MPI_Comm comm,PetscBuildTwoSidedType *twosided)
{
  PetscMPIInt    size;

  PetscFunctionBegin;
  *twosided = PETSC_BUILDTWOSIDED_NOTSET;
  if (_twosided_type == PETSC_BUILDTWOSIDED_NOTSET) {
    CHKERRMPI(MPI_Comm_size(comm,&size));
    _twosided_type = PETSC_BUILDTWOSIDED_ALLREDUCE; /* default for small comms, see https://gitlab.com/petsc/petsc/-/merge_requests/2611 */
#if defined(PETSC_HAVE_MPI_NONBLOCKING_COLLECTIVES)
    if (size > 1024) _twosided_type = PETSC_BUILDTWOSIDED_IBARRIER;
#endif
    CHKERRQ(PetscOptionsGetEnum(NULL,NULL,"-build_twosided",PetscBuildTwoSidedTypes,(PetscEnum*)&_twosided_type,NULL));
  }
  *twosided = _twosided_type;
  PetscFunctionReturn(0);
}

#if defined(PETSC_HAVE_MPI_NONBLOCKING_COLLECTIVES)
static PetscErrorCode PetscCommBuildTwoSided_Ibarrier(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata)
{
  PetscMPIInt    nrecvs,tag,done,i;
  MPI_Aint       lb,unitbytes;
  char           *tdata;
  MPI_Request    *sendreqs,barrier;
  PetscSegBuffer segrank,segdata;
  PetscBool      barrier_started;

  PetscFunctionBegin;
  CHKERRQ(PetscCommDuplicate(comm,&comm,&tag));
  CHKERRMPI(MPI_Type_get_extent(dtype,&lb,&unitbytes));
  PetscCheckFalse(lb != 0,comm,PETSC_ERR_SUP,"Datatype with nonzero lower bound %ld",(long)lb);
  tdata = (char*)todata;
  CHKERRQ(PetscMalloc1(nto,&sendreqs));
  for (i=0; i<nto; i++) {
    CHKERRMPI(MPI_Issend((void*)(tdata+count*unitbytes*i),count,dtype,toranks[i],tag,comm,sendreqs+i));
  }
  CHKERRQ(PetscSegBufferCreate(sizeof(PetscMPIInt),4,&segrank));
  CHKERRQ(PetscSegBufferCreate(unitbytes,4*count,&segdata));

  nrecvs  = 0;
  barrier = MPI_REQUEST_NULL;
  /* MPICH-3.2 sometimes does not create a request in some "optimized" cases.  This is arguably a standard violation,
   * but we need to work around it. */
  barrier_started = PETSC_FALSE;
  for (done=0; !done;) {
    PetscMPIInt flag;
    MPI_Status  status;
    CHKERRMPI(MPI_Iprobe(MPI_ANY_SOURCE,tag,comm,&flag,&status));
    if (flag) {                 /* incoming message */
      PetscMPIInt *recvrank;
      void        *buf;
      CHKERRQ(PetscSegBufferGet(segrank,1,&recvrank));
      CHKERRQ(PetscSegBufferGet(segdata,count,&buf));
      *recvrank = status.MPI_SOURCE;
      CHKERRMPI(MPI_Recv(buf,count,dtype,status.MPI_SOURCE,tag,comm,MPI_STATUS_IGNORE));
      nrecvs++;
    }
    if (!barrier_started) {
      PetscMPIInt sent,nsends;
      CHKERRQ(PetscMPIIntCast(nto,&nsends));
      CHKERRMPI(MPI_Testall(nsends,sendreqs,&sent,MPI_STATUSES_IGNORE));
      if (sent) {
        CHKERRMPI(MPI_Ibarrier(comm,&barrier));
        barrier_started = PETSC_TRUE;
        CHKERRQ(PetscFree(sendreqs));
      }
    } else {
      CHKERRMPI(MPI_Test(&barrier,&done,MPI_STATUS_IGNORE));
    }
  }
  *nfrom = nrecvs;
  CHKERRQ(PetscSegBufferExtractAlloc(segrank,fromranks));
  CHKERRQ(PetscSegBufferDestroy(&segrank));
  CHKERRQ(PetscSegBufferExtractAlloc(segdata,fromdata));
  CHKERRQ(PetscSegBufferDestroy(&segdata));
  CHKERRQ(PetscCommDestroy(&comm));
  PetscFunctionReturn(0);
}
#endif

static PetscErrorCode PetscCommBuildTwoSided_Allreduce(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata)
{
  PetscMPIInt      size,rank,*iflags,nrecvs,tag,*franks,i,flg;
  MPI_Aint         lb,unitbytes;
  char             *tdata,*fdata;
  MPI_Request      *reqs,*sendreqs;
  MPI_Status       *statuses;
  PetscCommCounter *counter;

  PetscFunctionBegin;
  CHKERRMPI(MPI_Comm_size(comm,&size));
  CHKERRMPI(MPI_Comm_rank(comm,&rank));
  CHKERRQ(PetscCommDuplicate(comm,&comm,&tag));
  CHKERRMPI(MPI_Comm_get_attr(comm,Petsc_Counter_keyval,&counter,&flg));
  PetscCheckFalse(!flg,PETSC_COMM_SELF,PETSC_ERR_PLIB,"Inner PETSc communicator does not have its tag/name counter attribute set");
  if (!counter->iflags) {
    CHKERRQ(PetscCalloc1(size,&counter->iflags));
    iflags = counter->iflags;
  } else {
    iflags = counter->iflags;
    CHKERRQ(PetscArrayzero(iflags,size));
  }
  for (i=0; i<nto; i++) iflags[toranks[i]] = 1;
  CHKERRMPI(MPIU_Allreduce(MPI_IN_PLACE,iflags,size,MPI_INT,MPI_SUM,comm));
  nrecvs   = iflags[rank];
  CHKERRMPI(MPI_Type_get_extent(dtype,&lb,&unitbytes));
  PetscCheck(lb == 0,comm,PETSC_ERR_SUP,"Datatype with nonzero lower bound %ld",(long)lb);
  CHKERRQ(PetscMalloc(nrecvs*count*unitbytes,&fdata));
  tdata    = (char*)todata;
  CHKERRQ(PetscMalloc2(nto+nrecvs,&reqs,nto+nrecvs,&statuses));
  sendreqs = reqs + nrecvs;
  for (i=0; i<nrecvs; i++) {
    CHKERRMPI(MPI_Irecv((void*)(fdata+count*unitbytes*i),count,dtype,MPI_ANY_SOURCE,tag,comm,reqs+i));
  }
  for (i=0; i<nto; i++) {
    CHKERRMPI(MPI_Isend((void*)(tdata+count*unitbytes*i),count,dtype,toranks[i],tag,comm,sendreqs+i));
  }
  CHKERRMPI(MPI_Waitall(nto+nrecvs,reqs,statuses));
  CHKERRQ(PetscMalloc1(nrecvs,&franks));
  for (i=0; i<nrecvs; i++) franks[i] = statuses[i].MPI_SOURCE;
  CHKERRQ(PetscFree2(reqs,statuses));
  CHKERRQ(PetscCommDestroy(&comm));

  *nfrom            = nrecvs;
  *fromranks        = franks;
  *(void**)fromdata = fdata;
  PetscFunctionReturn(0);
}

#if defined(PETSC_HAVE_MPI_REDUCE_SCATTER_BLOCK)
static PetscErrorCode PetscCommBuildTwoSided_RedScatter(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata)
{
  PetscMPIInt    size,*iflags,nrecvs,tag,*franks,i,flg;
  MPI_Aint       lb,unitbytes;
  char           *tdata,*fdata;
  MPI_Request    *reqs,*sendreqs;
  MPI_Status     *statuses;
  PetscCommCounter *counter;

  PetscFunctionBegin;
  CHKERRMPI(MPI_Comm_size(comm,&size));
  CHKERRQ(PetscCommDuplicate(comm,&comm,&tag));
  CHKERRMPI(MPI_Comm_get_attr(comm,Petsc_Counter_keyval,&counter,&flg));
  PetscCheckFalse(!flg,PETSC_COMM_SELF,PETSC_ERR_PLIB,"Inner PETSc communicator does not have its tag/name counter attribute set");
  if (!counter->iflags) {
    CHKERRQ(PetscCalloc1(size,&counter->iflags));
    iflags = counter->iflags;
  } else {
    iflags = counter->iflags;
    CHKERRQ(PetscArrayzero(iflags,size));
  }
  for (i=0; i<nto; i++) iflags[toranks[i]] = 1;
  CHKERRMPI(MPI_Reduce_scatter_block(iflags,&nrecvs,1,MPI_INT,MPI_SUM,comm));
  CHKERRMPI(MPI_Type_get_extent(dtype,&lb,&unitbytes));
  PetscCheckFalse(lb != 0,comm,PETSC_ERR_SUP,"Datatype with nonzero lower bound %ld",(long)lb);
  CHKERRQ(PetscMalloc(nrecvs*count*unitbytes,&fdata));
  tdata    = (char*)todata;
  CHKERRQ(PetscMalloc2(nto+nrecvs,&reqs,nto+nrecvs,&statuses));
  sendreqs = reqs + nrecvs;
  for (i=0; i<nrecvs; i++) {
    CHKERRMPI(MPI_Irecv((void*)(fdata+count*unitbytes*i),count,dtype,MPI_ANY_SOURCE,tag,comm,reqs+i));
  }
  for (i=0; i<nto; i++) {
    CHKERRMPI(MPI_Isend((void*)(tdata+count*unitbytes*i),count,dtype,toranks[i],tag,comm,sendreqs+i));
  }
  CHKERRMPI(MPI_Waitall(nto+nrecvs,reqs,statuses));
  CHKERRQ(PetscMalloc1(nrecvs,&franks));
  for (i=0; i<nrecvs; i++) franks[i] = statuses[i].MPI_SOURCE;
  CHKERRQ(PetscFree2(reqs,statuses));
  CHKERRQ(PetscCommDestroy(&comm));

  *nfrom            = nrecvs;
  *fromranks        = franks;
  *(void**)fromdata = fdata;
  PetscFunctionReturn(0);
}
#endif

/*@C
   PetscCommBuildTwoSided - discovers communicating ranks given one-sided information, moving constant-sized data in the process (often message lengths)

   Collective

   Input Parameters:
+  comm - communicator
.  count - number of entries to send/receive (must match on all ranks)
.  dtype - datatype to send/receive from each rank (must match on all ranks)
.  nto - number of ranks to send data to
.  toranks - ranks to send to (array of length nto)
-  todata - data to send to each rank (packed)

   Output Parameters:
+  nfrom - number of ranks receiving messages from
.  fromranks - ranks receiving messages from (length nfrom; caller should PetscFree())
-  fromdata - packed data from each rank, each with count entries of type dtype (length nfrom, caller responsible for PetscFree())

   Level: developer

   Options Database Keys:
.  -build_twosided <allreduce|ibarrier|redscatter> - algorithm to set up two-sided communication. Default is allreduce for communicators with <= 1024 ranks, otherwise ibarrier.

   Notes:
   This memory-scalable interface is an alternative to calling PetscGatherNumberOfMessages() and
   PetscGatherMessageLengths(), possibly with a subsequent round of communication to send other constant-size data.

   Basic data types as well as contiguous types are supported, but non-contiguous (e.g., strided) types are not.

   References:
.  * - Hoefler, Siebert and Lumsdaine, The MPI_Ibarrier implementation uses the algorithm in
   Scalable communication protocols for dynamic sparse data exchange, 2010.

.seealso: PetscGatherNumberOfMessages(), PetscGatherMessageLengths()
@*/
PetscErrorCode PetscCommBuildTwoSided(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata)
{
  PetscBuildTwoSidedType buildtype = PETSC_BUILDTWOSIDED_NOTSET;

  PetscFunctionBegin;
  CHKERRQ(PetscSysInitializePackage());
  CHKERRQ(PetscLogEventSync(PETSC_BuildTwoSided,comm));
  CHKERRQ(PetscLogEventBegin(PETSC_BuildTwoSided,0,0,0,0));
  CHKERRQ(PetscCommBuildTwoSidedGetType(comm,&buildtype));
  switch (buildtype) {
  case PETSC_BUILDTWOSIDED_IBARRIER:
#if defined(PETSC_HAVE_MPI_NONBLOCKING_COLLECTIVES)
    CHKERRQ(PetscCommBuildTwoSided_Ibarrier(comm,count,dtype,nto,toranks,todata,nfrom,fromranks,fromdata));
    break;
#else
    SETERRQ(comm,PETSC_ERR_PLIB,"MPI implementation does not provide MPI_Ibarrier (part of MPI-3)");
#endif
  case PETSC_BUILDTWOSIDED_ALLREDUCE:
    CHKERRQ(PetscCommBuildTwoSided_Allreduce(comm,count,dtype,nto,toranks,todata,nfrom,fromranks,fromdata));
    break;
  case PETSC_BUILDTWOSIDED_REDSCATTER:
#if defined(PETSC_HAVE_MPI_REDUCE_SCATTER_BLOCK)
    CHKERRQ(PetscCommBuildTwoSided_RedScatter(comm,count,dtype,nto,toranks,todata,nfrom,fromranks,fromdata));
    break;
#else
    SETERRQ(comm,PETSC_ERR_PLIB,"MPI implementation does not provide MPI_Reduce_scatter_block (part of MPI-2.2)");
#endif
  default: SETERRQ(comm,PETSC_ERR_PLIB,"Unknown method for building two-sided communication");
  }
  CHKERRQ(PetscLogEventEnd(PETSC_BuildTwoSided,0,0,0,0));
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscCommBuildTwoSidedFReq_Reference(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,
                                                           PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata,PetscMPIInt ntags,MPI_Request **toreqs,MPI_Request **fromreqs,
                                                           PetscErrorCode (*send)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,PetscMPIInt,void*,MPI_Request[],void*),
                                                           PetscErrorCode (*recv)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,void*,MPI_Request[],void*),void *ctx)
{
  PetscMPIInt i,*tag;
  MPI_Aint    lb,unitbytes;
  MPI_Request *sendreq,*recvreq;

  PetscFunctionBegin;
  CHKERRQ(PetscMalloc1(ntags,&tag));
  if (ntags > 0) {
    CHKERRQ(PetscCommDuplicate(comm,&comm,&tag[0]));
  }
  for (i=1; i<ntags; i++) {
    CHKERRQ(PetscCommGetNewTag(comm,&tag[i]));
  }

  /* Perform complete initial rendezvous */
  CHKERRQ(PetscCommBuildTwoSided(comm,count,dtype,nto,toranks,todata,nfrom,fromranks,fromdata));

  CHKERRQ(PetscMalloc1(nto*ntags,&sendreq));
  CHKERRQ(PetscMalloc1(*nfrom*ntags,&recvreq));

  CHKERRMPI(MPI_Type_get_extent(dtype,&lb,&unitbytes));
  PetscCheckFalse(lb != 0,comm,PETSC_ERR_SUP,"Datatype with nonzero lower bound %ld",(long)lb);
  for (i=0; i<nto; i++) {
    PetscMPIInt k;
    for (k=0; k<ntags; k++) sendreq[i*ntags+k] = MPI_REQUEST_NULL;
    CHKERRQ((*send)(comm,tag,i,toranks[i],((char*)todata)+count*unitbytes*i,sendreq+i*ntags,ctx));
  }
  for (i=0; i<*nfrom; i++) {
    void *header = (*(char**)fromdata) + count*unitbytes*i;
    PetscMPIInt k;
    for (k=0; k<ntags; k++) recvreq[i*ntags+k] = MPI_REQUEST_NULL;
    CHKERRQ((*recv)(comm,tag,(*fromranks)[i],header,recvreq+i*ntags,ctx));
  }
  CHKERRQ(PetscFree(tag));
  CHKERRQ(PetscCommDestroy(&comm));
  *toreqs = sendreq;
  *fromreqs = recvreq;
  PetscFunctionReturn(0);
}

#if defined(PETSC_HAVE_MPI_NONBLOCKING_COLLECTIVES)

static PetscErrorCode PetscCommBuildTwoSidedFReq_Ibarrier(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,
                                                          PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata,PetscMPIInt ntags,MPI_Request **toreqs,MPI_Request **fromreqs,
                                                          PetscErrorCode (*send)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,PetscMPIInt,void*,MPI_Request[],void*),
                                                          PetscErrorCode (*recv)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,void*,MPI_Request[],void*),void *ctx)
{
  PetscMPIInt    nrecvs,tag,*tags,done,i;
  MPI_Aint       lb,unitbytes;
  char           *tdata;
  MPI_Request    *sendreqs,*usendreqs,*req,barrier;
  PetscSegBuffer segrank,segdata,segreq;
  PetscBool      barrier_started;

  PetscFunctionBegin;
  CHKERRQ(PetscCommDuplicate(comm,&comm,&tag));
  CHKERRQ(PetscMalloc1(ntags,&tags));
  for (i=0; i<ntags; i++) {
    CHKERRQ(PetscCommGetNewTag(comm,&tags[i]));
  }
  CHKERRMPI(MPI_Type_get_extent(dtype,&lb,&unitbytes));
  PetscCheckFalse(lb != 0,comm,PETSC_ERR_SUP,"Datatype with nonzero lower bound %ld",(long)lb);
  tdata = (char*)todata;
  CHKERRQ(PetscMalloc1(nto,&sendreqs));
  CHKERRQ(PetscMalloc1(nto*ntags,&usendreqs));
  /* Post synchronous sends */
  for (i=0; i<nto; i++) {
    CHKERRMPI(MPI_Issend((void*)(tdata+count*unitbytes*i),count,dtype,toranks[i],tag,comm,sendreqs+i));
  }
  /* Post actual payloads.  These are typically larger messages.  Hopefully sending these later does not slow down the
   * synchronous messages above. */
  for (i=0; i<nto; i++) {
    PetscMPIInt k;
    for (k=0; k<ntags; k++) usendreqs[i*ntags+k] = MPI_REQUEST_NULL;
    CHKERRQ((*send)(comm,tags,i,toranks[i],tdata+count*unitbytes*i,usendreqs+i*ntags,ctx));
  }

  CHKERRQ(PetscSegBufferCreate(sizeof(PetscMPIInt),4,&segrank));
  CHKERRQ(PetscSegBufferCreate(unitbytes,4*count,&segdata));
  CHKERRQ(PetscSegBufferCreate(sizeof(MPI_Request),4,&segreq));

  nrecvs  = 0;
  barrier = MPI_REQUEST_NULL;
  /* MPICH-3.2 sometimes does not create a request in some "optimized" cases.  This is arguably a standard violation,
   * but we need to work around it. */
  barrier_started = PETSC_FALSE;
  for (done=0; !done;) {
    PetscMPIInt flag;
    MPI_Status  status;
    CHKERRMPI(MPI_Iprobe(MPI_ANY_SOURCE,tag,comm,&flag,&status));
    if (flag) {                 /* incoming message */
      PetscMPIInt *recvrank,k;
      void        *buf;
      CHKERRQ(PetscSegBufferGet(segrank,1,&recvrank));
      CHKERRQ(PetscSegBufferGet(segdata,count,&buf));
      *recvrank = status.MPI_SOURCE;
      CHKERRMPI(MPI_Recv(buf,count,dtype,status.MPI_SOURCE,tag,comm,MPI_STATUS_IGNORE));
      CHKERRQ(PetscSegBufferGet(segreq,ntags,&req));
      for (k=0; k<ntags; k++) req[k] = MPI_REQUEST_NULL;
      CHKERRQ((*recv)(comm,tags,status.MPI_SOURCE,buf,req,ctx));
      nrecvs++;
    }
    if (!barrier_started) {
      PetscMPIInt sent,nsends;
      CHKERRQ(PetscMPIIntCast(nto,&nsends));
      CHKERRMPI(MPI_Testall(nsends,sendreqs,&sent,MPI_STATUSES_IGNORE));
      if (sent) {
        CHKERRMPI(MPI_Ibarrier(comm,&barrier));
        barrier_started = PETSC_TRUE;
      }
    } else {
      CHKERRMPI(MPI_Test(&barrier,&done,MPI_STATUS_IGNORE));
    }
  }
  *nfrom = nrecvs;
  CHKERRQ(PetscSegBufferExtractAlloc(segrank,fromranks));
  CHKERRQ(PetscSegBufferDestroy(&segrank));
  CHKERRQ(PetscSegBufferExtractAlloc(segdata,fromdata));
  CHKERRQ(PetscSegBufferDestroy(&segdata));
  *toreqs = usendreqs;
  CHKERRQ(PetscSegBufferExtractAlloc(segreq,fromreqs));
  CHKERRQ(PetscSegBufferDestroy(&segreq));
  CHKERRQ(PetscFree(sendreqs));
  CHKERRQ(PetscFree(tags));
  CHKERRQ(PetscCommDestroy(&comm));
  PetscFunctionReturn(0);
}
#endif

/*@C
   PetscCommBuildTwoSidedF - discovers communicating ranks given one-sided information, calling user-defined functions during rendezvous

   Collective

   Input Parameters:
+  comm - communicator
.  count - number of entries to send/receive in initial rendezvous (must match on all ranks)
.  dtype - datatype to send/receive from each rank (must match on all ranks)
.  nto - number of ranks to send data to
.  toranks - ranks to send to (array of length nto)
.  todata - data to send to each rank (packed)
.  ntags - number of tags needed by send/recv callbacks
.  send - callback invoked on sending process when ready to send primary payload
.  recv - callback invoked on receiving process after delivery of rendezvous message
-  ctx - context for callbacks

   Output Parameters:
+  nfrom - number of ranks receiving messages from
.  fromranks - ranks receiving messages from (length nfrom; caller should PetscFree())
-  fromdata - packed data from each rank, each with count entries of type dtype (length nfrom, caller responsible for PetscFree())

   Level: developer

   Notes:
   This memory-scalable interface is an alternative to calling PetscGatherNumberOfMessages() and
   PetscGatherMessageLengths(), possibly with a subsequent round of communication to send other data.

   Basic data types as well as contiguous types are supported, but non-contiguous (e.g., strided) types are not.

   References:
.  * - Hoefler, Siebert and Lumsdaine, The MPI_Ibarrier implementation uses the algorithm in
   Scalable communication protocols for dynamic sparse data exchange, 2010.

.seealso: PetscCommBuildTwoSided(), PetscCommBuildTwoSidedFReq(), PetscGatherNumberOfMessages(), PetscGatherMessageLengths()
@*/
PetscErrorCode PetscCommBuildTwoSidedF(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata,PetscMPIInt ntags,
                                       PetscErrorCode (*send)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,PetscMPIInt,void*,MPI_Request[],void*),
                                       PetscErrorCode (*recv)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,void*,MPI_Request[],void*),void *ctx)
{
  MPI_Request    *toreqs,*fromreqs;

  PetscFunctionBegin;
  CHKERRQ(PetscCommBuildTwoSidedFReq(comm,count,dtype,nto,toranks,todata,nfrom,fromranks,fromdata,ntags,&toreqs,&fromreqs,send,recv,ctx));
  CHKERRMPI(MPI_Waitall(nto*ntags,toreqs,MPI_STATUSES_IGNORE));
  CHKERRMPI(MPI_Waitall(*nfrom*ntags,fromreqs,MPI_STATUSES_IGNORE));
  CHKERRQ(PetscFree(toreqs));
  CHKERRQ(PetscFree(fromreqs));
  PetscFunctionReturn(0);
}

/*@C
   PetscCommBuildTwoSidedFReq - discovers communicating ranks given one-sided information, calling user-defined functions during rendezvous, returns requests

   Collective

   Input Parameters:
+  comm - communicator
.  count - number of entries to send/receive in initial rendezvous (must match on all ranks)
.  dtype - datatype to send/receive from each rank (must match on all ranks)
.  nto - number of ranks to send data to
.  toranks - ranks to send to (array of length nto)
.  todata - data to send to each rank (packed)
.  ntags - number of tags needed by send/recv callbacks
.  send - callback invoked on sending process when ready to send primary payload
.  recv - callback invoked on receiving process after delivery of rendezvous message
-  ctx - context for callbacks

   Output Parameters:
+  nfrom - number of ranks receiving messages from
.  fromranks - ranks receiving messages from (length nfrom; caller should PetscFree())
.  fromdata - packed data from each rank, each with count entries of type dtype (length nfrom, caller responsible for PetscFree())
.  toreqs - array of nto*ntags sender requests (caller must wait on these, then PetscFree())
-  fromreqs - array of nfrom*ntags receiver requests (caller must wait on these, then PetscFree())

   Level: developer

   Notes:
   This memory-scalable interface is an alternative to calling PetscGatherNumberOfMessages() and
   PetscGatherMessageLengths(), possibly with a subsequent round of communication to send other data.

   Basic data types as well as contiguous types are supported, but non-contiguous (e.g., strided) types are not.

   References:
.  * - Hoefler, Siebert and Lumsdaine, The MPI_Ibarrier implementation uses the algorithm in
   Scalable communication protocols for dynamic sparse data exchange, 2010.

.seealso: PetscCommBuildTwoSided(), PetscCommBuildTwoSidedF(), PetscGatherNumberOfMessages(), PetscGatherMessageLengths()
@*/
PetscErrorCode PetscCommBuildTwoSidedFReq(MPI_Comm comm,PetscMPIInt count,MPI_Datatype dtype,PetscMPIInt nto,const PetscMPIInt *toranks,const void *todata,
                                          PetscMPIInt *nfrom,PetscMPIInt **fromranks,void *fromdata,PetscMPIInt ntags,MPI_Request **toreqs,MPI_Request **fromreqs,
                                          PetscErrorCode (*send)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,PetscMPIInt,void*,MPI_Request[],void*),
                                          PetscErrorCode (*recv)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,void*,MPI_Request[],void*),void *ctx)
{
  PetscErrorCode         (*f)(MPI_Comm,PetscMPIInt,MPI_Datatype,PetscMPIInt,const PetscMPIInt[],const void*,
                              PetscMPIInt*,PetscMPIInt**,void*,PetscMPIInt,MPI_Request**,MPI_Request**,
                              PetscErrorCode (*send)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,PetscMPIInt,void*,MPI_Request[],void*),
                              PetscErrorCode (*recv)(MPI_Comm,const PetscMPIInt[],PetscMPIInt,void*,MPI_Request[],void*),void *ctx);
  PetscBuildTwoSidedType buildtype = PETSC_BUILDTWOSIDED_NOTSET;
  PetscMPIInt i,size;

  PetscFunctionBegin;
  CHKERRQ(PetscSysInitializePackage());
  CHKERRMPI(MPI_Comm_size(comm,&size));
  for (i=0; i<nto; i++) {
    PetscCheckFalse(toranks[i] < 0 || size <= toranks[i],comm,PETSC_ERR_ARG_OUTOFRANGE,"toranks[%d] %d not in comm size %d",i,toranks[i],size);
  }
  CHKERRQ(PetscLogEventSync(PETSC_BuildTwoSidedF,comm));
  CHKERRQ(PetscLogEventBegin(PETSC_BuildTwoSidedF,0,0,0,0));
  CHKERRQ(PetscCommBuildTwoSidedGetType(comm,&buildtype));
  switch (buildtype) {
  case PETSC_BUILDTWOSIDED_IBARRIER:
#if defined(PETSC_HAVE_MPI_NONBLOCKING_COLLECTIVES)
    f = PetscCommBuildTwoSidedFReq_Ibarrier;
    break;
#else
    SETERRQ(comm,PETSC_ERR_PLIB,"MPI implementation does not provide MPI_Ibarrier (part of MPI-3)");
#endif
  case PETSC_BUILDTWOSIDED_ALLREDUCE:
  case PETSC_BUILDTWOSIDED_REDSCATTER:
    f = PetscCommBuildTwoSidedFReq_Reference;
    break;
  default: SETERRQ(comm,PETSC_ERR_PLIB,"Unknown method for building two-sided communication");
  }
  CHKERRQ((*f)(comm,count,dtype,nto,toranks,todata,nfrom,fromranks,fromdata,ntags,toreqs,fromreqs,send,recv,ctx));
  CHKERRQ(PetscLogEventEnd(PETSC_BuildTwoSidedF,0,0,0,0));
  PetscFunctionReturn(0);
}
