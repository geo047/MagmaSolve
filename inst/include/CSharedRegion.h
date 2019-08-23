#include <stdio.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <semaphore.h>
#include <errno.h>   
#include <cmath>

#include <algorithm>

typedef enum {HIDE, PRINT, LOG} hideprintlog;
typedef enum {INVMOD, SQRTMOD, INVSQRTMOD} matrixmod ;
typedef enum {WANTVECTORS, DONTWANTVECTORS, EXITSERVER} servermessage ;
#if (_USE_RCPP==1)
#define _PRINTERROR Rcpp::Rcerr
#define _PRINTSTD Rcpp::Rcout
#define _STOPFUNCTION Rcpp::stop(msg);
#else
#define _PRINTERROR std::cerr
#define _PRINTSTD std::cout
#define _STOPFUNCTION _PRINTERROR << msg << std::endl ;	exit(EXIT_FAILURE);
#endif

/*! \brief Struct used for to store inputs from command line arguments using the getopt.h library
 Struct used for to store inputs from command line arguments using the getopt.h library*/
struct arg_list {
	size_t 	matrixDimension ;	  /*! We assume a symmetric (square) matrix, so this is the row and column dimension */
	bool 	weWantVectors  ;	  /*! Boolean used to tell MAGMA function that we do or dont want eigenvectors*/
	int 	numGPUsWanted  ;	  /*! The number of GPUs to use - this will be checked against the number of GPUs present and truncated if need be*/
	std::string       memName    ;  /*! The name of the shared memory region */
	std::string       semName    ;  /*! the name of the locking semaphore used to message when the server has finshed with the memory area*/
  hideprintlog      msgChannel ;  /*! HIDE, PRINT or LOG - LOG may not be implemented*/
};

// producer = MAGMA_EVD_CLIENT and consumer = MAGMA_EVD_SERVER
typedef enum {MAGMA_EVD_CLIENT, MAGMA_EVD_SERVER} clientOrserver;
#define PID_STR_SIZE 10

/*!  Usage: Client program (e.g. R or R package) will launch the MAGMA_EVD_SERVER process, passing in the strings variable _memName and _semName for the server to mmap / signal with. 
These strings should be of form: "/syevdxmemory_<PID_OF_CLIENT>" and "/sem_<PID_OF_CLIENT>"
The client should also know the size of the matrix to be shared and pass that to the server.
 */

// If the protocol of using the **last** sizeof(size_t) bytes of the shmem region to hold the current matrix size
// does not work then we may need to map a data structure
// to the mmap() region, such as the following
struct shm_base {
  size_t matrixsize ;
  double * _shm_base ;
} ;
 
 // Defines "structure" of shared memory 
class CSharedRegion {       
public:
  size_t _regionsize_bytes ;  // the size in bytes of the shared memory region double array, rounded up to the page size
  size_t _numgpus ;
	size_t _max_matrix_dim ;
  size_t _matrix_dim ;
	double * _shm_base ;  // base pointer to be shared - We will treat the shared area as a single area that we can 'map' whatever data structure into as we may want to redefine what is shared.
  double * _vectors ;   // _vectors = _shm_base ;  		
	double * _values ;	  // _values  = _shm_base + (_max_matrix_dim * _max_matrix_dim) ;
	int _r ;
	char   * _memName ; // == "/syevdxmemory_<PID_OF_CLIENT>";
//  std::string * _memNameStr ;
	int 	_shm_fd ;  // shared memory file descriptor - I am currently closing this when the object is destroyed
	std::string _datapathstring ;
	clientOrserver _clientOrServer ; // Used to distingush if an object is the client (producer) or the server (consumer - consumes data to return dgeev_m decomposition)
	char   * _semName ; //  == "/sem_<PID_OF_CLIENT>"
	sem_t  * _sem_id ;  // semaphore for locking region of memory 
//  std::string  _semNameStr ;
  
	char   PID_str[PID_STR_SIZE]  ; 
	servermessage	_weWantVectors  ;  // this is used to comm a few different things, see definition of servermessage for options.
  hideprintlog  _msgChannel     ;
  
