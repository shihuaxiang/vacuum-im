#include "avatars.h"

#include <QFile>
#include <QBuffer>
#include <QDataStream>
#include <QFileDialog>
#include <QImageReader>
#include <QCryptographicHash>

#define DIR_AVATARS               "avatars"

#define SHC_PRESENCE              "/presence"
#define SHC_IQ_AVATAR             "/iq[@type='get']/query[@xmlns='" NS_JABBER_IQ_AVATAR "']"

#define ADR_STREAM_JID            Action::DR_StreamJid
#define ADR_CONTACT_JID           Action::DR_Parametr1

#define AVATAR_IMAGE_TYPE         "png"
#define AVATAR_IQ_TIMEOUT         30000

#define UNKNOWN_AVATAR            QString::null
#define EMPTY_AVATAR              QString("")

Avatars::Avatars()
{
	FPluginManager = NULL;
	FXmppStreams = NULL;
	FStanzaProcessor = NULL;
	FVCardPlugin = NULL;
	FPresencePlugin = NULL;
	FRostersModel = NULL;
	FRostersViewPlugin = NULL;
	FOptionsManager = NULL;

	FAvatarLabelId = -1;
	FAvatarsVisible = false;
	FShowEmptyAvatars = true;
	FAvatarSize = QSize(32,32);
}

Avatars::~Avatars()
{

}

void Avatars::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = tr("Avatars");
	APluginInfo->description = tr("Allows to set and display avatars");
	APluginInfo->version = "1.0";
	APluginInfo->author = "Potapov S.A. aka Lion";
	APluginInfo->homePage = "http://www.vacuum-im.org";
	APluginInfo->dependences.append(VCARD_UUID);
}

bool Avatars::initConnections(IPluginManager *APluginManager, int &/*AInitOrder*/)
{
	FPluginManager = APluginManager;

	IPlugin *plugin = APluginManager->pluginInterface("IXmppStreams").value(0,NULL);
	if (plugin)
	{
		FXmppStreams = qobject_cast<IXmppStreams *>(plugin->instance());
		if (FXmppStreams)
		{
			connect(FXmppStreams->instance(),SIGNAL(opened(IXmppStream *)),SLOT(onStreamOpened(IXmppStream *)));
			connect(FXmppStreams->instance(),SIGNAL(closed(IXmppStream *)),SLOT(onStreamClosed(IXmppStream *)));
		}
	}

	plugin = APluginManager->pluginInterface("IStanzaProcessor").value(0,NULL);
	if (plugin)
		FStanzaProcessor = qobject_cast<IStanzaProcessor *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IVCardPlugin").value(0,NULL);
	if (plugin)
	{
		FVCardPlugin = qobject_cast<IVCardPlugin *>(plugin->instance());
		if (FVCardPlugin)
		{
			connect(FVCardPlugin->instance(),SIGNAL(vcardReceived(const Jid &)),SLOT(onVCardChanged(const Jid &)));
			connect(FVCardPlugin->instance(),SIGNAL(vcardPublished(const Jid &)),SLOT(onVCardChanged(const Jid &)));
		}
	}

	plugin = APluginManager->pluginInterface("IPresencePlugin").value(0,NULL);
	if (plugin)
		FPresencePlugin = qobject_cast<IPresencePlugin *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IRostersModel").value(0,NULL);
	if (plugin)
	{
		FRostersModel = qobject_cast<IRostersModel *>(plugin->instance());
		if (FRostersModel)
		{
			connect(FRostersModel->instance(),SIGNAL(indexInserted(IRosterIndex *)), SLOT(onRosterIndexInserted(IRosterIndex *)));
		}
	}

	plugin = APluginManager->pluginInterface("IRostersViewPlugin").value(0,NULL);
	if (plugin)
	{
		FRostersViewPlugin = qobject_cast<IRostersViewPlugin *>(plugin->instance());
		if (FRostersViewPlugin)
		{
			connect(FRostersViewPlugin->rostersView()->instance(),SIGNAL(indexMultiSelection(const QList<IRosterIndex *> &, bool &)), 
				SLOT(onRosterIndexMultiSelection(const QList<IRosterIndex *> &, bool &)));
			connect(FRostersViewPlugin->rostersView()->instance(),SIGNAL(indexContextMenu(const QList<IRosterIndex *> &, int, Menu *)), 
				SLOT(onRosterIndexContextMenu(const QList<IRosterIndex *> &, int, Menu *)));
			connect(FRostersViewPlugin->rostersView()->instance(),SIGNAL(indexToolTips(IRosterIndex *, int, QMultiMap<int,QString> &)),
			        SLOT(onRosterIndexToolTips(IRosterIndex *, int, QMultiMap<int,QString> &)));
		}
	}

	plugin = APluginManager->pluginInterface("IOptionsManager").value(0,NULL);
	if (plugin)
	{
		FOptionsManager = qobject_cast<IOptionsManager *>(plugin->instance());
	}

	connect(Options::instance(),SIGNAL(optionsOpened()),SLOT(onOptionsOpened()));
	connect(Options::instance(),SIGNAL(optionsClosed()),SLOT(onOptionsClosed()));
	connect(Options::instance(),SIGNAL(optionsChanged(const OptionsNode &)),SLOT(onOptionsChanged(const OptionsNode &)));

	return FVCardPlugin!=NULL;
}

