#ifndef UASESSION_H
#define UASESSION_H

#include "drvOpcUa.h"
#include "devUaSubscription.h"
#include <string>
class autoSessionConnect;

class DevUaSession : public UaClientSdk::UaSessionCallback
{
    UA_DISABLE_COPY(DevUaSession);
public:
    DevUaSession(std::string &tag,int autocon,int debug,double opcua_AutoConnectInterval);
    virtual ~DevUaSession();

    // UaSessionCallback implementation ----------------------------------------------------
    virtual void connectionStatusChanged(OpcUa_UInt32 clientConnectionId, UaClientSdk::UaClient::ServerStatus serverStatus);
    // UaSessionCallback implementation ------------------------------------------------------

    UaString applicationCertificate;
    UaString applicationPrivateKey;
    UaString hostName;
    UaString url;
    double drvOpcua_AutoConnectInterval;// Configurable default for auto connection attempt interval

    UaStatus connect();
    UaStatus disconnect();
//LEGACY WILL BE REMOVED
    long getNodes();                    // get nodes and update subscription/read data
    long getBrowsePathItem(OpcUa_BrowsePath &browsePaths,std::string &ItemPath,const char nameSpaceDelim,const char pathDelimiter);
//
    UaStatus subscribe();
    UaStatus unsubscribe();

    long startSession(void);            // start this session

    void setBadQuality();               // when session connection breaks
    bool isConnected() const { return m_pSession->isConnected(); }

    UaStatus readFunc(UaDataValues &values,UaClientSdk::ServiceSettings &serviceSettings,UaDiagnosticInfos &diagnosticInfos,int Attribute);

    UaStatus writeFunc(UaItem *uaItem, UaVariant &tempValue);
    void writeComplete(OpcUa_UInt32 transactionId,const UaStatus&result,const UaStatusCodeArray& results,const UaDiagnosticInfos& diagnosticInfos);

    void itemStat(int v);
    void setDebug(int debug);
    int  getDebug();
    inline const char*  getTag() {return tag.c_str();}

    long addSubscription(std::string &subscrTag);   // add subscription tag in map subscriptions
    long addSession(std::string &tag);              // add session

    static void startAllSessions(void);             // start all sessions and all subscriptions
    static void opcuaStat(int verbose);             // show status of all sessions
    static void opcuaSetDebug(int verbose);         // set debug level for all sessions
    static int opcuaClose(int debug) ;
    static DevUaSession* getSession(std::string &tag);               // get session to the session or subscription tag
    static DevUaSession *addUaItem(std::string &tag,UaItem *uaItem); // search for session or session.subscription and add uaItem to it

private:
    // The structure of sessions and subscr2session maps
    //
    // sessions {                All sessions
    //          tag1=>session1
    //          tag2=>session2
    //          ...
    //          }
    //
    // subscr2session {        just for convenience to find the accorded session for a subscription
    //          tag3=>session1
    //          tag4=>session1
    //          tag5=>session2
    //          tag6=>session2
    //          ...
    //          }
    // This structure may be defined by drvOpcuaSetup() for the sessions and drvOpcuaSubscription() for the subscriptions
    // or later by a .json- or .xml-file.
    //
    // The DevUaSession class
    //
    // - contains all its uaItems in the member vector vUaItemInfo. This is the base store for the uaItems. From
    //   here references are set to define the items for subscriptions or for bulk access of unsubscribed ones.
    // - the vector unsubscribedItems with references on all uaItems without subscription
    //
    // The DevUaSubscription class
    //
    // - contains references to all its uaItems in the member vector m_vectorUaItemInfo.


    static std::map<std::string,DevUaSession*> sessions;         // sessionTag = DevUaSession*,       set by drvOpcuaSetup()
    static std::map<std::string,DevUaSession*> subscr2session;   // subscriptionTag = DevUaSession*, set by drvOpcuaSubscription()

    // array of all uaItems, to get an unique index used as transaction number for async access.
    std::vector<UaItem *> vUaItemInfo;

    // reference of all uaItems without subscription
    std::vector<UaItem *> unsubscribedItems;

    // all subscriptions of this session. References to subscribed items in the DevUaSubscription class
    std::map<std::string,DevUaSubscription*> subscriptions;

    int debug;
    int autoConnect;
    UaClientSdk::UaSession* m_pSession;
    std::string tag;
    UaClientSdk::UaClient::ServerStatus serverConnectionStatus;
    bool initialSubscriptionOver;
    autoSessionConnect *autoConnector;
    epicsTimerQueueActive &queue;
};

typedef std::map<std::string,DevUaSession*>::iterator SessionIterator;

// Timer to retry connecting the session when the server is down at IOC startup
class autoSessionConnect : public epicsTimerNotify {
public:
    autoSessionConnect(DevUaSession *session, const double delay, epicsTimerQueueActive &queue)
        : timer(queue.createTimer())
        , session(session)
        , delay(delay)
    {}
    virtual ~autoSessionConnect() { timer.destroy(); }
    void start() { timer.start(*this, delay); }
    virtual expireStatus expire(const epicsTime &/*currentTime*/) {
        UaStatus result = session->connect();
        if (result.isBad()) {
            return expireStatus(restart, delay);
        } else {
            return expireStatus(noRestart);
        }
    }
private:
    epicsTimer &timer;
    DevUaSession *session;
    const double delay;
};
#endif // UASESSION_H
