


#include <typeinfo>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../../../../bsd/libc++/dist/libcxxrt/src/abi_namespace.h"

using std::type_info;

#if 0
type_info::~type_info() {}
#endif
bool type_info::operator==(const type_info &other) const noexcept
{
	return __type_name == other.__type_name;
}




ABI_NAMESPACE::__fundamental_type_info::~__fundamental_type_info() {}
ABI_NAMESPACE::__array_type_info::~__array_type_info() {}
ABI_NAMESPACE::__function_type_info::~__function_type_info() {}
ABI_NAMESPACE::__enum_type_info::~__enum_type_info() {}
ABI_NAMESPACE::__class_type_info::~__class_type_info() {}
ABI_NAMESPACE::__si_class_type_info::~__si_class_type_info() {}
ABI_NAMESPACE::__vmi_class_type_info::~__vmi_class_type_info() {}
ABI_NAMESPACE::__pbase_type_info::~__pbase_type_info() {}
ABI_NAMESPACE::__pointer_type_info::~__pointer_type_info() {}
ABI_NAMESPACE::__pointer_to_member_type_info::~__pointer_to_member_type_info() {}




extern "C" void __cxa_throw(void *thrown_exception,
                            std::type_info *tinfo,
                            void(*dest)(void*))
{

}

#include "../../clang/lib/Headers/unwind.h"


extern "C" _Unwind_Reason_Code
__gxx_personality_v0(int version, _Unwind_Action actions, _Unwind_Exception_Class exceptionClass, struct _Unwind_Exception *exceptionObject, struct _Unwind_Context *context)
{
    return _URC_NO_REASON;
}


typedef void (*terminate_handler)();
typedef void (*unexpected_handler)();

struct __cxa_exception
{
#if __LP64__
	/**
	 * Reference count.  Used to support the C++11 exception_ptr class.  This
	 * is prepended to the structure in 64-bit mode and squeezed in to the
	 * padding left before the 64-bit aligned _Unwind_Exception at the end in
	 * 32-bit mode.
	 *
	 * Note that it is safe to extend this structure at the beginning, rather
	 * than the end, because the public API for creating it returns the address
	 * of the end (where the exception object can be stored).
	 */
	uintptr_t referenceCount;
#endif
	/** Type info for the thrown object. */
	std::type_info *exceptionType;
	/** Destructor for the object, if one exists. */
	void (*exceptionDestructor) (void *); 
	/** Handler called when an exception specification is violated. */
	unexpected_handler unexpectedHandler;
	/** Hander called to terminate. */
	terminate_handler terminateHandler;
	/**
	 * Next exception in the list.  If an exception is thrown inside a catch
	 * block and caught in a nested catch, this points to the exception that
	 * will be handled after the inner catch block completes.
	 */
	__cxa_exception *nextException;
	/**
	 * The number of handlers that currently have references to this
	 * exception.  The top (non-sign) bit of this is used as a flag to indicate
	 * that the exception is being rethrown, so should not be deleted when its
	 * handler count reaches 0 (which it doesn't with the top bit set).
	 */
	int handlerCount;
#if defined(__arm__) && !defined(__ARM_DWARF_EH__)
	/**
	 * The ARM EH ABI requires the unwind library to keep track of exceptions
	 * during cleanups.  These support nesting, so we need to keep a list of
	 * them.
	 */
	_Unwind_Exception *nextCleanup;
	/**
	 * The number of cleanups that are currently being run on this exception. 
	 */
	int cleanupCount;
#endif
	/**
	 * The selector value to be returned when installing the catch handler.
	 * Used at the call site to determine which catch() block should execute.
	 * This is found in phase 1 of unwinding then installed in phase 2.
	 */
	int handlerSwitchValue;
	/**
	 * The action record for the catch.  This is cached during phase 1
	 * unwinding.
	 */
	const char *actionRecord;
	/**
	 * Pointer to the language-specific data area (LSDA) for the handler
	 * frame.  This is unused in this implementation, but set for ABI
	 * compatibility in case we want to mix code in very weird ways.
	 */
	const char *languageSpecificData;
	/** The cached landing pad for the catch handler.*/
	void *catchTemp;
	/**
	 * The pointer that will be returned as the pointer to the object.  When
	 * throwing a class and catching a virtual superclass (for example), we
	 * need to adjust the thrown pointer to make it all work correctly.
	 */
	void *adjustedPtr;
#if !__LP64__
	/**
	 * Reference count.  Used to support the C++11 exception_ptr class.  This
	 * is prepended to the structure in 64-bit mode and squeezed in to the
	 * padding left before the 64-bit aligned _Unwind_Exception at the end in
	 * 32-bit mode.
	 *
	 * Note that it is safe to extend this structure at the beginning, rather
	 * than the end, because the public API for creating it returns the address
	 * of the end (where the exception object can be stored) 
	 */
	uintptr_t referenceCount;
#endif
	/** The language-agnostic part of the exception header. */
	_Unwind_Exception unwindHeader;
};

struct __cxa_eh_globals
{
	/**
	 * A linked list of exceptions that are currently caught.  There may be
	 * several of these in nested catch() blocks.
	 */
	__cxa_exception *caughtExceptions;
	/**
	 * The number of uncaught exceptions.
	 */
	unsigned int uncaughtExceptions;
};

extern "C" __cxa_eh_globals*
__cxa_get_globals() noexcept
{
    return NULL;
}

extern "C" void *
__cxa_allocate_exception(size_t thrown_size)
{
#if 0
  void *ret;

  thrown_size += sizeof (__cxa_refcounted_exception);
  ret = malloc (thrown_size);

  if (!ret)
    ret = emergency_pool.allocate (thrown_size);

  if (!ret)
    std::terminate ();

  memset (ret, 0, sizeof (__cxa_refcounted_exception));

  return (void *)((char *)ret + sizeof (__cxa_refcounted_exception));
#endif
    return NULL;
}