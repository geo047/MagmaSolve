
#include <math.h>   //  cmath does not have isnan() ?
#include <string.h>

#include <iostream>
#include <fstream>
#include <sstream>

// #include <magma_threadsetting.h> // requires -I<MAGMA_HOME>/control  - magma_threadsetting.h

#if defined _WITH_CUDA   
  #include <cuda.h>
  #include <cuda_runtime_api.h>
#else
  #if defined(__APPLE__) || defined(__MACOSX)
  #include <OpenCL/cl.hpp>
  #include <OpenCL/OpenCL.h>
  #else
  #include <CL/cl.hpp>
 // #include <CL/cl.h>
#endif
#endif

#include <errno.h>   

#undef _UNICODE
#include <getopt.h>




#ifdef __unix
#define SYSTEMDIRDELIM "/"
#elif _WIN64
#define SYSTEMDIRDELIM "\\"
#endif



#include "CSharedRegion.h"
 // struct arg_list is defined in CSharedRegion.h
#include  "CStringManipulation.h"
 

# include <cuda_runtime.h>
# include "magma_v2.h"
# include "magma_lapack.h"



// Function definitions
int  server_init( std::string pathString, bool print )  ;
std::string Get_ERROR_STR( int  errorin ) ;
void server_close( )  ;
int server_compute_solve_mgpu(  hideprintlog hideorprint  ) ;  // This function internally uses the static CSharedRegion * shrd_server object
int print_devices( bool print_num_bool, std::string pathString) ;
int GetNumDevicesUsingOpenCL(std::string plat_str, int clDevType ) ;
int GetNumDevicesUsingOpenCL_C(std::string plat_str, int clDevType ) ;

static CSharedRegion * shrd_server = NULL ;


// External Function definitions
extern "C" {
magma_int_t 
magma_dgetri_gpu ( magma_int_t n,
magmaDouble_ptr dA,
magma_int_t ldda,
magma_int_t*  ipiv,
magmaDouble_ptr dwork,
magma_int_t lwork,
magma_int_t* info 
);	
magma_int_t magma_dgetrf_gpu(magma_int_t m,
magma_int_t n,
magmaDouble_ptr dA,
magma_int_t ldda,
magma_int_t * ipiv,
magma_int_t * info 
);
magma_int_t magma_dgetrf_m( magma_int_t ngpu,
magma_int_t m,
magma_int_t n,
double *A,
magma_int_t lda,
magma_int_t *ipiv,
magma_int_t *info 
);
}



 
void PrintUsage(std::string progName) 
{
	std::cout   << "Usage: " << progName << " -d [matix dimension] -v [eigenvectors 1|0] -g [number of GPUs] -m [shared memory name] -s [named semaphore name] " << std::endl ;
	std::cout  <<  " -d [matix dimension]       The dimension of the (square) matrix  " << std::endl ;
	std::cout  <<  " -v [eigenvectors] 	        Request eigenvectors (=1)" << std::endl ;
	std::cout  <<  " -g [number of GPUs]        Requested number of GPUs	 " << std::endl ;
	std::cout  <<  " -m [shared memory name]    Name for shared memory which was created by client (will include a PID of client) " << std::endl ;
	std::cout  <<  " -s [named semaphore name]  Name for semaphore which was created by client (will include a PID of client)   " << std::endl ;
	std::cout  <<  " -p If flag present then print details of server execution" << std::endl ; 
  std::cout  << std::endl ;
}


 // struct arg_list is defined in CSharedRegion.h
struct arg_list get_solve_args(int argc, char* argv[])
{
	int opt;
	struct arg_list ret_list ;

	// set the defaults
	ret_list.matrixDimension = 10000 ;
	ret_list.weWantVectors = true ;
	ret_list.numGPUsWanted = 1 ;
	ret_list.memName = "" ;
	ret_list.semName = "" ;
	ret_list.msgChannel = HIDE ;  // default is hide text output from user (except on error)
  
	// Parse command-line arguments
	 const char* optString = "hn:v:g:m:s:p";
	opt = getopt(argc, argv, optString);

	while (opt != -1)
	{
		switch (opt) 
		{
		case ('?'):  // print usage message
			PrintUsage(argv[0]) ;
			exit(EXIT_FAILURE) ;
			break ;
		case ('h'):  // print usage message
			PrintUsage(argv[0]) ;
			exit(0) ;
			break ;
		case ('n'):  // get size of (square) matrix
			ret_list.matrixDimension = strtoll(optarg, NULL, 10) ;
			break ;
		case ('v'):  // get boolean of if we want eigenvectors returned or not
			if (strtoll(optarg, NULL, 10) == 1)
				ret_list.weWantVectors = true ;
			else 
				ret_list.weWantVectors = false ;
			break ;
		case ('g'):  // get requested number of GPUs to use in DSYEVD computation
			ret_list.numGPUsWanted = strtoll(optarg, NULL, 10) ; 		
			break ;
		case ('m'):  // get requested named shared memory area string
			ret_list.memName.assign(optarg) ; 		
			break ;
		case ('s'):  // get requested named semaphore string
			ret_list.semName.assign(optarg) ; 		
			break ;
    case ('p'):  // get requested named semaphore string
  		ret_list.msgChannel = PRINT ; 		
			break ;
		
		}
		opt = getopt(argc, argv, optString) ;
	}
	return (ret_list) ;
}