bool Avatars::initObjects()
{
	FAvatarsDir.setPath(FPluginManager->homePath());
	if (!FAvatarsDir.exists(DIR_AVATARS))
		FAvatarsDir.mkdir(DIR_AVATARS);
	FAvatarsDir.cd(DIR_AVATARS);

	onIconStorageChanged();
	connect(IconStorage::staticStorage(RSR_STORAGE_MENUICONS), SIGNAL(storageChanged()), SLOT(onIconStorageChanged()));

	if (FRostersModel)
	{
		FRostersModel->insertDefaultDataHolder(this);
	}
	return true;
}

bool Avatars::initSettings()
{
	Options::setDefaultValue(OPV_AVATARS_SHOW,true);
	Options::setDefaultValue(OPV_AVATARS_SHOWEMPTY,true);

	if (FOptionsManager)
	{
		FOptionsManager->insertOptionsHolder(this);
	}
	return true;
}

bool Avatars::stanzaReadWrite(int AHandlerId, const Jid &AStreamJid, Stanza &AStanza, bool &AAccept)
{
	static const QList<QString> availStanzaTypes = QList<QString>() << QString("") << QString("unavailable");
	if (FSHIPresenceOut.value(AStreamJid) == AHandlerId)
	{
		QDomElement vcardUpdate = AStanza.addElement("x",NS_VCARD_UPDATE);

		const QString &hash = FStreamAvatars.value(AStreamJid);
		if (!hash.isNull() && !FBlockingResources.contains(AStreamJid))   // isNull - avatar not ready, isEmpty - no avatar
		{
			QDomElement photoElem = vcardUpdate.appendChild(AStanza.createElement("photo")).toElement();
			if (!hash.isEmpty())
				photoElem.appendChild(AStanza.createTextNode(hash));
		}

		if (!hash.isEmpty())
		{
			QDomElement iqUpdate = AStanza.addElement("x",NS_JABBER_X_AVATAR);
			QDomElement hashElem = iqUpdate.appendChild(AStanza.createElement("hash")).toElement();
			hashElem.appendChild(AStanza.createTextNode(hash));
		}
	}
	else if (FSHIPresenceIn.value(AStreamJid)==AHandlerId && availStanzaTypes.contains(AStanza.type()))
	{
		Jid contactJid = AStanza.from();
		if (!FStreamAvatars.keys().contains(contactJid) && AStanza.firstElement("x",NS_MUC_USER).isNull())
		{
			QDomElement vcardUpdate = AStanza.firstElement("x",NS_VCARD_UPDATE);
			QDomElement iqUpdate = AStanza.firstElement("x",NS_JABBER_X_AVATAR);
			if (!vcardUpdate.isNull())
			{
				if (!vcardUpdate.firstChildElement("photo").isNull())
				{
					QString hash = vcardUpdate.firstChildElement("photo").text().toLower();
					if (!updateVCardAvatar(contactJid,hash,false))
					{
						FVCardPlugin->requestVCard(AStreamJid,contactJid.bare());
					}
				}
			}
			else if (AStreamJid && contactJid)
			{
				if (AStanza.type().isEmpty())
				{
					FBlockingResources.insert(AStreamJid, contactJid);
					if (!FStreamAvatars.value(AStreamJid).isNull())
					{
						FStreamAvatars[AStreamJid] = UNKNOWN_AVATAR;
						updatePresence(AStreamJid);
					}
				}
				else if (AStanza.type() == "unavailable")
				{
					FBlockingResources.remove(AStreamJid, contactJid);
					if (!FBlockingResources.contains(AStreamJid))
					{
						FVCardPlugin->requestVCard(AStreamJid, contactJid.bare());
					}
				}
			}
			else if (!iqUpdate.isNull())
			{
				QString hash = iqUpdate.firstChildElement("hash").text().toLower();
				if (!updateIqAvatar(contactJid,hash))
				{
					Stanza query("iq");
					query.setTo(contactJid.eFull()).setType("get").setId(FStanzaProcessor->newId());
					query.addElement("query",NS_JABBER_IQ_AVATAR);
					if (FStanzaProcessor->sendStanzaRequest(this,AStreamJid,query,AVATAR_IQ_TIMEOUT))
						FIqAvatarRequests.insert(query.id(),contactJid);
					else
						FIqAvatars.remove(contactJid);
				}
			}
			else
			{
				updateIqAvatar(contactJid,UNKNOWN_AVATAR);
			}
		}
	}
	else if (FSHIIqAvatarIn.value(AStreamJid) == AHandlerId)
	{
		QFile file(avatarFileName(FStreamAvatars.value(AStreamJid)));
		if (file.open(QFile::ReadOnly))
		{
			AAccept = true;
			Stanza result("iq");
			result.setTo(AStanza.from()).setType("result").setId(AStanza.id());
			QDomElement dataElem = result.addElement("query",NS_JABBER_IQ_AVATAR).appendChild(result.createElement("data")).toElement();
			dataElem.appendChild(result.createTextNode(file.readAll().toBase64()));
			FStanzaProcessor->sendStanzaOut(AStreamJid,result);
			file.close();
		}
	}
	return false;
}

