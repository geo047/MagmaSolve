pkgname <- "MagmaSolve"
source(file.path(R.home("share"), "R", "examples-header.R"))
options(warn = 1)
library('MagmaSolve')

base::assign(".oldSearch", base::search(), pos = 'CheckExEnv')
base::assign(".old_wd", base::getwd(), pos = 'CheckExEnv')
cleanEx()
nameEx("MagmaSolve")
### * MagmaSolve

flush(stderr()); flush(stdout())

### Name: MagmaSolve
### Title: MagmaSolve - provides a fast replacement for the eigen()
###   function, using a 2 stage GPU based MAGMA library routine. Also
###   provides a function that returns the sqrt and inverse sqrt of an
###   input matrix.
### Aliases: MagmaSolve
### Keywords: MagmaSolve, magma, MAGMA, eigen, GPU, EVD

### ** Examples

# setup
set.seed(101)
n <- 5
ngpu <- 4
K <- matrix(sample(1:100, n*n, TRUE), nrow=n)
res  <- tcrossprod(K)

# CPU based
 library(MagmaSolve)
 MagmaSolve::RunServer( matrixMaxDimension=n,  numGPUsWanted=ngpu, memName="/syevd_mem", semName="/syevd_sem", print=0)

  print("A")
  print(res[1:5,1:5])
  print("A^-1")
  print(solve(res)[1:5,1:5])
  invGPU  <- MagmaSolve::solve_mgpu(res)

 ## CPU 
   invCPU  <- solve(res)


   print(invGPU[1:5, 1:5])
   print("---------------------")
   print(invCPU[1:5, 1:5])
 
  StopServer()  # Client signals to server to terminate
  




### * <FOOTER>
###
cleanEx()
options(digits = 7L)
base::cat("Time elapsed: ", proc.time() - base::get("ptime", pos = 'CheckExEnv'),"\n")
grDevices::dev.off()
###
### Local variables: ***
### mode: outline-minor ***
### outline-regexp: "\\(> \\)?### [*]+" ***
### End: ***
quit('no')
