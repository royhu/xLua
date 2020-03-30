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
#include <zlib.h>
#include <assert.h>
#include <stdlib.h>

#define FSNI_VER "1.0.967"

#if defined(__ANDROID__)
#include <android/log.h>
#define FSNI_LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"FSNI-" FSNI_VER,__VA_ARGS__)
#define FSNI_LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"FSNI-" FSNI_VER,__VA_ARGS__)
#else
#define FSNI_LOGD(...)
#define FSNI_LOGE(...)
#endif

#define LUA_LIB

#include "fsni.h"
#include "unzip.h"
#include <unordered_map>
#include <thread>
#include <mutex>

// memcpy
#include <string.h>

#include "yasio/cxx17/string_view.hpp"
#include "yasio/detail/object_pool.hpp"

#if defined(_WIN32)
#define O_READ_FLAGS O_BINARY | O_RDONLY, S_IREAD
#define O_WRITE_FLAGS O_CREAT | O_RDWR | O_BINARY, S_IWRITE | S_IREAD
#define O_APPEND_FLAGS O_APPEND | O_CREAT | O_RDWR | O_BINARY, S_IWRITE | S_IREAD
#define posix_open ::_open
#define posix_close ::_close
#define posix_lseek ::_lseek
#define posix_read ::_read
#define posix_write ::_write
#include <io.h>
#include <fcntl.h>
#include <Windows.h>
#include <direct.h>
#else
// S_IRUSR | S_IRGRP | S_IROTH
#define O_READ_FLAGS O_RDONLY, S_IRUSR
#define O_WRITE_FLAGS O_CREAT | O_RDWR, S_IRWXU
#define O_APPEND_FLAGS O_APPEND | O_CREAT | O_RDWR, S_IRWXU
#define posix_open ::open
#define posix_close ::close
#define posix_lseek ::lseek
#define posix_read ::read
#define posix_write ::write
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#define CC_BREAK_IF(cond) if(cond) break
#define CC_UNUSED

// MINIZIP v1.2 no unzGoToFirstFile64, unzGoToNextFile64
#define unzGoToFirstFile64(A,B,C,D) unzGoToFirstFile2(A,B,C,D, NULL, 0, NULL, 0)
#define unzGoToNextFile64(A,B,C,D) unzGoToNextFile2(A,B,C,D, NULL, 0, NULL, 0)

// --------------------- ZipFile ---------------------
// from unzip.cpp
#define UNZ_MAXFILENAMEINZIP 256

struct ZipEntryInfo
{
    unz64_file_pos pos;
    uint64_t uncompressed_size;
};

class ZipFilePrivate
{
public:
    unzFile zipFile;
    std::mutex zipFileMtx;

    // std::unordered_map is faster if available on the platform
    typedef std::unordered_map<std::string, struct ZipEntryInfo> FileListContainer;
    FileListContainer fileList;
};


ZipFile::ZipFile(const std::string& zipFile, const std::string& filter)
    : m_data(new ZipFilePrivate)
{
    m_data->zipFile = unzOpen(zipFile.c_str());
    if (m_data->zipFile)
    {
        setFilter(filter);
    }
}

ZipFile::~ZipFile()
{
    if (m_data && m_data->zipFile)
    {
        unzClose(m_data->zipFile);
    }
    delete (m_data);
    m_data = nullptr;
}

bool ZipFile::isOpen() const
{
    return m_data->zipFile != nullptr;
}