void Avatars::stanzaRequestResult(const Jid &AStreamJid, const Stanza &AStanza)
{
	Q_UNUSED(AStreamJid);
	if (FIqAvatarRequests.contains(AStanza.id()))
	{
		Jid contactJid = FIqAvatarRequests.take(AStanza.id());
		if (AStanza.type() == "result")
		{
			QDomElement dataElem = AStanza.firstElement("query",NS_JABBER_IQ_AVATAR).firstChildElement("data");
			QByteArray avatarData = QByteArray::fromBase64(dataElem.text().toAscii());
			if (!avatarData.isEmpty())
			{
				QString hash = saveAvatar(avatarData);
				updateIqAvatar(contactJid,hash);
			}
			else
				FIqAvatars.remove(contactJid);
		}
		else
			FIqAvatars.remove(contactJid);
	}
}

void Avatars::stanzaRequestTimeout(const Jid &AStreamJid, const QString &AStanzaId)
{
	Q_UNUSED(AStreamJid);
	if (FIqAvatarRequests.contains(AStanzaId))
	{
		Jid contactJid = FIqAvatars.take(AStanzaId);
		FIqAvatars.remove(contactJid);
	}
}

int Avatars::rosterDataOrder() const
{
	return RDHO_DEFAULT;
}

QList<int> Avatars::rosterDataRoles() const
{
	static const QList<int> indexRoles = QList<int>() << RDR_AVATAR_HASH << RDR_AVATAR_IMAGE;
	return indexRoles;
}

