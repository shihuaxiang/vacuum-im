#include "streamparser.h"

#include <QMap>
#include <definitions/namespaces.h>
#include <utils/logger.h>

StreamParser::StreamParser(QObject *AParent) : QObject(AParent)
{
	restart();
}

StreamParser::~StreamParser()
{

}

void StreamParser::parseData(const QByteArray &AData)
{
	static QDomDocument doc;

	FReader.addData(AData);
	while (!FReader.atEnd())
	{
		FReader.readNext();
		if (FReader.isStartDocument())
		{
			FLevel = 0;
			FCurrentElem = QDomElement();
		}
		else if (FReader.isStartElement())
		{
			QMap<QStringRef, QStringRef> nsDeclarations;
			foreach(const QXmlStreamNamespaceDeclaration &nsDecl, FReader.namespaceDeclarations())
				nsDeclarations.insert(nsDecl.prefix(),nsDecl.namespaceUri());

			QDomElement newElement;
			if (nsDeclarations.contains(FReader.prefix()))
				newElement = doc.createElementNS(FReader.namespaceUri().toString(),FReader.qualifiedName().toString());
			else
				newElement = doc.createElement(FReader.qualifiedName().toString());

			foreach(const QXmlStreamAttribute &attribute, FReader.attributes())
			{
				QString attrNs = attribute.namespaceUri().toString();
				if (!attrNs.isEmpty())
					newElement.setAttributeNS(attrNs,attribute.qualifiedName().toString(),attribute.value().toString());
				else
					newElement.setAttribute(attribute.qualifiedName().toString(),attribute.value().toString());
			}

			for(QMap<QStringRef, QStringRef>::const_iterator it=nsDeclarations.constBegin(); it!=nsDeclarations.constEnd(); ++it)
			{
				if (it.key() != FReader.prefix())
				{
					QString prefix = it.key().toString();
					newElement.setAttribute(!prefix.isEmpty() ? prefix+QString(":xmlns") : QString("xmlns"), it->toString());
				}
			}

			FLevel++;
			if (FLevel == 1)
			{
				emit opened(newElement);
			}
			else if (FLevel == 2)
			{
				FRootElem = newElement;
				FCurrentElem = FRootElem;
			}
			else
			{
				FCurrentElem.appendChild(newElement);
				FCurrentElem = newElement;
			}

			FElemSpace = QDomText();
		}
		else if (FReader.isCharacters())
		{
			if (FReader.isCDATA())
				FCurrentElem.appendChild(doc.createCDATASection(FReader.text().toString()));
			else if (FReader.isWhitespace())
				FElemSpace = doc.createTextNode(FReader.text().toString());
			else
				FCurrentElem.appendChild(doc.createTextNode(FReader.text().toString()));
		}
		else if (FReader.isEndElement())
		{
			if (!FElemSpace.isNull() && !FCurrentElem.hasChildNodes())
				FCurrentElem.appendChild(FElemSpace);
			FElemSpace = QDomText();

			FLevel--;
			if (FLevel > 1)
				FCurrentElem = FCurrentElem.parentNode().toElement();
			else if (FLevel == 1)
				emit element(FRootElem);
			else if (FLevel == 0)
				emit closed();
		}
	}

	if (FReader.hasError() && FReader.error()!=QXmlStreamReader::PrematureEndOfDocumentError)
	{
		LOG_ERROR(QString("Failed to parse XML data: %1\n%2").arg(FReader.errorString(),QString::fromUtf8(AData)));
		emit error(XmppStreamError(XmppStreamError::EC_NOT_WELL_FORMED,FReader.errorString()));
	}
}

void StreamParser::restart()
{
	FReader.clear();
	FReader.setNamespaceProcessing(true);
}
