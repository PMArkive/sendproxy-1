/**
 * vim: set ts=4 :
 * =============================================================================
 * SendVar Proxy Manager
 * Copyright (C) 2011-2019 Afronanny & AlliedModders community.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#ifdef _WIN32
#undef GetProp
#ifdef _WIN64
	#define PLATFORM_x64
#else
	#define PLATFORM_x32
#endif
#elif defined __linux__
	#if defined __x86_64__
		#define PLATFORM_x64
	#else
		#define PLATFORM_x32
	#endif
#endif

#include "CDetour/detours.h"
#include "extension.h"
#include "interfaceimpl.h"
#include "natives.h"

//path: hl2sdk-<your sdk here>/public/<include>.h, "../public/" included to prevent compile errors due wrong directory scanning by compiler on my computer, and I'm too lazy to find where I can change that =D
#include <../public/iserver.h>
#include <../public/iclient.h>

#define DECL_DETOUR(name) \
	CDetour *name##_Detour = nullptr;

#define CREATE_DETOUR(name, signname, var) \
	if (name##_Detour == NULL) \
	{ \
		name##_Detour = DETOUR_CREATE_MEMBER(name, signname); \
		if(name##_Detour) { \
			name##_Detour->EnableDetour(); \
			var = true; \
		} else { \
			g_pSM->LogError(myself, "Failed to create " signname " detour, check error log.\n"); \
			var = false; \
		} \
	} else { \
		name##_Detour->EnableDetour(); \
		var = true; \
	}

#define CREATE_DETOUR_STATIC(name, signname, var) \
	if (name##_Detour == NULL) \
	{ \
		name##_Detour = DETOUR_CREATE_STATIC(name, signname); \
		if(name##_Detour) { \
			name##_Detour->EnableDetour(); \
			var = true; \
		} else { \
			g_pSM->LogError(myself, "Failed to create " signname " detour, check error log.\n"); \
			var = false; \
		} \
	} else { \
		name##_Detour->EnableDetour(); \
		var = true; \
	}

#define DISABLE_DETOUR(name) \
	if (name##_Detour != nullptr) \
	{ \
		name##_Detour->DisableDetour(); \
	}
	
#define DESTROY_DETOUR(name) \
	if (name##_Detour != nullptr) \
	{ \
		name##_Detour->Destroy(); \
		name##_Detour = nullptr; \
	}

SH_DECL_HOOK1_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, false, edict_t *);
SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, false, bool);
SH_DECL_HOOK0(IServer, GetClientCount, const, false, int);

DECL_DETOUR(CGameServer_SendClientMessages);
DECL_DETOUR(CGameClient_ShouldSendMessages);
DECL_DETOUR(SV_ComputeClientPacks);

class CGameClient;
class CFrameSnapshot;
class CGlobalEntityList;

CGameClient * g_pCurrentGameClientPtr = nullptr;
int g_iCurrentClientIndexInLoop = -1; //used for optimization
bool g_bCurrentGameClientCallFwd = false;
bool g_bCallingForNullClients = false;
bool g_bFirstTimeCalled = true;
bool g_bSVComputePacksDone = true;
IServer * g_pIServer = nullptr;

SendProxyManager g_SendProxyManager;
SendProxyManagerInterfaceImpl * g_pMyInterface = nullptr;
SMEXT_LINK(&g_SendProxyManager);

CThreadFastMutex g_WorkMutex;

#include <vector>

std::vector<SendPropHook> g_Hooks;
std::vector<SendPropHookGamerules> g_HooksGamerules;
std::vector<PropChangeHook> g_ChangeHooks;
std::vector<PropChangeHookGamerules> g_ChangeHooksGamerules;

std::vector<edict_t *> g_vHookedEdicts;

IServerGameEnts * gameents = nullptr;
IServerGameClients * gameclients = nullptr;
ISDKTools * g_pSDKTools = nullptr;
ISDKHooks * g_pSDKHooks = nullptr;
IGameConfig * g_pGameConf = nullptr;
IGameConfig * g_pGameConfSDKTools = nullptr;

ConVar * sv_parallel_packentities = nullptr;
ConVar * sv_parallel_sendsnapshot = nullptr;

edict_t * g_pGameRulesProxyEdict = nullptr;
void * g_pGameRules = nullptr;
bool g_bShouldChangeGameRulesState = false;
bool g_bSendSnapshots = false;

size_t g_numPerClientHooks = 0;

CGlobalVars * g_pGlobals = nullptr;

static CBaseEntity * FindEntityByServerClassname(int, const char *);
static void CallChangeCallbacks(const PropChangeHook &pInfo, void * pOldValue, void * pNewValue);
static void CallChangeGamerulesCallbacks(const PropChangeHookGamerules &pInfo, void * pOldValue, void * pNewValue);

const char * g_szGameRulesProxy;

//detours

/*Call stack:
	...
	1. CGameServer::SendClientMessages //function we hooking to send props individually for each client
	2. SV_ComputeClientPacks //function we hooking to set edicts state and to know, need we call callbacks or not, but not in csgo
	3. PackEntities_Normal //if we in multiplayer
	4. SV_PackEntity //also we can hook this instead hooking ProxyFn, but there no reason to do that
	5. SendTable_Encode
	6. SendTable_EncodeProp //here the ProxyFn will be called
	7. ProxyFn //here our callbacks is called
*/

DETOUR_DECL_MEMBER1(CGameServer_SendClientMessages, void, bool, bSendSnapshots)
{
	if (!bSendSnapshots)
	{
		g_bSendSnapshots = false;
		return DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(false); //if so, we do not interested in this call
	}
	else
		g_bSendSnapshots = true;
	if (!g_pIServer && g_pSDKTools)
		g_pIServer = g_pSDKTools->GetIServer();
	if (!g_pIServer)
		return DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true); //if so, we should stop to process this function! See below
	if (g_bFirstTimeCalled)
	{
#ifdef _WIN32
		//HACK, don't delete this, or server will be crashed on start!
		g_pIServer->GetClientCount();
#endif
		SH_ADD_HOOK(IServer, GetClientCount, g_pIServer, SH_MEMBER(&g_SendProxyManager, &SendProxyManager::GetClientCount), false);
		g_bFirstTimeCalled = false;
	}
	bool bCalledForNullIClientsThisTime = false;
	for (int iClients = 1; iClients <= playerhelpers->GetMaxClients(); iClients++)
	{
		IGamePlayer * pPlayer = playerhelpers->GetGamePlayer(iClients);
		bool bFake = (pPlayer->IsFakeClient() && !(pPlayer->IsSourceTV()
#if SOURCE_ENGINE == SE_TF2
		|| pPlayer->IsReplay()
#endif
		));
		volatile IClient * pClient = nullptr; //volatile used to prevent optimizations here for some reason
		if (!pPlayer->IsConnected() || bFake || (pClient = g_pIServer->GetClient(iClients - 1)) == nullptr)
		{
			if (!bCalledForNullIClientsThisTime && !g_bCallingForNullClients)
			{
				g_bCurrentGameClientCallFwd = false;
				g_bCallingForNullClients = true;
				DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true);
				g_bCallingForNullClients = false;
			}
			bCalledForNullIClientsThisTime = true;
			continue;
		}
		if (!pPlayer->IsInGame() || bFake) //We should call SV_ComputeClientPacks, but shouldn't call forwards!
			g_bCurrentGameClientCallFwd = false;
		else
			g_bCurrentGameClientCallFwd = true;
		g_pCurrentGameClientPtr = (CGameClient *)((char *)pClient - 4);
		g_iCurrentClientIndexInLoop = iClients - 1;
		DETOUR_MEMBER_CALL(CGameServer_SendClientMessages)(true);
	}
	g_bCurrentGameClientCallFwd = false;
	g_iCurrentClientIndexInLoop = -1;
	g_bShouldChangeGameRulesState = false;
}

