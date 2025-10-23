#include "3ds/ndsp/ndsp.h"
#include "3ds/svc.h"
#include "3ds/types.h"
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
#define NUM_BUFFERS 3

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
        socExit();
        free(socBuffer);
        gfxExit();
        return 1;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("bind() failed\n");
        closesocket(listenSock);
        socExit();
        free(socBuffer);
        gfxExit();
        return 1;
    }

    if (listen(listenSock, 1) < 0) {
        printf("listen() failed\n");
        closesocket(listenSock);
        socExit();
        free(socBuffer);
        gfxExit();
        return 1;
    }

    printf("Waiting for client connection...\n");
    int clientSock = accept(listenSock, NULL, NULL);
    if (clientSock < 0) {
        printf("accept() failed\n");
        closesocket(listenSock);
        socExit();
        free(socBuffer);
        gfxExit();
        return 1;
    }
    printf("Client connected!\n");

    // Allocate audio buffers
    u8* audioBufs[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        audioBufs[i] = (u8*)linearAlloc(BUF_SIZE);
        if (!audioBufs[i]) {
            printf("Failed to allocate audio buffer %d\n", i);
            for (int j = 0; j < i; j++)
                linearFree(audioBufs[j]);
            closesocket(clientSock);
            closesocket(listenSock);
            socExit();
            free(socBuffer);
            gfxExit();
            return 1;
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

    int submittedSeq = 0;

    printf("Receiving audio...\n");

    ndspWaveBuf waveBuf;
    waveBuf.status = NDSP_WBUF_DONE;

    while (aptMainLoop()) {
        int playingSeq = ndspChnGetWaveBufSeq(0);
        int buffersQueued = submittedSeq - playingSeq;

        // Wait for a free buffer slot
        while (buffersQueued >= NUM_BUFFERS) {
            playingSeq = ndspChnGetWaveBufSeq(0);
            buffersQueued = submittedSeq - playingSeq;
        }

        int bufIndex = submittedSeq % NUM_BUFFERS;

        // Read exactly BUF_SIZE bytes
        ssize_t received = 0;
        while (received < BUF_SIZE) {
            ssize_t ret = recv(clientSock, audioBufs[bufIndex] + received, BUF_SIZE - received, 0);
            if (ret == 0) {
                printf("Connection closed by client\n");
                goto cleanup;
            }
            if (ret < 0) {
                printf("recv() error\n");
                goto cleanup;
            }
            received += ret;
        }
        // svcSleepThread(1 * 1000 * 1000 * 1000ULL);
        while (waveBuf.status != NDSP_WBUF_DONE) {
            svcSleepThread(1 * 10 * 1000ULL);
        }

        memset(&waveBuf, 0, sizeof(waveBuf));
        waveBuf.data_vaddr = audioBufs[bufIndex];
        waveBuf.nsamples = BUF_SIZE / 4; // 4 bytes per sample frame (stereo 16-bit)
        waveBuf.looping = false;
        waveBuf.status = NDSP_WBUF_DONE;

        DSP_FlushDataCache(audioBufs[bufIndex], BUF_SIZE);
        ndspChnWaveBufAdd(0, &waveBuf);

        submittedSeq++;

        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
    }

cleanup:
    printf("Cleaning up...\n");

    ndspExit();

    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (audioBufs[i])
            linearFree(audioBufs[i]);
    }

    closesocket(clientSock);
    closesocket(listenSock);
    socExit();
    free(socBuffer);
    gfxExit();

    return 0;
}