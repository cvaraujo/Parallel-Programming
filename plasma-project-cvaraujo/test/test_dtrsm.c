/**
 *
 * @file
 *
 *  PLASMA is a software package provided by:
 *  University of Tennessee, US,
 *  University of Manchester, UK.
 *
 * @generated from /home/luszczek/workspace/plasma/bitbucket/plasma/test/test_ztrsm.c, normal z -> d, Fri Sep 28 17:38:32 2018
 *
 **/
#include "test.h"
#include "flops.h"
#include "plasma.h"
#include <plasma_core_blas.h>
#include "core_lapack.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <omp.h>

#define REAL

#define A(i_, j_) A[(i_) + (size_t)lda*(j_)]

/***************************************************************************//**
 *
 * @brief Tests DTRSM.
 *
 * @param[in,out] param - array of parameters
 * @param[in]     run - whether to run test
 *
 * Sets flags in param indicating which parameters are used.
 * If run is true, also runs test and stores output parameters.
 ******************************************************************************/
void test_dtrsm(param_value_t param[], bool run)
{
    //================================================================
    // Mark which parameters are used.
    //================================================================
    param[PARAM_SIDE   ].used = true;
    param[PARAM_UPLO   ].used = true;
    param[PARAM_TRANSA ].used = true;
    param[PARAM_DIAG   ].used = true;
    param[PARAM_DIM    ].used = PARAM_USE_M | PARAM_USE_N;
    param[PARAM_ALPHA  ].used = true;
    param[PARAM_PADA   ].used = true;
    param[PARAM_PADB   ].used = true;
    param[PARAM_NB     ].used = true;
    if (! run)
        return;

    //================================================================
    // Set parameters.
    //================================================================
    plasma_enum_t side = plasma_side_const(param[PARAM_SIDE].c);
    plasma_enum_t uplo = plasma_uplo_const(param[PARAM_UPLO].c);
    plasma_enum_t transa = plasma_trans_const(param[PARAM_TRANSA].c);
    plasma_enum_t diag = plasma_diag_const(param[PARAM_DIAG].c);

    int m = param[PARAM_DIM].dim.m;
    int n = param[PARAM_DIM].dim.n;
    int Am;

    if (side == PlasmaLeft) {
        Am = m;
    }
    else {
        Am = n;
    }

    int lda = imax(1, Am + param[PARAM_PADA].i);
    int ldb = imax(1, m + param[PARAM_PADB].i);

    int test = param[PARAM_TEST].c == 'y';
    double tol = param[PARAM_TOL].d * LAPACKE_dlamch('E');

#ifdef COMPLEX
    double alpha = param[PARAM_ALPHA].z;
#else
    double alpha = creal(param[PARAM_ALPHA].z);
#endif

    //================================================================
    // Set tuning parameters.
    //================================================================
    plasma_set(PlasmaTuning, PlasmaDisabled);
    plasma_set(PlasmaNb, param[PARAM_NB].i);

    //================================================================
    // Allocate and initialize arrays.
    //================================================================
    double *A =
        (double*)malloc((size_t)lda*n*sizeof(double));
    assert(A != NULL);

    double *B =
        (double*)malloc((size_t)ldb*n*sizeof(double));
    assert(B != NULL);

    int *ipiv;
    ipiv = (int*)malloc((size_t)Am*sizeof(int));
    assert(ipiv != NULL);

    int seed[] = {0, 0, 0, 1};
    lapack_int retval;

    //=================================================================
    // Initialize the matrices.
    // Factor A into LU to get well-conditioned triangular matrices.
    // Use L for unit triangle, and U for non-unit triangle,
    // transposing as necessary.
    // (There is some danger, as L^T or U^T may be much worse conditioned
    // than L or U, but in practice it seems okay.
    // See Higham, Accuracy and Stability of Numerical Algorithms, ch 8.)
    //=================================================================
    retval = LAPACKE_dlarnv(1, seed, (size_t)lda*n, A);
    assert(retval == 0);

    LAPACKE_dgetrf(CblasColMajor, Am, Am, A, lda, ipiv);

    if (diag == PlasmaUnit && uplo == PlasmaUpper) {
        // U = L^T
        for (int j = 0; j < Am; j++) {
            for (int i = 0; i < j; i++) {
                A(i,j) = A(j,i);
            }
        }
    }
    else if (diag == PlasmaNonUnit && uplo == PlasmaLower) {
        // L = U^T
        for (int j = 0; j < Am; j++) {
            for (int i = 0; i < j; i++) {
                A(j,i) = A(i,j);
            }
        }
    }

    retval = LAPACKE_dlarnv(1, seed, (size_t)ldb*n, B);
    assert(retval == 0);

    double *Bref = NULL;
    if (test) {
        Bref = (double*)malloc(
            (size_t)ldb*n*sizeof(double));
        assert(Bref != NULL);

        memcpy(Bref, B, (size_t)ldb*n*sizeof(double));
    }

    //================================================================
    // Run and time PLASMA.
    //================================================================
    plasma_time_t start = omp_get_wtime();

    plasma_dtrsm(
        side, uplo,
        transa, diag,
        m, n,
        alpha, A, lda,
               B, ldb);

    plasma_time_t stop = omp_get_wtime();
    plasma_time_t time = stop-start;

    param[PARAM_TIME].d = time;
    param[PARAM_GFLOPS].d = flops_dtrsm(side, m, n) / time / 1e9;

    //================================================================
    // Test results by checking the residual
    // ||alpha*B - A*X|| / (||A||*||X||)
    //================================================================
    if (test) {
        double zone  =  1.0;
        double zmone = -1.0;
        double work[1];

        // LAPACKE_[ds]lantr_work has a bug (returns 0)
        // in MKL <= 11.3.3 (at least). Fixed in LAPACK 3.6.1.
        // For now, call LAPACK directly.
        // LAPACK_dlantr is a macro for correct name mangling (e.g.
        // adding _ at the end) of the Fortran symbol.
        // The macro is either defined in lapacke.h, or in the file
        // plasma_core_lapack_d.h for the use with MKL.
        char normc = 'F';
        char uploc = lapack_const(uplo);
        char diagc = lapack_const(diag);
        double Anorm = LAPACK_dlantr(&normc, &uploc, &diagc,
                                     &Am, &Am, A, &lda, work);
        //double Anorm = LAPACKE_dlantr_work(
        //                   LAPACK_COL_MAJOR, 'F', lapack_const(uplo),
        //                   lapack_const(diag), Am, Am, A, lda, work);

        double Xnorm = LAPACKE_dlange_work(
                           LAPACK_COL_MAJOR, 'F', m, n, B, ldb, work);

        // B = A*X
        cblas_dtrmm(
            CblasColMajor,
            (CBLAS_SIDE)side, (CBLAS_UPLO)uplo,
            (CBLAS_TRANSPOSE)transa, (CBLAS_DIAG)diag,
            m, n,
            (zone), A, lda,
            B, ldb);

        // B -= alpha*Bref
        cblas_dscal((size_t)ldb*n, (alpha), Bref, 1);
        cblas_daxpy((size_t)ldb*n, (zmone), Bref, 1, B, 1);

        double error = LAPACKE_dlange_work(
                           LAPACK_COL_MAJOR, 'F', m, n, B, ldb, work);
        if (Anorm * Xnorm != 0)
            error /= (Anorm * Xnorm);

        param[PARAM_ERROR].d = error;
        param[PARAM_SUCCESS].i = error < tol;
    }

    //================================================================
    // Free arrays.
    //================================================================
    free(A);
    free(B);
    free(ipiv);
    if (test)
        free(Bref);
}