bool ZipFile::setFilter(const std::string& filter)
{
    bool ret = false;
    do
    {
        CC_BREAK_IF(!m_data);
        CC_BREAK_IF(!m_data->zipFile);

        // clear existing file list
        m_data->fileList.clear();

        // UNZ_MAXFILENAMEINZIP + 1 - it is done so in unzLocateFile
        char szCurrentFileName[UNZ_MAXFILENAMEINZIP + 1];
        unz_file_info64 fileInfo;
        unz64_file_pos posInfo;

        // go through all files and store position information about the required files
        int err = unzGoToFirstFile64(m_data->zipFile, &fileInfo,
            szCurrentFileName, sizeof(szCurrentFileName) - 1);
        while (err == UNZ_OK)
        {
            int posErr = unzGetFilePos64(m_data->zipFile, &posInfo);
            if (posErr == UNZ_OK)
            {
                // cache info about filtered files only (like 'assets/')
                cxx17::string_view currentFileName = szCurrentFileName;
                if (filter.empty()
                    || (currentFileName.size() > filter.size() && cxx20::starts_with(currentFileName, cxx17::string_view(filter))))
                {
                    ZipEntryInfo entry;
                    entry.pos = posInfo;
                    entry.uncompressed_size = fileInfo.uncompressed_size;
                    m_data->fileList[currentFileName.substr(filter.size()).data()] = entry;
                }
            }

            // next file - also get the information about it
            err = unzGoToNextFile64(m_data->zipFile, &fileInfo,
                szCurrentFileName, sizeof(szCurrentFileName) - 1);
        }
        ret = true;

    } while (false);

    return ret;
}

bool ZipFile::fileExists(const std::string& fileName) const
{
    bool ret = false;
    do
    {
        CC_BREAK_IF(!m_data);

        ret = m_data->fileList.find(fileName) != m_data->fileList.end();
    } while (false);

    return ret;
}

// -------------------- fsni ---------------------

#if defined(_WINDLL)
#define FSNI_API __declspec(dllexport)
#else
#define FSNI_API
#endif
#define FSNI_INVALID_FILE_HANDLE -1
#define APK_PREFIX "jar:file://"
#define APK_PREFIX_LEN sizeof(APK_PREFIX)
static std::string s_streamingPath, s_persistPath;
static ZipFile* s_zipFile = nullptr;

struct fsni_stream {
    union {
        voidp entry;
        int fd;
    };
    int64_t offset;
    bool streaming; // whether in apk or obb file.
};

static yasio::gc::object_pool<fsni_stream, std::recursive_mutex> s_fsni_pool;

static const int s_fsni_flags[][2] = {
    O_READ_FLAGS,
    O_WRITE_FLAGS,
    O_APPEND_FLAGS,
};

// helper methods
template<typename _Elem, typename _Pr, typename _Fn> inline
void _fsni_splitpath(_Elem* s, _Pr _Pred, _Fn func) // will convert '\\' to '/'
{
    _Elem* _Start = s; // the start of every string
    _Elem* _Ptr = s;   // source string iterator
    while (_Pred(_Ptr))
    {
        if ('\\' == *_Ptr || '/' == *_Ptr)
        {
            if (_Ptr != _Start) {
                auto _Ch = *_Ptr;
                *_Ptr = '\0';
                bool should_brk = func(s);
#if defined(_WIN32)
                *_Ptr = '\\';
#else // For unix linux like system.
                * _Ptr = '/';
#endif
                if (should_brk)
                    return;
            }
            _Start = _Ptr + 1;
        }
        ++_Ptr;
    }
    if (_Start < _Ptr) {
        func(s);
    }
}

template<typename _Elem, typename _Fn> inline
void fsni_splitpath(_Elem* s, _Fn func) // will convert '\\' to '/'
{
    _fsni_splitpath(s, [=](_Elem* _Ptr) {return *_Ptr != '\0'; }, func);
}

static void fsni_mkdir(std::string dir)
{
    if (dir.empty()) return;

    fsni_splitpath(&dir.front(), [](const char* subdir) {
        bool should_brk = false;

        if (!fsni_exists(subdir, fsni_chkflags::directory))
        {
#ifdef _WIN32
            should_brk = !(0 == ::mkdir(subdir));
#else
            should_brk = !(0 == ::mkdir(subdir, S_IRWXU | S_IRWXG | S_IRWXO));
#endif
        }

        return should_brk;
        });
}

