/*
 * parilut.c
 *
 * This file implements the parallel phase of the ILUT algorithm
 *
 * Started 10/21/95
 * George
 *
 * Taken over by MRGates 7/1/97.
 *
 * 7/8
 *  - added rowlen to rmat and verified
 * 7/11
 *  - MPI and validated
 *  - fixed one more problem with rowlen (a rcolind--)
 * 7/25
 *  - replaced George's reduction and second drop functions with my own
 *    The biggest difference is since I allow non-diagonal MIS sets then
 *    there is fill into L and the L must be processed in the correct
 *    order. Therefore I reverted to using the workspace as in serilut.
 *    (Note that this changes our answer so it is hard to verify.)
 *  - seperated the second drop function into four stages:
 *     1) drop below rtol
 *     2) seperate LU entries
 *     3) update L for the row
 *     4) form nrmat or DU for the row
 * 7/28
 *  - finished the local factorization to reduce non-diagonal sets.
 *    This allows fillin, but the remote reduction still does not since
 *    all the necesary rows are not recieved yet (and thus it doesn't
 *    know what rows are actually in the MIS--otherwise we could just
 *    ignore MIS fillin for the moment).
 * 7/29
 *  - send all factored rows, not just the requested ones (changes EraseMap also)
 *  - add the (maxnz+2) factor to the map, so for outside nodes, l is the exact index
 *  - removed inrval, instead using the new permutation to know what rows are MIS
 *  - removed map from the cinfo, since it was never refered to but globally
 * 8/1
 *  - implemented split PE numbering. change VPE(x) to (x) to get back unsplit numbering.
 * 8/6
 *  - Removed split PE numbering. After further testing, this does not seem to be an
 *    improvement, since it increases the number of levels. See par_split.c for that code.
 */

#include "./DistributedMatrixPilutSolver.h"
#include "ilu.h"

/*************************************************************************
* This function performs ILUT on the boundary nodes via MIS computation
**************************************************************************/
void ParILUT(DataDistType *ddist, FactorMatType *ldu,
	     ReduceMatType *rmat, int gmaxnz, double tol, 
             hypre_PilutSolverGlobals *globals )
{
  int nmis, i, j, k, l, nlevel, nbnd;
  CommInfoType cinfo;
  int *perm, *iperm, *newiperm, *newperm; 
  ReduceMatType *rmats[2], nrmat;

  PrintLine("ILUT start", globals);

  /* Initialize globals */
  global_maxnz = gmaxnz;

  nrows    = ddist->ddist_nrows;
  lnrows   = ddist->ddist_lnrows;
  firstrow = ddist->ddist_rowdist[mype];
  lastrow  = ddist->ddist_rowdist[mype+1];
  perm  = ldu->perm;
  iperm = ldu->iperm;

  ndone = rmat->rmat_ndone;
  nbnd = ntogo = rmat->rmat_ntogo;
  nleft = GlobalSESum(ntogo, pilut_comm);

  rmats[0] = rmat;
  rmats[1] = &nrmat;

  /* Initialize and allocate structures, including global workspace */
  ParINIT( &nrmat, &cinfo, ddist->ddist_rowdist, globals );

  /* Copy the old perm into new perm vectors at the begining.
   * After that this is done more or less automatically */
  newperm  = idx_malloc(lnrows, "ParILUT: newperm");
  newiperm = idx_malloc(lnrows, "ParILUT: newiperm");

  memcpy_idx(newperm,   perm, lnrows);
  memcpy_idx(newiperm, iperm, lnrows);

  ldu->nnodes[0] = ndone;
  nlevel = 0;

  while( nleft > 0 ) {
    /* printf("PE %d Nlevel: %d, Nleft: %d, (%d,%d)\n",
     * mype, nlevel, nleft, ndone, ntogo); fflush(0); */

    ComputeCommInfo(rmats[nlevel%2], &cinfo, ddist->ddist_rowdist, globals );
    nmis = SelectSet(rmats[nlevel%2], &cinfo, perm, iperm, newperm, newiperm, globals );

    FactorLocal(ldu, rmats[nlevel%2], rmats[(nlevel+1)%2], &cinfo,
		perm, iperm, newperm, newiperm, nmis, tol, globals );

    fflush(0); MPI_Barrier(pilut_comm);
    SendFactoredRows(ldu, &cinfo, newperm, nmis, globals);
    fflush(0); MPI_Barrier(pilut_comm);

    ComputeRmat(ldu, rmats[nlevel%2], rmats[(nlevel+1)%2], &cinfo,
		perm, iperm, newperm, newiperm, nmis, tol, globals);

    EraseMap(&cinfo, newperm, nmis, globals);

    /* copy the new portion of the permutation, and the entire inverse
     * (since updates to the inverse are scattered throughout.) */
    memcpy_idx(perm+ndone, newperm+ndone,  ntogo );
    memcpy_idx(iperm,      newiperm,       lnrows);

    /* setup next rmat */
    nlevel++;
    ndone = rmats[nlevel%2]->rmat_ndone = ndone+nmis;
    ntogo = rmats[nlevel%2]->rmat_ntogo = ntogo-nmis;

    nleft = GlobalSESum(ntogo, pilut_comm);

    if (nlevel > MAXNLEVEL)
      errexit("Maximum number of levels exceeded!\n", globals);
    ldu->nnodes[nlevel] = ndone;
  }
  ldu->nlevels = nlevel;

  free_multi(jr, jw, lr, w, map,
	     nrmat.rmat_rnz,        nrmat.rmat_rrowlen,  nrmat.rmat_rcolind,   
             nrmat.rmat_rvalues,
	     cinfo.gatherbuf,  cinfo.rrowind,  cinfo.rnbrind,   cinfo.rnbrptr, 
	     cinfo.snbrind, cinfo.srowind, cinfo.snbrptr,  
             cinfo.incolind,  cinfo.invalues, 
             newperm, newiperm, vrowdist, -1);

  PrintLine("ParILUT done", globals);
}


