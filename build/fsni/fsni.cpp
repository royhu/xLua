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

#define LUA_LIB

#include "fsni.h"
#include "unzip.h"
#include <map>
#include <thread>
#include <mutex>

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

// --------------------- ZipUtils ---------------------

// memory in iPhone is precious
// Should buffer factor be 1.5 instead of 2 ?
#define BUFFER_INC_FACTOR (2)

int ZipUtils::ccInflateMemoryWithHint(unsigned char* in, unsigned int inLength, unsigned char** out, unsigned int* outLength, unsigned int outLenghtHint)
{
	/* ret value */
	int err = Z_OK;

	int bufferSize = outLenghtHint;
	*out = new unsigned char[bufferSize];

	z_stream d_stream; /* decompression stream */
	d_stream.zalloc = (alloc_func)0;
	d_stream.zfree = (free_func)0;
	d_stream.opaque = (voidpf)0;

	d_stream.next_in = in;
	d_stream.avail_in = inLength;
	d_stream.next_out = *out;
	d_stream.avail_out = bufferSize;

	/* window size to hold 256k */
	if ((err = inflateInit2(&d_stream, 15 + 32)) != Z_OK)
		return err;

	for (;;)
	{
		err = inflate(&d_stream, Z_NO_FLUSH);

		if (err == Z_STREAM_END)
		{
			break;
		}

		switch (err)
		{
		case Z_NEED_DICT:
			err = Z_DATA_ERROR;
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			inflateEnd(&d_stream);
			return err;
		}

		// not enough memory ?
		if (err != Z_STREAM_END)
		{

			auto tmp = *out;
			*out = new unsigned char[bufferSize * BUFFER_INC_FACTOR];
			memcpy(*out, tmp, bufferSize);

			delete[]  tmp;

			/* not enough memory, ouch */
			if (!*out)
			{
				// CCLOG("cocos2d: ZipUtils: realloc failed");
				inflateEnd(&d_stream);
				return Z_MEM_ERROR;
			}

			d_stream.next_out = *out + bufferSize;
			d_stream.avail_out = bufferSize;
			bufferSize *= BUFFER_INC_FACTOR;
		}
	}

	*outLength = bufferSize - d_stream.avail_out;
	err = inflateEnd(&d_stream);
	return err;
}

int ZipUtils::ccInflateMemoryWithHint(unsigned char* in, unsigned int inLength, unsigned char** out, unsigned int outLengthHint)
{
	unsigned int outLength = 0;
	int err = ccInflateMemoryWithHint(in, inLength, out, &outLength, outLengthHint);

	if (err != Z_OK || *out == NULL) {
		if (err == Z_MEM_ERROR)
			/*{
				CCLOG("cocos2d: ZipUtils: Out of memory while decompressing map data!");
			} else
			if (err == Z_VERSION_ERROR)
			{
				CCLOG("cocos2d: ZipUtils: Incompatible zlib version!");
			} else
			if (err == Z_DATA_ERROR)
			{
				CCLOG("cocos2d: ZipUtils: Incorrect zlib compressed data!");
			}
			else
			{
				CCLOG("cocos2d: ZipUtils: Unknown error while decompressing map data!");
			}*/

			delete[] * out;
		*out = NULL;
		outLength = 0;
	}

	return outLength;
}

int ZipUtils::ccInflateMemory(unsigned char* in, unsigned int inLength, unsigned char** out)
{
	// 256k for hint
	return ccInflateMemoryWithHint(in, inLength, out, 256 * 1024);
}

