/****************************************************************************
Copyright (c) 2010 cocos2d-x.org

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/
#ifndef _FSNI_H_
#define _FSNI_H_

#define FSNI_VER "1.1.968"

#if defined(_WINDLL)
#  if defined(LUA_LIB)
#    define FSNI_API __declspec(dllexport)
#  else
#    define FSNI_API __declspec(dllimport)
#  endif
#else
#  define FSNI_API
#endif

#include <string>

// forward declaration
class ZipFilePrivate;
/**
* Zip file - reader helper class.
*
* It will cache the file list of a particular zip file with positions inside an archive,
* so it would be much faster to read some particular files or to check their existance.
*
* @since v2.0.5
*/
class ZipFile
{
public:
    /**
    * Constructor, open zip file and store file list.
    *
    * @param zipFile Zip file name
    * @param filter The first part of file names, which should be accessible.
    *               For example, "assets/". Other files will be missed.
    *
    * @since v2.0.5
    */
    ZipFile(const std::string& zipFile, const std::string& filter = std::string());
    virtual ~ZipFile();

    bool isOpen() const;

    /**
    * Regenerate accessible file list based on a new filter string.
    *
    * @param filter New filter string (first part of files names)
    * @return true whenever zip file is open successfully and it is possible to locate
    *              at least the first file, false otherwise
    *
    * @since v2.0.5
    */
    bool setFilter(const std::string& filter);

    /**
    * Check does a file exists or not in zip file
    *
    * @param fileName File to be checked on existance
    * @return true whenever file exists, false otherwise
    *
    * @since v2.0.5
    */
    bool fileExists(const std::string& fileName) const;

public:
    /** Internal data like zip file pointer / file list array and so on */
    ZipFilePrivate* m_data;
};

/*
The File Stream Native Interface
DllImports for C#
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern void fsni_startup(string streamingAssetPath, string persistDataPath);
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern IntPtr fsni_open(string fileName);
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern int fsni_read(IntPtr fp, byte[] buf, int size);
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern int fsni_seek(IntPtr fp, int offset, int origin);
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern void fsni_close(IntPtr fp);
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern int fsni_getsize(IntPtr fp);
[DllImport(LIBNAME, CallingConvention = CallingConvention.Cdecl)]
public static extern void fsni_cleanup();
*/
namespace fsni_chkflags {
    enum {
        file = 1,
        directory = 1 << 1,
    };
}
namespace fsni_mode {
    enum {
        read,
        write,
        append,
    };
};

extern "C" {
    FSNI_API void fsni_startup(const char* pszStreamingPath/*internal path*/, const char* pszPersistPath/*hot update path*/);
    /*
    @params:
      path: path of file
      mode: 0: read, 1: write, 2: append
    */
    FSNI_API voidp fsni_open(const char* path, int mode);
    FSNI_API int fsni_read(voidp fp, voidp buf, int size); // DLLimport( nt fsni_read(voidp fp, byte[] buf, int size)
    FSNI_API int fsni_write(voidp fp, const voidp buf, int size);
    FSNI_API int fsni_seek(voidp fp, int offset, int origin);
    FSNI_API void fsni_close(voidp fp);
    FSNI_API int fsni_getsize(voidp fp);
    FSNI_API int fsni_remove(const char* path);
    FSNI_API int fsni_rename(const char* oldName, const char* newName);

    /* ------------ native memory operations ------------- */
    FSNI_API voidp fsni_alloc(int size);
    FSNI_API void fsni_free(voidp);

    FSNI_API voidp fsni_strdup(const char* s);
    FSNI_API voidp fsni_strndup(const char* s, int len);
    FSNI_API voidp fsni_memdup(const voidp p, int size);

    /*
    @flags: 1: check file exists, 2: check directory exists£¬ 3£º check file or directory exists
    @see: fsni_chkflags
    */
    FSNI_API bool fsni_exists(const char* path, int flags);
    FSNI_API void fsni_cleanup();
}

#endif // _FSNI_H_

