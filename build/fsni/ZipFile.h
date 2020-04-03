/****************************************************************************
Copyright (c) 2010 cocos2d-x.org
Copyright (c) 2020 c4games.com

Inspired from: https://github.com/cocos2d/cocos2d-x/blob/v4/cocos/base/ZipUtils.h
and do some imporves:
    1). thread safe
    2). streaming support
 
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
#ifndef _ZIPFILE_H_
#define _ZIPFILE_H_

#include <stdint.h>
#include <string>

namespace fsni {
// forward declaration
struct ZipFilePrivate;
struct ZipEntryInfo;

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
    ZipFile();
    virtual ~ZipFile();

    /* open a zip file */
    bool open(const std::string& zipFile, const std::string& filter = "");

    /* close zip file */
    void close();

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
    bool exists(const std::string& fileName) const;

    /**
    * The virtual open, query from entry map only
    *
    */
    ZipEntryInfo* vopen(const std::string& fileName) const;

    /**
    * Read entry data
    */
    int read(ZipEntryInfo* entry, size_t offset, void* buf, int size);

    /**
    * Gets size of entry file
    */
    static uint64_t size(ZipEntryInfo* entry);

    // Safe bool conversion operator
    explicit operator bool() const { return _privateData; }

public:
    /** Internal data like zip file pointer / file list array and so on */
    ZipFilePrivate* _privateData = nullptr;
};
} // namespace fsni

#endif

