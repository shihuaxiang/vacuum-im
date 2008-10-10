#include "discoinfowindow.h"

#include <QHeaderView>

#define IN_INFO               "psi/statusmsg"

DiscoInfoWindow::DiscoInfoWindow(IServiceDiscovery *ADiscovery, const Jid &AStreamJid, const Jid &AContactJid,
                                 const QString &ANode, QWidget *AParent) : QDialog(AParent)
{
  ui.setupUi(this);
  setAttribute(Qt::WA_DeleteOnClose,true);
  setWindowIcon(Skin::getSkinIconset(SYSTEM_ICONSETFILE)->iconByName(IN_INFO));
  setWindowTitle(tr("%1 - Discovery Info").arg(AContactJid.full()));

  FDiscovery = ADiscovery;
  FStreamJid = AStreamJid;
  FContactJid = AContactJid;
  FNode = ANode;

  ui.lblError->setVisible(false);

  connect(FDiscovery->instance(),SIGNAL(discoInfoReceived(const IDiscoInfo &)),
    SLOT(onDiscoInfoReceived(const IDiscoInfo &)));
  connect(FDiscovery->instance(),SIGNAL(streamJidChanged(const Jid &, const Jid &)),
    SLOT(onStreamJidChanged(const Jid &, const Jid &)));
  connect(ui.pbtUpdate,SIGNAL(clicked()),SLOT(onUpdateClicked()));
  connect(ui.lwtFearures,SIGNAL(currentItemChanged(QListWidgetItem *, QListWidgetItem *)),
    SLOT(onCurrentFeatureChanged(QListWidgetItem *, QListWidgetItem *)));
  connect(ui.lwtFearures,SIGNAL(itemActivated(QListWidgetItem *)),SLOT(onListItemActivated(QListWidgetItem *)));

  if (!FDiscovery->hasDiscoInfo(FContactJid,ANode) || FDiscovery->discoInfo(FContactJid,ANode).error.code>0)
    requestDiscoInfo();
  else
    updateWindow();
}

DiscoInfoWindow::~DiscoInfoWindow()
{

}

void DiscoInfoWindow::updateWindow()
{
  IDiscoInfo dinfo = FDiscovery->discoInfo(FContactJid,FNode);
  qSort(dinfo.features);

  int row = 0;
  ui.twtIdentity->clearContents();
  foreach(IDiscoIdentity identity, dinfo.identity)
  {
    ui.twtIdentity->setRowCount(row+1);
    ui.twtIdentity->setItem(row,0,new QTableWidgetItem(identity.category));
    ui.twtIdentity->setItem(row,1,new QTableWidgetItem(identity.type));
    ui.twtIdentity->setItem(row,2,new QTableWidgetItem(identity.name));
    row++;
  }
  ui.twtIdentity->verticalHeader()->resizeSections(QHeaderView::ResizeToContents);

  ui.lwtFearures->clear();
  foreach(QString featureVar, dinfo.features)
  {
    IDiscoFeature dfeature = FDiscovery->discoFeature(featureVar);
    dfeature.var = featureVar;
    QListWidgetItem *listItem = new QListWidgetItem;
    listItem->setIcon(dfeature.icon);
    listItem->setText(dfeature.name.isEmpty() ? dfeature.var : dfeature.name);
    if (FDiscovery->hasFeatureHandler(featureVar))
    {
      QFont font = ui.lwtFearures->font();
      font.setBold(true);
      listItem->setData(Qt::FontRole,font);
    }
    listItem->setData(Qt::UserRole,dfeature.var);
    listItem->setData(Qt::UserRole+1,dfeature.description);
    ui.lwtFearures->addItem(listItem);
  }

  if (ui.lwtFearures->currentItem())
    ui.lblFeatureDesc->setText(ui.lwtFearures->currentItem()->data(Qt::UserRole).toString());

  if (dinfo.error.code > 0)
  {
    ui.lblError->setText(tr("Error %1: %2").arg(dinfo.error.code).arg(dinfo.error.message));
    ui.twtIdentity->setEnabled(false);
    ui.lwtFearures->setEnabled(false);
    ui.lblError->setVisible(true);
  }
  else
  {
    ui.twtIdentity->setEnabled(true);
    ui.lwtFearures->setEnabled(true);
    ui.lblError->setVisible(false);
  }

  ui.twtIdentity->horizontalHeader()->setResizeMode(0,QHeaderView::ResizeToContents);
  ui.twtIdentity->horizontalHeader()->setResizeMode(1,QHeaderView::ResizeToContents);
  ui.twtIdentity->horizontalHeader()->setResizeMode(2,QHeaderView::Stretch);

  ui.pbtUpdate->setEnabled(true);
}

void DiscoInfoWindow::requestDiscoInfo()
{
  if (FDiscovery->requestDiscoInfo(FStreamJid,FContactJid,FNode))
    ui.pbtUpdate->setEnabled(false);
}

void DiscoInfoWindow::onDiscoInfoReceived(const IDiscoInfo &/*ADiscoInfo*/)
{
  updateWindow();
}

void DiscoInfoWindow::onCurrentFeatureChanged(QListWidgetItem *ACurrent, QListWidgetItem * /*APrevious*/)
{
  if (ACurrent)
    ui.lblFeatureDesc->setText(ACurrent->data(Qt::UserRole+1).toString());
  else
    ui.lblFeatureDesc->setText("");
}

void DiscoInfoWindow::onUpdateClicked()
{
  requestDiscoInfo();
}

void DiscoInfoWindow::onListItemActivated(QListWidgetItem *AItem)
{
  QString feature = AItem->data(Qt::UserRole).toString();
  if (FDiscovery->hasFeatureHandler(feature))
  {
    IDiscoInfo dinfo = FDiscovery->discoInfo(FContactJid,FNode);
    FDiscovery->execFeatureHandler(FStreamJid,feature,dinfo);
  }
}

void DiscoInfoWindow::onStreamJidChanged(const Jid &/*ABefour*/, const Jid &AAfter)
{
  FStreamJid = AAfter;
}
