\name{MagmaEigenNonsym}
\alias{MagmaEigenNonsym}
\docType{package}
\title{
 MagmaEigenNonsym - provides a fast replacement for the eigen() function, using a 2 stage GPU based MAGMA library routine. Also provides a function that returns the sqrt and inverse sqrt of an input matrix.
}
\description{
Implements the symmetric eigenvalue decomposition using the MAGMA library (http://icl.cs.utk.edu/magma/index.html) 2-stage multi-GPU implementation with 64 bit integer interface. Currently only for real symmetric matricies. In this case it provides a direct replacement for R function eigen(symmetric=T) and also provides an efficient function for producing the square root and inverse square root of the input matrix. The package uses the 64 bit integer MAGMA library by a client-server shared memory architecture. This removes the problem that can arrise with larger datasets where R only provides the 32 bit BLAS/LAPACK interface.  The server side code checks how many GPUs are present so the client side R package code does not require CUDA/other interface to be present. The server side code requires the MAGMA library to be available (http://icl.cs.utk.edu/magma/) compiled as position independent code (shared library) with a multi-threaded high performance LAPACK and -DMAGMA_ILP64 defined (an example make.inc file should be available in MAGMA download package). The server code also requires an OpenCL library to be installed which is used to get the number of GPUs present on a system. This package can be used in conjunction with HiPLARb and HiPLARM that require high performance, but single-threaded BLAS routines, which will then degrade the performance of some routines (such as eigen() and possibly svd()) which require a multi threaded BLAS to operate effectively.

Compilation of the R package the 'client side' code:
Optionally set MAGMA_HOME and CUDA_ROOT as per the server side instructions. This will alow the client to compile the server code during package install.
Setting the OpenCL platform string and device type:


Compilation of the 'server side' code:
Requires environment variables MAGMA_HOME to be set to where the MAGMA install is present. MAGMA_HOME=/usr/local/magma is the default.
The CUDA version of MAGMA requires CUDA_ROOT to be set CUDA_ROOT=/usr/local/cuda is the default.
The server code uses OpenCL to determine how many GPUs are present on the system it is being run on.
The default platform string is stored in a file in <R library path>/rcppMagamSYEVD/extdata/platformstring.txt and contains the default platform string "NVidia", and the default device is set to "GPU". See the platformstring.txt file for other options.
If these libraries and variables are set correctly then the server side code will be compiled automatically when the package is installed. If failing installation on install then whenerver the package is loaded into the R environment using library(MagmaEigenNonsym) the system will attempt to compile the server code.


N.B. Calling the eigen_nonsym_mgpu() function with argument overwrite=TRUE will cause an *overwrite of the input matrix data* with the eigenvectors of the original matrix data (if they are requested). This is done to potentialy reduce the memory footprint of the function. If overwrite=TRUE then please ensure the original matrix is copied if the data needs to be used after the function is called. Using overwrite=FALSE will return the usual list of results in $vectors and $values list items of the result object.

}
\details{
\tabular{ll}{
Package: \tab MagmaEigenNonsym\cr
Type: \tab Package\cr
Version: \tab 1.0.0\cr
Date: \tab 2016-02-01\cr
License: \tab GPL-3 + file LICENCE \cr
LazyLoad: \tab yes\cr
}

}
\author{
Josh Bowden, CSIRO

Maintainer: Josh Bowden <josh.bowden@csiro.au>
}
\references{
Stanimire Tomov, Jack Dongarra, Marc Baboulin, Towards dense linear algebra for hybrid GPU accelerated manycore systems, Parallel Computing, Volume 36, Issues 5-6, June 2010, Pages 232-240, ISSN 0167-8191, http://dx.doi.org/10.1016/j.parco.2009.12.005.

@article{
    title     = {{Towards dense linear algebra for hybrid GPU accelerated manycore systems}},
    author    = {Stanimire Tomov and Jack Dongarra and Marc Baboulin},
    booktitle = {Parallel Matrix Algorithms and Applications},
    doi       = {10.1016/j.parco.2009.12.005},
    issn      = {0167-8191},
    journal   = {Parallel Computing},
    month     = jun,
    number    = {5-6},
    pages     = {232--240},
    posted-at = {2010-12-17 09:48:58},
    priority  = {2},
    volume    = {36},
    year      = {2010}
}

Solca, Raffaele; Haidar, Azzam; Tomov, Stanimire; Schulthess, Thomas C.; Dongarra, Jack, "Abstract: A Novel Hybrid CPU-GPU Generalized Eigensolver for Electronic Structure Calculations Based on Fine Grained Memory Aware Tasks," in High Performance Computing, Networking, Storage and Analysis (SCC), 2012 SC Companion: , vol., no., pp.1338-1339, 10-16 Nov. 2012
doi: 10.1109/SC.Companion.2012.173

@article{Haidar:2014:NHC:2747699.2747703,
 author = {Haidar, Azzam and Tomov, Stanimire and Dongarra, Jack and Solc\`{a}, Raffaele and Schulthess, Thomas},
 title = {A Novel Hybrid CPU-GPU Generalized Eigensolver for Electronic Structure Calculations Based on Fine-grained Memory Aware Tasks},
 journal = {Int. J. High Perform. Comput. Appl.},
 issue_date = {May       2014},
 volume = {28},
 number = {2},
 month = may,
 year = {2014},
 issn = {1094-3420},
 pages = {196--209},
 numpages = {14},
 url = {http://dx.doi.org/10.1177/1094342013502097},
 doi = {10.1177/1094342013502097},
 acmid = {2747703},
 publisher = {Sage Publications, Inc.},
 address = {Thousand Oaks, CA, USA},
 keywords = {Eigensolver, GPU, electronic structure calculations, generalized eigensolver, high performance, hybrid, multicore, two-stage},
} 

}

\keyword{ MagmaEigenNonsym, magma, MAGMA, eigen, GPU, EVD  }
\seealso{
eigen
}
\examples{

  n  <- 100
	library(MagmaEigenNonsym)
	RunServer(numGPUsWanted=1, matrixMaxDimension=n, memName="/syevd_mem", semName="/syevd_sem", print=0)
	set.seed(42)
        K <- matrix(sample(c(0,1), n*n, TRUE), nrow=n)
        K <- tcrossprod(K)
        Z <- matrix(data=0, nrow=2*n, ncol=n)
        for(ii in 1:(2*n)){
                indx <- sample(1:n, 1, FALSE)
                  Z[ii, indx] <- 1
          }
          res <- K %*% crossprod(Z, Z)

  # GPU based
  eigGPU <- MagmaEigenNonsym::eigen_nonsym_mgpu(res , symmetric = FALSE , only_values=FALSE)
  indx <- order(eigGPU$values, decreasing=TRUE)
  eigGPU$vectors <-  eigGPU$vectors[, indx]
  eigGPU$values <- eigGPU$values[indx]

  valuesGPU <-  eigGPU$values
  tmpmat <- Z %*% eigGPU$vectors
  vectorsGPU <-  qr.Q(qr(tmpmat), complete = TRUE)
  print(" in here ")

  eig <- eigen(res, symmetric=FALSE, EISPACK=TRUE)
  valuesCPU <- eig$values
  tmpmat <- Z %*% eig$vector
  vectorsCPU <- qr.Q(qr(tmpmat), complete = TRUE)

  print(" Eigenvvectors from GPU based code")
  print(vectorsGPU[1:5, 1:5])
  print(" Eigenvectors from CPU based code")
  print(vectorsCPU[1:5, 1:5])




	# the server program will be terminated when the R session exits or the package is unloaded
	# to terminate manually:
	StopServer()  # Client signals to server to terminate
  

}