  std::string logserver ;  // file name of log file for server
	std::string logclient ;  // file name of log file for client
  
  std::string errorStr ;
	
  
  // This is the creator
	CSharedRegion(clientOrserver PC, struct arg_list main_args) 
	{
		this->_numgpus = main_args.numGPUsWanted ;
    if (main_args.weWantVectors==0)
      this->_weWantVectors = DONTWANTVECTORS ;
    else
		  this->_weWantVectors = WANTVECTORS ;
      
    this->_msgChannel = main_args.msgChannel ;
      
    // Set this instance to be either a client or server  
		this->_clientOrServer = PC ;
    
    // Logging 
    logserver.append("/tmp/dgeev_server.log") ;
    logclient.append("/tmp/dgeev_client.log") ;
    
  
		if (this->_clientOrServer == MAGMA_EVD_CLIENT)  // this provides the matrix data (i.e. is the R process)
		{
			int pid_str_size ;
      //mode_t sem_mode = S_IRUSR | S_IWUSR ; could be used in sem_open()
      if (this->_msgChannel == PRINT) _PRINTSTD << "CSharedRegion(): MAGMA_EVD_CLIENT creator"  << std::endl ;
			pid_str_size = sprintf(PID_str, "%ld", (long)getpid());
			if (pid_str_size > PID_STR_SIZE) 
				error_and_die("CSharedRegion() MAGMA_EVD_CLIENT Error: PID string overrun buffer. Increase PID_STR_SIZE");			     
			
			this->_vectors = NULL ;
			this->_values = NULL ;
			this->_max_matrix_dim = main_args.matrixDimension ;
      this->_matrix_dim = main_args.matrixDimension ;
			
			size_t namesize = strlen(main_args.memName.c_str()) ;  // find the length of the input string
			namesize += pid_str_size ;  // add the size of the PID string part
			this->_memName = (char *) malloc(namesize+1) ;   // allocate the same size in our class variable
			//this->_memName = memcpy( main_args.memName,  namesize ) ; // copy from input to our class variable		
			sprintf(this->_memName, "%s_%s\0", main_args.memName.c_str(),this->PID_str );
       
			this->_shm_fd = shm_open(this->_memName, O_CREAT | O_TRUNC | O_RDWR, 0600);
			if (this->_shm_fd == -1)
				error_and_die("CSharedRegion() MAGMA_EVD_CLIENT Error calling: shm_open");
       
      // The last two sizeof(size_t) bytes of the shared memory area will contain the size of the current decomposition 
      // to be performed and then if we want to calculate eigenvectors or not (placed into _weWantVectors) respectively. 
      // This will be checked by the server to see if the matrix size has changed and pointers to evects and evals in 
      // the CSharedMemory object need to be shifted. The _weWantVectors position is overloaded with meaning as defined in
      // servermessage enumerated values above, primarily to message the server to quit.
      _regionsize_bytes = (2*sizeof(double)) + (((_max_matrix_dim * _max_matrix_dim) + _max_matrix_dim ) * sizeof(double) );  // Has to fit an array of eigenvectors and a vector of eigenvalues 
			int roundup_to = sysconf(_SC_PAGE_SIZE);
			_regionsize_bytes = closestdivisible_bysize(_regionsize_bytes, roundup_to) ;  // round up from matrix +1 vector size to nearest page size
			_r = ftruncate(this->_shm_fd, _regionsize_bytes);
			if (_r != 0)
				error_and_die("CSharedRegion() MAGMA_EVD_CLIENT Error calling:  ftruncate");
       
			_shm_base = (double *) mmap(0, _regionsize_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, this->_shm_fd, 0);
			if (_shm_base == MAP_FAILED)
				error_and_die("CSharedRegion() Error calling:  mmap");
        
			 /* close the shared memory segment as if it was a file  - this is done in the CSharedMemory objects destructor*/			  		
			_vectors = _shm_base ;
			_values  = _shm_base + (_max_matrix_dim * _max_matrix_dim) ;
			
			size_t semnamesize = strlen(main_args.semName.c_str()) ;  // find the length of the input string
			semnamesize += pid_str_size ;  // add the size of the PID string part
			this->_semName = (char *) malloc(semnamesize+1) ;   // allocate the same size in our class variable
			//memcpy( this->_semName, main_args.semName,  semnamesize ) ; // copy from input to our class variable
			sprintf(this->_semName, "%s_%s", main_args.semName.c_str(),this->PID_str );
      
      // S_IRUSR | S_IWUSR 
      // This semaphore will be locked until this client calls sem_post()
			_sem_id=sem_open(this->_semName, O_CREAT, S_IRUSR | S_IWUSR, 0);  // Open semaphore with an initial value of 0 (locked)
			if (this->_sem_id == SEM_FAILED)
				error_and_die("CSharedRegion() MAGMA_EVD_CLIENT Error calling: sem_open failed: sem_open()");
			
		}
		else if (this->_clientOrServer == MAGMA_EVD_SERVER) // the input data consumer (although it also produces evdx results)
		{
      // N.B. the client (in R) must be created before the server. This is done using the MagmaGPU:::RunServer() R function.
      // The RunServer function calls GetServerArgs() Rcpp function, that creates the client CSharedMemory object and then launches the 
      // server using a system() call.
      // The server will block as the semaphore was created by the client in the 0 state, until the client calls sem_post(), which it will do
      // after it writes matrix data into the shared memory region of client-server pair. On sem_post() by the client, the server will do the 
      // requested decomposition, while the client waits after a sem_wait() operation by the server. Once the server has finished it signals the
      // client via sem_post() that it has finished and then the client can use the decompoistion data, while the server then waits again (as client called sem_wait().
			this->_vectors = NULL ;
			this->_values = NULL ;
			this->_max_matrix_dim = main_args.matrixDimension ;
			this->_matrix_dim = main_args.matrixDimension ;
      this->errorStr.append("CSharedRegion() MAGMA_EVD_SERVER Error calling: ") ;
      
      if (this->_msgChannel == PRINT) _PRINTSTD << "CSharedRegion(): MAGMA_EVD_SERVER creator"  << std::endl ;
			// N.B. main_args.memName and this->_semName will be fully qualified PID inclusive strins as they are passed in from the Client R process
	   
			size_t namesize = strlen(main_args.memName.c_str()) ;  // find the length of the input string (does not count final NULL)    
			this->_memName = (char *) malloc(namesize+1) ;   // allocate the same size in our class variable
			memcpy( (void *) this->_memName, (const void *)main_args.memName.c_str(),   namesize+1 ) ; // copy from input to our class variable		
			
     // this->PrintObjectDetails( false ) ;
      
			this->_shm_fd = shm_open(this->_memName, O_RDWR, 0600);
      if (this->_shm_fd == -1)
      {
        int tryagain = 5 ;
        _PRINTERROR << "SERVER Error: shm_open() failed to open named shared memory region: " << this->_memName << std::endl ;
        
        while (tryagain > 0) {
            sleep(1) ;
            this->_shm_fd = shm_open(this->_memName, O_RDWR, 0600);
    	      if (this->_shm_fd == -1)  {
              _PRINTERROR << "SERVER Error: shm_open() failed to open named shared memory region:" << this->_memName << std::endl ;
              tryagain-- ;
              if (tryagain == 0) {                
                error_and_die("SERVER Error: shm_open:() failed to open named shared memory region. Exiting.");
              }
  		      }
            else
              tryagain = 0 ;            
        }        
      }
		
			// the last sizeof(size_t) bytes in the memory area will conatin thr size of the current decomposition to be performed.
      // This will be checked by the server to see if the matrix size has chaged and pointers to evects and evals need to be shifted
      _regionsize_bytes = (2*sizeof(double)) + (((_max_matrix_dim * _max_matrix_dim) + _max_matrix_dim ) * sizeof(double) );   // Has to fit an array of eigenvectors and a vector of eigenvalues 
			int roundup_to = sysconf(_SC_PAGE_SIZE);
			_regionsize_bytes = closestdivisible_bysize(_regionsize_bytes, roundup_to) ;  // round up to the page size

			_shm_base = (double *) mmap(0, _regionsize_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, this->_shm_fd, 0);
			if (_shm_base == MAP_FAILED)
				error_and_die("CSharedRegion() Error calling:  mmap");
			
			/* close the shared memory segment as if it was a file */
			//if (close(this->_shm_fd) == -1)
			//	error_and_die("CSharedRegion() MAGMA_EVD_SERVER Error calling: close(this->_shm_fd)\n");
			
			_vectors = _shm_base ;
			_values  = _shm_base + (_max_matrix_dim * _max_matrix_dim) ;
			
			// N.B. main_args.memName and this->_semName will be fully qualified name (i.e. inclusive of PID)
			size_t semnamesize = strlen(main_args.semName.c_str()) ;  // find the length of the input string
			this->_semName = (char *) malloc(semnamesize+1) ;   // allocate the same size in our class variable
			memcpy( (void *)  this->_semName, (const void *)  main_args.semName.c_str(),  semnamesize +1) ; // copy from input to our class variable
			
     if (this->_msgChannel == PRINT) _PRINTSTD << " CSharedRegion SERVER object: about to call sem_open():  " <<  this->_semName << std::endl  ;  
      std::flush(_PRINTSTD) ;
      _sem_id=sem_open(this->_semName, 0);  // Open named semaphore (with oflag set to 0)
			if (this->_sem_id == SEM_FAILED)
      {
        int tryagain = 5 ;
        _PRINTERROR << "SERVER failed to open named semaphore " << this->_semName << std::endl ;
        
        while (tryagain > 0) {
            sleep(1) ;
            _sem_id=sem_open(this->_semName, 0);  // Open named semaphore (with oflag set to 0)
  		      if (this->_sem_id == SEM_FAILED)  {
              _PRINTERROR << "SERVER failed to open named semaphore: " << this->_semName << std::endl ;
              tryagain-- ;
              if (tryagain == 0) {                
                error_and_die(errorStr.append( "sem_open() failed to open named semaphore. Exiting." ));
              }
  		      }
            else
              tryagain = 0 ;            
        }        
      }

		}
    
    if (this->_msgChannel == PRINT)  
      this->PrintObjectDetails( false ) ;
    else if (this->_msgChannel == LOG)  
      this->PrintObjectDetails( true ) ;
		
    // If this is MAGMA_EVD_CLIENT then we have to go and launch the server process with the details we used to create the shared mem area and the named semaphore
	
  }
  
