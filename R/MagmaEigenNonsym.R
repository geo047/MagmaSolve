#' @useDynLib MagmaEigenNonsym
#' @importFrom Rcpp sourceCpp

#' Creates the server executable.
#' @param environmentSetup  - type (string) e.g. "env LD_LIBRARY_PATH=/usr/local/magma-1.7.1:$LD_LIBRARY_PATH "
#' @param target            - type (string) The make target e.g. all | clean | dist-clean | install
#' @return A character vector containing output of the make process
#' @export
#' @description 
#' Creates nonsyevd_server executable using a call to 'make' and the makefile/make.inc information present in the <package root>/src directory.
#' Users have to set the following variable in make.inc :
#'      MAGMALIB  =$(MAGMA_HOME)/lib          # The path to the MAGMA library
#' 
#' Users must ensure that MAGMA_HOME and possibly CUDA_ROOT environment variables have been set in the shells environment
#' or the variables can be set using environmentSetup paramater.
#' e.g. "environmentSetup="env MAGMA_HOME=/data/bow355/A_275ERCP_R_MAGMA/magma-1.7.0 CUDA_ROOT=/cm/shared/apps/cuda65/toolkit/6.5.14 "
MakeServer <- function( environmentSetup="", target = "all" )
{ 
  # R.home() system.file(package="MagmaEigenNonsym") Sys.info()['sysname']
  package_path <- path.package("MagmaEigenNonsym")
  server_src_path <- paste0(package_path,"/src")
     
  magma_path <- Sys.getenv("MAGMA_HOME")
  if(nchar(magma_path) == 0)
  {
    if (grepl("MAGMA_HOME",environmentSetup) == FALSE) 
      stop("MAGMA_EVD_CLIENT Error: MAGMA_HOME environment variable is not set. Check that MAGMA GPU Linear Algebra library is installed and set MAGMA_HOME to its installation directory")
  }
  
  if (nchar(environmentSetup)==0 ) {
    environmentSetup <- ""      # : is the 'null' command in bash
  } 
  
  # test if we have an "env " at the start of the string (with or without blanks)
  if (grepl("env",environmentSetup) == FALSE) {
    environmentSetup <- paste0(" env ", environmentSetup )
  }  
     
  # save these paths for use later
  envfile <- paste0(package_path,"/extdata/environmentSetup.rds")
  saveRDS(environmentSetup,  file=envfile)  
  
  sysstring <- paste0(" cd ", server_src_path, " ; ", environmentSetup,  "  make ", target) 
  
  if (target=="all")
    message("MAGMA_EVD_CLIENT Info: MakeServer() called as: ", sysstring)
  
  return( system(sysstring) )
  
}


#' Creates the R client side shared memory region and then launches a server process which is given access to the shared region. The server then waits for the R client to give it a matrix on which it will compute the eigenvalue decomposition of useing a syevdx_2stage MAGMA library function.
#' @param environmentSetup  - type (string)  - Environment variables that need to be set, such as library include paths
#' @param numGPUsWanted     - type (string)  - The number of GPUs to use in for the non-symmetric eigenvalue (syevd) computation
#' @param matrixMaxDimension  - type (integer) - The maximum matrix size that this server instance can handle - sets the shared memory size
#' @param memName           - type (string)  - A name to give to the named shared memory region (will be created in /dev/shm/) and defaults to the user name if nothing specified
#' @param semName           - type (string)  - A name to give to the semaphore (will be placed in /dev/shm) and defaults to the user name if nothing specified
#' @param print             - type (integer 0|1|2) - 0 = don't print, 1 = print details of server progres to screen; 2 = print to log (not functional)
#' @return a vector character values containing output of the make process
#' @export
#' @details
#'  This function creates a command line with which to call the syevd_server executable and then calls the executable with a non-blocking system() call
#'  to launch the server process. The server then waits for the client to send it matrix data via the syevdx_client() function. The matrixMaxDimension paramater
#'  specifies the largest size matrix that can be processed by this instance of the syevd_server(). 
RunServer <- function( environmentSetup="", numGPUsWanted=0,  matrixMaxDimension=0, memName="", semName="", print=0)
{
   # Check input values
  if (nchar(memName) == 0) {
     memName <- Sys.getenv("USER") 
  }
  if (nchar(semName) == 0) {
     semName <- Sys.getenv("USER") 
  }
  if (numGPUsWanted < 0 ) {
     numGPUsWanted <- 0 
  }
  if (matrixMaxDimension <= 0 ) {
    stop("MAGMA_EVD_CLIENT Error: MagmaEigenNonsym::RunServer(): Input matrix dimension should specify the maximum size of a matrix to undergo eigenvalue decomposition")
  }
  
  if (print > 1) {
    message("MAGMA_EVD_CLIENT Info: MagmaEigenNonsym::RunServer(): Only print to screen is currently available")
    print <- 1 ;
  }
 
  # GetServerArgs() is an Rcpp function - Setting withVectors=TRUE as a default, although it will be specified when the  
  # computational functions are called via the CSharedMemory->SetCurrentMatrixSizeAndVectorsRequest() method
  server_exe_with_args <- GetServerArgs(matrixMaxDimension, withVectors=TRUE, numGPUsWanted, memName, semName, print )
  if (nchar(server_exe_with_args) == 0)
  {
    stop("MAGMA_EVD_CLIENT Error: MagmaEigenNonsym::RunServer(): Client could not get a server launch string")
  }
  
  Sys.sleep(1) # Give the client time to create the shared memory region before launching the server that needs access to it
  
  if (print == 1) message("MAGMA_EVD_CLIENT Info: Launching server with command: ", server_exe_with_args ) ;
  sysstring <- paste0(environmentSetup, "  ", server_exe_with_args)  # ;  trap \"trap - SIGTERM && killall background\" SIGINT SIGTERM EXIT 
  system(sysstring, wait=F) # wait=F adds an '&' on the end of the string for a non-blocking sytem call to launch the server
  return(0)
  
}


