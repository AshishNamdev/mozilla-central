/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#ifndef COM_auto_ptr_h___
#define COM_auto_ptr_h___



  // Wrapping includes can speed up compiles (see "Large Scale C++ Software Design")
#ifndef nsDebug_h___
  #include "nsDebug.h"
    // for |NS_PRECONDITION|
#endif

#ifndef nsISupports_h___
  #include "nsISupports.h"
    // for |nsresult|, |NS_IF_ADDREF|, et al
#endif



/*
  WARNING:
    This file defines several macros for internal use only.  These macros begin with the
    prefix NSCAP_.  Do not use these macros in your own code.  They are for internal use
    only for cross-platform compatibility, and are subject to change without notice.
*/


/*
  WARNING:
    The code in this file should be considered EXPERIMENTAL.  It defies several of our
    current coding conventions; in particular, it is based on templates.


    Except within the Composer module, it is not to be used in production code under
    any circumstances, until such time as our current coding-conventions barring templates
    can be relaxed.  At that time, this warning will be removed.

    It is checked-in only so that concerned parties can experiment, to see if it fills
    a useful (and affordable) role.

    NOT FOR USE IN PRODUCTION CODE!
*/


/*
  To do...

    + finish `User Manual'
    + better comments
    + a special macro for |QueryInterface|, a la "Essential COM"?
*/


