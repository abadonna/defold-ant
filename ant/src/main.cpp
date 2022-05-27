
#define LIB_NAME "ant"
#define MODULE_NAME "ant"

#include <dmsdk/sdk.h>
#include "ant.h"

ANTController* controller;

struct Listener {
    Listener() : L(0), callback(LUA_NOREF), self(LUA_NOREF) {}
    lua_State* L;
    int callback;
    int self;
};

static void UnregisterCallback(lua_State* L, Listener* cbk);
static int GetEqualIndexOfListener(lua_State* L, Listener* cbk);

dmArray<Listener> listeners;

static bool CheckCallback(Listener* cbk)
{
    if(cbk->callback == LUA_NOREF)
    {
        dmLogInfo("Callback do not exist.");
        return false;
    }
    lua_State* L = cbk->L;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, cbk->callback);
    //[-1] - callback
    lua_rawgeti(L, LUA_REGISTRYINDEX, cbk->self);
    //[-1] - self
    //[-2] - callback
    lua_pushvalue(L, -1);
    //[-1] - self
    //[-2] - self
    //[-3] - callback
    dmScript::SetInstance(L);
    //[-1] - self
    //[-2] - callback
    if (!dmScript::IsInstanceValid(L)) {
        UnregisterCallback(L, cbk);
        dmLogError("Could not run callback because the instance has been deleted.");
        lua_pop(L, 2);
        assert(top == lua_gettop(L));
        return false;
    }
    return true;
}

static int GetEqualIndexOfListener(lua_State* L, Listener* cbk)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, cbk->callback);
    int first = lua_gettop(L);
    int second = first + 1;
    for(uint32_t i = 0; i != listeners.Size(); ++i)
    {
        Listener* cb = &listeners[i];
        lua_rawgeti(L, LUA_REGISTRYINDEX, cb->callback);
        if (lua_equal(L, first, second)){
            lua_pop(L, 1);
            lua_rawgeti(L, LUA_REGISTRYINDEX, cbk->self);
            lua_rawgeti(L, LUA_REGISTRYINDEX, cb->self);
            if (lua_equal(L, second, second + 1)){
                lua_pop(L, 3);
                return i;
            }
            lua_pop(L, 2);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return -1;
}

static void UnregisterCallback(lua_State* L, Listener* cbk)
{
    int index = GetEqualIndexOfListener(L, cbk);
    if (index >= 0){
        if(cbk->callback != LUA_NOREF)
        {
            dmScript::Unref(cbk->L, LUA_REGISTRYINDEX, cbk->callback);
            dmScript::Unref(cbk->L, LUA_REGISTRYINDEX, cbk->self);
            cbk->callback = LUA_NOREF;
        }
        listeners.EraseSwap(index);
    } else {
        dmLogError("Can't remove a callback that didn't not register.");
    }
}

static void SendMessageToListeners(const char* message) {
    for(int i = listeners.Size() - 1; i >= 0; --i)
    {
        if (i > listeners.Size()){
            return;
        }
        Listener* cbk = &listeners[i];
        lua_State* L = cbk->L;
        int top = lua_gettop(L);
        if (CheckCallback(cbk)) {
            lua_pushstring(L, message); //message_id
            //lua_pushlstring(L, message, 1);
            int ret = lua_pcall(L, 2, 0, 0);
            if(ret != 0) {
                dmLogError("Error running callback: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        assert(top == lua_gettop(L));
    }
}

static int Test(lua_State* L)  {
    SendMessageToListeners("XXXX");
    return 0;
}

static int Init(lua_State* L)
{
    UCHAR ucDeviceNumber = (UCHAR)lua_tonumber(L, 1);
    UCHAR ucChannelType = (UCHAR)lua_tonumber(L, 2);

    lua_settop(L, 0);
    
    controller = new ANTController(SendMessageToListeners);
    if(controller->Init(ucDeviceNumber, ucChannelType))
    {
        lua_pushboolean(L, true);
    }
    else
    {
        delete controller;
        controller =  NULL;
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

    return 0;

}

static int AddListener(lua_State* L)
{
    Listener cbk;
    cbk.L = dmScript::GetMainThread(L);

    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    cbk.callback = dmScript::Ref(L, LUA_REGISTRYINDEX);

    dmScript::GetInstance(L);
    cbk.self = dmScript::Ref(L, LUA_REGISTRYINDEX);

    if(cbk.callback != LUA_NOREF)
    {
        if(listeners.Full())
        {
            listeners.OffsetCapacity(1);
        }
        listeners.Push(cbk);
    }
    return 0;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] =
{
    {"init", Init},
    {"close", Close},
    {"add_listener", AddListener},
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
DM_DECLARE_EXTENSION(ant, LIB_NAME, AppInitializeMyExtension, AppFinalizeMyExtension, InitializeMyExtension, 0, OnEventMyExtension, FinalizeMyExtension)
