#include "messageprocessor.h"

#include <QVariant>
#include <QTextCursor>
#include <definitions/messagedataroles.h>
#include <definitions/messagewriterorders.h>
#include <definitions/notificationdataroles.h>
#include <utils/logger.h>

#define SHC_MESSAGE         "/message"

MessageProcessor::MessageProcessor()
{
	FXmppStreams = NULL;
	FStanzaProcessor = NULL;
	FNotifications = NULL;
}

MessageProcessor::~MessageProcessor()
{

}

void MessageProcessor::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = tr("Message Manager");
	APluginInfo->description = tr("Allows other modules to send and receive messages");
	APluginInfo->version = "1.0";
	APluginInfo->author = "Potapov S.A. aka Lion";
	APluginInfo->homePage = "http://www.vacuum-im.org";
	APluginInfo->dependences.append(XMPPSTREAMS_UUID);
	APluginInfo->dependences.append(STANZAPROCESSOR_UUID);
}

bool MessageProcessor::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
	Q_UNUSED(AInitOrder);
	IPlugin *plugin = APluginManager->pluginInterface("IXmppStreams").value(0,NULL);
	if (plugin)
	{
		FXmppStreams = qobject_cast<IXmppStreams *>(plugin->instance());
		if (FXmppStreams)
		{
			connect(FXmppStreams->instance(),SIGNAL(added(IXmppStream *)),SLOT(onXmppStreamAdded(IXmppStream *)));
			connect(FXmppStreams->instance(),SIGNAL(removed(IXmppStream *)),SLOT(onXmppStreamRemoved(IXmppStream *)));
			connect(FXmppStreams->instance(),SIGNAL(jidChanged(IXmppStream *, const Jid &)),SLOT(onXmppStreamJidChanged(IXmppStream *, const Jid &)));
		}
	}

	plugin = APluginManager->pluginInterface("IStanzaProcessor").value(0,NULL);
	if (plugin)
	{
		FStanzaProcessor = qobject_cast<IStanzaProcessor *>(plugin->instance());
	}

	plugin = APluginManager->pluginInterface("INotifications").value(0,NULL);
	if (plugin)
	{
		FNotifications = qobject_cast<INotifications *>(plugin->instance());
		if (FNotifications)
		{
			connect(FNotifications->instance(),SIGNAL(notificationActivated(int)), SLOT(onNotificationActivated(int)));
			connect(FNotifications->instance(),SIGNAL(notificationRemoved(int)), SLOT(onNotificationRemoved(int)));
		}
	}

	return FStanzaProcessor!=NULL && FXmppStreams!=NULL;
}

bool MessageProcessor::initObjects()
{
	insertMessageWriter(MWO_MESSAGEPROCESSOR,this);
	insertMessageWriter(MWO_MESSAGEPROCESSOR_ANCHORS,this);
	return true;
}

bool MessageProcessor::stanzaReadWrite(int AHandlerId, const Jid &AStreamJid, Stanza &AStanza, bool &AAccept)
{
	if (FActiveStreams.value(AStreamJid) == AHandlerId)
	{
		Message message(AStanza);
		AAccept = sendMessage(AStreamJid,message,IMessageProcessor::DirectionIn) || AAccept;
	}
	return false;
}

void MessageProcessor::writeTextToMessage(int AOrder, Message &AMessage, QTextDocument *ADocument, const QString &ALang)
{
	if (AOrder == MWO_MESSAGEPROCESSOR)
	{
		AMessage.setBody(prepareBodyForSend(ADocument->toPlainText()),ALang);
	}
}

