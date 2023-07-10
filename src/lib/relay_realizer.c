/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <relay-client-transport/realizer.h>

int relayClientTransportRealizerInit(RelayClientTransportRealizer* self, struct ImprintAllocator* memory,
                                     struct DatagramTransport* transportToGuise,
                                     struct DatagramTransport* transportToRelay, Clog log)
{
    guiseClientInit(&self->guiseClient, memory, transportToGuise, log);
    self->state = RelayClientTransportRealizerIdle;
    self->transportToRelay = *transportToRelay;
    self->memory = memory;
    self->log = log;
    return 0;
}

int relayClientTransportRealizerReInit(RelayClientTransportRealizer* self, GuiseSerializeUserId userId,
                                       uint64_t secretPrivatePassword)
{
    guiseClientReInit(&self->guiseClient, &self->guiseClient.transport);
    guiseClientLogin(&self->guiseClient, userId, secretPrivatePassword);

    return 0;
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