void error_and_die(const std::string msg) 
{
	std::cerr << " MAGMA_EVD_SERVER exiting: " << msg << " Error number: " << strerror(errno) << std::endl ;
	// perror(msg);
  //delete shrd_server ;
  server_close() ;
	exit(EXIT_FAILURE);
}

void EraseFromLast(std::string & resStr, std::string findStr ) 
{
  int pos = resStr.find_last_of(findStr) ;
  if (pos != std::string::npos)
    resStr.erase (resStr.begin()+pos,resStr.end());
}

/*! 
 * main() entry point to Server C/C++ version of the code.
 * The client should start the server using a system call similar to:
 * 		> solver_server -n 10000 -v 1 -g 3 -m /syevx_<PID_of_client> -s /sem_<PID_of_client> 
 * 		Where:
 * 		-n = 10000 	- matrix size is 10000
 * 		-v = 1 		- we want eigenvectors returned (or 0 for just eigenvalues)
 * 		-g = 3 		- use 3 GPUs
 * 		The values of both /syevx_<PID_of_client> and /sem_<PID_of_client> are created when CSharedRegion client object is created
 * 		as values in object: CSharedRegion::_memName  and CSharedRegion::_semName 
 * 
 * This program uses a CSharedMemory object started in server mode to accept data from a client, including T/F for eigenvectors, and matrix size
 * and the input matrix data in a shared memory region, that both client and the server have access to 
 */
