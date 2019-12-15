// fsni test 2019.12.15 r1
#include <string>

#define voidp void*

extern "C" {
    void fsni_startup(const char* pszStreamingPath/*internal path*/, const char* pszPersistPath/*external storage path*/);
    void fsni_cleanup();
    voidp fsni_open(const char* fileName);
    int fsni_read(voidp fp, voidp buf, int size);
    int fsni_seek(voidp fp, int offset, int origin);
    void fsni_close(voidp fp);
    int fsni_getsize(voidp fp);
}

static std::string fsni_get_file_content(const char* filename)
{
    std::string content;
    voidp fp = fsni_open(filename);
    if (fp) {
        auto filesize = fsni_getsize(fp);
        if (filesize > 0) {
            content.resize(filesize);
            fsni_read(fp, &content.front(), content.size());
        }
        fsni_close(fp);
    }
    return content;
}

int main(int, char**) 
{
    fsni_startup(R"(jar:file://r.zip!/assets/)", "");

    std::string content1 = fsni_get_file_content("settings.xml");
    std::string content2 = fsni_get_file_content("EditorKeyMap.lua");

    fsni_cleanup();

    return 0;
}