DETOUR_DECL_MEMBER0(CGameClient_ShouldSendMessages, bool)
{
	if (!g_bSendSnapshots)
		return DETOUR_MEMBER_CALL(CGameClient_ShouldSendMessages)();
	if (g_bCallingForNullClients)
	{
		IClient * pClient = (IClient *)((char *)this + 4);
#if SOURCE_ENGINE == SE_TF2
		//don't remove this code
		int iUserID = pClient->GetUserID();
		IGamePlayer * pPlayer = playerhelpers->GetGamePlayer(pClient->GetPlayerSlot() + 1);
		if (pPlayer->GetUserId() != iUserID) //if so, there something went wrong, check this now!
#endif
		{
			if (pClient->IsHLTV()
#if SOURCE_ENGINE == SE_TF2
			|| pClient->IsReplay()
#endif
			|| (pClient->IsConnected() && !pClient->IsActive()))
				return true; //Also we need to allow connect for inactivated clients, sourcetv & replay
		}
		return false;
	}
	bool bOriginalResult = DETOUR_MEMBER_CALL(CGameClient_ShouldSendMessages)();
	if (!bOriginalResult)
		return false;
	if ((CGameClient *)this == g_pCurrentGameClientPtr)
		return true;
#if defined PLATFORM_x32
	else
	{
		volatile int iToSet = g_iCurrentClientIndexInLoop - 1;
#if SOURCE_ENGINE == SE_TF2
#ifdef _WIN32
		//some little trick to deceive msvc compiler
		__asm _emit 0x5F
		__asm _emit 0x5E
		__asm push edx
		__asm mov edx, iToSet
		__asm _emit 0x3B
		__asm _emit 0xF2
		__asm jge CompFailed
		__asm _emit 0x8B
		__asm _emit 0xF2
		__asm CompFailed:
		__asm pop edx
		__asm _emit 0x56
		__asm _emit 0x57
#elif defined __linux__
		volatile int iTemp;
		asm volatile("movl %%esi, %0" : "=g" (iTemp));
		if (iTemp < iToSet)
			asm volatile(
				"movl %0, %%esi\n\t"
				"movl %%esi, %%edx\n\t"
				"addl $84, %%esp\n\t"
				"popl %%esi\n\t"
				"pushl %%edx\n\t"
				"subl $84, %%esp\n\t"
				: : "g" (iToSet) : "%edx");
#endif
#elif SOURCE_ENGINE == SE_CSGO
#ifdef _WIN32
		volatile int iEax, iEdi, iEsi;
			//save registers
		__asm mov iEdi, edi
		__asm mov iEsi, esi
		__asm mov iEax, eax
		__asm mov eax, ebp
			//load stack ptr
			//we need to pop esi and edi to pop ebp register, we don't care about values in these, we also will use them as variables
		__asm pop esi
		__asm pop edi
		__asm mov edi, iToSet
		__asm mov esp, ebp
		__asm pop ebp
			//load needed info and compare
		__asm mov esi, [ebp-0x7F8] //0x7F8 is an offset of loop variable
		__asm cmp esi, edi
		__asm jge CompFailed
			//good, store our value
		__asm mov [ebp-0x7F8], edi
		__asm CompFailed:
			//push old and restore original registers
		__asm push ebp
		__asm mov ebp, eax
		__asm mov esp, ebp
		__asm sub esp, 0x10
		__asm mov esi, iEsi
		__asm mov edi, iEdi
		__asm mov eax, iEax
		__asm push edi
		__asm push esi
#elif defined __linux__
		volatile int iTemp;
		//we don't need to clubber edi register here, some low level shit
		asm volatile("movl %%edi, %0" : "=g" (iTemp));
		if (iTemp < iToSet)
			asm volatile("movl %0, %%edi" : : "g" (iToSet));
#endif
#endif
	}
#endif
	return false;
}

#if defined __linux__
void __attribute__((__cdecl__)) SV_ComputeClientPacks_ActualCall(int iClientCount, CGameClient ** pClients, CFrameSnapshot * pSnapShot);
#else
void __cdecl SV_ComputeClientPacks_ActualCall(int iClientCount, CGameClient ** pClients, CFrameSnapshot * pSnapShot);
#endif

//the better idea rewrite it with __declspec(naked) for csgo or use __stdcall function as main callback instead of this
DETOUR_DECL_STATIC3(SV_ComputeClientPacks, void, int, iClientCount, CGameClient **, pClients, CFrameSnapshot *, pSnapShot)
{
#if defined _WIN32 && SOURCE_ENGINE == SE_CSGO
	//so, here it is __userpurge call, we need manually get our arguments
	__asm mov iClientCount, ecx
	__asm mov pClients, edx
	__asm mov pSnapShot, ebx
#endif
	g_bSVComputePacksDone = false;
	if (!iClientCount || !g_bSendSnapshots || pClients[0] != g_pCurrentGameClientPtr)
		return SV_ComputeClientPacks_ActualCall(iClientCount, pClients, pSnapShot);
	IClient * pClient = (IClient *)((char *)pClients[0] + 4);
	int iClient = pClient->GetPlayerSlot();
	if (g_iCurrentClientIndexInLoop != iClient)
		return SV_ComputeClientPacks_ActualCall(iClientCount, pClients, pSnapShot);
	//Also here we can change actual values for each client! But for what?
	//Just mark all hooked edicts as changed to bypass check in SV_PackEntity!
	for (auto pEdict : g_vHookedEdicts)
	{
		if (pEdict && !(pEdict->m_fStateFlags & FL_EDICT_CHANGED))
			pEdict->m_fStateFlags |= FL_EDICT_CHANGED;
	}
	if (g_bShouldChangeGameRulesState && g_pGameRulesProxyEdict)
	{
		if (!(g_pGameRulesProxyEdict->m_fStateFlags & FL_EDICT_CHANGED))
			g_pGameRulesProxyEdict->m_fStateFlags |= FL_EDICT_CHANGED;
	}
	if (g_bCurrentGameClientCallFwd)
		g_bSVComputePacksDone = true;
	return SV_ComputeClientPacks_ActualCall(iClientCount, pClients, pSnapShot);
}