int main(int argc, char* argv[])
{
	struct arg_list main_args ;
  std::stringstream ss_string ;
 
  /// the path is required so we can obtain the file <path to package install>/extdata/platformstring.txt to read in what platform the user wants
  std::string pathstr ; 
	main_args = get_solve_args(argc, argv) ; 
  pathstr.append(argv[0]) ;
  EraseFromLast (pathstr,SYSTEMDIRDELIM) ;
  EraseFromLast (pathstr,SYSTEMDIRDELIM) ;
  if (pathstr == "")
  {
  		ss_string << " MAGMA_EVD_SERVER Error: We need a full path supplied to launch our executable"  ;
		  error_and_die(ss_string.str());
	}
	bool print = false ;
  if ( main_args.msgChannel == PRINT ) 
    print = true;
    
	int numgpuavail = server_init( pathstr, print ) ;  // initialise the magma library and return the number of GPUs available on the syatem	 
	if (main_args.numGPUsWanted > numgpuavail) // check that the number of GPUs requested are available
	{
		if (numgpuavail == 0) 
		{
			ss_string << " MAGMA_EVD_SERVER Error: Number of gpus available is 0. solve_server will now exit" ;
			server_close() ;
      exit(EXIT_FAILURE);
      //error_and_die(ss_string.str());
		}
		std::cout << " MAGMA_EVD_SERVER Info :Number of gpus requested (" << main_args.numGPUsWanted << ") is more than available (" << numgpuavail << ")." << std::endl ;
    std::cout << "  setting numGPUsWanted to: " << numgpuavail << std::endl ;
		main_args.numGPUsWanted = numgpuavail ;
	}
  
  // This is the statically declared CSharedRegion * object, creating the server version that will have to sem_wait() asap. 
  // this will *** not block *** 
  shrd_server  = (CSharedRegion * ) new CSharedRegion(MAGMA_EVD_SERVER, main_args) ; 
  if (shrd_server == NULL)
  { 
    ss_string << " Error from: " << argv[0] << " MAGMA_EVD_SERVER Error: could not create CSharedRegion * shrd_server object " ;
    error_and_die(ss_string.str());
  }
  if( main_args.msgChannel == PRINT)   std::cout << " MAGMA_EVD_SERVER Info: CSharedRegion() object created and moving to compute evd" << std::endl  ;
   
  int syevd_info ;
  int sem_val = 0 ;
  int num_loop = 1 ;
  int exit_server = 0 ; // if this turns to 1 (after call to shrd_server->SetCurrentMatrixSizeAndVectorsRequest()) then client has requested that the server exits
  // Our main computational loop - wait for data, lock sem, compute EVD, unlock semaphore and continue until client sets semaphore to > 1
  while (exit_server == 0)
  {
     // std::cout << " MAGMA_EVD_SERVER Info: in while loop - about to call sem_wait() " << std::endl  ;
     // std::flush(std::cout) ;
      
      // This may not work correctly - may need to get client to close the semaphore to escape out of server loop
      // if client calls sem_post(shrd_server>_sem_id) twice, then we want to escape out of the server while loop
      if (sem_getvalue(shrd_server->_sem_id, &sem_val) != 0)
      {
        ss_string << " MAGMA_EVD_SERVER Error: Error from: " << argv[0] << " error calling: sem_getvalue() " ;
        error_and_die(ss_string.str());
      }
      
      if( main_args.msgChannel == PRINT)  std::cout << " MAGMA_EVD_SERVER Info: waiting for new matrix data from the client. sem_val= " << sem_val << std::endl  ;
      std::flush(std::cout) ;
      // This will ****block**** the server until the client calls sem_post().
      if (sem_wait(shrd_server->_sem_id) != 0)  
      {
        ss_string << " MAGMA_EVD_SERVER Error: Error from: " << argv[0] << "  error calling: sem_wait() " ;
        error_and_die(ss_string.str());
      }
           
      // the server side code will check the last 2 x sizeof(double) bytes after the matrix + vector of doubles ( == evcts + evals) in the 
      // shared memory region  to see what the size of the matrix that was input by the client is
      // and sets  _values pointer and _matrix_dim values correctly. The 'true' value is not used 
      // in the server version, as _weWantVectors will also be read from the shared memory area. 
      exit_server = shrd_server->SetCurrentMatrixSizeAndVectorsRequest(0, true) ; 
      if( main_args.msgChannel == PRINT) std::cout <<  " MAGMA_EVD_SERVER Info: exit_server \t" <<  exit_server << std::endl ; 
      std::flush(std::cout) ;
      if (exit_server == 0)
      {     
          if( main_args.msgChannel == PRINT) {
            std::cout <<  " MAGMA_EVD_SERVER Info: _weWantVectors \t" <<  shrd_server->_weWantVectors << std::endl ; 
            std::cout <<  " MAGMA_EVD_SERVER Info: _matrix_dim \t" << shrd_server->_matrix_dim << std::endl ; 
           // shrd_server->PrintObjectDetails( false ) ;  // to log file == true 
            std::flush(std::cout) ;
          }
          syevd_info = server_compute_solve_mgpu( main_args.msgChannel ) ;
          if (syevd_info != 0)  
          {
            sem_post(shrd_server->_sem_id) ;
            ss_string << " MAGMA_EVD_SERVER Error: Error from: " << argv[0] ;
            ss_string << " info != 0 returned from server_compute_solve_mgpu() " << std::endl  ;
            ss_string << "N.B. IF Error numer = Bad address then GPU possibly ran out of memory." << std::endl   ;
            ss_string << "Then increase physical GPU memory or increase numGPUsWanted in RunServer()." << std::endl  ;
            error_and_die(ss_string.str());
          }
                      
        } // do not do any computation unless exit_server == 0
        
        // This will ****unblock**** the client
        if (sem_post(shrd_server->_sem_id) != 0)  
        {
          ss_string << " MAGMA_EVD_SERVER Error:  Error from: " << argv[0] << "  error calling: sem_post() " ;
          error_and_die(ss_string.str());
        } 
        sleep(1) ;
  }
  server_close() ;  // calls delete shrd_server ;
  if( main_args.msgChannel == PRINT) std::cout << "  MAGMA_EVD_SERVER Info: exited  " << std::endl ;
  
  return 0;
  
 }




