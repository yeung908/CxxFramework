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
    File:       OSHeap.h

    Contains:   Implements a heap

*/

#ifndef __OS_HEAP_H__
#define __OS_HEAP_H__

#include <OSHeaders.h>
#include "OSCond.h"

#define _OSHEAP_TESTING_ 0

class OSHeapElem;

/**
 * @brief 小顶堆，使用数组实现
 * @note 只保存 OSHeapElem 对象指针，不管理对象内存
 */
class OSHeap {
 public:

  enum {
    kDefaultStartSize = 1024 //UInt32
  };

  OSHeap(UInt32 inStartSize = kDefaultStartSize);
  ~OSHeap() { if (fHeap != nullptr) delete[] fHeap; }

  //
  // ACCESSORS

  UInt32 CurrentHeapSize() { return fFreeIndex - 1; }
  OSHeapElem *PeekMin() {
    if (CurrentHeapSize() > 0) return fHeap[1];
    return nullptr;
  }

  //
  // MODIFIERS

  // These are the two primary operations supported by the heap
  // abstract data type. both run in log(n) time.

  void Insert(OSHeapElem *inElem);
  OSHeapElem *ExtractMin() { return extract(1); }

  // removes specified element from the heap
  OSHeapElem *Remove(OSHeapElem *elem);

#if _OSHEAP_TESTING_
  //returns true if it passed the test, false otherwise
  static bool       Test();
#endif

 private:

  OSHeapElem *extract(UInt32 index);

#if _OSHEAP_TESTING_
  //verifies that the heap is in fact a heap
  void            sanityCheck(UInt32 root);
#endif

  OSHeapElem **fHeap;
  UInt32 fFreeIndex;
  UInt32 fArraySize;
};

class OSHeapElem {
 public:
  OSHeapElem(void *enclosingObject = nullptr)
      : fValue(0), fEnclosingObject(enclosingObject), fCurrentHeap(nullptr) {}
  ~OSHeapElem() {}

  //This data structure emphasizes performance over extensibility
  //If it were properly object-oriented, the compare routine would
  //be virtual. However, to avoid the use of v-functions in this data
  //structure, I am assuming that the objects are compared using a 64 bit number.
  //
  void SetValue(SInt64 newValue) { fValue = newValue; }
  SInt64 GetValue() { return fValue; }
  void *GetEnclosingObject() { return fEnclosingObject; }
  void SetEnclosingObject(void *obj) { fEnclosingObject = obj; }
  bool IsMemberOfAnyHeap() { return fCurrentHeap != nullptr; }

 private:

  SInt64 fValue;
  void *fEnclosingObject;
  OSHeap *fCurrentHeap;

  friend class OSHeap;
};

#endif // __OS_HEAP_H__