QList<int> Avatars::rosterDataTypes() const
{
	static const QList<int> indexTypes = QList<int>() << RIT_STREAM_ROOT << RIT_CONTACT;
	return indexTypes;
}

QVariant Avatars::rosterData(const IRosterIndex *AIndex, int ARole) const
{
	if (ARole == RDR_AVATAR_IMAGE)
	{
		QImage avatar = avatarImage(AIndex->data(RDR_FULL_JID).toString());
		if (avatar.isNull() && FShowEmptyAvatars)
			avatar = FEmptyAvatar;
		return avatar;
	}
	else if (ARole == RDR_AVATAR_HASH)
	{
		return avatarHash(AIndex->data(RDR_FULL_JID).toString());
	}
	return QVariant();
}

bool Avatars::setRosterData(IRosterIndex *AIndex, int ARole, const QVariant &AValue)
{
	Q_UNUSED(AIndex);
	Q_UNUSED(ARole);
	Q_UNUSED(AValue);
	return false;
}

QMultiMap<int, IOptionsWidget *> Avatars::optionsWidgets(const QString &ANodeId, QWidget *AParent)
{
	QMultiMap<int, IOptionsWidget *> widgets;
	if (FOptionsManager && ANodeId == OPN_ROSTER)
	{
		widgets.insertMulti(OWO_ROSTER_AVATARS, FOptionsManager->optionsNodeWidget(Options::node(OPV_AVATARS_SHOW),tr("Show avatars"),AParent));
		widgets.insertMulti(OWO_ROSTER_AVATARS, FOptionsManager->optionsNodeWidget(Options::node(OPV_AVATARS_SHOWEMPTY),tr("Show empty avatars"),AParent));
	}
	return widgets;
}

QString Avatars::avatarFileName(const QString &AHash) const
{
	return !AHash.isEmpty() ? FAvatarsDir.filePath(AHash.toLower()) : QString::null;
}

bool Avatars::hasAvatar(const QString &AHash) const
{
	return QFile::exists(avatarFileName(AHash));
}

QImage Avatars::loadAvatar(const QString &AHash) const
{
	return QImage(avatarFileName(AHash));
}

QString Avatars::saveAvatar(const QByteArray &AImageData) const
{
	if (!AImageData.isEmpty())
	{
		QString hash = QCryptographicHash::hash(AImageData,QCryptographicHash::Sha1).toHex();
		if (!hasAvatar(hash))
		{
			QFile file(avatarFileName(hash));
			if (file.open(QFile::WriteOnly|QFile::Truncate))
			{
				file.write(AImageData);
				file.close();
				return hash;
			}
		}
		else
			return hash;
	}
	return EMPTY_AVATAR;
}

QString Avatars::saveAvatar(const QImage &AImage, const char *AFormat) const
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	return AImage.save(&buffer,AFormat) ? saveAvatar(bytes) : EMPTY_AVATAR;
}

QString Avatars::avatarHash(const Jid &AContactJid) const
{
	QString hash = FCustomPictures.value(AContactJid.bare());
	if (hash.isEmpty())
		hash = FIqAvatars.value(AContactJid);
	if (hash.isEmpty())
		hash = FVCardAvatars.value(AContactJid.bare());
	return hash;
}

QImage Avatars::avatarImage(const Jid &AContactJid) const
{
	QString hash = avatarHash(AContactJid);
	if (!hash.isEmpty() && !FAvatarImages.contains(hash))
	{
		QString fileName = avatarFileName(hash);
		if (QFile::exists(fileName))
		{
			QImage image = QImage(fileName).scaled(FAvatarSize,Qt::KeepAspectRatio,Qt::SmoothTransformation);
			if (!image.isNull())
				FAvatarImages.insert(hash,image);
			return image;
		}
	}
	return FAvatarImages.value(hash);
}