// This is the modified original La_rs() code, with added magma 2stage non-symmetric eigenvalue decomposition
// N.B. It makes a copy of the input matrix data - 
// All data is passed in via the static CSharedRegion object shrd_server
int server_compute_solve_mgpu( hideprintlog hideorprint )
{
  magma_int_t n, n2,  lwork2, info = 0;  // define MAGMA_ILP64 to get these as 64 bit integers

  magma_print_environment();


  // Initialize the queue
   magma_queue_t queue = NULL ;
   magma_int_t dev =0;
   magma_queue_create(dev,&queue);
   magma_int_t ngpu=4;   // -------> this needs to be changed. 



  if (shrd_server == NULL)
  {
    std::cerr << " MAGMA_EVD_SERVER Error: server_compute_dgeev_mgpu(): shrd_server object is NULL" << std::endl ;
    return(-1) ;
  }
  
  n = shrd_server->_matrix_dim ;
  n2 = n*n;


  // Getting data from R land
  double *rvectors_ptr;
  rvectors_ptr = shrd_server->_vectors ;  // was rx


  /* Variables that need setting are
  Scalars
  -----------
  ldwork - size of dwork

  Arrays
  -------
  dwork  - double work array
  ipiv   - int work array  
  h_A    - CPU based array data
  d_A    - GPU based array data


  Unified memory version
  -------------------------
  cudaMallocManaged (&a,mm* sizeof ( double )); // unified mem. for a
  cudaMallocManaged (&r,mm* sizeof ( double )); // unified mem. for r
  cudaMallocManaged (&c,mm* sizeof ( double )); // unified mem. for c
  cudaMallocManaged (& dwork , ldwork * sizeof ( double )); // mem. dwork
  cudaMallocManaged (& piv ,m* sizeof ( int )); // unified mem. for ipiv


  */


// Setting of A
double *A;
magma_dmalloc_cpu( &A, n2);  // CPU based memory
//cudaMallocManaged(&A, n2* sizeof ( double )); // unified mem. for a
for (int i=0; i<n2; i++){
  A[i] = rvectors_ptr[i];
}


// Setting of d_A memory on GPU device
double *d_A;
magma_dmalloc_pinned(&d_A, n2);
// copy A into d_A 
magma_dsetmatrix ( n, n, A,n, d_A ,n, queue );

std::cout << "Check: contents of d_A ----- : --->   " << std::endl;
magma_dprint_gpu(5,5, d_A, n , queue);


// Setting of dwork
double *dwork;
magma_int_t ldwork ; 
ldwork = n * magma_get_dgetri_nb ( n ); // optimal block size
//cudaMallocManaged (& dwork , ldwork * sizeof ( double )); // mem. dwork
magma_dmalloc_cpu(&dwork, ldwork);  //sitting in GPU land


// Setting of ipiv workspace object
magma_int_t *ipiv;
//  cudaMallocManaged (& ipiv ,n* sizeof ( int )); // unified mem. for ipiv
 magma_imalloc_cpu( &ipiv,   n      );


//magma_int_t  ldda;
//ldda   = magma_roundup( n, 32 );  // multiple of 32 by default



std::cout << "About to start magma_dgetrf_gpu ..... " << std::endl;
// magma_dgetrf_gpu(n, n, d_A, n, ipiv, &info);
magma_dgetrf_m(ngpu, n, n, d_A, n, ipiv, &info);
std::cout << info << std::endl;



std::cout << "About to start magma_dgetri_gpu ..... " << std::endl;
magma_dgetri_gpu(n,d_A,n,ipiv,dwork,ldwork,&info);

std::cout << " Info === " << info << std::endl; 


std::cout << "Contents of A that sits in GPU land AFTER inverse  " << std::endl;
magma_dprint_gpu(5,5, d_A, n , queue);






 magma_free(dwork);
 magma_free_cpu(ipiv);
 magma_free(A);


std::cout << "Wow  "  << std::endl;

return info ;
 


}

void server_close( ) 
{
  if (shrd_server != NULL)
    delete shrd_server ;    
  shrd_server = NULL ;  
  magma_finalize() ;
}
  
static int numGPUS_STATIC = -1 ;
  
