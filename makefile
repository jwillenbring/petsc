#
# This is the makefile for compiling PETSc. See
# http://www.mcs.anl.gov/petsc/documentation/installation.html for directions on installing PETSc.
# See also conf for additional commands.
#
ALL: all
DIRS	 = src include tutorials interfaces share/petsc/matlab

# next line defines PETSC_DIR and PETSC_ARCH if they are not set
include ././${PETSC_ARCH}/lib/petsc/conf/petscvariables
include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test.common

# This makefile contains a lot of PHONY targets with improperly specified prerequisites
# where correct execution instead depends on the targets being processed in the correct
# order.  This is gross, but this makefile doesn't really do any work.  Sub-makes still
# benefit from parallelism.
.NOTPARALLEL:

OMAKE_SELF = $(OMAKE) -f makefile
OMAKE_SELF_PRINTDIR = $(OMAKE_PRINTDIR) -f makefile

#
# Basic targets to build PETSc libraries.
#
all:
	+@${OMAKE_SELF} PETSC_ARCH=${PETSC_ARCH}  PETSC_DIR=${PETSC_DIR} chk_petscdir chk_upgrade | tee ${PETSC_ARCH}/lib/petsc/conf/make.log
	@ln -sf ${PETSC_ARCH}/lib/petsc/conf/make.log make.log
	+@(${OMAKE_SELF_PRINTDIR} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} all-local; echo "$$?" > ${PETSC_ARCH}/lib/petsc/conf/error.log) 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log
	+@if [ "`cat ${PETSC_ARCH}/lib/petsc/conf/error.log 2> /dev/null`" != "0" ]; then \
           grep -E '(out of memory allocating.*after a total of|gfortran: fatal error: Killed signal terminated program f951|f95: fatal error: Killed signal terminated program f951)' ${PETSC_ARCH}/lib/petsc/conf/make.log | tee ${PETSC_ARCH}/lib/petsc/conf/memoryerror.log > /dev/null; \
           if test -s ${PETSC_ARCH}/lib/petsc/conf/memoryerror.log; then \
             printf ${PETSC_TEXT_HILIGHT}"**************************ERROR*************************************\n" 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log; \
             echo "  Error during compile, you need to increase the memory allocated to the VM and rerun " 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log; \
             printf "********************************************************************"${PETSC_TEXT_NORMAL}"\n" 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log;\
           else \
             printf ${PETSC_TEXT_HILIGHT}"**************************ERROR*************************************\n" 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log; \
             echo "  Error during compile, check ${PETSC_ARCH}/lib/petsc/conf/make.log" 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log; \
             echo "  Send it and ${PETSC_ARCH}/lib/petsc/conf/configure.log to petsc-maint@mcs.anl.gov" 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log;\
             printf "********************************************************************"${PETSC_TEXT_NORMAL}"\n" 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log;\
           fi \
	 else \
	  ${OMAKE_SELF} print_mesg_after_build PETSC_ARCH=${PETSC_ARCH}  PETSC_DIR=${PETSC_DIR} 2>&1 | tee -a ${PETSC_ARCH}/lib/petsc/conf/make.log ;\
        fi #solaris make likes to print the whole command that gave error. So split this up into the smallest chunk below
	@echo "Finishing make run at `date +'%a, %d %b %Y %H:%M:%S %z'`" >> ${PETSC_ARCH}/lib/petsc/conf/make.log
	@if [ "`cat ${PETSC_ARCH}/lib/petsc/conf/error.log 2> /dev/null`" != "0" ]; then exit 1; fi

all-local: info libs matlabbin petsc4py-build libmesh-build mfem-build slepc-build hpddm-build amrex-build bamg-build