#if defined _WIN32 && SOURCE_ENGINE == SE_CSGO
__declspec(naked) void __cdecl SV_ComputeClientPacks_ActualCall(int iClientCount, CGameClient ** pClients, CFrameSnapshot * pSnapShot)
{
	//we do not use ebp here
	__asm mov edx, pClients //we don't care about values in edx & ecx
	__asm mov ecx, iClientCount
	__asm mov ebx, pSnapShot
	__asm push ebx
	__asm call SV_ComputeClientPacks_Actual
	__asm add esp, 0x4 //restore our stack
	__asm retn
}
#else
#ifdef __linux__
void __attribute__((__cdecl__)) SV_ComputeClientPacks_ActualCall(int iClientCount, CGameClient ** pClients, CFrameSnapshot * pSnapShot)
#else
void __cdecl SV_ComputeClientPacks_ActualCall(int iClientCount, CGameClient ** pClients, CFrameSnapshot * pSnapShot)
#endif
{
	return DETOUR_STATIC_CALL(SV_ComputeClientPacks)(iClientCount, pClients, pSnapShot);
}
#endif

//hooks

void DoOnEntityDestroyed(int idx)
{
	auto it = g_Hooks.begin();
	while(it != g_Hooks.end())
	{
		auto &hook = *it;
		if (hook.objectID == idx)
		{
			g_SendProxyManager.UnhookProxy(hook);
			g_Hooks.erase(it);
			continue;
		}
		
		++it;
	}

	auto it2 = g_ChangeHooks.begin();
	while(it2 != g_ChangeHooks.end())
	{
		if (it2->objectID == idx) {
			g_ChangeHooks.erase(it2);
			continue;
		}
		
		++it2;
	}
}

void SendProxyManager::OnEntityDestroyed(CBaseEntity* pEnt)
{
	int idx = gamehelpers->EntityToBCompatRef(pEnt);
	DoOnEntityDestroyed(idx);
}

void Hook_ClientDisconnect(edict_t * pEnt)
{
	int idx = gamehelpers->IndexOfEdict(pEnt);
	DoOnEntityDestroyed(idx);
	RETURN_META(MRES_IGNORED);
}

void Hook_GameFrame(bool simulating)
{
	if (simulating)
	{
		for (auto &it : g_ChangeHooks)
		{
			switch(it.propType)
			{
				case PropType::Prop_Int:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(it.objectID);
					CBaseEntity * pEntity = gameents->EdictToBaseEntity(pEnt);
					int iCurrent = *(int *)((unsigned char *)pEntity + it.Offset);
					if (iCurrent != it.iLastValue)
					{
						CallChangeCallbacks(it, (void *)&it.iLastValue, (void *)&iCurrent);
						it.iLastValue = iCurrent;
					}
					break;
				}
				case PropType::Prop_Float:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(it.objectID);
					CBaseEntity * pEntity = gameents->EdictToBaseEntity(pEnt);
					float flCurrent = *(float *)((unsigned char *)pEntity + it.Offset);
					if (flCurrent != it.flLastValue)
					{
						CallChangeCallbacks(it, (void *)&it.flLastValue, (void *)&flCurrent);
						it.flLastValue = flCurrent;
					}
					break;
				}
				case PropType::Prop_String:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(it.objectID);
					CBaseEntity * pEntity = gameents->EdictToBaseEntity(pEnt);
					const char * szCurrent = (const char *)((unsigned char *)pEntity + it.Offset);
					if (strcmp(szCurrent, it.cLastValue) != 0)
					{
						CallChangeCallbacks(it, (void *)it.cLastValue, (void *)szCurrent);
						memset(it.cLastValue, 0, sizeof(it.cLastValue));
						strncpynull(it.cLastValue, szCurrent, sizeof(it.cLastValue));
					}
					break;
				}
				case PropType::Prop_Vector:
				{
					edict_t * pEnt = gamehelpers->EdictOfIndex(it.objectID);
					CBaseEntity * pEntity = gameents->EdictToBaseEntity(pEnt);
					Vector * pVec = (Vector *)((unsigned char *)pEntity + it.Offset);
					if (it.vecLastValue != *pVec)
					{
						CallChangeCallbacks(it, (void *)&it.vecLastValue, (void *)pVec);
						it.vecLastValue = *pVec;
					}
					break;
				}
				default: rootconsole->ConsolePrint("%s: SendProxy report: Unknown prop type (%s).", __func__, it.pVar->GetName());
			}
		}
		if (!g_pGameRules && g_pSDKTools)
		{
			g_pGameRules = g_pSDKTools->GetGameRules();
			if (!g_pGameRules)
			{
				g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
				return;
			}
		}
		//Gamerules hooks
		for (auto &it : g_ChangeHooksGamerules)
		{
			switch(it.propType)
			{
				case PropType::Prop_Int:
				{
					int iCurrent = *(int *)((unsigned char *)g_pGameRules + it.Offset);
					if (iCurrent != it.iLastValue)
					{
						CallChangeGamerulesCallbacks(it, (void *)&it.iLastValue, (void *)&iCurrent);
						it.iLastValue = iCurrent;
					}
					break;
				}
				case PropType::Prop_Float:
				{
					float flCurrent = *(float *)((unsigned char *)g_pGameRules + it.Offset);
					if (flCurrent != it.flLastValue)
					{
						CallChangeGamerulesCallbacks(it, (void *)&it.flLastValue, (void *)&flCurrent);
						it.flLastValue = flCurrent;
					}
					break;
				}
				case PropType::Prop_String:
				{
					const char * szCurrent = (const char *)((unsigned char *)g_pGameRules + it.Offset);
					if (strcmp(szCurrent, it.cLastValue) != 0)
					{
						CallChangeGamerulesCallbacks(it, (void *)it.cLastValue, (void *)szCurrent);
						memset(it.cLastValue, 0, sizeof(it.cLastValue));
						strncpynull(it.cLastValue, szCurrent, sizeof(it.cLastValue));
					}
					break;
				}
				case PropType::Prop_Vector:
				{
					Vector * pVec = (Vector *)((unsigned char *)g_pGameRules + it.Offset);
					if (it.vecLastValue != *pVec)
					{
						CallChangeGamerulesCallbacks(it, (void *)&it.vecLastValue, (void *)pVec);
						it.vecLastValue = *pVec;
					}
					break;
				}
				default: rootconsole->ConsolePrint("%s: SendProxy report: Unknown prop type (%s).", __func__, it.pVar->GetName());
			}
		}
	}
	RETURN_META(MRES_IGNORED);
}

