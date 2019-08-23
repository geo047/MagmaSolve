#include <math.h>   //  cmath does not have isnan() ?
#include <string.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include <Rcpp.h>
#include <R_ext/BLAS.h>

#if (_USE_RCPP==1)
#define _PRINTERROR Rcpp::Rcerr
#define _PRINTSTD Rcpp::Rcout
#else
#define _PRINTERROR std::cerr
#define _PRINTSTD std::cout
#endif
/*
struct arg_list  defined in CSharedRegion.h:
struct arg_list {
	size_t 	matrixDimension ;	// We assume a non-symmetric (square) matrix, so this is the row and column dimension 
	bool 	weWantVectors   ;	// Boolean used to tell MAGMA function that we do or dont want eigenvectors
	int 	numGPUsWanted   ;	// The number of GPUs to use - this will be checked against the number of GPUs present and truncated if need be
	std::string    memName  ;  	// The name of the shared memory region 
	std::string    semName  ; 	// the name of the locking semaphore used to message when the server has finshed with the memory area
};
*/
int CleanupSharedMemory(  ) ;

//Test if file exists (actually tests if it is accessible)
inline bool exists(const std::string& name) 
{
    std::ifstream f(name.c_str());
    if (f.good()) {
        f.close();
        return true;
    } else {
        f.close();
        return false;
    }   
}


// CSharedRegion.h will be found in <package_src_dir>/inst/include when package is being compiled 
// and in <package_installation_dir>/include when package is installed
#include "CSharedRegion.h"
static CSharedRegion * shrd_client = NULL ;
// volatile sig_atomic_t 

void catch_signal(int sig)
{
    if (shrd_client != NULL)
    {      
      _PRINTERROR << "Error: catch_signal() called so we will now terminate the server and clean up the client object!" << std::endl ;
      shrd_client->ExitServer( ) ;
      delete shrd_client ;
      shrd_client = NULL ;
    } 
    if (sig == SIGTERM)
      Rcpp::stop("\n"); 
      // _STOPFUNCTION ;
      // exit(1) ;
}

//' Function signals to the server through shared memory region to terminate from 
//' its main loop and then deletes the client CSharedMemory object
// [[Rcpp::export(rng=false)]]
void StopServer( )
{
  CleanupSharedMemory() ;
  /*if (shrd_client != NULL)
  {
    shrd_client->ExitServer() ;
    sleep(1) ;
    CleanupSharedMemory() ;
  }*/
}

//' Called when the package is unloaded or R terminated - it releases the shared memory objects that are used to communicate with the nonsyevd_server executable
// [[Rcpp::export(name=".CleanupSharedMemory")]] 
int CleanupSharedMemory(  )
{

  if (shrd_client != NULL)
  {
    shrd_client->ExitServer() ;
    sleep(1) ;
    _PRINTSTD << " MAGMA_EVD_CLIENT Info: Calling destructor of shrd_client object" << std::endl ;
    delete shrd_client ;
    shrd_client = NULL ;
  }
  
  return (0) ;
}