#
# Prints information about the system and version of PETSc being compiled
#
info:
	-@echo "=========================================="
	-@echo " "
	-@echo "See documentation/faq.html and documentation/bugreporting.html"
	-@echo "for help with installation problems.  Please send EVERYTHING"
	-@echo "printed out below when reporting problems.  Please check the"
	-@echo "mailing list archives and consider subscribing."
	-@echo " "
	-@echo "  https://petsc.org/release/community/mailing/"
	-@echo " "
	-@echo "=========================================="
	-@echo Starting make run on `hostname` at `date +'%a, %d %b %Y %H:%M:%S %z'`
	-@echo Machine characteristics: `uname -a`
	-@echo "-----------------------------------------"
	-@echo "Using PETSc directory: ${PETSC_DIR}"
	-@echo "Using PETSc arch: ${PETSC_ARCH}"
	-@echo "-----------------------------------------"
	-@grep "define PETSC_VERSION" ${PETSC_DIR}/include/petscversion.h | ${SED} "s/........//" | head -n 7
	-@echo "-----------------------------------------"
	-@echo "Using configure Options: ${CONFIGURE_OPTIONS}"
	-@echo "Using configuration flags:"
	-@grep "\#define " ${PETSCCONF_H} | tail -n +2
	-@echo "-----------------------------------------"
	-@echo "Using C compile: ${PETSC_CCOMPILE_SINGLE}"
	-@if [  "${MPICC_SHOW}" != "" ]; then \
             printf  "mpicc -show: %b\n" "${MPICC_SHOW}";\
          fi; \
        printf  "C compiler version: %b\n" "${C_VERSION}"; \
        if [ "${CXX}" != "" ]; then \
        echo "Using C++ compile: ${PETSC_CXXCOMPILE_SINGLE}";\
        if [ "${MPICXX_SHOW}" != "" ]; then \
               printf "mpicxx -show: %b\n" "${MPICXX_SHOW}"; \
            fi;\
            printf  "C++ compiler version: %b\n" "${Cxx_VERSION}"; \
          fi
	-@if [ "${FC}" != "" ]; then \
	   echo "Using Fortran compile: ${PETSC_FCOMPILE_SINGLE}";\
           if [ "${MPIFC_SHOW}" != "" ]; then \
             printf "mpif90 -show: %b\n" "${MPIFC_SHOW}"; \
           fi; \
             printf  "Fortran compiler version: %b\n" "${FC_VERSION}"; \
         fi
	-@if [ "${CUDAC}" != "" ]; then \
	   echo "Using CUDA compile: ${PETSC_CUCOMPILE_SINGLE}";\
         fi
	-@if [ "${CLANGUAGE}" = "CXX" ]; then \
           echo "Using C++ compiler to compile PETSc";\
        fi
	-@echo "-----------------------------------------"
	-@echo "Using C/C++ linker: ${PCC_LINKER}"
	-@echo "Using C/C++ flags: ${PCC_LINKER_FLAGS}"
	-@if [ "${FC}" != "" ]; then \
	   echo "Using Fortran linker: ${FC_LINKER}";\
	   echo "Using Fortran flags: ${FC_LINKER_FLAGS}";\
         fi
	-@echo "-----------------------------------------"
	-@echo "Using system modules: ${LOADEDMODULES}"
	-@if [ "${MPI_IS_MPIUNI}" = "1" ]; then \
           echo Using mpi.h: mpiuni; \
        else \
           TESTDIR=`mktemp -q -d -t petscmpi-XXXXXXXX` && \
           echo '#include <mpi.h>' > $${TESTDIR}/mpitest.c && \
           BUF=`${CPP} ${PETSC_CPPFLAGS} ${PETSC_CC_INCLUDES} $${TESTDIR}/mpitest.c |grep 'mpi\.h' | ( head -1 ; cat > /dev/null )` && \
           echo Using mpi.h: $${BUF}; ${RM} -rf $${TESTDIR}; \
        fi
	-@echo "-----------------------------------------"
	-@echo "Using libraries: ${PETSC_LIB}"
	-@echo "------------------------------------------"
	-@echo "Using mpiexec: ${MPIEXEC}"
	-@echo "------------------------------------------"
	-@echo "Using MAKE: ${MAKE}"
	-@echo "Default MAKEFLAGS: MAKE_NP:${MAKE_NP} MAKE_LOAD:${MAKE_LOAD} MAKEFLAGS:${MAKEFLAGS}"
	-@echo "=========================================="

#
# Build MatLab binaries
#
matlabbin:
	-@if [ "${MATLAB_MEX}" != "" -a "${MATLAB_SOCKET}" != "" -a "${PETSC_SCALAR}" = "real" -a "${PETSC_PRECISION}" = "double" ]; then \
          echo "BEGINNING TO COMPILE MATLAB INTERFACE"; \
            if [ ! -d "${PETSC_DIR}/${PETSC_ARCH}/lib/petsc" ] ; then ${MKDIR}  ${PETSC_DIR}/${PETSC_ARCH}/lib/petsc; fi; \
            if [ ! -d "${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/matlab" ] ; then ${MKDIR}  ${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/matlab; fi; \
            cd src/sys/classes/viewer/impls/socket/matlab && ${OMAKE_SELF} matlabcodes PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR}; \
            echo "========================================="; \
        fi