/* USER MANUAL

  What is |COM_auto_ptr|?

    |COM_auto_ptr| is a `smart-pointer'.  It is a template class that acts, syntactically,
    just like an ordinary pointer in C or C++, i.e., you can apply |*| or |->| to it to
    `get to' what it points at.  |COM_auto_ptr| is smart in that, unlike a raw COM
    interface pointer, |COM_auto_ptr| manages |AddRef| and |Release| _for_ you.

    For instance, here is a typical snippet of code (at its most compact) where you assign
    a COM interface pointer into a member variable:

      NS_IF_RELEASE(mFoop);  // If I have one already, I must release it before over-writing it.
      if ( mFooP = aPtr )    // Now it's safe to assign it in, and, if it's not NULL
        mFooP->AddRef();    // I must |AddRef| it, since I'll be holding on to it.

    If our member variable |mFooP| were a |COM_auto_ptr|, however, the snippet above
    would look like this:

      mFoop = aPtr;        // Note: automatically |Release|s the old and |AddRef|s the new

    |COM_auto_ptr| helps you write code that is leak-proof, exception safe, and significantly
    less verbose than you would with raw COM interface pointers.  With |COM_auto_ptr|, you
    may never have to call |AddRef| or |Release| by hand.


    You still have to understand COM.  You still have to know which functions return
    interface pointers that have already been |AddRef|ed and which don't.  You still
    have to ensure you program logic doesn't produce circularly referencing garbage.
    |COM_auto_ptr| is not a panacea.  It is, however, helpful, easy to use, well-tested,
    and polite.  It doesn't require that a function author cooperate with you, nor does
    your use force others to use it.


  Why does |COM_auto_ptr| have such a funny name?  I.e., why doesn't it follow our
  naming conventions?

    The name of this class is very important.  It is designed to communicate the purpose
    of the class easily to any programmer new to the project, who is already familiar with
    |std::auto_ptr| and who knows that COM requires ref-counting.  Relating this class'
    name to |auto_ptr| is far more important to clarity than following the local naming
    convention.  |func_AddRefs| and |func_doesnt_AddRef| use underscores for the same
    reason our special macros do, quoting from our coding conventions "...to make them
    stick out like a sore thumb".  Note also that since |AddRef| is one word,
    |func_AddRefs| and |func_doesnt_AddRef| couldn't have the right spacing if only inter-
    caps were used. 


  Where should I use |COM_auto_ptr|?

    ...


  Where _shouldn't_ I use |COM_auto_ptr|?

    ...


  How does a |COM_auto_ptr| differ from a raw pointer?

    A |COM_auto_ptr| differs, syntactically, from a raw COM interface pointer in three
    ways:

      + It's declared differently, e.g.,

        // instead of saying                // you say
        IFoo* fooP;                         COM_auto_ptr<IFoo> fooP;


      + You can't call |AddRef| or |Release| through it,

        fooP->AddRef();   // OK             fooP->AddRef();   // Error: no permission
        fooP->Release();  // OK             fooP->Release();  // Error: no permission


      + You can't just apply an |&| to it to pass it to the typical `getter' function

        AcquireFoo(&fooP);                  AcquireFoo( getter_AddRefs(fooP) );
        GetFoo(&fooP);                      GetFoo( getter_doesnt_AddRef(fooP) );


  How do I use |COM_auto_ptr|?

    Typically, you can use a |COM_auto_ptr| exactly as you would a standard COM
    interface pointer:

      IFoo* fooP;                           COM_auto_ptr<IFoo> fooP;
      // ...                                // ...
      fooP->SomeFunction(x, y, z);          fooP->SomeFunction(x, y, z);
      AnotherFunction(fooP);                AnotherFunction(fooP);

      if ( fooP )                           if ( fooP )
        // ...                                // ...

      if ( fooP == barP )                   if ( fooP == barP )
        // ...                                // ...

    There are some differences, though.  In particular, you can't call |AddRef| or |Release|
    through a |COM_auto_ptr| directly, nor would you need to.  |AddRef| is called for you
    whenever you assign a COM interface pointer _into_ a |COM_auto_ptr|.  |Release| is
    called on the old value, and also when the |COM_auto_ptr| goes out of scope.  Trying
    to call |AddRef| or |Release| yourself will generate a compile-time error.

      fooP->AddRef();                       // fooP->AddRef();  // ERROR: no permission
      fooP->Release();                      // fooP->Release(); // ERROR: no permission

    The final difference is that a bare |COM_auto_ptr| (or rather a pointer to it) can't
    be supplied as an argument to a function that `fills in' a COM interface pointer.
    Rather it must be wrapped with a utility call that says whether the function calls
    |AddRef| before returning, e.g.,

      ...->QueryInterface(riid, &fooP)      ...->QueryInterface(riid, func_AddRefs(fooP))

      LookupFoo(&fooP);                     LookupFoo( getter_doesnt_AddRef(fooP) );

    Don't worry.  It's a compile-time error if you forget to wrap it.


      IFoo* foo = 0;
      nsresult status = CreateIFoo(&foo);
      if ( NS_SUCCEEDED(status) )
        {
          IBar* bar = 0;
          if ( NS_SUCCEEDED(status = foo->QueryInterface(riid, &bar)) )
            {
              IFooBar* foobar = 0;
              if ( NS_SUCCEEDED(status = CreateIFooBar(foo, bar, &foobar)) )
                {
                  foobar->DoTheReallyHardThing();
                  foobar->Release();
                }
              bar->Release();
            }
          foo->Release();
        }


    COM_auto_ptr<IFoo> fooP;
    nsresult status = CreateIFoo( getter_AddRefs(fooP) );
    if ( NS_SUCCEEDED(status) )
      {
        COM_auto_ptr<IBar> barP;
        if ( NS_SUCCEEDED(status = foo->QueryInterface(riid, getter_AddRefs(barP))) )
          {
            COM_auto_ptr<IFooBar> fooBarP;
            if ( NS_SUCCEEDED(status = CreateIFooBar(fooP, barP, getter_AddRefs(fooBarP))) )
              fooBarP->DoTheReallyHardThing();
          }
      }

    
  Is there an easy way to convert my current code?

    ...


  What do I have to beware of?

    ...
*/







  /*
    Set up some #defines to turn off a couple of troublesome C++ features.
    Interestingly, none of the compilers barf on template stuff.

    Ideally, we would want declarations like these in a configuration file
    that that everybody would get.  Deciding exactly how to do that should
    be part of the process of moving from experimental to production.
  */

#if defined(__GNUG__) && (__GNUC_MINOR__ <= 90) && !defined(SOLARIS)
  #define NSCAP_NO_MEMBER_USING_DECLARATIONS
