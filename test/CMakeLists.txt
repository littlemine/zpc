# tuple
add_executable(tupletest tuple.cpp)
target_link_libraries(tupletest PRIVATE zpc)

add_test(ZsTuple tupletest)
add_dependencies(zensim tupletest)

# map
add_executable(maptest map.cpp)
target_link_libraries(maptest PRIVATE zpc)

add_test(ZsRBTreeMap maptest)
add_dependencies(zensim maptest)

# helperlexer
add_executable(helperlexer help_parser.cpp)
target_link_libraries(helperlexer PRIVATE zpc)

add_test(HelperLexer helperlexer)
add_dependencies(zensim helperlexer)

# nd vec
add_executable(ndarraytest ndvec.cpp)
target_link_libraries(ndarraytest PRIVATE zpc)

add_test(ZsNdArray ndarraytest)
add_dependencies(zensim ndarraytest)

# parallel primitives
add_executable(paraprim parallel_primitives.cpp)
target_link_libraries(paraprim PRIVATE zpc)

add_test(ZsParallelPrimitive paraprim)
add_dependencies(zensim paraprim)

# async runtime
add_executable(asyncruntime async_runtime.cpp)
target_link_libraries(asyncruntime PRIVATE zpc)
target_compile_features(asyncruntime PRIVATE cxx_std_20)

add_test(ZsAsyncRuntime asyncruntime)
add_dependencies(zensim asyncruntime)

add_executable(asyncatomicbenchmark async_atomic_benchmark.cpp)
target_link_libraries(asyncatomicbenchmark PRIVATE zpc)
target_compile_features(asyncatomicbenchmark PRIVATE cxx_std_20)

add_dependencies(zensim asyncatomicbenchmark)

add_executable(smartpointertest smart_pointers.cpp)
target_link_libraries(smartpointertest PRIVATE zpcbase)

add_test(ZsSmartPointers smartpointertest)
add_dependencies(zensim smartpointertest)

# async foundation
add_executable(asyncfoundation async_foundation.cpp)
target_link_libraries(asyncfoundation PRIVATE zpc)

add_test(ZsAsyncFoundation asyncfoundation)
add_dependencies(zensim asyncfoundation)

# async backend contract
add_executable(asyncbackendcontract async_backend_contract.cpp)
target_link_libraries(asyncbackendcontract PRIVATE zpc)

add_test(ZsAsyncBackendContract asyncbackendcontract)
add_dependencies(zensim asyncbackendcontract)

# async backend profile
add_executable(asyncbackendprofile async_backend_profile.cpp)
target_link_libraries(asyncbackendprofile PRIVATE zpc_async_runtime_core)

add_test(ZsAsyncBackendProfile asyncbackendprofile)
add_dependencies(zensim asyncbackendprofile)

# async native queue adapter
add_executable(asyncnativequeue async_native_queue_adapter.cpp)
target_link_libraries(asyncnativequeue PRIVATE zpc)

add_test(ZsAsyncNativeQueue asyncnativequeue)
add_dependencies(zensim asyncnativequeue)

# async runtime abi
add_executable(asyncruntimeabi async_runtime_abi.cpp)
target_link_libraries(asyncruntimeabi PRIVATE zpc)

add_test(ZsAsyncRuntimeAbi asyncruntimeabi)
add_dependencies(zensim asyncruntimeabi)

add_executable(asyncruntimeabidemo async_runtime_abi_demo.cpp)
target_link_libraries(asyncruntimeabidemo PRIVATE zpc)

add_dependencies(zensim asyncruntimeabidemo)

# validation schema
add_executable(validationschema validation_schema.cpp)
target_link_libraries(validationschema PRIVATE zpc_validation_core)

add_test(ZsValidationSchema validationschema)
add_dependencies(zensim validationschema)

# validation format
add_executable(validationformat validation_format.cpp)
target_link_libraries(validationformat PRIVATE zpc_validation_core)

add_test(ZsValidationFormat validationformat)
add_dependencies(zensim validationformat)

# validation compare
add_executable(validationcompare validation_compare.cpp)
target_link_libraries(validationcompare PRIVATE zpc_validation_core)

add_test(ZsValidationCompare validationcompare)
add_dependencies(zensim validationcompare)

# validation persistence
add_executable(validationpersistence validation_persistence.cpp)
target_link_libraries(validationpersistence PRIVATE zpc_validation_core)

add_test(ZsValidationPersistence validationpersistence)
add_dependencies(zensim validationpersistence)

# binary search
add_executable(binarysearchtest binary_search.cpp)
target_link_libraries(binarysearchtest PRIVATE zpc)

add_test(ZsBinarySearch binarysearchtest)
add_dependencies(zensim binarysearchtest)

# sycl backend
if(ZS_ENABLE_SYCL_ONEAPI OR ZS_ENABLE_SYCL_ACPP)
    #
    add_executable(sycltests sycl/main.cpp)
    target_link_libraries(sycltests PRIVATE zpc zpc_sycl_deps)

    add_test(ZsSyclTests sycltests)
    add_dependencies(zensim sycltests)
endif()
#

# cuda backend
if(ZS_ENABLE_CUDA)
    #
    add_executable(cutests cuda/main.cu)
    target_link_libraries(cutests PRIVATE zpc)

    add_test(ZsCudaTests cutests)
    add_dependencies(zensim cutests)
    #
    add_executable(cucontainer cuda/basic.cu)
    target_link_libraries(cucontainer PRIVATE zpc)

    add_test(ZsCudaBasic cucontainer)
    add_dependencies(zensim cucontainer)
endif(ZS_ENABLE_CUDA)

# musa backend
if(ZS_ENABLE_MUSA)
    #
    zs_add_musa_executable(mutests SOURCES musa/basic.cpp DEVICE_SOURCES musa/basic.cpp)
    # add_executable(mutests musa/basic.cpp)
    target_link_libraries(mutests PRIVATE zpc)

    add_test(ZsMusaBasic mutests)
    add_dependencies(zensim mutests)
endif(ZS_ENABLE_MUSA)