void MessageProcessor::writeMessageToText(int AOrder, Message &AMessage, QTextDocument *ADocument, const QString &ALang)
{
	if (AOrder == MWO_MESSAGEPROCESSOR)
	{
		QTextCursor cursor(ADocument);
		cursor.insertHtml(prepareBodyForReceive(AMessage.body(ALang)));
	}
	else if (AOrder == MWO_MESSAGEPROCESSOR_ANCHORS)
	{
		QRegExp regexp("\\b((https?|ftp)://|www\\.|xmpp:|magnet:|mailto:)\\S+\\b");
		regexp.setCaseSensitivity(Qt::CaseInsensitive);
		for (QTextCursor cursor = ADocument->find(regexp); !cursor.isNull(); cursor = ADocument->find(regexp,cursor))
		{
			QString link = cursor.selectedText();
			if (QUrl(link).scheme().isEmpty())
				link.prepend("http://");

			QTextCharFormat linkFormat = cursor.charFormat();
			linkFormat.setAnchor(true);
			linkFormat.setAnchorHref(link);
			cursor.setCharFormat(linkFormat);
		}
	}
}

QList<Jid> MessageProcessor::activeStreams() const
{
	return FActiveStreams.keys();
}

bool MessageProcessor::isActiveStream(const Jid &AStreamJid) const
{
	return FActiveStreams.contains(AStreamJid);
}

void MessageProcessor::appendActiveStream(const Jid &AStreamJid)
{
	if (FStanzaProcessor && AStreamJid.isValid() && !FActiveStreams.contains(AStreamJid))
	{
		IStanzaHandle shandle;
		shandle.handler = this;
		shandle.order = SHO_DEFAULT;
		shandle.direction = IStanzaHandle::DirectionIn;
		shandle.streamJid = AStreamJid;
		shandle.conditions.append(SHC_MESSAGE);
		FActiveStreams.insert(shandle.streamJid,FStanzaProcessor->insertStanzaHandle(shandle));
		emit activeStreamAppended(AStreamJid);
	}
}

void MessageProcessor::removeActiveStream(const Jid &AStreamJid)
{
	if (FStanzaProcessor && FActiveStreams.contains(AStreamJid))
	{
		FStanzaProcessor->removeStanzaHandle(FActiveStreams.take(AStreamJid));
		foreach(int notifyId, FNotifyId2MessageId.keys())
		{
			INotification notify = FNotifications->notificationById(notifyId);
			if (AStreamJid == notify.data.value(NDR_STREAM_JID).toString())
				removeMessageNotify(FNotifyId2MessageId.value(notifyId));
		}
		emit activeStreamRemoved(AStreamJid);
	}
}

bool MessageProcessor::sendMessage(const Jid &AStreamJid, Message &AMessage, int ADirection)
{
	if (processMessage(AStreamJid,AMessage,ADirection))
	{
		if (ADirection == IMessageProcessor::DirectionOut)
		{
			Stanza stanza = AMessage.stanza(); // Ignore changes in StanzaProcessor
			if (FStanzaProcessor && FStanzaProcessor->sendStanzaOut(AStreamJid,stanza))
			{
				displayMessage(AStreamJid,AMessage,ADirection);
				emit messageSent(AMessage);
				return true;
			}
		}
		else 
		{
			displayMessage(AStreamJid,AMessage,ADirection);
			emit messageReceived(AMessage);
			return true;
		}
	}
	return false;
}

bool MessageProcessor::processMessage(const Jid &AStreamJid, Message &AMessage, int ADirection)
{
	if (ADirection == IMessageProcessor::DirectionIn)
		AMessage.setTo(AStreamJid.full());
	else
		AMessage.setFrom(AStreamJid.full());

	if (AMessage.data(MDR_MESSAGE_ID).isNull())
		AMessage.setData(MDR_MESSAGE_ID,newMessageId());
	AMessage.setData(MDR_MESSAGE_DIRECTION,ADirection);

	bool hooked = false;
	QMapIterator<int,IMessageEditor *> it(FMessageEditors);
	ADirection == DirectionIn ? it.toFront() : it.toBack();
	while (!hooked && (ADirection==DirectionIn ? it.hasNext() : it.hasPrevious()))
	{
		ADirection == DirectionIn ? it.next() : it.previous();
		hooked = it.value()->messageReadWrite(it.key(), AStreamJid, AMessage, ADirection);
	}

	return !hooked;
}

