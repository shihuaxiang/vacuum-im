#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QObject>
#include <QApplication>
#include <QPluginLoader>
#include <QMultiHash>
#include <../../interfaces/ipluginmanager.h>

//PluginItem
class PluginItem :
  public QObject
{
  Q_OBJECT;

public:
  PluginItem(const QUuid &AUuid, QPluginLoader *ALoader, QObject *AParent)
    : QObject(AParent) 
  {
    FUuid = AUuid;
    FLoader = ALoader;
    FLoader->setParent(this);
    FInfo = new PluginInfo;
    FPlugin = qobject_cast<IPlugin *>(FLoader->instance());
    FPlugin->pluginInfo(FInfo);
    FPlugin->instance()->setParent(FLoader);  
  }
  ~PluginItem()
  {
    delete FInfo;
  }
  const QUuid &uuid() { return FUuid; } 
  IPlugin *plugin() { return FPlugin; }
  PluginInfo *info() { return FInfo; }
private:
  QUuid FUuid;
  QPluginLoader *FLoader;
  IPlugin *FPlugin;
  PluginInfo *FInfo;
};


class PluginManager : 
  public QObject, 
  public IPluginManager
{
  Q_OBJECT;
  Q_INTERFACES(IPluginManager);

public:
  PluginManager(QApplication *AParent);
  ~PluginManager();
  void loadPlugins();
  void initPlugins();
  void startPlugins();

  //IPluginManager 
  virtual QObject *instance() {return this;}
  virtual bool unloadPlugin(const QUuid &AUuid);
  virtual QApplication *application() const;
  virtual IPlugin* getPlugin(const QUuid &uid) const;
  virtual PluginList getPlugins() const;
  virtual PluginList getPlugins(const QString &AInterface) const;
  virtual const PluginInfo *getPluginInfo(const QUuid &AUuid) const;
  virtual QList<QUuid> getDependencesOn(const QUuid &AUuid) const;
  virtual QList<QUuid> getDependencesFor(const QUuid &AUuid) const;
public slots:
  virtual void quit();
signals:
  virtual void aboutToQuit();
protected slots:
  void onAboutToQuit();
protected:
  PluginItem *getPluginItem(const QUuid &AUuid) const;
  bool checkDependences(PluginItem *APluginItem) const;
  bool checkConflicts(PluginItem *APluginItem) const;
  QList<QUuid> getConflicts(PluginItem *APluginItem) const;
private:
  QList<PluginItem *> FPluginItems;
};

#endif
