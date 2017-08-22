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
    File:       HTTPSessionInterface.cpp
    Contains:   Implementation of HTTPSessionInterface object.
*/

#include "HTTPSessionInterface.h"

#if DEBUG
#define HTTP_SESSION_INTERFACE_DEBUGGING 1
#else
#define HTTP_SESSION_INTERFACE_DEBUGGING 0
#endif

std::atomic_uint HTTPSessionInterface::sSessionIndexCounter{kFirstHTTPSessionID};

void HTTPSessionInterface::Initialize() {

}

HTTPSessionInterface::HTTPSessionInterface()
    : Task(),
      fTimeoutTask(NULL, 30 * 1000),
      fInputStream(&fSocket),
      fOutputStream(&fSocket, &fTimeoutTask),
      fSessionMutex(),
      fSocket(NULL, Socket::kNonBlockingSocketType),
      fOutputSocketP(&fSocket),
      fInputSocketP(&fSocket),
      fLiveSession(true),
      fObjectHolders(0),
      fRequestBodyLen(-1),
      fAuthenticated(false) {

  fTimeoutTask.SetTask(this);
  fSocket.SetTask(this);

  //fSessionIndex = (UInt32)atomic_add(&sSessionIndexCounter, 1);
  fSessionIndex = ++sSessionIndexCounter;

  fInputStream.ShowRTSP(true);
  fOutputStream.ShowRTSP(true);
}

HTTPSessionInterface::~HTTPSessionInterface() {
  // If the input socket is != output socket, the input socket was created dynamically
  if (fInputSocketP != fOutputSocketP)
    delete fInputSocketP;
}

void HTTPSessionInterface::DecrementObjectHolderCount() {

  //#if __Win32__
  //maybe don't need this special case but for now on Win32 we do it the old way since the killEvent code hasn't been verified on Windows.
  this->Signal(Task::kReadEvent);//have the object wakeup in case it can go away.
  //atomic_sub(&fObjectHolders, 1);
  --fObjectHolders;
  //#else
  //    if (0 == atomic_sub(&fObjectHolders, 1))
  //        this->Signal(Task::kKillEvent);
  //#endif

}

QTSS_Error HTTPSessionInterface::Write(void *inBuffer, UInt32 inLength,
                                       UInt32 *outLenWritten, UInt32 inFlags) {
  UInt32 sendType = HTTPResponseStream::kDontBuffer;
  if ((inFlags & qtssWriteFlagsBufferData) != 0)
    sendType = HTTPResponseStream::kAlwaysBuffer;

  iovec theVec[2];
  theVec[1].iov_base = (char *) inBuffer;
  theVec[1].iov_len = inLength;
  return fOutputStream.WriteV(theVec, 2, inLength, outLenWritten, sendType);
}

QTSS_Error HTTPSessionInterface::WriteV(iovec *inVec,
                                        UInt32 inNumVectors,
                                        UInt32 inTotalLength,
                                        UInt32 *outLenWritten) {
  return fOutputStream.WriteV(inVec,
                              inNumVectors,
                              inTotalLength,
                              outLenWritten,
                              RTSPResponseStream::kDontBuffer);
}

QTSS_Error HTTPSessionInterface::Read(void *ioBuffer,
                                      UInt32 inLength,
                                      UInt32 *outLenRead) {
  //
  // Don't let callers of this function accidently creep past the end of the
  // request body.  If the request body size isn't known, fRequestBodyLen will be -1

  if (fRequestBodyLen == 0)
    return QTSS_NoMoreData;

  if ((fRequestBodyLen > 0) && ((SInt32) inLength > fRequestBodyLen))
    inLength = fRequestBodyLen;

  UInt32 theLenRead = 0;
  QTSS_Error theErr = fInputStream.Read(ioBuffer, inLength, &theLenRead);

  if (fRequestBodyLen >= 0)
    fRequestBodyLen -= theLenRead;

  if (outLenRead != NULL)
    *outLenRead = theLenRead;

  return theErr;
}