#
# Builds PETSc check examples for a given architecture
#
RUN_TEST = ${OMAKE_SELF} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} DIFF=${PETSC_DIR}/lib/petsc/bin/petscdiff

check_install: check
check:
	-@echo "Running check examples to verify correct installation"
	-@echo "Using PETSC_DIR=${PETSC_DIR} and PETSC_ARCH=${PETSC_ARCH}"
	@if [ "${PETSC_WITH_BATCH}" != "" ]; then \
           echo "Running with batch filesystem, cannot run make check"; \
        elif [ "${MPIEXEC}" = "/bin/false" ]; then \
           echo "*mpiexec not found*. cannot run make check"; \
        else \
          ${RM} -f check_error;\
          ${RUN_TEST} PETSC_OPTIONS="${PETSC_OPTIONS} ${PETSC_TEST_OPTIONS}" PATH="${PETSC_DIR}/${PETSC_ARCH}/lib:${PATH}" check_build 2>&1 | tee ./${PETSC_ARCH}/lib/petsc/conf/check.log; \
          if [ -f check_error ]; then \
            echo "Error while running make check"; \
            ${RM} -f check_error;\
            exit 1; \
          fi; \
          ${RM} -f check_error;\
        fi;

check_build:
	+@cd src/snes/tutorials >/dev/null; ${RUN_TEST} clean-legacy
	+@cd src/snes/tutorials >/dev/null; ${RUN_TEST} testex19
	+@if [ ! "${MPI_IS_MPIUNI}" ]; then cd src/snes/tutorials >/dev/null; ${RUN_TEST} testex19_mpi; fi
	+@if [ "${HYPRE_LIB}" != "" ] && [ "${PETSC_SCALAR}" = "real" ]; then \
          if [ "${CUDA_LIB}" != "" ]; then HYPRE_TEST=runex19_hypre_cuda; \
          elif [ "${HIP_LIB}" != "" ]; then HYPRE_TEST=runex19_hypre_hip; \
          else HYPRE_TEST=runex19_hypre; fi; \
          cd src/snes/tutorials >/dev/null; ${RUN_TEST} $${HYPRE_TEST}; \
        fi;
	+@if [ "${CUDA_LIB}" != "" ]; then \
          cd src/snes/tutorials >/dev/null; ${RUN_TEST} runex19_cuda; \
        fi;
	+@if [ "${MPI_IS_MPIUNI}" = "" ]; then \
          cd src/snes/tutorials >/dev/null; \
          if [ "${KOKKOS_KERNELS_LIB}" != "" ]  &&  [ "${PETSC_SCALAR}" = "real" ] && [ "${PETSC_PRECISION}" = "double" ]; then \
            ${RUN_TEST} runex3k_kokkos; \
          fi;\
          if [ "${MUMPS_LIB}" != "" ]; then \
             ${RUN_TEST} runex19_fieldsplit_mumps; \
          fi;\
          if [ "${SUITESPARSE_LIB}" != "" ]; then \
             ${RUN_TEST} runex19_suitesparse; \
          fi;\
          if [ "${SUPERLU_DIST_LIB}" != "" ]; then \
            ${RUN_TEST} runex19_superlu_dist; \
          fi;\
          if ( [ "${ML_LIB}" != "" ] ||  [ "${TRILINOS_LIB}" != "" ] ); then \
            ${RUN_TEST} runex19_ml; \
          fi; \
	  ${RUN_TEST} clean-legacy; \
          cd - > /dev/null; \
          if ( [ "${AMREX_LIB}" != "" ] && [ "${CUDA_LIB}" = "" ] ); then \
            echo "Running amrex test example to verify correct installation";\
            echo "Using PETSC_DIR=${PETSC_DIR} and PETSC_ARCH=${PETSC_ARCH}";\
            cd src/ksp/ksp/tutorials/amrex >/dev/null;\
            ${RUN_TEST} clean-legacy; \
            ${RUN_TEST} testamrex; \
            ${RUN_TEST} clean-legacy; \
            cd - > /dev/null; \
          fi;\
        fi;
	+@if [ "${HDF5_LIB}" != "" ]; then \
          cd src/vec/vec/tests >/dev/null;\
          ${RUN_TEST} clean-legacy; \
          ${RUN_TEST} runex47; \
          ${RUN_TEST} clean-legacy; \
         fi;
	+@if [ "${MPI4PY}" = "yes" ]; then \
           cd src/sys/tests >/dev/null; \
           ${RUN_TEST} clean-legacy; \
           ${RUN_TEST} testex55; \
           ${RUN_TEST} clean-legacy; \
         fi;
	+@if [ "${PETSC4PY}" = "yes" ]; then \
           cd src/ksp/ksp/tutorials >/dev/null; \
           ${RUN_TEST} clean-legacy; \
           ${RUN_TEST} testex100; \
           ${RUN_TEST} clean-legacy; \
         fi;
	+@grep -E "^#define PETSC_HAVE_FORTRAN 1" ${PETSCCONF_H} | tee .ftn.log > /dev/null; \
         if test -s .ftn.log; then \
           cd src/snes/tutorials >/dev/null; \
           ${RUN_TEST} clean-legacy; \
           ${RUN_TEST} testex5f; \
           ${RUN_TEST} clean-legacy; \
         fi; ${RM} .ftn.log;
	+@grep -E "^#define PETSC_HAVE_MATLAB 1" ${PETSCCONF_H} | tee .ftn.log > /dev/null; \
         if test -s .ftn.log; then \
           cd src/vec/vec/tutorials >/dev/null; \
           ${RUN_TEST} clean-legacy; \
           ${RUN_TEST} testex31; \
           ${RUN_TEST} clean-legacy; \
          fi; ${RM} .ftn.log;
	-@echo "Completed test examples"

