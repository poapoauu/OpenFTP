#include "stdafx.h"
#include "network_manager.h"

NetworkManager::NetworkManager(QWidget* parent) : QObject(parent)
{
	//bool result = socket.addCaCertificates("sslserver.pem");
	//socket.setReadBufferSize(100);
}


void NetworkManager::disconnectFromServer()
{
	socket.close();
	downloadInProgress = false;
}


void NetworkManager::connectToServer(const QString& serverAddress, const QString& serverPort, const QString& userName, const QString& userPassword)
{
	emit writeTextSignal("Attempting connection to Ftp server at: " + serverAddress + "::" + serverPort);

	username = userName;
	password = userPassword;

	socket.close();
	//socket.connectToHostEncrypted(serverAddress, serverPort.toInt());
	socket.connectToHost(QHostAddress(serverAddress), serverPort.toInt());
}


void NetworkManager::writeData(const QByteArray& data)
{
	socket.write(data);
}


void NetworkManager::flushSocket()
{
	socket.flush();
}


QByteArray NetworkManager::readAll()
{
	return socket.readAll();
}


void NetworkManager::onReadyRead()
{
	QByteArray data = (downloadInProgress) ? socket.readAll() : parseByteData();

 	if (data.isEmpty())
		return;

	if (downloadInProgress)
	{
		parseByteDownload(data);
	}
	else {
		emit parseJsonSignal(data);
	}
}


void NetworkManager::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::ClosingState)
	{
		emit writeTextSignal("Disconnected from the server.", Qt::red);
		socket.close();
		emit disconnectedFromServerSignal();
	}
	else if (socketState == QAbstractSocket::ConnectedState)
	{
		QJsonObject request
		{
			{ "username", username },
			{ "password", password },
		};

		socket.write(Serializer::JsonObjectToByteArray(request));
		//if(socket.waitForEncrypted(-1))
		//else {
		//	emit writeTextSignal("Error establishing secure connection: " + socket.errorString(), Qt::red);
		//}
	}
}

void NetworkManager::parseByteDownload(const QByteArray& data)
{
	writtenBytes += qSaveFile.write(data);
	if (packetsRecieved % 10 == 0)
	{
		emit updateProgressBarSignal(writtenBytes, currentDownloadFileSize);
	}

	packetsRecieved++;

	if (writtenBytes >= currentDownloadFileSize)
	{
		packetsRecieved = 0;
		writtenBytes = 0;
		qSaveFile.commit();
		emit writeTextSignal("Download complete: " + qSaveFile.fileName(), Qt::darkGreen);
		emit checkRemainingDownloadsSignal();
	}
}


QByteArray NetworkManager::parseByteData()
{
	QByteArray data = previousReadyReadData;
	data += socket.readAll();

	bool isJson = RequestManager::checkIfDataIsJson(data);
	if (isJson)
	{
		previousReadyReadData.clear();
	}
	else if (data.contains('[') && data.contains(']'))
	{
		QByteArrayList splitDataArray = data.split(']');
		data = splitDataArray.at(splitDataArray.count() - 2);
		data.append(']');

		bool isJson = RequestManager::checkIfDataIsJson(data);
	}
	else {
		previousReadyReadData = data;
		data = QByteArray();
	}

	return data;
}


void NetworkManager::beginPendingDownload(const File& currentDownload, const QString& directoryToSave)
{
	currentDownloadFileSize = currentDownload.fileSize;

	emit setProgressBarSignal();
	qSaveFile.setFileName(directoryToSave + "/" + currentDownload.fileName);
	bool open = qSaveFile.open(QIODevice::WriteOnly);
}


void NetworkManager::continueLargeUpload()
{
	quint64 readFromPosition = packetSize * packetsSent++;

	qFile.seek(readFromPosition);
	QByteArray fileData = qFile.read(packetSize);
	writeData(fileData);
}



void NetworkManager::setUploadDataToSend(const QByteArray& data)
{
	if (data.isEmpty())
		dataToSend = " ";
	else
		dataToSend = data;
}

void NetworkManager::uploadFileData()
{
	if (!dataToSend.isEmpty())
		socket.write(dataToSend);
}


void NetworkManager::setdownloadInProgress(const bool& download)
{
	downloadInProgress = download;
}


QString NetworkManager::getSocketPort()
{
	return QString::number(socket.peerPort());
}


QString NetworkManager::getSocketAddress()
{
	return socket.peerAddress().toString();
}