//' Initialise shared memory on client and obtain server launch string.
//' @description This function will create a CSharedMemory object that initialises the shared memory region and the semaphore used for comms beteween client and server.
//' If the object is already initialised it is removed and reinitialised. 
//' Returns a string of the form "-n 10000 -v 1 -g 3 -m /syevx_<PID_of_client> -s /sem_<PID_of_client> -p" that can be used to launch a nonsyevd_server process
//' that will accept matrix data on which to perform eigenvalue decomposition 
//' @param matrixDimension   - type (integer) - the dimension of the (assumed square) matrix
//' @param numGPUsWanted     - type (string)  - The number of GPUs to use in for the non-symmetric eigenvalue (syevd) computation
//' @param withVectors       - type (boolean) - true = We want the eigenvectors and eigenvalues calculated ; false = we only want eigenvalues calculated
//' @param memName           - type (string)  - a name to give to the named shared memory region (will be created in /dev/shm/) and defaults to the user name if nothing specified
//' @param semName           - type (string)  - a name to give to the semaphore (will be placed in /dev/shm) and defaults to the user name if nothing specified
//' @param printDetails      - type (integer 0|1|2) - 0 = don't print, 1 = print details of server progress to screen; 2 = print to log (not functional)
//' @return                  - type (string) A string that can be used a command line arguments to run the nonsyevd_server executable
// [[Rcpp::export(rng=false)]] 
std::string GetServerArgs(int matrixDimension, bool withVectors, int numGPUsWanted, std::string memName, std::string semName , int printDetails)
{
	struct arg_list main_args ;
  std::stringstream ss_string ;
  std::string serverpathstring ;
  
	main_args.matrixDimension = matrixDimension ;
	main_args.weWantVectors = withVectors ;
	main_args.numGPUsWanted = numGPUsWanted ;
	main_args.memName.assign(memName) ;
	main_args.semName.assign(semName) ;
  if (printDetails == 0 )
    main_args.msgChannel = HIDE ;
  else if (printDetails == 1) 
    main_args.msgChannel = PRINT ;
  else if (printDetails == 2)
    main_args.msgChannel = LOG ;
	//main_args._batchinfo_filename.assign("batch_balanced_npm.bin") ;  // batch_unbalanced_npm	
 
  if (shrd_client == NULL) 
	  shrd_client  = (CSharedRegion * ) new CSharedRegion(MAGMA_EVD_CLIENT, main_args) ;  // this creator will append the PID to memName and semName
  else
  {
    CleanupSharedMemory() ;
    shrd_client  = (CSharedRegion * ) new CSharedRegion(MAGMA_EVD_CLIENT, main_args) ;  // this creator will append the PID to memName and semName    
    //Rcpp::stop(" Error: GetServerArgs() has been called previously and the shared memory area initialised." ) ;
  }
   
  if (shrd_client == NULL)
    Rcpp::stop("Error: GetServerArgs() Failed to create client shared mameory object!") ;
  
  // register a signal handeler function
  signal(SIGTERM, catch_signal) ; // this is caught on kill (from top etc.)
  signal(SIGUSR1, catch_signal) ; // this may have to be set by SLURM as --signal=SIGUSR1@60
  signal(SIGINT, catch_signal) ;  // this is caught on kill (from top etc.)
  signal(SIGQUIT, catch_signal) ; // this is caught on kill (from top etc.)
  signal(SIGHUP, catch_signal) ;  // 
  
  // Get directory containing the executable:
  Rcpp::Function pathpackage_rcpp = Rcpp::Environment::base_env()["path.package"];  // get the R function "path.package()"
	SEXP retvect = pathpackage_rcpp ("MagmaEigenNonsym");  // use the R function in C++ 
	serverpathstring = Rcpp::as<std::string>(retvect) ; // convert from SEXP to C++ type
	serverpathstring = serverpathstring + "/bin/nonsyevd_server.exe" ;
  if (!exists(serverpathstring))
  {
    ss_string << " MAGMA_EVD_CLIENT Error: MagmaEigenNonsym::GetServerArgs()  " << std::endl ;
    ss_string << "\t Server executable does not exist: " << serverpathstring << std::endl ;
    ss_string << "\t Please set up an appropriate environment and set the <package_dir>/src/make.inc file variables." << std::endl ; 
    ss_string << "\t The environment requires the ILP64 version of the MAGMA library to be installed and MAGMA_HOME to be be set." << std::endl ;
    ss_string << "\t For the CUDA version of MAGMA we will also need CUDA_ROOT to be set." << std::endl ;    
    ss_string << "\t Then call:   MakeServer(environmentSetup=\" env MAGMA_HOME=<path to magma> CUDA_ROOT=<path to cuda>\", target=\"all\") " << std::endl ;
    ss_string << "\t followed by: MakeServer(environmentSetup=\" env MAGMA_HOME=<path to magma> CUDA_ROOT=<path to cuda>\", target=\"install\")" << std::endl ;
    Rcpp::stop(ss_string.str()) ;
    //return("") ;
  } else {
    if (main_args.msgChannel == PRINT ) _PRINTSTD << "MAGMA_EVD_CLIENT Info: Server executable exists: " << serverpathstring << std::endl ;
  }
  
  // Create the server command line
  // based on the "struct arg_list" defined in CSharedRegion.h
  ss_string << serverpathstring ;
  ss_string << " -n " << shrd_client->_matrix_dim ;
  ss_string << " -v " << shrd_client->_weWantVectors ; 
  ss_string << " -g " << shrd_client->_numgpus ;
  ss_string << " -m " << shrd_client->_memName ;
  ss_string << " -s " << shrd_client->_semName ;
  if (main_args.msgChannel == PRINT)
    ss_string << " -p " ;
  
  if (main_args.msgChannel == PRINT ) _PRINTSTD << " MAGMA_EVD_CLIENT Info: Server launching string: " << ss_string.str() << std::endl ;
  // 	"<R_HOME>/library/bin/nonsyevd_server -n 10000 -v 1 -g 3 -m /syevx_<PID_of_client> -s /sem_<PID_of_client>"
  
	return(ss_string.str()) ;
	
	// We get the server to check how many GPUs are present
  // therefore we do not need CUDA/other interface to be present for compilation of the MagmaEigenNonsym package
  
}

 

