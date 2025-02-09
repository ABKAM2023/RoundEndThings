#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "RoundEndThings.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include <fstream>

#define DEFAULT_SPEED 1.0f
#define DEFAULT_GRAVITY 1.0f

RoundEndThings g_RoundEndThings;
PLUGIN_EXPOSE(RoundEndThings, g_RoundEndThings);

IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars* gpGlobals = nullptr;

IUtilsApi* g_pUtils = nullptr;
IPlayersApi* g_pPlayers = nullptr;
IMenusApi* g_pMenus = nullptr;
ICookiesApi* g_pCookies = nullptr;

struct PlayerSettings
{
    bool bDisableSpeed = false;
    bool bDisableGravity = false;
};

PlayerSettings g_PlayerSettings[64]; 

bool g_bBHOPEnabled = true;
bool g_bSpeedEnabled = true;
bool g_bGravityEnabled = true;
float g_flIncreasedSpeed = 1.5f;
float g_flIncreasedGravity = 1.5f;

CGameEntitySystem* GameEntitySystem()
{
    return g_pUtils->GetCGameEntitySystem();
}

void LoadConfig()
{
    KeyValues* pKVConfig = new KeyValues("Config");
    if (!pKVConfig->LoadFromFile(g_pFullFileSystem, "addons/configs/RoundEndThings/settings.ini")) {
        g_pUtils->ErrorLog("[%s] Failed to load config addons/configs/RoundEndThings/settings.ini", g_PLAPI->GetLogTag());
        return;
    }

    g_bBHOPEnabled = pKVConfig->GetBool("bhop_enabled", true);
    g_bSpeedEnabled = pKVConfig->GetBool("speed_enabled", true);
    g_bGravityEnabled = pKVConfig->GetBool("gravity_enabled", true);
    g_flIncreasedSpeed = pKVConfig->GetFloat("speed", 1.5f);
    g_flIncreasedGravity = pKVConfig->GetFloat("gravity", 1.5f);

    delete pKVConfig;
}

void SavePlayerSettings(int iSlot)
{
    char szSpeed[16], szGravity[16];
    snprintf(szSpeed, sizeof(szSpeed), "%d", g_PlayerSettings[iSlot].bDisableSpeed);
    snprintf(szGravity, sizeof(szGravity), "%d", g_PlayerSettings[iSlot].bDisableGravity);

    g_pCookies->SetCookie(iSlot, "roundend_disable_speed", szSpeed);
    g_pCookies->SetCookie(iSlot, "roundend_disable_gravity", szGravity);
}

void LoadPlayerSettings(int iSlot)
{
    const char* szSpeed = g_pCookies->GetCookie(iSlot, "roundend_disable_speed");
    const char* szGravity = g_pCookies->GetCookie(iSlot, "roundend_disable_gravity");

    if (szSpeed && szSpeed[0])
    {
        g_PlayerSettings[iSlot].bDisableSpeed = atoi(szSpeed) != 0;
    }

    if (szGravity && szGravity[0])
    {
        g_PlayerSettings[iSlot].bDisableGravity = atoi(szGravity) != 0;
    }
}

void StartupServer()
{
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = g_pUtils->GetCEntitySystem();
    gpGlobals = g_pUtils->GetCGlobalVars();

    int ret;
    g_pPlayers = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        fprintf(stderr, "RoundEndThings: Failed to get IPlayersApi!\n");
    }

    LoadConfig(); 
}

bool RoundEndThings::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

    g_SMAPI->AddListener(this, this);
    return true;
}

bool RoundEndThings::Unload(char* error, size_t maxlen)
{
    ConVar_Unregister();
    
    if (g_pUtils)
    {
        g_pUtils->ClearAllHooks(g_PLID);
    }
    return true;
}

void OpenRoundEndSettingsMenu(int iSlot);

