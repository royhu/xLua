/****************************************************************************
Copyright (c) 2010 cocos2d-x.org
Copyright (c) 2020 c4games.com

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
#include "ZipFile.h"

#include <zlib.h>
#include "unzip.h"
#include <unordered_map>
#include <mutex>

#include "yasio/cxx17/string_view.hpp"

#define FSNI_BREAK_IF(cond) if(cond) break

// MINIZIP v1.2 no unzGoToFirstFile64, unzGoToNextFile64
#define unzGoToFirstFile64(A,B,C,D) unzGoToFirstFile2(A,B,C,D, NULL, 0, NULL, 0)
#define unzGoToNextFile64(A,B,C,D) unzGoToNextFile2(A,B,C,D, NULL, 0, NULL, 0)

// --------------------- ZipFile ---------------------
// from unzip.cpp
#define UNZ_MAXFILENAMEINZIP 256

namespace fsni {
struct ZipEntryInfo
{
    unz64_file_pos pos;
    uint64_t uncompressed_size;
};

struct ZipFilePrivate
{
    unzFile unzfile = nullptr;
    std::mutex unzfileMtx;

    // std::unordered_map is faster if available on the platform
    typedef std::unordered_map<std::string, struct ZipEntryInfo> FileListContainer;
    FileListContainer fileList;
};

ZipFile::ZipFile()
{
}

ZipFile::~ZipFile()
{
    this->close();
}

/* open a zip file */
bool ZipFile::open(const std::string& filePath , const std::string& filter)
{
    if (_privateData) this->close();

    auto unzfile = unzOpen(filePath.c_str());
    if (unzfile)
    {
        _privateData = new ZipFilePrivate();
        _privateData->unzfile = unzfile;
        setFilter(filter);
    }
    return _privateData;
}

/* close zip file */
void ZipFile::close()
{
    if (_privateData)
    {
        if (_privateData->unzfile)
            unzClose(_privateData->unzfile);
        delete (_privateData);
        _privateData = nullptr;
    }
}

bool ZipFile::setFilter(const std::string& filter)
{
    bool ret = false;
    do
    {
        FSNI_BREAK_IF(!_privateData);
        FSNI_BREAK_IF(!_privateData->unzfile);

        // clear existing file list
        _privateData->fileList.clear();

        // UNZ_MAXFILENAMEINZIP + 1 - it is done so in unzLocateFile
        char szCurrentFileName[UNZ_MAXFILENAMEINZIP + 1];
        unz_file_info64 fileInfo;
        unz64_file_pos posInfo;

        // go through all files and store position information about the required files
        int err = unzGoToFirstFile64(_privateData->unzfile, &fileInfo,
            szCurrentFileName, sizeof(szCurrentFileName) - 1);
        while (err == UNZ_OK)
        {
            int posErr = unzGetFilePos64(_privateData->unzfile, &posInfo);
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
                    _privateData->fileList[currentFileName.substr(filter.size()).data()] = entry;
                }
            }

            // next file - also get the information about it
            err = unzGoToNextFile64(_privateData->unzfile, &fileInfo,
                szCurrentFileName, sizeof(szCurrentFileName) - 1);
        }
        ret = true;

    } while (false);

    return ret;
}

bool ZipFile::exists(const std::string& fileName) const
{
    return _privateData && _privateData->fileList.find(fileName) != _privateData->fileList.end();
}

ZipEntryInfo* ZipFile::vopen(const std::string& fileName) const
{
    if (_privateData) {
        auto it = _privateData->fileList.find(fileName);
        if (it != _privateData->fileList.end())
            return &it->second;
    }
    return nullptr;
}

int ZipFile::read(ZipEntryInfo* entry, size_t offset, void* buf, int size)
{
    int n = 0;
    do {
        FSNI_BREAK_IF(_privateData == nullptr || entry == nullptr);

        FSNI_BREAK_IF(offset >= entry->uncompressed_size);

        // protect the private data
        std::unique_lock<std::mutex> lck(_privateData->unzfileMtx);

        int nRet = unzGoToFilePos64(_privateData->unzfile, &entry->pos);
        FSNI_BREAK_IF(UNZ_OK != nRet);

        nRet = unzOpenCurrentFile(_privateData->unzfile);

        nRet = unzSeek64(_privateData->unzfile, offset, SEEK_SET);
        n = unzReadCurrentFile(_privateData->unzfile, buf, size);

        unzCloseCurrentFile(_privateData->unzfile);

    } while (false);

    return n;
}

uint64_t ZipFile::size(ZipEntryInfo* entry) {
    return entry->uncompressed_size;
}

}