bool Avatars::setAvatar(const Jid &AStreamJid, const QImage &AImage, const char *AFormat)
{
	bool published = false;
	IVCard *vcard = FVCardPlugin!=NULL ? FVCardPlugin->vcard(AStreamJid.bare()) : NULL;
	if (vcard)
	{
		const static QSize maxSize = QSize(96,96);
		QImage avatar = AImage.width()>96 || AImage.height()>96 ? AImage.scaled(QSize(96,96),Qt::KeepAspectRatio,Qt::SmoothTransformation) : AImage;
		vcard->setPhotoImage(avatar, AFormat);
		published = FVCardPlugin->publishVCard(vcard,AStreamJid);
		vcard->unlock();
	}
	return published;
}

QString Avatars::setCustomPictire(const Jid &AContactJid, const QString &AImageFile)
{
	Jid contactJid = AContactJid.bare();
	if (!AImageFile.isEmpty())
	{
		QFile file(AImageFile);
		if (file.open(QFile::ReadOnly))
		{
			QString hash = saveAvatar(file.readAll());
			if (FCustomPictures.value(contactJid) != hash)
			{
				FCustomPictures[contactJid] = hash;
				updateDataHolder(contactJid);
			}
			file.close();
			return hash;
		}
	}
	else if (FCustomPictures.contains(contactJid))
	{
		FCustomPictures.remove(contactJid);
		updateDataHolder(contactJid);
	}
	return EMPTY_AVATAR;
}

QByteArray Avatars::loadAvatarFromVCard(const Jid &AContactJid) const
{
	if (FVCardPlugin)
	{
		QDomDocument vcard;
		QFile file(FVCardPlugin->vcardFileName(AContactJid.bare()));
		if (file.open(QFile::ReadOnly) && vcard.setContent(&file,true))
		{
			QDomElement binElem = vcard.documentElement().firstChildElement("vCard").firstChildElement("PHOTO").firstChildElement("BINVAL");
			if (!binElem.isNull())
			{
				return QByteArray::fromBase64(binElem.text().toLatin1());
			}
		}
	}
	return QByteArray();
}

void Avatars::updatePresence(const Jid &AStreamJid) const
{
	IPresence *presence = FPresencePlugin!=NULL ? FPresencePlugin->getPresence(AStreamJid) : NULL;
	if (presence && presence->isOpen())
		presence->setPresence(presence->show(),presence->status(),presence->priority());
}

void Avatars::updateDataHolder(const Jid &AContactJid)
{
	if (FRostersModel)
	{
		QMultiMap<int,QVariant> findData;
		foreach(int type, rosterDataTypes())
			findData.insert(RDR_TYPE,type);
		if (!AContactJid.isEmpty())
			findData.insert(RDR_PREP_BARE_JID,AContactJid.pBare());
		QList<IRosterIndex *> indexes = FRostersModel->rootIndex()->findChilds(findData,true);
		foreach (IRosterIndex *index, indexes)
		{
			emit rosterDataChanged(index,RDR_AVATAR_HASH);
			emit rosterDataChanged(index,RDR_AVATAR_IMAGE);
		}
	}
}

bool Avatars::updateVCardAvatar(const Jid &AContactJid, const QString &AHash, bool AFromVCard)
{
	foreach(Jid streamJid, FStreamAvatars.keys())
	{
		if (!FBlockingResources.contains(streamJid) && (AContactJid && streamJid))
		{
			QString &curHash = FStreamAvatars[streamJid];
			if (curHash.isNull() || curHash!=AHash)
			{
				if (AFromVCard)
				{
					curHash = AHash;
					updatePresence(streamJid);
				}
				else
				{
					curHash = UNKNOWN_AVATAR;
					updatePresence(streamJid);
					return false;
				}
			}
		}
	}

	Jid contactJid = AContactJid.bare();
	if (FVCardAvatars.value(contactJid) != AHash)
	{
		if (AHash.isEmpty() || hasAvatar(AHash))
		{
			FVCardAvatars[contactJid] = AHash;
			updateDataHolder(contactJid);
			emit avatarChanged(contactJid);
		}
		else if (!AHash.isEmpty())
		{
			return false;
		}
	}

	return true;
}

