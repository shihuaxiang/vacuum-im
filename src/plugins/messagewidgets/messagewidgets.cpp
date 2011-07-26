#include "messagewidgets.h"

#include <QPair>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

MessageWidgets::MessageWidgets()
{
	FPluginManager = NULL;
	FXmppStreams = NULL;
	FOptionsManager = NULL;
}

MessageWidgets::~MessageWidgets()
{

}

void MessageWidgets::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = tr("Message Widgets Manager");
	APluginInfo->description = tr("Allows other modules to use standard widgets for messaging");
	APluginInfo->version = "1.0";
	APluginInfo->author = "Potapov S.A. aka Lion";
	APluginInfo->homePage = "http://www.vacuum-im.org";
}

bool MessageWidgets::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
	Q_UNUSED(AInitOrder);
	FPluginManager = APluginManager;

	IPlugin *plugin = APluginManager->pluginInterface("IOptionsManager").value(0,NULL);
	if (plugin)
	{
		FOptionsManager = qobject_cast<IOptionsManager *>(plugin->instance());
	}

	plugin = APluginManager->pluginInterface("IXmppStreams").value(0,NULL);
	if (plugin)
	{
		FXmppStreams = qobject_cast<IXmppStreams *>(plugin->instance());
		if (FXmppStreams)
		{
			connect(FXmppStreams->instance(),SIGNAL(jidAboutToBeChanged(IXmppStream *, const Jid &)),
				SLOT(onStreamJidAboutToBeChanged(IXmppStream *, const Jid &)));
			connect(FXmppStreams->instance(),SIGNAL(removed(IXmppStream *)),SLOT(onStreamRemoved(IXmppStream *)));
		}
	}

	connect(Options::instance(),SIGNAL(optionsOpened()),SLOT(onOptionsOpened()));
	connect(Options::instance(),SIGNAL(optionsClosed()),SLOT(onOptionsClosed()));

	return true;
}

