#include <QtDebug>
#include "rostersmodelplugin.h"

#include <QTreeView>

RostersModelPlugin::RostersModelPlugin()
{
  FRostersModel = NULL;
  FRosterPlugin = NULL;
  FPresencePlugin = NULL;

  FRostersModel = new RostersModel(this);
  connect(FRostersModel,SIGNAL(streamJidChanged(const Jid &, const Jid &)),
    SLOT(onStreamJidChanged(const Jid &, const Jid &)));
}

RostersModelPlugin::~RostersModelPlugin()
{
  delete FRostersModel;
}

//IPlugin
void RostersModelPlugin::pluginInfo(PluginInfo *APluginInfo)
{
  APluginInfo->author = "Potapov S.A. aka Lion";
  APluginInfo->description = tr("Creating and handling roster tree");
  APluginInfo->homePage = "http://jrudevels.org";
  APluginInfo->name = tr("Rosters Model"); 
  APluginInfo->uid = ROSTERSMODEL_UUID;
  APluginInfo->version = "0.1";
}

bool RostersModelPlugin::initConnections(IPluginManager *APluginManager, int &/*AInitOrder*/)
{
  IPlugin *plugin = APluginManager->getPlugins("IRosterPlugin").value(0,NULL);
  if (plugin)
  {
    FRosterPlugin = qobject_cast<IRosterPlugin *>(plugin->instance());
    if (FRosterPlugin)
    {
      connect(FRosterPlugin->instance(), SIGNAL(rosterAdded(IRoster *)),SLOT(onRosterAdded(IRoster *))); 
      connect(FRosterPlugin->instance(), SIGNAL(rosterRemoved(IRoster *)),SLOT(onRosterRemoved(IRoster *))); 
    }
  }

  plugin = APluginManager->getPlugins("IPresencePlugin").value(0,NULL);
  if (plugin)
  {
    FPresencePlugin = qobject_cast<IPresencePlugin *>(plugin->instance());
    if (FPresencePlugin)
    {
      connect(FPresencePlugin->instance(), SIGNAL(presenceAdded(IPresence *)),SLOT(onPresenceAdded(IPresence *))); 
      connect(FPresencePlugin->instance(), SIGNAL(presenceRemoved(IPresence *)),SLOT(onPresenceRemoved(IPresence *))); 
    }
  }

  plugin = APluginManager->getPlugins("IAccountManager").value(0,NULL);
  if (plugin)
  {
    FAccountManager = qobject_cast<IAccountManager *>(plugin->instance());
  }

  return true;
}

//IRostersModelPlugin
IRostersModel *RostersModelPlugin::rostersModel()
{
  return FRostersModel;
}

IRosterIndex *RostersModelPlugin::addStream(IRoster *ARoster, IPresence *APresence)
{
  IRosterIndex *streamRoot = FRostersModel->addStream(ARoster,APresence);
  FRosterPlugin->loadRosterItems(ARoster->streamJid());
  if (FAccountManager)
  {
    IAccount *account = FAccountManager->accountByStream(ARoster->streamJid());
    if (account)
    {
      connect(account->instance(),SIGNAL(changed(const QString &, const QVariant &)),
        SLOT(onAccountChanged(const QString &, const QVariant &)));
      streamRoot->setData(RDR_Name,account->name());
    }
  }
  return streamRoot;
}

void RostersModelPlugin::removeStream(const Jid &AStreamJid)
{
  FRostersModel->removeStream(AStreamJid.pFull());
}

void RostersModelPlugin::onRosterAdded(IRoster *ARoster)
{
  IPresence *presence =0;
  if (FPresencePlugin)
    presence = FPresencePlugin->getPresence(ARoster->streamJid());
  if (presence)
    addStream(ARoster,presence);
}

void RostersModelPlugin::onRosterRemoved(IRoster *ARoster)
{
  removeStream(ARoster->streamJid());
}

void RostersModelPlugin::onPresenceAdded(IPresence *APresence)
{
  IRoster *roster =0;
  if (FRosterPlugin)
    roster = FRosterPlugin->getRoster(APresence->streamJid());
  if (roster)
    addStream(roster,APresence);
}

void RostersModelPlugin::onPresenceRemoved(IPresence *APresence)
{
  removeStream(APresence->streamJid());
}

void RostersModelPlugin::onAccountChanged(const QString &AName, const QVariant &/*AValue*/)
{
  if (AName == AVN_NAME)
  {
    IAccount *account = qobject_cast<IAccount *>(sender());
    if (account && account->isActive())
    {
      IRosterIndex *streamRoot = FRostersModel->getStreamRoot(account->xmppStream()->jid());
      if (streamRoot)
        streamRoot->setData(RDR_Name,account->name());
    }
  }
}

void RostersModelPlugin::onStreamJidChanged(const Jid &ABefour, const Jid &AAfter)
{
  if (!(ABefour && AAfter))
    FRosterPlugin->loadRosterItems(AAfter);
  emit streamJidChanged(ABefour,AAfter);
}

Q_EXPORT_PLUGIN2(RostersModelPlugin, RostersModelPlugin)