/*************************************************************************
* This function determines communication info. It assumes (and leaves)
* the map in a zero state. If memory requirements increase, it will
* free and reallocate memory for send/recieve buffers. Usually memory
* doesn't increase since the problem size is decreasing each iteration.
*
* The rrowind and srowind now have two bits packed into them, so
* (rowind>>2) is the index, rowind & 0x1 is lo, rowind & 0x2 is hi,
* where lo==1 means the lower half has this nonzero col index, hi==1 means
* the upper half has this nonzero col index.
**************************************************************************/
void ComputeCommInfo(ReduceMatType *rmat, CommInfoType *cinfo, int *rowdist,
             hypre_PilutSolverGlobals *globals)
{
  int i, ir, j, k, midpt, penum;
  int nrecv, nsend, rnnbr, snnbr, maxnrecv, maxnsend, maxnrand;
  int *rnz, *rcolind;
  int *rrowind,  *rnbrptr,  *rnbrind, *srowind, *snbrind, *snbrptr;
  MPI_Status Status ;

  PrintLine("ComputeCommInfo", globals);

  rnz = rmat->rmat_rnz;

  rnbrind  = cinfo->rnbrind;
  rnbrptr  = cinfo->rnbrptr;
  rrowind  = cinfo->rrowind;

  snbrind  = cinfo->snbrind;
  snbrptr  = cinfo->snbrptr;

  /* Determine the indices that are needed */
  nrecv  = 0;
  midpt = (firstrow + lastrow)/2;
  for (ir=0; ir<ntogo; ir++) {
    rcolind = rmat->rmat_rcolind[ir];
    for (j=1; j<rnz[ir]; j++) {
      k = rcolind[j];
      CheckBounds(0, k, nrows, globals);
      if ((k < firstrow || k >= lastrow) && map[k] == 0) {
        map[k] = 1;
        rrowind[nrecv++] = k;
      }
    }
  }

  /* Sort the indices to be received in increasing order */
  sincsort_fast(nrecv, rrowind);

  /* Determine processor boundaries in the rowind */
  rnnbr = 0;
  rnbrptr[0] = 0;
  for (penum=0, j=0;   penum<npes && j<nrecv;   penum++) {
    k = j;
    for (; j<nrecv; j++) {
      if (rrowind[j] >= rowdist[penum+1])
        break;
    }
    if (j-k > 0) { /* Something for pe penum */
      rnbrind[rnnbr] = penum;
      rnbrptr[++rnnbr] = j;
    }
  }
  cinfo->rnnbr = rnnbr;

  /* reset the map afterwards */
  for (i=0; i<nrecv; i++)
    map[rrowind[i]] = 0;

  /* Now you know from which processors, and what you need. */
  cinfo->maxntogo = GlobalSEMax(ntogo, pilut_comm);
  maxnrecv = rnnbr*(cinfo->maxntogo);  /*GlobalSEMax(nrecv);*/

  /* If memory requirements change, allocate new memory.
   * The first iteration this always occurs -- see ParINIT */
  if (cinfo->maxnrecv < maxnrecv) {
    if (cinfo->incolind) free(cinfo->incolind);
    if (cinfo->invalues) free(cinfo->invalues);
    cinfo->incolind = idx_malloc(maxnrecv*(global_maxnz+2), "ComputeCommInfo: cinfo->incolind");
    cinfo->invalues =  fp_malloc(maxnrecv*(global_maxnz+2), "ComputeCommInfo: cinfo->invalues");
    cinfo->maxnrecv = maxnrecv;
  }
  assert( cinfo->incolind != NULL );
  assert( cinfo->invalues != NULL );

  /* Zero our send buffer */
  for(i=0; i<npes; i++)
    pilu_send[i] = 0;

  /* tell the processors in nbrind what I'm going to send them. */
  for (i=0; i<rnnbr; i++)
    pilu_send[rnbrind[i]] = rnbrptr[i+1]-rnbrptr[i];    /* The # of rows I need */

  MPI_Alltoall( pilu_send, 1, MPI_INT,
		pilu_recv, 1, MPI_INT, pilut_comm );

  nsend = 0;
  snnbr = 0;
  snbrptr[0] = 0;
  for (penum=0; penum<npes; penum++) {
    if (pilu_recv[penum] > 0) {
      nsend += pilu_recv[penum];
      snbrind[snnbr] = penum;
      snbrptr[++snnbr] = nsend;
    }
  }
  cinfo->snnbr = snnbr;

  maxnsend = GlobalSEMax(nsend, pilut_comm);

  /* If memory requirements change, allocate new memory.
   * The first iteration this always occurs -- see ParINIT */
  if (cinfo->maxnsend < maxnsend) {
    if(cinfo->srowind) free(cinfo->srowind);
    cinfo->srowind  = idx_malloc(maxnsend, "ComputeCommInfo: cinfo->srowind");
    cinfo->maxnsend = maxnsend;
  }
  assert( cinfo->srowind  != NULL );
  srowind = cinfo->srowind;

  /* OK, now I go and send the rrowind to the processor */
  for (i=0; i<rnnbr; i++) {
    MPI_Send( rrowind+rnbrptr[i], rnbrptr[i+1]-rnbrptr[i], MPI_INT,
	      rnbrind[i], TAG_Comm_rrowind, pilut_comm );
  }

  /* issue corresponding recieves (assumes buffering) */
  for (i=0; i<snnbr; i++) {
    MPI_Recv( srowind+snbrptr[i], snbrptr[i+1]-snbrptr[i], MPI_INT,
	      snbrind[i], TAG_Comm_rrowind, pilut_comm, &Status ) ;
  }
}


/*************************************************************************
* This function returns what virtual PE the given row idx is located on.
**************************************************************************/
int Idx2PE(int idx,
             hypre_PilutSolverGlobals *globals)
{
  int penum = 0;
  while (idx >= vrowdist[penum+1]) {  /* idx >= lastrow? */
    penum++;
    assert( penum < npes );
  }

  return penum;
}

