/** BLE KW40Z Driver for Hexiwear
 *  This file contains BLE and Touch Buttons driver functionality for Hexiwear
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * Neither the name of NXP, nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * visit: http://www.mikroe.com and http://www.nxp.com
 *
 * get support at: http://www.mikroe.com/forum and https://community.nxp.com
 *
 * Project HEXIWEAR, 2015
 * Lorenzo Calisti, 2022
 */

#include "kw40z.h"

KW40Z::KW40Z(PinName txPin, PinName rxPin) : device(txPin, rxPin, 230400),
                                             rxThread(osPriorityNormal, 1024),
                                             mainThread(osPriorityNormal, 1024)
{
#if defined(LIB_DEBUG)
    printf("Initializing\n");
#endif

    device.format(8, SerialBase::None, 2);

    rxBuff = (uint8_t *)&hostInterface_rxPacket;

    /* initialize callbacks */
    buttonUpCb = NULL;
    buttonDownCb = NULL;
    buttonLeftCb = NULL;
    buttonRightCb = NULL;
    buttonSlideCb = NULL;
    alertCb = NULL;
    passkeyCb = NULL;
    notificationsCb = NULL;

    kw40_version.ver_patchNumber = 0;
    kw40_version.ver_minorNumber = 0;
    kw40_version.ver_majorNumber = 0;

    activeTsiGroup = 0;
    advertisementMode = 0;
    linkState = 0;
    bondPassKey = 0;

    /* intialization finalized, signal to start the threads */
    rxThread.start(callback(this, &KW40Z::rxTask));
    mainThread.start(callback(this, &KW40Z::mainTask));
}

KW40Z::~KW40Z(void)
{
}

void KW40Z::attach_buttonUp(button_t btnFct) { buttonUpCb = btnFct; }
void KW40Z::attach_buttonDown(button_t btnFct) { buttonDownCb = btnFct; }
void KW40Z::attach_buttonLeft(button_t btnFct) { buttonLeftCb = btnFct; }
void KW40Z::attach_buttonRight(button_t btnFct) { buttonRightCb = btnFct; }
void KW40Z::attach_buttonSlide(button_t btnFct) { buttonSlideCb = btnFct; }
void KW40Z::attach_alert(alert_t alertFct) { alertCb = alertFct; }
void KW40Z::attach_passkey(passkey_t passkeyFct) { passkeyCb = passkeyFct; }
void KW40Z::attach_notifications(notifications_t notFct) { notificationsCb = notFct; }

void KW40Z::mainTask(void)
{
#if defined(LIB_DEBUG)
    printf("MainTask Stared\n");
#endif

    SendGetActiveTsiGroup();
    SendGetAdvertisementMode();
    SendGetLinkState();
    SendGetVersion();

    while (1)
    {
        hostInterface_packet_t *packet;
        if (queue.try_get_for(Kernel::wait_for_u32_forever, &packet))
        {
            ProcessPacket(packet);
            mpool.free(packet);
        }
    }
}

void KW40Z::rxTask(void)
{
#if defined(LIB_DEBUG)
    printf("RxTask Stared\n");
#endif

    while (1)
    {
        if (device.readable())
        {
            char ch;
            device.read(&ch, sizeof(char));
            *rxBuff++ = ch;
            ProcessBuffer();

            /* check for buffer overflow */
            if (rxBuff >= ((uint8_t *)&hostInterface_rxPacket + sizeof(hostInterface_rxPacket)))
            {
                rxBuff = (uint8_t *)&hostInterface_rxPacket;
            }
        }
    }
}

void KW40Z::SendPacket(hostInterface_packet_t *txPacket, bool confirmRequested)
{
    /* copy txPacket to the mem pool */
    hostInterface_packet_t *packet = mpool.try_alloc();
    if (packet == nullptr)
    {
#if defined(LIB_DEBUG)
        printf("Error sending packet!!! Not enough memory\n");
#endif
        return;
    }

    memcpy(packet, txPacket, sizeof(hostInterface_packet_t));

    /* Set the TX bit in the Start 2 byte */
    packet->start2 |= gHostInterface_txPacketMask;

    if (true == confirmRequested)
    {
        /* Set the confirm requested bit in the Start 2 byte */
        packet->start2 |= gHostInterface_rxConfirmMask;
    }

#if defined(LIB_DEBUG)
    printf("SendPacket: ");
    DebugPrintPacket(packet);
#endif

    /* send message to main task */
    queue.try_put(packet);
}