int SendProxyManager::GetClientCount() const
{
	if (g_iCurrentClientIndexInLoop != -1)
		RETURN_META_VALUE(MRES_SUPERCEDE, g_iCurrentClientIndexInLoop + 1);
	RETURN_META_VALUE(MRES_IGNORED, 0/*META_RESULT_ORIG_RET(int)*/);
}

//main sm class implementation

bool SendProxyManager::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	char conf_error[255];
	if (!gameconfs->LoadGameConfigFile("sdktools.games", &g_pGameConfSDKTools, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
			snprintf(error, maxlength, "Could not read config file sdktools.games.txt: %s", conf_error);
		return false;
	}
	
	g_szGameRulesProxy = g_pGameConfSDKTools->GetKeyValue("GameRulesProxy");
	
	if (!gameconfs->LoadGameConfigFile("sendproxy", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
			snprintf(error, maxlength, "Could not read config file sendproxy.txt: %s", conf_error);
		return false;
	}
	
	CDetourManager::Init(smutils->GetScriptingEngine(), g_pGameConf);

	if (late) //if we loaded late, we need manually to call that
		OnCoreMapStart(nullptr, 0, 0);
	
	sharesys->AddDependency(myself, "sdktools.ext", true, true);
	sharesys->AddDependency(myself, "sdkhooks.ext", true, true);
	
	g_pMyInterface = new SendProxyManagerInterfaceImpl();
	sharesys->AddInterface(myself, g_pMyInterface);
	//we should not maintain compatibility with old plugins which uses earlier versions of sendproxy (< 1.3)
	sharesys->RegisterLibrary(myself, "sendproxy14");
	plsys->AddPluginsListener(this);

	return true;
}

void SendProxyManager::SDK_OnAllLoaded()
{
	sharesys->AddNatives(myself, g_MyNatives);
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);

	if (g_pSDKHooks)
	{
		g_pSDKHooks->AddEntityListener(this);
	}
}

void SendProxyManager::SDK_OnUnload()
{
	for (auto &it : g_Hooks)
	{
		it.pVar->SetProxyFn(it.pRealProxy);
	}
	
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);
	if (!g_bFirstTimeCalled)
		SH_REMOVE_HOOK(IServer, GetClientCount, g_pIServer, SH_MEMBER(this, &SendProxyManager::GetClientCount), false);

	DESTROY_DETOUR(CGameServer_SendClientMessages);
	DESTROY_DETOUR(CGameClient_ShouldSendMessages);
	DESTROY_DETOUR(SV_ComputeClientPacks);
	
	gameconfs->CloseGameConfigFile(g_pGameConf);
	gameconfs->CloseGameConfigFile(g_pGameConfSDKTools);
	
	plsys->RemovePluginsListener(this);
	if( g_pSDKHooks )
	{
		g_pSDKHooks->RemoveEntityListener(this);
	}
	delete g_pMyInterface;
}

void SendProxyManager::OnCoreMapEnd()
{
	for (auto &it : g_HooksGamerules)
	{
		UnhookProxyGamerules(it);
	}
	g_HooksGamerules.clear();
	
	g_pGameRulesProxyEdict = nullptr;
	
	if(g_numPerClientHooks > 0) {
		DISABLE_DETOUR(CGameServer_SendClientMessages);
		DISABLE_DETOUR(CGameClient_ShouldSendMessages);
		DISABLE_DETOUR(SV_ComputeClientPacks);
		g_iCurrentClientIndexInLoop = -1;
		g_bSVComputePacksDone = true;
	}
}

void SendProxyManager::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	CBaseEntity * pGameRulesProxyEnt = FindEntityByServerClassname(0, g_szGameRulesProxy);
	if (!pGameRulesProxyEnt)
	{
		smutils->LogError(myself, "Unable to get gamerules proxy ent (1)!");
		return;
	}
	g_pGameRulesProxyEdict = gameents->BaseEntityToEdict(pGameRulesProxyEnt);
	if (!g_pGameRulesProxyEdict)
		smutils->LogError(myself, "Unable to get gamerules proxy ent (2)!");
	
	if(g_numPerClientHooks > 0) {
		bool bDetoursInited = false;
		g_bSVComputePacksDone = false;
		g_iCurrentClientIndexInLoop = -1;
		CREATE_DETOUR(CGameServer_SendClientMessages, "CGameServer::SendClientMessages", bDetoursInited);
		CREATE_DETOUR(CGameClient_ShouldSendMessages, "CGameClient::ShouldSendMessages", bDetoursInited);
		CREATE_DETOUR_STATIC(SV_ComputeClientPacks, "SV_ComputeClientPacks", bDetoursInited);
	}
}

bool SendProxyManager::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	
	g_pGlobals = ismm->GetCGlobals();
	
	SH_ADD_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	
	GET_CONVAR(sv_parallel_packentities);
	sv_parallel_packentities->SetValue(0); //If we don't do that the sendproxy extension will crash the server (Post ref: https://forums.alliedmods.net/showpost.php?p=2540106&postcount=324 )
	GET_CONVAR(sv_parallel_sendsnapshot);
	sv_parallel_sendsnapshot->SetValue(0); //If we don't do that, sendproxy will not work correctly and may crash server. This affects all versions of sendproxy manager!
	
	return true;
}