check_usermakefile:
	-@echo "Testing compile with user makefile"
	-@echo "Using PETSC_DIR=${PETSC_DIR} and PETSC_ARCH=${PETSC_ARCH}"
	@cd src/snes/tutorials; ${RUN_TEST} clean-legacy
	@cd src/snes/tutorials; ${OMAKE} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} -f ${PETSC_DIR}/share/petsc/Makefile.user ex19
	@grep -E "^#define PETSC_HAVE_FORTRAN 1" ${PETSCCONF_H} | tee .ftn.log > /dev/null; \
         if test -s .ftn.log; then \
          cd src/snes/tutorials; ${OMAKE} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} -f ${PETSC_DIR}/share/petsc/Makefile.user ex5f; \
         fi; ${RM} .ftn.log;
	@cd src/snes/tutorials; ${RUN_TEST} clean-legacy
	-@echo "Completed compile with user makefile"

checkgitclean:
	@if ! git diff --quiet; then \
           echo "The repository has uncommitted files, cannot run checkclangformat" ;\
           git status -s --untracked-files=no ;\
           false;\
        fi;

checkclangformatversion:
	@version=`clang-format --version | cut -d" " -f3 | cut -d"." -f 1` ;\
         if [ "$$version" == "version" ]; then version=`clang-format --version | cut -d" " -f4 | cut -d"." -f 1`; fi;\
         if [ $$version != 15 ]; then echo "Require clang-format version 15! Currently used clang-format version is $$version" ;false ; fi

# Check that all the source code in the repository satisfies the .clang_format
checkclangformat: checkclangformatversion checkgitclean clangformat
	@if ! git diff --quiet; then \
          printf "The current commit has source code formatting problems\n" ;\
          if [ -z "${CI_PIPELINE_ID}"  ]; then \
            printf "Please run 'git diff' to check\n"; \
            git diff --stat; \
          else \
            git diff --patch-with-stat >  ${PETSC_ARCH}/lib/petsc/conf/checkclangformat.patch; \
            git diff --patch-with-stat --color=always | head -1000; \
            if [ `wc -l < ${PETSC_ARCH}/lib/petsc/conf/checkclangformat.patch` -gt 1000 ]; then \
              printf "The diff has been trimmed, check ${PETSC_ARCH}/lib/petsc/conf/checkclangformat.patch (in CI artifacts) for all changes\n"; \
            fi;\
          fi;\
          false;\
        fi;