/*************************************************************************
* This function computes a set that is independant between PEs but may
* contain dependencies within a PE. This variant simply gives rows to
* the lowest PE possible, which creates some load imbalancing between
* the highest and lowest PEs. It also forms the new permutation and
* marks the _local_ rows that are in the set (but not remote rows).
* For historical reasons the set is called a maximal indep. set (MIS).
**************************************************************************/
int SelectSet(ReduceMatType *rmat, CommInfoType *cinfo,
	      int *perm,    int *iperm,
	      int *newperm, int *newiperm,
              hypre_PilutSolverGlobals *globals)
{
  int ir, i, j, k, l, num, midpt, pe;
  int nnz, snnbr;
  int *rcolind, *snbrind, *snbrptr, *srowind;

  PrintLine("SelectSet", globals);

  snnbr    = cinfo->snnbr;
  snbrind  = cinfo->snbrind;
  snbrptr  = cinfo->snbrptr;
  srowind  = cinfo->srowind;

  /* determine local rows that do not have non-zeros on higher numbered PEs. */
  num = 0;
  midpt = (firstrow+lastrow)/2;
  for (ir=0; ir<ntogo; ir++) {
    i = perm[ir+ndone]+firstrow;

    rcolind = rmat->rmat_rcolind[ir];
    nnz     = rmat->rmat_rnz[ir];

    for (j=1; j<nnz; j++) {
      if ((rcolind[j] < firstrow  ||  rcolind[j] >= lastrow)  &&
	  mype > Idx2PE(rcolind[j], globals))
	break ;
    }
    if ( j == nnz ) {    /* passed test; put into set */
      jw[num++] = i;
      map[i]    = 1;     /* local doesn't need info in high bits */
    }
  }

  /* check for asymetries -- the triangular solves depend on the set being block diagonal */
  for (k=0; k<snnbr; k++)
    if (snbrind[k] < mype)
      for (i=snbrptr[k]; i<snbrptr[k+1]; i++)
	for (j=0; j<num; j++)
	  if (srowind[i] == jw[j]) {
	    CheckBounds(firstrow, jw[j], lastrow, globals);
	    map[jw[j]] = 0;
	    jw[j] = jw[--num];
	  }

  /* Compute the new permutation with MIS at beginning */
  j = ndone;
  k = ndone+num;
  for (ir=ndone; ir<lnrows; ir++) {
    l = perm[ir];
    CheckBounds(0, l, lnrows, globals);
    if (map[l+firstrow] == 1) {  /* This is in MIS, put it into ldu */
      CheckBounds(ndone, j, ndone+num, globals);
      newperm[j]  = l;
      newiperm[l] = j++;
    }
    else {
      CheckBounds(ndone+num, k, lnrows, globals);
      newperm[k]  = l;
      newiperm[l] = k++;
    }
  }

#ifndef NDEBUG
  /* DEBUGGING: check map is zero outside of local rows */
  for (i=0; i<firstrow; i++)
    assert(map[i] == 0);
  for (i=lastrow; i<nrows; i++)
    assert(map[i] == 0);
#endif

  return num;
}

