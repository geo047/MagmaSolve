# Makefile file for MagmaSolve  server compilation

PAC_DIR = ../bin
SERVER_DIR = ..
include ./make.inc
RM=rm -f


# -I$(CUDA_ROOT)/include is needed by magma.h header for CUDA version of MAGMA
INCCUBLAS = -I$(CUDA_ROOT)/include
INC       = -I../include  $(INCCUBLAS)
CPPFLAGS  = -DMAGMA_ILP64 
CPPFLAGS 	+= -D_WITH_CUDA=1
CXXFLAGS  = -g -O3 -xHost $(INC) -I$(MAGMALIB)/include 

 
#  All external libraries
LIBMKL =   -Wl,-rpath,$(INTEL_MKL_HOME)/lib/intel64_lin  -lmkl_intel_ilp64
LIBMAGMA  = -L$(MAGMALIB)/lib -Wl,-rpath,$(MAGMALIB)/lib -lmagma 
LIBCUDA		= -lcuda -lcudart
LIBGPU		=  -lOpenCL
LIBEXT    = $(LIBBLAS) -lpthread -lrt
LIB       = $(LIBDIR) $(LIBGPU) $(LIBMAGMA) $(LIBCUDA) $(LIBMKL)   $(LIBEXT)


SRCS=shmem_solve_server.cpp CStringManipulation.cpp
OBJS=$(subst .cpp,.o,$(SRCS))

all: solve_server.exe

solve_server.exe: $(OBJS)
	$(CXX) $(LDFLAGS) -o solve_server.exe $(OBJS) $(LIB) 

install:
	mkdir -p ../bin
	mv solve_server.exe ../bin

clean:
	$(RM) $(OBJS)

dist-clean: clean
	$(RM) solve_server ../bin/solve_server.exe 