int  server_init(std::string pathString, bool print_bool ) 
{
// Returns the number of GPUs on the system
// Sets the 'context' static variable
                                                                               
 //	int numGPUS_STATIC = 0 ;
 // static int INIT_CALLED_ONCE = 0 ;

 //mkl_set_threading_layer( MKL_THREADING_INTEL ) ; // MKL_THREADING_INTEL
 //mkl_set_interface_layer( MKL_INTERFACE_ILP64 ) ; // force the 64 bit version of the LAPACK functions

	magma_init() ;
	
//	numcalled = 0 ; // reset the iteration counter	
	if (  numGPUS_STATIC == -1 )
	{       
		numGPUS_STATIC = print_devices(print_bool, pathString) ;
    if (sizeof(magma_int_t) < 8) 
    {
      std::cerr << " MAGMA_EVD_SERVER Error: server_init(): sizeof(magma_int_t) < 8 bytes! "  << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: server_init(): The MAGMA library used has not been compiled with 64 bit integer interface "  << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: server_init(): Please link nonsyevd_server executable to MAGMA built with -DMAGMA_ILP64  "  << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: server_init(): Exiting! "  << std::endl ;
      server_close() ;
      exit(1) ;
    }
		return ( numGPUS_STATIC ) ;	
	}

	return ( numGPUS_STATIC ) ;

}


 std::string Get_ERROR_STR( int  errorin )
{
	switch 	(errorin)
	{
#if defined _MAGMA_WITH_CUDA
		case (CUDA_SUCCESS): return (std::string)"CUDA_SUCCESS" ;
		case (CUDA_ERROR_INVALID_VALUE): return (std::string)"CUDA_ERROR_INVALID_VALUE " ;
		case (CUDA_ERROR_OUT_OF_MEMORY): return (std::string)"CUDA_ERROR_OUT_OF_MEMORY " ;
		case (CUDA_ERROR_NOT_INITIALIZED): return (std::string)"CUDA_ERROR_NOT_INITIALIZED " ;
		case (CUDA_ERROR_DEINITIALIZED): return (std::string)"CUDA_ERROR_DEINITIALIZED " ;
		case (CUDA_ERROR_PROFILER_DISABLED): return (std::string)"CUDA_ERROR_PROFILER_DISABLED " ;
		case (CUDA_ERROR_PROFILER_NOT_INITIALIZED): return (std::string)"CUDA_ERROR_PROFILER_NOT_INITIALIZED " ;
		case (CUDA_ERROR_PROFILER_ALREADY_STARTED): return (std::string)"CUDA_ERROR_PROFILER_ALREADY_STARTED " ;
		case (CUDA_ERROR_PROFILER_ALREADY_STOPPED): return (std::string)"CUDA_ERROR_PROFILER_ALREADY_STOPPED " ;
		case (CUDA_ERROR_NO_DEVICE): return (std::string)"CUDA_ERROR_NO_DEVICE " ;
		case (CUDA_ERROR_INVALID_DEVICE): return (std::string)"CUDA_ERROR_INVALID_DEVICE " ;
		case (CUDA_ERROR_INVALID_IMAGE): return (std::string)"CUDA_ERROR_INVALID_IMAGE " ;
		case (CUDA_ERROR_INVALID_CONTEXT): return (std::string)"CUDA_ERROR_INVALID_CONTEXT " ;
		case (CUDA_ERROR_CONTEXT_ALREADY_CURRENT): return (std::string)"CUDA_ERROR_CONTEXT_ALREADY_CURRENT " ;
		case (CUDA_ERROR_MAP_FAILED): return (std::string)"CUDA_ERROR_MAP_FAILED " ;
		case (CUDA_ERROR_UNMAP_FAILED): return (std::string)"CUDA_ERROR_UNMAP_FAILED " ;
		case (CUDA_ERROR_ARRAY_IS_MAPPED): return (std::string)"CUDA_ERROR_ARRAY_IS_MAPPED " ;
		case (CUDA_ERROR_ALREADY_MAPPED): return (std::string)"CUDA_ERROR_ALREADY_MAPPED " ;
		case (CUDA_ERROR_NO_BINARY_FOR_GPU): return (std::string)"CUDA_ERROR_NO_BINARY_FOR_GPU " ;
		case (CUDA_ERROR_ALREADY_ACQUIRED): return (std::string)"CUDA_ERROR_ALREADY_ACQUIRED " ;
		case (CUDA_ERROR_NOT_MAPPED): return (std::string)"CUDA_ERROR_NOT_MAPPED " ;
		case (CUDA_ERROR_NOT_MAPPED_AS_ARRAY): return (std::string)"CUDA_ERROR_NOT_MAPPED_AS_ARRAY " ;
		case (CUDA_ERROR_NOT_MAPPED_AS_POINTER): return (std::string)"CUDA_ERROR_NOT_MAPPED_AS_POINTER " ;
		case (CUDA_ERROR_ECC_UNCORRECTABLE): return (std::string)"CUDA_ERROR_ECC_UNCORRECTABLE " ;
		case (CUDA_ERROR_UNSUPPORTED_LIMIT): return (std::string)"CUDA_ERROR_UNSUPPORTED_LIMIT " ;
		case (CUDA_ERROR_CONTEXT_ALREADY_IN_USE): return (std::string)"CUDA_ERROR_CONTEXT_ALREADY_IN_USE " ;
		case (CUDA_ERROR_INVALID_SOURCE): return (std::string)"CUDA_ERROR_INVALID_SOURCE " ;
		case (CUDA_ERROR_FILE_NOT_FOUND): return (std::string)"CUDA_ERROR_FILE_NOT_FOUND " ;
		case (CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND): return (std::string)"CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND " ;
		case (CUDA_ERROR_SHARED_OBJECT_INIT_FAILED): return (std::string)"CUDA_ERROR_SHARED_OBJECT_INIT_FAILED " ;
		case (CUDA_ERROR_OPERATING_SYSTEM): return (std::string)"CUDA_ERROR_OPERATING_SYSTEM " ;
		case (CUDA_ERROR_INVALID_HANDLE): return (std::string)"CUDA_ERROR_INVALID_HANDLE " ;
		case (CUDA_ERROR_NOT_FOUND): return (std::string)"CUDA_ERROR_NOT_FOUND " ;
		case (CUDA_ERROR_NOT_READY): return (std::string)"CUDA_ERROR_NOT_READY " ;
		case (CUDA_ERROR_LAUNCH_FAILED): return (std::string)"CUDA_ERROR_LAUNCH_FAILED " ;
		case (CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES): return (std::string)"CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES " ;
		case (CUDA_ERROR_LAUNCH_TIMEOUT): return (std::string)"CUDA_ERROR_LAUNCH_TIMEOUT " ;
		case (CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING): return (std::string)"CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING " ;
		case (CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED): return (std::string)"CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED " ;
		case (CUDA_ERROR_PEER_ACCESS_NOT_ENABLED): return (std::string)"CUDA_ERROR_PEER_ACCESS_NOT_ENABLED " ;
		case (CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE): return (std::string)"CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE " ;
		case (CUDA_ERROR_CONTEXT_IS_DESTROYED): return (std::string)"CUDA_ERROR_CONTEXT_IS_DESTROYED " ;
		case (CUDA_ERROR_ASSERT): return (std::string)"CUDA_ERROR_ASSERT " ;
		case (CUDA_ERROR_TOO_MANY_PEERS): return (std::string)"CUDA_ERROR_TOO_MANY_PEERS " ;
		case (CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED): return (std::string)"CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED " ;
		case (CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED): return (std::string)"CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED " ;
		case (CUDA_ERROR_UNKNOWN): return (std::string)"CUDA_ERROR_UNKNOWN" ;
		default :
			return "CUDA switch not known" ;
#else
		
	std::cerr << "Warning: Get_ERROR_STR() Not compiled for CUDA" << std::endl ;
  std::cerr << " MAGMA_EVD_SERVER Error: In src/make.inc please define USEGPU=1 within src/make.inc if you have NVidia GPUs or AMD GPUs or USEGPU=0 if you have Intel Phi and the relevant MAGMA library " << std::endl ;
	
#endif 
	
	}

	return "" ;
}





