/*
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Copyright (c) 1999-2008 Apple Inc.  All Rights Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 *
 */
/*
    File:       OSThread.cpp

    Contains:   Thread abstraction implementation

*/

#include <stdio.h>
#include <stdlib.h>
#include "SafeStdLib.h"

#ifdef __MacOSX__
#include <mach/mach_types.h>
#include <mach/mach_time.h>
#endif

#ifndef __Win32__
#if __PTHREADS__

#include <pthread.h>

#if USE_THR_YIELD
#include <thread.h>
#endif
#else
#include <mach/mach.h>
#include <mach/cthreads.h>
#endif

#include <unistd.h>

#endif

#include "OSThread.h"
#include "OSTime.h"
#include "MyAssert.h"

#ifdef __sgi__
#include <time.h>
#endif

//
// OSThread.cp
//
void *OSThread::sMainThreadData = nullptr;

#ifdef __Win32__
DWORD   OSThread::sThreadStorageIndex = 0;
#elif __PTHREADS__
pthread_key_t OSThread::gMainKey = 0;
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
pthread_attr_t OSThread::sThreadAttr;
#endif
#endif

#if __linux__ || __MacOSX__
bool  OSThread::sWrapSleep = true;
#endif

void OSThread::Initialize() {

#ifdef __Win32__
  sThreadStorageIndex = ::TlsAlloc();
  Assert(sThreadStorageIndex >= 0);
#elif __PTHREADS__
  pthread_key_create(&OSThread::gMainKey, nullptr);
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING

  //
  // Added for Solaris...

  pthread_attr_init(&sThreadAttr);
  /* Indicate we want system scheduling contention scope. This
     thread is permanently "bound" to an LWP */
  pthread_attr_setscope(&sThreadAttr, PTHREAD_SCOPE_SYSTEM);
#endif

#endif
}

OSThread::OSThread()
    : fStopRequested(false),
      fJoined(false),
      fThreadData(nullptr) {
}

OSThread::~OSThread() {
  this->StopAndWaitForThread();
}

void OSThread::Start() {
  // 调用 pthread_create 创建一个线程,该线程的入口函数为 OSThread::_Entry 函数。

#ifdef __Win32__
  unsigned int theId = 0; // We don't care about the identifier
  fThreadID = (HANDLE)_beginthreadex(nullptr,   // Inherit security
      0,      // Inherit stack size
      _Entry, // Entry function
      (void*)this,    // Entry arg
      0,      // Begin executing immediately
      &theId);
  Assert(fThreadID != nullptr);
#elif __PTHREADS__
  pthread_attr_t *theAttrP;
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
  // 注意:对于有_POSIX_THREAD_PRIORITY_SCHEDULING 宏定义的 pthread 库,创建线程的时候,应该添
  // 加属性 PTHREAD_SCOPE_SYSTEM,但是这里被注释掉了!!!
  //theAttrP = &sThreadAttr;
  theAttrP = 0;
#else
  theAttrP = nullptr;
#endif
  int err =
      pthread_create((pthread_t *) &fThreadID, theAttrP, _Entry, (void *) this);
  Assert(err == 0);
#else
  fThreadID = (UInt32)cthread_fork((cthread_fn_t)_Entry, (any_t)this);
#endif
}

void OSThread::StopAndWaitForThread() {
  fStopRequested = true;
  if (!fJoined)
    Join();
}

void OSThread::Join() {
  // What we're trying to do is allow the thread we want to delete to complete
  // running. So we wait for it to stop.
  Assert(!fJoined);
  fJoined = true;
#ifdef __Win32__
  DWORD theErr = ::WaitForSingleObject(fThreadID, INFINITE);
  Assert(theErr == WAIT_OBJECT_0);
#elif __PTHREADS__
  void *retVal;
  pthread_join((pthread_t) fThreadID, &retVal);
#else
  cthread_join((cthread_t)fThreadID);
#endif
}

void OSThread::ThreadYield() {
  // on platforms who's threading is not pre-emptive yield
  // to another thread
#if THREADING_IS_COOPERATIVE
#if __PTHREADS__
#if USE_THR_YIELD
  thr_yield();
#else
  sched_yield();
#endif
#endif
#endif
}

