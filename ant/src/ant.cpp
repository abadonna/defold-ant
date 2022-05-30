#include "ant.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#define ENABLE_EXTENDED_MESSAGES

#define USER_BAUDRATE         (57600)  // For AT3/AP2, use 57600

#define USER_ANTCHANNEL       (0)
#define USER_DEVICENUM        (0)


#define USER_NETWORK_KEY      {0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45} //ant plus network key
#define USER_NETWORK_NUM      (0)      // The network key is assigned to this network number

#define MESSAGE_TIMEOUT       (1000)

// Indexes into message recieved from ANT
#define MESSAGE_BUFFER_DATA1_INDEX ((UCHAR) 0)
#define MESSAGE_BUFFER_DATA2_INDEX ((UCHAR) 1)
#define MESSAGE_BUFFER_DATA3_INDEX ((UCHAR) 2)
#define MESSAGE_BUFFER_DATA4_INDEX ((UCHAR) 3)
#define MESSAGE_BUFFER_DATA5_INDEX ((UCHAR) 4)
#define MESSAGE_BUFFER_DATA6_INDEX ((UCHAR) 5)
#define MESSAGE_BUFFER_DATA7_INDEX ((UCHAR) 6)
#define MESSAGE_BUFFER_DATA8_INDEX ((UCHAR) 7)
#define MESSAGE_BUFFER_DATA9_INDEX ((UCHAR) 8)
#define MESSAGE_BUFFER_DATA10_INDEX ((UCHAR) 9)
#define MESSAGE_BUFFER_DATA11_INDEX ((UCHAR) 10)
#define MESSAGE_BUFFER_DATA12_INDEX ((UCHAR) 11)
#define MESSAGE_BUFFER_DATA13_INDEX ((UCHAR) 12)
#define MESSAGE_BUFFER_DATA14_INDEX ((UCHAR) 13)

ANTController::ANTController(MESSAGE_CALLBACK callback)
{
    fMessageCallback = callback;
    ucChannelType = CHANNEL_TYPE_INVALID;
    pclSerialObject = (DSISerialGeneric*)NULL;
    pclMessageObject = (DSIFramerANT*)NULL;
    uiDSIThread = (DSI_THREAD_ID)NULL;
    bMyDone = FALSE;
    bDone = FALSE;
    bDisplay = TRUE;
    bBroadcasting = FALSE;

    memset(aucTransmitBuffer,0,ANT_STANDARD_DATA_PAYLOAD_SIZE);
}


ANTController::~ANTController()
{
    if(pclMessageObject)
      delete pclMessageObject;

    if(pclSerialObject)
      delete pclSerialObject;
}

////////////////////////////////////////////////////////////////////////////////
// Init
//
// ucDeviceNumber_: USB Device Number (0 for first USB stick plugged and so on)
//                  If not specified on command line, 0xFF is passed in as invalid.
// ucChannelType_:  ANT Channel Type. 0 = Master, 1 = Slave
//                  If not specified, 2 is passed in as invalid.
//
////////////////////////////////////////////////////////////////////////////////
BOOL ANTController::Init(UCHAR usbNumber, UCHAR channelType, USHORT deviceType, USHORT transType, USHORT radioFreq, USHORT period)
{
    BOOL bStatus;

    usDeviceType = deviceType;
    usTransType = transType;
    usRadioFreq = radioFreq;
    usPeriod = period;

    // Initialize condition var and mutex
    UCHAR ucCondInit = DSIThread_CondInit(&condTestDone);
    assert(ucCondInit == DSI_THREAD_ENONE);

    UCHAR ucMutexInit = DSIThread_MutexInit(&mutexTestDone);
    assert(ucMutexInit == DSI_THREAD_ENONE);

    // Create Serial object.
    pclSerialObject = new DSISerialGeneric();
    assert(pclSerialObject);

    ucChannelType = channelType;

    // Initialize Serial object.
    // The device number depends on how many USB sticks have been
    // plugged into the PC. The first USB stick plugged will be 0
    // the next 1 and so on.
    //
    // The Baud Rate depends on the ANT solution being used. AP1
    // is 50000, all others are 57600
    bStatus = pclSerialObject->Init(USER_BAUDRATE, usbNumber);
    assert(bStatus);

    // Create Framer object.
    pclMessageObject = new DSIFramerANT(pclSerialObject);
    assert(pclMessageObject);

    // Initialize Framer object.
    bStatus = pclMessageObject->Init();
    assert(bStatus);

    // Let Serial know about Framer.
    pclSerialObject->SetCallback(pclMessageObject);

    // Open Serial.
    bStatus = pclSerialObject->Open();

    // If the Open function failed, most likely the device
    // we are trying to access does not exist, or it is connected
    // to another program
    if(!bStatus)
    {
        LogMessage("Failed to connect to device at USB port %d\n", usbNumber);
        return FALSE;
    }

    // Create message thread.
    uiDSIThread = DSIThread_CreateThread(&ANTController::RunMessageThread, this);
    assert(uiDSIThread);

    LogMessage("Initialization was successful!\n"); fflush(stdout);

    return this->InitANT();
}

