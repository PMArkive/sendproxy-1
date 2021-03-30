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

#ifndef _EXTENSION_H_INC_
#define _EXTENSION_H_INC_

 /*
	TODO:
		Implement interface:
			Add common function for prop change hooks
			Add remove listeners for prop change hooks
 */

#include "smsdk_ext.h"
#include <string>
#include <stdint.h>
#include "convar.h"
#include "dt_send.h"
#include "server_class.h"
#include "ISendProxy.h"
#include <eiface.h>
#include <ISDKHooks.h>
#include <ISDKTools.h>

#define GET_CONVAR(name) \
	name = g_pCVar->FindVar(#name); \
	if (name == nullptr) { \
		if (error != nullptr && maxlen != 0) { \
			ismm->Format(error, maxlen, "Could not find ConVar: " #name); \
		} \
		return false; \
	}

class IServerGameEnts;

void GlobalProxy(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID);
void GlobalProxyGamerules(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID);
bool IsPropValid(SendProp *, PropType);
char * strncpynull(char * pDestination, const char * pSource, size_t szCount);

struct ListenerCallbackInfo
{
	IExtension *							m_pExt;
	IExtensionInterface *					m_pExtAPI;
	ISendProxyUnhookListener *				m_pCallBack;
};

struct CallBackInfo
{
	CallBackInfo() = default;
	CallBackInfo(const CallBackInfo & rObj) = default;
	CallBackInfo(CallBackInfo && rObj) = default;
	CallBackInfo & operator=(const CallBackInfo & rObj) = default;
	CallBackInfo & operator=(CallBackInfo && rObj) = default;
	~CallBackInfo() = default;
	void *									pOwner; //Pointer to plugin context or IExtension *
	void *									pCallback;
	CallBackType							iCallbackType;
};

struct SendPropHook
{
	SendPropHook() = default;
	SendPropHook(const SendPropHook & rObj) = default;
	SendPropHook(SendPropHook && rObj) = default;
	SendPropHook &operator=(SendPropHook &&) = default;
	SendPropHook &operator=(const SendPropHook &) = default;
	~SendPropHook() = default;
	CallBackInfo							sCallbackInfo;
	SendProp *								pVar;
	edict_t *								pEnt;
	SendVarProxyFn							pRealProxy;
	int										objectID;
	PropType								propType;
	int										Offset;
	int										Element{0};
	std::vector<ListenerCallbackInfo>		vListeners;
	bool per_client = false;
};

struct SendPropHookGamerules
{
	SendPropHookGamerules() = default;
	SendPropHookGamerules(const SendPropHookGamerules & rObj) = default;
	SendPropHookGamerules(SendPropHookGamerules && rObj) = default;
	SendPropHookGamerules &operator=(SendPropHookGamerules &&) = default;
	SendPropHookGamerules &operator=(const SendPropHookGamerules &) = default;
	~SendPropHookGamerules() = default;
	CallBackInfo							sCallbackInfo;
	SendProp *								pVar;
	SendVarProxyFn							pRealProxy;
	PropType								propType;
	int										Element{0};
	std::vector<ListenerCallbackInfo>		vListeners;
	bool per_client = false;
};

struct dumbvec_t
{
	dumbvec_t() = default;
	dumbvec_t(const dumbvec_t &) = default;
	dumbvec_t(dumbvec_t &&) = default;
	dumbvec_t &operator=(const dumbvec_t &) = default;
	dumbvec_t &operator=(dumbvec_t &&) = default;
	
	float &operator[](int i) { return value[i]; }
	const float &operator[](int i) const { return value[i]; }
	dumbvec_t &operator=(const Vector &vec) {
		value[0] = vec.x;
		value[1] = vec.y;
		value[2] = vec.z;
		return *this;
	}
	
	bool operator==(const Vector &vec) {
		return value[0] == vec.x &&
				value[1] == vec.y &&
				value[2] == vec.z;
	}
	
	bool operator!=(const Vector &vec) {
		return value[0] != vec.x &&
				value[1] != vec.y &&
				value[2] != vec.z;
	}
	
	float value[3];
};

struct PropChangeHook
{
	PropChangeHook() = default;
	PropChangeHook(const PropChangeHook & rObj) = default;
	PropChangeHook(PropChangeHook && rObj) = default;
	PropChangeHook &operator=(PropChangeHook &&) = default;
	PropChangeHook &operator=(const PropChangeHook &) = default;
	~PropChangeHook() = default;
	union //unfortunately we MUST use union instead of std::variant cuz we should prevent libstdc++ linking in linux =|
	{
		int									iLastValue{0};
		float								flLastValue;
		dumbvec_t							vecLastValue;
		char								cLastValue[4096];
	};
	SendProp *								pVar;
	PropType								propType;
	unsigned int							Offset;
	int										objectID;
	int										Element{0};
	std::vector<CallBackInfo>				vCallbacksInfo;
};

struct PropChangeHookGamerules
{
	PropChangeHookGamerules() = default;
	PropChangeHookGamerules(const PropChangeHookGamerules & rObj) = default;
	PropChangeHookGamerules(PropChangeHookGamerules && rObj) = default;
	PropChangeHookGamerules &operator=(PropChangeHookGamerules &&) = default;
	PropChangeHookGamerules &operator=(const PropChangeHookGamerules &) = default;
	~PropChangeHookGamerules() = default;
	union //unfortunately we MUST use union instead of std::variant cuz we should prevent libstdc++ linking in linux =|
	{
		int									iLastValue{0};
		float								flLastValue;
		dumbvec_t							vecLastValue;
		char								cLastValue[4096];
	};
	SendProp *								pVar;
	PropType								propType;
	unsigned int							Offset;
	int										Element{0};
	std::vector<CallBackInfo>				vCallbacksInfo;
};

extern std::vector<SendPropHook> g_Hooks;
extern std::vector<SendPropHookGamerules> g_HooksGamerules;
extern std::vector<PropChangeHook> g_ChangeHooks;
extern std::vector<PropChangeHookGamerules> g_ChangeHooksGamerules;
 
class SendProxyManager :
	public SDKExtension,
	public IPluginsListener,
	public ISMEntityListener
{
public: //sm
	virtual bool SDK_OnLoad(char * error, size_t maxlength, bool late);
	virtual void SDK_OnUnload();
	virtual void SDK_OnAllLoaded();
	
	virtual void OnCoreMapEnd();
	virtual void OnCoreMapStart(edict_t *, int, int);

public: //other
	virtual void OnPluginUnloaded(IPlugin * plugin);
	//returns true upon success
	bool AddHookToList(SendPropHook &&hook);
	bool AddHookToListGamerules(SendPropHookGamerules &&hook);

	//returns false if prop type not supported or entity is invalid
	bool AddChangeHookToList(PropChangeHook &&sHook, CallBackInfo &&pInfo);
	bool AddChangeHookToListGamerules(PropChangeHookGamerules &&sHook, CallBackInfo &&pInfo);

	bool UnhookProxy(const SendPropHook &hook);
	bool UnhookProxyGamerules(const SendPropHookGamerules &hook);

	bool UnhookChange(PropChangeHook &hook, const CallBackInfo &pInfo);
	bool UnhookChangeGamerules(PropChangeHookGamerules &hook, const CallBackInfo &pInfo);
	virtual int GetClientCount() const;
public: // ISMEntityListener
	virtual void OnEntityDestroyed(CBaseEntity * pEntity);
public:
#if defined SMEXT_CONF_METAMOD
	virtual bool SDK_OnMetamodLoad(ISmmAPI * ismm, char * error, size_t maxlen, bool late);
	//virtual bool SDK_OnMetamodUnload(char *error, size_t maxlength);
	//virtual bool SDK_OnMetamodPauseChange(bool paused, char *error, size_t maxlength);
#endif
};

extern SendProxyManager g_SendProxyManager;
extern IServerGameEnts * gameents;
extern const char * g_szGameRulesProxy;
constexpr int g_iEdictCount = 2048; //default value, we do not need to get it manually cuz it is constant
extern ISDKTools * g_pSDKTools;
extern void * g_pGameRules;

#endif // _EXTENSION_H_INC_