#endif

#if defined(_MSC_VER) && (_MSC_VER<1100)
  #define NSCAP_NO_EXPLICIT
  #define NSCAP_NO_BOOL
#endif

#if defined(IRIX)
  #define NSCAP_NO_MEMBER_USING_DECLARATIONS
  #define NSCAP_NO_EXPLICIT
  #define NSCAP_NO_NEW_CASTS
  #define NSCAP_NO_BOOL
#endif


#ifdef NSCAP_NO_EXPLICIT
  #define explicit
#endif

#ifndef NSCAP_NO_NEW_CASTS
  #define NSCAP_REINTERPRET_CAST(T,x)  reinterpret_cast<T>(x)
#else
  #define NSCAP_REINTERPRET_CAST(T,x)  ((T)(x))
#endif

#ifndef NSCAP_NO_BOOL
  typedef bool NSCAP_BOOL;
#else
  typedef PRBool NSCAP_BOOL;
#endif


template <class T>
class nsDerivedSafe : public T
    /*
      No client should ever see or have to type the name of this class.  It is the
      artifact that makes it a compile-time error to call |AddRef| and |Release|
      on a |COM_auto_ptr|.

      See |COM_auto_ptr::operator->|, |COM_auto_ptr::operator*|, et al.
    */
  {
    private:
#ifndef NSCAP_NO_MEMBER_USING_DECLARATIONS
      using T::AddRef;
      using T::Release;
#else
      nsrefcnt AddRef();
      nsrefcnt Release();
#endif
  };

#if defined(NSCAP_NO_MEMBER_USING_DECLARATIONS) && defined(NSCAP_NEED_UNUSED_VIRTUAL_IMPLEMENTATIONS)
template <class T>
nsrefcnt
nsDerivedSafe<T>::AddRef()
  {
    return 0;
  }

template <class T>
nsrefcnt
nsDerivedSafe<T>::Release()
  {
    return 0;
  }

#endif




#ifdef NASCAP_FEATURE_DONT_ADDREF
template <class T>
struct nsDontAddRef
    /*
      ...cooperates with |COM_auto_ptr| to allow you to assign in a pointer _without_
      |AddRef|ing it.  You would rarely use this directly, but rather through the
      machinery of |getter_AddRefs| in the argument list to functions that |AddRef|
      their results before returning them to the caller.

      See also |getter_AddRefs()| and |class nsGetterAddRefs|.
    */
  {
    explicit
    nsDontAddRef( T* aRawPtr )
        : mRawPtr(aRawPtr)
      {
        // nothing else to do here
      }

    T* mRawPtr;
  };

template <class T>
nsDontAddRef<T>
dont_AddRef( T* aRawPtr )
    /*
      ...makes typing easier, because it deduces the template type, e.g., 
      you write |dont_AddRef(fooP)| instead of |nsDontAddRef<IFoo>(fooP)|.

      Like the class it is shorthand for, you would rarely use this directly,
      but rather through |getter_AddRefs|.
    */
  {
    return nsDontAddRef<T>(aRawPtr);
  }
#endif