  // The client can request the server to exit by placing a value of 2 in the shared memory region 
  // where the weWantVectors flag woulld usually go. The server will read this value in SetCurrentMatrixSizeAndVectorsRequest() call
  // in its main loop and will exit.
  int ExitServer( void )
  { 
    if (this->_clientOrServer == MAGMA_EVD_CLIENT)
    {      
        // servermessage sm_weWantVectors = EXITSERVER ;
      
        double * LastPosOffset = this->_shm_base + ((this->_regionsize_bytes/sizeof(double)) - 2 ) ;
        // LastPosOffset[1] = (double) sm_weWantVectors ;
        LastPosOffset[1] = ConvertServerMessageToDouble(EXITSERVER) ;
        _PRINTSTD << " MAGMA_EVD_CLIENT Info: CSharedMemory->ExitServer() has just requested server to exit "  << std::endl ;
        
        // This signals to the server that is waiting on the client by releasing the semaphore
        if (sem_post(this->_sem_id) != 0) 
        {
          error_and_die("MAGMA_EVD_CLIENT Error: CSharedRegion->ExitServer() Error calling: sem_post() ");
        }        
    } 
  }
    
  servermessage ConvertDoubleToServerMessage(double inputDouble )
  {
      size_t t_sm ;
      t_sm = inputDouble ;
      servermessage sm_ret = (servermessage) t_sm ; 
      // _PRINTSTD << "ConvertDoubleToServerMessage INFO: inputDouble: " << inputDouble << " t_sm: "<< t_sm <<  " sm_ret: " << sm_ret << std::endl ;
      return (sm_ret) ;          
  }
  double  ConvertServerMessageToDouble( servermessage inputSM )
  {
      size_t t_sm ;
      t_sm = inputSM ;
      double d_ret = (double) t_sm ; 
      // _PRINTSTD << "ConvertToServerMessageDouble INFO: inputSM: " << inputSM << " t_sm: "<< t_sm <<  " d_ret: " << d_ret << std::endl ;
      return (d_ret) ;          
  }
  
