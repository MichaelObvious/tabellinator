#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define CFLAGS "-Wall", "-Wextra"
#define LIBS "-lm"

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    Cstr tool_path = PATH("./main.c");
    #ifndef _WIN32
        CMD("cc", CFLAGS, "-o", "tabellinator", "main.c", LIBS);
    #else
        // CMD("cl.exe", "main.c");
    #endif

    return 0;
}