template <class T>
class COM_auto_ptr
    /*
      ...
    */
  {
    public:
      typedef T element_type;

      explicit
      COM_auto_ptr( T* aRawPtr = 0 )
          : mRawPtr(aRawPtr),
            mIsAwaitingAddRef(0)
        {
          NS_IF_ADDREF(mRawPtr);
        }

#ifdef NASCAP_FEATURE_DONT_ADDREF
      explicit
      COM_auto_ptr( const nsDontAddRef<T>& aSmartPtr )
          : mRawPtr(aSmartPtr.mRawPtr),
            mIsAwaitingAddRef(0)
        {
          // nothing else to do here
        }
#endif

      COM_auto_ptr( const COM_auto_ptr<T>& aSmartPtr )
          : mRawPtr(aSmartPtr.mRawPtr),
            mIsAwaitingAddRef(0)
        {
          NS_IF_ADDREF(mRawPtr);
        }

     ~COM_auto_ptr()
        {
          if ( mRawPtr && !mIsAwaitingAddRef )
            NS_RELEASE(mRawPtr);
        }

      COM_auto_ptr&
      operator=( T* rhs )
        {
          reset(rhs);
          return *this;
        }

#ifdef NASCAP_FEATURE_DONT_ADDREF
      COM_auto_ptr&
      operator=( const nsDontAddRef<T>& rhs )
        {
          if ( mRawPtr && !mIsAwaitingAddRef )
            NS_RELEASE(mRawPtr);
          mIsAwaitingAddRef = 0;
          mRawPtr = rhs.mRawPtr;
          return *this;
        }
#endif

      COM_auto_ptr&
      operator=( const COM_auto_ptr& rhs )
        {
          reset(rhs.mRawPtr);
          return *this;
        }

      nsDerivedSafe<T>*
      operator->() const
          // returns a |nsDerivedSafe<T>*| to deny clients the use of |AddRef| and |Release|
        {
          NS_PRECONDITION(mRawPtr != 0, "You can't dereference a NULL COM_auto_ptr with operator->().");
          return get();
        }

      nsDerivedSafe<T>&
      operator*() const
          // returns a |nsDerivedSafe<T>*| to deny clients the use of |AddRef| and |Release|
        {
          NS_PRECONDITION(mRawPtr != 0, "You can't dereference a NULL COM_auto_ptr with operator*().");
          return *get();
        }

      operator nsDerivedSafe<T>*() const
        {
          return get();
        }

      nsDerivedSafe<T>*
      get() const
          // returns a |nsDerivedSafe<T>*| to deny clients the use of |AddRef| and |Release|
        {
          return NSCAP_REINTERPRET_CAST(nsDerivedSafe<T>*, mRawPtr);
        }

      void
      reset( T* aRawPtr = 0 )
        {
          NS_IF_ADDREF(aRawPtr);
          if ( mRawPtr && !mIsAwaitingAddRef )
            NS_RELEASE(mRawPtr);
          mIsAwaitingAddRef = 0;
          mRawPtr = aRawPtr;
        }

#if 0
    private:
      friend class nsGetterAddRefs<T>;
      friend class nsGetterDoesntAddRef<T>;

      /*
        In a perfect world, the following two member functions, |StartAssignment| and
        |FinishAssignment|, would be private.  They are and should be only accessed by
        the closely related classes |nsGetterAddRefs<T>| and |nsGetterDoesntAddRef<T>|.

        Unfortunately, some compilers---most notably VC++5.0---fail to grok the
        friend declarations above or in any alternate acceptable form.  So, physically
        they will be public (until our compilers get smarter); but they are not to be
        considered part of the logical public interface.
      */
#endif

      T**
      StartAssignment( NSCAP_BOOL awaiting_AddRef )
        {
          if ( mRawPtr && !mIsAwaitingAddRef )
            NS_RELEASE(mRawPtr);
          mIsAwaitingAddRef = awaiting_AddRef;
          mRawPtr = 0;
          return &mRawPtr;
        }

      void
      FinishAssignment()
        {
          if ( mIsAwaitingAddRef )
            {
              NS_IF_ADDREF(mRawPtr);
              mIsAwaitingAddRef = 0;
            }
        }

    private:
      T* mRawPtr;
      NSCAP_BOOL mIsAwaitingAddRef;
  };




  /*
    The following functions make comparing |COM_auto_ptr|s and raw pointers
    more convenient.
  */

template <class T>
inline
NSCAP_BOOL
operator==( const COM_auto_ptr<T>& aLeft, const T*const aRight )
  {
    return aLeft.get() == aRight;
  }

template <class T>
inline
NSCAP_BOOL
operator!=( const COM_auto_ptr<T>& aLeft, const T*const aRight )
  {
    return aLeft.get() != aRight;
  }

template <class T>
inline
NSCAP_BOOL
operator==( const T*const aLeft, const COM_auto_ptr<T>& aRight )
  {
    return aLeft == aRight.get();
  }