extern "C" {
    FSNI_API void fsni_startup(const char* pszStreamingPath/*internal path*/, const char* pszPersistPath/*hot update path*/)
    {
        s_streamingPath = pszStreamingPath;
        s_persistPath = pszPersistPath;

        if (cxx20::starts_with(cxx17::string_view(s_streamingPath), APK_PREFIX))
        { // Android streamingPath format: jar:file://${APK_PATH}!/assets/
            // [FSNI] Init, streamingAssetsPath: jar:file:///data/app/com.c4games.redalert3d-TBAXBO37ccSyzWzUsJwcHQ==/base.apk!/assets
            // because filter always full relative to zip file, so should remove prefix
            auto endpos = s_streamingPath.rfind("!/");
            if (endpos != std::string::npos) {
                std::string apkPath = s_streamingPath.substr(APK_PREFIX_LEN - 1, endpos - APK_PREFIX_LEN + 1);
                std::string strFilter = s_streamingPath.substr(endpos + 2);
                s_zipFile = new ZipFile(apkPath, strFilter);
                if (!s_zipFile->isOpen()) {
                    delete s_zipFile;
                    s_zipFile = nullptr;
                }
            }
        }
    }
    FSNI_API void fsni_cleanup()
    {
        s_streamingPath.clear();
        s_persistPath.clear();
        if (s_zipFile != nullptr) {
            delete s_zipFile;
            s_zipFile = nullptr;
        }
    }

    FSNI_API voidp fsni_open(const char* fileName, int mode)
    {
        union {
            voidp entry;
            int fd;
        };

        int internalError = 0, error = 0;

        bool absolute = (fileName[0] == '/' || (isalpha(fileName[0]) && fileName[1] == ':'));

        // try open from hot update path disk
        std::string fullPath;
        if (!absolute)
            s_persistPath + fileName;
        else
            fullPath = fileName;
        auto flags = s_fsni_flags[mode];

        bool readonly = flags[0] == s_fsni_flags[fsni_mode::read][0];
        if (!readonly) { // try make file's parent directory
            auto slash = fullPath.find_last_of(R"(/\)");
            if (slash != std::string::npos) {
                auto chTmp = fullPath[slash]; // store
                fullPath[slash] = '\0';
                if (!fsni_exists(fullPath.c_str(), fsni_chkflags::directory))
                    fsni_mkdir(fullPath.substr(0, slash));
                fullPath[slash] = chTmp; // restore
            }
        }

        fd = posix_open(fullPath.c_str(), flags[0], flags[1]);
        bool streaming = false;
        if (fd == FSNI_INVALID_FILE_HANDLE) {
            internalError = errno;
            if (readonly && !absolute) { // only readonly and not absolute path, we can try to read from app internal path
                // try open from internal path
                if (s_zipFile != nullptr) { // android, from apk
                    auto it = s_zipFile->m_data->fileList.find(fileName);
                    if (it != s_zipFile->m_data->fileList.end()) {
                        entry = &it->second;
                        streaming = true;
                    }
                    else error = ENOENT;
                }
                else { // ios, from disk
                    fullPath = s_streamingPath + fileName;
                    fd = posix_open(fullPath.c_str(), O_READ_FLAGS);
                    if (fd == -1)
                        internalError = errno;
                }
            }
        }

        if ((!streaming && fd != FSNI_INVALID_FILE_HANDLE) || (streaming && entry != nullptr)) {
            fsni_stream* f = (fsni_stream*)s_fsni_pool.allocate();
            if (f != nullptr) {
                f->entry = entry;
                f->offset = 0;
                f->streaming = streaming;
                return f;
            }
            else error = ENOMEM;
        }

        FSNI_LOGE("fsni_open ----> %s failed, internalError:%d(%s), error:%d(%s)!", fullPath.c_str(),
            internalError, strerror(internalError),
            error, strerror(error));
        return nullptr;
    }

    FSNI_API int fsni_read(voidp fp, voidp buf, int size)
    {
        fsni_stream* nfs = (fsni_stream*)fp;
        if (nfs != nullptr) {
            if (!nfs->streaming) {
                if (nfs->fd != FSNI_INVALID_FILE_HANDLE)
                    return posix_read(nfs->fd, buf, size);
            }
            else {
                int n = 0;
                do {
                    auto entry = (ZipEntryInfo*)nfs->entry;
                    CC_BREAK_IF(entry == nullptr);

                    auto shared_data = s_zipFile->m_data;
                    CC_BREAK_IF(nfs->offset >= entry->uncompressed_size);

                    std::unique_lock<std::mutex> lck(shared_data->zipFileMtx);

                    int nRet = unzGoToFilePos64(shared_data->zipFile, &entry->pos);
                    CC_BREAK_IF(UNZ_OK != nRet);

                    nRet = unzOpenCurrentFile(shared_data->zipFile);

                    nRet = unzSeek64(shared_data->zipFile, nfs->offset, SEEK_SET);
                    n = unzReadCurrentFile(shared_data->zipFile, buf, size);
                    if (n > 0) {
                        nfs->offset += n;
                    }

                    unzCloseCurrentFile(shared_data->zipFile);

                } while (false);

                return n;
            }
        }

        return 0;
    }

    FSNI_API int fsni_write(voidp fp, const voidp buf, int size) {
        fsni_stream* nfs = (fsni_stream*)fp;
        if (nfs != nullptr && !nfs->streaming && nfs->fd != FSNI_INVALID_FILE_HANDLE)
        { // for write mode, must always writeable path
            return posix_write(nfs->fd, buf, size);
        }
        return 0;
    }

    FSNI_API int fsni_seek(voidp fp, int offset, int origin)
    {
        fsni_stream* nfs = (fsni_stream*)fp;
        if (nfs != nullptr) {
            if (!nfs->streaming) {
                if (nfs->fd != FSNI_INVALID_FILE_HANDLE)
                    return posix_lseek(nfs->fd, offset, origin);
            }
            else {
                long result = -1;
                auto entry = (ZipEntryInfo*)nfs->entry;
                if (entry != nullptr) {

                    switch (origin) {
                    case SEEK_SET:
                        result = offset;
                        break;
                    case SEEK_CUR:
                        result = nfs->offset + offset;
                        break;
                    case SEEK_END:
                        result = (long)entry->uncompressed_size + offset;
                        break;
                    default:;
                    }

                    if (result >= 0) {
                        nfs->offset = result;
                    }
                    else
                        result = -1;
                }

                return result;
            }
        }

        return -1;
    }

    FSNI_API void fsni_close(voidp fp)
    {
        fsni_stream* nfs = (fsni_stream*)fp;
        if (nfs != nullptr) {
            if (!nfs->streaming) {
                if (nfs->fd != FSNI_INVALID_FILE_HANDLE)
                    posix_close(nfs->fd);
            }
            s_fsni_pool.deallocate(nfs);
        }
    }

    FSNI_API int fsni_getsize(voidp fp)
    {
        int size = fsni_seek(fp, 0, SEEK_END);
        fsni_seek(fp, 0, SEEK_SET);
        return size;
    }

    int fsni_remove(const char* path)
    {
        return ::remove(path);
    }
    int fsni_rename(const char* oldName, const char* newName)
    {
        return ::rename(oldName, newName);
    }
    bool fsni_exists(const char* path, int flags)
    {
        bool found = false;
#if defined(_WIN32)
        DWORD attr = GetFileAttributesA(path);
        if (attr != INVALID_FILE_ATTRIBUTES) {
            if (flags & 1)
                found = !(attr & FILE_ATTRIBUTE_DIRECTORY);
            if (flags & 2)
                found = (attr & FILE_ATTRIBUTE_DIRECTORY);
        }
#else
        struct stat st;
        if (::stat(path, &st) == 0)
        {
            if (flags & 1)
                found = S_ISREG(st.st_mode);
            if (flags & 2)
                found = S_ISDIR(st.st_mode);;
        }
#endif
        // check android, from apk
        if (!found && s_zipFile != nullptr) {
            found = (s_zipFile->m_data->fileList.find(path) != s_zipFile->m_data->fileList.end());
        }

        return found;
    }
}