QTSS_Error HTTPSessionInterface::RequestEvent(QTSS_EventType inEventMask) {
  if (inEventMask & QTSS_ReadableEvent)
    fInputSocketP->RequestEvent(EV_RE);
  if (inEventMask & QTSS_WriteableEvent)
    fOutputSocketP->RequestEvent(EV_WR);

  return QTSS_NoErr;
}

/*
	take the TCP socket away from a RTSP session that's
	waiting to be snarfed.
*/

void HTTPSessionInterface::SnarfInputSocket(HTTPSessionInterface *fromRTSPSession) {
  Assert(fromRTSPSession != NULL);
  Assert(fromRTSPSession->fOutputSocketP != NULL);

  fInputStream.SnarfRetreat(fromRTSPSession->fInputStream);

  if (fInputSocketP == fOutputSocketP)
    fInputSocketP = new TCPSocket(this, Socket::kNonBlockingSocketType);
  else
    fInputSocketP->Cleanup();   // if this is a socket replacing an old socket, we need
  // to make sure the file descriptor gets closed
  fInputSocketP->SnarfSocket(fromRTSPSession->fSocket);

  // fInputStream, meet your new input socket
  fInputStream.AttachToSocket(fInputSocketP);
}

void *HTTPSessionInterface::SetupParams(QTSSDictionary *inSession,
                                        UInt32 * /*outLen*/) {
  HTTPSessionInterface *theSession = (HTTPSessionInterface *) inSession;

  theSession->fLocalAddr = theSession->fSocket.GetLocalAddr();
  theSession->fRemoteAddr = theSession->fSocket.GetRemoteAddr();

  theSession->fLocalPort = theSession->fSocket.GetLocalPort();
  theSession->fRemotePort = theSession->fSocket.GetRemotePort();

  StrPtrLen *theLocalAddrStr = theSession->fSocket.GetLocalAddrStr();
  StrPtrLen *theLocalDNSStr = theSession->fSocket.GetLocalDNSStr();
  StrPtrLen *theRemoteAddrStr = theSession->fSocket.GetRemoteAddrStr();
  if (theLocalAddrStr == NULL || theLocalDNSStr == NULL || theRemoteAddrStr
      == NULL) {    //the socket is bad most likely values are all 0. If the socket had an error we shouldn't even be here.
    //theLocalDNSStr is set to localAddr if it is unavailable, so it should be present at this point as well.
    Assert(0);   //for debugging
    return NULL; //nothing to set
  }
  theSession->SetVal(easyHTTPSesLocalAddr,
                     &theSession->fLocalAddr,
                     sizeof(theSession->fLocalAddr));
  theSession->SetVal(easyHTTPSesLocalAddrStr,
                     theLocalAddrStr->Ptr,
                     theLocalAddrStr->Len);
  theSession->SetVal(easyHTTPSesLocalDNS,
                     theLocalDNSStr->Ptr,
                     theLocalDNSStr->Len);
  theSession->SetVal(easyHTTPSesRemoteAddr,
                     &theSession->fRemoteAddr,
                     sizeof(theSession->fRemoteAddr));
  theSession->SetVal(easyHTTPSesRemoteAddrStr,
                     theRemoteAddrStr->Ptr,
                     theRemoteAddrStr->Len);

  theSession->SetVal(easyHTTPSesLocalPort,
                     &theSession->fLocalPort,
                     sizeof(theSession->fLocalPort));
  theSession->SetVal(easyHTTPSesRemotePort,
                     &theSession->fRemotePort,
                     sizeof(theSession->fRemotePort));
  return NULL;
}

QTSS_Error HTTPSessionInterface::SendHTTPPacket(StrPtrLen *contentXML,
                                                bool connectionClose,
                                                bool decrement) {
  return QTSS_NoErr;
}