void ANTController::Close()
{
    //Wait for test to be done
    DSIThread_MutexLock(&mutexTestDone);
    bDone = TRUE;

    UCHAR ucWaitResult = DSIThread_CondTimedWait(&condTestDone, &mutexTestDone, DSI_THREAD_INFINITE);
    assert(ucWaitResult == DSI_THREAD_ENONE);

    DSIThread_MutexUnlock(&mutexTestDone);

    //Destroy mutex and condition var
    DSIThread_MutexDestroy(&mutexTestDone);
    DSIThread_CondDestroy(&condTestDone);

    //Close all stuff
    if(pclSerialObject)
      pclSerialObject->Close();
}

void ANTController::LogMessage(const char *format, ...)
{
    va_list args;
    va_start (args, format);
    char message [512];
    vsprintf (message, format, args);
    va_end(args);

    printf("%s", message);
    fMessageCallback(message);
}

////////////////////////////////////////////////////////////////////////////////
// InitANT
//
// Resets the system and starts the test
//
////////////////////////////////////////////////////////////////////////////////
BOOL ANTController::InitANT(void)
{
   BOOL bStatus;

   // Reset system
   LogMessage("Resetting module...\n");
   bStatus = pclMessageObject->ResetSystem();
   DSIThread_Sleep(1000);

   // Start the test by setting network key
   LogMessage("Setting network key...\n");
   UCHAR ucNetKey[8] = USER_NETWORK_KEY;

   bStatus = pclMessageObject->SetNetworkKey(USER_NETWORK_NUM, ucNetKey, MESSAGE_TIMEOUT);

   return bStatus;
}

