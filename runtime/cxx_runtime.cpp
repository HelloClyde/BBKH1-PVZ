#include <new>
#include <cstdlib>
#include <exception>
#include "runtime.h"
static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__==8,"Review native default new alignment");

volatile uint32_t pvz_runtime_new_failure_caller;
void *operator new(std::size_t size){
    void *p=malloc(size?size:1);
    if(!p){pvz_runtime_new_failure_caller=(uint32_t)(uintptr_t)__builtin_return_address(0);throw std::bad_alloc();}
    return p;
}
void *operator new[](std::size_t size){return ::operator new(size);}
void *operator new(std::size_t size,const std::nothrow_t&) noexcept{return malloc(size?size:1);}
void *operator new[](std::size_t size,const std::nothrow_t&) noexcept{return malloc(size?size:1);}
void operator delete(void *p) noexcept{free(p);}
void operator delete[](void *p) noexcept{free(p);}
void operator delete(void *p,std::size_t) noexcept{free(p);}
void operator delete[](void *p,std::size_t) noexcept{free(p);}
extern "C" void __cxa_pure_virtual(){pvz_runtime_abort(101);}
extern "C" void __cxa_deleted_virtual(){pvz_runtime_abort(102);}
struct Destructor {void (*call)(void *);void *object;};
static Destructor destructors[512];
static unsigned destructor_count;
extern "C" int __cxa_atexit(void (*fn)(void *),void *object,void *){
    if(destructor_count>=512)return -1;
    destructors[destructor_count++]={fn,object};return 0;
}
extern "C" void __cxa_finalize(void *){while(destructor_count){auto d=destructors[--destructor_count];d.call(d.object);}}
extern "C" void pvz_cxx_finalize(){__cxa_finalize(nullptr);}
static void terminate_native() noexcept{
    int status=106; /* terminate without an active exception */
    try {
        auto current=std::current_exception();
        if(current)std::rethrow_exception(current);
    }catch(const std::bad_alloc&){status=12;}
    catch(...){status=107;}
    pvz_runtime_abort(status);
}
extern "C" void pvz_cxx_initialize(){std::set_terminate(terminate_native);}
extern "C" void *__dso_handle;
void *__dso_handle;