void KW40Z::SendInternal(hostInterface_packet_t *txPacket)
{
    uint8_t retries = 0;
    bool confirmRequested = false;
    confirmReceived = false;

#if defined(LIB_DEBUG)
    printf("SendInternal: ");
    DebugPrintPacket(txPacket);
#endif

    if (gHostInterface_rxConfirmMask == (txPacket->start2 & gHostInterface_rxConfirmMask))
    {
        confirmRequested = true;
#if defined(LIB_DEBUG)
        printf("Found confirmRequested\n");
#endif
    }

    do
    {
        char *txBuff = (char *)txPacket;
        uint8_t length = txPacket->length + gHostInterface_headerSize + 1;

        for (uint8_t i = 0; i < length; i++)
        {
            device.write((const void *)txBuff, sizeof(char));
            txBuff++;
        }

#if defined(LIB_DEBUG)
        printf("TX: ");
        DebugPrintPacket(txPacket);
#endif

        retries++;

#if defined(gHostInterface_RxConfirmationEnable)
        if ((confirmRequested == true) && (confirmReceived == false))
        {
            ThisThread::sleep_for(gHostInterface_retransmitTimeout);
        }
#endif
    } while ((confirmRequested == true) &&
             (confirmReceived == false) &&
             (retries < gHostInterface_retransmitCount));
}

void KW40Z::ProcessBuffer()
{
#if defined(RAW_DEBUG)
    printf("%02X ", rxBuff - 1);
#endif
    /* check if header has been received */
    if (rxBuff > ((uint8_t *)&hostInterface_rxPacket + gHostInterface_headerSize))
    {
        /* check packet header */
        if ((gHostInterface_startByte1 != hostInterface_rxPacket.start1) ||
            (gHostInterface_startByte2 != (hostInterface_rxPacket.start2 & 0xFE)) ||
            (hostInterface_rxPacket.length > gHostInterface_dataSize))
        {
#if defined(LIB_DEBUG)
            printf("Check header failed: ");
            DebugPrintPacket(&hostInterface_rxPacket);
#endif

            SearchStartByte();
        }
        else
        {
            /* check data length */
            if (rxBuff > ((uint8_t *)&hostInterface_rxPacket + gHostInterface_headerSize + hostInterface_rxPacket.length))
            {
                /* check trailer byte */
                if (gHostInterface_trailerByte != hostInterface_rxPacket.data[hostInterface_rxPacket.length])
                {
#if defined(LIB_DEBUG)
                    printf("Trailer byte failed: ");
                    DebugPrintPacket(&hostInterface_rxPacket);
#endif

                    SearchStartByte();
                }
                else
                {
                    /* send message to main task */
                    hostInterface_packet_t *rxPacket = mpool.try_alloc();
                    if (rxPacket != nullptr)
                    {

#if defined(gHostInterface_RxConfirmationEnable)
                        if (hostInterface_rxPacket.type == packetType_OK)
                        {
                            confirmReceived = true;
                        }
#endif
                        memcpy(rxPacket, &hostInterface_rxPacket, sizeof(hostInterface_packet_t));
                        queue.try_put(rxPacket);

#if defined(LIB_DEBUG)
                        printf("RX: ");
                        DebugPrintPacket(&hostInterface_rxPacket);
#endif
#if defined(RAW_DEBUG)
                        printf("\n");
#endif
                    }
                    /* reset buffer position */
                    rxBuff = (uint8_t *)&hostInterface_rxPacket;
                }
            }
        }
    }
}

void KW40Z::SearchStartByte()
{
    bool found = false;
    uint8_t *rdIdx = (uint8_t *)&hostInterface_rxPacket;
    rdIdx++;

    while (rdIdx < rxBuff)
    {
        if (*rdIdx == gHostInterface_startByte1)
        {
            uint32_t len = rxBuff - rdIdx + 1;

            memcpy(&hostInterface_rxPacket, rdIdx, len);
            rxBuff = (uint8_t *)&hostInterface_rxPacket + len;
            found = true;

#if defined(LIB_DEBUG)
            printf("moving %d", len);
#endif
            break;
        }
        rdIdx++;
    }

    if (!found)
    {
        /* reset buffer position */
        rxBuff = (uint8_t *)&hostInterface_rxPacket;
        memset(rxBuff, 0, sizeof(hostInterface_packet_t));
    }

#if defined(LIB_DEBUG)
    printf("Search done: \n");
    printf("rxBuff: ");
    DebugPrintPacket((hostInterface_packet_t *)rxBuff);
    printf("rxPack: ");
    DebugPrintPacket(&hostInterface_rxPacket);
#endif
}

