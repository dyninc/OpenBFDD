/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jake Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for various "smart" pointer type classes.
#pragma once
#include <stdio.h>

namespace openbfdd
{ 
  /**
   * RIAA base class for self deleting types  that use a value besides NULL for 
   * empty. T Must have simple copy semantics & compare semantic for 
   * nullcomapreval 
   * 
   */
  template <typename T, T nullval, void (*freeFn)(T)> class RiaaBase 
  {
  public:
    T val;
    RiaaBase() : val(nullval) {}
    RiaaBase(const T val) : val(val) {}
    ~RiaaBase() {Dispose();}
    operator T() const  { return val;}
    T operator=(T newval) {Dispose();val = newval; return newval;}
    bool operator==(T cmp) const {return val == cmp;}
    T Detach() {T old = val; val = nullval;return old;} // detach without freeing
    void Dispose() {if (!isNull()) freeFn(val);val=nullval;} 
    bool IsValid() const {return !isNull();}
  protected:
    bool isNull() const {return val==nullval;}
  private:
    RiaaBase& operator=(RiaaBase<T, nullval, freeFn> &src); // don't want two RiaaBase freeing the same object
    RiaaBase(const RiaaBase<T, nullval, freeFn> &src); // never use this.
  };


  /**
   * RIAA base class for self deleting pointer types  that use NULL for empty. T 
   * Must have simple copy semantics & compare semantics. 
   *  
   * (We need a separate template class, because NULL is not allowed as a template 
   * parameter.) 
   */
  template <typename T, void (*freeFn)(T*)> class RiaaNullBase
  {
  public:
    T* val;
    RiaaNullBase() : val(NULL) {}
    RiaaNullBase( T *val) : val(val) {}
    ~RiaaNullBase() {Dispose();}
    operator T*() const  { return val;}
    T* operator=(T* newval) {Dispose();val = newval; return newval;}
    bool operator==(T* cmp) const {return val == cmp;}
    T* Detach() {T* old = val; val = NULL;return old;} // detach without freeing
    void Dispose() {if (!isNull()) freeFn(val);val=NULL;} 
    bool IsValid() const {return !isNull();}
    T* operator->()  { return val;}
  protected:
    bool isNull() const {return val==NULL;}
  private:
    RiaaNullBase& operator=(RiaaNullBase<T, freeFn> &src) ; // don't want two RiaaNullBase freeing the same object
    RiaaNullBase(const RiaaNullBase<T, freeFn> &src); // never use this.
  };

  /**
   * RAII class for file descriptors.
   * 
   */
  void CloseFileDescriptor(int val);
  typedef RiaaBase<int, -1, CloseFileDescriptor> FileDescriptor;

  /**
   * RAII class for FILE* handles.
   * 
   */
  void CloseFileHandle(FILE *val);
  typedef RiaaNullBase<FILE, CloseFileHandle> FileHandle;


  /**
   * RIAA class for simple array allocated with new or new[]
   */
  template <typename T> void FreeFree(T *val) {free(val);}
  template <typename T> void FreeArray(T *val) {delete[] val;}
  template <typename T> void FreeDelete(T *val) {delete val;}
  template <typename T> struct Riaa 
  {
    typedef RiaaNullBase<T, FreeArray<T> >  DeleteArray;
    typedef RiaaNullBase<T, FreeDelete<T> >  Delete;
    typedef RiaaNullBase<T, FreeFree<T> >  Free;
  };


  /**
   * RIAA class for self deleting pointer types  that use NULL for empty, that 
   * calls a delete function on a class. T Must have simple copy semantics & 
   * compare semantics. 
   *  
   * (We need a separate template class, because NULL is not allowed as a template 
   * parameter.) 
   */
  template <typename T, typename C, void (C::*freeFn)(T*)> class RiaaClassCall
  {
  public:
    T* val;
    C* myClass;
    RiaaClassCall(C* myClass) : val(NULL), myClass(myClass) {}
    RiaaClassCall(const T* val, C* myClass) : val(val), myClass(myClass) {}
    ~RiaaClassCall() {Dispose();}
    operator T*() const  { return val;}
    T* operator=(T* newval) {Dispose();val = newval; return newval;}
    bool operator==(T* cmp) const {return val == cmp;}
    T* Detach() {T* old = val; val = NULL;return old;} // detach without freeing
    void Dispose() {if (!isNull()) (myClass->*freeFn)(val);val=NULL;} 
    bool IsValid() const {return !isNull();}
    T* operator->()  { return val;}
  protected:
    bool isNull() const {return val==NULL;}
  private:
    RiaaClassCall& operator=(RiaaClassCall<T, C, freeFn> &src) ; // don't want two RiaaClassCall freeing the same object
    RiaaClassCall(const RiaaClassCall<T, C, freeFn> &src); // never use this.
  };

  /**
   * RIAA class for self deleting NON pointer type, that calls a function on a 
   * class. T Must have simple copy semantics & compare semantics. 
   *  
   * (We need a separate template class from RiaaClassCall, because NULL is not 
   * allowed as a template parameter.) 
   */
  template <typename T, typename C, typename R, R (C::*freeFn)(T)> class RiaaObjCallVar
  {
  public:
    T val;
    bool valid;
    C* myClass;
    RiaaObjCallVar(C* myClass) : valid(false), myClass(myClass) {}
    RiaaObjCallVar(T val, C* myClass) : val(val), valid(true), myClass(myClass) {}
    ~RiaaObjCallVar() {Dispose();}
    operator T&() { return val;}
    T& operator=(T newval) {Dispose();val = newval; valid=true; return val;}
    bool operator==(T cmp) const {return val == cmp;}
    T& Detach() {valid = false; return val;} // detach without calling the callback
    void Attach(T newval) {val = newval; valid=true;} // change/set without calling the callback.
    void Dispose() {if (valid) (myClass->*freeFn)(val);valid=false;} 
    bool IsValid() const {return valid;}
    //T& operator->()  { return val; }  //??
  protected:
  private:
    RiaaObjCallVar& operator=(RiaaObjCallVar<T, C, R, freeFn> &src) ; // don't want two RiaaObjCallVar freeing the same object
    RiaaObjCallVar(const RiaaObjCallVar<T, C, R, freeFn> &src); // never use this.
  };

  /**
   * Same as RiaaObjCallVar, but with void return type. 
   * 
   */
  template <typename T, typename C, void (C::*freeFn)(T)> class RiaaObjCall : public RiaaObjCallVar<T, C, void, freeFn >
  {
  public:
    RiaaObjCall(C* myClass) : RiaaObjCallVar<T, C, void, freeFn >(myClass) {}
    RiaaObjCall(T val, C* myClass) : RiaaObjCallVar<T, C, void, freeFn >(val, myClass) {}
    T& operator=(T newval) {return RiaaObjCallVar<T, C, void, freeFn >::operator=(newval);}
  private:
    RiaaObjCall& operator=(RiaaObjCall<T, C, freeFn> &src) ; // don't want two RiaaObjCallVar freeing the same object
    RiaaObjCall(const RiaaObjCall<T, C, freeFn> &src); // never use this.
  };

};