void SendProxyManager::OnPluginUnloaded(IPlugin * plugin)
{
	IPluginContext * pCtx = plugin->GetBaseContext();
	auto it = g_Hooks.begin();
	while(it != g_Hooks.end())
	{
		auto &hook = *it;
		
		if (hook.sCallbackInfo.iCallbackType == CallBackType::Callback_PluginFunction &&
			((IPluginFunction *)hook.sCallbackInfo.pCallback)->GetParentContext() == pCtx)
		{
			UnhookProxy(hook);
			g_Hooks.erase(it);
			continue;
		}
		
		++it;
	}
	auto it2 = g_HooksGamerules.begin();
	while(it2 != g_HooksGamerules.end())
	{
		auto &hook = *it2;
		
		if (hook.sCallbackInfo.iCallbackType == CallBackType::Callback_PluginFunction &&
			((IPluginFunction *)hook.sCallbackInfo.pCallback)->GetParentContext() == pCtx)
		{
			UnhookProxyGamerules(hook);
			g_HooksGamerules.erase(it2);
			continue;
		}
		
		++it2;
	}
	auto it3 = g_ChangeHooks.begin();
	while(it3 != g_ChangeHooks.end())
	{
		auto &pCallbacks = it3->vCallbacksInfo;
		auto it4 = pCallbacks.begin();
		while(it4 != pCallbacks.end()) {
			auto &cb = *it4;
			
			if (cb.iCallbackType == CallBackType::Callback_PluginFunction &&
				(IPluginContext *)cb.pOwner == pCtx)
			{
				pCallbacks.erase(it4);
				continue;
			}
			
			++it4;
		}
		//else do not needed here
		if (pCallbacks.empty()) {
			g_ChangeHooks.erase(it3);
			continue;
		}
		
		++it3;
	}
	auto it5 = g_ChangeHooksGamerules.begin();
	while(it5 != g_ChangeHooksGamerules.end())
	{
		auto &pCallbacks = it5->vCallbacksInfo;

		auto it6 = pCallbacks.begin();
		while(it6 != pCallbacks.end()) {
			auto &cb = *it6;
			
			if (cb.iCallbackType == CallBackType::Callback_PluginFunction &&
				(IPluginContext *)cb.pOwner == pCtx)
			{
				pCallbacks.erase(it6);
				continue;
			}
			
			++it6;
		}
		//else do not needed here
		if (pCallbacks.empty()) {
			g_ChangeHooksGamerules.erase(it5);
			continue;
		}
		
		++it5;
	}
}

//functions

bool SendProxyManager::AddHookToList(SendPropHook &&hook)
{
	//Need to make sure this prop isn't already hooked for this entity - we don't care anymore
	bool bEdictHooked = false;
	for (auto &it : g_Hooks)
	{
		if (it.objectID == hook.objectID)
		{
			//we don't care anymore
			//if (g_Hooks[i].pVar == hook.pVar)
			//	return false;
			//else
				bEdictHooked = true;
		}
	}
	
	if(hook.per_client) {
		if(++g_numPerClientHooks == 1) {
			bool bDetoursInited = false;
			g_bSVComputePacksDone = false;
			g_iCurrentClientIndexInLoop = -1;
			CREATE_DETOUR(CGameServer_SendClientMessages, "CGameServer::SendClientMessages", bDetoursInited);
			CREATE_DETOUR(CGameClient_ShouldSendMessages, "CGameClient::ShouldSendMessages", bDetoursInited);
			CREATE_DETOUR_STATIC(SV_ComputeClientPacks, "SV_ComputeClientPacks", bDetoursInited);
		}
	}
	
	g_Hooks.emplace_back(std::move(hook));
	if (!bEdictHooked) {
		g_vHookedEdicts.emplace_back(hook.pEnt);
	}
	return true;
}

bool SendProxyManager::AddHookToListGamerules(SendPropHookGamerules &&hook)
{
	//Need to make sure this prop isn't already hooked for this entity - we don't care anymore
	/*for (int i = 0; i < g_HooksGamerules.size(); i++)
	{
		if (g_HooksGamerules[i].pVar == hook.pVar)
			return false;
	}*/
	
	if(hook.per_client) {
		if(++g_numPerClientHooks == 1) {
			bool bDetoursInited = false;
			g_bSVComputePacksDone = false;
			g_iCurrentClientIndexInLoop = -1;
			CREATE_DETOUR(CGameServer_SendClientMessages, "CGameServer::SendClientMessages", bDetoursInited);
			CREATE_DETOUR(CGameClient_ShouldSendMessages, "CGameClient::ShouldSendMessages", bDetoursInited);
			CREATE_DETOUR_STATIC(SV_ComputeClientPacks, "SV_ComputeClientPacks", bDetoursInited);
		}
	}
	
	g_HooksGamerules.emplace_back(std::move(hook));
	return true;
}

bool SendProxyManager::AddChangeHookToList(PropChangeHook &&sHook, CallBackInfo &&pInfo)
{
	PropChangeHook *pHookInfo = nullptr;
	for (auto &it : g_ChangeHooks)
	{
		if (it.pVar == sHook.pVar)
		{
			pHookInfo = &it;
			break;
		}
	}
	if (pHookInfo)
	{
		//just validate it
		switch (sHook.propType)
		{
		case PropType::Prop_Int:
		case PropType::Prop_Float:
		case PropType::Prop_String:
		case PropType::Prop_Vector:
			break;
		default: return false;
		}
		pHookInfo->vCallbacksInfo.emplace_back(std::move(pInfo));
	}
	else
	{
		edict_t * pEnt = gamehelpers->EdictOfIndex(sHook.objectID);
		if (!pEnt || pEnt->IsFree()) return false; //should never happen
		CBaseEntity * pEntity = gameents->EdictToBaseEntity(pEnt);
		if (!pEntity) return false; //should never happen
		switch (sHook.propType)
		{
		case PropType::Prop_Int: sHook.iLastValue = *(int *)((unsigned char *)pEntity + sHook.Offset); break;
		case PropType::Prop_Float: sHook.flLastValue = *(float *)((unsigned char*)pEntity + sHook.Offset); break;
		case PropType::Prop_String: strncpynull(sHook.cLastValue, (const char *)((unsigned char *)pEntity + sHook.Offset), sizeof(sHook.cLastValue)); break;
		case PropType::Prop_Vector: sHook.vecLastValue = *(Vector *)((unsigned char *)pEntity + sHook.Offset); break;
		default: return false;
		}

		sHook.vCallbacksInfo.emplace_back(std::move(pInfo));
		g_ChangeHooks.emplace_back(std::move(sHook));
	}
	return true;
}

