/******************************************************************************
 * Copyright 1998-2019 Lawrence Livermore National Security, LLC and other
 * HYPRE Project Developers. See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR MIT)
 ******************************************************************************/

#include "_hypre_parcsr_ls.h"
#include "_hypre_utilities.hpp"

void init_fine_to_coarse( HYPRE_Int n_fine, HYPRE_Int * pass_marker, HYPRE_Int color,
                          HYPRE_Int * fine_to_coarse, HYPRE_Int & n_cpts );

void init_big_convert( HYPRE_Int n_fine, HYPRE_Int * pass_marker, HYPRE_Int color,
                       HYPRE_Int * fine_to_coarse, HYPRE_BigInt cpts, HYPRE_BigInt * big_convert );

void compute_num_cols_offd_fine_to_coarse( HYPRE_Int * pass_marker_offd, HYPRE_Int color,
                                           HYPRE_Int num_cols_offd_A, HYPRE_Int & num_cols_offd,
                                           HYPRE_Int ** fine_to_coarse_offd );

template<typename T>
struct tuple_plus : public thrust::binary_function<thrust::tuple<T,T>, thrust::tuple<T,T>, thrust::tuple<T,T> >
{
  __host__ __device__
  thrust::tuple<T,T> operator()( const thrust::tuple<T,T> & x1, const thrust::tuple<T,T> & x2)
  {
    return thrust::make_tuple( thrust::get<0>(x1)+thrust::get<0>(x2), thrust::get<1>(x1)+thrust::get<1>(x2) );
  }
};

struct TupleMinus : public thrust::unary_function<thrust::tuple<int,int>,int>
  {
    __host__ __device__
    int operator()(thrust::tuple<int,int> & x) const
    {
      int x1 = thrust::get<0>(x);
      int x2 = thrust::get<1>(x);
      return x1-x2;
    };
  };

struct plus_one : public thrust::unary_function<int,int>
  {
    __host__ __device__
    int operator()(int x) const
    {
      return x+1;
    };
  };


__global__
void hypreCUDAKernel_cfmarker_masked_rowsum( HYPRE_Int nrows,
                                             HYPRE_Int *A_diag_i,
                                             HYPRE_Complex *A_diag_data,
                                             HYPRE_Int *A_offd_i,
                                             HYPRE_Complex *A_offd_data,
                                             HYPRE_Int *CF_marker,
                                             HYPRE_Complex *row_sum);

__global__
void hypreCUDAKernel_mutlipass_pi_rowsum( HYPRE_Int      num_points,
                                          HYPRE_Int     *pass_order,
                                          HYPRE_Int     *A_diag_i,
                                          HYPRE_Complex *A_diag_data,
                                          HYPRE_Int     *P_diag_i,
                                          HYPRE_Complex *P_diag_data,
                                          HYPRE_Int     *P_offd_i,
                                          HYPRE_Complex *P_offd_data,
                                          HYPRE_Complex *row_sum );

__global__
void hypreCUDAKernel_mutli_pi_rowsum( HYPRE_Int      num_points,
                                      HYPRE_Int     *pass_order,
                                      HYPRE_Int     *A_diag_i,
                                      HYPRE_Complex *A_diag_data,
                                      HYPRE_Int     *Pi_diag_i,
                                      HYPRE_Complex *Pi_diag_data,
                                      HYPRE_Int     *Pi_offd_i,
                                      HYPRE_Complex *Pi_offd_data,
                                      HYPRE_Complex *w_row_sum );

__global__
void hypreCUDAKernel_generate_Pdiag_i_Poffd_i( HYPRE_Int  num_points,
                                               HYPRE_Int  color,
                                               HYPRE_Int *pass_order,
                                               HYPRE_Int *pass_marker,
                                               HYPRE_Int *pass_marker_offd,
                                               HYPRE_Int *S_diag_i,
                                               HYPRE_Int *S_diag_j,
                                               HYPRE_Int *S_offd_i,
                                               HYPRE_Int *S_offd_j,
                                               HYPRE_Int *P_diag_i,
                                               HYPRE_Int *P_offd_i,
                                               HYPRE_Int * nnz_diag,
                                               HYPRE_Int * nnz_offd );

__global__
void hypreCUDAKernel_generate_Pdiag_j_Poffd_j_count( int num_points,
                                                     int color,
                                                     int  *pass_order,
                                                     int  *pass_marker,
                                                     int  *pass_marker_offd,
                                                     int  *S_diag_i,
                                                     int  *S_diag_j,
                                                     int  *S_offd_i,
                                                     int  *S_offd_j,
                                                     int  *diag_shifts,
                                                     int  *offd_shifts );

__global__
void hypreCUDAKernel_generate_Pdiag_j_Poffd_j( int    num_points,
                                               int    color,
                                               int    *pass_order,
                                               int    *pass_marker,
                                               int    *pass_marker_offd,
                                               int    *fine_to_coarse,
                                               int    *fine_to_coarse_offd,
                                               int    *A_diag_i,
                                               int    *A_diag_j,
                                               double *A_diag_data,
                                               int    *A_offd_i,
                                               int    *A_offd_j,
                                               double *A_offd_data,
                                               int    *Soc_diag_j,
                                               int    *Soc_offd_j,
                                               int    *diag_shifts,
                                               int    *offd_shifts,
                                               int    *P_diag_j,
                                               double *P_diag_data,
                                               int    *P_offd_j,
                                               double *P_offd_data );

__global__
void hypreCUDAKernel_insert_remaining_weights( HYPRE_Int start,
                                               HYPRE_Int stop,
                                               HYPRE_Int  * pass_order,
                                               HYPRE_Int  * Pi_diag_i,
                                               HYPRE_Int  * Pi_diag_j,
                                               HYPRE_Real * Pi_diag_data,
                                               HYPRE_Int  * P_diag_i,
                                               HYPRE_Int  * P_diag_j,
                                               HYPRE_Real * P_diag_data,
                                               HYPRE_Int  * Pi_offd_i,
                                               HYPRE_Int  * Pi_offd_j,
                                               HYPRE_Real * Pi_offd_data,
                                               HYPRE_Int  * P_offd_i,
                                               HYPRE_Int  * P_offd_j,
                                               HYPRE_Real * P_offd_data );

__global__
void hypreCUDAKernel_generate_Qdiag_j_Qoffd_j( int    num_points,
                                               int    color,
                                               int    *pass_order,
                                               int    *pass_marker,
                                               int    *pass_marker_offd,
                                               int    *fine_to_coarse,
                                               int    *fine_to_coarse_offd,
                                               int    *A_diag_i,
                                               int    *A_diag_j,
                                               double *A_diag_data,
                                               int    *A_offd_i,
                                               int    *A_offd_j,
                                               double *A_offd_data,
                                               int    *Soc_diag_j,
                                               int    *Soc_offd_j,
                                               int    *diag_shifts,
                                               int    *offd_shifts,
                                               int    *Q_diag_j,
                                               double *Q_diag_data,
                                               int    *Q_offd_j,
                                               double *Q_offd_data,
                                               double *w_row_sum );

__global__
void hypreCUDAKernel_pass_order_count( int num_points,
                                       int color,
                                       int  *points_left,
                                       int  *pass_marker,
                                       int  *pass_marker_offd,
                                       int  *S_diag_i,
                                       int  *S_diag_j,
                                       int  *S_offd_i,
                                       int  *S_offd_j,
                                       int  *diag_shifts,
                                       int  *points_left_shifts );

__global__
void hypreCUDAKernel_pass_order_pass_marker_update( int remaining_pts,
                                                    int current_pass,
                                                    int * points_left_old,
                                                    int * pass_marker_old,
                                                    int * S_diag_i,
                                                    int * S_diag_j,
                                                    int * S_offd_i,
                                                    int * S_offd_j,
                                                    int * pass_marker_offd,
                                                    int * diag_shifts,
                                                    int * points_left_shifts,
                                                    int * pass_marker,
                                                    int * pass_order,
                                                    int * points_left );

/*--------------------------------------------------------------------------
 * hypre_ParAMGBuildModMultipass
 * This routine implements Stuben's direct interpolation with multiple passes.
 * expressed with matrix matrix multiplications
 *--------------------------------------------------------------------------*/