//' Function used to obtain the eigenvalue decomposition (EVD) of a matrix using a MAGMA 2-stage multi-gpu EVD algorithm
//' @description This function performs the eigenvalue decomposition of the input matrix and returns the eigenvalues and (if requested) the 
//' eigenvectors of the input matrix in the returned list, identical to the base R eigen() function. If overwrite=TRUE then the eigenvectors are copied into the ***input matrix*** and the original matrix 
//' data is overwritten. 
//' The method involves the offload of the matrix data to a seperate nonsyevd_server executable by copying data into a shared memory area and 
//' signalling to the server that the data is availble. This function will block until the server has completed the decomposition. The 
//' function checks that the input is square, however it does not check that the matrix is non-symmetric.
//' N.B. The maximum size allowed of the input matrix is goverend by what was provided in the MagmaEigenNonsym::RunServer() function. The server 
//' will automatically be restarted with a larger shared memory area if user wants to perorm EVD on a larger matrix.
//' @param matrix - the input matrix to be used in eigenvalue decomposition. It is assumed to be square 
//' @param symmetric - the input is assumed to be non-symmetric and real. Function will fail if symmetric=TRUE.
//' @param only_values - If TRUE: only compute eigenvalues. If FALSE: Compute eigenvectors also. 
//' @param overwrite - If TRUE: The resulting eigenvectors (if requested) will overwrite the input matrix to potentially reduce memory requirements. 
//' @param printInfo - Prints diagnostic information about the client processing
//' @return A list that contains the eigenvalues and if requested the eignenvectors. If overwrite==TRUE then the eignevectors are copied into the ***input matrix***
// [[Rcpp::export(rng=false)]]
Rcpp::List eigen_nonsym_mgpu(Rcpp::NumericMatrix matrix, bool symmetric=false,  bool only_values=false, bool overwrite=false, bool printInfo=false )
{
  std::stringstream ss_string;
  
  if (symmetric == true)
   Rcpp::stop(" MAGMA_EVD_CLIENT Error: eigen_nonsym_mgpu() assumes a non-symmetric matrix as input and user has specified symmetric") ;
    
  bool withVectors = ! only_values ;
  if (shrd_client != NULL)
  {
    try {
      // Check that we have enough memeory in the sahred memory region:
      if (matrix.ncol() > shrd_client->_max_matrix_dim)  
      {
         // delete shrd_client ;
         _PRINTSTD << " MAGMA_EVD_CLIENT Error: eigen_nonsym_mgpu() input matrix size (" << matrix.ncol() << ") " ;
         _PRINTSTD << " is bigger than maximum requested! (" << shrd_client->_max_matrix_dim << ")" << std::endl ;
         _PRINTSTD << " Please run RunServer() with a larger matrixMaxDimension argument"  << ")"<< std::endl ;
         std::flush(_PRINTSTD) ;
         Rcpp::stop("MAGMA_EVD_CLIENT Error: eigen_nonsym_mgpu()") ;
         // return(Rcpp::List::create(  Rcpp::Named("error") = ss_string.str() )) ;
      }
      
      if (matrix.ncol() != matrix.nrow()) {
         _PRINTSTD << " MAGMA_EVD_CLIENT Error: eigen_nonsym_mgpu() The input matrix is not square! Rows: " << matrix.nrow() << " Cols:" << matrix.ncol()  << std::endl ;
         std::flush(_PRINTSTD) ;
         Rcpp::stop("MAGMA_EVD_CLIENT Error: eigen_nonsym_mgpu()") ;
         //return(Rcpp::List::create(  Rcpp::Named("error") = ss_string.str()  )) ;
      }
        
      shrd_client->SetCurrentMatrixSizeAndVectorsRequest(matrix.ncol(), withVectors) ;
      size_t numbytes = matrix.ncol() * matrix.ncol() * sizeof(double) ;
      
      // Copy memory from R matrix to shared memory region
      shrd_client->copy_matrix_into_shmem(matrix.begin(), numbytes ) ;
      //  *** Call sem_post() so that the server can then move on to do processing 
      if (printInfo == true) _PRINTSTD << " MAGMA_EVD_CLIENT Info: copy_matrix_into_shmem() has copied memory to shared region, calling sem_post()" << std::endl  ;
      
      sem_post(shrd_client->_sem_id);
      if (printInfo == true) _PRINTSTD << " MAGMA_EVD_CLIENT Info: sem_post() was called - release the server thread " << std::endl  ;
      std::flush(_PRINTSTD) ;
      
      sleep(1) ;  // Give the server some time to acknowledge the change in semaphre state.
      
      // *** Blocks here until server releases semaphore ***
      if (printInfo == true) _PRINTSTD << " MAGMA_EVD_CLIENT Info: Waiting on the server thread to sem_post() so client can copy back memory" << std::endl  ;
      sem_wait(shrd_client->_sem_id);  
      if (printInfo == true) _PRINTSTD << " MAGMA_EVD_CLIENT Info: sem_wait() was called " << std::endl  ;
      
      
      // Create storage for the eigenvalues to be returned to R
      Rcpp::NumericVector rvalues(matrix.ncol()) ;      
     // Copy back the eigen values
      // offset to get evalues is numbytes 
      shrd_client->copy_shmem_into_matrix(rvalues.begin(), rvalues.length() * sizeof(double) , numbytes) ;
      std::reverse(rvalues.begin(),rvalues.end())   ; // inplace reverse of the eigenvalues    //Original R version: ord <- rev(seq_along(z$values))
          
      if (printInfo == true) _PRINTSTD << " MAGMA_EVD_CLIENT Info: has copied the rvalues data from shared memory" << std::endl  ;  
      if (shrd_client->_weWantVectors == WANTVECTORS)
    	{
        if (overwrite==true) {
          shrd_client->copy_shmem_into_matrix(matrix.begin(), matrix.ncol() * matrix.ncol() * sizeof(double) , 0) ;
          shrd_client->copy_matrix_inplace_reverse_rows(matrix.begin(), matrix.nrow()) ;
          Rcpp::CharacterVector retInfoAboutEvects("Eigen vectors have been returned in the input matrix")  ;
          return (Rcpp::List::create(  Rcpp::Named("values") = rvalues,  // pcascores->_scores,  // corrected_scores,
                                       Rcpp::Named("vectors") = retInfoAboutEvects )) ;
        }
        else
        {
          Rcpp::NumericMatrix vectors(matrix.ncol(),matrix.ncol()) ;  // create the output matrix
         // shrd_client->copy_shmem_into_matrix_reverse_cols(vectors.begin(), vectors.nrow()) ;  // place eignevectors in the original input matrix and return to user
         // shrd_client->copy_matrix_inplace_reverse_rows(vectors.begin(), vectors.nrow()) ;
          shrd_client->copy_shmem_into_matrix(vectors.begin(), vectors.ncol() * vectors.ncol() * sizeof(double) , 0) ;
          shrd_client->copy_matrix_inplace_reverse_rows(vectors.begin(), vectors.nrow()) ;
         // shrd_client->copy_matrix_inplace_reverse_cols(vectors.begin(), vectors.nrow()) ;
          return (Rcpp::List::create(  Rcpp::Named("values") = rvalues,  // pcascores->_scores,  // corrected_scores,
                                       Rcpp::Named("vectors") = vectors )) ;
        }    		
        
    	}
    	else
    	{
    		return (Rcpp::List::create(  Rcpp::Named("values") = rvalues )) ;
    	}
         
    } catch (...) {
     _PRINTSTD << " MAGMA_EVD_CLIENT Info: eigen_nonsym_mgpu(): Fell into catch block " << std::endl  ;
      std::flush(_PRINTSTD) ;
      CleanupSharedMemory() ;
      return (Rcpp::List::create(  Rcpp::Named("values") = "",  // pcascores->_scores,  // corrected_scores,
                                  Rcpp::Named("vectors") = "" )) ;
    }
  } else {
    Rcpp::stop(" MAGMA_EVD_CLIENT Error: eigen_nonsym_mgpu() failed as the client CSharedMemory object is NULL (User possibly needs to call RunServer())") ;
    // return("Error: nonsyevdx_client_fill_matrix failed as Client CSharedMemory object is NULL (User possibly needs to call RunServer())") ;
  }
	
}
 