  // This is set by the Client. 
  // The server has to check the last sizeof(size_t) bytes to see what matrix size has been input 
  // and will then set this->_matrix_dim and the pointers accordingly
  // returns 0 on normal operation of setting matrix and eigenvector pointers and > 0 if the client has requested the server to exit
  int SetCurrentMatrixSizeAndVectorsRequest(size_t matrixsize, bool weWantVectors)
  { 
    if (this->_clientOrServer == MAGMA_EVD_CLIENT)
    {
        this->_matrix_dim = matrixsize ;  // update matrixdim
        
        servermessage sm_weWantVectors ;
        if (weWantVectors == false)
          sm_weWantVectors = DONTWANTVECTORS ;
        else
          sm_weWantVectors = WANTVECTORS ;
          
        // this->_vectors = this->_shm_base ;
      	this->_values  = this->_shm_base + (this->_matrix_dim * this->_matrix_dim) ;  // this is where the evals results will be placed
        double * LastPosOffset = this->_shm_base + ((_regionsize_bytes/sizeof(double)) - 2 ) ;
        LastPosOffset[0] = static_cast<double>(matrixsize) ;
        // LastPosOffset[1] = static_cast<double>(sm_weWantVectors) ;
        // LastPosOffset[1] = (double) sm_weWantVectors ;
         LastPosOffset[1] = ConvertServerMessageToDouble(sm_weWantVectors) ;
        if (this->_msgChannel == PRINT) _PRINTSTD << "MAGMA_EVD_CLIENT INFO: recieved matrix size value of: " << this->_matrix_dim << std::endl ;
    } 
    else if (this->_clientOrServer == MAGMA_EVD_SERVER)
    {
        // As we are the server, find the 2 last double positions and read 
        // in the matrix size requested by the client and if we want vectors or not (or if we want to exit)
        double * LastPosOffset = this->_shm_base + ((_regionsize_bytes/sizeof(double)) - 2 ) ;
        double d_matdim , d_weWantVects ;
        servermessage sm_weWantVects ;
        
       // this->_matrix_dim =  static_cast<size_t>(LastPosOffset[0]) ;
      //  size_t temp_weWantVectors =  static_cast<size_t>(LastPosOffset[1]) ;
        d_matdim = LastPosOffset[0] ;
       // d_weWantVects = LastPosOffset[1] ;  // read as a double
        
        // convert from doubles into data type
      //  sm_weWantVects = (servermessage) d_weWantVects ;  // convert to a server message
        sm_weWantVects = ConvertDoubleToServerMessage(LastPosOffset[1]) ;
        this->_matrix_dim = (size_t) d_matdim ;
        
        if (sm_weWantVects ==  DONTWANTVECTORS)
          this->_weWantVectors  = DONTWANTVECTORS ;
        else if (sm_weWantVects == WANTVECTORS)
          this->_weWantVectors  = WANTVECTORS ;
        else  if (sm_weWantVects == EXITSERVER)
          return (1) ;  // this means we want to exit
          
        this->_values  = this->_shm_base + (this->_matrix_dim * this->_matrix_dim) ; 
        if (this->_msgChannel == PRINT) _PRINTSTD << "MAGMA_EVD_SERVER  INFO: recieved matrix size value of: " << this->_matrix_dim << std::endl ;
    }
    return (0) ;
  }
	