void KW40Z::ProcessPacket(hostInterface_packet_t *packet)
{
#if defined(LIB_DEBUG)
    printf("ProcessPacket: ");
    DebugPrintPacket(packet);
#endif

    /* check if this is a TX packet */
    if (gHostInterface_txPacketMask == (packet->start2 & gHostInterface_txPacketMask))
    {
        /* revert back the TX bit in Start2 byte */
        packet->start2 &= ~gHostInterface_txPacketMask;

        /* This is not a received packet, so call SendInternal */
        SendInternal(packet);
    }
    else
    {
#ifdef gHostInterface_TxConfirmationEnable
        /* acknowledge the packet reception if requested by sender */
        if (gHostInterface_rxConfirmMask == (packet->start2 & gHostInterface_rxConfirmMask))
        {
            SendPacketOK();
        }
#endif

        switch (packet->type)
        {
        /* button presses */
        case packetType_pressUp:
            if (buttonUpCb != NULL)
                buttonUpCb();
            break;

        case packetType_pressDown:
            if (buttonDownCb != NULL)
                buttonDownCb();
            break;

        case packetType_pressLeft:
            if (buttonLeftCb != NULL)
                buttonLeftCb();
            break;

        case packetType_pressRight:
            if (buttonRightCb != NULL)
                buttonRightCb();
            break;

        case packetType_slide:
            if (buttonSlideCb != NULL)
                buttonSlideCb();
            break;

        /* Alert Service */
        case packetType_alertIn:
            if (alertCb != NULL)
                alertCb(&packet->data[0], packet->length);
            break;

        /* Passkey for pairing received */
        case packetType_passDisplay:
            if (passkeyCb != NULL)
            {
                memcpy((uint8_t *)&bondPassKey, &packet->data[0], 3);
                passkeyCb();
            }
            break;

        /* OTAP messages */
        case packetType_otapCompleted:
        case packetType_otapFailed:
            break;

        /* TSI Status */
        case packetType_buttonsGroupSendActive:
            activeTsiGroup = packet->data[0];
            break;

        /* Advertisement Mode Info */
        case packetType_advModeSend:
            advertisementMode = packet->data[0];
            break;

        /* Link State */
        case packetType_linkStateSend:
            linkState = packet->data[0];
            break;

        /* ANCS Service Notification Received */
        case packetType_notification:
            if (notificationsCb != NULL)
                notificationsCb(packet->data[0], packet->data[1]);
            break;

        /* Build version */
        case packetType_buildVersion:
            kw40_version.ver_patchNumber = packet->data[2];
            kw40_version.ver_minorNumber = packet->data[1];
            kw40_version.ver_majorNumber = packet->data[0];
            break;

        case packetType_OK:
            /* do nothing, passthrough, the flag is set in the RxTask */
            break;

        default:
            break;
        }
    }
}

