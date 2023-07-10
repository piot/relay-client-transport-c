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
#include <relay-client/client.h>
#include <stdio.h>
#include <udp-client/udp_client.h>
#include <time.h>

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

    CLOG_VERBOSE("example start")
    CLOG_VERBOSE("initialized")

    FldOutStream outStream;

    uint8_t buf[1024];
    fldOutStreamInit(&outStream, buf, 1024);

    GuiseClientRealize clientRealize;
    GuiseClientRealizeSettings settings;

    ImprintDefaultSetup memory;

    DatagramTransport transportInOut;

    imprintDefaultSetupInit(&memory, 16 * 1024 * 1024);

    int startupErr = udpClientStartup();
    if (startupErr < 0) {
        return startupErr;
    }

    const char* hostToConnectTo = "127.0.0.1";

    if (argc > 1) {
        hostToConnectTo = argv[1];
    }

    UdpClientSocket udpClientSocket;
    udpClientInit(&udpClientSocket, hostToConnectTo, 27004);

    transportInOut.self = &udpClientSocket;
    transportInOut.receive = clientReceive;
    transportInOut.send = clientSend;

    Secret secret;
    int secretErr = readSecret(&secret);
    if (secretErr < 0) {
        CLOG_SOFT_ERROR("could not read lines from secret.txt %d", secretErr)
        return secretErr;
    }

    settings.memory = &memory.tagAllocator.info;
    settings.transport = transportInOut;
    settings.userId = secret.userId;
    settings.secretPasswordHash = secret.passwordHash;
    Clog guiseClientLog;
    guiseClientLog.config = &g_clog;
    guiseClientLog.constantPrefix = "GuiseClient";
    settings.log = guiseClientLog;

    guiseClientRealizeInit(&clientRealize, &settings);
    guiseClientRealizeReInit(&clientRealize, &settings);

    clientRealize.state = GuiseClientRealizeStateInit;
    clientRealize.targetState = GuiseClientRealizeStateLogin;

    GuiseClientState reportedState;
    reportedState = GuiseClientStateIdle;

    RelayClient relayClient;
    Clog relayClientLog;
    relayClientLog.config = &g_clog;
    relayClientLog.constantPrefix = "RelayClient";

    UdpClientSocket udpClientSocketRelay;
    udpClientInit(&udpClientSocketRelay, hostToConnectTo, 27005);
    DatagramTransport transportRelayServer;
    transportRelayServer.self = &udpClientSocketRelay;
    transportRelayServer.receive = clientReceive;
    transportRelayServer.send = clientSend;

    bool hasCreatedRelayClient = false;

    RelayConnector* connector = 0;
    RelayListener* listener = 0;

    while (true) {
        struct timespec ts;

        ts.tv_sec = 0;
        ts.tv_nsec = 16 * 1000000;
        nanosleep(&ts, &ts);

        MonotonicTimeMs now = monotonicTimeMsNow();
        guiseClientRealizeUpdate(&clientRealize, now);

        if (reportedState != clientRealize.client.state) {
            reportedState = clientRealize.client.state;
            if (reportedState == GuiseClientStateLoggedIn && !hasCreatedRelayClient) {
                // Logged in
                int relayInitErr = relayClientInit(&relayClient, clientRealize.client.mainUserSessionId,
                                                   transportRelayServer, settings.memory, "relayClient",
                                                   relayClientLog);

                if (relayInitErr < 0) {
                    return relayInitErr;
                }

                RelaySerializeApplicationId appId = 42;
                RelaySerializeChannelId channelId = 1;
                listener = relayClientStartListen(&relayClient, appId, channelId);
                hasCreatedRelayClient = true;
                CLOG_C_DEBUG(&relayClient.log, "start listening %" PRIX64, listener->userSessionId)

                connector = relayClientStartConnect(&relayClient, clientRealize.client.userId, appId, channelId);
                CLOG_C_DEBUG(&relayClient.log, "start listening %" PRIX64, connector->userSessionId)
            }
        }

        if (hasCreatedRelayClient) {
            int updateErr = relayClientUpdate(&relayClient, monotonicTimeMsNow());
            if (updateErr < 0) {
                return updateErr;
            }

            if (connector != 0 && connector->state == RelayConnectorStateConnected) {
                relayConnectorSend(connector, (const uint8_t*) "hello", 6);
            }

            if (listener != 0 && listener->state == RelayListenerStateConnected) {
                if (listener->connections[0].connectionId != 0) {
                    relayListenerSendToConnectionIndex(listener, 0, (const uint8_t*) "world!", 7);
                }

                uint8_t outConnectionIndex;
                static uint8_t buf[DATAGRAM_TRANSPORT_MAX_SIZE];
                ssize_t octetCountRead = relayListenerReceivePacket(listener, &outConnectionIndex, buf,
                                                                    DATAGRAM_TRANSPORT_MAX_SIZE);
                if (octetCountRead > 0) {
                    CLOG_C_DEBUG(&relayClient.log,
                                 "found packet in listener from connectionIndex %hhu octetCount:%zd '%s'",
                                 outConnectionIndex, octetCountRead, buf)
                }
            }
        }
    }

    //    imprintDefaultSetupDestroy(&memory);
}
