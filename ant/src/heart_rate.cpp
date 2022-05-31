#include "ant.h"

void ProcessHeartRateData(ANT_MESSAGE stMessage, std::unordered_map<char*, float> *data)
{
    // Merge the 2 bytes to form the HRM event time
    //USHORT usEventTime = ((USHORT)stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX + 5] << 8) +
    //(USHORT)stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX + 4];

    (*data)["hr"] = stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX + 7];
    (*data)["beat_count"] = stMessage.aucData[MESSAGE_BUFFER_DATA2_INDEX + 6];
}

