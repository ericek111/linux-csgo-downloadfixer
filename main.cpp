#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>
#include <libgen.h>
#include <curl/curl.h>

#include "remote.hpp"

#define DOWNLOADMANAGER_SIGNATURE "\x55\x48\x8D\x3D\x00\x00\x00\x00\x48\x89\xE5\x5D\xE9\xBF\xFF\xFF\xFF"
#define DOWNLOADMANAGER_MASK "xxxx????xxxxxxxxx"

using namespace std;

remote::Handle csgo;
remote::MapModuleMemoryRegion engine;
bool nowrite = false;

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

    cout << endl << "CSGO Process Located [" << csgo.GetPath() << "][" << csgo.GetPid() << "]" << endl << endl;

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

    unsigned long foundDownloadManagerMov = (long) engine.find(csgo, DOWNLOADMANAGER_SIGNATURE, DOWNLOADMANAGER_MASK) + 1;
    cout << ">>> found TheDownloadManager mov: 0x" << std::hex << foundDownloadManagerMov << endl << endl;

    unsigned long downloadManager = csgo.GetAbsoluteAddress((void*)(foundDownloadManagerMov), 3, 7);
    cout << ">>> Address of TheDownloadManager: 0x" << std::hex << downloadManager << endl << endl;

    unsigned long m_activeRequest, m_queuedRequests_List, curReq;
    int m_queuedRequests_Count, one = 1, httpdone = 2, httperror = 4, m_activeRequest_status = httperror;
    bool bAsHTTP;
    char baseURLbuf[PATH_MAX], gamePathbuf[PATH_MAX];

    CURL *curl;
    FILE *fp;
    CURLcode res;
    curl = curl_easy_init();
    unsigned long responseCode;
    if(!curl) {
        cout << "Failed to initialize cURL!" << endl;
        exit(1);
    }

    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

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
            std::string downloadDir = saveURL.substr(0, saveURL.find_last_of("/\\"));

            cout << "#" << std::dec << i << " / " << " Downloading " << downloadURL << " to " << saveURL << endl;
            const char* mkcmd = (std::string("mkdir -p '") + downloadDir + "'").c_str();
            cout << mkcmd << endl;

            // might as well do it the ghetto way - we're on Linux, so this is available
            system(mkcmd);

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
            fclose(fp);
            if(!nowrite)
                csgo.Write((void*) (curReq + 8), &httpdone, sizeof(int));
        }

        free(requestList);
        if(nowrite) continue;

        // in case we're downloading faster than CSGO's updating the thread
        usleep(200000);
        csgo.Read((void*) (downloadManager + 2 * 8), &m_queuedRequests_Count, sizeof(unsigned long));
        cout << m_queuedRequests_Count << endl;
        for (int i = 0; i < m_queuedRequests_Count + 1; ++i) {
            csgo.Read((void*) (downloadManager + 4 * 8), &m_activeRequest, sizeof(unsigned long));
            csgo.Write((void*) (m_activeRequest + 8), &httperror, sizeof(int));
            cout << i << endl;
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

int main(int argc, char* argv[]) {
    if(argc > 1) {
        for (int i = 1; i < argc; i++) {
            if(string(argv[i]) == string("-nowrite")) {
                nowrite = true;
            }
        }
    }
    while (true) {
        run(argc, argv);
        sleep(10);
    }
}