/*************************************************************************
* This function sends the factored rows to the appropriate processors. The
* rows are sent in the order of the _new_ MIS permutation. Each PE then
* uses the recieved information to mark _remote_ rows in the MIS. It takes
* as input the factored rows in LDU, the new permutation vectors, and the
* global map with local MIS rows already marked. This also updates the
* rnbrptr[i] to be the actual number of rows recieved from PE rnbrind[i].
* 3/20/98: Bug fix, lengths input to sgatherbuf increased by one to reflect
*   fact that diagonal element is also transmitted. -AJC
**************************************************************************/
void SendFactoredRows(FactorMatType *ldu, CommInfoType *cinfo,
		      int *newperm, int nmis, hypre_PilutSolverGlobals *globals)
{
  int i, j, k, ku, kg, l, penum, snnbr, rnnbr, cnt, inCnt;
  int *snbrind, *rnbrind, *rnbrptr, *sgatherbuf, *incolind;
  int *usrowptr, *uerowptr, *ucolind;
  double *dgatherbuf, *uvalues, *dvalues, *invalues;
  MPI_Status Status; 
  MPI_Request *index_requests, *value_requests ;

  PrintLine("SendFactoredRows", globals);

  snnbr   = cinfo->snnbr;
  snbrind = cinfo->snbrind;

  rnnbr   = cinfo->rnnbr;
  rnbrind = cinfo->rnbrind;
  rnbrptr = cinfo->rnbrptr;

  /* NOTE we cast a (double*) to an (int*) */
  sgatherbuf = (int *)cinfo->gatherbuf;
  dgatherbuf = cinfo->gatherbuf;

  incolind = cinfo->incolind;
  invalues = cinfo->invalues;

  usrowptr = ldu->usrowptr;
  uerowptr = ldu->uerowptr;
  ucolind  = ldu->ucolind;
  uvalues  = ldu->uvalues;
  dvalues  = ldu->dvalues;

  /* Allocate requests */
  index_requests = hypre_TAlloc( MPI_Request, rnnbr );
  value_requests = hypre_TAlloc( MPI_Request, rnnbr );

  /* Issue asynchronous receives for rows from other processors.
     Asynchronous receives needed to avoid overflowing comm buffers. */
  j = 0;
  cnt = (cinfo->maxntogo)*(global_maxnz+2) ;
  for (i=0; i<rnnbr; i++) {
    penum = rnbrind[i];

    MPI_Irecv( incolind+j, cnt, MPI_INT,
	      penum, TAG_Send_colind, pilut_comm, &index_requests[i] );

    MPI_Irecv( invalues+j, cnt, MPI_DOUBLE,
	      penum, TAG_Send_values, pilut_comm, &value_requests[i] );

    j += cnt;
  }

  /* pack the colind for sending*/
  l = 0;
  for (j=ndone; j<ndone+nmis; j++) {
    k = newperm[j];
    CheckBounds(firstrow, k+firstrow, lastrow, globals);
    assert(IsInMIS(map[k+firstrow]));
    CheckBounds(0, uerowptr[k]-usrowptr[k], global_maxnz+1, globals);

    /* sgatherbuf[l++] = uerowptr[k]-usrowptr[k]; */  /* store length */
    /* Bug fix, 3/20/98 */
    sgatherbuf[l++] = uerowptr[k]-usrowptr[k]+1;  /* store length */
    sgatherbuf[l++] = k+firstrow;               /* store row #  */

    for (ku=usrowptr[k], kg=l;   ku<uerowptr[k];   ku++, kg++)
      sgatherbuf[kg] = ucolind[ku];
    l += global_maxnz;
  }

  /* send colind to each neighbor */
  for (i=0; i<snnbr; i++) {
    MPI_Send( sgatherbuf, l, MPI_INT,
	      snbrind[i], TAG_Send_colind, pilut_comm );
  }

  /* pack the values */
  l = 0;
  for (j=ndone; j<ndone+nmis; j++) {
    k = newperm[j];
    CheckBounds(firstrow, k+firstrow, lastrow, globals);
    assert(IsInMIS(map[k+firstrow]));

    l++;                          /* first element undefined */
    dgatherbuf[l++] = dvalues[k]; /* store diagonal */

    for (ku=usrowptr[k], kg=l;   ku<uerowptr[k];   ku++, kg++)
      dgatherbuf[kg] = uvalues[ku];
    l += global_maxnz;
  }

  /* send values to each neighbor */
  for (i=0; i<snnbr; i++) {
    MPI_Send( dgatherbuf, l, MPI_DOUBLE,
	      snbrind[i], TAG_Send_values, pilut_comm );
  }

  /* Finish receiving rows */
  j = 0;
  cnt = (cinfo->maxntogo)*(global_maxnz+2) ;
  for (i=0; i<rnnbr; i++) {
    penum = rnbrind[i];

    MPI_Wait( &index_requests[i], &Status);

    /* save where each row is recieved into the map */
    MPI_Get_count( &Status, MPI_INT, &inCnt );
    rnbrptr[i] = inCnt;
    for (k=0; k<inCnt; k += global_maxnz+2)
      map[incolind[j+k+1]] = ((j+k)<<1) + 1; /* pack MIS flag in LSB */

    MPI_Wait( &value_requests[i], &Status);

    j += cnt;
    CheckBounds(0, j, (cinfo->maxnrecv)*(global_maxnz+2)+2, globals);
  }

  /* clean up memory */
  hypre_TFree(index_requests);
  hypre_TFree(value_requests);
}


