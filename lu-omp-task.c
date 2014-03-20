/*
 * Copyright (C) 2013 University of Houston (HPCTools Laboratory).
 *  
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *  
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *  
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *  
 * Contact information:
 * Priyanka Ghosh & Yonghong Yan
 * http://www.cs.uh.edu/~hpctools
 *
 * Original Sequential version by: Elkin E. Garcia
 *
 */

/*
 Blocked LU Decomposition parallelized using OpenMP tasks
 **** VERSION WITH TASKWAIT ****

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <omp.h>
#include <math.h>


//#define DEBUG 1
//#define CHECK 1
#define MEASURE 1
#define T 999

void Print_Matrix (double *v, int M, int N);
void ProcessDiagonalBlock(double *A, int L1, int N);
void ProcessBlockOnRow(double *A, double *D, int L1, int L3, int N);
void ProcessBlockOnColumn(double *A, double *D, int L1, int L2, int N);

void ProcessInnerBlock(double *A, double *R, double *C, int L1, int L2, int L3, int N);
void stepLU(double *A, int Block, int offset,  int N );
void InitMatrix2(double *A, int N);
void InitMatrix3(double *A, int N);

void stage1(double *A, int offset, int *sizedim, int *start, int N, int M);
void stage2(double *A, int offset, int *sizedim, int *start, int N, int M);
void stage3(double *A, int offset, int *sizedim, int *start, int N, int M);

void lu2 (double *A, int N);
int itr = 0;

unsigned long GetTickCount()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);

    return (tv.tv_sec * 1000000) + (tv.tv_usec);
}

int N = 100;
int Block = 1;
int M=1; //number of blocks per dimension

int main (int argc, char *argv[])
{
	double *A,*A2,*L,*U, temp2;
	int i,j,k;
	int temp=0;
	int offset = 0;
	double t1,t2;

        if (argc < 3)
	{
		printf("Usage: ./lu <Matrix size> <number of blocks per dimension>\n");
		exit(1);
	}

	if( argc > 1 )
		N = atoi(argv[1]);

	if( argc > 2 )
		M = atoi(argv[2]);

	A = (double *)malloc (N*N*sizeof(double));
	A2 = (double *)malloc (N*N*sizeof(double));
	L = (double *)malloc (N*N*sizeof(double));
	U = (double *)malloc (N*N*sizeof(double));
	if( A==NULL || A2==NULL || L==NULL || U==NULL) {
		printf("Can't allocate memory\n");
		exit(1);
	}

	/* INITIALIZATION */
	InitMatrix3(A,N);
	for(i=0; i<N*N; i++) {
		A2[i] = A[i]; // Copy of A for verification of correctness
		L[i] = 0;
		U[i] = 0;
	}


	int *sizedim;
	int *start;
	int R; //Remain

	sizedim = (int*)malloc(M*sizeof(int));
	start = (int*)malloc(M*sizeof(int));
	R = N;

	t1 = GetTickCount();
#pragma omp parallel
	{
#pragma omp master
		{
			while (N-offset>M){

				for (i=0;i<M;i++){
					if (i<R%M){
						sizedim[i]=R/M+1;
						start[i]=(R/M+1)*i;
					}
					else{
						sizedim[i]=R/M;
						start[i]=(R/M+1)*(R%M)+(R/M)*(i-R%M);
					}
				}


				stage1(A, offset, sizedim, start, N, M);
				stage2(A, offset, sizedim, start, N, M);
				stage3(A, offset, sizedim, start, N, M);

				offset+=sizedim[0];
				R=R-sizedim[0];

			} //end of while
		} //end of master
	} //end of parallel region
	ProcessDiagonalBlock(&A[offset*N+offset], N-offset, N);

	t2 = GetTickCount();

	printf("Time for LU-decomposition in secs: %f \n", (t2-t1)/1000000);



#ifdef CHECK
	/* PROOF OF CORRECTNESS */

	for (i=0;i<N;i++)
		for (j=0;j<N;j++)
			if (i>j)
				L[i*N+j] = A[i*N+j];
			else
				U[i*N+j] = A[i*N+j];
	for (i=0;i<N;i++)
		L[i*N+i] = 1;


	for (i=0;i<N;i++)
		for (j=0;j<N;j++){
			temp2=0;
			for (k=0;k<N;k++)
				temp2+=L[i*N+k]*U[k*N+j];
			if ((A2[i*N+j]-temp2)/A2[i*N+j] >0.1 || (A2[i*N+j]-temp2)/A2[i*N+j] <-0.1)
				temp++;
		}
	printf("Errors = %d \n", temp);
#endif
	return;

}

void stage1(double *A, int offset, int *sizedim, int *start, int N, int M)
{
	// always start[0] is 0, so it is not used;

	ProcessDiagonalBlock(&A[offset*N+offset], sizedim[0], N);
}

void stage2(double *A, int offset, int *sizedim, int *start, int N, int M)
{
	int x=offset, y=offset;
	int B = sizedim[0]; 
	int i;
	int L1 = sizedim[0];
	int L2, L3;
	/* Processing only one big block in column and row */
	for (i=1;i<M;i++){
		L2 = sizedim[i];
		L3 = sizedim[i];
#pragma omp task firstprivate(i, L1, L2,x, y, N) 
		{
			ProcessBlockOnColumn(&A[(x+start[i])*N+y], &A[x*N+y], L1, L2, N);
		}
#pragma omp task firstprivate(i, L1, L2,x, y, N)
		{
			ProcessBlockOnRow(&A[x*N+(y+start[i])], &A[x*N+y], L1, L3, N);
		}
	}
#pragma omp taskwait
}