int ZipUtils::ccInflateGZipFile(const char* path, unsigned char** out)
{
	int len;
	unsigned int offset = 0;

	//CCAssert(out, "");
	//CCAssert(&*out, "");

	gzFile inFile = gzopen(path, "rb");
	if (inFile == NULL) {
		//CCLOG("cocos2d: ZipUtils: error open gzip file: %s", path);
		return -1;
	}

	/* 512k initial decompress buffer */
	unsigned int bufferSize = 512 * 1024;
	unsigned int totalBufferSize = bufferSize;

	*out = (unsigned char*)malloc(bufferSize);
	if (!out)
	{
		//CCLOG("cocos2d: ZipUtils: out of memory");
		return -1;
	}

	for (;;) {
		len = gzread(inFile, *out + offset, bufferSize);
		if (len < 0)
		{
			//CCLOG("cocos2d: ZipUtils: error in gzread");
			free(*out);
			*out = NULL;
			return -1;
		}
		if (len == 0)
		{
			break;
		}

		offset += len;

		// finish reading the file
		if ((unsigned int)len < bufferSize)
		{
			break;
		}

		bufferSize *= BUFFER_INC_FACTOR;
		totalBufferSize += bufferSize;
		unsigned char* tmp = (unsigned char*)realloc(*out, totalBufferSize);

		if (!tmp)
		{
			//CCLOG("cocos2d: ZipUtils: out of memory");
			free(*out);
			*out = NULL;
			return -1;
		}

		*out = tmp;
	}

	if (gzclose(inFile) != Z_OK)
	{
		//CCLOG("cocos2d: ZipUtils: gzclose failed");
	}

	return offset;
}

int ZipUtils::ccInflateCCZFile(const char* path, unsigned char** out)
{
	//CCAssert(out, "");
	//CCAssert(&*out, "");

	// load file into memory
	//auto compressed = CCFileUtils::sharedFileUtils()->getDataFromFile(path);

	//return ccInflateCCZData(compressed.getBytes(), compressed.getSize(), out);

	return 0;
}

int ZipUtils::ccInflateCCZData(const unsigned char* compressed, unsigned long fileLen, unsigned char** out)
{
#if 0
	if (NULL == compressed || 0 == fileLen)
	{
		CCLOG("cocos2d: Error loading CCZ compressed file");
		return -1;
	}

	struct CCZHeader* header = (struct CCZHeader*) compressed;

	// verify header
	if (header->sig[0] != 'C' || header->sig[1] != 'C' || header->sig[2] != 'Z' || header->sig[3] != '!')
	{
		CCLOG("cocos2d: Invalid CCZ file");
		// delete[] compressed;
		return -1;
	}

	// verify header version
	unsigned int version = CC_SWAP_INT16_BIG_TO_HOST(header->version);
	if (version > 2)
	{
		CCLOG("cocos2d: Unsupported CCZ header format");
		// delete[] compressed;
		return -1;
	}

	// verify compression format
	if (CC_SWAP_INT16_BIG_TO_HOST(header->compression_type) != CCZ_COMPRESSION_ZLIB)
	{
		CCLOG("cocos2d: CCZ Unsupported compression method");
		// delete[] compressed;
		return -1;
	}

	unsigned int len = CC_SWAP_INT32_BIG_TO_HOST(header->len);

	*out = (unsigned char*)malloc(len);
	if (!*out)
	{
		CCLOG("cocos2d: CCZ: Failed to allocate memory for texture");
		// delete[] compressed;
		return -1;
	}


	unsigned long destlen = len;
	unsigned long source = (unsigned long)compressed + sizeof(*header);
	int ret = uncompress(*out, &destlen, (Bytef*)source, fileLen - sizeof(*header));

	// delete[] compressed;

	if (ret != Z_OK)
	{
		CCLOG("cocos2d: CCZ: Failed to uncompress data");
		free(*out);
		*out = NULL;
		return -1;
	}

	return len;
#endif
	return 0;
}


// --------------------- ZipFile ---------------------
// from unzip.cpp
#define UNZ_MAXFILENAMEINZIP 256

struct ZipEntryInfo
{
	unz_file_pos pos;
	uLong uncompressed_size;
};

struct UnzFileStream {
	ZipEntryInfo* entry;
	long offset;
};

class ZipFilePrivate
{
public:
	unzFile zipFile;
	std::mutex zipFileMtx;

	// std::unordered_map is faster if available on the platform
	typedef std::map<std::string, struct ZipEntryInfo> FileListContainer;
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