HYPRE_Int
hypre_BoomerAMGBuildModMultipassDevice( hypre_ParCSRMatrix  *A,
                                        HYPRE_Int           *CF_marker,
                                        hypre_ParCSRMatrix  *S,
                                        HYPRE_BigInt        *num_cpts_global,
                                        HYPRE_Real           trunc_factor,
                                        HYPRE_Int            P_max_elmts,
                                        HYPRE_Int            interp_type,
                                        HYPRE_Int            num_functions,
                                        HYPRE_Int           *dof_func,
                                        hypre_ParCSRMatrix **P_ptr )
{
#ifdef HYPRE_PROFILE
   hypre_profile_times[HYPRE_TIMER_ID_MULTIPASS_INTERP] -= hypre_MPI_Wtime();
#endif

   hypre_assert( hypre_ParCSRMatrixMemoryLocation(A) == HYPRE_MEMORY_DEVICE );
   hypre_assert( hypre_ParCSRMatrixMemoryLocation(S) == HYPRE_MEMORY_DEVICE );

   MPI_Comm                comm = hypre_ParCSRMatrixComm(A);
   hypre_ParCSRCommPkg    *comm_pkg = hypre_ParCSRMatrixCommPkg(A);
   hypre_ParCSRCommHandle *comm_handle;

   hypre_CSRMatrix *A_diag = hypre_ParCSRMatrixDiag(A);
   hypre_assert( hypre_CSRMatrixMemoryLocation(A_diag) == HYPRE_MEMORY_DEVICE );

   HYPRE_Real      *A_diag_data_dev = hypre_CSRMatrixData(A_diag);
   HYPRE_Int       *A_diag_i_dev = hypre_CSRMatrixI(A_diag);
   //HYPRE_Int       *A_diag_j_dev = hypre_CSRMatrixJ(A_diag);

   HYPRE_Int        n_fine = hypre_CSRMatrixNumRows(A_diag);

   hypre_CSRMatrix *A_offd = hypre_ParCSRMatrixOffd(A);
   hypre_assert( hypre_CSRMatrixMemoryLocation(A_offd) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *A_offd_i_dev = hypre_CSRMatrixI(A_offd);
   //HYPRE_Int       *A_offd_j_dev = hypre_CSRMatrixJ(A_offd);
   HYPRE_Real      *A_offd_data_dev = hypre_CSRMatrixData(A_offd);

   HYPRE_Int        num_cols_offd_A = hypre_CSRMatrixNumCols(A_offd);

   hypre_CSRMatrix *S_diag = hypre_ParCSRMatrixDiag(S);
   hypre_assert( hypre_CSRMatrixMemoryLocation(S_diag) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *S_diag_i_dev = hypre_CSRMatrixI(S_diag);
   HYPRE_Int       *S_diag_j_dev = hypre_CSRMatrixJ(S_diag);

   HYPRE_Int       *S_diag_i = NULL;
   HYPRE_Int       *S_diag_j = NULL;

   hypre_CSRMatrix *S_offd = hypre_ParCSRMatrixOffd(S);
   hypre_assert( hypre_CSRMatrixMemoryLocation(S_offd) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *S_offd_i_dev = hypre_CSRMatrixI(S_offd);
   HYPRE_Int       *S_offd_j_dev = hypre_CSRMatrixJ(S_offd);

   HYPRE_Int       *S_offd_i = NULL;
   HYPRE_Int       *S_offd_j = NULL;

   hypre_ParCSRMatrix **Pi;
   hypre_ParCSRMatrix *P;
   hypre_CSRMatrix *P_diag;
   HYPRE_Real      *P_diag_data_dev;
   HYPRE_Int       *P_diag_i_dev; /*at first counter of nonzero cols for each row,
                                      finally will be pointer to start of row */
   HYPRE_Int       *P_diag_j_dev;

   hypre_CSRMatrix *P_offd;
   HYPRE_Real      *P_offd_data_dev = NULL;
   HYPRE_Int       *P_offd_i_dev; /*at first counter of nonzero cols for each row,
                                      finally will be pointer to start of row */

   HYPRE_Int       *P_offd_i;
   HYPRE_Int       *P_offd_j;

   HYPRE_Int       *P_offd_j_dev = NULL;
   HYPRE_BigInt    *col_map_offd_P = NULL;
   HYPRE_Int        num_cols_offd_P = 0;

   HYPRE_Int        num_sends = 0;
   HYPRE_Int       *int_buf_data = NULL;

   HYPRE_Int       *points_left;
   HYPRE_Int       *pass_marker;
   HYPRE_Int       *pass_marker_offd = NULL;
   HYPRE_Int       *pass_order;
   HYPRE_Int       *pass_starts;

   HYPRE_Int       *fine_to_coarse_dev;
   HYPRE_Int       *points_left_dev;
   HYPRE_Int       *pass_marker_dev;
   HYPRE_Int       *pass_marker_offd_dev = NULL;
   HYPRE_Int       *pass_order_dev;
   HYPRE_Int       *CF_marker_dev;

   HYPRE_Int        i, j, i1, j1;
   HYPRE_Int        num_passes, p, remaining;
   HYPRE_Int        global_remaining;
   HYPRE_Int        cnt, cnt_old, cnt_rem, current_pass;
   HYPRE_Int        startc, index;

   HYPRE_BigInt     total_global_cpts;
   HYPRE_Int        my_id, num_procs;
   HYPRE_Int        P_offd_size = 0;

   HYPRE_Int       *dof_func_offd = NULL;
   HYPRE_Real      *row_sums = NULL;


   /* MPI size and rank*/
   hypre_MPI_Comm_size(comm, &num_procs);
   hypre_MPI_Comm_rank(comm, &my_id);

   if (num_procs > 1)
   {
      if (my_id == num_procs - 1)
      {
         total_global_cpts = num_cpts_global[1];
      }
      hypre_MPI_Bcast(&total_global_cpts, 1, HYPRE_MPI_BIG_INT, num_procs-1, comm);
   }
   else
   {
      total_global_cpts = num_cpts_global[1];
   }


   /* Generate pass marker array */
   //FIXME: Remove _dev suffix when we're done.
   pass_marker_dev = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);
   /* contains pass numbers for each variable according to original order */
   pass_order_dev = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);
   /* contains row numbers according to new order, pass 1 followed by pass 2 etc */
   fine_to_coarse_dev = hypre_TAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);
   /* reverse of pass_order, keeps track where original numbers go */
   points_left_dev = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);
   /* contains row numbers of remaining points, auxiliary */

   P_diag_i_dev = hypre_CTAlloc(HYPRE_Int, n_fine+1, HYPRE_MEMORY_DEVICE);
   P_offd_i_dev = hypre_CTAlloc(HYPRE_Int, n_fine+1, HYPRE_MEMORY_DEVICE);

   // Copy CF_marker to dev
   //FIXME: Assuming this is on the host, we should do something better
   CF_marker_dev = hypre_TAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);
   hypre_TMemcpy( CF_marker_dev, CF_marker, HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE, HYPRE_MEMORY_HOST);

   cnt = 0;
   remaining = 0;

   HYPRE_Int * points_end = HYPRE_THRUST_CALL( copy_if,
                                               thrust::make_counting_iterator(0),
                                               thrust::make_counting_iterator(n_fine),
                                               CF_marker_dev,
                                               points_left_dev,
                                               not_equal<HYPRE_Int>(1) );

   HYPRE_Int * pass_end = HYPRE_THRUST_CALL( copy_if,
                                             thrust::make_counting_iterator(0),
                                             thrust::make_counting_iterator(n_fine),
                                             CF_marker_dev,
                                             pass_order_dev,
                                             equal<HYPRE_Int>(1) );

   remaining = points_end - points_left_dev;
   cnt = pass_end - pass_order_dev;


   HYPRE_THRUST_CALL( replace_if,
                      thrust::make_zip_iterator( thrust::make_tuple(pass_marker_dev, P_diag_i_dev+1, P_offd_i_dev+1) ),
                      thrust::make_zip_iterator( thrust::make_tuple(pass_marker_dev+n_fine, P_diag_i_dev+n_fine+1, P_offd_i_dev+n_fine+1) ),
                      thrust::make_zip_iterator( thrust::make_tuple(CF_marker_dev,CF_marker_dev,CF_marker_dev) ),
                      equal<thrust::tuple<HYPRE_Int,HYPRE_Int,HYPRE_Int>>(thrust::make_tuple(1,1,1)),
                      thrust::make_tuple((HYPRE_Int)1,(HYPRE_Int)1,(HYPRE_Int)0) );

   HYPRE_THRUST_CALL( exclusive_scan,
                      thrust::make_transform_iterator(CF_marker_dev,equal<HYPRE_Int>(1)),
                      thrust::make_transform_iterator(CF_marker_dev + n_fine,equal<HYPRE_Int>(1)),
                      fine_to_coarse_dev,
                      (HYPRE_Int)0 );

   pass_starts = hypre_CTAlloc(HYPRE_Int, 10, HYPRE_MEMORY_HOST);
   /* contains beginning for each pass in pass_order field, assume no more than 10 passes */

   pass_starts[0] = 0;
   pass_starts[1] = cnt;

   if (num_functions > 1)
   {
      dof_func_offd = hypre_CTAlloc(HYPRE_Int,  num_cols_offd_A, HYPRE_MEMORY_HOST);
      index = 0;
      num_sends = hypre_ParCSRCommPkgNumSends(comm_pkg);
      int_buf_data = hypre_CTAlloc(HYPRE_Int,  hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends), HYPRE_MEMORY_HOST);
      for (i = 0; i < num_sends; i++)
      {
         startc = hypre_ParCSRCommPkgSendMapStart(comm_pkg, i);
         for (j = startc; j < hypre_ParCSRCommPkgSendMapStart(comm_pkg, i+1); j++)
         {
            int_buf_data[index++] = dof_func[hypre_ParCSRCommPkgSendMapElmt(comm_pkg,j)];
         }
      }

      comm_handle = hypre_ParCSRCommHandleCreate( 11, comm_pkg, int_buf_data, dof_func_offd);

      hypre_ParCSRCommHandleDestroy(comm_handle);

      hypre_TFree(int_buf_data, HYPRE_MEMORY_HOST);
   }

   if (num_cols_offd_A)
   {
     num_sends = hypre_ParCSRCommPkgNumSends(comm_pkg);

     int_buf_data = hypre_CTAlloc(HYPRE_Int,  hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends), HYPRE_MEMORY_DEVICE);

     HYPRE_THRUST_CALL( gather,
                        hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg),
                        hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg) +
                        hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends),
                        pass_marker_dev,
                        int_buf_data );

      pass_marker_offd_dev = hypre_CTAlloc(HYPRE_Int,  num_cols_offd_A, HYPRE_MEMORY_DEVICE);

      /* create a handle to start communication. 11: for integer */
      comm_handle = hypre_ParCSRCommHandleCreate_v2(11, comm_pkg, HYPRE_MEMORY_DEVICE, int_buf_data, HYPRE_MEMORY_DEVICE, pass_marker_offd_dev);

      /* destroy the handle to finish communication */
      hypre_ParCSRCommHandleDestroy(comm_handle);

      // FIXME: Remove this when pass_marker_offd usage completely moved to the GPU
      pass_marker_offd = hypre_CTAlloc(HYPRE_Int,  num_cols_offd_A, HYPRE_MEMORY_HOST);
      hypre_TMemcpy( pass_marker_offd, pass_marker_offd_dev, HYPRE_Int, num_cols_offd_A, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
   }

   current_pass = 1;
   num_passes = 1;
   /* color points according to pass number */
   hypre_MPI_Allreduce(&remaining, &global_remaining, 1, HYPRE_MPI_INT, hypre_MPI_MAX, comm);

   HYPRE_Int * points_left_old = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);
   HYPRE_Int * pass_marker_old = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);

   while (global_remaining > 0)
   {
      HYPRE_Int remaining_pts = remaining;

      cnt_rem = 0;
      cnt_old = cnt;
      {
        dim3 bDim = hypre_GetDefaultCUDABlockDimension();
        dim3 gDim = hypre_GetDefaultCUDAGridDimension(remaining_pts, "warp", bDim);

        HYPRE_Int * diag_shifts        = hypre_CTAlloc(HYPRE_Int, remaining_pts, HYPRE_MEMORY_DEVICE);
        HYPRE_Int * points_left_shifts = hypre_CTAlloc(HYPRE_Int, remaining_pts, HYPRE_MEMORY_DEVICE);

        hypreCUDAKernel_pass_order_count<<<gDim,bDim>>>( remaining_pts,
                                                         current_pass,
                                                         points_left_dev,
                                                         pass_marker_dev,
                                                         pass_marker_offd_dev,
                                                         S_diag_i_dev,
                                                         S_diag_j_dev,
                                                         S_offd_i_dev,
                                                         S_offd_j_dev,
                                                         diag_shifts,
                                                         points_left_shifts );

        auto cnt_reduce = HYPRE_THRUST_CALL( reduce,
                                             thrust::make_zip_iterator( thrust::make_tuple(diag_shifts, points_left_shifts) ),
                                             thrust::make_zip_iterator( thrust::make_tuple(diag_shifts+remaining_pts, points_left_shifts+remaining_pts) ),
                                             thrust::tuple<HYPRE_Int,HYPRE_Int>(cnt_old,0),
                                             tuple_plus<HYPRE_Int>() );

        cnt     = thrust::get<0>(cnt_reduce);
        cnt_rem = thrust::get<1>(cnt_reduce);

        HYPRE_THRUST_CALL( exclusive_scan,
                           thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, points_left_shifts) ),
                           thrust::make_zip_iterator(thrust::make_tuple(diag_shifts+remaining_pts, points_left_shifts+remaining_pts) ),
                           thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, points_left_shifts) ),
                           thrust::tuple<HYPRE_Int,HYPRE_Int>(cnt_old,0),
                           tuple_plus<HYPRE_Int>() );


        hypre_TMemcpy( points_left_old, points_left_dev, HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE, HYPRE_MEMORY_DEVICE);
        hypre_TMemcpy( pass_marker_old, pass_marker_dev, HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE, HYPRE_MEMORY_DEVICE);

        hypreCUDAKernel_pass_order_pass_marker_update<<<gDim,bDim>>>( remaining_pts,
                                                                      current_pass,
                                                                      points_left_old,
                                                                      pass_marker_old,
                                                                      S_diag_i_dev,
                                                                      S_diag_j_dev,
                                                                      S_offd_i_dev,
                                                                      S_offd_j_dev,
                                                                      pass_marker_offd_dev,
                                                                      diag_shifts,
                                                                      points_left_shifts,
                                                                      pass_marker_dev,
                                                                      pass_order_dev,
                                                                      points_left_dev );

        hypre_TFree(diag_shifts, HYPRE_MEMORY_DEVICE);
        hypre_TFree(points_left_shifts, HYPRE_MEMORY_DEVICE);
      }

      remaining = cnt_rem;
      current_pass++;
      num_passes++;
      if (num_passes > 9)
      {
         hypre_error_w_msg(HYPRE_ERROR_GENERIC," Warning!!! too many passes! out of range!\n");
         break;
      }
      pass_starts[num_passes] = cnt;

      /* update pass_marker_offd */
      if (num_cols_offd_A)
      {
        HYPRE_THRUST_CALL( gather,
                           hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg),
                           hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg) +
                           hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends),
                           pass_marker_dev,
                           int_buf_data );

        /* create a handle to start communication. 11: for integer */
        comm_handle = hypre_ParCSRCommHandleCreate_v2(11, comm_pkg, HYPRE_MEMORY_DEVICE, int_buf_data, HYPRE_MEMORY_DEVICE, pass_marker_offd_dev);

        /* destroy the handle to finish communication */
        hypre_ParCSRCommHandleDestroy(comm_handle);

        // FIXME: Remove this when pass_marker_offd usage completely moved to the GPU
        hypre_TMemcpy( pass_marker_offd, pass_marker_offd_dev, HYPRE_Int, num_cols_offd_A, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
      }

      hypre_MPI_Allreduce(&remaining, &global_remaining, 1, HYPRE_MPI_INT, hypre_MPI_MAX, comm);

   } // while (global_remaining > 0)

   hypre_TFree(points_left_old,HYPRE_MEMORY_DEVICE);
   hypre_TFree(pass_marker_old,HYPRE_MEMORY_DEVICE);

   hypre_TFree(int_buf_data, HYPRE_MEMORY_DEVICE);
   hypre_TFree(points_left_dev, HYPRE_MEMORY_DEVICE);// FIXME: Clean up when done

   /* generate row sum of weak points and C-points to be ignored */
   row_sums = hypre_CTAlloc(HYPRE_Real, n_fine, HYPRE_MEMORY_DEVICE);

   if (num_functions >  1)
   {
     hypre_error_w_msg(HYPRE_ERROR_GENERIC,"Sorry, this code not yet ported to the GPU!");

     /*
     HYPRE_Real *row_sums_host = hypre_CTAlloc(HYPRE_Real, n_fine, HYPRE_MEMORY_HOST);
     hypre_TMemcpy( row_sums_host, row_sums, HYPRE_Real, n_fine, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
      for (i=0; i < n_fine; i++)
      {
         if (CF_marker[i] < 0)
         {
            for (j = A_diag_i[i]+1; j < A_diag_i[i+1]; j++)
            {
               if (dof_func[i] == dof_func[A_diag_j[j]])
               {
                  row_sums_host[i] += A_diag_data[j];
               }
            }
            for (j = A_offd_i[i]; j < A_offd_i[i+1]; j++)
            {
               if (dof_func[i] == dof_func_offd[A_offd_j[j]])
               {
                   row_sums_host[i] += A_offd_data[j];
               }
            }
         }
      }
      hypre_TMemcpy( row_sums, row_sums_host, HYPRE_Real, n_fine, HYPRE_MEMORY_DEVICE, HYPRE_MEMORY_HOST);
      hypre_TFree(row_sums_host, HYPRE_MEMORY_HOST);
     */
   }
   else
   {
     dim3 bDim = hypre_GetDefaultCUDABlockDimension();
     dim3 gDim = hypre_GetDefaultCUDAGridDimension(n_fine, "warp", bDim);

     HYPRE_CUDA_LAUNCH( hypreCUDAKernel_cfmarker_masked_rowsum, gDim, bDim,
                        n_fine, A_diag_i_dev, A_diag_data_dev, A_offd_i_dev, A_offd_data_dev,
                        CF_marker_dev, row_sums );
   }


     Pi = hypre_CTAlloc(hypre_ParCSRMatrix*, num_passes, HYPRE_MEMORY_HOST);
#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)
   hypre_GpuProfilingPushRange("MultipassPiDevice");
#endif
   hypre_GenerateMultipassPiDevice(A, S, num_cpts_global, &pass_order_dev[pass_starts[1]],
                                   pass_marker_dev, pass_marker_offd_dev,
                                   pass_starts[2]-pass_starts[1], 1, row_sums, &Pi[0]);
#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)
   hypre_GpuProfilingPopRange();