	/*! Destructor */
	~CSharedRegion() 
	{		
			
		/* close the shared memory segment as if it was a file */
		if (close(this->_shm_fd) == -1)
			error_and_die("CSharedRegion() MAGMA_EVD_CLIENT Error calling: close(this->_shm_fd)\n");
			
		if (munmap(this->_shm_base, _regionsize_bytes) != 0)
			error_and_die("~CSharedRegion() error from: munmap()");
		
		if (this->_clientOrServer == MAGMA_EVD_CLIENT) 
		{	
			if (shm_unlink(_memName) != 0)
			{
				error_and_die("~CSharedRegion() error from: shm_unlink()");	
			}
		}
		
		//Semaphore Close: Close a named semaphore
		if ( sem_close(_sem_id) < 0 )
			error_and_die("~CSharedRegion() error from: sem_close()");
		
		if (this->_clientOrServer == MAGMA_EVD_CLIENT) 
		{
			//Semaphore unlink: Remove a named semaphore  from the system.
			if ( sem_unlink(this->_semName) < 0 )
			{
				error_and_die("~CSharedRegion() error from: sem_unlink()");
			}
		}
	
		free(this->_memName) ;
		free(this->_semName) ;
   // free(this->_sem_id) ;
		
		_vectors = NULL ;
		_values = NULL ;
	}
	
	
	// this is a client function - copies from R matrix memory into shared memory region
	void copy_matrix_into_shmem(const void * in_ptr, size_t numbytes) 
	{
			
  //  this->PrintObjectDetails( false ) ;
		if (_vectors != NULL)
		{
		//	sem_wait(_sem_id);
			memcpy(this->_vectors, in_ptr, numbytes ) ;  // copy original matrix data into shared memory area
	//		sem_post(_sem_id);
		}
		else
			error_and_die("MAGMA_EVD_CLIENT ERROR: copy_matrix_into_shmem() : vectors shared memory area is NULL");
		
	}
	