/*************************************************************************
* This function creates the new reduce matrix. It takes as input the
* current reduced matrix and the outside nodes sent from other PEs.
* Also both the old permutation (which applies to this rmat) and the new
* permutation (which applies to the new rmat) are taken as input. After
* each row is computed, the number of non-zeros is kept the same.
*
* Note that all fill elements into the L portion mus fill unto the same
* processor as the row being subtracted is, since it is block diagonal.
**************************************************************************/
void ComputeRmat(FactorMatType *ldu, ReduceMatType *rmat,
		 ReduceMatType *nrmat, CommInfoType *cinfo,
		 int *perm,    int *iperm,
		 int *newperm, int *newiperm, int nmis, double tol,
                 hypre_PilutSolverGlobals *globals)
{
  int i, ir, inr, start, j, k, kk, l, m, end, nnz;
  int *usrowptr, *uerowptr, *ucolind, *incolind, *rcolind, rrowlen;
  double *uvalues, *nrm2s, *invalues, *rvalues, *dvalues;
  double mult, nval, rtol;

  PrintLine("ComputeRmat", globals);

  usrowptr = ldu->usrowptr;
  uerowptr = ldu->uerowptr;
  ucolind  = ldu->ucolind;
  uvalues  = ldu->uvalues;
  dvalues  = ldu->dvalues;
  nrm2s    = ldu->nrm2s;

  incolind = cinfo->incolind;
  invalues = cinfo->invalues;

  /* OK, now reduce the remaining rows.
   * inr counts the rows actually factored as an index for the nrmat */
  inr = 0;
  for (ir=ndone+nmis; ir<lnrows; ir++) {
    i = newperm[ir];
    CheckBounds(0, i, lnrows, globals);
    assert(!IsInMIS(map[i+firstrow]));

    rtol = nrm2s[i]*tol;

    /* get the row according to the _previous_ permutation */
    k = iperm[i]-ndone;
    CheckBounds(0, k, ntogo, globals);
    nnz     = rmat->rmat_rnz[k];
    rcolind = rmat->rmat_rcolind[k];
    rvalues = rmat->rmat_rvalues[k];
    rrowlen = rmat->rmat_rrowlen[k];

    /* Initialize workspace and determine the L indices (ie., MIS).
     * The L indices are stored as either the row's new local permutation
     * or the permuted order we recieved the row. The LSB is a flag
     * for being local (==0) or remote (==1). */
    jr[rcolind[0]] = 0;  /* store diagonal first */
    jw[0] = rcolind[0];
     w[0] = rvalues[0];

    lastlr = 0;
    for (lastjr=1; lastjr<nnz; lastjr++) {
      CheckBounds(0, rcolind[lastjr], nrows, globals);

      /* record L elements */
      if (IsInMIS(map[rcolind[lastjr]])) {
	if (rcolind[lastjr] >= firstrow  &&  rcolind[lastjr] < lastrow)
	  lr[lastlr] = (newiperm[rcolind[lastjr]-firstrow] << 1);
	else {
	  lr[lastlr] = map[rcolind[lastjr]];  /* map[] == (l<<1) | 1 */
	  assert(incolind[StripMIS(map[rcolind[lastjr]])+1] == 
                 rcolind[lastjr]);
	}
        lastlr++;
      }

      jr[rcolind[lastjr]] = lastjr;
      jw[lastjr] = rcolind[lastjr];
       w[lastjr] = rvalues[lastjr];
    }
    assert(lastjr == nnz);
    assert(lastjr > 0);

    /* Go through the L nonzeros and pull in the contributions */
    while( lastlr != 0 ) {
      k = ExtractMinLR( globals );

      if ( IsLocal(k) ) {  /* Local node -- row is in DU */
	CheckBounds(0, StripLocal(k), lnrows, globals);
	kk = newperm[ StripLocal(k) ];  /* remove the local bit (LSB) */
	k  = kk+firstrow;

	CheckBounds(0, kk, lnrows, globals);
	CheckBounds(0, jr[k], lastjr, globals);
	assert(jw[jr[k]] == k);

        mult = w[jr[k]]*dvalues[kk];
        w[jr[k]] = mult;

        if (fabs(mult) < rtol)
          continue;	/* First drop test */

        for (l=usrowptr[kk]; l<uerowptr[kk]; l++) {
          CheckBounds(0, ucolind[l], nrows, globals);
          m = jr[ucolind[l]];
          if (m == -1) {
            if (fabs(mult*uvalues[l]) < rtol)
              continue;  /* Don't worry. The fill has too small of a value */

	    /* record L elements -- these must be local */
            if (IsInMIS(map[ucolind[l]])) {
	      assert(ucolind[l] >= firstrow  &&  ucolind[l] < lastrow);
	      lr[lastlr] = (newiperm[ucolind[l]-firstrow] << 1);
	      lastlr++;
	    }

            /* Create fill */
            jr[ucolind[l]] = lastjr;
            jw[lastjr] = ucolind[l];
             w[lastjr] = -mult*uvalues[l];
	    lastjr++;
          }
          else 
            w[m] -= mult*uvalues[l];
        }
      }
      else { /* Outside node -- row is in incolind/invalues */
        start = StripLocal(k);             /* Remove the local bit (LSB) */
        end   = start + incolind[start];   /* get length */
	start++;
	k     = incolind[start];           /* get diagonal colind == row index */

	CheckBounds(0, k, nrows, globals);
	CheckBounds(0, jr[k], lastjr, globals);
	assert(jw[jr[k]] == k);

        mult = w[jr[k]]*invalues[start];
        w[jr[k]] = mult;

        if (fabs(mult) < rtol)
          continue;	/* First drop test */

        for (l=++start; l<=end; l++) {
          CheckBounds(0, incolind[l], nrows, globals);
          m = jr[incolind[l]];
          if (m == -1) {
            if (fabs(mult*invalues[l]) < rtol)
              continue;  /* Don't worry. The fill has too small of a value */

	    /* record L elements -- these must be remote */
            if (IsInMIS(map[incolind[l]])) {
	      assert(incolind[l] < firstrow  ||  incolind[l] >= lastrow);
	      lr[lastlr] = map[incolind[l]];  /* map[] == (l<<1) | 1 */
	      lastlr++;
	    }

            /* Create fill */
            jr[incolind[l]] = lastjr;
            jw[lastjr] = incolind[l];
             w[lastjr] = -mult*invalues[l];
	    lastjr++;
          }
          else 
            w[m] -= mult*invalues[l];
        }
      }
    } /* L non-zeros */

    /* perform SecondDrops and store in appropriate places */
    SecondDropSmall( rtol, globals );
    m = SeperateLU_byMIS( globals);
    UpdateL( i, m, ldu, globals );
    FormNRmat( inr++, m, nrmat, global_maxnz, rrowlen, rcolind, rvalues, globals );
    /* FormNRmat( inr++, m, nrmat, 3*global_maxnz, rcolind, rvalues, globals ); */
  }
}


