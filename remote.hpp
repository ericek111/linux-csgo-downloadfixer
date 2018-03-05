#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <sys/uio.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>

namespace remote {
    class Handle;

    struct MapModuleMemoryRegion {
    public:
        // Memory
        unsigned long start;
        unsigned long end;

        // Permissions
        bool readable;
        bool writable;
        bool executable;
        bool shared;

        // File data
        unsigned long offset;
        unsigned char deviceMajor;
        unsigned char deviceMinor;
        unsigned long inodeFileNumber;
        std::string pathname;
        std::string filename;

        unsigned long client_start;

        void* find(Handle handle, const char* data, const char* pattern);
    };

    class Handle {
    public:
        unsigned long addressOfClientModule;
        Handle() : pid(-1) {}
        Handle(pid_t target);
        Handle(std::string target);

        std::string GetPath();
        std::string GetWorkingDirectory();
        pid_t GetPid(){ return pid; }

        void ParseMaps();

        bool IsValid();
        bool IsRunning();

        bool Write(void* address, void* buffer, size_t size);
        bool Read(void* address, void* buffer, size_t size);

        unsigned long GetCallAddress(void* address);
        unsigned long GetAbsoluteAddress(void* address, int offset, int size);

        MapModuleMemoryRegion* GetRegionOfAddress(void* address);

    private:
        std::string GetSymbolicLinkTarget(std::string target);

        pid_t pid;
        std::string pidStr;
        //FILE* memr;
        //FILE* memw;

    public:
        std::vector<MapModuleMemoryRegion> regions;
    };

    bool FindProcessByName(std::string name, Handle* out);
};