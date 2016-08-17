/*
 * Simple application to test pj_compensate. Assumes defaults for OpenMPI 1.6.5
 * SM BTL.
 */
#include <mpi.h>
#include <stdio.h>

enum Ranks {
  SLAVE1,
  SLAVE2,
  MASTER
};

#define TAG 0
#define SYNCSIZE 4026
#define ASYNCSIZE 4000

int
main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != MASTER + 1) {
    if (!rank)
      fprintf(stderr, "Expected -np %d\n", MASTER + 1);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  char buffer[SYNCSIZE];
  if (rank == MASTER) {
    MPI_Request r, r2;
    /* Synchronous send, OK */
    MPI_Send(buffer, SYNCSIZE, MPI_CHAR, SLAVE2, TAG, MPI_COMM_WORLD);
    /* Aynchronous send, OK */
    MPI_Send(buffer, ASYNCSIZE, MPI_CHAR, SLAVE1, TAG, MPI_COMM_WORLD);
    /* Synchronous Isend, OK */
    MPI_Isend(buffer, SYNCSIZE, MPI_CHAR, SLAVE1, TAG, MPI_COMM_WORLD, &r);
    /* Aynchronous Isend, NOT OK */
    MPI_Isend(buffer, ASYNCSIZE, MPI_CHAR, SLAVE1, TAG, MPI_COMM_WORLD, &r2);
    /* Wait for the sync Isend only */
    MPI_Wait(&r, MPI_STATUS_IGNORE);
  } else if (rank == SLAVE1) {
    MPI_Request r;
    /* Irecv, NOT supported */
    MPI_Irecv(buffer, ASYNCSIZE, MPI_CHAR, MASTER, TAG, MPI_COMM_WORLD, &r);
    /* Recv, OK */
    MPI_Recv(buffer, SYNCSIZE, MPI_CHAR, MASTER, TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(buffer, ASYNCSIZE, MPI_CHAR, MASTER, TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    /* Wait for the Irecv */
    MPI_Wait(&r, MPI_STATUS_IGNORE);
  } else if (rank == SLAVE2) {
    /* All ok */
    MPI_Recv(buffer, SYNCSIZE, MPI_CHAR, MASTER, TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  }
  MPI_Finalize();
}
