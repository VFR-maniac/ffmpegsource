//  Copyright (c) 2009 Karl Blomster
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.


#include "auto_config.h"

/*
If you want to manually change what libraries FFmpegSource will be linked to,
here is the place to do it. auto_config.h is automatically generated by a .bat
file but may not always contain correct #defines.

If you want to force compilation WITH a given library, uncomment the corresponding #define.
If you want to force compilation WITHOUT a given library, uncomment the corresponding #undef.
*/

// libpostproc (libpostproc.a)
//#define	WITH_LIBPOSTPROC
//#undef	WITH_LIBPOSTPROC

// OpenCore AMR narrowband (libopencore-amrnb.a)
//#define	WITH_OPENCORE_AMR_NB
//#undef	WITH_OPENCORE_AMR_NB

// OpenCore AMR wideband (libopencore-amrwb.a)
//#define	WITH_OPENCORE_AMR_WB
//#undef	WITH_OPENCORE_AMR_WB

// pthreads (libpthreadGC2.a)
//#define	WITH_PTHREAD_GC2
//#undef	WITH_PTHREAD_GC2