bool MessageWidgets::initObjects()
{
	Shortcuts::declareGroup(SCTG_TABWINDOW, tr("Tab window"), SGO_TABWINDOW);
	Shortcuts::declareShortcut(SCT_TABWINDOW_CLOSETAB, tr("Close tab"), tr("Ctrl+W","Close tab"));
	Shortcuts::declareShortcut(SCT_TABWINDOW_DETACHTAB, tr("Detach tab to separate window"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_TABWINDOW_NEXTTAB, tr("Next tab"), QKeySequence::NextChild);
	Shortcuts::declareShortcut(SCT_TABWINDOW_PREVTAB, tr("Previous tab"), QKeySequence::PreviousChild);
	Shortcuts::declareShortcut(SCT_TABWINDOW_SHOWCLOSEBUTTTONS, tr("Set tabs closable"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_TABWINDOW_TABSBOTTOM, tr("Show tabs at bottom"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_TABWINDOW_TABSINDICES, tr("Show tabs indices"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_TABWINDOW_RENAMEWINDOW, tr("Rename tab window"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_TABWINDOW_CLOSEWINDOW, tr("Close tab window"), tr("Esc","Close tab window"));
	Shortcuts::declareShortcut(SCT_TABWINDOW_DELETEWINDOW, tr("Delete tab window"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_TABWINDOW_SETASDEFAULT, tr("Use as default tab window"), QKeySequence::UnknownKey);
	for (int tabNumber=1; tabNumber<=10; tabNumber++)
		Shortcuts::declareShortcut(QString(SCT_TABWINDOW_QUICKTAB).arg(tabNumber), QString::null, tr("Alt+%1","Show tab").arg(tabNumber % 10));

	Shortcuts::declareGroup(SCTG_MESSAGEWINDOWS, tr("Message windows"), SGO_MESSAGEWINDOWS);
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_QUOTE, tr("Quote selected text"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_CLOSEWINDOW, tr("Close message window"), tr("Esc","Close message window"));
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_EDITNEXTMESSAGE, tr("Edit next message"), tr("Ctrl+Down","Edit next message"), Shortcuts::WidgetShortcut);
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_EDITPREVMESSAGE, tr("Edit previous message"), tr("Ctrl+Up","Edit previous message"), Shortcuts::WidgetShortcut);
	
	Shortcuts::declareGroup(SCTG_MESSAGEWINDOWS_CHAT, tr("Chat window"), SGO_MESSAGEWINDOWS_CHAT);
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_CHAT_SENDMESSAGE, tr("Send message"), tr("Return","Send message"), Shortcuts::WidgetShortcut);

	Shortcuts::declareGroup(SCTG_MESSAGEWINDOWS_NORMAL, tr("Message window"), SGO_MESSAGEWINDOWS_NORMAL);
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_NORMAL_SENDMESSAGE, tr("Send message"), tr("Ctrl+Return","Send message"), Shortcuts::WidgetShortcut);

	insertViewUrlHandler(VUHO_MESSAGEWIDGETS_DEFAULT,this);
	insertEditContentsHandler(ECHO_MESSAGEWIDGETS_FORMAT,this);
	insertEditContentsHandler(ECHO_MESSAGEWIDGETS_IMAGES,this);

	return true;
}

bool MessageWidgets::initSettings()
{
	Options::setDefaultValue(OPV_MESSAGES_SHOWSTATUS,true);
	Options::setDefaultValue(OPV_MESSAGES_ARCHIVESTATUS,false);
	Options::setDefaultValue(OPV_MESSAGES_EDITORAUTORESIZE,true);
	Options::setDefaultValue(OPV_MESSAGES_SHOWINFOWIDGET,true);
	Options::setDefaultValue(OPV_MESSAGES_INFOWIDGETMAXSTATUSCHARS,140);
	Options::setDefaultValue(OPV_MESSAGES_EDITORMINIMUMLINES,1);
	Options::setDefaultValue(OPV_MESSAGES_CLEANCHATTIMEOUT,30);
	Options::setDefaultValue(OPV_MESSAGES_TABWINDOWS_ENABLE,true);
	Options::setDefaultValue(OPV_MESSAGES_TABWINDOW_NAME,tr("Tab Window"));
	Options::setDefaultValue(OPV_MESSAGES_TABWINDOW_TABSCLOSABLE,true);
	Options::setDefaultValue(OPV_MESSAGES_TABWINDOW_TABSBOTTOM,false);
	Options::setDefaultValue(OPV_MESSAGES_TABWINDOW_SHOWINDICES,false);
	Options::setDefaultValue(OPV_MESSAGES_TABWINDOW_REMOVETABSONCLOSE,false);

	if (FOptionsManager)
	{
		IOptionsDialogNode dnode = { ONO_MESSAGES, OPN_MESSAGES, tr("Messages"), MNI_NORMAL_MHANDLER_MESSAGE };
		FOptionsManager->insertOptionsDialogNode(dnode);
		FOptionsManager->insertOptionsHolder(this);
	}
	return true;
}

QMultiMap<int, IOptionsWidget *> MessageWidgets::optionsWidgets(const QString &ANodeId, QWidget *AParent)
{
	QMultiMap<int, IOptionsWidget *> widgets;
	if (FOptionsManager && ANodeId == OPN_MESSAGES)
	{
		widgets.insertMulti(OWO_MESSAGES,FOptionsManager->optionsNodeWidget(Options::node(OPV_MESSAGES_TABWINDOWS_ENABLE),tr("Enable tab windows"),AParent));
		widgets.insertMulti(OWO_MESSAGES,FOptionsManager->optionsNodeWidget(Options::node(OPV_MESSAGES_SHOWSTATUS),tr("Show status changes in chat windows"),AParent));
		widgets.insertMulti(OWO_MESSAGES,FOptionsManager->optionsNodeWidget(Options::node(OPV_MESSAGES_ARCHIVESTATUS),tr("Save status messages to history"),AParent));
		widgets.insertMulti(OWO_MESSAGES,FOptionsManager->optionsNodeWidget(Options::node(OPV_MESSAGES_EDITORAUTORESIZE),tr("Auto resize input field"),AParent));
		widgets.insertMulti(OWO_MESSAGES,FOptionsManager->optionsNodeWidget(Options::node(OPV_MESSAGES_SHOWINFOWIDGET),tr("Show contact information in chat windows"),AParent));
		widgets.insertMulti(OWO_MESSAGES,new MessengerOptions(this,AParent));
	}
	return widgets;
}

bool MessageWidgets::viewUrlOpen(int AOrder, IViewWidget* APage, const QUrl &AUrl)
{
	Q_UNUSED(APage);
	Q_UNUSED(AOrder);
	return QDesktopServices::openUrl(AUrl);
}

void MessageWidgets::editContentsChanged(int AOrder, IEditWidget *AWidget, int &APosition, int &ARemoved, int &AAdded)
{
	Q_UNUSED(ARemoved);
	if (!AWidget->textFormatEnabled() && AAdded>0)
	{
		QTextCharFormat emptyFormat;
		QList< QPair<int,int> > formats;
		QTextBlock block = AWidget->document()->findBlock(APosition);
		while (block.isValid() && block.position()<=APosition+AAdded)
		{
			for (QTextBlock::iterator it = block.begin(); !it.atEnd(); it++)
			{
				QTextCharFormat textFormat = it.fragment().charFormat();
				if (textFormat.isImageFormat())
				{
					if (AOrder == ECHO_MESSAGEWIDGETS_IMAGES)
						formats.append(qMakePair(it.fragment().position(),it.fragment().length()));
				}
				else if (textFormat != emptyFormat)
				{
					if (AOrder == ECHO_MESSAGEWIDGETS_FORMAT)
						formats.append(qMakePair(it.fragment().position(),it.fragment().length()));
				}
			}
			block = block.next();
		}

		if (!formats.isEmpty())
		{
			int posOffset = 0;
			QTextCursor cursor(AWidget->document());
			cursor.beginEditBlock();
			for (int i=0; i<formats.count(); i++)
			{
				const QPair<int,int> &format = formats.at(i);
				cursor.setPosition(format.first-posOffset);
				cursor.setPosition(format.first-posOffset+format.second,QTextCursor::KeepAnchor);
				if (AOrder == ECHO_MESSAGEWIDGETS_IMAGES)
				{
					posOffset += cursor.selectedText().length();
					cursor.removeSelectedText();
				}
				else if (AOrder == ECHO_MESSAGEWIDGETS_FORMAT)
				{
					cursor.setCharFormat(emptyFormat);
				}
			}
			cursor.endEditBlock();
			AAdded -= posOffset;
		}
	}
}

IInfoWidget *MessageWidgets::newInfoWidget(const Jid &AStreamJid, const Jid &AContactJid, QWidget *AParent)
{
	IInfoWidget *widget = new InfoWidget(this,AStreamJid,AContactJid,AParent);
	FCleanupHandler.add(widget->instance());
	emit infoWidgetCreated(widget);
	return widget;
}

IViewWidget *MessageWidgets::newViewWidget(const Jid &AStreamJid, const Jid &AContactJid, QWidget *AParent)
{
	IViewWidget *widget = new ViewWidget(this,AStreamJid,AContactJid,AParent);
	connect(widget->instance(),SIGNAL(urlClicked(const QUrl &)),SLOT(onViewWidgetUrlClicked(const QUrl &)));
	FCleanupHandler.add(widget->instance());
	emit viewWidgetCreated(widget);
	return widget;
}

IEditWidget *MessageWidgets::newEditWidget(const Jid &AStreamJid, const Jid &AContactJid, QWidget *AParent)
{
	IEditWidget *widget = new EditWidget(this,AStreamJid,AContactJid,AParent);
	connect(widget->instance(),SIGNAL(contentsChanged(int, int, int)),SLOT(onEditWidgetContentsChanged(int, int, int)));
	FCleanupHandler.add(widget->instance());
	emit editWidgetCreated(widget);
	return widget;
}

IReceiversWidget *MessageWidgets::newReceiversWidget(const Jid &AStreamJid, QWidget *AParent)
{
	IReceiversWidget *widget = new ReceiversWidget(this,AStreamJid,AParent);
	FCleanupHandler.add(widget->instance());
	emit receiversWidgetCreated(widget);
	return widget;
}

IMenuBarWidget *MessageWidgets::newMenuBarWidget(IInfoWidget *AInfo, IViewWidget *AView, IEditWidget *AEdit, IReceiversWidget *AReceivers, QWidget *AParent)
{
	IMenuBarWidget *widget = new MenuBarWidget(AInfo,AView,AEdit,AReceivers,AParent);
	FCleanupHandler.add(widget->instance());
	emit menuBarWidgetCreated(widget);
	return widget;
}

IToolBarWidget *MessageWidgets::newToolBarWidget(IInfoWidget *AInfo, IViewWidget *AView, IEditWidget *AEdit, IReceiversWidget *AReceivers, QWidget *AParent)
{
	IToolBarWidget *widget = new ToolBarWidget(AInfo,AView,AEdit,AReceivers,AParent);
	FCleanupHandler.add(widget->instance());
	insertQuoteAction(widget);
	emit toolBarWidgetCreated(widget);
	return widget;
}

IStatusBarWidget *MessageWidgets::newStatusBarWidget(IInfoWidget *AInfo, IViewWidget *AView, IEditWidget *AEdit, IReceiversWidget *AReceivers, QWidget *AParent)
{
	IStatusBarWidget *widget = new StatusBarWidget(AInfo,AView,AEdit,AReceivers,AParent);
	FCleanupHandler.add(widget->instance());
	emit statusBarWidgetCreated(widget);
	return widget;
}

QList<IMessageWindow *> MessageWidgets::messageWindows() const
{
	return FMessageWindows;
}

IMessageWindow *MessageWidgets::newMessageWindow(const Jid &AStreamJid, const Jid &AContactJid, IMessageWindow::Mode AMode)
{
	IMessageWindow *window = findMessageWindow(AStreamJid,AContactJid);
	if (!window)
	{
		window = new MessageWindow(this,AStreamJid,AContactJid,AMode);
		FMessageWindows.append(window);
		WidgetManager::setWindowSticky(window->instance(),true);
		connect(window->instance(),SIGNAL(windowDestroyed()),SLOT(onMessageWindowDestroyed()));
		FCleanupHandler.add(window->instance());
		emit messageWindowCreated(window);
		return window;
	}
	return NULL;
}

IMessageWindow *MessageWidgets::findMessageWindow(const Jid &AStreamJid, const Jid &AContactJid) const
{
	foreach(IMessageWindow *window,FMessageWindows)
		if (window->streamJid() == AStreamJid && window->contactJid() == AContactJid)
			return window;
	return NULL;
}

QList<IChatWindow *> MessageWidgets::chatWindows() const
{
	return FChatWindows;
}

IChatWindow *MessageWidgets::newChatWindow(const Jid &AStreamJid, const Jid &AContactJid)
{
	IChatWindow *window = findChatWindow(AStreamJid,AContactJid);
	if (!window)
	{
		window = new ChatWindow(this,AStreamJid,AContactJid);
		FChatWindows.append(window);
		WidgetManager::setWindowSticky(window->instance(),true);
		connect(window->instance(),SIGNAL(tabPageDestroyed()),SLOT(onChatWindowDestroyed()));
		FCleanupHandler.add(window->instance());
		emit chatWindowCreated(window);
		return window;
	}
	return NULL;
}

IChatWindow *MessageWidgets::findChatWindow(const Jid &AStreamJid, const Jid &AContactJid) const
{
	foreach(IChatWindow *window,FChatWindows)
		if (window->streamJid() == AStreamJid && window->contactJid() == AContactJid)
			return window;
	return NULL;
}

QList<QUuid> MessageWidgets::tabWindowList() const
{
	QList<QUuid> list;
	foreach(QString tabWindowId, Options::node(OPV_MESSAGES_TABWINDOWS_ROOT).childNSpaces("window"))
		list.append(tabWindowId);
	return list;
}

QUuid MessageWidgets::appendTabWindow(const QString &AName)
{
	QUuid id = QUuid::createUuid();
	QString name = AName;
	if (name.isEmpty())
	{
		QList<QString> names;
		foreach(QString tabWindowId, Options::node(OPV_MESSAGES_TABWINDOWS_ROOT).childNSpaces("window"))
			names.append(Options::node(OPV_MESSAGES_TABWINDOW_ITEM,tabWindowId).value().toString());

		int i = 0;
		do
		{
			i++;
			name = tr("Tab Window %1").arg(i);
		} while (names.contains(name));
	}
	Options::node(OPV_MESSAGES_TABWINDOW_ITEM,id.toString()).setValue(name,"name");
	emit tabWindowAppended(id,name);
	return id;
}

void MessageWidgets::deleteTabWindow(const QUuid &AWindowId)
{
	if (AWindowId!=Options::node(OPV_MESSAGES_TABWINDOWS_DEFAULT).value().toString() && tabWindowList().contains(AWindowId))
	{
		ITabWindow *window = findTabWindow(AWindowId);
		if (window)
			window->instance()->deleteLater();
		Options::node(OPV_MESSAGES_TABWINDOWS_ROOT).removeChilds("window",AWindowId.toString());
		emit tabWindowDeleted(AWindowId);
	}
}

QString MessageWidgets::tabWindowName(const QUuid &AWindowId) const
{
	if (tabWindowList().contains(AWindowId))
		return Options::node(OPV_MESSAGES_TABWINDOW_ITEM,AWindowId.toString()).value("name").toString();
	return Options::defaultValue(OPV_MESSAGES_TABWINDOW_NAME).toString();
}

void MessageWidgets::setTabWindowName(const QUuid &AWindowId, const QString &AName)
{
	if (!AName.isEmpty() && tabWindowList().contains(AWindowId))
	{
		Options::node(OPV_MESSAGES_TABWINDOW_ITEM,AWindowId.toString()).setValue(AName,"name");
		emit tabWindowNameChanged(AWindowId,AName);
	}
}

QList<ITabWindow *> MessageWidgets::tabWindows() const
{
	return FTabWindows;
}

ITabWindow *MessageWidgets::openTabWindow(const QUuid &AWindowId)
{
	ITabWindow *window = findTabWindow(AWindowId);
	if (!window)
	{
		window = new TabWindow(this,AWindowId);
		FTabWindows.append(window);
		WidgetManager::setWindowSticky(window->instance(),true);
		connect(window->instance(),SIGNAL(tabPageAdded(ITabPage *)),SLOT(onTabWindowPageAdded(ITabPage *)));
		connect(window->instance(),SIGNAL(windowDestroyed()),SLOT(onTabWindowDestroyed()));
		emit tabWindowCreated(window);
	}
	window->showWindow();
	return window;
}

ITabWindow *MessageWidgets::findTabWindow(const QUuid &AWindowId) const
{
	foreach(ITabWindow *window,FTabWindows)
		if (window->windowId() == AWindowId)
			return window;
	return NULL;
}

void MessageWidgets::assignTabWindowPage(ITabPage *APage)
{
	if (Options::node(OPV_MESSAGES_TABWINDOWS_ENABLE).value().toBool())
	{
		QList<QUuid> availWindows = tabWindowList();
		QUuid windowId = FPageWindows.value(APage->tabPageId());
		if (!availWindows.contains(windowId))
			windowId = Options::node(OPV_MESSAGES_TABWINDOWS_DEFAULT).value().toString();
		if (!availWindows.contains(windowId))
			windowId = availWindows.value(0);
		ITabWindow *window = openTabWindow(windowId);
		window->addTabPage(APage);
	}
}

QList<IViewDropHandler *> MessageWidgets::viewDropHandlers() const
{
	return FViewDropHandlers;
}

void MessageWidgets::insertViewDropHandler(IViewDropHandler *AHandler)
{
	if (!FViewDropHandlers.contains(AHandler))
	{
		FViewDropHandlers.append(AHandler);
		emit viewDropHandlerInserted(AHandler);
	}
}

void MessageWidgets::removeViewDropHandler(IViewDropHandler *AHandler)
{
	if (FViewDropHandlers.contains(AHandler))
	{
		FViewDropHandlers.removeAll(AHandler);
		emit viewDropHandlerRemoved(AHandler);
	}
}

QMultiMap<int, IViewUrlHandler *> MessageWidgets::viewUrlHandlers() const
{
	return FViewUrlHandlers;
}

void MessageWidgets::insertViewUrlHandler(int AOrder, IViewUrlHandler *AHandler)
{
	if (!FViewUrlHandlers.values(AOrder).contains(AHandler))
	{
		FViewUrlHandlers.insertMulti(AOrder,AHandler);
		emit viewUrlHandlerInserted(AOrder,AHandler);
	}
}

void MessageWidgets::removeViewUrlHandler(int AOrder, IViewUrlHandler *AHandler)
{
	if (FViewUrlHandlers.values(AOrder).contains(AHandler))
	{
		FViewUrlHandlers.remove(AOrder,AHandler);
		emit viewUrlHandlerRemoved(AOrder,AHandler);
	}
}

QMultiMap<int, IEditContentsHandler *> MessageWidgets::editContentsHandlers() const
{
	return FEditContentsHandlers;
}

void MessageWidgets::insertEditContentsHandler(int AOrder, IEditContentsHandler *AHandler)
{
	if (!FEditContentsHandlers.values(AOrder).contains(AHandler))
	{
		FEditContentsHandlers.insertMulti(AOrder,AHandler);
		emit editContentsHandlerInserted(AOrder,AHandler);
	}
}

void MessageWidgets::removeEditContentsHandler(int AOrder, IEditContentsHandler *AHandler)
{
	if (FEditContentsHandlers.values(AOrder).contains(AHandler))
	{
		FEditContentsHandlers.remove(AOrder,AHandler);
		emit editContentsHandlerRemoved(AOrder,AHandler);
	}
}

void MessageWidgets::insertQuoteAction(IToolBarWidget *AWidget)
{
	if (AWidget->viewWidget() && AWidget->editWidget())
	{
		Action *action = new Action(AWidget->instance());
		action->setToolTip(tr("Quote selected text"));
		action->setIcon(RSR_STORAGE_MENUICONS, MNI_MESSAGEWIDGETS_QUOTE);
		action->setShortcutId(SCT_MESSAGEWINDOWS_QUOTE);
		connect(action,SIGNAL(triggered(bool)),SLOT(onQuoteActionTriggered(bool)));
		AWidget->toolBarChanger()->insertAction(action,TBG_MWTBW_MESSAGEWIDGETS_QUOTE);
	}
}

void MessageWidgets::deleteWindows()
{
	foreach(ITabWindow *window, tabWindows())
		delete window->instance();
}

void MessageWidgets::deleteStreamWindows(const Jid &AStreamJid)
{
	QList<IChatWindow *> chatWindows = FChatWindows;
	foreach(IChatWindow *window, chatWindows)
		if (window->streamJid() == AStreamJid)
			delete window->instance();

	QList<IMessageWindow *> messageWindows = FMessageWindows;
	foreach(IMessageWindow *window, messageWindows)
		if (window->streamJid() == AStreamJid)
			delete window->instance();
}

void MessageWidgets::onViewWidgetUrlClicked(const QUrl &AUrl)
{
	IViewWidget *widget = qobject_cast<IViewWidget *>(sender());
	if (widget)
	{
		for (QMap<int,IViewUrlHandler *>::const_iterator it = FViewUrlHandlers.constBegin(); it!=FViewUrlHandlers.constEnd(); it++)
			if (it.value()->viewUrlOpen(it.key(),widget,AUrl))
				break;
	}
}

void MessageWidgets::onEditWidgetContentsChanged(int APosition, int ARemoved, int AAdded)
{
	IEditWidget *widget = qobject_cast<IEditWidget *>(sender());
	if (widget)
	{
		widget->document()->blockSignals(true);
		for (QMap<int,IEditContentsHandler *>::const_iterator it = FEditContentsHandlers.constBegin(); it!=FEditContentsHandlers.constEnd(); it++)
			it.value()->editContentsChanged(it.key(),widget,APosition,ARemoved,AAdded);
		widget->document()->blockSignals(false);
	}
}

void MessageWidgets::onQuoteActionTriggered(bool)
{
	Action *action = qobject_cast<Action *>(sender());
	IToolBarWidget *widget = action!=NULL ? qobject_cast<IToolBarWidget *>(action->parent()) : NULL;
	if (widget && widget->viewWidget() && widget->viewWidget()->messageStyle() && widget->editWidget())
	{
		QTextDocumentFragment fragment = widget->viewWidget()->messageStyle()->selection(widget->viewWidget()->styleWidget());
		if (!fragment.toPlainText().trimmed().isEmpty())
		{
			QTextEdit *editor = widget->editWidget()->textEdit();
			editor->textCursor().beginEditBlock();
			if (!editor->textCursor().atBlockStart())
				editor->textCursor().insertText("\n");
			editor->textCursor().insertText("> ");
			editor->textCursor().insertFragment(fragment);
			editor->textCursor().insertText("\n");
			editor->textCursor().endEditBlock();
			editor->setFocus();
		}
	}
}

void MessageWidgets::onMessageWindowDestroyed()
{
	IMessageWindow *window = qobject_cast<IMessageWindow *>(sender());
	if (window)
	{
		FMessageWindows.removeAt(FMessageWindows.indexOf(window));
		emit messageWindowDestroyed(window);
	}
}

void MessageWidgets::onChatWindowDestroyed()
{
	IChatWindow *window = qobject_cast<IChatWindow *>(sender());
	if (window)
	{
		FChatWindows.removeAt(FChatWindows.indexOf(window));
		emit chatWindowDestroyed(window);
	}
}

void MessageWidgets::onTabWindowPageAdded(ITabPage *APage)
{
	ITabWindow *window = qobject_cast<ITabWindow *>(sender());
	if (window)
	{
		if (window->windowId() != Options::node(OPV_MESSAGES_TABWINDOWS_DEFAULT).value().toString())
			FPageWindows.insert(APage->tabPageId(), window->windowId());
		else
			FPageWindows.remove(APage->tabPageId());
	}
}

void MessageWidgets::onTabWindowDestroyed()
{
	ITabWindow *window = qobject_cast<ITabWindow *>(sender());
	if (window)
	{
		FTabWindows.removeAt(FTabWindows.indexOf(window));
		emit tabWindowDestroyed(window);
	}
}

void MessageWidgets::onStreamJidAboutToBeChanged(IXmppStream *AXmppStream, const Jid &AAfter)
{
	if (!(AAfter && AXmppStream->streamJid()))
		deleteStreamWindows(AXmppStream->streamJid());
}

void MessageWidgets::onStreamRemoved(IXmppStream *AXmppStream)
{
	deleteStreamWindows(AXmppStream->streamJid());
}

void MessageWidgets::onOptionsOpened()
{
	if (tabWindowList().isEmpty())
		appendTabWindow(tr("Main Tab Window"));

	if (!tabWindowList().contains(Options::node(OPV_MESSAGES_TABWINDOWS_DEFAULT).value().toString()))
		Options::node(OPV_MESSAGES_TABWINDOWS_DEFAULT).setValue(tabWindowList().value(0).toString());

	QByteArray data = Options::fileValue("messages.tab-window-pages").toByteArray();
	QDataStream stream(data);
	stream >> FPageWindows;
}

void MessageWidgets::onOptionsClosed()
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << FPageWindows;
	Options::setFileValue(data,"messages.tab-window-pages");

	deleteWindows();
}

Q_EXPORT_PLUGIN2(plg_messagewidgets, MessageWidgets)