.onLoad <- function(libname, pkgname) { 
  # register a 'destructor' function that calls delete on the client CSharedRegion object (in the Rcpp code)
  e <- globalenv()
  reg.finalizer(e, .finalizer_function, onexit = TRUE) # when the R session ends the registered function should run   
} 


.finalizer_function <- function(e) {
  .CleanupSharedMemory()
}


.onAttach <- function(libname, pkgname) { 
  # Create the server executable if it does not exist
  # packagepathstring <- libname ;# path.package ("MagmaEigenNonsym");  
  serverstring = paste0(libname,"/MagmaEigenNonsym/bin/nonsyevd_server.exe")
 # file.exists(c("/home/bow355/R/library/MagmaEigenNonsym/bin/nonsyevd_server"))
  if (!file.exists(serverstring)) {
    
     packageStartupMessage("MAGMA_EVD_CLIENT Info: server executable = ", serverstring, " Does not exist!, We will try to build it now.")
     envexist <- 0
     envfile <- paste0(libname,"/extdata/environmentSetup.rds")
      if (file.exists(envfile)) {
        con <- file(envfile, "r")
        environmentSetup <- readRDS(con)
        close(con)
        envexist <- envexist + 1
      } else {
        environmentSetup <- ""
      }
         
      if (envexist==1) {
        # we have cached the environment used to originally compile the server, so use them 
        # here to see if we can recompile the exe without user input
        MakeServer(environmentSetup=environmentSetup, target="dist-clean")  
        MakeServer(environmentSetup=environmentSetup, target="all")  
        MakeServer(environmentSetup=environmentSetup, target="install")  # this moves the executable to ./bin directory
        packageStartupMessage("\t...the MagmaEigenNonsym server executable was not found so we have attempted to rebuild")
        if (file.exists(serverstring)) { 
          packageStartupMessage("\t MAGMA_EVD_CLIENT Info: It looks like we succeeded!") 
        } else {
          packageStartupMessage("\t Error: MAGMA_EVD_CLIENT - IIt looks like we failed to install the server executable.")
          packageStartupMessage("\t Error: Please set up an appropriate environment and set the <package_dir>/src/make.inc file variables.") 
          packageStartupMessage("\t Error: This package requires the ILP64 version of the MAGMA library to be installed and MAGMA_HOME to be be set.")
          packageStartupMessage("\t Error: For the CUDA version of MAGMA we will also need CUDA_ROOT set.")
          packageStartupMessage("\t Error: Then call: MakeServer(environmentSetup=\" env MAGMA_HOME=<path to magma> CUDA_ROOT=<path to cuda>\", target=\"all\") ")
          packageStartupMessage("\t Error: followed by: MakeServer(environmentSetup=\" env MAGMA_HOME=<path to magma> CUDA_ROOT=<path to cuda>\", target=\"install\")")
        }
        
      } else {
        # We are compiling the package during installation:
        libversion <- ""
        # make the environmentSetup string
        magma_path <- Sys.getenv("MAGMA_HOME")
        if(nchar(magma_path) == 0) {
          environmentSetup=" env MAGMA_HOME=/usr/local/magma "
        } else {
           environmentSetup=paste0(" env MAGMA_HOME=", magma_path)
        }        
        cuda_path <- Sys.getenv("CUDA_ROOT")
        if(nchar(cuda_path) == 0) {
          environmentSetup=paste0(environmentSetup, " CUDA_ROOT=/usr/local/cuda")
        } else {
           environmentSetup=paste0(environmentSetup, " CUDA_ROOT=", cuda_path)          
        }        
        MakeServer( environmentSetup=environmentSetup, target="dist-clean")  
        MakeServer( environmentSetup=environmentSetup, target="all")  
        MakeServer( environmentSetup=environmentSetup, target="install")  # this moves the executable to ./bin directory
        if (!file.exists(serverstring)) {
          packageStartupMessage("\t Error: MAGMA_EVD_CLIENT - It looks like we failed to install the server executable.")
          packageStartupMessage("\t Error: Please set up an appropriate environment and set the <package_dir>/src/make.inc file variables.") 
          packageStartupMessage("\t Error: This package requires the ILP64 version of the MAGMA library to be installed and MAGMA_HOME to be be set.")
          packageStartupMessage("\t Error: For the CUDA version of MAGMA we will also need CUDA_ROOT set.")       
          packageStartupMessage("\t Error: Then call: MakeServer(environmentSetup=\" env MAGMA_HOME=<path to magma> CUDA_ROOT=<path to cuda>\", target=\"all\") ")
          packageStartupMessage("\t Error: followed by: MakeServer(target=\"install\")")
        }
       
      }
   }
}