//Test if file exists (actually tests if it is accessible)
std::string  GetString(std::string pathString, std::string findStr) 
{
    std::string resstring, tempstring ;
    CStringManipulation csm ;
    
    //pathString.append("/extdata/platformstring.txt") ;
    std::ifstream f(pathString.c_str(), std::ifstream::in);
    if (f.is_open()) {    
      while (f.good()) {
        getline(f,resstring) ; // could place this in a while (f.good()) loop
          //  std::cout << "resstring: " << resstring << std::endl ;
          resstring = csm.LeftSideOfString("//", resstring, false) ;
          tempstring = csm.LeftSideOfString("=", resstring, false) ;
          if(findStr == tempstring)
          {
              resstring = csm.RightSideOfString("=", resstring, true) ;
              f.close();
              return resstring ;
          }          
        }
        f.close();
        resstring = "" ;
        return resstring ;
    } else {
        std::cerr << "MAGMA_EVD_SERVER Error: File " << pathString << " could not be opened " << std::endl ;
        std::cerr << "MAGMA_EVD_SERVER Error: Please ensure it exists and contains an appropriate OpenCL platform string " << std::endl ;
        return "" ;
    }   
}



/* ////////////////////////////////////////////////////////////////////////////
   -- Print the available GPU device information (if input == TRUE)  and return the number of devices present
*/
int print_devices( bool print_num_bool, std::string pathString)
{
  int ndevices = 0 ;
  int cublasreturn ;
  std::string vendorstr ; 
  std::string devtype ;
  int clDevType ;
  
  // vendorstr = GetPlatformString( pathString) ;
  pathString.append("/extdata/platformstring.txt") ;
  vendorstr =  GetString(pathString, "platform") ;
  devtype =  GetString(pathString, "device") ;
  
  if (vendorstr.length() == 0)
  {
      std::cerr << " MAGMA_EVD_SERVER Error: Could not get the platform string " << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: Please specify an OpenCL platform using file: " << std::endl ;
      std::cerr <<  "\t <package install dir>/extdata/platformstring.txt " << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: Available platforms are: " << std::endl ;
#if !defined _WITH_CUDA 
      ndevices = GetNumDevicesUsingOpenCL( vendorstr, CL_DEVICE_TYPE_GPU) ;
#endif
      std::cerr << " MAGMA_EVD_SERVER Error: Exiting " << std::endl ;
      server_close() ;
      exit(-1) ;
  }
  if (devtype.length() < 3)
  {
      std::cerr << " MAGMA_EVD_SERVER Error: Could not get the device type requested " << devtype <<  std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: Please specify an OpenCL device using the file: " << std::endl ;
      std::cerr <<  "\t <package install dir>/extdata/platformstring.txt " << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: Available device types are: CPU, GPU or MIC " << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: Exiting " << std::endl ;
      server_close() ;
      exit(-1) ;
  }

//  std::cout << "Obtained from file: " << vendorstr << " and device type = " << clDevType << std::endl ;  
// vendorstr.append(_OPENCL_VENDOR_STRING) ;
  
#if defined _WITH_CUDA 	
   if (print_num_bool==true)
   std::cout << "MAGMA_EVD_SERVER INFO: About to check for number of GPUs() (using CUDA)" << std::endl ;
   
	  cublasreturn = cuDeviceGetCount( &ndevices );
  	if( CUDA_SUCCESS != cublasreturn ) 
	  {                
  		std::cout <<  "MAGMA_EVD_SERVER error: cuDeviceGetCount() failed, return value was:" << cublasreturn << "  " << std::endl ; 
  		std::cout  <<  Get_ERROR_STR(cublasreturn) << std::endl ;         
	  }   
	  if (print_num_bool == true) 
		  std::cout << "MAGMA_EVD_SERVER info: Number of devices found:" << ndevices << std::endl ;

    for( int idevice = 0; idevice < ndevices; idevice++ )
    {
      char name[200];
    	#if CUDA_VERSION > 3010 
          size_t totalMem;
    	#else
          unsigned int totalMem;
    	#endif

      int clock;
      int major, minor;
      CUdevice dev;

	  //Rcpp::Rcout << "rcppMagma: cuDeviceGet():" << std::endl ;
      cuDeviceGet( &dev, idevice );
	  //Rcpp::Rcout << "rcppMagma: cuDeviceGetName():" << std::endl ;
      cuDeviceGetName( name, sizeof(name), dev );
	  //Rcpp::Rcout << "rcppMagma: cuDeviceComputeCapability():" << std::endl ;
      cuDeviceComputeCapability( &major, &minor, dev );
	  //Rcpp::Rcout << "rcppMagma: cuDeviceTotalMem():" << std::endl ;
      cuDeviceTotalMem( &totalMem, dev );
	  //Rcpp::Rcout << "rcppMagma: cuDeviceGetAttribute():" << std::endl ;
      cuDeviceGetAttribute( &clock, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, dev );
	  
      if (print_num_bool == true) 
		  std::cout << "device " << idevice << " " << name << " " << clock/1000.f << " MHz clock, " << totalMem/1024.f/1024.f <<  " MB memory, capability "  << major << "." << minor << std::endl ;

    }
#else
    
   if  (memcmp ( devtype.c_str(), "cpu", 3 )==0 )
    clDevType = CL_DEVICE_TYPE_CPU ;
  else if (memcmp ( devtype.c_str(), "gpu", 3 )==0) 
	  clDevType =  CL_DEVICE_TYPE_GPU ;
  else if (memcmp ( devtype.c_str(), "acc", 3 )==0) 
	  clDevType =  CL_DEVICE_TYPE_ACCELERATOR ;
 
 if (print_num_bool==true)
   std::cout << "MAGMA_EVD_SERVER INFO: About to check for number of GPUs() (using OpenCL)" << std::endl ;
   
   
   
  if (clDevType == CL_DEVICE_TYPE_GPU )
   //ndevices = 3 ;
   ndevices = GetNumDevicesUsingOpenCL( vendorstr, CL_DEVICE_TYPE_GPU) ;
  else if (clDevType == CL_DEVICE_TYPE_CPU )
   ndevices = GetNumDevicesUsingOpenCL( vendorstr, CL_DEVICE_TYPE_CPU) ;
  else if (clDevType == CL_DEVICE_TYPE_ACCELERATOR )
   ndevices = GetNumDevicesUsingOpenCL( vendorstr, CL_DEVICE_TYPE_ACCELERATOR ) ; 
#endif

	return (ndevices) ;
}


