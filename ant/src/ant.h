
#include <unordered_map>

#include "ANT_LIB/types.h"
#include "ANT_LIB/dsi_framer_ant.hpp"
#include "ANT_LIB/dsi_thread.h"
#include "ANT_LIB/dsi_serial_generic.hpp"

#define CHANNEL_TYPE_MASTER   (0)
#define CHANNEL_TYPE_SLAVE    (1)
#define CHANNEL_TYPE_INVALID  (2)

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

typedef void (*MESSAGE_CALLBACK)(const char* message, std::unordered_map<char*, float> *data);
typedef void (*DATA_PROCESS_CALLBACK)(ANT_MESSAGE stMessage, std::unordered_map<char*, float> *data);

void ProcessHeartRateData(ANT_MESSAGE stMessage, std::unordered_map<char*, float> *data);

class ANTController {
public:
    ANTController(MESSAGE_CALLBACK callback);
    virtual ~ANTController();
    BOOL Init(UCHAR usbNumber, UCHAR channelType, USHORT deviceType, USHORT transType, USHORT radioFreq, USHORT period, DATA_PROCESS_CALLBACK callback);
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
    MESSAGE_CALLBACK funcMessageCallback;
    DATA_PROCESS_CALLBACK funcDataProcessCallback;

    BOOL bDisplay;

    UCHAR aucTransmitBuffer[ANT_STANDARD_DATA_PAYLOAD_SIZE];

};