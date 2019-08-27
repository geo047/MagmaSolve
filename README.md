# eigen_mgpu()

Description:
An R package for matrix and linear algebra operations on large datasets using the MAGMA based linear algebra library 64 bit integer based routines. The functions can be used as replacement for several CPU-bound R functions. It provides an interface to the 64 bit MAGMA library by a client-server shared memory architecture. This removes the problem that can arrise with larger datasets where R only provides the 32 bit BLAS/LAPACK interface.  The server side code checks how many GPUs are present so the client side R package code does not require CUDA/other interface to be present. The server side code requires the MAGMA library available (http://icl.cs.utk.edu/magma/) compiled as position independent code (shared library) with a multi-threaded high performance LAPACK and -DMAGMA_ILP64 defined (an example make.inc file should be available in MAGMA download package).

Compilation of the 'server side' code:
Requires environment variables MAGMA_HOME to be set to where the MAGMA install is present. MAGMA_HOME=/usr/local/magma is the default.
The CUDA version of MAGMA requires CUDA_ROOT to be set CUDA_ROOT=/usr/local/cuda is the default.
If these variables are set correctly then the server side code will be compiled automatically when the package is installed, and failing on install then whenerver the package is loaded into the R environment using library(MagmaSolve).


Compilation of the R package the 'client side' code:
Optionally set MAGMA and CUDA_ROOT as per the server side instructions. This will allow the client to compile the server code during package install.

Setting the OpenCL platform string:
N.B. Currently the OpenCL code is not used and a _USE_CUDA flag is enabled in the inst/src/Makefile to build the solve_server code using CUDA functions to determine the maximum number   of GPUs.
Instructions for OpenCL version:
The server code checks a string to determine how many GPUs are present on the system it is being run on.
The default platform string is stored in a file in <R library path>/MagmaEigenNonsym/extdata/platformstring.txt and contains the default platform string "NVidia"
This string will need to be changed to another platform (i.e. AMD or Intel) if the MAGMA library being used is supporting that platform.
