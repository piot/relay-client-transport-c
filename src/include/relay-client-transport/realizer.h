/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef RELAY_CLIENT_TRANSPORT_H
#define RELAY_CLIENT_TRANSPORT_H

#include <datagram-transport/transport.h>
#include <guise-client/client.h>
#include <relay-client/client.h>

struct ImprintAllocator;

typedef enum RelayClientTransportRealizerState {
    RelayClientTransportRealizerIdle,
    RelayClientTransportRealizerAuthenticating,
    RelayClientTransportRealizerAuthenticated,
} RelayClientTransportRealizerState;

typedef struct RelayClientTransportRealizer {
    RelayClient relayClient;
    GuiseClient guiseClient;
    RelayClientTransportRealizerState state;
    DatagramTransport transportToGuise;
    DatagramTransport transportToRelay;
    struct ImprintAllocator* memory;
    Clog log;
} RelayClientTransportRealizer;

int relayClientTransportRealizerInit(RelayClientTransportRealizer* self, struct ImprintAllocator* memory,
                                     struct DatagramTransport* transportToGuise,
                                     struct DatagramTransport* transportToRelay, Clog log);
int relayClientTransportRealizerReInit(RelayClientTransportRealizer* self, GuiseSerializeUserId userId,
                                       uint64_t secretPrivatePassword);
int relayClientTransportRealizerUpdate(RelayClientTransportRealizer* self, MonotonicTimeMs now);

RelayListener* relayClientTransportRealizerStartListen(RelayClientTransportRealizer* self,
                                            RelaySerializeApplicationId applicationId,
                                            RelaySerializeChannelId channelId);

#endif