/*************************************************************************
* This function performs a serial ILUT on the local MIS rows, then calls
* SecondDrop to drop some elements and create LDU. If the set is truly
* independant, then this just puts the row into DU. If there are
* dependencies within a PE this factors those, adding to L, and forms DU.
**************************************************************************/
void FactorLocal(FactorMatType *ldu, ReduceMatType *rmat,
		 ReduceMatType *nrmat, CommInfoType *cinfo,
		 int *perm,    int *iperm,
		 int *newperm, int *newiperm, int nmis, double tol,
                 hypre_PilutSolverGlobals *globals)
{
  int i, ir, j, k, kk, l, m, nnz, diag;
  int *usrowptr, *uerowptr, *ucolind, *rcolind;
  double *uvalues, *nrm2s, *rvalues, *dvalues;
  double mult, nval, rtol;

  PrintLine("FactorLocal", globals);

  assert( rmat  != nrmat    );
  assert( perm  != newperm  );
  assert( iperm != newiperm );

  usrowptr = ldu->usrowptr;
  uerowptr = ldu->uerowptr;
  ucolind  = ldu->ucolind;
  uvalues  = ldu->uvalues;
  dvalues  = ldu->dvalues;
  nrm2s    = ldu->nrm2s;

  /* OK, now factor the nmis rows */
  for (ir=ndone; ir<ndone+nmis; ir++) {
    i = newperm[ir];
    CheckBounds(0, i, lnrows, globals);
    assert(IsInMIS(map[i+firstrow]));

    rtol = nrm2s[i]*tol;  /* Compute relative tolerance */
    diag = newiperm[i];

    /* get the row according to the _previous_ permutation */
    k = iperm[i]-ndone;
    CheckBounds(0, k, ntogo, globals);
    nnz     = rmat->rmat_rnz[k];
    rcolind = rmat->rmat_rcolind[k];
    rvalues = rmat->rmat_rvalues[k];

    /* Initialize workspace and determines the L indices.
     * Since there are only local nodes, we just store the
     * row's new permutation into lr, without any flags. */
    jr[rcolind[0]] = 0;  /* store diagonal first */
    jw[0] = rcolind[0];
     w[0] = rvalues[0];
    assert(jw[0] == i+firstrow);

    lastlr = 0;
    for (lastjr=1; lastjr<nnz; lastjr++) {
      CheckBounds(0, rcolind[lastjr], nrows, globals);

      /* record L elements */
      if (rcolind[lastjr] >= firstrow  &&
	  rcolind[lastjr] <  lastrow   &&
	  newiperm[rcolind[lastjr]-firstrow] < diag) {
	lr[lastlr] = newiperm[rcolind[lastjr]-firstrow];
        lastlr++;
      }

      jr[rcolind[lastjr]] = lastjr;
      jw[lastjr] = rcolind[lastjr];
       w[lastjr] = rvalues[lastjr];
    }

    /* Go through the L nonzeros and pull in the contributions */
    while( lastlr != 0 ) {
      k = ExtractMinLR(globals);

      CheckBounds(0, k, lnrows, globals);
      kk = newperm[ k ];
      k  = kk+firstrow;

      CheckBounds(0, kk, lnrows, globals);
      CheckBounds(0, jr[k], lastjr, globals);
      assert(jw[jr[k]] == k);

      mult = w[jr[k]]*dvalues[kk];
      w[jr[k]] = mult;

      if (fabs(mult) < rtol)
	continue;	/* First drop test */

      for (l=usrowptr[kk]; l<uerowptr[kk]; l++) {
	CheckBounds(0, ucolind[l], nrows, globals);
	m = jr[ucolind[l]];
	if (m == -1) {
	  if (fabs(mult*uvalues[l]) < rtol)
	    continue;  /* Don't worry. The fill has too small of a value */

	  /* record L elements */
	  if (ucolind[l] >= firstrow  &&
	      ucolind[l] <  lastrow   &&
	      newiperm[ucolind[l]-firstrow] < diag) {
	    assert(IsInMIS(map[ucolind[l]]));
	    lr[lastlr] = newiperm[ucolind[l]-firstrow];
	    lastlr++;
	  }

	  /* Create fill */
	  jr[ucolind[l]]  = lastjr;
	  jw[lastjr] = ucolind[l];
	   w[lastjr] = -mult*uvalues[l];
	  lastjr++;
	}
	else 
	  w[m] -= mult*uvalues[l];
      }
    } /* L non-zeros */

    /* perform SecondDrops and store in appropriate places */
    SecondDropSmall( rtol, globals );
    m = SeperateLU_byDIAG( diag, newiperm, globals );
    UpdateL( i, m, ldu, globals );
    FormDU( i, m, ldu, rcolind, rvalues, tol, globals );
  }
}


/*************************************************************************
* This function drops small values from the workspace, and also resets
* the jr[] array to all -1's.
**************************************************************************/
void SecondDropSmall( double rtol,
             hypre_PilutSolverGlobals *globals )
{
  int i;

  /* Reset the jr array. */
  for (i=0; i<lastjr; i++) {
    CheckBounds(0, jw[i], nrows, globals);
    jr[jw[i]] = -1;
  }

  /* Remove any (off-diagonal) elements of the row below the tolerance */
  for (i=1; i<lastjr;) {
    if (fabs(w[i]) < rtol) {
      jw[i] = jw[--lastjr];
       w[i] =  w[lastjr];
    }
    else
      i++;
  }
}



/*****************************************************************
* This function seperates the L and U portions of the workspace
* and returns the point at which they seperate, so
*  L entries are between [1     .. point)
*  U or rmat entries are [point .. lastjr)
* We assume the diagonal D is index [0].
*
* This version compares the (new) permuted order of entries to the
* given permuted order of the row (diag) to determine entries in L.
* This is suitable for local factorizations.
******************************************************************/
int SeperateLU_byDIAG( int diag, int *newiperm,
             hypre_PilutSolverGlobals *globals )
{
  int first, last, itmp;
  double dtmp;

  /* Perform a Qsort type pass to seperate L and U (rmat) entries. */
  if (lastjr == 1)
    last = first = 1;
  else {
    last  = 1;
    first = lastjr-1;
    while (true) {
      while (last < first  &&  /* while (last < first  AND  [last] is in L) */
	       (jw[last] >= firstrow &&
		jw[last] <  lastrow  &&
		newiperm[jw[last]-firstrow] < diag))
        last++;
      while (last < first  &&  /* while (last < first  AND  [first] is not in L) */
	     ! (jw[first] >= firstrow &&
		jw[first] <  lastrow  &&
		newiperm[jw[first]-firstrow] < diag))
        first--;

      if (last < first) {
        SWAP(jw[first], jw[last], itmp);
        SWAP( w[first],  w[last], dtmp);
        last++; first--;
      }

      if (last == first) {
        if ((jw[last] >= firstrow &&  /* if [last] is in L */
	     jw[last] <  lastrow  &&
	     newiperm[jw[last]-firstrow] < diag)) {
          first++;
          last++;
        }
        break;
      }
      else if (last > first) {
        first++;
        break;
      }
    }
  }

#ifndef NDEBUG
  /* DEBUGGING: verify sorting to some extent */
  for (itmp=1; itmp<last; itmp++) {
    assert((jw[itmp] >= firstrow &&   /* [itmp] is in L -- must be MIS */
	    jw[itmp] <  lastrow  &&
	    newiperm[jw[itmp]-firstrow] < diag));
    assert(IsInMIS(map[jw[itmp]]));
  }
  for (itmp=first; itmp<lastjr; itmp++) {
    assert(!(jw[itmp] >= firstrow &&  /* [itmp] is not in L -- may be MIS still */
	     jw[itmp] <  lastrow  &&
	     newiperm[jw[itmp]-firstrow] < diag));
  }
  assert(last == first);
#endif

  return first;
}