	void copy_shmem_into_matrix(void * output_ptr, size_t numbytes, size_t offset_into_shmem) 
	{			
		if (_vectors != NULL)
		{
		//	sem_wait(_sem_id);
      // copy original matrix data into shared memory area
			memcpy(output_ptr, ((char *) this->_shm_base)+offset_into_shmem, numbytes ) ;  
     // if (offset_into_shmem == 0) output_ptr[0] = 1000.0 ;
		//	sem_post(_sem_id);
		}
		else
			error_and_die("MAGMA_EVD_CLIENT ERROR: copy_shmem_into_matrix() : vectors shared memory area is NULL");
		
	}
  
  
  // This one reverses the columns
  void copy_shmem_into_matrix_reverse_cols(double * output_ptr, const int matrix_rows) 
  {
    double * shmem_ptr =this->_shm_base ;
  //  #pragma omp parallel for shared(output_ptr , shmem_ptr)
    for (int t1 = 0 ; t1 < matrix_rows  ; t1++)
    {   
      for (int t2 = 0 ; t2 < matrix_rows ; t2++)
      {
        int opposite_t2 = matrix_rows - t2 - 1;
        output_ptr[t1 * matrix_rows + t2] = shmem_ptr[t1 * matrix_rows + opposite_t2] ;  // place first col in mirror image opposite column
      }
    }
  }
  
  
  // This one reverses the rows
  void copy_shmem_into_matrix_reverse_rows(double * output_ptr, const int matrix_rows) 
  {
    double * shmem_ptr =this->_shm_base ;
  //  #pragma omp parallel for shared(output_ptr , shmem_ptr)
    for (int t1 = 0 ; t1 < matrix_rows  ; t1++)
    {   
      for (int t2 = 0 ; t2 < matrix_rows ; t2++)
      {
        // evals_ptr_mod[t1]
        int opposite_t1 = matrix_rows - t1 - 1;
        output_ptr[opposite_t1 * matrix_rows + t2] = shmem_ptr[t1 * matrix_rows + t2] ;  // place top row in mirror image bottom row
      }
    }
  }
  