#endif

   if (interp_type == 8)
   {
      for (i=1; i<num_passes-1; i++)
      {
         hypre_ParCSRMatrix *Q;
         HYPRE_BigInt *c_pts_starts = hypre_ParCSRMatrixRowStarts(Pi[i-1]);
#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)
         hypre_GpuProfilingPushRange(std::string("MultipassPiDevice Loop"+std::to_string(i)).c_str());
#endif
         hypre_GenerateMultipassPiDevice(A, S, c_pts_starts, &pass_order_dev[pass_starts[i+1]],
                                         pass_marker_dev, pass_marker_offd_dev,
                                         pass_starts[i+2]-pass_starts[i+1], i+1, row_sums, &Q);
#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)
   hypre_GpuProfilingPopRange();
#endif
         Pi[i] = hypre_ParCSRMatMat(Q, Pi[i-1]);

         hypre_ParCSRMatrixOwnsRowStarts(Q)=0;
         hypre_ParCSRMatrixOwnsRowStarts(Pi[i])=1;
         hypre_ParCSRMatrixDestroy(Q);
      }
   }
   else if (interp_type == 9)
   {
      for (i=1; i<num_passes-1; i++)
      {
         HYPRE_BigInt *c_pts_starts = hypre_ParCSRMatrixRowStarts(Pi[i-1]);
#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)
         hypre_GpuProfilingPushRange(std::string("MultiPiDevice Loop"+std::to_string(i)).c_str());
#endif
         hypre_GenerateMultiPiDevice(A, S, Pi[i-1], c_pts_starts, &pass_order_dev[pass_starts[i+1]],
                                     pass_marker_dev, pass_marker_offd_dev,
                                     pass_starts[i+2]-pass_starts[i+1], i+1,
                                     num_functions, dof_func, dof_func_offd, &Pi[i] );
#if defined(HYPRE_USING_CUDA) || defined(HYPRE_USING_HIP)
   hypre_GpuProfilingPopRange();
#endif
      }
   }

   // We don't need the row sums anymore
   hypre_TFree (row_sums, HYPRE_MEMORY_DEVICE);

   /* populate P_diag_i[i+1] with nnz of i-th row */
   for (i = 0; i < num_passes-1; i++)
   {
     /* Old host code:
      j1 = 0;
      for (j=pass_starts[i+1]; j < pass_starts[i+2]; j++)
      {
         i1 = pass_order[j];
         P_diag_i[i1+1] = Pi_diag_i[j1+1]-Pi_diag_i[j1];
         P_offd_i[i1+1] = Pi_offd_i[j1+1]-Pi_offd_i[j1];
         j1++;
      }
     */

      HYPRE_Int *Pi_diag_i = hypre_CSRMatrixI(hypre_ParCSRMatrixDiag(Pi[i]));
      HYPRE_Int *Pi_offd_i = hypre_CSRMatrixI(hypre_ParCSRMatrixOffd(Pi[i]));

      HYPRE_Int start = pass_starts[i+1];
      HYPRE_Int stop  = pass_starts[i+2];

      //PB: Thrust doesn't like having a zip_iterator for the output here
      //    It wants it to be a RandomAccessIterator "model" (for which
      //    I'm having a hard time finding documentation). If we could
      //    overcome that, then we ought to be able to "zip" (i.e. fuse)
      //    these two scatters together.
      HYPRE_THRUST_CALL( scatter,
                         thrust::make_transform_iterator( thrust::make_zip_iterator(thrust::make_tuple(Pi_diag_i+1, Pi_diag_i) ), TupleMinus()),
                         thrust::make_transform_iterator( thrust::make_zip_iterator(thrust::make_tuple(Pi_diag_i+1+(stop-start), Pi_diag_i+(stop-start)) ), TupleMinus()),
                         thrust::make_transform_iterator(pass_order_dev+start, plus_one()),
                         P_diag_i_dev );

      HYPRE_THRUST_CALL( scatter,
                         thrust::make_transform_iterator( thrust::make_zip_iterator(thrust::make_tuple(Pi_offd_i+1, Pi_offd_i) ), TupleMinus()),
                         thrust::make_transform_iterator( thrust::make_zip_iterator(thrust::make_tuple(Pi_offd_i+1+(stop-start), Pi_offd_i+(stop-start)) ), TupleMinus()),
                         thrust::make_transform_iterator(pass_order_dev+start, plus_one()),
                         P_offd_i_dev );
   }


   /*
   for (i=0; i < n_fine; i++)
   {
      P_diag_i[i+1] += P_diag_i[i];
      P_offd_i[i+1] += P_offd_i[i];
   }
   */
   HYPRE_THRUST_CALL( inclusive_scan,
                      thrust::make_zip_iterator( thrust::make_tuple(P_diag_i_dev,P_offd_i_dev) ),
                      thrust::make_zip_iterator( thrust::make_tuple(P_diag_i_dev+n_fine+1,P_offd_i_dev+n_fine+1) ),
                      thrust::make_zip_iterator( thrust::make_tuple(P_diag_i_dev,P_offd_i_dev) ),
                      tuple_plus<int>());

   // FIXME: Clean this up

   HYPRE_Int P_diag_size;
   hypre_TMemcpy( &P_diag_size, &P_diag_i_dev[n_fine], HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

   hypre_TMemcpy( &P_offd_size, &P_offd_i_dev[n_fine], HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);


   P_diag_j_dev = hypre_CTAlloc(HYPRE_Int, P_diag_size, HYPRE_MEMORY_DEVICE);
   P_diag_data_dev = hypre_CTAlloc(HYPRE_Real, P_diag_size, HYPRE_MEMORY_DEVICE);
   P_offd_j_dev = hypre_CTAlloc(HYPRE_Int, P_offd_size, HYPRE_MEMORY_DEVICE);
   P_offd_data_dev = hypre_CTAlloc(HYPRE_Real, P_offd_size, HYPRE_MEMORY_DEVICE);

   /* insert weights for coarse points */
   {
     /* Old Host code:
        for (i=0; i < pass_starts[1]; i++)
        {
        i1 = pass_order[i];
        j = P_diag_i[i1];
        P_diag_j[j] = fine_to_coarse[i1];
        P_diag_data[j] = 1.0;
        }
     */
     HYPRE_THRUST_CALL( scatter,
                        thrust::make_permutation_iterator( fine_to_coarse_dev, pass_order_dev ),
                        thrust::make_permutation_iterator( fine_to_coarse_dev+pass_starts[1], pass_order_dev+pass_starts[1] ),
                        thrust::make_permutation_iterator( P_diag_i_dev, pass_order_dev ),
                        P_diag_j_dev );

     HYPRE_THRUST_CALL( scatter,
                        thrust::make_constant_iterator<HYPRE_Real>(1.0),
                        thrust::make_constant_iterator<HYPRE_Real>(1.0)+pass_starts[1],
                        thrust::make_permutation_iterator( P_diag_i_dev, pass_order_dev ),
                        P_diag_data_dev );
   }


   /* generate col_map_offd_P by combining all col_map_offd_Pi
    * and reompute indices if needed */

   /* insert remaining weights */
   for (p=0; p < num_passes-1; p++)
   {
      HYPRE_Int *Pi_diag_i = hypre_CSRMatrixI(hypre_ParCSRMatrixDiag(Pi[p]));
      HYPRE_Int *Pi_offd_i = hypre_CSRMatrixI(hypre_ParCSRMatrixOffd(Pi[p]));
      HYPRE_Int *Pi_diag_j = hypre_CSRMatrixJ(hypre_ParCSRMatrixDiag(Pi[p]));
      HYPRE_Int *Pi_offd_j = hypre_CSRMatrixJ(hypre_ParCSRMatrixOffd(Pi[p]));
      HYPRE_Real *Pi_diag_data = hypre_CSRMatrixData(hypre_ParCSRMatrixDiag(Pi[p]));
      HYPRE_Real *Pi_offd_data = hypre_CSRMatrixData(hypre_ParCSRMatrixOffd(Pi[p]));

      HYPRE_Int num_points = pass_starts[p+2] - pass_starts[p+1];

      dim3 bDim = hypre_GetDefaultCUDABlockDimension();
      dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

      HYPRE_CUDA_LAUNCH( hypreCUDAKernel_insert_remaining_weights, gDim, bDim,
                         pass_starts[p+1],  pass_starts[p+2],
                         pass_order_dev, Pi_diag_i, Pi_diag_j, Pi_diag_data,
                         P_diag_i_dev, P_diag_j_dev, P_diag_data_dev,
                         Pi_offd_i, Pi_offd_j, Pi_offd_data,
                         P_offd_i_dev, P_offd_j_dev, P_offd_data_dev );
   }


   /* Note that col indices in P_offd_j probably not consistent,
      this gets fixed after truncation */

   P = hypre_ParCSRMatrixCreate(comm,
                                hypre_ParCSRMatrixGlobalNumRows(A),
                                total_global_cpts,
                                hypre_ParCSRMatrixRowStarts(A),
                                num_cpts_global,
                                num_cols_offd_P,
                                P_diag_size,
                                P_offd_size);
   P_diag = hypre_ParCSRMatrixDiag(P);
   hypre_CSRMatrixData(P_diag) = P_diag_data_dev;
   hypre_CSRMatrixI(P_diag) = P_diag_i_dev;
   hypre_CSRMatrixJ(P_diag) = P_diag_j_dev;
   P_offd = hypre_ParCSRMatrixOffd(P);
   hypre_CSRMatrixData(P_offd) = P_offd_data_dev;
   hypre_CSRMatrixI(P_offd) = P_offd_i_dev;
   hypre_CSRMatrixJ(P_offd) = P_offd_j_dev;
   hypre_ParCSRMatrixOwnsRowStarts(P) = 0;

   /* Compress P, removing coefficients smaller than trunc_factor * Max */

   if (trunc_factor != 0.0 || P_max_elmts > 0)
   {
      hypre_BoomerAMGInterpTruncationDevice(P, trunc_factor, P_max_elmts);
      P_diag_data_dev = hypre_CSRMatrixData(P_diag);
      P_diag_i_dev = hypre_CSRMatrixI(P_diag);
      P_diag_j_dev = hypre_CSRMatrixJ(P_diag);
      P_offd_data_dev = hypre_CSRMatrixData(P_offd);
      P_offd_i_dev = hypre_CSRMatrixI(P_offd);
      P_offd_j_dev = hypre_CSRMatrixJ(P_offd);
   }

   num_cols_offd_P = 0;

   if (P_offd_size)
     {
       // FIXME: Clean this up
       P_offd_i = hypre_CTAlloc(HYPRE_Int, n_fine+1, HYPRE_MEMORY_HOST);
      hypre_TMemcpy( P_offd_i, P_offd_i_dev, HYPRE_Int, n_fine+1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
      P_offd_j = hypre_CTAlloc(HYPRE_Int, P_offd_size, HYPRE_MEMORY_HOST);
      hypre_TMemcpy( P_offd_j, P_offd_j_dev, HYPRE_Int, P_offd_size, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

      pass_order = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_HOST);
      hypre_TMemcpy( pass_order, pass_order_dev, HYPRE_Int, n_fine, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

      HYPRE_BigInt *tmp_P_offd_j = hypre_CTAlloc(HYPRE_BigInt, P_offd_size, HYPRE_MEMORY_HOST);
      HYPRE_BigInt *big_P_offd_j = hypre_CTAlloc(HYPRE_BigInt, P_offd_size, HYPRE_MEMORY_HOST);
      for (p=0; p < num_passes-1; p++)
      {
         HYPRE_BigInt *col_map_offd_Pi = hypre_ParCSRMatrixColMapOffd(Pi[p]);
         for (i = pass_starts[p+1]; i < pass_starts[p+2]; i++)
         {
            i1 = pass_order[i];
            for (j = P_offd_i[i1]; j < P_offd_i[i1+1]; j++)
            {
               big_P_offd_j[j] = col_map_offd_Pi[P_offd_j[j]];
            }
         }
      }

      for (i=0; i < P_offd_size; i++)
      {
         tmp_P_offd_j[i] = big_P_offd_j[i];
      }

      hypre_BigQsort0(tmp_P_offd_j, 0, P_offd_size-1);

      num_cols_offd_P = 1;
      for (i=0; i < P_offd_size-1; i++)
      {
          if (tmp_P_offd_j[i+1] > tmp_P_offd_j[i])
          {
             tmp_P_offd_j[num_cols_offd_P++] = tmp_P_offd_j[i+1];
          }
      }

      col_map_offd_P = hypre_CTAlloc(HYPRE_BigInt, num_cols_offd_P, HYPRE_MEMORY_HOST);

      for (i=0; i < num_cols_offd_P; i++)
      {
         col_map_offd_P[i] = tmp_P_offd_j[i];
      }

      for (i=0; i < P_offd_size; i++)
      {
         P_offd_j[i] = hypre_BigBinarySearch(col_map_offd_P,
               big_P_offd_j[i],
               num_cols_offd_P);
      }
      hypre_TFree(tmp_P_offd_j, HYPRE_MEMORY_HOST);
      hypre_TFree(big_P_offd_j, HYPRE_MEMORY_HOST);

      // FIXME: Clean this up
      hypre_TMemcpy( P_offd_j_dev, P_offd_j, HYPRE_Int, P_offd_size, HYPRE_MEMORY_DEVICE, HYPRE_MEMORY_HOST);
      hypre_TFree(P_offd_j,HYPRE_MEMORY_HOST);
      hypre_TFree(P_offd_i,HYPRE_MEMORY_HOST);

      hypre_TFree (pass_order, HYPRE_MEMORY_HOST);
   } // if (P_offd_size)

   hypre_ParCSRMatrixColMapOffd(P) = col_map_offd_P;
   hypre_CSRMatrixNumCols(P_offd) = num_cols_offd_P;

   hypre_CSRMatrixMemoryLocation(P_diag) = HYPRE_MEMORY_DEVICE;
   hypre_CSRMatrixMemoryLocation(P_offd) = HYPRE_MEMORY_DEVICE;

   hypre_MatvecCommPkgCreate(P);

   for (i=0; i < num_passes-1; i++)
   {
      hypre_ParCSRMatrixDestroy(Pi[i]);
   }

   hypre_TFree (Pi, HYPRE_MEMORY_HOST);
   hypre_TFree (dof_func_offd, HYPRE_MEMORY_HOST);
   hypre_TFree (pass_starts, HYPRE_MEMORY_HOST);

   //hypre_TFree (pass_marker, HYPRE_MEMORY_HOST);// FIXME: Clean up when done
   hypre_TFree (pass_marker_offd, HYPRE_MEMORY_HOST);


    hypre_TFree (pass_marker_dev, HYPRE_MEMORY_DEVICE);// FIXME: Clean up when done
    hypre_TFree (pass_marker_offd_dev, HYPRE_MEMORY_DEVICE);
    hypre_TFree (pass_order_dev, HYPRE_MEMORY_DEVICE);
    hypre_TFree (fine_to_coarse_dev, HYPRE_MEMORY_DEVICE);

    HYPRE_THRUST_CALL( replace_if,
                       CF_marker_dev,
                       CF_marker_dev+n_fine,
                       equal<int>(-3),
                       static_cast<int>(-1) );

   // FIXME: We're assuming we need to hand CF_marker back to the host
   hypre_TMemcpy( CF_marker, CF_marker_dev, HYPRE_Int, n_fine, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
   hypre_TFree(CF_marker_dev, HYPRE_MEMORY_DEVICE);

   *P_ptr = P;

   return hypre_error_flag;

}


HYPRE_Int
hypre_GenerateMultipassPiDevice( hypre_ParCSRMatrix  *A,
                                 hypre_ParCSRMatrix  *S,
                                 HYPRE_BigInt        *c_pts_starts,
                                 HYPRE_Int           *pass_order, /* array containing row numbers of rows in A and S to be considered */
                                 HYPRE_Int           *pass_marker,
                                 HYPRE_Int           *pass_marker_offd,
                                 HYPRE_Int            num_points,
                                 HYPRE_Int            color,
                                 HYPRE_Real          *row_sums,
                                 hypre_ParCSRMatrix **P_ptr)
{
   MPI_Comm                comm = hypre_ParCSRMatrixComm(A);
   hypre_ParCSRCommPkg    *comm_pkg = hypre_ParCSRMatrixCommPkg(A);
   hypre_ParCSRCommHandle *comm_handle;

   hypre_CSRMatrix *A_diag = hypre_ParCSRMatrixDiag(A);
   hypre_assert( hypre_CSRMatrixMemoryLocation(A_diag) == HYPRE_MEMORY_DEVICE );

   HYPRE_Real      *A_diag_data = hypre_CSRMatrixData(A_diag);
   HYPRE_Int       *A_diag_i = hypre_CSRMatrixI(A_diag);
   HYPRE_Int       *A_diag_j = hypre_CSRMatrixJ(A_diag);

   HYPRE_Int        n_fine = hypre_CSRMatrixNumRows(A_diag);

   hypre_CSRMatrix *A_offd = hypre_ParCSRMatrixOffd(A);
   hypre_assert( hypre_CSRMatrixMemoryLocation(A_offd) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *A_offd_i = hypre_CSRMatrixI(A_offd);
   HYPRE_Int       *A_offd_j = hypre_CSRMatrixJ(A_offd);
   HYPRE_Real      *A_offd_data = hypre_CSRMatrixData(A_offd);

   HYPRE_Int        num_cols_offd_A = hypre_CSRMatrixNumCols(A_offd);

   hypre_CSRMatrix *S_diag = hypre_ParCSRMatrixDiag(S);
   hypre_assert( hypre_CSRMatrixMemoryLocation(S_diag) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *S_diag_i = hypre_CSRMatrixI(S_diag);
   HYPRE_Int       *S_diag_j = hypre_CSRMatrixJ(S_diag);

   hypre_CSRMatrix *S_offd = hypre_ParCSRMatrixOffd(S);
   hypre_assert( hypre_CSRMatrixMemoryLocation(S_offd) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *S_offd_i = hypre_CSRMatrixI(S_offd);
   HYPRE_Int       *S_offd_j = hypre_CSRMatrixJ(S_offd);


   HYPRE_BigInt    *col_map_offd_P = NULL;
   HYPRE_BigInt    *col_map_offd_P_dev = NULL;
   HYPRE_Int        num_cols_offd_P;
   HYPRE_Int        nnz_diag, nnz_offd;
   HYPRE_Int        n_cpts;

   HYPRE_Int        cpt, cnt_diag, cnt_offd;

   hypre_ParCSRMatrix *P;
   hypre_CSRMatrix *P_diag;
   HYPRE_Real      *P_diag_data;
   HYPRE_Int       *P_diag_i; /*at first counter of nonzero cols for each row,
                                      finally will be pointer to start of row */
   HYPRE_Int       *P_diag_j;

   hypre_CSRMatrix *P_offd;
   HYPRE_Real      *P_offd_data = NULL;
   HYPRE_Int       *P_offd_i; /*at first counter of nonzero cols for each row,
                                      finally will be pointer to start of row */
   HYPRE_Int       *P_offd_j = NULL;


   HYPRE_Int       *fine_to_coarse;
   HYPRE_Int       *fine_to_coarse_offd = NULL;
   HYPRE_BigInt    *f_pts_starts = NULL;
   HYPRE_Int        my_id, num_procs;
   HYPRE_BigInt     total_global_fpts;
   HYPRE_BigInt     total_global_cpts;
   HYPRE_BigInt    *big_convert;
   HYPRE_BigInt    *big_convert_offd = NULL;
   HYPRE_BigInt    *big_buf_data = NULL;
   HYPRE_Int        num_sends;

   /* MPI size and rank*/
   hypre_MPI_Comm_size(comm, &num_procs);
   hypre_MPI_Comm_rank(comm, &my_id);

   /* define P matrices */

   fine_to_coarse = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);

   /* fill P */
   init_fine_to_coarse( n_fine, pass_marker, color, fine_to_coarse, n_cpts );

   if (num_procs > 1)
   {
      HYPRE_BigInt big_Fpts;
      big_Fpts = num_points;

      f_pts_starts = hypre_CTAlloc(HYPRE_BigInt, 2, HYPRE_MEMORY_HOST);
      hypre_MPI_Scan(&big_Fpts, f_pts_starts+1, 1, HYPRE_MPI_BIG_INT, hypre_MPI_SUM, comm);
      f_pts_starts[0] = f_pts_starts[1] - big_Fpts;
      if (my_id == num_procs - 1)
      {
         total_global_fpts = f_pts_starts[1];
         total_global_cpts = c_pts_starts[1];
      }
      hypre_MPI_Bcast(&total_global_fpts, 1, HYPRE_MPI_BIG_INT, num_procs-1, comm);
      hypre_MPI_Bcast(&total_global_cpts, 1, HYPRE_MPI_BIG_INT, num_procs-1, comm);
   }
   else
   {
      f_pts_starts = hypre_CTAlloc(HYPRE_BigInt, 2, HYPRE_MEMORY_HOST);
      f_pts_starts[0] = 0;
      f_pts_starts[1] = num_points;
      total_global_fpts = f_pts_starts[1];
      total_global_cpts = c_pts_starts[1];
   }


   {
      big_convert = hypre_CTAlloc(HYPRE_BigInt, n_fine, HYPRE_MEMORY_DEVICE);

      init_big_convert(n_fine, pass_marker, color,
                       fine_to_coarse, c_pts_starts[0], big_convert );

      num_cols_offd_P = 0;

      if (num_cols_offd_A)
      {
         num_sends = hypre_ParCSRCommPkgNumSends(comm_pkg);

         big_convert_offd = hypre_CTAlloc(HYPRE_BigInt,  num_cols_offd_A, HYPRE_MEMORY_DEVICE);
         big_buf_data = hypre_CTAlloc(HYPRE_BigInt,  hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends), HYPRE_MEMORY_DEVICE);

         HYPRE_THRUST_CALL( gather,
                            hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg),
                            hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg) +
                            hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends),
                            big_convert,
                            big_buf_data );

         comm_handle = hypre_ParCSRCommHandleCreate_v2( 21, comm_pkg, HYPRE_MEMORY_DEVICE, big_buf_data, HYPRE_MEMORY_DEVICE, big_convert_offd);

         hypre_ParCSRCommHandleDestroy(comm_handle);

         // This will allocate fine_to_coarse_offd
         compute_num_cols_offd_fine_to_coarse( pass_marker_offd, color, num_cols_offd_A,
                                               num_cols_offd_P, &fine_to_coarse_offd );


         //FIXME: Clean this up when we don't need the host pointer anymore
         col_map_offd_P = hypre_CTAlloc(HYPRE_BigInt, num_cols_offd_P, HYPRE_MEMORY_HOST);
         col_map_offd_P_dev = hypre_CTAlloc(HYPRE_BigInt, num_cols_offd_P, HYPRE_MEMORY_DEVICE);

         cpt = 0;
         HYPRE_BigInt * col_map_end = HYPRE_THRUST_CALL( copy_if,
                                                         big_convert_offd,
                                                         big_convert_offd + num_cols_offd_A,
                                                         pass_marker_offd,
                                                         col_map_offd_P_dev,
                                                         equal<int>(color) );
         cpt = col_map_end - col_map_offd_P_dev;

         //FIXME: Clean this up when we don't need the host pointer anymore
         hypre_TMemcpy( col_map_offd_P, col_map_offd_P_dev, HYPRE_Int, num_cols_offd_P, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

         hypre_TFree(big_convert, HYPRE_MEMORY_DEVICE);
         hypre_TFree(big_convert_offd, HYPRE_MEMORY_DEVICE );
         hypre_TFree(big_buf_data, HYPRE_MEMORY_DEVICE);
      } // if (num_cols_offd_A)
   }

   P_diag_i  = hypre_CTAlloc(HYPRE_Int, num_points+1, HYPRE_MEMORY_DEVICE);
   P_offd_i  = hypre_CTAlloc(HYPRE_Int, num_points+1, HYPRE_MEMORY_DEVICE);

   nnz_diag = 0;
   nnz_offd = 0;
   {
     dim3 bDim = hypre_GetDefaultCUDABlockDimension();
     dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

     HYPRE_Int * nnz_diag_array = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);
     HYPRE_Int * nnz_offd_array = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);

     HYPRE_CUDA_LAUNCH( hypreCUDAKernel_generate_Pdiag_i_Poffd_i, gDim, bDim,
                        num_points, color, pass_order, pass_marker, pass_marker_offd,
                        S_diag_i, S_diag_j, S_offd_i, S_offd_j,
                        P_diag_i, P_offd_i,
                        nnz_diag_array, nnz_offd_array );


     auto nnz_reduce = HYPRE_THRUST_CALL( reduce,
                                          thrust::make_zip_iterator(thrust::make_tuple(nnz_diag_array,nnz_offd_array)),
                                          thrust::make_zip_iterator(thrust::make_tuple(nnz_diag_array+num_points,nnz_offd_array+num_points)),
                                          thrust::tuple<HYPRE_Int,HYPRE_Int>(0,0),
                                          tuple_plus<HYPRE_Int>() );

     nnz_diag = thrust::get<0>(nnz_reduce);
     nnz_offd = thrust::get<1>(nnz_reduce);

     hypre_TFree(nnz_diag_array, HYPRE_MEMORY_DEVICE);
     hypre_TFree(nnz_offd_array, HYPRE_MEMORY_DEVICE);
   }

   HYPRE_THRUST_CALL( inclusive_scan,
                      thrust::make_zip_iterator( thrust::make_tuple( P_diag_i, P_offd_i) ),
                      thrust::make_zip_iterator( thrust::make_tuple( P_diag_i + num_points+1, P_offd_i + num_points+1) ),
                      thrust::make_zip_iterator( thrust::make_tuple( P_diag_i, P_offd_i) ),
                      tuple_plus<HYPRE_Int>() );

   P_diag_j    = hypre_CTAlloc(HYPRE_Int, nnz_diag, HYPRE_MEMORY_DEVICE);
   P_diag_data = hypre_CTAlloc(HYPRE_Real, nnz_diag, HYPRE_MEMORY_DEVICE);
   P_offd_j    = hypre_CTAlloc(HYPRE_Int, nnz_offd, HYPRE_MEMORY_DEVICE);
   P_offd_data = hypre_CTAlloc(HYPRE_Real, nnz_offd, HYPRE_MEMORY_DEVICE);

   cnt_diag = 0;
   cnt_offd = 0;
   {
     dim3 bDim = hypre_GetDefaultCUDABlockDimension();
     dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

     HYPRE_Int * diag_shifts = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);
     HYPRE_Int * offd_shifts = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);

     hypreCUDAKernel_generate_Pdiag_j_Poffd_j_count<<<gDim,bDim>>>( num_points,
                                                                    color,
                                                                    pass_order,
                                                                    pass_marker,
                                                                    pass_marker_offd,
                                                                    S_diag_i,
                                                                    S_diag_j,
                                                                    S_offd_i,
                                                                    S_offd_j,
                                                                    diag_shifts,
                                                                    offd_shifts );

     auto cnt_reduce = HYPRE_THRUST_CALL( reduce,

                                          thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, offd_shifts)),
                                          thrust::make_zip_iterator(thrust::make_tuple(diag_shifts+num_points, offd_shifts+num_points)),
                                          thrust::tuple<int,int>(0,0),
                                          tuple_plus<int>() );

     cnt_diag = thrust::get<0>(cnt_reduce);
     cnt_offd = thrust::get<1>(cnt_reduce);

     HYPRE_THRUST_CALL( inclusive_scan,
                        thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, offd_shifts)),
                        thrust::make_zip_iterator(thrust::make_tuple(diag_shifts+num_points, offd_shifts+num_points)),
                        thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, offd_shifts)),
                        tuple_plus<int>() );

     hypreCUDAKernel_generate_Pdiag_j_Poffd_j<<<gDim,bDim>>>( num_points,
                                                              color,
                                                              pass_order,
                                                              pass_marker,
                                                              pass_marker_offd,
                                                              fine_to_coarse,
                                                              fine_to_coarse_offd,
                                                              A_diag_i,
                                                              A_diag_j,
                                                              A_diag_data,
                                                              A_offd_i,
                                                              A_offd_j,
                                                              A_offd_data,
                                                              hypre_ParCSRMatrixSocDiagJ(S),
                                                              hypre_ParCSRMatrixSocOffdJ(S),
                                                              diag_shifts,
                                                              offd_shifts,
                                                              P_diag_j,
                                                              P_diag_data,
                                                              P_offd_j,
                                                              P_offd_data );

     hypre_TFree(fine_to_coarse, HYPRE_MEMORY_DEVICE);
     hypre_TFree(fine_to_coarse_offd, HYPRE_MEMORY_DEVICE);

     hypre_TFree(diag_shifts, HYPRE_MEMORY_DEVICE);
     hypre_TFree(offd_shifts, HYPRE_MEMORY_DEVICE);
   }



   dim3 bDim = hypre_GetDefaultCUDABlockDimension();
   dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

   HYPRE_CUDA_LAUNCH( hypreCUDAKernel_mutlipass_pi_rowsum, gDim, bDim,
                      num_points, pass_order, A_diag_i, A_diag_data,
                      P_diag_i, P_diag_data, P_offd_i, P_offd_data,
                      row_sums );

   HYPRE_Int P_diag_nnz;
   HYPRE_Int P_offd_nnz;

   hypre_TMemcpy( &P_diag_nnz, &P_diag_i[num_points], HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
   hypre_TMemcpy( &P_offd_nnz, &P_offd_i[num_points], HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

   P = hypre_ParCSRMatrixCreate(comm,
                                total_global_fpts,
                                total_global_cpts,
                                f_pts_starts,
                                c_pts_starts,
                                num_cols_offd_P,
                                P_diag_nnz,
                                P_offd_nnz);

   P_diag = hypre_ParCSRMatrixDiag(P);
   hypre_CSRMatrixData(P_diag) = P_diag_data;
   hypre_CSRMatrixI(P_diag) = P_diag_i;
   hypre_CSRMatrixJ(P_diag) = P_diag_j;
   P_offd = hypre_ParCSRMatrixOffd(P);
   hypre_CSRMatrixData(P_offd) = P_offd_data;
   hypre_CSRMatrixI(P_offd) = P_offd_i;
   hypre_CSRMatrixJ(P_offd) = P_offd_j;
   hypre_ParCSRMatrixOwnsRowStarts(P) = 1;
   hypre_ParCSRMatrixOwnsColStarts(P) = 0;
   hypre_ParCSRMatrixColMapOffd(P) = col_map_offd_P;
   hypre_ParCSRMatrixDeviceColMapOffd(P) = col_map_offd_P_dev;

   hypre_CSRMatrixMemoryLocation(P_diag) = HYPRE_MEMORY_DEVICE;
   hypre_CSRMatrixMemoryLocation(P_offd) = HYPRE_MEMORY_DEVICE;

   hypre_MatvecCommPkgCreate(P);

   *P_ptr = P;

   return hypre_error_flag;
}

HYPRE_Int
hypre_GenerateMultiPiDevice( hypre_ParCSRMatrix  *A,
                             hypre_ParCSRMatrix  *S,
                             hypre_ParCSRMatrix  *P,
                             HYPRE_Int           *c_pts_starts,
                             HYPRE_Int           *pass_order, /* array containing row numbers of rows in A and S to be considered */
                             HYPRE_Int           *pass_marker,
                             HYPRE_Int           *pass_marker_offd,
                             HYPRE_Int            num_points,
                             HYPRE_Int            color,
                             HYPRE_Int            num_functions,
                             HYPRE_Int           *dof_func,
                             HYPRE_Int           *dof_func_offd,
                             hypre_ParCSRMatrix **Pi_ptr )
{
   MPI_Comm                comm = hypre_ParCSRMatrixComm(A);
   hypre_ParCSRCommPkg    *comm_pkg = hypre_ParCSRMatrixCommPkg(A);
   hypre_ParCSRCommHandle *comm_handle;

   hypre_CSRMatrix *A_diag = hypre_ParCSRMatrixDiag(A);
   hypre_assert( hypre_CSRMatrixMemoryLocation(A_diag) == HYPRE_MEMORY_DEVICE );

   HYPRE_Real      *A_diag_data = hypre_CSRMatrixData(A_diag);
   HYPRE_Int       *A_diag_i = hypre_CSRMatrixI(A_diag);
   HYPRE_Int       *A_diag_j = hypre_CSRMatrixJ(A_diag);

   HYPRE_Int        n_fine = hypre_CSRMatrixNumRows(A_diag);

   hypre_CSRMatrix *A_offd = hypre_ParCSRMatrixOffd(A);
   hypre_assert( hypre_CSRMatrixMemoryLocation(A_offd) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *A_offd_i = hypre_CSRMatrixI(A_offd);
   HYPRE_Int       *A_offd_j = hypre_CSRMatrixJ(A_offd);
   HYPRE_Real      *A_offd_data = hypre_CSRMatrixData(A_offd);

   HYPRE_Int        num_cols_offd_A = hypre_CSRMatrixNumCols(A_offd);

   hypre_CSRMatrix *S_diag = hypre_ParCSRMatrixDiag(S);
   hypre_assert( hypre_CSRMatrixMemoryLocation(S_diag) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *S_diag_i = hypre_CSRMatrixI(S_diag);
   HYPRE_Int       *S_diag_j = hypre_CSRMatrixJ(S_diag);

   hypre_CSRMatrix *S_offd = hypre_ParCSRMatrixOffd(S);
   hypre_assert( hypre_CSRMatrixMemoryLocation(S_offd) == HYPRE_MEMORY_DEVICE );

   HYPRE_Int       *S_offd_i = hypre_CSRMatrixI(S_offd);
   HYPRE_Int       *S_offd_j = hypre_CSRMatrixJ(S_offd);

   HYPRE_BigInt    *col_map_offd_Q = NULL;
   HYPRE_BigInt    *col_map_offd_Q_dev = NULL;
   HYPRE_Int        num_cols_offd_Q;

   hypre_ParCSRMatrix *Pi;
   hypre_CSRMatrix *Pi_diag;
   HYPRE_Int       *Pi_diag_i;
   HYPRE_Real      *Pi_diag_data;

   hypre_CSRMatrix *Pi_offd;
   HYPRE_Int       *Pi_offd_i;
   HYPRE_Real      *Pi_offd_data;

   HYPRE_Int        nnz_diag, nnz_offd;
   HYPRE_Int        n_cpts;

   HYPRE_Int        cpt, cnt_diag, cnt_offd;

   hypre_ParCSRMatrix *Q;
   hypre_CSRMatrix *Q_diag;
   HYPRE_Real      *Q_diag_data;
   HYPRE_Int       *Q_diag_i; /*at first counter of nonzero cols for each row,
                                      finally will be pointer to start of row */
   HYPRE_Int       *Q_diag_j;


   hypre_CSRMatrix *Q_offd;
   HYPRE_Real      *Q_offd_data = NULL;

   HYPRE_Int       *Q_offd_i; /*at first counter of nonzero cols for each row,
                                      finally will be pointer to start of row */
   HYPRE_Int       *Q_offd_j = NULL;

   HYPRE_Int       *fine_to_coarse;
   HYPRE_Int       *fine_to_coarse_offd = NULL;
   HYPRE_BigInt    *f_pts_starts = NULL;
   HYPRE_Int        my_id, num_procs;
   HYPRE_BigInt     total_global_fpts;
   HYPRE_BigInt     total_global_cpts;
   HYPRE_BigInt    *big_convert;
   HYPRE_BigInt    *big_convert_offd = NULL;
   HYPRE_BigInt    *big_buf_data = NULL;
   HYPRE_Int        num_sends;

   HYPRE_Real      *w_row_sum;

   /* MPI size and rank*/
   hypre_MPI_Comm_size(comm, &num_procs);
   hypre_MPI_Comm_rank(comm, &my_id);

   /* define P matrices */

   fine_to_coarse = hypre_CTAlloc(HYPRE_Int, n_fine, HYPRE_MEMORY_DEVICE);

   /* fill P */
   init_fine_to_coarse( n_fine, pass_marker, color, fine_to_coarse, n_cpts );

   if (num_procs > 1)
   {
      HYPRE_BigInt big_Fpts;
      big_Fpts = num_points;

      f_pts_starts = hypre_CTAlloc(HYPRE_BigInt, 2, HYPRE_MEMORY_HOST);
      hypre_MPI_Scan(&big_Fpts, f_pts_starts+1, 1, HYPRE_MPI_BIG_INT, hypre_MPI_SUM, comm);
      f_pts_starts[0] = f_pts_starts[1] - big_Fpts;
      if (my_id == num_procs - 1)
      {
         total_global_fpts = f_pts_starts[1];
         total_global_cpts = c_pts_starts[1];
      }
      hypre_MPI_Bcast(&total_global_fpts, 1, HYPRE_MPI_BIG_INT, num_procs-1, comm);
      hypre_MPI_Bcast(&total_global_cpts, 1, HYPRE_MPI_BIG_INT, num_procs-1, comm);
   }
   else
   {
      f_pts_starts = hypre_CTAlloc(HYPRE_BigInt, 2, HYPRE_MEMORY_HOST);
      f_pts_starts[0] = 0;
      f_pts_starts[1] = num_points;
      total_global_fpts = f_pts_starts[1];
      total_global_cpts = c_pts_starts[1];
   }

   {
      big_convert = hypre_CTAlloc(HYPRE_BigInt, n_fine, HYPRE_MEMORY_DEVICE);

      init_big_convert(n_fine, pass_marker, color,
                       fine_to_coarse, c_pts_starts[0], big_convert );

      num_cols_offd_Q = 0;
      if (num_cols_offd_A)
      {
         num_sends = hypre_ParCSRCommPkgNumSends(comm_pkg);

         big_convert_offd = hypre_CTAlloc(HYPRE_BigInt,  num_cols_offd_A, HYPRE_MEMORY_DEVICE);
         big_buf_data = hypre_CTAlloc(HYPRE_BigInt,  hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends), HYPRE_MEMORY_DEVICE);

         HYPRE_THRUST_CALL( gather,
                            hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg),
                            hypre_ParCSRCommPkgDeviceSendMapElmts(comm_pkg) +
                            hypre_ParCSRCommPkgSendMapStart(comm_pkg, num_sends),
                            big_convert,
                            big_buf_data );

         comm_handle = hypre_ParCSRCommHandleCreate_v2( 21, comm_pkg, HYPRE_MEMORY_DEVICE, big_buf_data, HYPRE_MEMORY_DEVICE, big_convert_offd);

         hypre_ParCSRCommHandleDestroy(comm_handle);

         // This will allocate fine_to_coarse_offd_dev
         compute_num_cols_offd_fine_to_coarse( pass_marker_offd, color, num_cols_offd_A,
                                               num_cols_offd_Q, &fine_to_coarse_offd );

         //FIXME: PB: It seems we need the host value too?!?!
         col_map_offd_Q = hypre_CTAlloc(HYPRE_BigInt, num_cols_offd_Q, HYPRE_MEMORY_HOST);
         col_map_offd_Q_dev = hypre_CTAlloc(HYPRE_BigInt, num_cols_offd_Q, HYPRE_MEMORY_DEVICE);

         cpt = 0;
         HYPRE_BigInt * col_map_end = HYPRE_THRUST_CALL( copy_if,
                                                         big_convert_offd,
                                                         big_convert_offd + num_cols_offd_A,
                                                         pass_marker_offd,
                                                         col_map_offd_Q_dev,
                                                         equal<int>(color) );
         cpt = col_map_end - col_map_offd_Q_dev;

         //FIXME: PB: It seems like we're required to have a host version of this??
         hypre_TMemcpy( col_map_offd_Q, col_map_offd_Q_dev, HYPRE_Int, num_cols_offd_Q, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

         hypre_TFree(big_convert, HYPRE_MEMORY_DEVICE);
         hypre_TFree(big_convert_offd, HYPRE_MEMORY_DEVICE );
         hypre_TFree(big_buf_data, HYPRE_MEMORY_DEVICE);

      } // if (num_cols_offd_A)
   }

   /* generate Q_diag_i and Q_offd_i */
   Q_diag_i = hypre_CTAlloc(HYPRE_Int, num_points+1, HYPRE_MEMORY_DEVICE);
   Q_offd_i = hypre_CTAlloc(HYPRE_Int, num_points+1, HYPRE_MEMORY_DEVICE);

   nnz_diag = 0;
   nnz_offd = 0;
   {
     dim3 bDim = hypre_GetDefaultCUDABlockDimension();
     dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

     HYPRE_Int * nnz_diag_array = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);
     HYPRE_Int * nnz_offd_array = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);

     // We can reuse this from the Multipass function
     HYPRE_CUDA_LAUNCH( hypreCUDAKernel_generate_Pdiag_i_Poffd_i, gDim, bDim,
                        num_points, color, pass_order, pass_marker, pass_marker_offd,
                        S_diag_i, S_diag_j, S_offd_i, S_offd_j,
                        Q_diag_i, Q_offd_i,
                        nnz_diag_array, nnz_offd_array );

     auto nnz_reduce = HYPRE_THRUST_CALL( reduce,
                                          thrust::make_zip_iterator(thrust::make_tuple(nnz_diag_array,nnz_offd_array)),
                                          thrust::make_zip_iterator(thrust::make_tuple(nnz_diag_array+num_points,nnz_offd_array+num_points)),
                                          thrust::tuple<HYPRE_Int,HYPRE_Int>(0,0),
                                          tuple_plus<HYPRE_Int>() );

     nnz_diag = thrust::get<0>(nnz_reduce);
     nnz_offd = thrust::get<1>(nnz_reduce);

     hypre_TFree(nnz_diag_array, HYPRE_MEMORY_DEVICE);
     hypre_TFree(nnz_offd_array, HYPRE_MEMORY_DEVICE);
   }

   HYPRE_THRUST_CALL( inclusive_scan,
                      thrust::make_zip_iterator( thrust::make_tuple( Q_diag_i, Q_offd_i) ),
                      thrust::make_zip_iterator( thrust::make_tuple( Q_diag_i + num_points+1, Q_offd_i + num_points+1) ),
                      thrust::make_zip_iterator( thrust::make_tuple( Q_diag_i, Q_offd_i) ),
                      tuple_plus<HYPRE_Int>() );

   Q_diag_j = hypre_CTAlloc(HYPRE_Int, nnz_diag, HYPRE_MEMORY_DEVICE);
   Q_diag_data = hypre_CTAlloc(HYPRE_Real, nnz_diag, HYPRE_MEMORY_DEVICE);
   Q_offd_j = hypre_CTAlloc(HYPRE_Int, nnz_offd, HYPRE_MEMORY_DEVICE);
   Q_offd_data= hypre_CTAlloc(HYPRE_Real, nnz_offd, HYPRE_MEMORY_DEVICE);
   w_row_sum = hypre_CTAlloc(HYPRE_Real, num_points, HYPRE_MEMORY_DEVICE);

   cnt_diag = 0;
   cnt_offd = 0;
   if (num_functions > 1)
   {
     hypre_error_w_msg(HYPRE_ERROR_GENERIC,"Sorry, this code not yet ported to the GPU!");
     /*
      for (i=0; i < num_points; i++)
      {
         i1 = pass_order[i];
         j2 = A_diag_i[i1]+1;
         //if (w_row_minus) w_row_sum[i] = -w_row_minus[i1];
         for (j = S_diag_i[i1]; j < S_diag_i[i1+1]; j++)
         {
            j1 = S_diag_j[j];
            while (A_diag_j[j2] != j1)
            {
               if (dof_func[i1] == dof_func[A_diag_j[j2]])
               {
                  w_row_sum[i] += A_diag_data[j2];
               }
               j2++;
            }
            if (pass_marker[j1] == color && A_diag_j[j2] == j1)
            {
               Q_diag_j[cnt_diag] = fine_to_coarse[j1];
               Q_diag_data[cnt_diag++] = A_diag_data[j2++];
            }
            else
            {
               if (dof_func[i1] == dof_func[A_diag_j[j2]])
               {
                  w_row_sum[i] += A_diag_data[j2];
               }
               j2++;
            }
         }
         while (j2 < A_diag_i[i1+1])
         {
            if (dof_func[i1] == dof_func[A_diag_j[j2]])
            {
               w_row_sum[i] += A_diag_data[j2];
            }
            j2++;
         }
         j2 = A_offd_i[i1];
         for (j = S_offd_i[i1]; j < S_offd_i[i1+1]; j++)
         {
            j1 = S_offd_j[j];
            while (A_offd_j[j2] != j1)
            {
               if (dof_func[i1] == dof_func_offd[A_offd_j[j2]])
               {
                  w_row_sum[i] += A_offd_data[j2];
               }
               j2++;
            }
            if (pass_marker_offd[j1] == color && A_offd_j[j2] == j1)
            {
               Q_offd_j[cnt_offd] = fine_to_coarse_offd[j1];
               Q_offd_data[cnt_offd++] = A_offd_data[j2++];
            }
            else
            {
               if (dof_func[i1] == dof_func_offd[A_offd_j[j2]])
               {
                  w_row_sum[i] += A_offd_data[j2];
               }
               j2++;
            }
         }
         while (j2 < A_offd_i[i1+1])
         {
            if (dof_func[i1] == dof_func_offd[A_offd_j[j2]])
            {
               w_row_sum[i] += A_offd_data[j2];
            }
            j2++;
         }
      }
     */
   }
   else
   {
     dim3 bDim = hypre_GetDefaultCUDABlockDimension();
     dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

     HYPRE_Int * diag_shifts = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);
     HYPRE_Int * offd_shifts = hypre_CTAlloc(HYPRE_Int, num_points, HYPRE_MEMORY_DEVICE);

     // We can completely reuse this kernel from the Multipass function
     hypreCUDAKernel_generate_Pdiag_j_Poffd_j_count<<<gDim,bDim>>>( num_points,
                                                                    color,
                                                                    pass_order,
                                                                    pass_marker,
                                                                    pass_marker_offd,
                                                                    S_diag_i,
                                                                    S_diag_j,
                                                                    S_offd_i,
                                                                    S_offd_j,
                                                                    diag_shifts,
                                                                    offd_shifts );

     auto cnt_reduce = HYPRE_THRUST_CALL( reduce,

                                          thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, offd_shifts)),
                                          thrust::make_zip_iterator(thrust::make_tuple(diag_shifts+num_points, offd_shifts+num_points)),
                                          thrust::tuple<int,int>(0,0),
                                          tuple_plus<int>() );

     cnt_diag = thrust::get<0>(cnt_reduce);
     cnt_offd = thrust::get<1>(cnt_reduce);

     HYPRE_THRUST_CALL( inclusive_scan,
                        thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, offd_shifts)),
                        thrust::make_zip_iterator(thrust::make_tuple(diag_shifts+num_points, offd_shifts+num_points)),
                        thrust::make_zip_iterator(thrust::make_tuple(diag_shifts, offd_shifts)),
                        tuple_plus<int>() );


     hypreCUDAKernel_generate_Qdiag_j_Qoffd_j<<<gDim,bDim>>>( num_points,
                                                              color,
                                                              pass_order,
                                                              pass_marker,
                                                              pass_marker_offd,
                                                              fine_to_coarse,
                                                              fine_to_coarse_offd,
                                                              A_diag_i,
                                                              A_diag_j,
                                                              A_diag_data,
                                                              A_offd_i,
                                                              A_offd_j,
                                                              A_offd_data,
                                                              hypre_ParCSRMatrixSocDiagJ(S),
                                                              hypre_ParCSRMatrixSocOffdJ(S),
                                                              diag_shifts,
                                                              offd_shifts,
                                                              Q_diag_j,
                                                              Q_diag_data,
                                                              Q_offd_j,
                                                              Q_offd_data,
                                                              w_row_sum );

     hypre_TFree(diag_shifts, HYPRE_MEMORY_DEVICE);
     hypre_TFree(offd_shifts, HYPRE_MEMORY_DEVICE);
   }

   hypre_TFree(fine_to_coarse, HYPRE_MEMORY_DEVICE);
   hypre_TFree(fine_to_coarse_offd, HYPRE_MEMORY_DEVICE);

   HYPRE_Int Q_diag_nnz;
   HYPRE_Int Q_offd_nnz;

   hypre_TMemcpy( &Q_diag_nnz, &Q_diag_i[num_points], HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
   hypre_TMemcpy( &Q_offd_nnz, &Q_offd_i[num_points], HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

   Q = hypre_ParCSRMatrixCreate(comm,
                                total_global_fpts,
                                total_global_cpts,
                                f_pts_starts,
                                c_pts_starts,
                                num_cols_offd_Q,
                                Q_diag_nnz,
                                Q_offd_nnz);

   Q_diag = hypre_ParCSRMatrixDiag(Q);
   hypre_CSRMatrixData(Q_diag) = Q_diag_data;
   hypre_CSRMatrixI(Q_diag) = Q_diag_i;
   hypre_CSRMatrixJ(Q_diag) = Q_diag_j;
   Q_offd = hypre_ParCSRMatrixOffd(Q);
   hypre_CSRMatrixData(Q_offd) = Q_offd_data;
   hypre_CSRMatrixI(Q_offd) = Q_offd_i;
   hypre_CSRMatrixJ(Q_offd) = Q_offd_j;
   hypre_ParCSRMatrixOwnsRowStarts(Q) = 1;
   hypre_ParCSRMatrixOwnsColStarts(Q) = 0;
   hypre_ParCSRMatrixColMapOffd(Q) = col_map_offd_Q;
   hypre_ParCSRMatrixDeviceColMapOffd(Q) = col_map_offd_Q_dev;

   hypre_CSRMatrixMemoryLocation(Q_diag) = HYPRE_MEMORY_DEVICE;
   hypre_CSRMatrixMemoryLocation(Q_offd) = HYPRE_MEMORY_DEVICE;

   hypre_MatvecCommPkgCreate(Q);

   Pi = hypre_ParCSRMatMat(Q, P);


   // Once Q is on the device, P is coming in on the device
   // So Pi should be on the device then
   Pi_diag = hypre_ParCSRMatrixDiag(Pi);
   Pi_diag_data = hypre_CSRMatrixData(Pi_diag);
   Pi_diag_i = hypre_CSRMatrixI(Pi_diag);
   Pi_offd = hypre_ParCSRMatrixOffd(Pi);
   Pi_offd_data = hypre_CSRMatrixData(Pi_offd);
   Pi_offd_i = hypre_CSRMatrixI(Pi_offd);

   {
     dim3 bDim = hypre_GetDefaultCUDABlockDimension();
     dim3 gDim = hypre_GetDefaultCUDAGridDimension(num_points, "warp", bDim);

     HYPRE_CUDA_LAUNCH( hypreCUDAKernel_mutli_pi_rowsum, gDim, bDim,
                        num_points, pass_order, A_diag_i, A_diag_data,
                        Pi_diag_i, Pi_diag_data, Pi_offd_i, Pi_offd_data,
                        w_row_sum );

     hypre_TFree( w_row_sum, HYPRE_MEMORY_DEVICE);
   }

   hypre_ParCSRMatrixOwnsRowStarts(Q)=0;
   hypre_ParCSRMatrixOwnsRowStarts(Pi)=1;
   hypre_ParCSRMatrixDestroy(Q);

   *Pi_ptr = Pi;

   return hypre_error_flag;
}

void init_fine_to_coarse( HYPRE_Int n_fine, HYPRE_Int * pass_marker, HYPRE_Int color,
                          HYPRE_Int * fine_to_coarse, HYPRE_Int & n_cpts )
{
  // Host code this is replacing:
  // n_cpts = 0;
  // for (int i=0; i < pass_marker.size(); i++)
  //  {
  //    if (pass_marker[i] == color)
  //      fine_to_coarse[i] = n_cpts++;
  //    else
  //      fine_to_coarse[i] = -1;
  //  }

  // It is substantially faster (I measured) to just copy the end of the fine_to_coarse_dev
  // array to the host get the value of n_cpts than to do a thrust::count_if on
  // pass_marker
  HYPRE_THRUST_CALL( exclusive_scan,
                     thrust::make_transform_iterator(pass_marker,equal<int>(color)),
                     thrust::make_transform_iterator(pass_marker+n_fine,equal<int>(color)),
                     fine_to_coarse,
                     (int)0 );

  HYPRE_Int * n_cpts_dev = &fine_to_coarse[n_fine-1];
  hypre_TMemcpy( &n_cpts, n_cpts_dev, HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);

  HYPRE_THRUST_CALL( replace_if,
                     fine_to_coarse,
                     fine_to_coarse+n_fine,
                     pass_marker,
                     not_equal<int>(color),
                     (int) -1 );
}

struct local_equal_plus_constant :
    public thrust::binary_function<HYPRE_BigInt,HYPRE_BigInt,HYPRE_BigInt>
  {
    HYPRE_BigInt _value;

    local_equal_plus_constant(HYPRE_BigInt value) : _value(value) {}

     __host__ __device__
     HYPRE_BigInt operator()(HYPRE_BigInt /*x*/, HYPRE_BigInt y)
     { return y + _value; }
  };

void init_big_convert( HYPRE_Int n_fine, HYPRE_Int * pass_marker, HYPRE_Int color,
                       HYPRE_Int * fine_to_coarse, HYPRE_BigInt cpts,
                       HYPRE_BigInt * big_convert )
{
  // Host code this function is replacing
  //
  // for (i=0; i < n_fine; i++)
  // {
  //    if (pass_marker[i] == color)
  //    {
  //       big_convert[i] = (HYPRE_BigInt)fine_to_coarse[i] + c_pts_starts[0];
  //    }
  // }

  local_equal_plus_constant op(cpts);

  HYPRE_THRUST_CALL( transform_if,
                     big_convert,
                     big_convert + n_fine,
                     (HYPRE_BigInt*)fine_to_coarse,
                     pass_marker,
                     big_convert,
                     op,
                     equal<int>(color) );
}

void compute_num_cols_offd_fine_to_coarse( HYPRE_Int * pass_marker_offd, HYPRE_Int color,
                                           HYPRE_Int num_cols_offd_A, HYPRE_Int & num_cols_offd,
                                           HYPRE_Int ** fine_to_coarse_offd )
{
  /* Original host code

    num_cols_offd_P = 0;
    for (i=0; i < num_cols_offd_A; i++)
    {
      if (pass_marker_offd[i] == color)
      {
        fine_to_coarse_offd[i] = num_cols_offd_P++;
      }
    }

  */

  // We allocate with a "+1" because the host version of this code incremented the counter
  // even on the last match, so we create an extra entry the exclusive_scan will reflect this
  // and we can read off the last entry and only do 1 kernel call and 1 memcpy
  *fine_to_coarse_offd = hypre_TAlloc(HYPRE_Int,  num_cols_offd_A+1, HYPRE_MEMORY_DEVICE);

  num_cols_offd = 0;

  HYPRE_THRUST_CALL( exclusive_scan,
                     thrust::make_transform_iterator(pass_marker_offd,equal<HYPRE_Int>(color)),
                     thrust::make_transform_iterator(pass_marker_offd + num_cols_offd_A+1,equal<HYPRE_Int>(color)),
                     *fine_to_coarse_offd,
                     (HYPRE_Int)0 );

  hypre_TMemcpy( &num_cols_offd, *fine_to_coarse_offd + num_cols_offd_A, HYPRE_Int, 1, HYPRE_MEMORY_HOST, HYPRE_MEMORY_DEVICE);
}

__global__
void hypreCUDAKernel_cfmarker_masked_rowsum( HYPRE_Int nrows,
                                             HYPRE_Int *A_diag_i,
                                             HYPRE_Complex *A_diag_data,
                                             HYPRE_Int *A_offd_i,
                                             HYPRE_Complex *A_offd_data,
                                             HYPRE_Int *CF_marker,
                                             HYPRE_Complex *row_sums )
{
  HYPRE_Int row_i = hypre_cuda_get_grid_warp_id<1,1>();

  if (row_i >= nrows || CF_marker[row_i] >= 0)
   {
      return;
   }

   HYPRE_Int lane = hypre_cuda_get_lane_id<1>();
   HYPRE_Int p = 0;
   HYPRE_Int q = 0;

   // A_diag part
   if (lane < 2)
   {
      p = read_only_load(A_diag_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   HYPRE_Complex row_sum_i = 0.0;

   // j = A_diag_1[i]+1 ==> p+1
   for (HYPRE_Int j = p+1 + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Complex value = A_diag_data[j];

      row_sum_i += value;
   }

   // A_offd part
   if (lane < 2)
   {
      p = read_only_load(A_offd_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Complex value = A_offd_data[j];

      row_sum_i += value;
   }

   row_sum_i = warp_reduce_sum(row_sum_i);

   if(lane == 0)
     row_sums[row_i] += row_sum_i;
}

__global__
void hypreCUDAKernel_mutlipass_pi_rowsum( HYPRE_Int      num_points,
                                          HYPRE_Int     *pass_order,
                                          HYPRE_Int     *A_diag_i,
                                          HYPRE_Complex *A_diag_data,
                                          HYPRE_Int     *P_diag_i,
                                          HYPRE_Complex *P_diag_data,
                                          HYPRE_Int     *P_offd_i,
                                          HYPRE_Complex *P_offd_data,
                                          HYPRE_Complex *row_sums )
{
  HYPRE_Int row_i = hypre_cuda_get_grid_warp_id<1,1>();

  if (row_i >= num_points)
   {
      return;
   }

   HYPRE_Int lane = hypre_cuda_get_lane_id<1>();
   HYPRE_Int p = 0;
   HYPRE_Int q = 0;

   HYPRE_Real row_sum_C = 0.0;

   // P_diag
   if (lane < 2)
   {
      p = read_only_load(P_diag_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Complex value = P_diag_data[j];

      row_sum_C += value;
   }

   // P_offd part
   if (lane < 2)
   {
      p = read_only_load(P_offd_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Complex value = P_offd_data[j];

      row_sum_C += value;
   }

   row_sum_C = warp_reduce_sum(row_sum_C);

   HYPRE_Int i1 = pass_order[row_i];

   HYPRE_Real diagonal = A_diag_data[A_diag_i[i1]];

   HYPRE_Real value = row_sum_C*diagonal;

   HYPRE_Real row_sum_i = 0.0;
   if(lane == 0)
     {
       row_sum_i = row_sums[i1];

       if( value != 0 )
         {
           row_sum_i /= value;
           row_sums[i1] = row_sum_i;
         }
     }

   row_sum_i = __shfl_sync(HYPRE_WARP_FULL_MASK, row_sum_i, 0);

   // P_diag
   if (lane < 2)
   {
      p = read_only_load(P_diag_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

       P_diag_data[j] = -P_diag_data[j]*row_sum_i;
   }

   // P_offd
   if (lane < 2)
   {
      p = read_only_load(P_offd_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

       P_offd_data[j] = -P_offd_data[j]*row_sum_i;
   }
}

__global__
void hypreCUDAKernel_mutli_pi_rowsum( HYPRE_Int      num_points,
                                      HYPRE_Int     *pass_order,
                                      HYPRE_Int     *A_diag_i,
                                      HYPRE_Complex *A_diag_data,
                                      HYPRE_Int     *Pi_diag_i,
                                      HYPRE_Complex *Pi_diag_data,
                                      HYPRE_Int     *Pi_offd_i,
                                      HYPRE_Complex *Pi_offd_data,
                                      HYPRE_Complex *w_row_sum )
{
  HYPRE_Int row_i = hypre_cuda_get_grid_warp_id<1,1>();

  if (row_i >= num_points)
   {
      return;
   }

   HYPRE_Int lane = hypre_cuda_get_lane_id<1>();
   HYPRE_Int p = 0;
   HYPRE_Int q = 0;

   HYPRE_Real row_sum_C = 0.0;

   // Pi_diag
   if (lane < 2)
   {
      p = read_only_load(Pi_diag_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Complex value = Pi_diag_data[j];

      row_sum_C += value;
   }

   // Pi_offd
   if (lane < 2)
   {
      p = read_only_load(Pi_offd_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Complex value = Pi_offd_data[j];

      row_sum_C += value;
   }

   row_sum_C = warp_reduce_sum(row_sum_C);

   HYPRE_Int i1 = pass_order[row_i];

   HYPRE_Real diagonal = A_diag_data[A_diag_i[i1]];

   HYPRE_Real value = row_sum_C*diagonal;

   if( lane == 0 )
     {
       row_sum_C += w_row_sum[row_i];

       if( value != 0 )
         {
           row_sum_C /= value;
         }
     }

   row_sum_C = __shfl_sync(HYPRE_WARP_FULL_MASK, row_sum_C, 0);

   // Pi_diag
   if (lane < 2)
   {
      p = read_only_load(Pi_diag_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      Pi_diag_data[j] = -Pi_diag_data[j]*row_sum_C;
   }

   // Pi_offd
   if (lane < 2)
   {
      p = read_only_load(Pi_offd_i + row_i + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      Pi_offd_data[j] = -Pi_offd_data[j]*row_sum_C;
   }
}

__global__
void hypreCUDAKernel_generate_Pdiag_i_Poffd_i( HYPRE_Int  num_points,
                                               HYPRE_Int  color,
                                               HYPRE_Int *pass_order,
                                               HYPRE_Int *pass_marker,
                                               HYPRE_Int *pass_marker_offd,
                                               HYPRE_Int *S_diag_i,
                                               HYPRE_Int *S_diag_j,
                                               HYPRE_Int *S_offd_i,
                                               HYPRE_Int *S_offd_j,
                                               HYPRE_Int *P_diag_i,
                                               HYPRE_Int *P_offd_i,
                                               HYPRE_Int * nnz_diag,
                                               HYPRE_Int * nnz_offd )
{
  /*
    nnz_diag = 0;
    nnz_offd = 0;
    for (i=0; i < num_points; i++)
    {
      i1 = pass_order[i];
      for (j=S_diag_i[i1]; j < S_diag_i[i1+1]; j++)
      {
         j1 = S_diag_j[j];
         if (pass_marker[j1] == color)
         {
             P_diag_i[i+1]++;
             nnz_diag++;
         }
      }
      for (j=S_offd_i[i1]; j < S_offd_i[i1+1]; j++)
      {
         j1 = S_offd_j[j];
         if (pass_marker_offd[j1] == color)
         {
             P_offd_i[i+1]++;
             nnz_offd++;
         }
      }
    }
  */

   HYPRE_Int row_i = hypre_cuda_get_grid_warp_id<1,1>();

   if (row_i >= num_points)
    {
       return;
    }

   HYPRE_Int i1 = pass_order[row_i];

   HYPRE_Int lane = hypre_cuda_get_lane_id<1>();

   HYPRE_Int p = 0;
   HYPRE_Int q = 0;

   // S_diag
   if (lane < 2)
   {
      p = read_only_load(S_diag_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   HYPRE_Int diag_increment = 0;
   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Int j1 = S_diag_j[j];

      if (pass_marker[j1] == color)
         {
           diag_increment += 1;
         }
   }

   diag_increment = warp_reduce_sum(diag_increment);


   // Increment P_diag_i, but then we need to also do a block reduction
   // on diag_increment to log the total nnz_diag for the block
   // Then after the kernel, we'll accumulate nnz_diag for each block
   if(lane == 0)
     {
       P_diag_i[row_i+1] += diag_increment;
       nnz_diag[row_i] = diag_increment;
     }

   // S_offd
   if (lane < 2)
   {
      p = read_only_load(S_offd_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   HYPRE_Int offd_increment = 0;
   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      HYPRE_Int j1 = S_offd_j[j];
      if (pass_marker_offd[j1] == color)
         {
           offd_increment += 1;
         }
   }

   offd_increment = warp_reduce_sum(offd_increment);

   // Increment P_offd_i, but then we need to also do a block reduction
   // on offd_increment to log the total nnz_offd for the block
   // Then after the kernel, we'll accumulate nnz_offd for each block
   if(lane == 0)
     {
       P_offd_i[row_i+1] += offd_increment;
       nnz_offd[row_i] = offd_increment;
     }
}

__global__
void hypreCUDAKernel_generate_Pdiag_j_Poffd_j_count( int num_points,
                                                     int color,
                                                     int  *pass_order,
                                                     int  *pass_marker,
                                                     int  *pass_marker_offd,
                                                     int  *S_diag_i,
                                                     int  *S_diag_j,
                                                     int  *S_offd_i,
                                                     int  *S_offd_j,
                                                     int  *diag_shifts,
                                                     int  *offd_shifts )
{
  int row_i = hypre_cuda_get_grid_warp_id<1,1>();

  if (row_i >= num_points)
    {
      return;
    }

  int i1 = pass_order[row_i];


  int lane = hypre_cuda_get_lane_id<1>();

  int p = 0;
  int q = 0;

   // S_diag
   if (lane < 2)
   {
      p = read_only_load(S_diag_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   int equal = 0;
   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      int j1 =  S_diag_j[j];
      if( j1 > -1 && pass_marker[j1] == color )
        equal++;
   }

   equal = warp_reduce_sum(equal);
   if(lane == 0)
     diag_shifts[row_i] = equal;

  // S_diag
  // PB: I leave an alternative implementation here that mimics the code in the subsequent
  //     hypreCUDAKernel_generate_Pdiag_j_Poffd_j kernel. I didn't use this version since
  //     it would iterate over more entries (A_diag_i vs. S_diag_i) so I anticipated it
  //     would perform worse. But I did not measure it. I did however verify it is correct.
  /*
  if (lane < 2)
   {
      p = read_only_load(A_diag_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   int equal = 0;
   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      int j1 =  Soc_diag_j[j];
      if( j1 > -1 && pass_marker[j1] == color )
        equal++;
   }

   equal = warp_reduce_sum(equal);
   if(lane == 0)
     diag_shifts[row_i] = equal;
  */

   // S_offd
   if (lane < 2)
   {
      p = read_only_load(S_offd_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   equal = 0;
   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      int j1 = S_offd_j[j];
      if (j1 > -1 && pass_marker_offd[j1] == color )
        equal++;
   }
   equal = warp_reduce_sum(equal);
   if(lane == 0)
     offd_shifts[row_i] = equal;

  // S_offd
  // PB: I leave an alternative implementation here that mimics the code in the subsequent
  //     hypreCUDAKernel_generate_Pdiag_j_Poffd_j kernel. I didn't use this version since
  //     it would iterate over more entries (A_offd_i vs. S_offd_i) so I anticipated it
  //     would perform worse. But I did not measure it. I did however verify it is correct.
  /*
   if (lane < 2)
   {
      p = read_only_load(A_offd_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   equal = 0;
   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      if ( j >= q )
      {
         continue;
      }

      int j1 = Soc_offd_j[j];
      if (j1 > -1 && pass_marker_offd[j1] == color )
        equal++;
   }
   equal = warp_reduce_sum(equal);
   if(lane == 0)
     offd_shifts[row_i] = equal;
   */
}

__global__
void hypreCUDAKernel_generate_Pdiag_j_Poffd_j( int    num_points,
                                               int    color,
                                               int    *pass_order,
                                               int    *pass_marker,
                                               int    *pass_marker_offd,
                                               int    *fine_to_coarse,
                                               int    *fine_to_coarse_offd,
                                               int    *A_diag_i,
                                               int    *A_diag_j,
                                               double *A_diag_data,
                                               int    *A_offd_i,
                                               int    *A_offd_j,
                                               double *A_offd_data,
                                               int    *Soc_diag_j,
                                               int    *Soc_offd_j,
                                               int    *diag_shifts,
                                               int    *offd_shifts,
                                               int    *P_diag_j,
                                               double *P_diag_data,
                                               int    *P_offd_j,
                                               double *P_offd_data )
{
  // Host implementation that
  // hypreCUDAKernel_generate_Pdiag_j_Poffd_j_count
  // and
  // hypreCUDAKernel_generate_Pdiag_j_Poffd_j
  // are replacing
  /*
  for (i=0; i < num_points; i++)
    {
      i1 = pass_order[i];
      j2 = A_diag_i[i1];
      for (j = S_diag_i[i1]; j < S_diag_i[i1+1]; j++)
        {
          j1 = S_diag_j[j];
          while (A_diag_j[j2] != j1) j2++;
          if (pass_marker[j1] == color && A_diag_j[j2] == j1)
            {
              P_diag_j[cnt_diag] = fine_to_coarse[j1];
              P_diag_data[cnt_diag++] = A_diag_data[j2];
            }
        }
      j2 = A_offd_i[i1];
      for (j = S_offd_i[i1]; j < S_offd_i[i1+1]; j++)
        {
          j1 = S_offd_j[j];
          while (A_offd_j[j2] != j1) j2++;
          if (pass_marker_offd[j1] == color && A_offd_j[j2] == j1)
            {
              P_offd_j[cnt_offd] = fine_to_coarse_offd[j1];
              P_offd_data[cnt_offd++] = A_offd_data[j2];
            }
        }
    }
  */

  int row_i = hypre_cuda_get_grid_warp_id<1,1>();

   if (row_i >= num_points)
    {
       return;
    }

   int i1 = pass_order[row_i];


   int lane = hypre_cuda_get_lane_id<1>();

   // Read how far we have to shift for the scan into P_[diag/offd]_j
   int diag_shift = row_i == 0 ? 0 : diag_shifts[row_i-1];
   int offd_shift = row_i == 0 ? 0 : offd_shifts[row_i-1];

   int sum = 0;

   int p = 0;
   int q = 0;

   // S_diag
   if (lane < 2)
   {
      p = read_only_load(A_diag_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      int equal = 0;
      int j1 = -1;
      if( j < q )
        {
          j1 =  Soc_diag_j[j];
          if( j1 > -1 && pass_marker[j1] == color )
            equal = 1;
        }

      int pos = warp_prefix_sum(lane, equal, sum);

      if ( j >= q )
      {
         continue;
      }

      if(equal)
        {
          P_diag_j[diag_shift+pos] = fine_to_coarse[j1];
          P_diag_data[diag_shift+pos] = A_diag_data[j];
        }

      diag_shift += sum;
   }

   // S_offd
   if (lane < 2)
   {
      p = read_only_load(A_offd_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
     int equal = 0;
     int j1 = -1;
     if( j < q )
       {
         j1 = Soc_offd_j[j];
         if (j1 > -1 && pass_marker_offd[j1] == color )
           equal = 1;
       }

     int pos = warp_prefix_sum(lane, equal, sum);

     if ( j >= q )
       {
         continue;
       }

     if(equal)
       {
         P_offd_j[offd_shift+pos] = fine_to_coarse_offd[j1];
         P_offd_data[offd_shift+pos] = A_offd_data[j];
       }

     offd_shift += sum;
   }
}

__global__
void hypreCUDAKernel_insert_remaining_weights( HYPRE_Int start,
                                               HYPRE_Int stop,
                                               HYPRE_Int  * pass_order,
                                               HYPRE_Int  * Pi_diag_i,
                                               HYPRE_Int  * Pi_diag_j,
                                               HYPRE_Real * Pi_diag_data,
                                               HYPRE_Int  * P_diag_i,
                                               HYPRE_Int  * P_diag_j,
                                               HYPRE_Real * P_diag_data,
                                               HYPRE_Int  * Pi_offd_i,
                                               HYPRE_Int  * Pi_offd_j,
                                               HYPRE_Real * Pi_offd_data,
                                               HYPRE_Int  * P_offd_i,
                                               HYPRE_Int  * P_offd_j,
                                               HYPRE_Real * P_offd_data )
{
  /*
    j1 = 0;
    for (i = pass_starts[p+1]; i < pass_starts[p+2]; i++)
      {
         i1 = pass_order[i];
         i2 = Pi_diag_i[j1];
         for (j = P_diag_i[i1]; j < P_diag_i[i1+1]; j++)
         {
            P_diag_j[j] = Pi_diag_j[i2];
            P_diag_data[j] = Pi_diag_data[i2++];
         }
         i2 = Pi_offd_i[j1];
         for (j = P_offd_i[i1]; j < P_offd_i[i1+1]; j++)
         {
            P_offd_j[j] = Pi_offd_j[i2];
            P_offd_data[j] = Pi_offd_data[i2++];
         }
         j1++;

     }
  */

  HYPRE_Int num_points = stop-start;

  HYPRE_Int row_i = hypre_cuda_get_grid_warp_id<1,1>();

  if (row_i >= num_points)
   {
      return;
   }

  HYPRE_Int i = row_i + start;
  HYPRE_Int i1 = pass_order[i];

  HYPRE_Int j1 = row_i;

  int lane = hypre_cuda_get_lane_id<1>();
  int p = 0;
  int q = 0;

  // P_diag
  if (lane < 2)
    {
      p = read_only_load(P_diag_i + i1 + lane);
    }
  q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
  p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

  HYPRE_Int loop_cnt = 0;
  HYPRE_Int i2 = Pi_diag_i[j1];
  for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
    {
      if ( j >= q )
        {
          continue;
        }

      HYPRE_Int idx = i2 + lane + loop_cnt*HYPRE_WARP_SIZE;

      P_diag_j[j] = Pi_diag_j[idx];
      P_diag_data[j] = Pi_diag_data[idx];

      loop_cnt++;
    }

  // P_offd
  if (lane < 2)
    {
      p = read_only_load(P_offd_i + i1 + lane);
    }
  q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
  p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

  loop_cnt = 0;
  i2 = Pi_offd_i[j1];
  for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
    {
      if ( j >= q )
        {
          continue;
        }

      HYPRE_Int idx = i2 + lane + loop_cnt*HYPRE_WARP_SIZE;

      P_offd_j[j] = Pi_offd_j[idx];
      P_offd_data[j] = Pi_offd_data[idx];

      loop_cnt++;
    }
}


__global__
void hypreCUDAKernel_generate_Qdiag_j_Qoffd_j( int    num_points,
                                               int    color,
                                               int    *pass_order,
                                               int    *pass_marker,
                                               int    *pass_marker_offd,
                                               int    *fine_to_coarse,
                                               int    *fine_to_coarse_offd,
                                               int    *A_diag_i,
                                               int    *A_diag_j,
                                               double *A_diag_data,
                                               int    *A_offd_i,
                                               int    *A_offd_j,
                                               double *A_offd_data,
                                               int    *Soc_diag_j,
                                               int    *Soc_offd_j,
                                               int    *diag_shifts,
                                               int    *offd_shifts,
                                               int    *Q_diag_j,
                                               double *Q_diag_data,
                                               int    *Q_offd_j,
                                               double *Q_offd_data,
                                               double *w_row_sum )
{
  /* Original host code:
    for (i=0; i < num_points; i++)
      {
         i1 = pass_order[i];
         j2 = A_diag_i[i1]+1;
         for (j = S_diag_i[i1]; j < S_diag_i[i1+1]; j++)
         {
            j1 = S_diag_j[j];
            while (A_diag_j[j2] != j1)
            {
               w_row_sum[i] += A_diag_data[j2];
               j2++;
            }
            if (pass_marker[j1] == color && A_diag_j[j2] == j1)
            {
               Q_diag_j[cnt_diag] = fine_to_coarse[j1];
               Q_diag_data[cnt_diag++] = A_diag_data[j2++];
            }
            else
            {
               w_row_sum[i] += A_diag_data[j2];
               j2++;
            }
         }
         while (j2 < A_diag_i[i1+1])
         {
            w_row_sum[i] += A_diag_data[j2];
            j2++;
         }
         j2 = A_offd_i[i1];
         for (j = S_offd_i[i1]; j < S_offd_i[i1+1]; j++)
         {
            j1 = S_offd_j[j];
            while (A_offd_j[j2] != j1)
            {
               w_row_sum[i] += A_offd_data[j2];
               j2++;
            }
            if (pass_marker_offd[j1] == color && A_offd_j[j2] == j1)
            {
               Q_offd_j[cnt_offd] = fine_to_coarse_offd[j1];
               Q_offd_data[cnt_offd++] = A_offd_data[j2++];
            }
            else
            {
               w_row_sum[i] += A_offd_data[j2];
               j2++;
            }
         }
         while (j2 < A_offd_i[i1+1])
         {
            w_row_sum[i] += A_offd_data[j2];
            j2++;
         }
      }
  */

  int row_i = hypre_cuda_get_grid_warp_id<1,1>();

   if (row_i >= num_points)
    {
       return;
    }

   int i1 = pass_order[row_i];

   int lane = hypre_cuda_get_lane_id<1>();

   // Read how far we have to shift for the scan into Q_[diag/offd]_j
   int diag_shift = row_i == 0 ? 0 : diag_shifts[row_i-1];
   int offd_shift = row_i == 0 ? 0 : offd_shifts[row_i-1];

   int sum = 0;

   int p = 0;
   int q = 0;

   double w_row_sum_i = 0.0;

   // S_diag
   if (lane < 2)
   {
      p = read_only_load(A_diag_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0)+1;

   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      int equal = 0;
      int j1 = -1;
      if( j < q )
        {
          j1 =  Soc_diag_j[j];

          if( j1 > -1 && pass_marker[j1] == color )
            equal = 1;
        }

      int pos = warp_prefix_sum(lane, equal, sum);

      if ( j >= q )
      {
         continue;
      }

      if(equal)
        {
          Q_diag_j[diag_shift+pos] = fine_to_coarse[j1];
          Q_diag_data[diag_shift+pos] = A_diag_data[j];
        }
      else
        {
          w_row_sum_i += A_diag_data[j];
        }

      diag_shift += sum;
   }

   // S_offd
   if (lane < 2)
   {
      p = read_only_load(A_offd_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   for (int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
      int equal = 0;
      int j1 = -1;
      if( j < q )
        {
          j1 = Soc_offd_j[j];

          if (j1 > -1 && pass_marker_offd[j1] == color )
            equal = 1;
        }

      int pos = warp_prefix_sum(lane, equal, sum);

      if ( j >= q )
        {
          continue;
        }

      if(equal)
        {
          Q_offd_j[offd_shift+pos] = fine_to_coarse_offd[j1];
          Q_offd_data[offd_shift+pos] = A_offd_data[j];
        }
      else
        w_row_sum_i += A_offd_data[j];

      offd_shift += sum;
   }

   w_row_sum_i = warp_reduce_sum(w_row_sum_i);

   if(lane == 0)
     w_row_sum[row_i] = w_row_sum_i;
}

__global__
void hypreCUDAKernel_pass_order_count( HYPRE_Int num_points,
                                       HYPRE_Int color,
                                       HYPRE_Int  *points_left,
                                       HYPRE_Int  *pass_marker,
                                       HYPRE_Int  *pass_marker_offd,
                                       HYPRE_Int  *S_diag_i,
                                       HYPRE_Int  *S_diag_j,
                                       HYPRE_Int  *S_offd_i,
                                       HYPRE_Int  *S_offd_j,
                                       HYPRE_Int  *diag_shifts,
                                       HYPRE_Int  *points_left_shifts )
{
  HYPRE_Int row_i = hypre_cuda_get_grid_warp_id<1,1>();

  if (row_i >= num_points)
    {
      return;
    }

  HYPRE_Int i1 = points_left[row_i];

  HYPRE_Int lane = hypre_cuda_get_lane_id<1>();

  HYPRE_Int p = 0;
  HYPRE_Int q = 0;

   // S_diag
   if (lane < 2)
   {
      p = read_only_load(S_diag_i + i1 + lane);
   }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   HYPRE_Int equal = 0;
   HYPRE_Int brk = 0;
   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
   {
     if(!brk)
       {
         if( j < q )
           {
             HYPRE_Int j1 =  S_diag_j[j];
             if( pass_marker[j1] == color )
               equal++;
           }

         if(equal)
           brk=1;

         brk = __any_sync(HYPRE_WARP_FULL_MASK, brk);
       }
   }

   if(brk)
     {
       // Only one warp can increment because of the break
       // so we just need to increment by 1
       if(lane == 0 )
         diag_shifts[row_i] += 1;

       return;
     }

   // S_offd
   // We shouldn't get here if brk got set
   hypre_device_assert(!brk);

   if(!brk)
     {

   if (lane < 2)
     {
       p = read_only_load(S_offd_i + i1 + lane);
     }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   equal = 0;
   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
     {
       if(!brk)
         {
           if( j < q )
             {
               HYPRE_Int j1 = S_offd_j[j];
               if (pass_marker_offd[j1] == color )
                 equal++;
             }

           if(equal)
             brk=1;

           brk = __any_sync(HYPRE_WARP_FULL_MASK, brk);
         }
     }

   if(brk)
     {
       // Only one warp can increment because of the break
       // so we just need to increment by 1
       if(lane == 0 )
         diag_shifts[row_i] += 1;

       return;
     }

     }
   // We shouldn't get here if brk got set
   hypre_device_assert(!brk);

   if(!brk && lane == 0 )
     points_left_shifts[row_i] += 1;
}

__global__
void hypreCUDAKernel_pass_order_pass_marker_update( HYPRE_Int remaining_pts,
                                                    HYPRE_Int current_pass,
                                                    HYPRE_Int * points_left_old,
                                                    HYPRE_Int * pass_marker_old,
                                                    HYPRE_Int * S_diag_i,
                                                    HYPRE_Int * S_diag_j,
                                                    HYPRE_Int * S_offd_i,
                                                    HYPRE_Int * S_offd_j,
                                                    HYPRE_Int * pass_marker_offd,
                                                    HYPRE_Int * diag_shifts,
                                                    HYPRE_Int * points_left_shifts,
                                                    HYPRE_Int * pass_marker,
                                                    HYPRE_Int * pass_order,
                                                    HYPRE_Int * points_left )
{
  HYPRE_Int i = hypre_cuda_get_grid_warp_id<1,1>();

   if (i >= remaining_pts)
    {
       return;
    }

   HYPRE_Int i1 = points_left_old[i];

   HYPRE_Int lane = hypre_cuda_get_lane_id<1>();

   HYPRE_Int p = 0;
   HYPRE_Int q = 0;

   // S_diag
   if (lane < 2)
     {
       p = read_only_load(S_diag_i + i1 + lane);
     }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);

   HYPRE_Int brk = 0;
   for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
     {
       if(!brk)
         {
           HYPRE_Int cond = 0;

           if( j < q )
             {
               HYPRE_Int j1 =  S_diag_j[j];
               cond = (pass_marker_old[j1] == current_pass);
             }

           uint64_t equal = __ballot_sync(HYPRE_WARP_FULL_MASK,cond);

           if(equal)
             {
               uint64_t lowest_lane_mask = equal & -equal;

               // Figure out what the lane index is
               HYPRE_Int bscount = 1;
               for( HYPRE_Int bs = 0; bs < HYPRE_WARP_SIZE; bs++ )
                 {
                   if(lowest_lane_mask != 1)
                     {
                       lowest_lane_mask = lowest_lane_mask >> 1;
                       bscount++;
                     }
                 }

               if(lane == bscount+1)
                 {
                   pass_marker[i1] = current_pass + 1;
                   pass_order[diag_shifts[i]] = i1;
                 }

               brk=1;
             }

           brk = __any_sync(HYPRE_WARP_FULL_MASK,brk);
         }
     }

   // S_offd
   if (lane < 2)
     {
       p = read_only_load(S_offd_i + i1 + lane);
     }
   q = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 1);
   p = __shfl_sync(HYPRE_WARP_FULL_MASK, p, 0);
   if(!brk)
     {
       for (HYPRE_Int j = p + lane; __any_sync(HYPRE_WARP_FULL_MASK, j < q); j += HYPRE_WARP_SIZE)
         {
           if(!brk)
             {
               HYPRE_Int cond = 0;

               if( j < q )
                 {
                   HYPRE_Int j1 =  S_offd_j[j];
                   cond = (pass_marker_offd[j1] == current_pass);
                 }

               uint64_t equal = __ballot_sync(HYPRE_WARP_FULL_MASK,cond);

               if(equal)
                 {
                   uint64_t lowest_lane_mask = equal & -equal;

                   // Figure out what the lane index is
                   HYPRE_Int bscount = 1;
                   for( HYPRE_Int bs = 0; bs < HYPRE_WARP_SIZE; bs++ )
                     {
                       if(lowest_lane_mask != 1)
                         {
                           lowest_lane_mask = lowest_lane_mask >> 1;
                           bscount++;
                         }
                     }

                   if(lane == bscount+1)
                     {
                       pass_marker[i1] = current_pass + 1;
                       pass_order[diag_shifts[i]] = i1;
                     }

                   brk=1;
                 }

               brk = __any_sync(HYPRE_WARP_FULL_MASK,brk);
             }
         }
     }

   if(!brk)
     {
       points_left[points_left_shifts[i]] = i1;
     }
}