int GetNumDevicesUsingOpenCL_C(std::string plat_str, int clDevTypeIN )
{
#if defined _WITH_CUDA 
  std::cout << " MAGMA_EVD_SERVER Info: GetNumDevicesUsingOpenCL_C(): _WITH_CUDA was defined so OpenCL will not be used" << std::endl ;
  return (0) ;
#else
  cl_platform_id oclpd_PlatformID ;
	cl_uint oclpd_NumDevices  = 0 ;
	cl_int clErrNum =0 ;

	const char * platformNameWantedIn = plat_str.c_str() ;  // platform_name  == "NVIDIA CUDA"  "ATI Stream"
	int platformNumberWantedIn = 0 ;
	char chBuffer[1024];
  cl_uint num_platforms = 0;
  cl_platform_id* clPlatformIDs  = NULL ;
  cl_int oclpd_ErrNum;
	cl_uint i ;
	int platIDNum = -1 ;

    // Get OpenCL platform count
	  // need to allocate this since I started checking for memory problems as memcpy() dies (which is used in clGetPlatformIDs())
	  clPlatformIDs = (cl_platform_id*)malloc(1 * sizeof(cl_platform_id)) ;
    oclpd_ErrNum = clGetPlatformIDs (1, clPlatformIDs, &num_platforms);
    if (oclpd_ErrNum != CL_SUCCESS)
    {
    	  free(clPlatformIDs) ;
		    std::cerr<<  "openclGetPlatformID() Error in clGetPlatformIDs Call !!!"   << std::endl ;
        return -1000;
    }
    
    if(num_platforms == 0)
    {
        free(clPlatformIDs) ;
		    std::cerr <<  "openclGetPlatformID() Error - No OpenCL platform found!"   << std::endl ;		
        return -32;
    }
    
     free(clPlatformIDs) ;  // free the original tester version
    
    // if there's a platform or more, make space for ID's
    if ((clPlatformIDs = (cl_platform_id*)malloc(num_platforms * sizeof(cl_platform_id))) == NULL)
    {
        std::cerr <<   "openclGetPlatformID() Error - Failed to allocate memory for cl_platform ID's!"   << std::endl ;		
        return -3000;
    }

			if (platformNameWantedIn != NULL) // will also select a specified number of the platform if given (>= 0)
			{
					oclpd_ErrNum = clGetPlatformIDs (num_platforms, clPlatformIDs, NULL);
					// OCLErrorString(oclpd_ErrNum) ;
					 i = 0 ;
					for( i = 0; i < num_platforms; ++i)
					{
						oclpd_ErrNum = clGetPlatformInfo (clPlatformIDs[i], CL_PLATFORM_NAME, 1024, &chBuffer, NULL);
						std::cout<<  "OpenCL platform available : " << chBuffer   << "  And looking for: " << platformNameWantedIn << std::endl ;
						if(oclpd_ErrNum == CL_SUCCESS)
						{
								if((strstr(chBuffer, platformNameWantedIn) != NULL) /*&& (i == platformNumberWantedIn)*/) // if the platform name matches the one wanted then set
								{
									// *oclpd_PlatformInOut = clPlatformIDs[i];  platIDNum
									platIDNum = i ;
									break;
								}
						}
						else
							return (0) ; // no devices available in the platform!
					}
			}

	if (platIDNum == -1)
		return -32 ; // == CL_INVALID_PLATFORM
	
	clErrNum = clGetDeviceIDs(clPlatformIDs[platIDNum], clDevTypeIN,  0,  NULL,  &oclpd_NumDevices);
	free(clPlatformIDs);
	return  (oclpd_NumDevices) ;
  
#endif
}