/*****************************************************************
* This function seperates the L and U portions of the workspace
* and returns the point at which they seperate, so
*  L entries are between [1     .. point)
*  U or rmat entries are [point .. lastjr)
* We assume the diagonal D is index [0].
*
* This version simply uses the MIS to determine entries in L.
* This is suitable for reductions involving rows on other PEs,
* where -every- row in the MIS will be part of L.
******************************************************************/
int SeperateLU_byMIS( hypre_PilutSolverGlobals *globals )
{
  int first, last, itmp;
  double dtmp;

  /* Perform a Qsort type pass to seperate L and U (rmat) entries. */
  if (lastjr == 1)
    last = first = 1;
  else {
    last  = 1;
    first = lastjr-1;
    while (true) {
      while (last < first  &&    IsInMIS(map[jw[last ]]))  /* and [last] is in L */
        last++;
      while (last < first  &&  ! IsInMIS(map[jw[first]]))  /* and [first] is not in L */
        first--;

      if (last < first) {
        SWAP(jw[first], jw[last], itmp);
        SWAP( w[first],  w[last], dtmp);
        last++; first--;
      }

      if (last == first) {
        if (IsInMIS(map[jw[last]])) {
          first++;
          last++;
        }
        break;
      }
      else if (last > first) {
        first++;
        break;
      }
    }
  }

#ifndef NDEBUG
  /* DEBUGGING: verify sorting to some extent */
  for (itmp=1; itmp<last; itmp++)
    assert(IsInMIS(map[jw[itmp]]));
  for (itmp=first; itmp<lastjr; itmp++)
    assert(!IsInMIS(map[jw[itmp]]));
  assert(last == first);
#endif

  return first;
}


/*************************************************************************
* This function updates the L part of the given row, assuming that the
* workspace has already been split into L and U entries. L may already
* be partially or completely full--this fills it and then starts to
* replace the min value.
**************************************************************************/
void UpdateL(int lrow, int last, FactorMatType *ldu,
             hypre_PilutSolverGlobals *globals)
{
  int i, j, min, start, end;
  int *lcolind;
  double *lvalues;

  lcolind = ldu->lcolind;
  lvalues = ldu->lvalues;

  start = ldu->lsrowptr[lrow];
  end   = ldu->lerowptr[lrow];

  /* The entries between [1, last) are part of L */
  for (i=1; i<last; i++) {
    if (end-start < global_maxnz) {  /* In case we did not have maxnz in L */
      lcolind[end] = jw[i];
      lvalues[end] =  w[i];
      end++;
    }
    else {
      min = start;  /* find min and replace if i is larger */
      for (j=start+1; j<end; j++) {
	if (fabs(lvalues[j]) < fabs(lvalues[min]))
	  min = j;
      }

      if (fabs(lvalues[min]) < fabs(w[i])) {
	lcolind[min] = jw[i];
	lvalues[min] =  w[i];
      }
    }
  }
  ldu->lerowptr[lrow] = end;
  CheckBounds(0, end-start, global_maxnz+1, globals);
}


/*************************************************************************
* This function forms the new reduced row corresponding to 
* the given row, assuming that the
* workspace has already been split into L and U (rmat) entries. It reuses
* the memory for the row in the reduced matrix, storing the new row into
* nrmat->*[rrow].
* New version allows new row to be larger than original row, so it does not
* necessarily reuse the same memory. AC 3-18
**************************************************************************/
void FormNRmat(int rrow, int first, ReduceMatType *nrmat,
               int max_rowlen,
	       int in_rowlen, int *in_colind, double *in_values,
               hypre_PilutSolverGlobals *globals )
{
  int nz, max, j, out_rowlen, *rcolind;
  double *rvalues;

  assert(in_colind[0] == jw[0]);  /* diagonal at the beginning */

  /* check to see if we need to reallocate space */
  out_rowlen = hypre_min( max_rowlen, lastjr-first+1 );
  if( out_rowlen > in_rowlen )
  {
    free_multi( in_colind, in_values, -1 );
    rcolind = idx_malloc( out_rowlen, "FornNRmat: rcolind");
    rvalues = fp_malloc( out_rowlen, "FornNRmat: rvalues");
  }else
  {
    rcolind = in_colind;
    rvalues = in_values;
  }

  rcolind[0] = jw[0];
  rvalues[0] = w[0];

  /* The entries [first, lastjr) are part of U (rmat) */
  if (lastjr-first+1 <= max_rowlen) { /* Simple copy */
    for (nz=1, j=first;   j<lastjr;   nz++, j++) {
      rcolind[nz] = jw[j];
      rvalues[nz] =  w[j];
    }
    assert(nz == lastjr-first+1);
  }
  else { /* Keep largest out_rowlen elements in the reduced row */
    for (nz=1; nz<out_rowlen; nz++) {
      max = first;
      for (j=first+1; j<lastjr; j++) {
	if (fabs(w[j]) > fabs(w[max]))
	  max = j;
      }
      
      rcolind[nz] = jw[max];   /* store max */
      rvalues[nz] =  w[max];
      
      jw[max] = jw[--lastjr];  /* swap max out */
       w[max] =  w[  lastjr];
    }
    assert(nz == out_rowlen);
  }
  assert(nz <= max_rowlen);
  
  /* link the reused storage to the new reduced system */
  nrmat->rmat_rnz[rrow]     = nz;
  nrmat->rmat_rrowlen[rrow] = out_rowlen;
  nrmat->rmat_rcolind[rrow] = rcolind;
  nrmat->rmat_rvalues[rrow] = rvalues;
}