# Compare ABI/API of two versions of PETSc library with the old one defined by PETSC_{DIR,ARCH}_ABI_OLD
abitest:
	@if [ "${PETSC_DIR_ABI_OLD}" = "" ] || [ "${PETSC_ARCH_ABI_OLD}" = "" ]; \
		then printf "You must set environment variables PETSC_DIR_ABI_OLD and PETSC_ARCH_ABI_OLD to run abitest\n"; \
		exit 1; \
	fi;
	-@echo "Comparing ABI/API of the following two PETSc versions (you must have already configured and built them using GCC and with -g):"
	-@echo "========================================================================================="
	-@echo "    Old: PETSC_DIR_ABI_OLD  = ${PETSC_DIR_ABI_OLD}"
	-@echo "         PETSC_ARCH_ABI_OLD = ${PETSC_ARCH_ABI_OLD}"
	-@pushd ${PETSC_DIR_ABI_OLD} >> /dev/null ; echo "         Branch             = "`git rev-parse --abbrev-ref HEAD`
	-@echo "    New: PETSC_DIR          = ${PETSC_DIR}"
	-@echo "         PETSC_ARCH         = ${PETSC_ARCH}"
	-@echo "         Branch             = "`git rev-parse --abbrev-ref HEAD`
	-@echo "========================================================================================="
	-@$(PYTHON)	${PETSC_DIR}/lib/petsc/bin/maint/abicheck.py -old_dir ${PETSC_DIR_ABI_OLD} -old_arch ${PETSC_ARCH_ABI_OLD} -new_dir ${PETSC_DIR} -new_arch ${PETSC_ARCH} -report_format html

# Compare ABI/API of current PETSC_ARCH/PETSC_DIR with a previous branch
abitestcomplete:
	-@if [[ -f "${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/conf/configure.log" ]]; then \
          OPTIONS=`grep -h -m 1 "Configure Options: " ${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/conf/configure.log  | sed "s!Configure Options: --configModules=PETSc.Configure --optionsModule=config.compilerOptions!!g"` ;\
echo $${OPTIONS} ;\
        fi ; \
        if [[ "${PETSC_DIR_ABI_OLD}" != "" ]]; then \
          PETSC_DIR_OLD=${PETSC_DIR_ABI_OLD}; \
        else \
          PETSC_DIR_OLD=${PETSC_DIR}/../petsc-abi; \
        fi ; \
        echo "=================================================================================================" ;\
        echo "Doing ABI/API comparison between" ${branch} " and " `git rev-parse --abbrev-ref HEAD` "using " $${OPTIONS} ;\
        echo "=================================================================================================" ;\
        if [[ ! -d $${PETSC_DIR_OLD} ]]; then \
          git clone ${PETSC_DIR} $${PETSC_DIR_OLD} ; \
        else \
          cd $${PETSC_DIR_OLD} ; \
          git pull ; \
        fi ; \
        cd $${PETSC_DIR_OLD} ; \
        git checkout ${branch} ; \
        PETSC_DIR=`pwd` PETSC_ARCH=arch-branch-`git rev-parse ${branch}` ./configure $${OPTIONS} ; \
        PETSC_DIR=`pwd` PETSC_ARCH=arch-branch-`git rev-parse ${branch}` make all test ; \
        cd ${PETSC_DIR} ; \
        ./configure $${OPTIONS}; \
        make all test ; \
        PETSC_DIR_ABI_OLD=$${PETSC_DIR_OLD} PETSC_ARCH_ABI_OLD=arch-branch-`git rev-parse ${branch}` make abitest

# Deletes PETSc libraries
deletelibs:
	-${RM} -rf ${PETSC_LIB_DIR}/libpetsc*.*
deletemods:
	-${RM} -f ${PETSC_DIR}/${PETSC_ARCH}/include/petsc*.mod

allclean:
	-@${OMAKE} -f gmakefile clean

clean:: allclean

distclean: chk_petscdir
	@if [ -f ${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/conf/reconfigure-${PETSC_ARCH}.py ]; then \
	  echo "*** Preserving ${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/conf/reconfigure-${PETSC_ARCH}.py in ${PETSC_DIR} ***"; \
          mv -f ${PETSC_DIR}/${PETSC_ARCH}/lib/petsc/conf/reconfigure-${PETSC_ARCH}.py ${PETSC_DIR}/; fi
	@echo "*** Deleting all build files in ${PETSC_DIR}/${PETSC_ARCH} ***"
	-${RM} -rf ${PETSC_DIR}/${PETSC_ARCH}/


#
reconfigure: allclean
	@unset MAKEFLAGS && ${PYTHON} ${PETSC_ARCH}/lib/petsc/conf/reconfigure-${PETSC_ARCH}.py

install:
	@${PYTHON} ./config/install.py -destDir=${DESTDIR}
	+${OMAKE_SELF} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} PETSC_INSTALL=$@ install-builtafterpetsc

