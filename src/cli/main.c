/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/console.h>
#include <flood/in_stream.h>
#include <flood/out_stream.h>
#include <flood/text_in_stream.h>
#include <guise-client/client.h>
#include <guise-client/network_realizer.h>
#include <guise-serialize/parse_text.h>
#include <imprint/default_setup.h>
#include <inttypes.h>
#include <relay-client-transport/realizer.h>
#include <relay-client/client.h>
#include <stdio.h>
#include <time.h>
#include <udp-client/udp_client.h>

clog_config g_clog;
char g_clog_temp_str[CLOG_TEMP_STR_SIZE];

static ssize_t clientReceive(void* _self, uint8_t* data, size_t size)
{
    UdpClientSocket* self = _self;

    return udpClientReceive(self, data, size);
}

static int clientSend(void* _self, const uint8_t* data, size_t size)
{
    UdpClientSocket* self = _self;

    return udpClientSend(self, data, size);
}

typedef struct Secret {
    GuiseSerializeUserId userId;
    uint64_t passwordHash;
} Secret;

static int readSecret(Secret* secret)
{
    CLOG_DEBUG("reading secret file")
    FILE* fp = fopen("secret.txt", "r");
    if (fp == 0) {
        CLOG_ERROR("could not find secret.txt")
        //        return -4;
    }

    char line[1024];
    char* ptr = fgets(line, 1024, fp);
    if (ptr == 0) {
        return -39;
    }
    fclose(fp);

    FldTextInStream textInStream;
    FldInStream inStream;

    fldInStreamInit(&inStream, (const uint8_t*) line, tc_strlen(line));
    fldTextInStreamInit(&textInStream, &inStream);

    GuiseSerializeUserInfo userInfo;

    int err = guiseTextStreamReadUser(&textInStream, &userInfo);
    if (err < 0) {
        return err;
    }

    secret->userId = userInfo.userId;
    secret->passwordHash = userInfo.passwordHash;

    return 0;
}

int main(int argc, char* argv[])
{
    (void) argc;
    (void) argv;

    g_clog.log = clog_console;
    g_clog.level = CLOG_TYPE_VERBOSE;

    CLOG_INFO("relay client transport cli")

    FldOutStream outStream;

    uint8_t buf[1024];
    fldOutStreamInit(&outStream, buf, 1024);

    ImprintDefaultSetup memory;

    DatagramTransport guiseTransport;

    imprintDefaultSetupInit(&memory, 16 * 1024 * 1024);

    int startupErr = udpClientStartup();
    if (startupErr < 0) {
        return startupErr;
    }

    const char* hostToConnectTo = "127.0.0.1";

    if (argc > 1) {
        hostToConnectTo = argv[1];
    }

    UdpClientSocket guiseUdpSocket;
    udpClientInit(&guiseUdpSocket, hostToConnectTo, 27004);
    guiseTransport.self = &guiseUdpSocket;
    guiseTransport.receive = clientReceive;
    guiseTransport.send = clientSend;

    UdpClientSocket relayUdpSocket;
    udpClientInit(&relayUdpSocket, hostToConnectTo, 27005);
    DatagramTransport relayTransport;
    relayTransport.self = &relayUdpSocket;
    relayTransport.receive = clientReceive;
    relayTransport.send = clientSend;

    Secret secret;
    int secretErr = readSecret(&secret);
    if (secretErr < 0) {
        CLOG_SOFT_ERROR("could not read lines from secret.txt %d", secretErr)
        return secretErr;
    }

    Clog guiseClientLog;
    guiseClientLog.config = &g_clog;
    guiseClientLog.constantPrefix = "GuiseClient";

    RelayClientTransportRealizer clientRealize;

    relayClientTransportRealizerInit(&clientRealize, &memory.tagAllocator.info, &guiseTransport, &relayTransport,
                                     guiseClientLog);
    relayClientTransportRealizerReInit(&clientRealize, secret.userId, secret.passwordHash);

    bool hasStartedListen = false;
    while (true) {
        struct timespec ts;

        ts.tv_sec = 0;
        ts.tv_nsec = 16 * 1000000;
        nanosleep(&ts, &ts);

        MonotonicTimeMs now = monotonicTimeMsNow();
        relayClientTransportRealizerUpdate(&clientRealize, now);
        if (clientRealize.state == RelayClientTransportRealizerAuthenticated) {
            if (!hasStartedListen) {
                RelayListener* listener = relayClientTransportRealizerStartListen(&clientRealize, 42, 0);
                if (listener == 0) {
                    return -99;
                }
                hasStartedListen = true;
            }
        }
    }

    //    imprintDefaultSetupDestroy(&memory);
}