bool SendProxyManager::AddChangeHookToListGamerules(PropChangeHookGamerules &&sHook, CallBackInfo &&pInfo)
{
	PropChangeHookGamerules *pHookInfo = nullptr;
	for (auto &it : g_ChangeHooksGamerules)
	{
		if (it.pVar == sHook.pVar)
		{
			pHookInfo = &it;
			break;
		}
	}
	if (pHookInfo)
	{
		//just validate it
		switch (sHook.propType)
		{
		case PropType::Prop_Int:
		case PropType::Prop_Float:
		case PropType::Prop_String:
		case PropType::Prop_Vector:
			break;
		default: return false;
		}
		pHookInfo->vCallbacksInfo.emplace_back(std::move(pInfo));
	}
	else
	{
		switch (sHook.propType)
		{
		case PropType::Prop_Int: sHook.iLastValue = *(int *)((unsigned char *)g_pGameRules + sHook.Offset); break;
		case PropType::Prop_Float: sHook.flLastValue = *(float *)((unsigned char*)g_pGameRules + sHook.Offset); break;
		case PropType::Prop_String: strncpynull(sHook.cLastValue, (const char *)((unsigned char *)g_pGameRules + sHook.Offset), sizeof(sHook.cLastValue)); break;
		case PropType::Prop_Vector: sHook.vecLastValue = *(Vector *)((unsigned char *)g_pGameRules + sHook.Offset); break;
		default: return false;
		}

		sHook.vCallbacksInfo.emplace_back(std::move(pInfo));
		g_ChangeHooksGamerules.emplace_back(std::move(sHook));
	}
	return true;
}

bool SendProxyManager::UnhookProxy(const SendPropHook &hook)
{
	if(hook.per_client) {
		if(--g_numPerClientHooks == 0) {
			DISABLE_DETOUR(CGameServer_SendClientMessages);
			DISABLE_DETOUR(CGameClient_ShouldSendMessages);
			DISABLE_DETOUR(SV_ComputeClientPacks);
			g_iCurrentClientIndexInLoop = -1;
			g_bSVComputePacksDone = true;
		}
	}
	
	//if there are other hooks for this prop, don't change the proxy, just remove it from our list
	auto it2 = g_Hooks.begin();
	while(it2 != g_Hooks.end()) {
		if (it2->pVar == hook.pVar &&
			&*it2 != &hook)
		{
			CallListenersForHook(hook);
			return true;
		}
		
		++it2;
	}
	auto it = g_vHookedEdicts.begin();
	while(it != g_vHookedEdicts.end()) {
		if(*it == hook.pEnt) {
			g_vHookedEdicts.erase(it);
			break;
		}
		
		++it;
	}
	CallListenersForHook(hook);
	hook.pVar->SetProxyFn(hook.pRealProxy);
	return false;
}

bool SendProxyManager::UnhookProxyGamerules(const SendPropHookGamerules &hook)
{
	if(hook.per_client) {
		if(--g_numPerClientHooks == 0) {
			DISABLE_DETOUR(CGameServer_SendClientMessages);
			DISABLE_DETOUR(CGameClient_ShouldSendMessages);
			DISABLE_DETOUR(SV_ComputeClientPacks);
			g_iCurrentClientIndexInLoop = -1;
			g_bSVComputePacksDone = true;
		}
	}
	
	//if there are other hooks for this prop, don't change the proxy, just remove it from our list
	auto it = g_HooksGamerules.begin();
	while(it != g_HooksGamerules.end())
	{
		if (it->pVar == hook.pVar &&
			&*it != &hook)
		{
			CallListenersForHookGamerules(hook);
			return true;
		}
		
		++it;
	}
	CallListenersForHookGamerules(hook);
	hook.pVar->SetProxyFn(hook.pRealProxy);
	return false;
}

bool SendProxyManager::UnhookChange(PropChangeHook &hook, const CallBackInfo &pInfo)
{
	auto &pCallbacks = hook.vCallbacksInfo;
	auto it2 = pCallbacks.begin();
	while(it2 != pCallbacks.end()) {
		auto &cb = *it2;
		
		if (cb.iCallbackType == pInfo.iCallbackType &&
			cb.pCallback == (void *)pInfo.pCallback)
		{
			pCallbacks.erase(it2);
			continue;
		}
		
		++it2;
	}
	//if there no any callbacks anymore, then remove all info about this hook
	if (pCallbacks.empty()) {
		return true;
	}
	
	return false;
}

bool SendProxyManager::UnhookChangeGamerules(PropChangeHookGamerules &hook, const CallBackInfo &pInfo)
{
	auto &pCallbacks = hook.vCallbacksInfo;
	auto it2 = pCallbacks.begin();
	while(it2 != pCallbacks.end()) {
		auto &cb = *it2;
		
		if (cb.iCallbackType == pInfo.iCallbackType &&
			cb.pCallback == (void *)pInfo.pCallback)
		{
			pCallbacks.erase(it2);
			continue;
		}
		
		++it2;
	}
	//if there no any callbacks anymore, then remove all info about this hook
	if (pCallbacks.empty()) {
		return true;
	}
	
	return false;
}

//callbacks

//Change

