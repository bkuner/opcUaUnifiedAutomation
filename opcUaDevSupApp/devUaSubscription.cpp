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

#include "drvOpcUa.h"
#include "devUaClient.h"
#include "devUaSubscription.h"
#include <epicsExport.h>

using namespace UaClientSdk;

// Configurable default for publishing interval
static double drvOpcua_DefaultPublishInterval = 100.0;  // ms

extern "C" {
    epicsExportAddress(double, drvOpcua_DefaultPublishInterval);
}

DevUaSubscription::DevUaSubscription(int debug=0)
    : debug(debug)
{}

DevUaSubscription::~DevUaSubscription()
{
    deleteSubscription();
}

void DevUaSubscription::subscriptionStatusChanged(
    OpcUa_UInt32      clientSubscriptionHandle,
    const UaStatus&   status)
{
    OpcUa_ReferenceParameter(clientSubscriptionHandle); // We use the callback only for this subscription
    errlogPrintf("DevUaSubscription: subscription no longer valid - failed with status %d (%s)\n",
                 status.statusCode(),
                 status.toString().toUtf8());
}

void DevUaSubscription::dataChange(
    OpcUa_UInt32               clientSubscriptionHandle,
    const UaDataNotifications& dataNotifications,
    const UaDiagnosticInfos&   diagnosticInfos)
{
    OpcUa_ReferenceParameter(clientSubscriptionHandle); // We use the callback only for this subscription
    OpcUa_ReferenceParameter(diagnosticInfos);
    OpcUa_UInt32 i = 0;
    char timeBuf[30];
    getTime(timeBuf);
    if(debug>2) errlogPrintf("dataChange %s\n",timeBuf);
    for ( i=0; i<dataNotifications.length(); i++ )
    {
        struct dataChangeError {};
        OPCUA_ItemINFO* uaItem = m_vectorUaItemInfo->at(dataNotifications[i].ClientHandle);
        if(debug>3)
            errlogPrintf("\t%s\n",uaItem->prec->name);
        else if(uaItem->debug >= 2)
            errlogPrintf("dataChange: %s %s\n",timeBuf,uaItem->prec->name);
        epicsMutexLock(uaItem->flagLock);
        try {
            if (OpcUa_IsBad(dataNotifications[i].Value.StatusCode) )
            {
                if(debug) errlogPrintf("%s %s dataChange FAILED with status %s, Handle=%d\n",timeBuf,uaItem->prec->name,
                       UaStatus(dataNotifications[i].Value.StatusCode).toString().toUtf8(),dataNotifications[i].ClientHandle);
                throw dataChangeError();
            }
            uaItem->stat = 0;

            uaItem->varVal = dataNotifications[i].Value.Value;

            if(uaItem->inpDataType) { // is OUT Record
                if(uaItem->debug >= 2) errlogPrintf("dataChange %s\tOUT rec flagSuppressWrite:%d\n", uaItem->prec->name,uaItem->flagSuppressWrite);
                if(uaItem->flagSuppressWrite==0) {     // Means: dataChange by external value change. Set Record! Invoke processing by callback but suppress another write operation
                    uaItem->flagSuppressWrite = 1;
                    callbackRequest(&(uaItem->callback)); // out-records are SCAN="passive" so scanIoRequest doesn't work
                }
                else {  // Means dataChange after write operation of the record. Ignore this, no callback, suppress another processing of the record
                    uaItem->flagSuppressWrite=0;
                }
            }
            else { // is IN Record
                if(uaItem->prec->scan == SCAN_IO_EVENT)
                {
                    scanIoRequest( uaItem->ioscanpvt );    // Update the record immediatly, for scan>SCAN_IO_EVENT update by periodic scan.
                }

            }
        }
        catch(dataChangeError) {
            uaItem->stat = 1;
        }
        // I'm not shure about the posibility of another exception but of the damage it could do!
        catch(...) {
            uaItem->stat = 1;
            if(debug || (uaItem->debug>= 2)) errlogPrintf("%s %s\tdataChange: unexpected exception '%s'\n",timeBuf,uaItem->prec->name,epicsTypeNames[uaItem->recDataType]);
            uaItem->debug = 4;
        }

        // set Timestamp if specified by TSE field
        UaDateTime dt = UaDateTime(dataNotifications[i].Value.ServerTimestamp);
        if(uaItem->prec->tse == epicsTimeEventDeviceTime ) {
            uaItem->prec->time.secPastEpoch = dt.toTime_t() - POSIX_TIME_AT_EPICS_EPOCH;
            uaItem->prec->time.nsec         = dt.msec()*1000000L; // msec is 100ns steps
        }
        if(uaItem->debug >= 4) {
            errlogPrintf("server timestamp: %s, TSE:%d\n",dt.toString().toUtf8(),uaItem->prec->tse);
        }
        epicsMutexUnlock(uaItem->flagLock);


        if(uaItem->debug >= 4)
            errlogPrintf("\tepicsType: %2d,%s opcType%2d:%s flagSuppressWrite:%d\n",
                    uaItem->recDataType,epicsTypeNames[uaItem->recDataType],
                    uaItem->itemDataType,variantTypeStrings(uaItem->itemDataType),
                    uaItem->flagSuppressWrite);
    } //end for
    return;
}