bool Avatars::updateIqAvatar(const Jid &AContactJid, const QString &AHash)
{
	if (FIqAvatars.value(AContactJid) != AHash)
	{
		if (AHash.isEmpty() || hasAvatar(AHash))
		{
			FIqAvatars[AContactJid] = AHash;
			updateDataHolder(AContactJid);
			emit avatarChanged(AContactJid);
		}
		else if (!AHash.isEmpty())
		{
			return false;
		}
	}
	return true;
}

bool Avatars::isSelectionAccepted(const QList<IRosterIndex *> &ASelected) const
{
	static const QList<int> acceptTypes = QList<int>() << RIT_STREAM_ROOT << RIT_CONTACT;
	if (!ASelected.isEmpty())
	{
		int singleType = -1;
		foreach(IRosterIndex *index, ASelected)
		{
			int indexType = index->type();
			if (!acceptTypes.contains(indexType))
				return false;
			else if (singleType!=-1 && singleType!=indexType)
				return false;
			else if (!FStreamAvatars.contains(index->data(RDR_STREAM_JID).toString()))
				return false;
			singleType = indexType;
		}
		return true;
	}
	return false;
}

void Avatars::onStreamOpened(IXmppStream *AXmppStream)
{
	if (FStanzaProcessor && FVCardPlugin)
	{
		IStanzaHandle shandle;
		shandle.handler = this;
		shandle.streamJid = AXmppStream->streamJid();

		shandle.order = SHO_PI_AVATARS;
		shandle.direction = IStanzaHandle::DirectionIn;
		shandle.conditions.append(SHC_PRESENCE);
		FSHIPresenceIn.insert(shandle.streamJid,FStanzaProcessor->insertStanzaHandle(shandle));

		shandle.order = SHO_DEFAULT;
		shandle.direction = IStanzaHandle::DirectionOut;
		FSHIPresenceOut.insert(shandle.streamJid,FStanzaProcessor->insertStanzaHandle(shandle));

		shandle.order = SHO_DEFAULT;
		shandle.direction = IStanzaHandle::DirectionIn;
		shandle.conditions.clear();
		shandle.conditions.append(SHC_IQ_AVATAR);
		FSHIIqAvatarIn.insert(shandle.streamJid,FStanzaProcessor->insertStanzaHandle(shandle));
	}
	FStreamAvatars.insert(AXmppStream->streamJid(),UNKNOWN_AVATAR);

	if (FVCardPlugin)
	{
		FVCardPlugin->requestVCard(AXmppStream->streamJid(),AXmppStream->streamJid().bare());
	}
}

void Avatars::onStreamClosed(IXmppStream *AXmppStream)
{
	if (FStanzaProcessor && FVCardPlugin)
	{
		FStanzaProcessor->removeStanzaHandle(FSHIPresenceIn.take(AXmppStream->streamJid()));
		FStanzaProcessor->removeStanzaHandle(FSHIPresenceOut.take(AXmppStream->streamJid()));
		FStanzaProcessor->removeStanzaHandle(FSHIIqAvatarIn.take(AXmppStream->streamJid()));
	}
	FStreamAvatars.remove(AXmppStream->streamJid());
	FBlockingResources.remove(AXmppStream->streamJid());
}

void Avatars::onVCardChanged(const Jid &AContactJid)
{
	QString hash = saveAvatar(loadAvatarFromVCard(AContactJid));
	updateVCardAvatar(AContactJid,hash,true);
}

void Avatars::onRosterIndexInserted(IRosterIndex *AIndex)
{
	if (FRostersViewPlugin &&  rosterDataTypes().contains(AIndex->type()))
	{
		Jid contactJid = AIndex->data(RDR_PREP_BARE_JID).toString();
		if (!FVCardAvatars.contains(contactJid))
			onVCardChanged(contactJid);
		if (FAvatarsVisible)
			FRostersViewPlugin->rostersView()->insertLabel(FAvatarLabelId, AIndex);
	}
}

void Avatars::onRosterIndexMultiSelection(const QList<IRosterIndex *> &ASelected, bool &AAccepted)
{
	AAccepted = AAccepted || isSelectionAccepted(ASelected);
}

