/*************************************************************************\
* Copyright (c) 2016 Helmholtz-Zentrum Berlin
*     fuer Materialien und Energie GmbH (HZB), Berlin, Germany.
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU Lesser General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
\*************************************************************************/
#ifndef DEVUASUBSCRIPTION_H
#define DEVUASUBSCRIPTION_H

#include "drvOpcUa.h"
#include "devUaClient.h"
#include <uasubscription.h>

class DevUaSubscription :public UaClientSdk::UaSubscriptionCallback
{
    UA_DISABLE_COPY(DevUaSubscription);
public:
    DevUaSubscription(int debug);
    virtual ~DevUaSubscription();

    virtual void subscriptionStatusChanged(
        OpcUa_UInt32      clientSubscriptionHandle,
        const UaStatus&   status);
    virtual void dataChange(
        OpcUa_UInt32               clientSubscriptionHandle,
        const UaDataNotifications& dataNotifications,
        const UaDiagnosticInfos&   diagnosticInfos);
    virtual void newEvents(
        OpcUa_UInt32                clientSubscriptionHandle,
        UaEventFieldLists&          eventFieldList);

    UaStatus createSubscription(UaClientSdk::UaSession *pSession);
    UaStatus deleteSubscription();
    UaStatus createMonitoredItems(std::vector<UaNodeId> &vUaNodeId,std::vector<OPCUA_ItemINFO *> *m_vectorUaItemInfo);

    int debug;              // debug output independant from single channels
private:
    UaClientSdk::UaSession*                  m_pSession;
    UaClientSdk::UaSubscription*             m_pSubscription;
    std::vector<OPCUA_ItemINFO *> *m_vectorUaItemInfo;
};
#endif // DEVUASUBSCRIPTION_H