# A smaller install with fewer extras
install-lib:
	@${PYTHON} ./config/install.py -destDir=${DESTDIR} -no-examples
	+${OMAKE_SELF} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} PETSC_INSTALL=$@ install-builtafterpetsc

install-builtafterpetsc:
	+${OMAKE_SELF} PETSC_ARCH=${PETSC_ARCH} PETSC_DIR=${PETSC_DIR} PETSC_INSTALL=${PETSC_INSTALL} petsc4py-install libmesh-install mfem-install slepc-install hpddm-install amrex-install bamg-install

mpistreams:
	+@cd src/benchmarks/streams; ${OMAKE_SELF} PATH="${PETSC_DIR}/${PETSC_ARCH}/lib:${PATH}" PETSC_DIR=${PETSC_DIR} PETSC_ARCH=${PETSC_ARCH} mpistreams

mpistream:
	+@cd src/benchmarks/streams; ${OMAKE_SELF} PATH="${PETSC_DIR}/${PETSC_ARCH}/lib:${PATH}" PETSC_DIR=${PETSC_DIR} PETSC_ARCH=${PETSC_ARCH} mpistream

openmpstreams:
	+@cd src/benchmarks/streams; ${OMAKE_SELF} PATH="${PETSC_DIR}/${PETSC_ARCH}/lib:${PATH}" PETSC_DIR=${PETSC_DIR} PETSC_ARCH=${PETSC_ARCH} openmpstreams

openmpstream:
	+@cd src/benchmarks/streams; ${OMAKE_SELF} PATH="${PETSC_DIR}/${PETSC_ARCH}/lib:${PATH}" PETSC_DIR=${PETSC_DIR} PETSC_ARCH=${PETSC_ARCH} openmpstream

# for legacy reasons
stream: mpistream

streams: mpistreams

# ------------------------------------------------------------------
#
# All remaining actions are intended for PETSc developers only.
# PETSc users should not generally need to use these commands.
#
#  See the users manual for how the tags files may be used from Emacs and Vi/Vim
#
alletags:
	-@${PYTHON} lib/petsc/bin/maint/generateetags.py
	-@find config -type f -name "*.py" |grep -v SCCS | xargs etags -o TAGS_PYTHON

# obtain gtags from https://www.gnu.org/software/global/
allgtags:
	-@find ${PETSC_DIR}/include ${PETSC_DIR}/src -regex '\(.*makefile\|.*\.\(cc\|hh\|cpp\|cxx\|C\|hpp\|c\|h\|cu\|m\)$$\)' | grep -v ftn-auto  | gtags -f -