bool MessageProcessor::displayMessage(const Jid &AStreamJid, Message &AMessage, int ADirection)
{
	Q_UNUSED(AStreamJid);
	IMessageHandler *handler = findMessageHandler(AMessage,ADirection);
	if (handler)
	{
		if (handler->messageDisplay(AMessage,ADirection))
		{
			notifyMessage(handler,AMessage,ADirection);
			return true;
		}
	}
	return false;
}

QList<int> MessageProcessor::notifiedMessages() const
{
	return FNotifiedMessages.keys();
}

Message MessageProcessor::notifiedMessage(int AMesssageId) const
{
	return FNotifiedMessages.value(AMesssageId);
}

int MessageProcessor::notifyByMessage(int AMessageId) const
{
	return FNotifyId2MessageId.key(AMessageId,-1);
}

int MessageProcessor::messageByNotify(int ANotifyId) const
{
	return FNotifyId2MessageId.value(ANotifyId,-1);
}

void MessageProcessor::showNotifiedMessage(int AMessageId)
{
	IMessageHandler *handler = FHandlerForMessage.value(AMessageId,NULL);
	if (handler)
		handler->messageShowWindow(AMessageId);
}

void MessageProcessor::removeMessageNotify(int AMessageId)
{
	int notifyId = FNotifyId2MessageId.key(AMessageId);
	if (notifyId > 0)
	{
		FNotifiedMessages.remove(AMessageId);
		FNotifyId2MessageId.remove(notifyId);
		FHandlerForMessage.remove(AMessageId);
		FNotifications->removeNotification(notifyId);
		emit messageNotifyRemoved(AMessageId);
	}
}

void MessageProcessor::textToMessage(Message &AMessage, const QTextDocument *ADocument, const QString &ALang) const
{
	QTextDocument *documentCopy = ADocument->clone();
	QMapIterator<int,IMessageWriter *> it(FMessageWriters);
	it.toBack();
	while (it.hasPrevious())
	{
		it.previous();
		it.value()->writeTextToMessage(it.key(),AMessage,documentCopy,ALang);
	}
	delete documentCopy;
}

void MessageProcessor::messageToText(QTextDocument *ADocument, const Message &AMessage, const QString &ALang) const
{
	Message messageCopy = AMessage;
	QMapIterator<int,IMessageWriter *> it(FMessageWriters);
	it.toFront();
	while (it.hasNext())
	{
		it.next();
		it.value()->writeMessageToText(it.key(),messageCopy,ADocument,ALang);
	}
}

bool MessageProcessor::createMessageWindow(const Jid &AStreamJid, const Jid &AContactJid, Message::MessageType AType, int AShowMode) const
{
	for (QMultiMap<int, IMessageHandler *>::const_iterator it = FMessageHandlers.constBegin(); it!=FMessageHandlers.constEnd(); ++it)
		if (it.value()->messageShowWindow(it.key(),AStreamJid,AContactJid,AType,AShowMode))
			return true;
	return false;
}

QMultiMap<int, IMessageHandler *> MessageProcessor::messageHandlers() const
{
	return FMessageHandlers;
}

void MessageProcessor::insertMessageHandler(int AOrder, IMessageHandler *AHandler)
{
	if (AHandler && !FMessageHandlers.contains(AOrder,AHandler))
		FMessageHandlers.insertMulti(AOrder,AHandler);
}

void MessageProcessor::removeMessageHandler(int AOrder, IMessageHandler *AHandler)
{
	if (FMessageHandlers.contains(AOrder,AHandler))
		FMessageHandlers.remove(AOrder,AHandler);
}

QMultiMap<int, IMessageWriter *> MessageProcessor::messageWriters() const
{
	return FMessageWriters;
}

