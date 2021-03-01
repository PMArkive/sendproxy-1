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

#ifndef SENDPROXY_IFACE_IMPL_INC
#define SENDPROXY_IFACE_IMPL_INC

#include "extension.h"
#include "ISendProxy.h"

void CallListenersForHookID(int iID);
void CallListenersForHookIDGamerules(int iID);

class SendProxyManagerInterfaceImpl : public ISendProxyManager
{
public: //SMInterface
	virtual const char * GetInterfaceName() override;
	virtual unsigned int GetInterfaceVersion() override;
public: //interface impl:
	virtual bool HookProxy(IExtension *, SendProp *, CBaseEntity *, PropType, CallBackType, void *) override;
	virtual bool HookProxy(IExtension *, const char *, CBaseEntity *, PropType, CallBackType, void *) override;
	virtual bool HookProxyGamerules(IExtension *, SendProp *, PropType, CallBackType, void *) override;
	virtual bool HookProxyGamerules(IExtension *, const char *, PropType, CallBackType, void *) override;
	virtual bool UnhookProxy(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *) override;
	virtual bool UnhookProxy(IExtension *, const char *, CBaseEntity *, CallBackType, void *) override;
	virtual bool UnhookProxyGamerules(IExtension *, SendProp *, CallBackType, void *) override;
	virtual bool UnhookProxyGamerules(IExtension *, const char *, CallBackType, void *) override;
	virtual bool AddUnhookListener(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool AddUnhookListener(IExtension *, const char *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool AddUnhookListenerGamerules(IExtension *, SendProp *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool AddUnhookListenerGamerules(IExtension *, const char *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListener(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListener(IExtension *, const char *, CBaseEntity *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListenerGamerules(IExtension *, SendProp *, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListenerGamerules(IExtension *, const char *, CallBackType, void *, ISendProxyUnhookListener *) override;
	//same for the arrays =|
	virtual bool HookProxyArray(IExtension *, SendProp *, CBaseEntity *, PropType, int, CallBackType, void *) override;
	virtual bool HookProxyArray(IExtension *, const char *, CBaseEntity *, PropType, int, CallBackType, void *) override;
	virtual bool UnhookProxyArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *) override;
	virtual bool UnhookProxyArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *) override;
	virtual bool HookProxyArrayGamerules(IExtension *, SendProp *, PropType, int, CallBackType, void *) override;
	virtual bool HookProxyArrayGamerules(IExtension *, const char *, PropType, int, CallBackType, void *) override;
	virtual bool UnhookProxyArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *) override;
	virtual bool UnhookProxyArrayGamerules(IExtension *, const char *, int, CallBackType, void *) override;
	virtual bool AddUnhookListenerArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool AddUnhookListenerArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool AddUnhookListenerArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool AddUnhookListenerArrayGamerules(IExtension *, const char *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListenerArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListenerArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListenerArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	virtual bool RemoveUnhookListenerArrayGamerules(IExtension *, const char *, int, CallBackType, void *, ISendProxyUnhookListener *) override;
	//checkers
	virtual bool IsProxyHooked(IExtension *, SendProp *, CBaseEntity *) const override;
	virtual bool IsProxyHooked(IExtension *, const char *, CBaseEntity *) const override;
	virtual bool IsProxyHookedGamerules(IExtension *, SendProp *) const override;
	virtual bool IsProxyHookedGamerules(IExtension *, const char *) const override;
	virtual bool IsProxyHookedArray(IExtension *, SendProp *, CBaseEntity *, int) const override;
	virtual bool IsProxyHookedArray(IExtension *, const char *, CBaseEntity *, int) const override;
	virtual bool IsProxyHookedArrayGamerules(IExtension *, SendProp *, int) const override;
	virtual bool IsProxyHookedArrayGamerules(IExtension *, const char *, int) const override;

	/*
	TODO:
	//For the change hooks
	virtual bool HookChange(IExtension *, SendProp *, CBaseEntity *, PropType, CallBackType, void *) override;
	virtual bool HookChange(IExtension *, const char *, CBaseEntity *, PropType, CallBackType, void *) override;
	virtual bool HookChangeGamerules(IExtension *, SendProp *, PropType, CallBackType, void *) override;
	virtual bool HookChangeGamerules(IExtension *, const char *, PropType, CallBackType, void *) override;
	virtual bool UnhookChange(IExtension *, SendProp *, CBaseEntity *, CallBackType, void *) override;
	virtual bool UnhookChange(IExtension *, const char *, CBaseEntity *, CallBackType, void *) override;
	virtual bool UnhookChangeGamerules(IExtension *, SendProp *, CallBackType, void *) override;
	virtual bool UnhookChangeGamerules(IExtension *, const char *, CallBackType, void *) override;
	//same for the arrays =|
	virtual bool HookChangeArray(IExtension *, SendProp *, CBaseEntity *, PropType, int, CallBackType, void *) override;
	virtual bool HookChangeArray(IExtension *, const char *, CBaseEntity *, PropType, int, CallBackType, void *) override;
	virtual bool UnhookChangeArray(IExtension *, SendProp *, CBaseEntity *, int, CallBackType, void *) override;
	virtual bool UnhookChangeArray(IExtension *, const char *, CBaseEntity *, int, CallBackType, void *) override;
	virtual bool HookChangeArrayGamerules(IExtension *, SendProp *, PropType, int, CallBackType, void *) override;
	virtual bool HookChangeArrayGamerules(IExtension *, const char *, PropType, int, CallBackType, void *) override;
	virtual bool UnhookChangeArrayGamerules(IExtension *, SendProp *, int, CallBackType, void *) override;
	virtual bool UnhookChangeArrayGamerules(IExtension *, const char *, int, CallBackType, void *) override;
	//checkers
	virtual bool IsChangeHooked(IExtension *, SendProp *, CBaseEntity *) override;
	virtual bool IsChangeHooked(IExtension *, const char *, CBaseEntity *) override;
	virtual bool IsChangeHookedGamerules(IExtension *, SendProp *) override;
	virtual bool IsChangeHookedGamerules(IExtension *, const char *) override;
	virtual bool IsChangeHookedArray(IExtension *, SendProp *, CBaseEntity *, int) override;
	virtual bool IsChangeHookedArray(IExtension *, const char *, CBaseEntity *, int) override;
	virtual bool IsChangeHookedArrayGamerules(IExtension *, SendProp *, int) override;
	virtual bool IsChangeHookedArrayGamerules(IExtension *, const char *, int) override;
	//More TODO: Add unhook listeners for change hooks
	*/
};

#endif