void RoundEndThings::AllPluginsLoaded()
{
    int ret;
    char error[64];

    g_pUtils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        V_strncpy(error, "Failed to lookup utils api. Aborting", sizeof(error));
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    g_pUtils->StartupServer(g_PLID, StartupServer);

    g_pMenus = (IMenusApi*)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        V_strncpy(error, "Failed to lookup menus api. Aborting", sizeof(error));
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }

    g_pCookies = (ICookiesApi*)g_SMAPI->MetaFactory(COOKIES_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        V_strncpy(error, "Failed to lookup cookies api. Aborting", sizeof(error));
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }

    g_pUtils->RegCommand(g_PLID, {"mm_ret"}, {"!ret"}, [](int iSlot, const char* szContent) -> bool
    {
        OpenRoundEndSettingsMenu(iSlot);
        return true;
    });

    g_pUtils->HookEvent(g_PLID, "round_end", [](const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
    {
         g_RoundEndThings.OnRoundEnd();
    });

    g_pUtils->HookEvent(g_PLID, "round_start", [](const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
    {
         g_RoundEndThings.OnRoundStart();
    });

    g_pCookies->HookClientCookieLoaded(g_PLID, [](int iSlot)
    {
        LoadPlayerSettings(iSlot);
    });
}

void OpenRoundEndSettingsMenu(int iSlot)
{
    Menu hMenu;
    g_pMenus->SetTitleMenu(hMenu, "Настройки конца раунда");

    if (g_bSpeedEnabled)
    {
        g_pMenus->AddItemMenu(hMenu, "speed", g_PlayerSettings[iSlot].bDisableSpeed ? "Отключить скорость [X]" : "Отключить скорость [ ]");
    }

    if (g_bGravityEnabled)
    {
        g_pMenus->AddItemMenu(hMenu, "gravity", g_PlayerSettings[iSlot].bDisableGravity ? "Отключить гравитацию [X]" : "Отключить гравитацию [ ]");
    }

    g_pMenus->SetExitMenu(hMenu, true);
    g_pMenus->SetCallback(hMenu, [](const char* szBack, const char* szFront, int iItem, int iSlot)
    {
        if (iItem < 7)
        {
            if (!strcmp(szBack, "speed"))
            {
                g_PlayerSettings[iSlot].bDisableSpeed = !g_PlayerSettings[iSlot].bDisableSpeed;
                g_pUtils->PrintToChat(iSlot, "Настройка скорости изменена.");
            }
            else if (!strcmp(szBack, "gravity"))
            {
                g_PlayerSettings[iSlot].bDisableGravity = !g_PlayerSettings[iSlot].bDisableGravity;
                g_pUtils->PrintToChat(iSlot, "Настройка гравитации изменена.");
            }
            SavePlayerSettings(iSlot); 
            OpenRoundEndSettingsMenu(iSlot); 
        }
    });
    g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

void RoundEndThings::OnRoundEnd()
{
    if (g_bBHOPEnabled)
    {
        engine->ServerCommand("sv_autobunnyhopping 1\n");
        engine->ServerCommand("sv_enablebunnyhopping 1\n");
    }

    for (int i = 0; i < 64; ++i)
    {
        CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(i);
        if (!pPlayerController)
            continue;
        CCSPlayerPawn* pPlayerPawn = pPlayerController->m_hPlayerPawn();
        if (!pPlayerPawn || !pPlayerPawn->IsAlive())
            continue;

        if (g_bSpeedEnabled && !g_PlayerSettings[i].bDisableSpeed)
        {
            pPlayerPawn->m_flVelocityModifier() = g_flIncreasedSpeed;
        }
        else
        {
            pPlayerPawn->m_flVelocityModifier() = DEFAULT_SPEED;
        }

        if (g_bGravityEnabled && !g_PlayerSettings[i].bDisableGravity)
        {
            pPlayerPawn->m_flGravityScale() = g_flIncreasedGravity;
        }
        else
        {
            pPlayerPawn->m_flGravityScale() = DEFAULT_GRAVITY;
        }
    }
}

void RoundEndThings::OnRoundStart()
{
    if (g_bBHOPEnabled)
    {
        engine->ServerCommand("sv_autobunnyhopping 0\n");
        engine->ServerCommand("sv_enablebunnyhopping 0\n");
    }

    for (int i = 0; i < 64; ++i)
    {
        CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(i);
        if (!pPlayerController)
            continue;
        CCSPlayerPawn* pPlayerPawn = pPlayerController->m_hPlayerPawn();
        if (!pPlayerPawn || !pPlayerPawn->IsAlive())
            continue;

        pPlayerPawn->m_flVelocityModifier() = DEFAULT_SPEED;
        pPlayerPawn->m_flGravityScale() = DEFAULT_GRAVITY;
    }
}

const char* RoundEndThings::GetAuthor()
{
    return "ABKAM";
}

const char* RoundEndThings::GetName()
{
    return "RoundEndThings";
}

const char* RoundEndThings::GetDescription()
{
    return "RoundEndThings";
}

const char* RoundEndThings::GetLicense()
{
    return "GPL";
}

const char* RoundEndThings::GetVersion()
{
    return "1.0";
}

const char* RoundEndThings::GetDate()
{
    return __DATE__;
}

const char* RoundEndThings::GetLogTag()
{
    return "RoundEndThings";
}

const char* RoundEndThings::GetURL()
{
    return "";
}