void CallChangeCallbacks(const PropChangeHook &pInfo, void * pOldValue, void * pNewValue)
{
	for (auto &sCallback : pInfo.vCallbacksInfo)
	{
		switch (sCallback.iCallbackType)
		{
			case CallBackType::Callback_CPPCallbackInterface:
			{
				edict_t * pEnt = gamehelpers->EdictOfIndex(pInfo.objectID);
				if (!pEnt)
					break; //???
				ISendProxyChangeCallbacks * pCallbacks = (ISendProxyChangeCallbacks *)sCallback.pCallback;
				pCallbacks->OnEntityPropChange(gameents->EdictToBaseEntity(pEnt), pInfo.pVar, pNewValue, pOldValue, pInfo.propType, pInfo.Element);
				break;
			}
			case CallBackType::Callback_PluginFunction:
			{
				IPluginFunction * pCallBack = (IPluginFunction *)sCallback.pCallback;
				switch (pInfo.propType)
				{
					case PropType::Prop_Int:
					{
						pCallBack->PushCell(pInfo.objectID);
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushCell(pInfo.iLastValue);
						pCallBack->PushCell(*(int *)pNewValue);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
					case PropType::Prop_Float:
					{
						pCallBack->PushCell(pInfo.objectID);
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushFloat(pInfo.flLastValue);
						pCallBack->PushFloat(*(float *)pNewValue);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
					case PropType::Prop_String:
					{
						pCallBack->PushCell(pInfo.objectID);
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushString(pInfo.cLastValue);
						pCallBack->PushString((char *)pNewValue);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
					case PropType::Prop_Vector:
					{
						cell_t vector[2][3];
						Vector * pVec = (Vector *)pNewValue;
						vector[0][0] = sp_ftoc(pVec->x);
						vector[0][1] = sp_ftoc(pVec->y);
						vector[0][2] = sp_ftoc(pVec->z);
						vector[1][0] = sp_ftoc(pInfo.vecLastValue[0]);
						vector[1][1] = sp_ftoc(pInfo.vecLastValue[1]);
						vector[1][2] = sp_ftoc(pInfo.vecLastValue[2]);
						pCallBack->PushCell(pInfo.objectID);
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushArray(vector[1], 3);
						pCallBack->PushArray(vector[0], 3);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
				}
				break;
			}
		}
	}
}

void CallChangeGamerulesCallbacks(const PropChangeHookGamerules &pInfo, void * pOldValue, void * pNewValue)
{
	for (auto &sCallback : pInfo.vCallbacksInfo)
	{
		switch (sCallback.iCallbackType)
		{
			case CallBackType::Callback_CPPCallbackInterface:
			{
				ISendProxyChangeCallbacks * pCallbacks = (ISendProxyChangeCallbacks *)sCallback.pCallback;
				pCallbacks->OnGamerulesPropChange(pInfo.pVar, pNewValue, pOldValue, pInfo.propType, pInfo.Element);
				break;
			}
			case CallBackType::Callback_PluginFunction:
			{
				IPluginFunction * pCallBack = (IPluginFunction *)sCallback.pCallback;
				switch (pInfo.propType)
				{
					case PropType::Prop_Int:
					{
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushCell(pInfo.iLastValue);
						pCallBack->PushCell(*(int *)pNewValue);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
					case PropType::Prop_Float:
					{
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushFloat(pInfo.flLastValue);
						pCallBack->PushFloat(*(float *)pNewValue);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
					case PropType::Prop_String:
					{
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushString(pInfo.cLastValue);
						pCallBack->PushString((char *)pNewValue);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
					case PropType::Prop_Vector:
					{
						cell_t vector[2][3];
						Vector * pVec = (Vector *)pNewValue;
						vector[0][0] = sp_ftoc(pVec->x);
						vector[0][1] = sp_ftoc(pVec->y);
						vector[0][2] = sp_ftoc(pVec->z);
						vector[1][0] = sp_ftoc(pInfo.vecLastValue[0]);
						vector[1][1] = sp_ftoc(pInfo.vecLastValue[1]);
						vector[1][2] = sp_ftoc(pInfo.vecLastValue[2]);
						pCallBack->PushString(pInfo.pVar->GetName());
						pCallBack->PushArray(vector[1], 3);
						pCallBack->PushArray(vector[0], 3);
						pCallBack->PushCell(pInfo.Element);
						pCallBack->Execute(0);
						break;
					}
				}
				break;
			}
		}
	}
}

//Proxy

bool CallInt(const SendPropHook &hook, int &ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;
			cell_t value = ret;
			cell_t result = Pl_Continue;
			callback->PushCell(hook.objectID);
			callback->PushString(hook.pVar->GetName());
			callback->PushCellByRef(&value);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				ret = value;
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			int iValue = ret;
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&iValue, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (void *)&iValue, hook.propType, hook.Element);
			}
			if (bChange)
			{
				ret = iValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallIntGamerules(const SendPropHookGamerules &hook, int &ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;
			cell_t value = ret;
			cell_t result = Pl_Continue;
			callback->PushString(hook.pVar->GetName());
			callback->PushCellByRef(&value);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				ret = value;
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			int iValue = ret;
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&iValue, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (void *)&iValue, hook.propType, hook.Element);
			}
			if (bChange)
			{
				ret = iValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallFloat(const SendPropHook &hook, float &ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}
	
	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;
			float value = ret;
			cell_t result = Pl_Continue;
			callback->PushCell(hook.objectID);
			callback->PushString(hook.pVar->GetName());
			callback->PushFloatByRef(&value);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				ret = value;
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			float flValue = ret;
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&flValue, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (void *)&flValue, hook.propType, hook.Element);
			}
			if (bChange)
			{
				ret = flValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallFloatGamerules(const SendPropHookGamerules &hook, float &ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;
			float value = ret;
			cell_t result = Pl_Continue;
			callback->PushString(hook.pVar->GetName());
			callback->PushFloatByRef(&value);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				ret = value;
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			float flValue = ret;
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&flValue, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (void *)&flValue, hook.propType, hook.Element);
			}
			if (bChange)
			{
				ret = flValue;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallString(const SendPropHook &hook, char *&ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	static char value[4096];
	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;
			strncpynull(value, ret, 4096);
			cell_t result = Pl_Continue;
			callback->PushCell(hook.objectID);
			callback->PushString(hook.pVar->GetName());
			callback->PushStringEx(value, 4096, SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				ret = value;
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			strncpynull(value, ret, 4096);
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)value, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnEntityPropProxyFunctionCalls(gameents->EdictToBaseEntity(hook.pEnt), hook.pVar, (void *)value, hook.propType, hook.Element);
			}
			if (bChange)
			{
				ret = value;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallStringGamerules(const SendPropHookGamerules &hook, char *&ret)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	static char value[4096];
	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			void *pGamerules = g_pSDKTools->GetGameRules();
			if(!pGamerules)
			{
				g_pSM->LogError(myself, "CRITICAL ERROR: Could not get gamerules pointer!");
			}
			
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;
			strncpynull(value, ret, 4096);
			cell_t result = Pl_Continue;
			callback->PushString(hook.pVar->GetName());
			callback->PushStringEx(value, 4096, SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				ret = value;
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			void * pGamerules = g_pSDKTools->GetGameRules();
			if(!pGamerules)
				return false;
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			strncpynull(value, ret, 4096);
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)value, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (void *)value, hook.propType, hook.Element);
			}
			if (bChange)
			{
				ret = value;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallVector(const SendPropHook &hook, Vector &vec)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;

			cell_t vector[3];
			vector[0] = sp_ftoc(vec.x);
			vector[1] = sp_ftoc(vec.y);
			vector[2] = sp_ftoc(vec.z);

			cell_t result = Pl_Continue;
			callback->PushCell(hook.objectID);
			callback->PushString(hook.pVar->GetName());
			callback->PushArray(vector, 3, SM_PARAM_COPYBACK);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				vec.x = sp_ctof(vector[0]);
				vec.y = sp_ctof(vector[1]);
				vec.z = sp_ctof(vector[2]);
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			Vector vNewVec(vec.x, vec.y, vec.z);
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&vNewVec, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (void *)&vNewVec, hook.propType, hook.Element);
			}
			if (bChange)
			{
				vec.x = vNewVec.x;
				vec.y = vNewVec.y;
				vec.z = vNewVec.z;
				return true;
			}
			break;
		}
	}
	return false;
}

bool CallVectorGamerules(const SendPropHookGamerules &hook, Vector &vec)
{
	if (!g_bSVComputePacksDone)
		return false;
	
	AUTO_LOCK_FM(g_WorkMutex);
	
	if(hook.per_client && g_iCurrentClientIndexInLoop == -1) {
		return false;
	}

	switch (hook.sCallbackInfo.iCallbackType)
	{
		case CallBackType::Callback_PluginFunction:
		{
			IPluginFunction *callback = (IPluginFunction *)hook.sCallbackInfo.pCallback;

			cell_t vector[3];
			vector[0] = sp_ftoc(vec.x);
			vector[1] = sp_ftoc(vec.y);
			vector[2] = sp_ftoc(vec.z);

			cell_t result = Pl_Continue;
			callback->PushString(hook.pVar->GetName());
			callback->PushArray(vector, 3, SM_PARAM_COPYBACK);
			callback->PushCell(hook.Element);
			if(hook.per_client) {
				callback->PushCell(g_iCurrentClientIndexInLoop + 1);
			}
			callback->Execute(&result);
			if (result == Pl_Changed)
			{
				vec.x = sp_ctof(vector[0]);
				vec.y = sp_ctof(vector[1]);
				vec.z = sp_ctof(vector[2]);
				return true;
			}
			break;
		}
		case CallBackType::Callback_CPPCallbackInterface:
		{
			ISendProxyCallbacks * pCallbacks = (ISendProxyCallbacks *)hook.sCallbackInfo.pCallback;
			Vector vNewVec(vec.x, vec.y, vec.z);
			bool bChange = false;
			if(hook.per_client) {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (CBasePlayer *)gamehelpers->ReferenceToEntity(g_iCurrentClientIndexInLoop + 1), (void *)&vNewVec, hook.propType, hook.Element);
			} else {
				bChange = pCallbacks->OnGamerulesPropProxyFunctionCalls(hook.pVar, (void *)&vNewVec, hook.propType, hook.Element);
			}
			if (bChange)
			{
				vec.x = vNewVec.x;
				vec.y = vNewVec.y;
				vec.z = vNewVec.z;
				return true;
			}
			break;
		}
	}
	return false;
}