		// go through all files and store position information about the required files
		int err = unzGoToFirstFile64(m_data->zipFile, &fileInfo,
			szCurrentFileName, sizeof(szCurrentFileName) - 1);
		while (err == UNZ_OK)
		{
			unz_file_pos posInfo;
			int posErr = unzGetFilePos(m_data->zipFile, &posInfo);
			if (posErr == UNZ_OK)
			{
				std::string currentFileName = szCurrentFileName;
				// cache info about filtered files only (like 'assets/')
				if (filter.empty()
					|| currentFileName.substr(0, filter.length()) == filter)
				{
					ZipEntryInfo entry;
					entry.pos = posInfo;
					entry.uncompressed_size = (uLong)fileInfo.uncompressed_size;
					m_data->fileList[currentFileName] = entry;
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

unsigned char* ZipFile::getFileData(const std::string& fileName, unsigned long* pSize)
{
	unsigned char* pBuffer = NULL;
	if (pSize)
	{
		*pSize = 0;
	}

	do
	{
		CC_BREAK_IF(!m_data->zipFile);
		CC_BREAK_IF(fileName.empty());

		ZipFilePrivate::FileListContainer::const_iterator it = m_data->fileList.find(fileName);
		CC_BREAK_IF(it == m_data->fileList.end());

		const ZipEntryInfo& fileInfo = it->second;

		std::unique_lock<std::mutex> lck(m_data->zipFileMtx);
		int nRet = unzGoToFilePos(m_data->zipFile, &fileInfo.pos);
		CC_BREAK_IF(UNZ_OK != nRet);

		nRet = unzOpenCurrentFile(m_data->zipFile);
		CC_BREAK_IF(UNZ_OK != nRet);

		pBuffer = new unsigned char[fileInfo.uncompressed_size];
		int CC_UNUSED nSize = unzReadCurrentFile(m_data->zipFile, pBuffer, fileInfo.uncompressed_size);
		// CCAssert(nSize == 0 || nSize == (int)fileInfo.uncompressed_size, "the file size is wrong");

		if (pSize)
		{
			*pSize = fileInfo.uncompressed_size;
		}
		unzCloseCurrentFile(m_data->zipFile);
	} while (0);

	return pBuffer;
}

#if 0
bool ZipFile::getFileData(const std::string & fileName, ResizableBuffer * buffer)
{
	bool bRet = false;
	do
	{
		CC_BREAK_IF(!m_data->zipFile);
		CC_BREAK_IF(fileName.empty());

		ZipFilePrivate::FileListContainer::const_iterator it = m_data->fileList.find(fileName);
		CC_BREAK_IF(it == m_data->fileList.end());

		const ZipEntryInfo& fileInfo = it->second;

		std::unique_lock<std::mutex> lck(m_data->zipFileMtx);
		int nRet = unzGoToFilePos(m_data->zipFile, &fileInfo.pos);
		CC_BREAK_IF(UNZ_OK != nRet);

		nRet = unzOpenCurrentFile(m_data->zipFile);
		CC_BREAK_IF(UNZ_OK != nRet);

		buffer->resize(fileInfo.uncompressed_size);
		int CC_UNUSED nSize = unzReadCurrentFile(m_data->zipFile, buffer->buffer(), fileInfo.uncompressed_size);
		CCAssert(nSize == 0 || nSize == (int)fileInfo.uncompressed_size, "the file size is wrong");

		unzCloseCurrentFile(m_data->zipFile);
		bRet = true;
	} while (0);

	return bRet;
}
#endif

UnzFileStream* ZipFile::uzfsOpen(const std::string& fileName)
{
	auto it = m_data->fileList.find(fileName);
	if (it != m_data->fileList.end()) {
		auto uzfs = (UnzFileStream*)calloc(1, sizeof(UnzFileStream));
		if (uzfs != nullptr) {
			uzfs->entry = &it->second;
		}
		return uzfs;
	}
	return nullptr;
}
int ZipFile::uzfsRead(UnzFileStream* uzfs, void* buf, unsigned int size)
{
	int n = 0;
	do {
		CC_BREAK_IF(uzfs == nullptr || uzfs->offset >= uzfs->entry->uncompressed_size);

		std::unique_lock<std::mutex> lck(m_data->zipFileMtx);

		int nRet = unzGoToFilePos(m_data->zipFile, &uzfs->entry->pos);
		CC_BREAK_IF(UNZ_OK != nRet);

		nRet = unzOpenCurrentFile(m_data->zipFile);

		nRet = unzSeek64(m_data->zipFile, uzfs->offset, SEEK_SET);
		n = unzReadCurrentFile(m_data->zipFile, buf, size);
		if (n > 0) {
			uzfs->offset += n;
		}

		unzCloseCurrentFile(m_data->zipFile);

	} while (false);

	return n;
}
long ZipFile::uzfsSeek(UnzFileStream* uzfs, long offset, int origin)
{
	long result = -1;
	if (uzfs != nullptr) {
		switch (origin) {
		case SEEK_SET:
			result = offset;
			break;
		case SEEK_CUR:
			result = uzfs->offset + offset;
			break;
		case SEEK_END:
			result = (long)uzfs->entry->uncompressed_size + offset;
			break;
		default:;
		}

		if (result >= 0) {
			uzfs->offset = result;
		}
		else
			result = -1;
	}

	return result;
}
void ZipFile::uzfsClose(UnzFileStream* uzfs)
{
	if (uzfs != nullptr) {
		free(uzfs);
	}
}

// -------------------- fsni ---------------------

static std::string s_streamingPath, s_persistPath;
static ZipFile* s_zipFile = nullptr;
#define FSNI_INVALID_FILE_HANDLE (void*)-1
struct fsni_stream {
	voidp entry;
	long offset;
	bool streaming; // whether in apk or obb file.
};

extern "C" {
	void fsni_init(const char* pszStreamingPath/*internal path*/, const char* pszPersistPath/*hot update path*/)
	{
		s_streamingPath = pszStreamingPath;
		s_persistPath = pszPersistPath;

		// finally, uncomment
	// #if defined(ANDROID)
		s_zipFile = new ZipFile(pszStreamingPath);
		if (!s_zipFile->isOpen()) {
			delete s_zipFile;
			s_zipFile = nullptr;
		}
		// #endif
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
		// try open from hot update path disk
		std::string fullPath = s_persistPath + fileName;
		auto entry = (void*)posix_open(fullPath.c_str(), O_READ_FLAGS);
		bool streaming = false;
		if (entry == FSNI_INVALID_FILE_HANDLE) {
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
				entry = (void*)posix_open(fullPath.c_str(), O_READ_FLAGS);
			}
		}

		if ((!streaming && entry != FSNI_INVALID_FILE_HANDLE) || (streaming && entry != nullptr)) {
			fsni_stream* f = (fsni_stream*)calloc(1, sizeof(fsni_stream));
			if (f != nullptr) {
				f->entry = entry;
				f->streaming = streaming;
				return f;
			}
		}
		return nullptr;
	}

	int fsni_read(voidp fp, voidp buf, int size)
	{
		fsni_stream* nfs = (fsni_stream*)fp;
		if (nfs != nullptr) {
			if (!nfs->streaming) {
				if (nfs->entry != FSNI_INVALID_FILE_HANDLE)
					return posix_read((int)nfs->entry, buf, size);
			}
			else {
				int n = -1;
				do {
					auto entry = (ZipEntryInfo*)nfs->entry;
					CC_BREAK_IF(entry == nullptr);

					auto shared_data = s_zipFile->m_data;
					CC_BREAK_IF(nfs->offset >= entry->uncompressed_size);

					std::unique_lock<std::mutex> lck(shared_data->zipFileMtx);

					int nRet = unzGoToFilePos(shared_data->zipFile, &entry->pos);
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

		return -1;
	}

	int fsni_seek(voidp fp, int offset, int origin)
	{
		fsni_stream* nfs = (fsni_stream*)fp;
		if (nfs != nullptr) {
			if (!nfs->streaming) {
				if (nfs->entry != FSNI_INVALID_FILE_HANDLE)
					return posix_lseek((int)nfs->entry, offset, origin);
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
				if (nfs->entry != FSNI_INVALID_FILE_HANDLE)
					posix_close((int)nfs->entry);
			}
			free(nfs);
		}
	}

	int fsni_getsize(voidp fp)
	{
		int size = fsni_seek(fp, 0, SEEK_END);
		fsni_seek(fp, 0, SEEK_SET);
		return size;
	}
}