void Avatars::onRosterIndexContextMenu(const QList<IRosterIndex *> &AIndexes, int ALabelId, Menu *AMenu)
{
	if (ALabelId==RLID_DISPLAY && isSelectionAccepted(AIndexes))
	{
		int indexType = AIndexes.first()->type();
		QMap<int, QStringList> rolesMap = FRostersViewPlugin->rostersView()->indexesRolesMap(AIndexes,QList<int>()<<RDR_STREAM_JID<<RDR_PREP_BARE_JID);
		if (indexType == RIT_STREAM_ROOT)
		{
			Menu *avatar = new Menu(AMenu);
			avatar->setTitle(tr("Avatar"));
			avatar->setIcon(RSR_STORAGE_MENUICONS,MNI_AVATAR_CHANGE);

			Action *setup = new Action(avatar);
			setup->setText(tr("Set avatar"));
			setup->setIcon(RSR_STORAGE_MENUICONS,MNI_AVATAR_SET);
			setup->setData(ADR_STREAM_JID,rolesMap.value(RDR_STREAM_JID));
			connect(setup,SIGNAL(triggered(bool)),SLOT(onSetAvatarByAction(bool)));
			avatar->addAction(setup,AG_DEFAULT,false);

			Action *clear = new Action(avatar);
			clear->setText(tr("Clear avatar"));
			clear->setIcon(RSR_STORAGE_MENUICONS,MNI_AVATAR_REMOVE);
			clear->setData(ADR_STREAM_JID,rolesMap.value(RDR_STREAM_JID));
			connect(clear,SIGNAL(triggered(bool)),SLOT(onClearAvatarByAction(bool)));
			avatar->addAction(clear,AG_DEFAULT,false);

			AMenu->addAction(avatar->menuAction(),AG_RVCM_AVATARS,true);
		}
		else if (indexType == RIT_CONTACT)
		{
			Menu *picture = new Menu(AMenu);
			picture->setTitle(tr("Custom picture"));
			picture->setIcon(RSR_STORAGE_MENUICONS,MNI_AVATAR_CHANGE);

			Action *setup = new Action(picture);
			setup->setText(tr("Set custom picture"));
			setup->setIcon(RSR_STORAGE_MENUICONS,MNI_AVATAR_CUSTOM);
			setup->setData(ADR_CONTACT_JID,rolesMap.value(RDR_PREP_BARE_JID));
			connect(setup,SIGNAL(triggered(bool)),SLOT(onSetAvatarByAction(bool)));
			picture->addAction(setup,AG_DEFAULT,false);

			Action *clear = new Action(picture);
			clear->setText(tr("Clear custom picture"));
			clear->setIcon(RSR_STORAGE_MENUICONS,MNI_AVATAR_REMOVE);
			clear->setData(ADR_CONTACT_JID,rolesMap.value(RDR_PREP_BARE_JID));
			connect(clear,SIGNAL(triggered(bool)),SLOT(onClearAvatarByAction(bool)));
			picture->addAction(clear,AG_DEFAULT,false);

			AMenu->addAction(picture->menuAction(),AG_RVCM_AVATARS,true);
		}
	}
}

void Avatars::onRosterIndexToolTips(IRosterIndex *AIndex, int ALabelId, QMultiMap<int,QString> &AToolTips)
{
	if ((ALabelId == RLID_DISPLAY || ALabelId == FAvatarLabelId) && rosterDataTypes().contains(AIndex->type()))
	{
		QString hash = AIndex->data(RDR_AVATAR_HASH).toString();
		if (hasAvatar(hash))
		{
			QString fileName = avatarFileName(hash);
			QSize imageSize = QImageReader(fileName).size();
			if (ALabelId!=FAvatarLabelId && (imageSize.height()>64 || imageSize.width()>64))
				imageSize.scale(QSize(64,64), Qt::KeepAspectRatio);
			QString avatarMask = "<img src='%1' width=%2 height=%3 />";
			AToolTips.insert(RTTO_AVATAR_IMAGE,avatarMask.arg(fileName).arg(imageSize.width()).arg(imageSize.height()));
		}
	}
}