allfortranstubs:
	-@${RM} -rf ${PETSC_ARCH}/include/petsc/finclude/ftn-auto/*-tmpdir
	@${PYTHON} lib/petsc/bin/maint/generatefortranstubs.py ${BFORT}  ${VERBOSE}
	-@${PYTHON} lib/petsc/bin/maint/generatefortranstubs.py -merge  ${VERBOSE}
	-@${RM} -rf ${PETSC_ARCH}/include/petsc/finclude/ftn-auto/*-tmpdir
deletefortranstubs:
	-@find . -type d -name ftn-auto | xargs rm -rf

# Build just manual pages + prerequisites
# Also builds citations
hloc=include/petsc/private
allmanpages: chk_loc deletemanualpages
	-echo " /* SUBMANSEC = PetscH */ " > ${hloc}/generated_khash.h
	-sed -e 's?<T>?I?g' -e 's?<t>?i?g' -e 's?<KeyType>?PetscInt?g' ${hloc}/hashset.txt >> ${hloc}/generated_khash.h
	-sed -e 's?<T>?IJ?g' -e 's?<t>?ij?g' -e 's?<KeyType>?struct {PetscInt i, j;}?g' ${hloc}/hashset.txt >> ${hloc}/generated_khash.h
	-sed -e 's?<T>?I?g' -e 's?<t>?i?g' -e 's?<KeyType>?PetscInt?g'  -e 's?<ValType>?PetscInt?g' ${hloc}/hashmap.txt >> ${hloc}/generated_khash.h
	-sed -e 's?<T>?IJ?g' -e 's?<t>?ij?g' -e 's?<KeyType>?struct {PetscInt i, j;}?g' -e 's?<ValType>?PetscInt?g' ${hloc}/hashmap.txt >> ${hloc}/generated_khash.h
	-sed -e 's?<T>?IJ?g' -e 's?<t>?ij?g' -e 's?<KeyType>?struct {PetscInt i, j;}?g' -e 's?<ValType>?PetscScalar?g' ${hloc}/hashmap.txt >> ${hloc}/generated_khash.h
	-sed -e 's?<T>?IV?g' -e 's?<t>?iv?g' -e 's?<KeyType>?PetscInt?g'  -e 's?<ValType>?PetscScalar?g' ${hloc}/hashmap.txt >> ${hloc}/generated_khash.h
	-sed -e 's?<T>?Obj?g' -e 's?<t>?obj?g' -e 's?<KeyType>?PetscInt64?g'  -e 's?<ValType>?PetscObject?g' ${hloc}/hashmap.txt >> ${hloc}/generated_khash.h
	-${RM} ${PETSC_DIR}/${PETSC_ARCH}/manualpages.err
	-${OMAKE_SELF} ACTION=manualpages tree_src LOC=${LOC}
	-@sed -e s%man+../%man+manualpages/% ${LOC}/manualpages/manualpages.cit > ${LOC}/manualpages/htmlmap
	-@cat ${PETSC_DIR}/doc/classic/mpi.www.index >> ${LOC}/manualpages/htmlmap
	cat ${PETSC_DIR}/${PETSC_ARCH}/manualpages.err
	a=`cat ${PETSC_DIR}/${PETSC_ARCH}/manualpages.err | wc -l`; test ! $$a -gt 0

# Build just manual examples + prerequisites
allmanexamples: chk_loc allmanpages
	-${OMAKE_SELF} ACTION=manexamples tree_basic LOC=${LOC}

# Build all classic docs except html sources
alldoc1: chk_loc allmanpages allmanexamples
	-${OMAKE_SELF} manimplementations LOC=${LOC}
	-${PYTHON} lib/petsc/bin/maint/wwwindex.py ${PETSC_DIR} ${LOC}

# Builds .html versions of the source
# html overwrites some stuff - hence this is done later.
alldoc2: chk_loc allmanpages
	-${OMAKE_SELF} ACTION=html PETSC_DIR=${PETSC_DIR} alltree LOC=${LOC}

# A version which presumes allmanpages has already been run
alldoc_post: chk_loc
	-${OMAKE_SELF} ACTION=html PETSC_DIR=${PETSC_DIR} alltree LOC=${LOC}

alldocclean: deletemanualpages allcleanhtml

# Deletes man pages (.md version)
deletemanualpages: chk_loc
	-@if [ -d ${LOC} -a -d ${LOC}/manualpages ]; then \
          find ${LOC}/manualpages -type f -name "*.md" -exec ${RM} {} \; ;\
          ${RM} ${LOC}/manualpages/manualpages.cit ;\
        fi

allcleanhtml:
	-${OMAKE_SELF} ACTION=cleanhtml PETSC_DIR=${PETSC_DIR} alltree

###########################################################
# targets to build distribution and update docs
###########################################################

# Creates ${HOME}/petsc.tar.gz [and petsc-with-docs.tar.gz]
dist:
	${PETSC_DIR}/lib/petsc/bin/maint/builddist ${PETSC_DIR} main

###########################################################
#
#  See script for details
#
gcov:
	-output_file_base_name=${PETSC_ARCH}-gcovr-report.json; \
	petsc_arch_dir=${PETSC_DIR}/${PETSC_ARCH}; \
	pushd $${petsc_arch_dir}/obj && \
	gcovr --json --output $${petsc_arch_dir}/$${output_file_base_name} --exclude '.*/ftn-auto/.*' --exclude-lines-by-pattern '^\s*SETERR.*' --exclude-throw-branches --exclude-unreachable-branches -j 4 --gcov-executable ${PETSC_COVERAGE_EXEC} --root ${PETSC_DIR} . ${PETSC_GCOV_OPTIONS} && \
	${RM} -f $${petsc_arch_dir}/$${output_file_base_name}.tar.gz && \
	tar -czf $${petsc_arch_dir}/$${output_file_base_name}.tar.gz -C $${petsc_arch_dir} ./$${output_file_base_name} && \
	${RM} $${petsc_arch_dir}/$${output_file_base_name}; \
	popd

