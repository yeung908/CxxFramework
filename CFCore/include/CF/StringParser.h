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
    File:       StringParser.h

    Contains:   A couple of handy utilities for parsing a stream.

*/


#ifndef __CF_STRING_PARSER_H__
#define __CF_STRING_PARSER_H__

#include <CF/Types.h>
#include <CF/StrPtrLen.h>

#define STRING_PARSER_TESTING 0

namespace CF {

class StringParser {
 public:

  explicit StringParser(StrPtrLen *inStream)
      : fStartGet(inStream == nullptr ? nullptr : inStream->Ptr),
        fEndGet(inStream == nullptr ? nullptr : inStream->Ptr + inStream->Len),
        fCurLineNumber(1),
        fStream(inStream) {
  }
  ~StringParser() = default;

  // Built-in masks for common stop conditions
  static UInt8 sDigitMask[];      // stop when you hit a digit
  static UInt8 sWordMask[];       // stop when you hit a word
  static UInt8 sEOLMask[];        // stop when you hit an eol
  static UInt8 sEOLWhitespaceMask[]; // stop when you hit an EOL or whitespace
  static UInt8 sEOLWhitespaceQueryMask[]; // stop when you hit an EOL, ? or whitespace

  static UInt8 sNonWordFSlashMask[]; // skip over word and forward slash

  static UInt8 sNonWhitespaceMask[]; // skip over whitespace


  //GetBuffer:
  //Returns a pointer to the string object
  StrPtrLen *GetStream() { return fStream; }

  //Expect:
  //These functions consume the given token/word if it is in the stream.
  //If not, they return false.
  //In all other situations, true is returned.
  //NOTE: if these functions return an error, the object goes into a state where
  //it cannot be guarenteed to function correctly.
  bool Expect(char stopChar);
  bool ExpectEOL();

  //Returns the next word
  void ConsumeWord(StrPtrLen *outString = nullptr) {
    ConsumeUntil(outString, sNonWordMask);
  }

  void ConsumeWordAndFSlash(StrPtrLen *outString = nullptr) {
    ConsumeUntil(outString, sNonWordFSlashMask);
  }

  //Returns all the data before inStopChar
  void ConsumeUntil(StrPtrLen *outString, char inStopChar);

  //Returns whatever integer is currently in the stream
  UInt32 ConsumeInteger(StrPtrLen *outString = nullptr);
  Float32 ConsumeFloat();
  Float32 ConsumeNPT();

  //Keeps on going until non-whitespace
  void ConsumeWhitespace() {
    ConsumeUntil(nullptr, sNonWhitespaceMask);
  }

  //Assumes 'stop' is a 255-char array of booleans. Set this array
  //to a mask of what the stop characters are. true means stop character.
  //You may also pass in one of the many prepackaged masks defined above.
  void ConsumeUntil(StrPtrLen *spl, UInt8 *stop);

  //+ rt 8.19.99
  //returns whatever is available until non-whitespace
  void ConsumeUntilWhitespace(StrPtrLen *spl = nullptr) {
    ConsumeUntil(spl, sEOLWhitespaceMask);
  }

  void ConsumeUntilDigit(StrPtrLen *spl = nullptr) {
    ConsumeUntil(spl, sDigitMask);
  }

  void ConsumeLength(StrPtrLen *spl, SInt32 numBytes);

  void ConsumeEOL(StrPtrLen *outString);

  //GetThru:
  //Works very similar to ConsumeUntil except that it moves past the stop token,
  //and if it can't find the stop token it returns false
  inline bool GetThru(StrPtrLen *spl, char stop);
  inline bool GetThruEOL(StrPtrLen *spl);
  inline bool ParserIsEmpty(StrPtrLen *outString);
  //Returns the current character, doesn't move past it.
  inline char PeekFast() { if (fStartGet) return *fStartGet; else return '\0'; }
  char operator[](int i);

  //Returns some info about the stream
  UInt32 GetDataParsedLen();
  UInt32 GetDataReceivedLen();
  UInt32 GetDataRemaining();
  char *GetCurrentPosition() { return fStartGet; }
  int GetCurrentLineNumber() { return fCurLineNumber; }

  // A utility for extracting quotes from the start and end of a parsed
  // string. (Warning: Do not call this method if you allocated your own
  // pointer for the Ptr field of the StrPtrLen class.) - [sfu]
  //
  // Not sure why this utility is here and not in the StrPtrLen class - [jm]
  static void UnQuote(StrPtrLen *outString);

#if STRING_PARSER_TESTING
  static bool Test();
#endif

 private:

  void advanceMark();

  //built in masks for some common stop conditions
  static UInt8 sNonWordMask[];

  char *fStartGet;
  char *fEndGet;
  int fCurLineNumber;
  StrPtrLen *fStream;

};

bool StringParser::GetThru(StrPtrLen *outString, char inStopChar) {
  ConsumeUntil(outString, inStopChar);
  return Expect(inStopChar);
}

bool StringParser::GetThruEOL(StrPtrLen *outString) {
  ConsumeUntil(outString, sEOLMask);
  return ExpectEOL();
}

}

#endif // __CF_STRING_PARSER_H__