////////////////////////////////////////////////////////////////////////////////
// RunMessageThread
//
// Callback function that is used to create the thread. This is a static
// function.
//
////////////////////////////////////////////////////////////////////////////////
DSI_THREAD_RETURN ANTController::RunMessageThread(void *pvParameter_)
{
   ((ANTController*) pvParameter_)->MessageThread();
   return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// MessageThread
//
// Run message thread
////////////////////////////////////////////////////////////////////////////////
void ANTController::MessageThread()
{
   ANT_MESSAGE stMessage;
   USHORT usSize;
   bDone = FALSE;

   while(!bDone)
   {
      if(pclMessageObject->WaitForMessage(1000))
      {
         usSize = pclMessageObject->GetMessage(&stMessage);

         if(bDone)
            break;

         if(usSize == DSI_FRAMER_ERROR)
         {
            // Get the message to clear the error
            usSize = pclMessageObject->GetMessage(&stMessage, MESG_MAX_SIZE_VALUE);
            continue;
         }

         if(usSize != DSI_FRAMER_ERROR && usSize != DSI_FRAMER_TIMEDOUT && usSize != 0)
         {
            ProcessMessage(stMessage, usSize);
         }
      }
   }

   DSIThread_MutexLock(&mutexTestDone);
   UCHAR ucCondResult = DSIThread_CondSignal(&condTestDone);
   assert(ucCondResult == DSI_THREAD_ENONE);
   DSIThread_MutexUnlock(&mutexTestDone);

}


////////////////////////////////////////////////////////////////////////////////
// ProcessMessage
//
// Process ALL messages that come from ANT, including event messages.
//
// stMessage: Message struct containing message recieved from ANT
// usSize_:
////////////////////////////////////////////////////////////////////////////////
void ANTController::ProcessMessage(ANT_MESSAGE stMessage, USHORT usSize_)
{
   BOOL bStatus;
   BOOL bPrintBuffer = FALSE;
   UCHAR ucDataOffset = MESSAGE_BUFFER_DATA2_INDEX;   // For most data messages


   switch(stMessage.ucMessageID)
   {
      //RESPONSE MESG
      case MESG_RESPONSE_EVENT_ID:
      {
         //RESPONSE TYPE
         switch(stMessage.aucData[1])
         {
            case MESG_NETWORK_KEY_ID:
            {
               if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error configuring network key: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               LogMessage("Network key set.\n");
               LogMessage("Assigning channel...\n");
               if(ucChannelType == CHANNEL_TYPE_MASTER)
               {
                  bStatus = pclMessageObject->AssignChannel(USER_ANTCHANNEL, PARAMETER_TX_NOT_RX, 0, MESSAGE_TIMEOUT);
               }
               else if(ucChannelType == CHANNEL_TYPE_SLAVE)
               {
                  bStatus = pclMessageObject->AssignChannel(USER_ANTCHANNEL, 0, 0, MESSAGE_TIMEOUT);
               }
               break;
            }

            case MESG_ASSIGN_CHANNEL_ID:
            {
               if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error assigning channel: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               LogMessage("Channel assigned\n");
               LogMessage("Setting Channel ID... Ch:%d Device:%d Devtype:%d Transtype:%d \n", USER_ANTCHANNEL, USER_DEVICENUM, usDeviceType, usTransType);
               bStatus = pclMessageObject->SetChannelID(USER_ANTCHANNEL, USER_DEVICENUM, usDeviceType, usTransType, MESSAGE_TIMEOUT);
               break;
            }

            case MESG_CHANNEL_ID_ID:
            {
               if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error configuring Channel ID: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               LogMessage("Channel ID set \n");
               LogMessage("Setting Radio Frequency...\n");
               bStatus = pclMessageObject->SetChannelRFFrequency(USER_ANTCHANNEL, usRadioFreq, MESSAGE_TIMEOUT);
               break;
            }

            case MESG_CHANNEL_RADIO_FREQ_ID:
            {
               if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error configuring Radio Frequency: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               LogMessage("Radio Frequency set\n");


               LogMessage("Setting Message Period...\n");
               bStatus = pclMessageObject->SetChannelPeriod(USER_ANTCHANNEL, (USHORT)8070, MESSAGE_TIMEOUT);
               
               break;
            }

            case MESG_CHANNEL_MESG_PERIOD_ID:
            {
                if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
                {
                    printf("Error assigning Message Period: Code 0%d\n", stMessage.aucData[2]);
                    break;
                }
                LogMessage("Message period assigned\n");
                LogMessage("Opening channel...\n");
                bBroadcasting = TRUE;
                bStatus = pclMessageObject->OpenChannel(USER_ANTCHANNEL, MESSAGE_TIMEOUT);
                break;
            }

            case MESG_OPEN_CHANNEL_ID:
            {
               if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error opening channel: Code 0%d\n", stMessage.aucData[2]);
                  bBroadcasting = FALSE;
                  break;
               }
               LogMessage("Chanel opened\n");
#if defined (ENABLE_EXTENDED_MESSAGES)
               LogMessage("Enabling extended messages...\n");
               pclMessageObject->RxExtMesgsEnable(TRUE);
#endif
               break;
            }

            case MESG_RX_EXT_MESGS_ENABLE_ID:
            {
               if(stMessage.aucData[2] == INVALID_MESSAGE)
               {
                  LogMessage("Extended messages not supported in this ANT product\n");
                  break;
               }
               else if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error enabling extended messages: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               LogMessage("Extended messages enabled\n");
               break;
            }

            case MESG_UNASSIGN_CHANNEL_ID:
            {
               if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error unassigning channel: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               LogMessage("Channel unassigned\n");
               bMyDone = TRUE;
               break;
            }

            case MESG_CLOSE_CHANNEL_ID:
            {
               if(stMessage.aucData[2] == CHANNEL_IN_WRONG_STATE)
               {
                  // We get here if we tried to close the channel after the search timeout (slave)
                  LogMessage("Channel is already closed\n");
                  LogMessage("Unassigning channel...\n");
                  bStatus = pclMessageObject->UnAssignChannel(USER_ANTCHANNEL, MESSAGE_TIMEOUT);
                  break;
               }
               else if(stMessage.aucData[2] != RESPONSE_NO_ERROR)
               {
                  LogMessage("Error closing channel: Code 0%d\n", stMessage.aucData[2]);
                  break;
               }
               // If this message was successful, wait for EVENT_CHANNEL_CLOSED to confirm channel is closed
               break;
            }

            case MESG_REQUEST_ID:
            {
               if(stMessage.aucData[2] == INVALID_MESSAGE)
               {
                  LogMessage("Requested message not supported in this ANT product\n");
               }
               break;
            }

            case MESG_EVENT_ID:
            {
               switch(stMessage.aucData[2])
                  {
                  case EVENT_CHANNEL_CLOSED:
                  {
                     LogMessage("Channel Closed\n");
                     LogMessage("Unassigning channel...\n");
                     bStatus = pclMessageObject->UnAssignChannel(USER_ANTCHANNEL, MESSAGE_TIMEOUT);
                     break;
                  }
                  case EVENT_TX:
                  {
                     // This event indicates that a message has just been
                     // sent over the air. We take advantage of this event to set
                     // up the data for the next message period.
                     static UCHAR ucIncrement = 0;      // Increment the first byte of the buffer

                     aucTransmitBuffer[0] = ucIncrement++;

                     // Broadcast data will be sent over the air on
                     // the next message period.
                     if(bBroadcasting)
                     {
                        pclMessageObject->SendBroadcastData(USER_ANTCHANNEL, aucTransmitBuffer);

                        // Echo what the data will be over the air on the next message period.
                        if(bDisplay)
                        {
                           LogMessage("Tx:(%d): [%02x],[%02x],[%02x],[%02x],[%02x],[%02x],[%02x],[%02x]\n",
                              USER_ANTCHANNEL,
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA1_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA2_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA3_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA4_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA5_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA6_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA7_INDEX],
                              aucTransmitBuffer[MESSAGE_BUFFER_DATA8_INDEX]);
                        }
                        else
                        {
                           static int iIndex = 0;
                           static char ac[] = {'|','/','-','\\'};
                           LogMessage("Tx: %c\r",ac[iIndex++]); fflush(stdout);
                           iIndex &= 3;
                        }
                     }
                     break;

                  }
                  case EVENT_RX_SEARCH_TIMEOUT:
                  {
                     LogMessage("Search Timeout\n");
                     break;
                  }
                  case EVENT_RX_FAIL:
                  {
                     LogMessage("Rx Fail\n");
                     break;
                  }
                  case EVENT_TRANSFER_RX_FAILED:
                  {
                     LogMessage("Burst receive has failed\n");
                     break;
                  }
                  case EVENT_TRANSFER_TX_COMPLETED:
                  {
                     LogMessage("Tranfer Completed\n");
                     break;
                  }
                  case EVENT_TRANSFER_TX_FAILED:
                  {
                     LogMessage("Tranfer Failed\n");
                     break;
                  }
                  case EVENT_RX_FAIL_GO_TO_SEARCH:
                  {
                     LogMessage("Go to Search\n");
                     break;
                  }
                  case EVENT_CHANNEL_COLLISION:
                  {
                     LogMessage("Channel Collision\n");
                     break;
                  }
                  case EVENT_TRANSFER_TX_START:
                  {
                     LogMessage("Burst Started\n");
                     break;
                  }
                  default:
                  {
                     LogMessage("Unhandled channel event: 0x%X\n", stMessage.aucData[2]);
                     break;
                  }

               }

               break;
            }

            default:
            {
               LogMessage("Unhandled response 0%d to message 0x%X\n", stMessage.aucData[2], stMessage.aucData[1]);
               break;
            }
         }
         break;
      }

      case MESG_STARTUP_MESG_ID:
      {
         LogMessage("RESET Complete, reason: ");

         UCHAR ucReason = stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX];

         if(ucReason == RESET_POR)
            LogMessage("RESET_POR");
         if(ucReason & RESET_SUSPEND)
            LogMessage("RESET_SUSPEND ");
         if(ucReason & RESET_SYNC)
            LogMessage("RESET_SYNC ");
         if(ucReason & RESET_CMD)
            LogMessage("RESET_CMD ");
         if(ucReason & RESET_WDT)
            LogMessage("RESET_WDT ");
         if(ucReason & RESET_RST)
            LogMessage("RESET_RST ");
         printf("\n");

         break;
      }

      case MESG_CAPABILITIES_ID:
      {
         LogMessage("CAPABILITIES:\n");
         LogMessage("   Max ANT Channels: %d\n",stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX]);
         LogMessage("   Max ANT Networks: %d\n",stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX]);

         UCHAR ucStandardOptions = stMessage.aucData[MESSAGE_BUFFER_DATA3_INDEX];
         UCHAR ucAdvanced = stMessage.aucData[MESSAGE_BUFFER_DATA4_INDEX];
         UCHAR ucAdvanced2 = stMessage.aucData[MESSAGE_BUFFER_DATA5_INDEX];

         LogMessage("Standard Options:\n");
         if( ucStandardOptions & CAPABILITIES_NO_RX_CHANNELS )
            LogMessage("CAPABILITIES_NO_RX_CHANNELS\n");
         if( ucStandardOptions & CAPABILITIES_NO_TX_CHANNELS )
            LogMessage("CAPABILITIES_NO_TX_CHANNELS\n");
         if( ucStandardOptions & CAPABILITIES_NO_RX_MESSAGES )
            LogMessage("CAPABILITIES_NO_RX_MESSAGES\n");
         if( ucStandardOptions & CAPABILITIES_NO_TX_MESSAGES )
            LogMessage("CAPABILITIES_NO_TX_MESSAGES\n");
         if( ucStandardOptions & CAPABILITIES_NO_ACKD_MESSAGES )
            LogMessage("CAPABILITIES_NO_ACKD_MESSAGES\n");
         if( ucStandardOptions & CAPABILITIES_NO_BURST_TRANSFER )
            LogMessage("CAPABILITIES_NO_BURST_TRANSFER\n");

         LogMessage("Advanced Options:\n");
         if( ucAdvanced & CAPABILITIES_OVERUN_UNDERRUN )
            LogMessage("CAPABILITIES_OVERUN_UNDERRUN\n");
         if( ucAdvanced & CAPABILITIES_NETWORK_ENABLED )
            LogMessage("CAPABILITIES_NETWORK_ENABLED\n");
         if( ucAdvanced & CAPABILITIES_AP1_VERSION_2 )
            LogMessage("CAPABILITIES_AP1_VERSION_2\n");
         if( ucAdvanced & CAPABILITIES_SERIAL_NUMBER_ENABLED )
            LogMessage("CAPABILITIES_SERIAL_NUMBER_ENABLED\n");
         if( ucAdvanced & CAPABILITIES_PER_CHANNEL_TX_POWER_ENABLED )
            LogMessage("CAPABILITIES_PER_CHANNEL_TX_POWER_ENABLED\n");
         if( ucAdvanced & CAPABILITIES_LOW_PRIORITY_SEARCH_ENABLED )
            LogMessage("CAPABILITIES_LOW_PRIORITY_SEARCH_ENABLED\n");
         if( ucAdvanced & CAPABILITIES_SCRIPT_ENABLED )
            LogMessage("CAPABILITIES_SCRIPT_ENABLED\n");
         if( ucAdvanced & CAPABILITIES_SEARCH_LIST_ENABLED )
            LogMessage("CAPABILITIES_SEARCH_LIST_ENABLED\n");

         if(usSize_ > 4)
         {
            LogMessage("Advanced 2 Options 1:\n");
            if( ucAdvanced2 & CAPABILITIES_LED_ENABLED )
               LogMessage("CAPABILITIES_LED_ENABLED\n");
            if( ucAdvanced2 & CAPABILITIES_EXT_MESSAGE_ENABLED )
               LogMessage("CAPABILITIES_EXT_MESSAGE_ENABLED\n");
            if( ucAdvanced2 & CAPABILITIES_SCAN_MODE_ENABLED )
               LogMessage("CAPABILITIES_SCAN_MODE_ENABLED\n");
            if( ucAdvanced2 & CAPABILITIES_RESERVED )
               LogMessage("CAPABILITIES_RESERVED\n");
            if( ucAdvanced2 & CAPABILITIES_PROX_SEARCH_ENABLED )
               LogMessage("CAPABILITIES_PROX_SEARCH_ENABLED\n");
            if( ucAdvanced2 & CAPABILITIES_EXT_ASSIGN_ENABLED )
               LogMessage("CAPABILITIES_EXT_ASSIGN_ENABLED\n");
            if( ucAdvanced2 & CAPABILITIES_FS_ANTFS_ENABLED)
               LogMessage("CAPABILITIES_FREE_1\n");
            if( ucAdvanced2 & CAPABILITIES_FIT1_ENABLED )
               LogMessage("CAPABILITIES_FIT1_ENABLED\n");
         }
         break;
      }
      case MESG_CHANNEL_STATUS_ID:
      {
         LogMessage("Got Status\n");

         char astrStatus[][32] = {  "STATUS_UNASSIGNED_CHANNEL",
                                    "STATUS_ASSIGNED_CHANNEL",
                                    "STATUS_SEARCHING_CHANNEL",
                                    "STATUS_TRACKING_CHANNEL"   };

         UCHAR ucStatusByte = stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX] & STATUS_CHANNEL_STATE_MASK; // MUST MASK OFF THE RESERVED BITS
         LogMessage("STATUS: %s\n",astrStatus[ucStatusByte]);
         break;
      }
      case MESG_CHANNEL_ID_ID:
      {
         // Channel ID of the device that we just recieved a message from.
         USHORT usDeviceNumber = stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX] | (stMessage.aucData[MESSAGE_BUFFER_DATA3_INDEX] << 8);
         UCHAR ucDeviceType =  stMessage.aucData[MESSAGE_BUFFER_DATA4_INDEX];
         UCHAR ucTransmissionType = stMessage.aucData[MESSAGE_BUFFER_DATA5_INDEX];

         LogMessage("CHANNEL ID: (%d/%d/%d)\n", usDeviceNumber, ucDeviceType, ucTransmissionType);
         break;
      }
      case MESG_VERSION_ID:
      {
         LogMessage("VERSION: %s\n", (char*) &stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX]);
         break;
      }
      case MESG_ACKNOWLEDGED_DATA_ID:
      case MESG_BURST_DATA_ID:
      case MESG_BROADCAST_DATA_ID:
      {
         // The flagged and unflagged data messages have the same
         // message ID. Therefore, we need to check the size to
         // verify of a flag is present at the end of a message.
         // To enable flagged messages, must call ANT_RxExtMesgsEnable first.
         if(usSize_ > MESG_DATA_SIZE)
         {
            UCHAR ucFlag = stMessage.aucData[MESSAGE_BUFFER_DATA10_INDEX];

            if(bDisplay && ucFlag & ANT_EXT_MESG_BITFIELD_DEVICE_ID)
            {
               // Channel ID of the device that we just recieved a message from.
               USHORT usDeviceNumber = stMessage.aucData[MESSAGE_BUFFER_DATA11_INDEX] | (stMessage.aucData[MESSAGE_BUFFER_DATA12_INDEX] << 8);
               UCHAR ucDeviceType =  stMessage.aucData[MESSAGE_BUFFER_DATA13_INDEX];
               UCHAR ucTransmissionType = stMessage.aucData[MESSAGE_BUFFER_DATA14_INDEX];

               LogMessage("Chan ID(%d/%d/%d) - ", usDeviceNumber, ucDeviceType, ucTransmissionType);
            }
         }

         // Display recieved message
         bPrintBuffer = TRUE;
         ucDataOffset = MESSAGE_BUFFER_DATA2_INDEX;   // For most data messages

         if(bDisplay)
         {
            if(stMessage.ucMessageID == MESG_ACKNOWLEDGED_DATA_ID )
               LogMessage("Acked Rx:(%d): ", stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX]);
            else if(stMessage.ucMessageID == MESG_BURST_DATA_ID)
               LogMessage("Burst(0x%02x) Rx:(%d): ", ((stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX] & 0xE0) >> 5), stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX] & 0x1F );
            else
               LogMessage("Rx:(%d): ", stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX]);
         }
         break;
      }
      case MESG_EXT_BROADCAST_DATA_ID:
      case MESG_EXT_ACKNOWLEDGED_DATA_ID:
      case MESG_EXT_BURST_DATA_ID:
      {

         // The "extended" part of this message is the 4-byte channel
         // id of the device that we recieved this message from. This message
         // is only available on the AT3. The AP2 uses flagged versions of the
         // data messages as shown above.

         // Channel ID of the device that we just recieved a message from.
         USHORT usDeviceNumber = stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX] | (stMessage.aucData[MESSAGE_BUFFER_DATA3_INDEX] << 8);
         UCHAR ucDeviceType =  stMessage.aucData[MESSAGE_BUFFER_DATA4_INDEX];
         UCHAR ucTransmissionType = stMessage.aucData[MESSAGE_BUFFER_DATA5_INDEX];

         bPrintBuffer = TRUE;
         ucDataOffset = MESSAGE_BUFFER_DATA6_INDEX;   // For most data messages

         if(bDisplay)
         {
            // Display the channel id
            LogMessage("Chan ID(%d/%d/%d) ", usDeviceNumber, ucDeviceType, ucTransmissionType );

            if(stMessage.ucMessageID == MESG_EXT_ACKNOWLEDGED_DATA_ID)
               LogMessage("- Acked Rx:(%d): ", stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX]);
            else if(stMessage.ucMessageID == MESG_EXT_BURST_DATA_ID)
               LogMessage("- Burst(0x%02x) Rx:(%d): ", ((stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX] & 0xE0) >> 5), stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX] & 0x1F );
            else
               LogMessage("- Rx:(%d): ", stMessage.aucData[MESSAGE_BUFFER_DATA1_INDEX]);
         }

         break;
      }

      default:
      {
         break;
      }
   }

   // If we recieved a data message, diplay its contents here.
   if(bPrintBuffer)
   {
      if(bDisplay)
      {
         LogMessage("[%02x],[%02x],[%02x],[%02x],[%02x],[%02x],[%02x],[%02x]\n",
            stMessage.aucData[ucDataOffset + 0],
            stMessage.aucData[ucDataOffset + 1],
            stMessage.aucData[ucDataOffset + 2],
            stMessage.aucData[ucDataOffset + 3],
            stMessage.aucData[ucDataOffset + 4],
            stMessage.aucData[ucDataOffset + 5],
            stMessage.aucData[ucDataOffset + 6],
            stMessage.aucData[ucDataOffset + 7]);
      }
      else
      {
         static int iIndex = 0;
         static char ac[] = {'|','/','-','\\'};
         LogMessage("Rx: %c\r",ac[iIndex++]); fflush(stdout);
         iIndex &= 3;

      }
   }

   return;
}