void MessageProcessor::insertMessageWriter(int AOrder, IMessageWriter *AWriter)
{
	if (AWriter && !FMessageWriters.contains(AOrder,AWriter))
		FMessageWriters.insertMulti(AOrder,AWriter);
}

void MessageProcessor::removeMessageWriter(int AOrder, IMessageWriter *AWriter)
{
	if (FMessageWriters.contains(AOrder,AWriter))
		FMessageWriters.remove(AOrder,AWriter);
}

QMultiMap<int, IMessageEditor *> MessageProcessor::messageEditors() const
{
	return FMessageEditors;
}

void MessageProcessor::insertMessageEditor(int AOrder, IMessageEditor *AEditor)
{
	if (AEditor && !FMessageEditors.contains(AOrder,AEditor))
		FMessageEditors.insertMulti(AOrder,AEditor);
}

void MessageProcessor::removeMessageEditor(int AOrder, IMessageEditor *AEditor)
{
	if (FMessageEditors.contains(AOrder,AEditor))
		FMessageEditors.remove(AOrder,AEditor);
}

int MessageProcessor::newMessageId()
{
	static int messageId = 1;
	return messageId++;
}

IMessageHandler *MessageProcessor::findMessageHandler(const Message &AMessage, int ADirection)
{
	for (QMultiMap<int, IMessageHandler *>::const_iterator it = FMessageHandlers.constBegin(); it!=FMessageHandlers.constEnd(); ++it)
		if (it.value()->messageCheck(it.key(),AMessage,ADirection))
			return it.value();
	return NULL;
}

void MessageProcessor::notifyMessage(IMessageHandler *AHandler, const Message &AMessage, int ADirection)
{
	if (FNotifications && AHandler)
	{
		INotification notify = AHandler->messageNotify(FNotifications, AMessage, ADirection);
		if (notify.kinds > 0)
		{
			int notifyId = FNotifications->appendNotification(notify);
			int messageId = AMessage.data(MDR_MESSAGE_ID).toInt();
			FNotifiedMessages.insert(messageId,AMessage);
			FNotifyId2MessageId.insert(notifyId,messageId);
			FHandlerForMessage.insert(messageId,AHandler);
			emit messageNotifyInserted(messageId);
		}
	}
}

QString MessageProcessor::prepareBodyForSend(const QString &AString) const
{
	QString result = AString;
	result.remove(QChar::Null);
	result.remove(QChar::ObjectReplacementCharacter);
	return result;
}

QString MessageProcessor::prepareBodyForReceive(const QString &AString) const
{
	QString result = Qt::escape(AString);
	result.replace('\n',"<br>");
	result.replace("  ","&nbsp; ");
	result.replace('\t',"&nbsp; &nbsp; ");
	return result;
}

void MessageProcessor::onNotificationActivated(int ANotifyId)
{
	if (FNotifyId2MessageId.contains(ANotifyId))
		showNotifiedMessage(FNotifyId2MessageId.value(ANotifyId));
}

void MessageProcessor::onNotificationRemoved(int ANotifyId)
{
	if (FNotifyId2MessageId.contains(ANotifyId))
		removeMessageNotify(FNotifyId2MessageId.value(ANotifyId));
}

void MessageProcessor::onXmppStreamAdded(IXmppStream *AXmppStream)
{
	appendActiveStream(AXmppStream->streamJid());
}

void MessageProcessor::onXmppStreamRemoved(IXmppStream *AXmppStream)
{
	removeActiveStream(AXmppStream->streamJid());
}

void MessageProcessor::onXmppStreamJidChanged(IXmppStream *AXmppStream, const Jid &ABefore)
{
	if (FActiveStreams.contains(ABefore))
	{
		int handleId = FActiveStreams.take(ABefore);
		FActiveStreams.insert(AXmppStream->streamJid(),handleId);
	}
}

Q_EXPORT_PLUGIN2(plg_messageprocessor, MessageProcessor)