/*!
 * Uses the OpenCL library to determine how many devices are present of the type requested in the platform specified
 * @param plat_str  The OpenCL platform string that can be used to choose which hardware we want to use. Examples would be NVidia or AMD or Intel
 * @param clDevType The device type to choose from the platform - CL_DEVICE_TYPE_CPU for CPU (AMD or Intel platforms), CL_DEVICE_TYPE_GPU for GPUs (AMD or NVidia or Intel)  CL_DEVICE_TYPE_ACCELERATOR (for Intel Phi on Intel platform)
 */

int GetNumDevicesUsingOpenCL(std::string plat_str, int clDevType )
{
#if defined _WITH_CUDA 
  std::cout << " MAGMA_EVD_SERVER Info: _WITH_CUDA was defined so OpenCL will not be used" << std::endl ;
  return (0) ;
#else
  
    // The .getInfo() method will copy the platform  names available into 
    // this string and we will try to match what is available with 
    // what is requested through plat_str
    // Get available platforms
   std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    std::string platformVendor ; 
    
    //Convert the input string to lower case so we can compare lower case with lower case
    std::transform(plat_str.begin(), plat_str.end(), plat_str.begin(), ::tolower);
  
  	// Get info on the specific platform number requested	
		int  found ;
    int platnum = -1 ;
    std::cout << " MAGMA_EVD_SERVER Info: Available OpenCL CL_PLATFORM_VENDOR strings: " << std::endl ;
    
    
		for (int t1 = 0 ; t1 < platforms.size(); t1++)
		{
			platforms[t1].getInfo((cl_platform_info) CL_PLATFORM_VENDOR, &platformVendor); // Obtain a platforms vendor info string
      std::cout << "\t\t" << platformVendor << std::endl ;
			std::transform(platformVendor.begin(), platformVendor.end(), platformVendor.begin(), ::tolower);  // Convert to lower case - from the #include <algorithm> library
			
      found = platformVendor.find(plat_str) ;
			if (found != std::string::npos)
				platnum = t1 ;
		}
    if (platnum == -1)
    {
      std::cerr << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: GetNumDevicesUsingOpenCL() The platform that this package was built for does not seem to exist!" << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: platform requested: " << plat_str << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: Recompile the nonsyevd_server executable (using Make_SYEVD_Server()) specifying " << std::endl ;
      std::cerr << " MAGMA_EVD_SERVER Error: an available platform (as seen above)in the <R_LIB>/MagmaEigenNonsym/src/make.inc file " << std::endl ;
    }

		platforms[platnum].getInfo((cl_platform_info) CL_PLATFORM_VENDOR, &platformVendor);
    
		std::cout   <<   "Selected: "   <<   "\t" << platformVendor << std::endl; 

		std::vector<cl::Device> devices ;  // this will be a list of all devices
		platforms[platnum].getDevices(clDevType,&devices) ;  // Get the full list of devices available on this platform
		return (devices.size()) ; // return the number of available devices
   
  return (0) ;
#endif
			
}