void Avatars::onSetAvatarByAction(bool)
{
	Action *action = qobject_cast<Action *>(sender());
	if (action)
	{
		QString fileName = QFileDialog::getOpenFileName(NULL, tr("Select avatar image"),"",tr("Image Files (*.png *.jpg *.bmp *.gif)"));
		if (!fileName.isEmpty())
		{
			if (!action->data(ADR_STREAM_JID).isNull())
			{
				QImage avatar(fileName);
				foreach(Jid streamJid, action->data(ADR_STREAM_JID).toStringList())
					setAvatar(streamJid,avatar,AVATAR_IMAGE_TYPE);
			}
			else if (!action->data(ADR_CONTACT_JID).isNull())
			{
				foreach(Jid contactJid, action->data(ADR_CONTACT_JID).toStringList())
					setCustomPictire(contactJid,fileName);
			}
		}
	}
}

void Avatars::onClearAvatarByAction(bool)
{
	Action *action = qobject_cast<Action *>(sender());
	if (action)
	{
		if (!action->data(ADR_STREAM_JID).isNull())
		{
			Jid streamJid = action->data(ADR_STREAM_JID).toString();
			setAvatar(streamJid,QImage());
		}
		else if (!action->data(ADR_CONTACT_JID).isNull())
		{
			Jid contactJid = action->data(ADR_CONTACT_JID).toString();
			setCustomPictire(contactJid,QString::null);
		}
	}
}

void Avatars::onIconStorageChanged()
{
	FEmptyAvatar = QImage(IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->fileFullName(MNI_AVATAR_EMPTY)).scaled(FAvatarSize,Qt::KeepAspectRatio,Qt::FastTransformation);
}

void Avatars::onOptionsOpened()
{
	QByteArray data = Options::fileValue("roster.avatars.custom-pictures").toByteArray();
	QDataStream stream(data);
	stream >> FCustomPictures;

	for (QMap<Jid,QString>::iterator it = FCustomPictures.begin(); it != FCustomPictures.end(); )
	{
		if (!hasAvatar(it.value()))
			it = FCustomPictures.erase(it);
		else
			it++;
	}

	onOptionsChanged(Options::node(OPV_AVATARS_SHOW));
	onOptionsChanged(Options::node(OPV_AVATARS_SHOWEMPTY));
}

void Avatars::onOptionsClosed()
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << FCustomPictures;
	Options::setFileValue(data,"roster.avatars.custom-pictures");

	FIqAvatars.clear();
	FVCardAvatars.clear();
	FAvatarImages.clear();
	FCustomPictures.clear();
}

void Avatars::onOptionsChanged(const OptionsNode &ANode)
{
	if (ANode.path() == OPV_AVATARS_SHOW)
	{
		FAvatarsVisible = ANode.value().toBool();
		if (FRostersViewPlugin && FRostersModel)
		{
			if (FAvatarsVisible)
			{
				QMultiMap<int,QVariant> findData;
				foreach(int type, rosterDataTypes())
					findData.insertMulti(RDR_TYPE,type);
				QList<IRosterIndex *> indexes = FRostersModel->rootIndex()->findChilds(findData, true);

				IRostersLabel label;
				label.order = RLO_AVATAR_IMAGE;
				label.value = RDR_AVATAR_IMAGE;
				FAvatarLabelId = FRostersViewPlugin->rostersView()->registerLabel(label);

				foreach (IRosterIndex *index, indexes)
					FRostersViewPlugin->rostersView()->insertLabel(FAvatarLabelId, index);
			}
			else
			{
				FRostersViewPlugin->rostersView()->destroyLabel(FAvatarLabelId);
				FAvatarLabelId = -1;
				FAvatarImages.clear();
			}
		}
	}
	else if (ANode.path() == OPV_AVATARS_SHOWEMPTY)
	{
		FShowEmptyAvatars = ANode.value().toBool();
		updateDataHolder();
	}
}

Q_EXPORT_PLUGIN2(plg_avatars, Avatars)
