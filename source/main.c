#include "3ds/ndsp/ndsp.h"
#include "3ds/services/hid.h"
#include "3ds/svc.h"
#include "3ds/types.h"
#include "netinet/in.h"
#include <3ds.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 9999
#define BUF_SIZE 4096
#define NUM_BUFFERS 6

int main()
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("TCP audio receiver\n");
    printf("Listening on port %d...\n", PORT);

    // Allocate and initialize soc buffer
    u32* socBuffer = (u32*)memalign(0x1000, 0x100000);
    if (!socBuffer) {
        printf("Failed to allocate socBuffer\n");
        return 1;
    }
    if (R_FAILED(socInit(socBuffer, 0x100000))) {
        printf("socInit failed\n");
        free(socBuffer);
        return 1;
    }

    // Create TCP listening socket
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        printf("socket() failed\n");
        goto exit_soc;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("bind() failed\n");
        goto exit_socket;
    }

    if (listen(listenSock, 1) < 0) {
        printf("listen() failed\n");
        goto exit_socket;
    }

    printf("Waiting for client connection...\n");
    int clientSock = accept(listenSock, NULL, NULL);
    if (clientSock < 0) {
        printf("accept() failed\n");
        goto exit_socket;
    }

    printf("Client connected!\n");

    // Configure socket
    // int flag = 1;
    // setsockopt(clientSock, IPPROTO_TCP, TCP_, &flag, sizeof(flag));
    int recvBufSize = 64 * 1024;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVBUF, &recvBufSize, sizeof(recvBufSize));

    // Allocate audio buffers
    u8* audioBufs[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        audioBufs[i] = (u8*)linearAlloc(BUF_SIZE);
        if (!audioBufs[i]) {
            printf("Failed to allocate audio buffer %d\n", i);
            for (int j = 0; j < i; j++)
                linearFree(audioBufs[j]);
            goto exit_client;
        }
        memset(audioBufs[i], 0, BUF_SIZE);
    }

    // Initialize NDSP
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, 22050.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    ndspWaveBuf waveBufs[NUM_BUFFERS];
    memset(waveBufs, 0, sizeof(waveBufs));

    printf("Receiving audio...\n");

    int submittedSeq = 0;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;

        int queued = submittedSeq - ndspChnGetWaveBufSeq(0);
        if (queued >= NUM_BUFFERS)
            continue;

        int bufIndex = submittedSeq % NUM_BUFFERS;
        ndspWaveBuf* wb = &waveBufs[bufIndex];

        // Wait until NDSP is done with this buffer
        if (wb->status != NDSP_WBUF_DONE && wb->status != 0) {
            svcSleepThread(1 * 1000 * 1000ULL); // ~1ms
            continue;
        }

        // Receive exactly BUF_SIZE bytes
        ssize_t ret = recv(clientSock, audioBufs[bufIndex], BUF_SIZE, MSG_WAITALL);
        if (ret <= 0) {
            printf("Connection closed or recv() failed\n");
            break;
        }

        // Prepare and queue wave buffer
        DSP_FlushDataCache(audioBufs[bufIndex], BUF_SIZE);
        memset(wb, 0, sizeof(*wb));
        wb->data_vaddr = audioBufs[bufIndex];
        wb->nsamples = BUF_SIZE / 4; // stereo 16-bit = 4 bytes per frame
        wb->looping = false;

        ndspChnWaveBufAdd(0, wb);
        submittedSeq++;

        if (submittedSeq % NUM_BUFFERS == 0)
            submittedSeq = 0;
    }

    printf("Cleaning up...\n");

    // --- CLEANUP ---
    ndspExit();

    for (int i = 0; i < NUM_BUFFERS; i++)
        if (audioBufs[i])
            linearFree(audioBufs[i]);

exit_client:
    closesocket(clientSock);
exit_socket:
    closesocket(listenSock);
exit_soc:
    socExit();
    free(socBuffer);
    gfxExit();

    return 0;
}