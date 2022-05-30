
#include "ANT_LIB/types.h"
#include "ANT_LIB/dsi_framer_ant.hpp"
#include "ANT_LIB/dsi_thread.h"
#include "ANT_LIB/dsi_serial_generic.hpp"

#define CHANNEL_TYPE_MASTER   (0)
#define CHANNEL_TYPE_SLAVE    (1)
#define CHANNEL_TYPE_INVALID  (2)

typedef void (*MESSAGE_CALLBACK)(const char* message);

class ANTController {
public:
   ANTController(MESSAGE_CALLBACK callback);
   virtual ~ANTController();
BOOL Init(UCHAR usbNumber, UCHAR channelType, USHORT deviceType, USHORT transType, USHORT radioFreq, USHORT perio);
   void Close();

private:
   BOOL InitANT();

   //Starts the Message thread.
   static DSI_THREAD_RETURN RunMessageThread(void *pvParameter_);

   //Listens for a response from the module
   void MessageThread();
   //Decodes the received message
   void ProcessMessage(ANT_MESSAGE stMessage, USHORT usSize_);

   void LogMessage(const char *format, ...);
   BOOL bBursting; //holds whether the bursting phase of the test has started
   BOOL bBroadcasting;
   BOOL bMyDone;
   BOOL bDone;
   UCHAR ucChannelType;
   USHORT usDeviceType;
   USHORT usTransType;
   USHORT usRadioFreq;
   USHORT usPeriod;
   DSISerialGeneric* pclSerialObject;
   DSIFramerANT* pclMessageObject;
   DSI_THREAD_ID uiDSIThread;
   DSI_CONDITION_VAR condTestDone;
   DSI_MUTEX mutexTestDone;
   MESSAGE_CALLBACK fMessageCallback;

   BOOL bDisplay;

   UCHAR aucTransmitBuffer[ANT_STANDARD_DATA_PAYLOAD_SIZE];



};