void GlobalProxy(const SendProp *pProp, const void *pStructBase, const void * pData, DVariant *pOut, int iElement, int objectID)
{
	edict_t * pEnt = gamehelpers->EdictOfIndex(objectID);
	bool bHandled = false;
	for (auto &it : g_Hooks)
	{
		if (it.objectID == objectID &&
			it.pVar == pProp &&
			pEnt == it.pEnt)
		{
			switch (it.propType)
			{
				case PropType::Prop_Int:
				{
					int result = *(int *)pData;

					if (CallInt(it, result))
					{
						long data = result;
						it.pRealProxy(pProp, pStructBase, &data, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				case PropType::Prop_Float:
				{
					float result = *(float *)pData;

					if (CallFloat(it, result))
					{
						it.pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				case PropType::Prop_String:
				{
					char * result = *(char **)pData;

					if (CallString(it, result))
					{
						it.pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				case PropType::Prop_Vector:
				{
					Vector result = *(Vector *)pData;

					if (CallVector(it, result))
					{
						it.pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				default: rootconsole->ConsolePrint("%s: SendProxy report: Unknown prop type (%s).", __func__, it.pVar->GetName());
			}
		}
	}
	if (!bHandled)
	{
		//perhaps we aren't hooked, but we can still find the real proxy for this prop
		for (auto &it : g_Hooks)
		{
			if (it.pVar == pProp)
			{
				it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
				return;
			}
		}
		g_pSM->LogError(myself, "CRITICAL: Proxy for unmanaged entity %d called for prop %s", objectID, pProp->GetName());
	}
}

void GlobalProxyGamerules(const SendProp *pProp, const void *pStructBase, const void * pData, DVariant *pOut, int iElement, int objectID)
{
	if (!g_bShouldChangeGameRulesState)
		g_bShouldChangeGameRulesState = true; //If this called once, so, the props wants to be sent at this time, and we should do this for all clients!
	bool bHandled = false;
	for (auto &it : g_HooksGamerules)
	{
		if (it.pVar == pProp)
		{
			switch (it.propType)
			{
				case PropType::Prop_Int:
				{
					int result = *(int *)pData;

					if (CallIntGamerules(it, result))
					{
						long data = result;
						it.pRealProxy(pProp, pStructBase, &data, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				case PropType::Prop_Float:
				{
					float result = *(float *)pData;

					if (CallFloatGamerules(it, result))
					{
						it.pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					} 
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				case PropType::Prop_String:
				{
					char * result = *(char **)pData; //We need to use const because of C++11 restriction

					if (CallStringGamerules(it, result))
					{
						it.pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				case PropType::Prop_Vector:
				{
					Vector result = *(Vector *)pData;

					if (CallVectorGamerules(it, result))
					{
						it.pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return; // If somebody already handled this call, do not call other hooks for this entity & prop
					}
					else
					{
						it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
					}
					bHandled = true;
					continue;
				}
				default: rootconsole->ConsolePrint("%s: SendProxy report: Unknown prop type (%s).", __func__, it.pVar->GetName());
			}
		}
	}
	if (!bHandled)
	{
		//perhaps we aren't hooked, but we can still find the real proxy for this prop
		for (auto &it : g_HooksGamerules)
		{
			if (it.pVar == pProp)
			{
				it.pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
				return;
			}
		}
		g_pSM->LogError(myself, "CRITICAL: Proxy for unmanaged gamerules called for prop %s", pProp->GetName());
	}
}

//help

CBaseEntity * FindEntityByServerClassname(int iStart, const char * pServerClassName)
{
	if (iStart >= g_iEdictCount)
		return nullptr;
	for (int i = iStart; i < g_iEdictCount; i++)
	{
		CBaseEntity * pEnt = gamehelpers->ReferenceToEntity(i);
		if (!pEnt)
			continue;
		IServerNetworkable * pNetworkable = ((IServerUnknown *)pEnt)->GetNetworkable();
		if (!pNetworkable)
			continue;
		const char * pName = pNetworkable->GetServerClass()->GetName();
		if (pName && !strcmp(pName, pServerClassName))
			return pEnt;
	}
	return nullptr;
}

bool IsPropValid(SendProp * pProp, PropType iType)
{
	switch (iType)
	{
		case PropType::Prop_Int: 
			if (pProp->GetType() != DPT_Int)
				return false;
			return true;
		case PropType::Prop_Float:
		{
			if (pProp->GetType() != DPT_Float)
				return false;
			return true;
		}
		case PropType::Prop_String:
		{
			if (pProp->GetType() != DPT_String)
				return false;
			return true;
		}
		case PropType::Prop_Vector:
		{
			if (pProp->GetType() != DPT_Vector)
				return false;
			return true;
		}
	}
	return false;
}

char * strncpynull(char * pDestination, const char * pSource, size_t szCount)
{
	if(!pSource) {
		pSource = "";
	}
	
	strncpy(pDestination, pSource, szCount);
	pDestination[szCount - 1] = 0;
	return pDestination;
}
