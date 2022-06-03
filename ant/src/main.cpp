
#define LIB_NAME "ant"
#define MODULE_NAME "ant"

#include <dmsdk/sdk.h>
#include "ant.h"

#include <mutex>

struct CallbackData
{
    const char* message;
    std::unordered_map<char*, float> *data;
};

static dmMutex::HMutex mutex;
static dmArray<CallbackData> callbacksQueue;
std::unordered_map<char*, float> recieved;

ANTController* controller;
dmScript::LuaCallbackInfo* callback;

static int GetHeartRate(lua_State* L) {
    lua_pushnumber(L, recieved["hr"]);
    return 1;
}

static void InvokeMessageCallback(const char* message, std::unordered_map<char*, float> *data) {

    if (callback == NULL) {
        return;
    }

    if (!dmScript::IsCallbackValid(callback))
    {
        dmLogError("Callback is invalid.");
        return;
    }

    lua_State* L = dmScript::GetCallbackLuaContext(callback);
    int top = lua_gettop(L);

    if (!dmScript::SetupCallback(callback)) {
        return;
    }

    lua_newtable(L);
    int t = lua_gettop(L);
    lua_pushstring(L, "text"); 
    lua_pushstring(L, message); 
    lua_settable(L, t);

    if (data) {
        for (auto it = data->begin(); it != data->end(); ++it) {
            lua_pushstring(L, it->first);
            lua_pushnumber(L, it->second);
            lua_settable(L, t);
        }

    }

    dmScript::PCall(L, 2, 0); // self + # user arguments
    dmScript::TeardownCallback(callback);
    assert(top == lua_gettop(L));
}

void AddToQueueCallback(const char* message, std::unordered_map<char*, float> *data) {
    CallbackData cb;
    cb.message = strdup(message);
    cb.data = NULL;
    
    if (data) {
        cb.data = new std::unordered_map<char*, float>();
        for (auto it = data->begin(); it != data->end(); ++it) {
            (*cb.data)[it->first] = it->second;
        }
    }

    DM_MUTEX_SCOPED_LOCK(mutex);
    if(callbacksQueue.Full())
    {
        callbacksQueue.OffsetCapacity(2);
    }
    callbacksQueue.Push(cb); 
}

void UpdateCallback()
{
    if (callbacksQueue.Empty()) {
        return;
    }

    dmArray<CallbackData> tmp;
    {
        DM_MUTEX_SCOPED_LOCK(mutex);
        tmp.Swap(callbacksQueue);
    }

    for(uint32_t i = 0; i != tmp.Size(); ++i)
    {
        CallbackData* cb = &tmp[i];
        InvokeMessageCallback(cb->message, cb->data);
        if (cb->data) {
            for (auto it = cb->data->begin(); it != cb->data->end(); ++it) {
                recieved[it->first] = it->second;
            }
            
            delete cb->data;
        }
    }

}

static int Test(lua_State* L)  {
    mutex = dmMutex::New();
    std::unordered_map<char*, float> data;
    data["hr"] = 1;
    AddToQueueCallback("XXXX", &data);
    AddToQueueCallback("yyy", NULL);
    return 0;
}

static bool Init(UCHAR usbNumber, UCHAR channelType = 0, USHORT deviceType = 0, USHORT transType = 0, USHORT radioFreq  = 66, USHORT period = 8192, DATA_PROCESS_CALLBACK callback = NULL)
{
    mutex = dmMutex::New();
    controller = new ANTController(AddToQueueCallback);
    if(controller->Init(usbNumber, channelType, deviceType, transType, radioFreq, period, callback))
    {
        return true;
    }

    delete controller;
    controller =  NULL;
    return false;
}

static int InitGeneric(lua_State* L)
{
    UCHAR ucDeviceNumber = (UCHAR)lua_tonumber(L, 1);
    UCHAR ucChannelType = (UCHAR)lua_tonumber(L, 2);

    lua_settop(L, 0);

    if (Init(ucDeviceNumber, ucChannelType))
    {
        lua_pushboolean(L, true);
    }
    else
    {
        lua_pushboolean(L, false);
    }
    
    return 1;

}

static int InitHR(lua_State* L)
{
    UCHAR ucDeviceNumber = (UCHAR)lua_tonumber(L, 1);
   
    lua_settop(L, 0);

    if (Init(ucDeviceNumber, 1 /*slave*/, 120 /*device  type - hr monitor*/, 0 /*transtype*/, 57 /*freq*/, 8070 /*period*/, ProcessHeartRateData))
    {
        lua_pushboolean(L, true);
    }
    else
    {
        lua_pushboolean(L, false);
    }

    return 1;
}

static int Close(lua_State* L)
{
    if (controller) {
        controller->Close();
        delete controller;
    }

    if (callback) {
        dmScript::DestroyCallback(callback); 
    }

    return 0;

}

static int SetCallback(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    if (callback) {
        dmScript::DestroyCallback(callback); 
    }
    callback = dmScript::CreateCallback(L, 1);
    return 0;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] =
{
    {"init", InitGeneric},
    {"init_hr", InitHR},
    {"close", Close},
    {"set_callback", SetCallback},
    {"get_heart_rate", GetHeartRate},
    {"test", Test},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);

    // Register lua names
    luaL_register(L, MODULE_NAME, Module_methods);

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

static dmExtension::Result UpdateANT(dmExtension::Params* params)
{
    UpdateCallback();
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppInitializeMyExtension(dmExtension::AppParams* params)
{
    dmLogInfo("AppInitializeMyExtension");
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeMyExtension(dmExtension::Params* params)
{
    // Init Lua
    LuaInit(params->m_L);
    dmLogInfo("Registered %s Extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeMyExtension(dmExtension::AppParams* params)
{
    dmLogInfo("AppFinalizeMyExtension");
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeMyExtension(dmExtension::Params* params)
{
    dmLogInfo("FinalizeMyExtension");
    return dmExtension::RESULT_OK;
}

static void OnEventMyExtension(dmExtension::Params* params, const dmExtension::Event* event)
{
    switch(event->m_Event)
    {
        case dmExtension::EVENT_ID_ACTIVATEAPP:
            dmLogInfo("OnEventMyExtension - EVENT_ID_ACTIVATEAPP");
            break;
        case dmExtension::EVENT_ID_DEACTIVATEAPP:
            dmLogInfo("OnEventMyExtension - EVENT_ID_DEACTIVATEAPP");
            break;
        case dmExtension::EVENT_ID_ICONIFYAPP:
            dmLogInfo("OnEventMyExtension - EVENT_ID_ICONIFYAPP");
            break;
        case dmExtension::EVENT_ID_DEICONIFYAPP:
            dmLogInfo("OnEventMyExtension - EVENT_ID_DEICONIFYAPP");
            break;
        default:
            dmLogWarning("OnEventMyExtension - Unknown event id");
            break;
    }
}

// Defold SDK uses a macro for setting up extension entry points:
//
// DM_DECLARE_EXTENSION(symbol, name, app_init, app_final, init, update, on_event, final)

// MyExtension is the C++ symbol that holds all relevant extension data.
// It must match the name field in the `ext.manifest`
DM_DECLARE_EXTENSION(ant, LIB_NAME, AppInitializeMyExtension, AppFinalizeMyExtension, InitializeMyExtension, UpdateANT, OnEventMyExtension, FinalizeMyExtension)