void KW40Z::SendBatteryLevel(uint8_t percentage)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_batteryLevel;
    txPacket.length = 1;
    txPacket.data[0] = percentage;
    txPacket.data[1] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendAccel(int16_t x, int16_t y, int16_t z)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_accel;
    txPacket.length = 6;

    txPacket.data[0] = (uint8_t)((x >> 8) & 0xFF);
    txPacket.data[1] = (uint8_t)x;
    txPacket.data[2] = (uint8_t)((y >> 8) & 0xFF);
    txPacket.data[3] = (uint8_t)y;
    txPacket.data[4] = (uint8_t)((z >> 8) & 0xFF);
    txPacket.data[5] = (uint8_t)z;
    txPacket.data[6] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendGyro(int16_t x, int16_t y, int16_t z)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_gyro;
    txPacket.length = 6;
    txPacket.data[0] = (uint8_t)((x >> 8) & 0xFF);
    txPacket.data[1] = (uint8_t)x;
    txPacket.data[2] = (uint8_t)((y >> 8) & 0xFF);
    txPacket.data[3] = (uint8_t)y;
    txPacket.data[4] = (uint8_t)((z >> 8) & 0xFF);
    txPacket.data[5] = (uint8_t)z;
    txPacket.data[6] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendMag(int16_t x, int16_t y, int16_t z)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_magnet;
    txPacket.length = 6;
    txPacket.data[0] = (uint8_t)((x >> 8) & 0xFF);
    txPacket.data[1] = (uint8_t)x;
    txPacket.data[2] = (uint8_t)((y >> 8) & 0xFF);
    txPacket.data[3] = (uint8_t)y;
    txPacket.data[4] = (uint8_t)((z >> 8) & 0xFF);
    txPacket.data[5] = (uint8_t)z;
    txPacket.data[6] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendAmbientLight(uint8_t percentage)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_ambiLight;
    txPacket.length = 1;
    txPacket.data[0] = percentage;
    txPacket.data[1] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendTemperature(uint16_t celsius)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_temperature;
    txPacket.length = 2;
    memcpy(&txPacket.data[0], (uint8_t *)&celsius, txPacket.length);
    txPacket.data[2] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendHumidity(uint16_t percentage)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_humidity;
    txPacket.length = 2;
    memcpy(&txPacket.data[0], (uint8_t *)&percentage, txPacket.length);
    txPacket.data[2] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendPressure(uint16_t pascal)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_pressure;
    txPacket.length = 2;
    memcpy(&txPacket.data[0], (uint8_t *)&pascal, txPacket.length);
    txPacket.data[2] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendHeartRate(uint8_t rate)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_steps;
    txPacket.length = 1;
    txPacket.data[0] = rate;
    txPacket.data[1] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendSteps(uint16_t steps)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_steps;
    txPacket.length = 2;
    memcpy(&txPacket.data[0], (uint8_t *)&steps, txPacket.length);
    txPacket.data[2] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendCalories(uint16_t calories)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_calories;
    txPacket.length = 2;
    memcpy(&txPacket.data[0], (uint8_t *)&calories, txPacket.length);
    txPacket.data[2] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendAlert(uint8_t *pData, uint8_t length)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_alertOut;
    txPacket.length = length;
    memcpy(&txPacket.data[0], pData, length);
    txPacket.data[length] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::ToggleTsiGroup(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_buttonsGroupToggleActive;
    txPacket.length = 0;
    txPacket.data[0] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::ToggleAdvertisementMode(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_advModeToggle;
    txPacket.length = 0;
    txPacket.data[0] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendSetApplicationMode(gui_current_app_t mode)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_appMode;
    txPacket.length = 1;
    txPacket.data[0] = (uint8_t)mode;
    txPacket.data[1] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendGetActiveTsiGroup(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_buttonsGroupGetActive;
    txPacket.length = 0;
    txPacket.data[0] = gHostInterface_trailerByte;

    SendPacket(&txPacket, false);
}

void KW40Z::SendGetAdvertisementMode(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_advModeGet;
    txPacket.length = 0;
    txPacket.data[0] = gHostInterface_trailerByte;

    SendPacket(&txPacket, false);
}

void KW40Z::SendGetLinkState(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_linkStateGet;
    txPacket.length = 0;
    txPacket.data[0] = gHostInterface_trailerByte;

    SendPacket(&txPacket, false);
}

void KW40Z::SendGetVersion(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_buildVersion;
    txPacket.length = 3;
    txPacket.data[0] = HEXIWEAR_VERSION_MAJOR;
    txPacket.data[1] = HEXIWEAR_VERSION_MINOR;
    txPacket.data[2] = HEXIWEAR_VERSION_PATCH;
    txPacket.data[3] = gHostInterface_trailerByte;

    SendPacket(&txPacket, true);
}

void KW40Z::SendPacketOK(void)
{
    hostInterface_packet_t txPacket = {0};

    txPacket.start1 = gHostInterface_startByte1;
    txPacket.start2 = gHostInterface_startByte2;
    txPacket.type = packetType_OK;
    txPacket.length = 0;
    txPacket.data[0] = gHostInterface_trailerByte;

    SendPacket(&txPacket, false);
}

uint8_t KW40Z::GetAdvertisementMode(void)
{
    return advertisementMode;
}

uint32_t KW40Z::GetPassKey(void)
{
    return bondPassKey;
}

uint8_t KW40Z::GetLinkState(void)
{
    return linkState;
}

hexiwear_version_t KW40Z::GetVersion(void)
{
    return kw40_version;
}

uint8_t KW40Z::GetTsiGroup(void)
{
    return activeTsiGroup;
}

#if defined(LIB_DEBUG)
void KW40Z::DebugPrintPacket(hostInterface_packet_t *packet)
{
    char *idx = (char *)packet;
    uint8_t length = packet->length + gHostInterface_headerSize + 1;

    if (length > sizeof(hostInterface_packet_t))
    {
        length = sizeof(hostInterface_packet_t);
    }

    for (uint8_t i = 0; i < length; i++)
    {
        printf("%02X ", *idx);
        idx++;
    }
    printf("\n");
}
#endif
