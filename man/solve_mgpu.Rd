% Generated by roxygen2 (4.1.0): do not edit by hand
% Please edit documentation in R/RcppExports.R
\name{solve_mgpu}
\alias{solve_mgpu}
\title{Function used to obtain the eigenvalue decomposition (EVD) of a non-symmetric matrix using a MAGMA 2-stage multi-gpu EVD algorithm}
\usage{
solve_mgpu(matrix)
}
\arguments{
\item{matrix}{- the input matrix to be used in eigenvalue decomposition. It is assumed to be square}
}
\value{
A list that contains the eigenvalues and if requested the eignenvectors. If overwrite==TRUE then the eignevectors are copied into the ***input matrix***
}
\description{
This function performs the eigenvalue decomposition of the input matrix and returns the eigenvalues and (if requested) the
eigenvectors of the input matrix in the returned list, identical to the base R eigen() function. If overwrite=TRUE then the eigenvectors are copied into the ***input matrix*** and the original matrix
data is overwritten.
The method involves the offload of the matrix data to a seperate syevd_server executable by copying data into a shared memory area and
signalling to the server that the data is availble. This function will block until the server has completed the decomposition. The
function checks that the input is square, however it does not check that the matrix is symmetric.
N.B. The maximum size allowed of the input matrix is goverend by what was provided in the MagmaSolve::RunServer() function. The server
will automatically be restarted with a larger shared memory area if user wants to perorm EVD on a larger matrix.
}