void stage3(double *A, int offset, int *sizedim, int *start, int N, int M)
{
	int x=offset, y=offset;
	int B = sizedim[0];
	int i,j;
	int L1 = sizedim[0];
	int L2, L3;
	for (i=1;i<M;i++)
		for (j=1;j<M;j++){
			L2 = sizedim[i];
			L3 = sizedim[j];
#pragma omp task firstprivate(i,j,M,N,x,y,L1,L2,L3) 
			{
			   ProcessInnerBlock(&A[(x+start[i])*N+(y+start[j])],  &A[x*N+(y+start[j])], &A[(x+start[i])*N+y], L1, L2, L3, N);
			}
		}
#pragma omp taskwait

}

void ProcessDiagonalBlock(double *A, int L1, int N)
	/* *A is a pointer to the block processed */
	/* The size of the diagonal block is L1xL1 */
	/* N is the size of the matrix in one dimension */
{
	int i,j,k;
	for (i=0;i<L1;i++)
		for (j=i+1;j<L1;j++){
			A[j*N+i]/=A[i*N+i];
			/*       DAXPY(&A[j*N+(i+1)],&A[i*N+(i+1)] ,-A[j*N+i],L1-(i+1),1); */
			for (k=i+1;k<L1;k++)
				A[j*N+k] = A[j*N+k] - A[j*N+i]*A[i*N+k];
		}
}

void ProcessBlockOnColumn(double *A, double *D, int L1, int L2, int N)
	/* The size of the column block is L2xL1 */
{
	/* *A is a pointer to the column block processed */
	/* *D is a pointer to the diagonal block required */
	/* The size of the column block is L2xL1 */
	/* The size of the diagonal block is L1xL1 */
	int i,j,k;
	for (i=0;i<L1;i++)
		for (j=0;j<L2;j++){
			A[j*N+i]/=D[i*N+i];
			/*       DAXPY(&A[j*N+(i+1)],&D[i*N+(i+1)],-A[j*N+i],L1-(i+1),1); */
			for (k=i+1;k<L1;k++)
				A[j*N+k]+=-A[j*N+i]*D[i*N+k];
		}
}

void ProcessBlockOnRow(double *A, double *D, int L1, int L3, int N)
	/* The size of the row block is L2xL1 */
{
	/* *A is a pointer to the row block processed */
	/* *D is a pointer to the diagonal block required */
	/* The size of the row block is L1xL3 */
	/* The size of the diagonal block is L1xL1 */
	int i,j,k;
	for (i=0;i<L1;i++)
		for (j=i+1;j<L1;j++)
			/*       DAXPY(&A[N*j],&A[N*i],-D[j*N+i],L3,1); */
			for (k=0;k<L3;k++)
				A[j*N+k]+=-D[j*N+i]*A[i*N+k];
}

void ProcessInnerBlock(double *A, double *R, double *C, int L1, int L2, int L3, int N)
{
	/* *A is a pointer to the inner block processed */
	/* *R is a pointer to the row block required */
	/* *C is a pointer to the column block required */
	/* The size of the row block is L1xL3 */
	/* The size of the column block is L2xL1 */
	/* The size of the inner block is L2xL3 */
	int i,j,k;
	for (i=0;i<L1;i++)
		for (j=0;j<L2;j++)
			/*       DAXPY(&A[N*j],&R[N*i],-C[j*N+i],L3,1); */
			for (k=0;k<L3;k++)
				A[j*N+k]+=-C[j*N+i]*R[i*N+k];

}

void Print_Matrix (double *v, int M, int N)
{

	int i,j;

	printf("\n");
	for (i=0;i<M;i++){
		for (j=0;j<N;j++)
			printf("%.2f,",v[i*N+j]);
		printf("\n");
	}
	printf("\n");

	return;
}

void InitMatrix2(double *A, int N)
{
	long long i, j,k;

	for (i=0;i<N*N;i++)
		A[i]=0;
	for (k=0;k<N;k++)
		for (i = k; i < N; i++)
			for (j = k; j < N; j++)
				A[i * N + j] +=1;
}

void InitMatrix3(double *A, int N)
{
	long long i,j,k;
	double *L, *U;
	L = (double*) malloc(N*N*sizeof(double));
	U = (double*) malloc(N*N*sizeof(double));
#pragma omp parallel for
	for (i=0;i<N;i++)
		for (j=0;j<N;j++){
			A[i*N+j]=0;
			if (i>=j)
				L[i*N+j] = i-j+1;
			else
				L[i*N+j] = 0;
			if (i<=j)
				U[i*N+j] = j-i+1;
			else
				U[i*N+j] = 0;
		}
#pragma omp parallel for
	for (i=0;i<N;i++)
		for (j=0;j<N;j++)
			for (k=0;k<N;k++)
				A[i*N+j]+=L[i*N+k]*U[k*N+j];
}
