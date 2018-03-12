#ifndef DEVUACLIENT_H
#define DEVUACLIENT_H

#include "drvOpcUa.h"
#include "devUaSubscription.h"
#include <string>
class autoSessionConnect;

class DevUaClient : public UaClientSdk::UaSessionCallback
{
    UA_DISABLE_COPY(DevUaClient);
public:
    DevUaClient(int autocon,int debug,double opcua_AutoConnectInterval=10);
    virtual ~DevUaClient();

    // UaSessionCallback implementation ----------------------------------------------------
    virtual void connectionStatusChanged(OpcUa_UInt32 clientConnectionId, UaClientSdk::UaClient::ServerStatus serverStatus);
    // UaSessionCallback implementation ------------------------------------------------------

    UaString applicationCertificate;
    UaString applicationPrivateKey;
    UaString hostName;
    UaString url;
    UaStatus connect();
    UaStatus disconnect();
    UaStatus subscribe();
    UaStatus unsubscribe();
    void setBadQuality();

    void addOPCUA_Item(OPCUA_ItemINFO *h);
    long getNodes();
    long getBrowsePathItem(OpcUa_BrowsePath &browsePaths,std::string &ItemPath,const char nameSpaceDelim,const char pathDelimiter);
    UaStatus createMonitoredItems();

    UaStatus readFunc(UaDataValues &values,UaClientSdk::ServiceSettings &serviceSettings,UaDiagnosticInfos &diagnosticInfos,int Attribute);
    UaStatus writeFunc(OPCUA_ItemINFO *uaItem, UaVariant &tempValue);

    void writeComplete(OpcUa_UInt32 transactionId,const UaStatus&result,const UaStatusCodeArray& results,const UaDiagnosticInfos& diagnosticInfos);

    void itemStat(int v);
    void setDebug(int debug);
    int  getDebug();

    /* To allow record access within the callback function need same index of node-id and itemInfo */
    std::vector<UaNodeId>         vUaNodeId;    // array of node ids as to be used within the opcua library
    std::vector<OPCUA_ItemINFO *> vUaItemInfo;  // array of record data including the link with the node description

    double drvOpcua_AutoConnectInterval;       // Configurable default for auto connection attempt interval

private:
    int debug;
    int autoConnect;
    UaClientSdk::UaSession* m_pSession;
    DevUaSubscription* m_pDevUaSubscription;
    UaClientSdk::UaClient::ServerStatus serverConnectionStatus;
    bool initialSubscriptionOver;
    autoSessionConnect *autoConnector;
    epicsTimerQueueActive &queue;
};

// Timer to retry connecting the session when the server is down at IOC startup
class autoSessionConnect : public epicsTimerNotify {
public:
    autoSessionConnect(DevUaClient *client, const double delay, epicsTimerQueueActive &queue)
        : timer(queue.createTimer())
        , client(client)
        , delay(delay)
    {}
    virtual ~autoSessionConnect() { timer.destroy(); }
    void start() { timer.start(*this, delay); }
    virtual expireStatus expire(const epicsTime &/*currentTime*/) {
        UaStatus result = client->connect();
        if (result.isBad()) {
            return expireStatus(restart, delay);
        } else {
            return expireStatus(noRestart);
        }
    }
private:
    epicsTimer &timer;
    DevUaClient *client;
    const double delay;
};
#endif // DEVUACLIENT_H