void OSThread::Sleep(UInt32 inMsec) {

#ifdef __Win32__
  ::Sleep(inMsec);
#elif __linux__ || __MacOSX__

  if (inMsec == 0)
    return;

  SInt64 startTime = OSTime::Milliseconds();
  SInt64 timeLeft = inMsec;
  SInt64 timeSlept = 0;
  UInt64 utimeLeft = 0;

  do { // loop in case we leave the sleep early
    //qtss_printf("OSThread::Sleep time slept= %qd request sleep=%qd\n",timeSlept, timeLeft);
    timeLeft = inMsec - timeSlept;
    if (timeLeft < 1)
      break;

    utimeLeft = timeLeft * 1000;
    //qtss_printf("OSThread::Sleep usleep=%qd\n", utimeLeft);
    ::usleep(utimeLeft);

    timeSlept = (OSTime::Milliseconds() - startTime);
    if (timeSlept < 0) // system time set backwards
      break;

  } while (timeSlept < inMsec);

  //qtss_printf("total sleep = %qd request sleep=%"   _U32BITARG_   "\n", timeSlept,inMsec);

#elif defined(__osf__) || defined(__hpux__)
  if (inMsec < 1000)
      ::usleep(inMsec * 1000); // useconds must be less than 1,000,000
  else
      ::sleep((inMsec + 500) / 1000); // round to the nearest whole second
#elif defined(__sgi__)
  struct timespec ts;

  ts.tv_sec = 0;
  ts.tv_nsec = inMsec * 1000000;

  nanosleep(&ts, 0);
#else
  ::usleep(inMsec * 1000);
#endif
}

#ifdef __Win32__
unsigned int WINAPI OSThread::_Entry(LPVOID inThread) {
#else
void *OSThread::_Entry(void *inThread) { //static
#endif

  OSThread *theThread = (OSThread *) inThread;
#ifdef __Win32__
  BOOL theErr = ::TlsSetValue(sThreadStorageIndex, theThread);
  Assert(theErr == TRUE);
#elif __PTHREADS__
  theThread->fThreadID = (pthread_t) pthread_self();
  // 注意:在这里调用了 pthread_setspecific 将 theThread 和线程的 key 相关联
  pthread_setspecific(OSThread::gMainKey, theThread);
#else
  theThread->fThreadID = (UInt32)cthread_self();
  cthread_set_data(cthread_self(), (any_t)theThread);
#endif

  //
  // Run the thread
  // Entry 函数是 OSThread 的纯虚函数,所以这里实际上会调用派生类的 Entry
  // 函数,也就是 TaskThread::Entry 函数。
  theThread->Entry();

#ifdef __Win32__
  return 0;
#else
  return nullptr;
#endif
}

OSThread *OSThread::GetCurrent() {
#ifdef __Win32__
  return (OSThread *)::TlsGetValue(sThreadStorageIndex);
#elif __PTHREADS__
  return (OSThread *) pthread_getspecific(OSThread::gMainKey);
#else
  return (OSThread*)cthread_data(cthread_self());
#endif
}

#ifdef __Win32__
int OSThread::GetErrno()
{
    int winErr = ::GetLastError();


    // Convert to a POSIX errorcode. The *major* assumption is that
    // the meaning of these codes is 1-1 and each Winsock, etc, etc
    // function is equivalent in errors to the POSIX standard. This is
    // a big assumption, but the server only checks for a small subset of
    // the real errors, on only a small number of functions, so this is probably ok.
    switch (winErr)
    {

    case ERROR_FILE_NOT_FOUND: return ENOENT;

    case ERROR_PATH_NOT_FOUND: return ENOENT;




    case WSAEINTR:      return EINTR;
    case WSAENETRESET:  return EPIPE;
    case WSAENOTCONN:   return ENOTCONN;
    case WSAEWOULDBLOCK:return EAGAIN;
    case WSAECONNRESET: return EPIPE;
    case WSAEADDRINUSE: return EADDRINUSE;
    case WSAEMFILE:     return EMFILE;
    case WSAEINPROGRESS:return EINPROGRESS;
    case WSAEADDRNOTAVAIL: return EADDRNOTAVAIL;
    case WSAECONNABORTED: return EPIPE;
    case 0:             return 0;

    default:            return ENOTCONN;
    }
}
#endif