  // This one reverses the columns
  void copy_matrix_inplace_reverse_cols(double * output_ptr, const int matrix_rows) 
  {
    double * shmem_ptr =this->_shm_base ;
  //  #pragma omp parallel for shared(output_ptr , shmem_ptr)
    for (int t1 = 0 ; t1 < matrix_rows  ; t1++)
    {   
      for (int t2 = 0 ; t2 < matrix_rows ; t2++)
      {
        int opposite_t2 = matrix_rows - t2 - 1;
        double temp_val = output_ptr[t1 * matrix_rows + t2] ;  // store first column into temporary
        output_ptr[t1 * matrix_rows + t2] = output_ptr[t1 * matrix_rows + opposite_t2] ;  // place last col in mirror image at first col
        output_ptr[t1 * matrix_rows + opposite_t2] = temp_val ;  // place temp into last col
      }
    }
  }
  
  
  void copy_matrix_inplace_reverse_rows(double * output_ptr, const int matrix_rows) 
  {
    double * evect_row_buffer = new double[matrix_rows] ;
   // #pragma omp parallel for shared(output_ptr )
   
    // _PRINTSTD << "matrix_rows / 2 = " << matrix_rows / 2 << std::endl ;
    for (int t1 = 0 ; t1 < matrix_rows / 2 ; t1++)
    {   
        int opposite = matrix_rows - t1 - 1;
        for (int t2 = 0 ; t2 < matrix_rows ; t2++)       
          evect_row_buffer[t2] = output_ptr[t1 * matrix_rows + t2] ;  // place top row in buffer
        for (int t2 = 0 ; t2 < matrix_rows ; t2++) 
          output_ptr[t1 * matrix_rows + t2] = output_ptr[opposite * matrix_rows + t2] ; // copy bottom row (opposite row) over top row
        for (int t2 = 0 ; t2 < matrix_rows ; t2++) 
          output_ptr[opposite * matrix_rows + t2] = evect_row_buffer[t2] ;  // copy buffer of top row into bottom row
    }
     delete evect_row_buffer ;  // delete the line buffer
  }
  
  /* Moves the (modified) eigenvalues onto the diagonal of a matrix */
  void copy_shmem_into_matrix_with_mods(double * output_ptr, const int matrix_rows , const matrixmod matmod) 
  {
    double * evals_ptr = this->_shm_base + (matrix_rows * matrix_rows) ;
    
   // #pragma omp parallel for shared(output_ptr )
    for (int t1 = 0 ; t1 < matrix_rows; t1++)
    {   
      double evals_mod ;
      if (matmod == INVMOD)
        evals_mod = 1.0 / evals_ptr[t1] ;
      else if (matmod == SQRTMOD)
        evals_mod = std::sqrt(evals_ptr[t1]) ;
      else if (matmod == INVSQRTMOD)
        evals_mod = 1.0 / std::sqrt(evals_ptr[t1]) ;
      for (int t2 = 0 ; t2 < matrix_rows; t2++)
      {
        // evals_ptr_mod[t1]
        output_ptr[t1 * matrix_rows + t2] =  evals_mod * this->_shm_base[t1 * matrix_rows + t2] ;
      }
    }
    // delete evals_ptr_mod ;  // delete the copy of the modified eigenvalues
  }
	
  
  
	void error_and_die(std::string msg) 
	{
		// printf("%s %s\n", msg, strerror(errno));
		// perror(msg);
   //  _PRINTERROR << msg << std::endl ;
     
	//	exit(EXIT_FAILURE);
	//	Rcpp::stop(msg) ;
	  _STOPFUNCTION ;
	  
	}
  
