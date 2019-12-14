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

#if defined(__ANDROID__)
#include <android/log.h>
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"FSNI",__VA_ARGS__)
#else
#define LOGD(...)
#endif

#define LUA_LIB

#define MZ_2_9_1 0

#include "fsni.h"
#if !MZ_2_9_1
#include "unzip.h"
#else
#include "mz_compat.h"
#endif
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
#else
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
    unz_file_pos pos;
    uLong uncompressed_size;
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

#if !MZ_2_9_1
        // go through all files and store position information about the required files
        int err = unzGoToFirstFile64(m_data->zipFile, &fileInfo,
            szCurrentFileName, sizeof(szCurrentFileName) - 1);
#else
        int err = unzGoToFirstFile(m_data->zipFile);
#endif
        while (err == UNZ_OK)
        {
#if MZ_2_9_1
            char extra[128];
            unzGetCurrentFileInfo64(m_data->zipFile, &fileInfo, szCurrentFileName, sizeof(szCurrentFileName), extra,
                sizeof(extra), NULL, -1);
#endif
            unz_file_pos posInfo;

            int posErr = unzGetFilePos(m_data->zipFile, &posInfo);
            if (posErr == UNZ_OK)
            {
                // cache info about filtered files only (like 'assets/')
                cxx17::string_view currentFileName = szCurrentFileName;
                if (filter.empty()
                    || cxx20::starts_with(currentFileName, cxx17::string_view(filter)))
                {
                    ZipEntryInfo entry;
                    entry.pos = posInfo;
                    entry.uncompressed_size = (uLong)fileInfo.uncompressed_size;
                    m_data->fileList[currentFileName.substr(filter.size()).data()] = entry;
                }
            }
            // next file - also get the information about it
#if !MZ_2_9_1
            err = unzGoToNextFile64(m_data->zipFile, &fileInfo,
                szCurrentFileName, sizeof(szCurrentFileName) - 1);
#else
            err = unzGoToNextFile(m_data->zipFile);
#endif
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
    uLong offset;
    bool streaming; // whether in apk or obb file.
};

static yasio::gc::object_pool<fsni_stream, std::recursive_mutex> s_fsni_pool;

extern "C" {
    void fsni_startup(const char* pszStreamingPath/*internal path*/, const char* pszPersistPath/*hot update path*/)
    {
        s_streamingPath = pszStreamingPath;
        s_persistPath = pszPersistPath;

        if (cxx20::starts_with(cxx17::string_view(s_streamingPath), APK_PREFIX))
        {
            auto endpos = s_streamingPath.find_last_of('!');
            if (endpos != std::string::npos) {
                std::string apkPath = s_streamingPath.substr(APK_PREFIX_LEN - 1, endpos - APK_PREFIX_LEN + 1);
                std::string strFilter = s_streamingPath.substr(endpos + 1);
                s_zipFile = new ZipFile(apkPath, strFilter);
                if (!s_zipFile->isOpen()) {
                    LOGD("fsni_startup ----> open %s failed, filter: %s", apkPath.c_str(), strFilter.c_str());
                    delete s_zipFile;
                    s_zipFile = nullptr;
                }
                else {
                    LOGD("fsni_startup ----> open %s succeed, filter: %s", apkPath.c_str(), strFilter.c_str());
                }
            }
        }
    }
    void fsni_cleanup()
    {
        s_streamingPath.clear();
        s_persistPath.clear();
        if (s_zipFile != nullptr) {
            delete s_zipFile;
            s_zipFile = nullptr;
        }
    }

    voidp fsni_open(const char* fileName)
    {
        union {
            voidp entry;
            int fd;
        };

        LOGD("fsni_open ----> %s", fileName);

        // try open from hot update path disk
        std::string fullPath = s_persistPath + fileName;
        fd = posix_open(fullPath.c_str(), O_READ_FLAGS);
        bool streaming = false;
        if (fd == FSNI_INVALID_FILE_HANDLE) {
            // try open from internal path
            if (s_zipFile != nullptr) { // android, from apk
                auto it = s_zipFile->m_data->fileList.find(fileName);
                if (it != s_zipFile->m_data->fileList.end()) {
                    entry = &it->second;
                    streaming = true;
                }
            }
            else { // ios, from disk
                fullPath = s_streamingPath + fileName;
                fd = posix_open(fullPath.c_str(), O_READ_FLAGS);
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
        }

        LOGD("fsni_open ----> %s failed!", fileName);
        return nullptr;
    }

    int fsni_read(voidp fp, voidp buf, int size)
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

                    int nRet = unzGoToFilePos(shared_data->zipFile, &entry->pos);
                    CC_BREAK_IF(UNZ_OK != nRet);

                    nRet = unzOpenCurrentFile(shared_data->zipFile);

                    if (nfs->offset > 0)
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

    int fsni_seek(voidp fp, int offset, int origin)
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

    void fsni_close(voidp fp)
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

    int fsni_getsize(voidp fp)
    {
        int size = fsni_seek(fp, 0, SEEK_END);
        fsni_seek(fp, 0, SEEK_SET);
        return size;
    }
}
