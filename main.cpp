#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>

#include <curl/curl.h>

#include "remote.hpp"

#define DOWNLOADMANAGER_SIGNATURE "\x55\x48\x8D\x3D\x00\x00\x00\x00\x48\x89\xE5\x5D\xE9\xBF\xFF\xFF\xFF"
#define DOWNLOADMANAGER_MASK "xxxx????xxxxxxxxx"

#define ENABLE_BZ2

using namespace std;

remote::Handle csgo;
remote::MapModuleMemoryRegion engine;
bool nowrite = false, nobz2 = false;
int afterDelay = 10;

inline bool file_exists(const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}
// https://stackoverflow.com/a/27172926/3303059
// I am not satisfied with this "solution" at all!
static int exec_prog(const char *prog, const char *arg1, const char *arg2) {
    pid_t   my_pid;
    int     status, timeout;

    if (0 == (my_pid = fork())) {
        if (-1 == execlp(prog, prog, arg1, arg2, NULL)) {
            perror("child process execve failed [%m]");
            return -1;
        }
    }
    timeout = 20000;

    while (0 == waitpid(my_pid, &status, WNOHANG)) {
        if ( --timeout < 0 ) {
            perror("timeout");
            return -1;
        }
        usleep(500);
    }

    return 0;
}
int run(int argc, char* argv[]) {
    if (getuid() != 0) {
        cout << "You should run this as root." << endl;
        return 0;
    }

    cout << "Waiting for csgo.";
    while (true) {
        if (remote::FindProcessByName("csgo_linux64", &csgo)) {
            break;
        }
        cout << ".";
        usleep(1000000);
    }

    cout << endl << "CSGO Process Located [" << csgo.GetPath() << "][" << csgo.GetPid() << "]" << endl;

    engine.start = 0;

    while (engine.start == 0) {
        if (!csgo.IsRunning()) {
            cout << "Exited game before engine could be located, terminating" << endl;
            return 0;
        }

        csgo.ParseMaps();

        for (auto region : csgo.regions) {
            if (region.filename.compare("engine_client.so") == 0 && region.executable) {
                cout << "engine_client.so: [" << std::hex << region.start << "][" << std::hex << region.end << "][" << region.pathname << "]" << endl;
                engine = region;
                break;
            }
        }

        usleep(500);
    }

    // or https://github.com/ericek111/java-csgo-externals/blob/wip/src/me/lixko/csgoexternals/offsets/Offsets.java#L193
    std::string moddir = csgo.GetPath().substr(0, csgo.GetPath().find_last_of("/\\")) + "/csgo";
    struct stat moddirstat;
    stat(moddir.c_str(), &moddirstat);

    unsigned long foundDownloadManagerMov = (long) engine.find(csgo, DOWNLOADMANAGER_SIGNATURE, DOWNLOADMANAGER_MASK) + 1;
    cout << ">>> found TheDownloadManager mov: 0x" << std::hex << foundDownloadManagerMov << endl;

    unsigned long downloadManager = csgo.GetAbsoluteAddress((void*)(foundDownloadManagerMov), 3, 7);
    cout << ">>> Address of TheDownloadManager: 0x" << std::hex << downloadManager << endl << endl;

    unsigned long m_activeRequest, m_queuedRequests_List, curReq;
    int m_queuedRequests_Count, one = 1, httpdone = 2, httperror = 4, m_activeRequest_status = httperror;
    bool bAsHTTP;
    char baseURLbuf[PATH_MAX], gamePathbuf[PATH_MAX];

    CURL *curl;
    FILE *fp;
    CURLcode res;
    char curlerr[CURL_ERROR_SIZE];
    curl = curl_easy_init();
    unsigned long responseCode;
    if(!curl) {
        cout << "Failed to initialize cURL!" << endl;
        exit(1);
    }

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerr);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
    // Error [22]: HTTP response code said error
    // The requested URL returned error: 404 Not Found
    

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3);
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Half-Life 2");

    while (csgo.IsRunning()) {
        sleep(1);
        csgo.Read((void*) (downloadManager + 4 * 8), &m_activeRequest, sizeof(unsigned long));
        if(!m_activeRequest)
            continue;

        csgo.Read((void*) downloadManager, &m_queuedRequests_List, sizeof(unsigned long));
        if(!m_queuedRequests_List)
            continue;

        csgo.Read((void*) (downloadManager + 2 * 8), &m_queuedRequests_Count, sizeof(unsigned long));

        void** requestList = (void**) malloc((m_queuedRequests_Count + 1) * sizeof(void*));
        *requestList = (void*) m_activeRequest;
        csgo.Read((void*) m_queuedRequests_List, requestList + 1, m_queuedRequests_Count * sizeof(void*));

        /*for (int i = 0; i < m_queuedRequests_Count+1; ++i) {
            curReq = (unsigned long)(requestList[i]);
            csgo.Read((void*) (curReq + 20), &baseURLbuf, sizeof(baseURLbuf));
            csgo.Read((void*) (curReq + 532), &gamePathbuf, sizeof(gamePathbuf));
            printf("#%d/%d: %s%s\n", i, m_queuedRequests_Count, baseURLbuf, gamePathbuf);
        }*/

        //cout << std::dec << m_queuedRequests_Count << endl;

        m_activeRequest_status = httperror;

        for (int i = 0; i < m_queuedRequests_Count+1; ++i) {
            csgo.Read((void*) (downloadManager + 4 * 8), &m_activeRequest, sizeof(unsigned long));
            if(!m_activeRequest)
                break;

            if(!nowrite)
                csgo.Write((void*) (m_activeRequest + 8), &httperror, sizeof(int));
            // Process the active request first, then proceed further. If we write status to the active request,
            // the vector gets shifted and we will skip some files.
            curReq = (unsigned long)(requestList[i]);

            csgo.Read((void*) (curReq + 3), &bAsHTTP, sizeof(bool));
            if(!bAsHTTP)
                continue;

            csgo.Read((void*) (curReq + 20), &baseURLbuf, sizeof(baseURLbuf));
            csgo.Read((void*) (curReq + 532), &gamePathbuf, sizeof(gamePathbuf));
            std::string baseURL(baseURLbuf);
            std::string gamePath(gamePathbuf);
            std::string downloadURL = baseURL + gamePath;
            std::string saveURL = moddir + "/" + gamePath;
            std::string gamePathWithBZ2 = gamePath + ".bz2";
            std::string downloadDir = saveURL.substr(0, saveURL.find_last_of("/\\"));

            if(file_exists(saveURL)) {
                cout << "#" << std::dec << i << " Skipping " << gamePath << endl;
                goto SKIPLOOP;
            }

            cout << "#" << std::dec << i << " Downloading " << downloadURL << endl;// << " to " << saveURL << endl;

            // might as well do it the ghetto way - we're on Linux, so this is available
            exec_prog("mkdir", "-p", downloadDir.c_str());
            fp = fopen(saveURL.c_str(), "wb");
            if(fp == NULL) {
                cout << "Failed to fopen " << saveURL << endl;
                //if(i >= 0)
                if(!nowrite)
                    csgo.Write((void*) (curReq + 8), &httperror, sizeof(int));
                continue;
            }

            curl_easy_setopt(curl, CURLOPT_URL, downloadURL.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
            fclose(fp);

            // HTTP/1.1 200 OK
            if(responseCode != 200 || res) {
                cout << "Error [" << dec << res << " / HTTP: " << responseCode << "]: " << curl_easy_strerror(res) << endl;
                if(strlen(curlerr))
                    cout << curlerr << endl;
                unlink(saveURL.c_str());
                goto SKIPLOOP;
            }
            
            if(!nobz2) {
                char *ext = strrchr(gamePathbuf, '.');
                if(ext && !strcmp(ext, ".bz2")) {
                    // The file is a BZ2 archive, we can unpack it and just skip the next download.
                    exec_prog("bzip2", "-dk", saveURL.c_str());
                }
            }

SKIPLOOP:
            if(!res) {
                chown(saveURL.c_str(), moddirstat.st_uid, moddirstat.st_gid);
            }

            if(!nowrite)
                csgo.Write((void*) (curReq + 8), &httpdone, sizeof(int));

        }

        free(requestList);
        if(nowrite) {
            sleep(afterDelay);
            continue;
        }

        // in case we're downloading faster than CSGO's updating the thread
        usleep(200000);
        csgo.Read((void*) (downloadManager + 2 * 8), &m_queuedRequests_Count, sizeof(unsigned long));
        // cout << m_queuedRequests_Count << endl;
        for (int i = 0; i < m_queuedRequests_Count + 1; ++i) {
            csgo.Read((void*) (downloadManager + 4 * 8), &m_activeRequest, sizeof(unsigned long));
            csgo.Write((void*) (m_activeRequest + 8), &httperror, sizeof(int));
            // cout << i << endl;
            usleep(50000);
        }

        /*while (m_queuedRequests_Count) {

            for (int i = 0; i < m_queuedRequests_Count; ++i) {
                curReq = (unsigned long)(requestList[i]);
                csgo.Write((void*) (curReq + 8), &httpdone, sizeof(int));
            }

            cout << m_queuedRequests_Count << endl;
            csgo.Read((void*) (downloadManager + 2 * 8), &m_queuedRequests_Count, sizeof(unsigned long));
            csgo.Read((void*) (downloadManager + 4 * 8), &m_activeRequest, sizeof(unsigned long));
            if(!m_activeRequest)
                break;
            csgo.Write((void*) (m_activeRequest + 8), &httpdone, sizeof(int));
            usleep(50000);
        }*/

        //csgo.Write((void*) (m_activeRequest + 8), &m_activeRequest_status, sizeof(int));
    }

    curl_easy_cleanup(curl);

    cout << "Game ended." << endl;
    return 0;
}

bool isNumber(char *str) {
    while(*str) {
        if(*str < '0' || *str > '9')
            return false;
        str++;
    }
    return true;
}
int main(int argc, char* argv[]) {
    if(argc > 1) {
        for (int i = 1; i < argc; i++) {
            if(!strcmp(argv[i], "?") || !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                cout << "CS:GO Download fixer by ericek111: https://github.com/ericek111/linux-csgo-downloadfixer" << endl;
                cout << "Syntax: ./csgo_downloadfixer [-nowrite [time]] [-nobz2]" << endl;
                exit(0);
            } else if(!strcmp(argv[i], "-nowrite")) {
                nowrite = true;
                cout << "Disabled memory writes." << endl;

                if(argc > i + 1 && isNumber(argv[i + 1])) {
                    afterDelay = atoi(argv[2]);
                    cout << "Set delay after download to " << afterDelay << " seconds." << endl;
                }
            } else if(!strcmp(argv[i], "-nobz2")) {
                //int ret = system("bzip2 --version > /dev/null 2>&1");
                nobz2 = false;
            }
        }
    }
    while (true) {
        run(argc, argv);
        sleep(10);
    }
}