  void PrintObjectDetails( const bool toLogFile ) 
  {
    std::ofstream out ;
    std::streambuf *coutbuf ;
    static int count = 0;
    
    if (toLogFile == true)
    {
      if (_clientOrServer == MAGMA_EVD_SERVER ) 
        out.open(logserver.c_str(),std::ios::out | std::ios::app );
      else 
        out.open(logclient.c_str(),std::ios::out | std::ios::app );
    
      coutbuf = _PRINTSTD.rdbuf(); //save old buf
      _PRINTSTD.rdbuf(out.rdbuf()); //redirect std::cout to log file
    }
    
    _PRINTSTD  <<  "_clientOrServer:  \t" ;
    if (_clientOrServer == MAGMA_EVD_CLIENT ) 
        _PRINTSTD << " MAGMA_EVD_CLIENT " << "Count = " << count++ << std::endl ;  // Used to distingush if an object is the client (producer) or the server (consumer - consumes data to return dgeev decomposition)
    else 
        _PRINTSTD << " MAGMA_EVD_SERVER " << "Count = " << count++ << std::endl ;
    _PRINTSTD <<  "_regionsize_bytes: \t" << _regionsize_bytes << std::endl ;  // the size in bytes of the shared memory region double array, rounded up to the page size
    _PRINTSTD <<  "_numgpus \t\t" <<  _numgpus  << std::endl ; 
    _PRINTSTD <<  "_weWantVectors \t\t" <<  _weWantVectors << std::endl ; 
    _PRINTSTD <<  "_max_matrix_dim \t" << _max_matrix_dim  << std::endl ; 
    _PRINTSTD <<  "_matrix_dim \t\t" <<  _matrix_dim << std::endl ; 
    _PRINTSTD <<   "_shm_base \t\t" <<  _shm_base << std::endl ; // base pointer to be shared - We will treat the shared area as a single area that we can 'map' whatever data structure into as we may want to redefine what is shared.
    _PRINTSTD <<  "_vectors \t\t" << _vectors  << std::endl ; 
    _PRINTSTD <<  "_values  \t\t" << _values << std::endl ; 	
    
    _PRINTSTD <<  "_memName \t\t " << _memName << std::endl ; // == "/syevdxmemory_<PID_OF_CLIENT>";
    _PRINTSTD <<  "_shm_fd \t\t" << _shm_fd  << std::endl ;   // shared memory file descriptor - I am currently closing this when the object is destroyed

    _PRINTSTD <<  "_semName \t\t" <<  _semName  << std::endl ; //  == "/sem_<PID_OF_CLIENT>"
    _PRINTSTD <<  "_sem_id points to: \t" << _sem_id << std::endl ;   // semaphore for locking region of memory 
	  _PRINTSTD << std::endl ;
    
    if (toLogFile == true)
    {
	    _PRINTSTD.rdbuf(coutbuf); //reset to standard output again
      out.close() ;
    }
	}

	/*
	void get_exepath() 	
	{
		// Getting the path to the package kernel file (<packagedir>/include) can be fun
		Rcpp::Function pathpackage_rcpp = Rcpp::Environment::base_env()["path.package"];
		SEXP retvect = pathpackage_rcpp ("MagmaGPU");  // use the R function in C++ 
		_datapathstring = Rcpp::as<std::string>(retvect) ; // convert from SEXP to C++ type
		// _oclcode_pathstring = "f:/R/R-3.2.2/library/Harman" ;
		_datapathstring = _datapathstring +"/src" ;
	}
	
	void make_exe() 	
	{
		// Getting the path to the package kernel file (<packagedir>/include) can be fun
		Rcpp::Function pathpackage_rcpp = Rcpp::Environment::base_env()["path.package"];
		SEXP retvect = pathpackage_rcpp ("MagmaGPU");  // use the R function in C++ 
		_datapathstring = Rcpp::as<std::string>(retvect) ; // convert from SEXP to C++ type
		// _oclcode_pathstring = "f:/R/R-3.2.2/library/Harman" ;
		_datapathstring = _datapathstring +"/src" ;
	}
	*/
	
	size_t closestdivisible_bysize(size_t inputsizeIN, size_t sizeIN)
	{
	  double result = 0 ;
	  result = (double) inputsizeIN / (double) sizeIN ;
	  result = ceil(result) ;
	  return (size_t) ( result * sizeIN ) ;
	}

}; // end of class definition

