/**************************************************************
* Copyright (c) 2010-2013, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for various "smart" pointer type classes.
#pragma once
#include <stdio.h>

/**
 * RAII base class for self deleting types  that use a value besides NULL for
 * empty. T Must have simple copy semantics & compare semantic for
 * nullcomapreval
 *
 */
template<typename T, T nullval, void (*freeFn)(T)> class RaiiBase
{
public:
  T val;
  RaiiBase() : val(nullval) { }
  RaiiBase(const T val) : val(val) { }
  ~RaiiBase() { Dispose();}
  operator T() const  { return val;}
  T operator=(T newval) { Dispose(); val = newval; return newval;}
  bool operator==(T cmp) const { return val == cmp;}
  T Detach() { T old = val; val = nullval; return old;} // detach without freeing
  void Dispose() { if (!isNull())
      freeFn(val); val = nullval;}
  bool IsValid() const { return !isNull();}
protected:
  bool isNull() const { return val == nullval;}
private:
  RaiiBase & operator=(RaiiBase<T, nullval, freeFn> &src); // don't want two RaiiBase freeing the same object
  RaiiBase(const RaiiBase<T, nullval, freeFn> &src); // never use this.
};


/**
 * RAII base class for self deleting pointer types  that use NULL for empty. T
 * Must have simple copy semantics & compare semantics.
 *
 * (We need a separate template class, because NULL is not allowed as a template
 * parameter.)
 */
template<typename T, void (*freeFn)(T *)> class RaiiNullBase
{
public:
  T *val;
  RaiiNullBase() : val(NULL) { }
  RaiiNullBase(T *val) : val(val) { }
  ~RaiiNullBase() { Dispose();}
  operator T*() const  { return val;}
  T * operator=(T *newval) { Dispose(); val = newval; return newval;}
  bool operator==(T *cmp) const { return val == cmp;}
  T* Detach() { T *old = val; val = NULL; return old;} // detach without freeing
  void Dispose() { if (!isNull())
      freeFn(val); val = NULL;}
  bool IsValid() const { return !isNull();}
  T * operator->()  { return val;}
  T*& ClearAndGetRef() { Dispose(); return val;} // Use when T** is needed
protected:
  bool isNull() const { return val == NULL;}
private:
  RaiiNullBase & operator=(RaiiNullBase<T, freeFn> &src); // don't want two RaiiNullBase freeing the same object
  RaiiNullBase(const RaiiNullBase<T, freeFn> &src); // never use this.
};

/**
 * RAII class for file descriptors.
 *
 */
void CloseFileDescriptor(int val);
typedef RaiiBase<int, - 1, CloseFileDescriptor> FileDescriptor;

/**
 * RAII class for FILE* handles.
 *
 */
void CloseFileHandle(FILE *val);
typedef RaiiNullBase<FILE, CloseFileHandle> FileHandle;


/**
 * RAII class for simple array allocated with new or new[]
 */
template<typename T> void FreeFree(T *val) { free(val);}
template<typename T> void FreeArray(T *val) { delete[] val;}
template<typename T> void FreeDelete(T *val) { delete val;}
template<typename T> struct Raii
{
  typedef RaiiNullBase<T, FreeArray<T> >  DeleteArray;
  typedef RaiiNullBase<T, FreeDelete<T> >  Delete;
  typedef RaiiNullBase<T, FreeFree<T> >  Free;
};


/**
 * RAII class for self deleting pointer types  that use NULL for empty, that
 * calls a delete function on a class. T Must have simple copy semantics &
 * compare semantics.
 *
 * (We need a separate template class, because NULL is not allowed as a template
 * parameter.)
 */
template<typename T, typename C, void (C::*freeFn)(T *)> class RaiiClassCall
{
public:
  T *val;
  C *myClass;
  RaiiClassCall(C *myClass) : val(NULL), myClass(myClass) { }
  RaiiClassCall(const T *val, C *myClass) : val(val), myClass(myClass) { }
  ~RaiiClassCall() { Dispose();}
  operator T*() const  { return val;}
  T * operator=(T *newval) { Dispose(); val = newval; return newval;}
  bool operator==(T *cmp) const { return val == cmp;}
  T* Detach() { T *old = val; val = NULL; return old;} // detach without freeing
  void Dispose() { if (!isNull())
      (myClass->*freeFn)(val); val = NULL;}
  bool IsValid() const { return !isNull();}
  T * operator->()  { return val;}
protected:
  bool isNull() const { return val == NULL;}
private:
  RaiiClassCall & operator=(RaiiClassCall<T, C, freeFn> &src); // don't want two RaiiClassCall freeing the same object
  RaiiClassCall(const RaiiClassCall<T, C, freeFn> &src); // never use this.
};

/**
 * RAII class for self deleting NON pointer type, that calls a function on a
 * class. T Must have simple copy semantics & compare semantics.
 *
 * (We need a separate template class from RaiiClassCall, because NULL is not
 * allowed as a template parameter.)
 */
template<typename T, typename C, typename R, R(C:: * freeFn)(T)> class RaiiObjCallVar
{
public:
  T val;
  bool valid;
  C *myClass;
  RaiiObjCallVar(C *myClass) : val(T()), valid(false), myClass(myClass) { }
  RaiiObjCallVar(T val, C *myClass) : val(val), valid(true), myClass(myClass) { }
  ~RaiiObjCallVar() { Dispose();}
  operator T&() { return val;}
  T & operator=(T newval) { Dispose(); val = newval; valid = true; return val;}
  bool operator==(T cmp) const { return val == cmp;}
  T& Detach() { valid = false; return val;} // detach without calling the callback
  void Attach(T newval) { val = newval; valid = true;} // change/set without calling the callback.
  void Dispose() { if (valid)
      (myClass->*freeFn)(val); valid = false;}
  bool IsValid() const { return valid;}
  //T& operator->()  { return val; }  //??
protected:
private:
  RaiiObjCallVar & operator=(RaiiObjCallVar<T, C, R, freeFn> &src); // don't want two RaiiObjCallVar freeing the same object
  RaiiObjCallVar(const RaiiObjCallVar<T, C, R, freeFn> &src); // never use this.
};

/**
 * Same as RaiiObjCallVar, but with void return type.
 *
 */
template<typename T, typename C, void (C::*freeFn)(T)> class RaiiObjCall : public RaiiObjCallVar<T, C, void, freeFn>
{
public:
  RaiiObjCall(C *myClass) : RaiiObjCallVar<T, C, void, freeFn>(myClass) { }
  RaiiObjCall(T val, C *myClass) : RaiiObjCallVar<T, C, void, freeFn>(val, myClass) { }
  T & operator=(T newval) { return RaiiObjCallVar<T, C, void, freeFn>::operator =(newval);}
private:
  RaiiObjCall & operator=(RaiiObjCall<T, C, freeFn> &src); // don't want two RaiiObjCallVar freeing the same object
  RaiiObjCall(const RaiiObjCall<T, C, freeFn> &src); // never use this.
};


/**
 * Call to delete all elements in a single parameter stl container that contains
 * pointers.
 *
 * @param C
 * @param container
 */
template<typename C> void DeletePointerContainer(C &container)
{
  typename C::iterator it;
  for (it = container.begin(); it != container.end(); ++it)
  {
    delete *it;
  }

  container.clear();
}
