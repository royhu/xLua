// fsni test 2019.12.15 r1
#include <string>

#define voidp void*

extern "C" {
    void fsni_startup(const char* pszStreamingPath/*internal path*/, const char* pszPersistPath/*hot update path*/);
    void fsni_cleanup();
    voidp fsni_open(const char* fileName);
    int fsni_read(voidp fp, voidp buf, int size);
    int fsni_seek(voidp fp, int offset, int origin);
    void fsni_close(voidp fp);
    int fsni_getsize(voidp fp);
}

int main(int, char**) 
{
    std::string content;
    fsni_startup(R"(jar:file://D:\dev\projects\base.apk!/assets/)", "");

    voidp fp = fsni_open("Main.lua");
    if (fp) {
        content.resize(fsni_getsize(fp));
        fsni_read(fp, &content.front(), content.size());
        fsni_close(fp);
    }

    fp = fsni_open("extern.lua");
    if (fp) {
        content.resize(fsni_getsize(fp));
        fsni_read(fp, &content.front(), content.size());
        fsni_close(fp);
    }

    fsni_cleanup();

    return 0;
}
