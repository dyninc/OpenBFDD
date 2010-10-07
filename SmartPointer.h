/************************************************************** 
* Copyright (c) 2010, Dynamic Network Services, Inc.
* Jacob Montgomery (jmontgomery@dyn.com) & Tom Daly (tom@dyn.com)
* Distributed under the FreeBSD License - see LICENSE
***************************************************************/
// Base header for various "smart" pointer type classes.
#pragma once
#include <unistd.h>
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
    bool IsValid() {return !isNull();}
  protected:
    bool isNull() {return val==nullval;}
  private:
    RiaaBase& operator=(RiaaBase<T, nullval, freeFn> &src)  {fprintf(stderr, "Bad operator=\n"); return *this;}  // don't want two RiaaBase freeing the same object
    RiaaBase(const RiaaBase<T, nullval, freeFn> &src) {fprintf(stderr, "Bad constructor\n");}; // never use this.
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
    RiaaNullBase(T* val) : val(val) {}
    ~RiaaNullBase() {Dispose();}
    operator T*() const  { return val;}
    T* operator=(T* newval) {Dispose();val = newval; return newval;}
    bool operator==(T* cmp) const {return val == cmp;}
    T* Detach() {T* old = val; val = NULL;return old;} // detach without freeing
    void Dispose() {if (!isNull()) freeFn(val);val=NULL;} 
    bool IsValid() {return !isNull();}
    T* operator->()  { return val; }
  protected:
    bool isNull() {return val==NULL;}
  private:
    RiaaNullBase& operator=(RiaaNullBase<T, freeFn> &src)  {fprintf(stderr, "Bad operator=\n"); return *this;}  // don't want two RiaaNullBase freeing the same object
    RiaaNullBase(const RiaaNullBase<T, freeFn> &src) {fprintf(stderr, "Bad constructor\n");}; // never use this.
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
   * RIAA base class for class pointers that use delete.
   */
  template <typename T> class RiaaClass
  {
  public:
    T* val;
    RiaaClass() : val(NULL) {}
    RiaaClass(T* val) : val(val) {}
    ~RiaaClass() {Dispose();}
    operator T*() const  { return val;}
    T* operator=(T* newval) {Dispose();val = newval; return newval;}
    bool operator==(T* cmp) const {return val == cmp;}
    T* Detach() {T* old = val; val = NULL;return old;} // detach without freeing
    void Dispose() {if (!isNull()) delete val;val=NULL;} 
    bool IsValid() {return !isNull();}
    T* operator->()  { return val; }
  protected:
    bool isNull() {return val==NULL;}

  private:
    RiaaClass& operator=(RiaaClass<T> &src)  {fprintf(stderr, "Bad operator=\n"); return *this;}  // don't want two RiaaClass freeing the same object
    RiaaClass(const RiaaClass<T> &src) {fprintf(stderr, "Bad constructor\n");}; // never use this.
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
    RiaaClassCall(T* val, C* myClass) : val(val), myClass(myClass) {}
    ~RiaaClassCall() {Dispose();}
    operator T*() const  { return val;}
    T* operator=(T* newval) {Dispose();val = newval; return newval;}
    bool operator==(T* cmp) const {return val == cmp;}
    T* Detach() {T* old = val; val = NULL;return old;} // detach without freeing
    void Dispose() {if (!isNull()) (myClass->*freeFn)(val);val=NULL;} 
    bool IsValid() {return !isNull();}
    T* operator->()  { return val; }
  protected:
    bool isNull() {return val==NULL;}
  private:
    RiaaClassCall& operator=(RiaaClassCall<T, C, freeFn> &src)  {fprintf(stderr, "Bad operator=\n"); return *this;}  // don't want two RiaaClassCall freeing the same object
    RiaaClassCall(const RiaaClassCall<T, C, freeFn> &src) {fprintf(stderr, "Bad constructor\n");}; // never use this.
  };


};