template <class T>
inline
NSCAP_BOOL
operator!=( const T*const aLeft, const COM_auto_ptr<T>& aRight )
  {
    return aLeft != aRight.get();
  }




template <class T>
class nsGetterAddRefs
    /*
      ...

      This class is designed to be used for anonymous temporary objects in the
      argument list of calls that return COM interface pointers, e.g.,

        COM_auto_ptr<IFoo> fooP;
        ...->QueryInterface(iid, nsGetterAddRefs<IFoo>(fooP))
        ...->QueryInterface(iid, getter_AddRefs(fooP))

      When initialized with a |COM_auto_ptr|, as in the example above, it returns
      a |void**| (or |T**| if needed) that the outer call (|QueryInterface| in this
      case) can fill in.  When this temporary object goes out of scope, just after
      the call returns, its destructor assigned the resulting interface pointer, i.e.,
      |QueryInterface|s result, into the |COM_auto_ptr| it was initialized with.

      See also |nsGetterDoesntAddRef|.
    */
  {
    public:
      explicit
      nsGetterAddRefs( COM_auto_ptr<T>& aSmartPtr )
          : mTargetSmartPtr(&aSmartPtr)
        {
          // nothing else to do
        }

      operator void**()
        {
          return NSCAP_REINTERPRET_CAST(void**, mTargetSmartPtr->StartAssignment(0));
        }

      T*&
      operator*()
        {
          NS_PRECONDITION(mTargetSmartPtr != 0, "getter_AddRefs into no destination");
          return *(mTargetSmartPtr->StartAssignment(0));
        }

      operator T**()
        {
          NS_PRECONDITION(mTargetSmartPtr != 0, "getter_AddRefs into no destination");
          return mTargetSmartPtr->StartAssignment(0);
        }

    private:
      COM_auto_ptr<T>* mTargetSmartPtr;
  };

template <class T>
inline
nsGetterAddRefs<T>
getter_AddRefs( COM_auto_ptr<T>& aSmartPtr )
    /*
      Used around a |COM_auto_ptr| when 
      ...makes the class |nsGetterAddRefs<T>| invisible.
    */
  {
    return nsGetterAddRefs<T>(aSmartPtr);
  }





template <class T>
class nsGetterDoesntAddRef
    /*
      ...
    */
  {
    public:
      explicit
      nsGetterDoesntAddRef( COM_auto_ptr<T>& aSmartPtr )
          : mTargetSmartPtr(&aSmartPtr)
        {
          // nothing else to do
        }

      nsGetterDoesntAddRef( nsGetterDoesntAddRef<T>& F )
          : mTargetSmartPtr(F.mTargetSmartPtr)
        {
          F.mTargetSmartPtr = 0;
        }

     ~nsGetterDoesntAddRef()
        {
          if ( mTargetSmartPtr )
            mTargetSmartPtr->FinishAssignment();
        }

      operator void**()
        {
          return NSCAP_REINTERPRET_CAST(void**, mTargetSmartPtr->StartAssignment(1));
        }

      T*&
      operator*()
        {
          NS_PRECONDITION(mTargetSmartPtr != 0, "getter_doesnt_AddRef into no destination");
          return *(mTargetSmartPtr->StartAssignment(1));
        }

      operator T**()
        {
          NS_PRECONDITION(mTargetSmartPtr != 0, "getter_doesnt_AddRef into no destination");
          return mTargetSmartPtr->StartAssignment(1);
        }

    private:
      nsGetterDoesntAddRef<T> operator=( const nsGetterDoesntAddRef<T>& ); // not to be implemented

    private:
      COM_auto_ptr<T>* mTargetSmartPtr;
  };

template <class T>
inline
nsGetterDoesntAddRef<T>
getter_doesnt_AddRef( COM_auto_ptr<T>& aSmartPtr )
  {
    return nsGetterDoesntAddRef<T>(aSmartPtr);
  }

#endif // !defined(COM_auto_ptr_h___)