void DevUaSubscription::newEvents(
    OpcUa_UInt32                clientSubscriptionHandle,
    UaEventFieldLists&          eventFieldList)
{
    OpcUa_ReferenceParameter(clientSubscriptionHandle);
    OpcUa_ReferenceParameter(eventFieldList);
    if(debug) errlogPrintf("DevUaSubscription::newEvents called\n");
}

UaStatus DevUaSubscription::createSubscription(UaSession *pSession)
{
    m_pSession = pSession;

    UaStatus result;
    ServiceSettings serviceSettings;
    SubscriptionSettings subscriptionSettings;
    subscriptionSettings.publishingInterval = drvOpcua_DefaultPublishInterval;
    if(debug) errlogPrintf("Creating subscription\n");
    result = pSession->createSubscription(
        serviceSettings,
        this,
        1,
        subscriptionSettings,
        OpcUa_True,
        &m_pSubscription);
    if (result.isBad())
    {
        errlogPrintf("DevUaSubscription::createSubscription failed with status %#8x (%s)\n",
                     result.statusCode(),
                     result.toString().toUtf8());
    }
    return result;
}

UaStatus DevUaSubscription::deleteSubscription()
{
    UaStatus result;
    ServiceSettings serviceSettings;
    // let the SDK cleanup the resources for the existing subscription
    if(debug) errlogPrintf("Deleting subscription\n");
    result = m_pSession->deleteSubscription(
        serviceSettings,
        &m_pSubscription);
    if (result.isBad())
    {
        errlogPrintf("DevUaSubscription::deleteSubscription failed with status %#8x (%s)\n",
                     result.statusCode(),
                     result.toString().toUtf8());
    }
    //TODO: setting the pointer NULL if delete failed might be a memory leak?
    return result;
}

UaStatus DevUaSubscription::createMonitoredItems(std::vector<UaNodeId> &vUaNodeId,std::vector<OPCUA_ItemINFO *> *uaItemInfo)
{
    if(debug) errlogPrintf("DevUaSubscription::createMonitoredItems\n");
    if( uaItemInfo->size() == vUaNodeId.size())
        m_vectorUaItemInfo = uaItemInfo;
    else
    {
        errlogPrintf("\nDevUaSubscription::createMonitoredItems Error: Nr of uaItems %i != nr of browsepathItems %i\n",(int)uaItemInfo->size(),(int)vUaNodeId.size());
        return OpcUa_BadInvalidState;
    }
    if(false == m_pSession->isConnected() ) {
        errlogPrintf("\nDevUaSubscription::createMonitoredItems Error: session not connected\n");
        return OpcUa_BadInvalidState;

    }

    UaStatus result;
    OpcUa_UInt32 i;
    ServiceSettings serviceSettings;
    UaMonitoredItemCreateRequests itemsToCreate;
    UaMonitoredItemCreateResults createResults;
    OPCUA_ItemINFO *info;
    // Configure one item to add to subscription
    // We monitor the value of the ServerStatus -> CurrentTime
    itemsToCreate.create(vUaNodeId.size());
    for(i=0; i<vUaNodeId.size(); i++) {
        info = uaItemInfo->at(i);
        if ( !vUaNodeId[i].isNull() ) {
            UaNodeId tempNode(vUaNodeId[i]);
            itemsToCreate[i].ItemToMonitor.AttributeId = OpcUa_Attributes_Value;
            tempNode.copyTo(&(itemsToCreate[i].ItemToMonitor.NodeId));
            itemsToCreate[i].RequestedParameters.ClientHandle = i;
            itemsToCreate[i].RequestedParameters.SamplingInterval = info->samplingInterval;
            itemsToCreate[i].RequestedParameters.QueueSize = info->queueSize;
            itemsToCreate[i].RequestedParameters.DiscardOldest = (info->discardOldest ? OpcUa_True : OpcUa_False);
            itemsToCreate[i].MonitoringMode = OpcUa_MonitoringMode_Reporting;
        }
        else {
            errlogPrintf("%s Skip illegal node: %s\n",info->prec->name,info->ItemPath);
        }
    }
    if(debug) errlogPrintf("\nAdd monitored items to subscription ...\n");
    result = m_pSubscription->createMonitoredItems(
        serviceSettings,
        OpcUa_TimestampsToReturn_Both,
        itemsToCreate,
        createResults);
    if (result.isGood())
    {
        // check individual results
        for (i = 0; i < createResults.length(); i++)
        {
            if (OpcUa_IsGood(createResults[i].StatusCode))
            {
                if(debug>1) errlogPrintf("%4d: %s\n",i,
                    UaNodeId(itemsToCreate[i].ItemToMonitor.NodeId).toXmlString().toUtf8());
            }
            else
            {
                if(debug) {
                    OPCUA_ItemINFO* uaItem = m_vectorUaItemInfo->at(i);
                    errlogPrintf("%4d %s DevUaSubscription::createMonitoredItems failed for node: %s - Status %s\n",
                        i, uaItem->prec->name,
                        UaNodeId(itemsToCreate[i].ItemToMonitor.NodeId).toXmlString().toUtf8(),
                        UaStatus(createResults[i].StatusCode).toString().toUtf8());
                }
            }
        }
    }
    else
    {
       if(debug)  errlogPrintf("DevUaSubscription::createMonitoredItems service call failed with status %s\n", result.toString().toUtf8());
    }
    return result;
}