mergegcov:
	$(PYTHON) ${PETSC_DIR}/lib/petsc/bin/maint/gcov.py --merge-branch `lib/petsc/bin/maint/check-merge-branch.sh` --html --xml ${PETSC_GCOV_OPTIONS}

#
# -------------------------------------------------------------------------------
#
# Some macros to check if the fortran interface is up-to-date.
#
countfortranfunctions:
	-@cd ${PETSC_DIR}/src/fortran; grep -E '^void' custom/*.c auto/*.c | \
	cut -d'(' -f1 | tr -s  ' ' | cut -d' ' -f2 | uniq | grep -E -v "(^$$|Petsc)" | \
	sed "s/_$$//" | sort > /tmp/countfortranfunctions

countcfunctions:
	-@grep PETSC_EXTERN ${PETSC_DIR}/include/*.h  | grep "(" | tr -s ' ' | \
	cut -d'(' -f1 | cut -d' ' -f3 | grep -v "\*" | tr -s '\012' |  \
	tr 'A-Z' 'a-z' |  sort | uniq > /tmp/countcfunctions

difffortranfunctions: countfortranfunctions countcfunctions
	-@echo -------------- Functions missing in the fortran interface ---------------------
	-@${DIFF} /tmp/countcfunctions /tmp/countfortranfunctions | grep "^<" | cut -d' ' -f2
	-@echo ----------------- Functions missing in the C interface ------------------------
	-@${DIFF} /tmp/countcfunctions /tmp/countfortranfunctions | grep "^>" | cut -d' ' -f2
	-@${RM}  /tmp/countcfunctions /tmp/countfortranfunctions

checkbadfortranstubs:
	-@echo "========================================="
	-@echo "Functions with MPI_Comm as an Argument"
	-@echo "========================================="
	-@cd ${PETSC_DIR}/src/fortran/auto; grep '^void' *.c | grep 'MPI_Comm' | \
	tr -s ' ' | tr -s ':' ' ' |cut -d'(' -f1 | cut -d' ' -f1,3
	-@echo "========================================="
	-@echo "Functions with a String as an Argument"
	-@echo "========================================="
	-@cd ${PETSC_DIR}/src/fortran/auto; grep '^void' *.c | grep 'char \*' | \
	tr -s ' ' | tr -s ':' ' ' |cut -d'(' -f1 | cut -d' ' -f1,3
	-@echo "========================================="
	-@echo "Functions with Pointers to PETSc Objects as Argument"
	-@echo "========================================="
	-@cd ${PETSC_DIR}/src/fortran/auto; \
	_p_OBJ=`grep _p_ ${PETSC_DIR}/include/*.h | tr -s ' ' | \
	cut -d' ' -f 3 | tr -s '\012' | grep -v '{' | cut -d'*' -f1 | \
	sed "s/_p_//g" | tr -s '\012 ' ' *|' ` ; \
	for OBJ in $$_p_OBJ; do \
	grep "$$OBJ \*" *.c | tr -s ' ' | tr -s ':' ' ' | \
	cut -d'(' -f1 | cut -d' ' -f1,3; \
	done

checkpackagetests:
	-@echo "Missing package tests"
	-@cat config/examples/*.py > configexamples; pushd config/BuildSystem/config/packages/; packages=`ls *.py | sed "s/\\.py//g"`;popd; for i in $${packages}; do j=`echo $${i} | tr '[:upper:]' '[:lower:]'`; printf $${j} ; grep -E "(with-$${j}|download-$${j})" configexamples | grep -v "=0" | wc -l ; done
	-@echo "Missing download package tests"
	-@cat config/examples/*.py > configexamples; pushd config/BuildSystem/config/packages/; packages=`grep -l "download " *.py  | sed "s/\\.py//g"`;popd; for i in $${packages}; do j=`echo $${i} | tr '[:upper:]' '[:lower:]'`; printf $${j} ; grep -E "(download-$${j})" configexamples | grep -v "=0" | wc -l ; done

.PHONY: info info_h all deletelibs allclean update \
        alletags etags etags_complete etags_noexamples etags_makefiles etags_examples etags_fexamples alldoc allmanpages \
        allcleanhtml  countfortranfunctions \
        start_configure configure_petsc configure_clean matlabbin install
