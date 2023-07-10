/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <relay-client-transport/realizer.h>

int relayClientTransportRealizerInit(RelayClientTransportRealizer* self, struct ImprintAllocator* memory,
                                     struct DatagramTransport* transportToGuise,
                                     struct DatagramTransport* transportToRelay, Clog log)
{
    guiseClientInit(&self->guiseClient, memory, log);

    self->state = RelayClientTransportRealizerIdle;
    self->transportToRelay = *transportToRelay;
    self->transportToGuise = *transportToGuise;
    self->memory = memory;
    self->log = log;
    return 0;
}

int relayClientTransportRealizerReInit(RelayClientTransportRealizer* self, GuiseSerializeUserId userId,
                                       uint64_t secretPrivatePassword)
{
    int err = guiseClientReInit(&self->guiseClient, &self->transportToGuise, userId, secretPrivatePassword);
    if (err < 0) {
        return err;
    }

    self->state = RelayClientTransportRealizerAuthenticating;

    return 0;
}

RelayListener* relayClientTransportRealizerStartListen(RelayClientTransportRealizer* self,  RelaySerializeApplicationId applicationId,
                                      RelaySerializeChannelId channelId)
{
    return relayClientStartListen(&self->relayClient, applicationId, channelId);
}


int relayClientTransportRealizerUpdate(RelayClientTransportRealizer* self, MonotonicTimeMs now)
{
    switch (self->state) {
        case RelayClientTransportRealizerIdle:
        case RelayClientTransportRealizerAuthenticating: {
            int err = guiseClientUpdate(&self->guiseClient, now);
            if (err < 0) {
                return err;
            }
            if (self->guiseClient.state == GuiseClientStateLoggedIn) {
                CLOG_C_DEBUG(&self->log, "authenticated from guise server, starting contacting the relay server")
                relayClientInit(&self->relayClient, self->guiseClient.mainUserSessionId, self->transportToRelay,
                                self->memory, "", self->log);
                self->state = RelayClientTransportRealizerAuthenticated;
            }
        }

        break;
        case RelayClientTransportRealizerAuthenticated: {
            int err = relayClientUpdate(&self->relayClient, now);
            if (err < 0) {
                return err;
            }
        }
    }

    return 0;
}