/*************************************************************************
* This function forms the DU part of the given row, assuming that the
* workspace has already been split into L and U entries. It disposes of
* the memory used by the row in the reduced matrix.
**************************************************************************/
void FormDU(int lrow, int first, FactorMatType *ldu,
	    int *rcolind, double *rvalues, double tol,
             hypre_PilutSolverGlobals *globals )
{
  int nz, max, j, end;
  int *ucolind, *uerowptr;
  double *uvalues;

  ucolind  = ldu->ucolind;
  uerowptr = ldu->uerowptr;
  uvalues  = ldu->uvalues;

  /* 
   * Take care of the diagonal
   */
  if (w[0] == 0.0) {
    printf("Zero pivot in row %d, adding e to proceed!\n", lrow);
    ldu->dvalues[lrow] = 1.0/tol;
  }
  else
    ldu->dvalues[lrow] = 1.0/w[0];

  /* 
   * Take care of the elements of U
   * Note U is completely empty beforehand.
   */
  end = ldu->uerowptr[lrow];

  assert(ldu->usrowptr[lrow] == ldu->uerowptr[lrow]);
  for (nz=0; nz<global_maxnz && lastjr>first; nz++) {
    /* The entries [first, lastjr) are part of U */
    max = first;
    for (j=first+1; j<lastjr; j++) {
      if (fabs(w[j]) > fabs(w[max]))
	max = j;
    }

    ucolind[end] = jw[max];  /* store max */
    uvalues[end] =  w[max];
    end++;
    
    jw[max] = jw[--lastjr];  /* swap max out */
     w[max] =  w[  lastjr];
  }
  uerowptr[lrow] = end;

  /* free the row storage */
  free( rcolind );
  free( rvalues );
}


/*************************************************************************
* This function zeros the map for all local rows and rows we recieved.
* During debugging it checks the entire map to ensure other entries remain
* zero as expected. cinfo->rnbrptr[i] has the _actual_ number of rows
* recieved from PE rnbrind[i], which is set in SendFactoredRows.
**************************************************************************/
void EraseMap(CommInfoType *cinfo, int *newperm, int nmis,
             hypre_PilutSolverGlobals *globals)
{
  int i, j, k, cnt, rnnbr;
  int *rnbrptr, *incolind;

  rnnbr    = cinfo->rnnbr;
  rnbrptr  = cinfo->rnbrptr;
  incolind = cinfo->incolind;

  PrintLine("EraseMap", globals);

  /* clear map of all MIS rows */
  for (i=ndone; i<ndone+nmis; i++) 
    map[newperm[i]+firstrow] = 0;

  /* clear map of all recieved rows. see SendFactoredRows code */
  j = 1;  /* row index in [1] */
  cnt = (cinfo->maxntogo)*(global_maxnz+2) ;
  for (i=0; i<rnnbr; i++) {
    for (k=0; k<rnbrptr[i]; k += global_maxnz+2)
      map[incolind[j+k]] = 0;
    j += cnt;
  }

#ifndef NDEBUG
  /* DEBUGGING: check entire map */
  for (i=0; i<nrows; i++)
    if ( map[i] != 0 ) {
      printf("PE %d BAD ERASE %d [%d %d]\n", mype, i, firstrow, lastrow);
      map[i] = 0;
    }
#endif
}


/*************************************************************************
* This function allocates datastructures for the new reduced matrix (nrmat),
* the global workspace, and the communication info. Some parts of the
* comm info are allocated dynamically so we just initialize their size to
* zero here, forcing an allocation the first time ComputeCommInfo is called.
* Comments indicate where in George's code these originally existed.
**************************************************************************/
void ParINIT( ReduceMatType *nrmat, CommInfoType *cinfo, int *rowdist,
              hypre_PilutSolverGlobals *globals )
{
  int i;

  PrintLine("ParINIT", globals);

  /* save a global copy of the row distribution */
  vrowdist = idx_malloc(npes+1, "ParINIT: vrowdist");
  memcpy_idx(vrowdist, rowdist, npes+1);

  /* ---- ParILUT ---- */
  /* Allocate the new rmat */
  nrmat->rmat_rnz     = idx_malloc(ntogo, "ParILUT: nrmat->rmat_rnz"    );
  nrmat->rmat_rrowlen = idx_malloc(ntogo, "ParILUT: nrmat->rmat_rrowlen");
  nrmat->rmat_rcolind = (int **) mymalloc( sizeof(int*)*ntogo, "ParILUT: nrmat->rmat_rcolind");
  nrmat->rmat_rvalues = (double **)  mymalloc( sizeof(double*) *ntogo, "ParILUT: nrmat->rmat_rvalues");

  /* Allocate work space */
  jr = idx_malloc_init(nrows, -1, "ParILUT: jr");
  lr = idx_malloc_init(nleft, -1, "ParILUT: lr");
  jw = idx_malloc(nleft, "ParILUT: jw");
  w  =  fp_malloc(nleft, "ParILUT: w");

  /* ---- ComputeCommInfo ---- */
  /* Allocate global map */
  map = idx_malloc_init(nrows, 0, "ComputeCommInfo: map");

  /* Allocate cinfo */
  cinfo->rnbrind  = idx_malloc(npes,   "ComputeCommInfo: cinfo->rnbrind");
  cinfo->rrowind  = idx_malloc(nleft,  "ComputeCommInfo: cinfo->rrowind");
  cinfo->rnbrptr  = idx_malloc(npes+1, "ComputeCommInfo: cinfo->rnbrptr");
  
  cinfo->snbrind  = idx_malloc(npes,   "ComputeCommInfo: cinfo->snbrind");
  cinfo->snbrptr  = idx_malloc(npes+1, "ComputeCommInfo: cinfo->snbrptr");

  /* force allocates within ComputeCommInfo */
  cinfo->incolind = NULL;
  cinfo->invalues = NULL;
  cinfo->srowind  = NULL;
  cinfo->maxnrecv = 0;
  cinfo->maxnsend = 0;

  /* ---- ComputeMIS ---- */
  cinfo->gatherbuf = fp_malloc(ntogo*(global_maxnz+2), "ComputeMIS: gatherbuf");
}
