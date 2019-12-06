#include "./headers/stdafx.h"
#include "./headers/model.h"


ClientModel::ClientModel(QWidget* parent) : QObject(parent), settingsManager(parent)
{
	QCoreApplication::setOrganizationName("kandabi");
	QCoreApplication::setOrganizationDomain("kandabi.com");
	QCoreApplication::setApplicationName("qtFtpServer");

	currentLocalDirectory = settingsManager.getDefaultBrowserDirectory();

	localBrowserModel = new QFileSystemModel(this);
	localBrowserModel->setFilter( QDir::NoDotDot | QDir::AllEntries);
	localBrowserModel->setRootPath(currentLocalDirectory);
}


void ClientModel::init()
{
	emit writeTextSignal("OpenFTP client 0.1.1, written by kandabi", Qt::darkGray);
	emit setFileBrowserSignal(*localBrowserModel);
}


void ClientModel::connectToServer()
{
	emit writeTextSignal("Attempting connection to Ftp server...");

	socket.close();
	socket.connectToHost(QHostAddress("127.0.0.1"), 20);
}


void ClientModel::disconnectFromServer()
{
	socket.close();
}


void ClientModel::onReadyRead()
{	
	QByteArray data = (downloadInProgress) ? socket.readAll() : parseByteData();
	
	if (data.isEmpty())
		return;

	if (downloadInProgress)
	{
		parseDownload(data);
	}
	else {
		parseJson(data);
	}
}

void ClientModel::parseJson(const QByteArray& data)
{
	//emit writeTextSignal("Packet size: " + QString::number(data.size()));

	bool isJson = RequestManager::checkIfDataIsJson(data);
	if (!isJson)
		return;
	//Q_ASSERT(isJson);

	QJsonParseError jsonError;
	QJsonArray jsonArray = QJsonDocument::fromJson(data, &jsonError).array();

	QJsonObject serverDetails = jsonArray.first().toObject();
	RequestManager::ResponseType responseType = static_cast<RequestManager::ResponseType>(serverDetails.value("response_status").toInt());

	if (responseType != RequestManager::ResponseType::DownloadFolder && responseType != RequestManager::ResponseType::BeginFileDownload && responseType != RequestManager::ResponseType::DownloadComplete)
		currentServerDirectory = serverDetails.value("directory").toString();

	switch (responseType)
	{
	case RequestManager::ResponseType::Connected:
		emit writeTextSignal("Succesfully connected to the FTP server, Address: " + socket.peerAddress().toString() +  "::" + QString::number(socket.peerPort()), Qt::darkGreen);
		break;
	case RequestManager::ResponseType::FolderCreated:
		emit writeTextSignal("New folder created in: " + currentServerDirectory);
		checkRemainingUploads();
		break;
	case RequestManager::ResponseType::FolderAlreadyExists:
		checkRemainingUploads();
		//emit writeTextSignal("Folder already exists in: " + currentServerDirectory, Qt::darkRed);
		//emit beepSignal();
		break;
	case RequestManager::ResponseType::DeletedFiles:
		emit deletedFilesSignal();
		break;
	case RequestManager::ResponseType::FileUploading:
		emit updateProgressBarSignal(serverDetails.value("bytesWritten").toInt());
		break;
	case RequestManager::ResponseType::FileAlreadyExists:
		emit fileAlreadyExistsSignal(QFileInfo(fileListToUpload.first()).fileName());
		break;
	case RequestManager::ResponseType::BeginFileUpload:
		uploadFileData();
		break;
	case RequestManager::ResponseType::UploadCompleted:
		emit updateProgressBarSignal(serverDetails.value("bytesWritten").toInt());
		emit writeTextSignal("Upload complete: " + currentUpload.fileName(), Qt::darkGreen);
		checkRemainingUploads();
		break;
	case RequestManager::ResponseType::BeginFileDownload:
		beginPendingDownload(currentDownload);
		break;
	case RequestManager::ResponseType::DownloadFolder:
	{
		QList<File> extraFiles = getFilesListFromJson(jsonArray);
		for (const File& file : extraFiles)
		{
			fileListToDownload.prepend(file);
		}

		checkRemainingDownloads();					
		break;
	}
	//case RequestManager::ResponseType::DownloadComplete:
	case RequestManager::ResponseType::Unauthorized:
		emit writeTextSignal("Unauthorized server access.", Qt::darkRed);
		return;
	case RequestManager::ResponseType::Closing:
		emit writeTextSignal("Server shutting down, disconnecting.", Qt::darkRed);
		return;
	case RequestManager::ResponseType::UnknownResponse:
		emit writeTextSignal("Unknown server response.", Qt::darkRed);
		return;
	}
	

	if (responseType != RequestManager::ResponseType::FileUploading && responseType != RequestManager::ResponseType::DownloadFolder && 
		responseType != RequestManager::ResponseType::BeginFileDownload && responseType != RequestManager::ResponseType::DownloadComplete)
	{
		serverFileList.clear();
		serverFileList = getFilesListFromJson(jsonArray);

		serverBrowserModel = new FileListServerModel(serverFileList, this);
		emit connectedToServerSignal(serverBrowserModel, currentServerDirectory);
	}
}


void ClientModel::parseDownload(const QByteArray& data)
{
	writtenBytes += qSaveFile.write(data);
	int fileSize = currentDownload.fileSize;
	emit updateProgressBarSignal(writtenBytes);

	if (writtenBytes >= fileSize)
	{
		writtenBytes = 0;
		qSaveFile.commit();
		checkRemainingDownloads();
	}
}

void ClientModel::checkRemainingDownloads()
{
	downloadInProgress = false;
	if (!fileListToDownload.isEmpty())
	{
		for (int index = 0; index < fileListToDownload.count(); ++index)
		{
			currentDownload = fileListToDownload[index];
			if (!currentDownload.isDir)
			{
				fileListToDownload.removeAt(index);
				break;
			}
		}

		if (currentDownload.isDir)
		{
			//QStringList array = currentDownload.filePath.split(currentServerDirectory).last().split('/');
			directoryToSave = currentLocalDirectory + currentDownload.filePath.split(currentServerDirectory).last();
			fileListToDownload.removeLast();
		}

		createDownloadRequest(currentDownload);

	}
	else {
		directoryToSave = "";
		emit uploadCompleteSignal();
	}
}


void ClientModel::checkRemainingUploads()
{
	if (!fileListToUpload.isEmpty())
	{
		bool isDir = true;
		
		for (int index = 0; index < fileListToUpload.count(); ++index)
		{
			currentUpload.setFile(fileListToUpload[index]);
			if (!currentUpload.isDir())
			{
				isDir = false;
				fileListToUpload.removeAt(index);
				break;
			}
		}

		if (isDir)
		{
			directoryToUpload = currentServerDirectory + currentUpload.filePath().split(currentLocalDirectory).last();
			fileListToUpload.removeLast();

			QDir directory(currentUpload.absoluteFilePath());
			QFileInfoList fileInfoList = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files);

			for (const QFileInfo& file : fileInfoList)
			{
				fileListToUpload.prepend(file.absoluteFilePath());

			}

		}

		uploadFileRequest(currentUpload, isDir);
	}
	else {
		directoryToUpload = "";
		emit uploadCompleteSignal();
	}
}



void ClientModel::uploadFileData()
{
	socket.write(dataToSend);
}

void ClientModel::uploadFileRequest(const QFileInfo& currentUpload, bool isDir)
{
	if (isDir) //*** Check if its a directory;
	{
		createFolderAction(directoryToUpload, true);
	}
	else {

		QFile qfile(currentUpload.absoluteFilePath());
		if (!qfile.open(QIODevice::ReadOnly))
		{
			emit uploadFailedSignal("Unable to load file, transfer failed. " + qfile.errorString());
			return;
		}

		QString fileSize = QString::number(qfile.size());
		emit writeTextSignal("Sending File: " + currentUpload.fileName() + " File Size: " + fileSize + " Directory to upload: " + directoryToUpload);
		emit setProgressBarSignal(qfile.size());

		QMap<QString, QString> requestVariables{
			{"requestPath", currentServerDirectory},
			{"uploadFileName",  currentUpload.fileName() },
			{"uploadFilePath", directoryToUpload},
			{"uploadFileSize", fileSize},
			{"uploadOverwriteExisting", QString::number(true)},
		};

		QJsonObject request = RequestManager::createServerRequest(RequestManager::RequestType::UploadFile, requestVariables);
		QByteArray data = Serializer::JsonObjectToByteArray(request);

		socket.write(data);
		socket.flush();

		QByteArray fileData = qfile.readAll();
		if (fileData.isEmpty())
			fileData = " ";
		dataToSend = fileData;
	}
}


QByteArray ClientModel::parseByteData()
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
		//Q_ASSERT(isJson);
	}
	else {
		previousReadyReadData = data;
		data = QByteArray();
	}
	
	return data;
}


void ClientModel::queueFilesToDownload(const QModelIndexList& indices, bool appendMorefiles)
{
	for (const QModelIndex& index : indices)
	{
		File file = serverFileList[index.row()];
		if (file.fileName == ".")
			continue;

		fileListToDownload.append(file);
	}

	writeTextSignal("Appended " + QString::number(fileListToDownload.count()) + " files to download.");

	if (!appendMorefiles)
	{
		directoryToSave = currentLocalDirectory;
		checkRemainingDownloads();
	}
}


void ClientModel::queueFilesToUpload(const QStringList& fileList, bool appendMorefiles)
{
	emit writeTextSignal(QString::number(fileList.count()) + " Files added to upload queue.");
	fileListToUpload += fileList;

	if (!appendMorefiles)
	{
		directoryToUpload = currentServerDirectory;
		checkRemainingUploads();
		return;
	}
}


//void ClientModel::parseFolderFiles()
//{
//	
//}


void ClientModel::createDownloadRequest(const File& file)
{
	QString fileName = file.fileName;
	bool isFolder = file.isDir;
	QString requestPath = FileManager::getPreviousFolderPath(file.filePath);
	if (isFolder)
	{
		createFolderAction(directoryToSave, false);
		requestPath = file.filePath;
	}
	
	QMap<QString, QString> requestVariables
	{
		{"requestPath", requestPath },
		{"downloadFileName", fileName},
		//{"isFolder", QString::number(isFolder)}
	};

	RequestManager::RequestType requestType = (isFolder) ? RequestManager::RequestType::DownloadFolder : RequestManager::RequestType::DownloadFile;
	QJsonObject response = RequestManager::createServerRequest(requestType, requestVariables);
	QByteArray data = Serializer::JsonObjectToByteArray(response);

	socket.write(data);
}


void ClientModel::beginPendingDownload(const File& currentDownload)
{
	QMap<QString, QString> requestVariables{
		{"requestPath", currentDownload.filePath}
	};

	QJsonObject request = RequestManager::createServerRequest(RequestManager::RequestType::NextPendingDownload, requestVariables);
	QByteArray data = Serializer::JsonObjectToByteArray(request);

	downloadInProgress = true;
	emit setProgressBarSignal(currentDownload.fileSize);
	qSaveFile.setFileName(directoryToSave + "/" + currentDownload.fileName);
	bool open = qSaveFile.open(QIODevice::WriteOnly);

	socket.write(data);
}


void ClientModel::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::ClosingState)
	{
		emit writeTextSignal("Disconnected from the server.", Qt::darkRed);
		socket.close();
		serverFileList.clear();
		emit disconnectedFromServerSignal();
	}
	else if (socketState == QAbstractSocket::ConnectedState)
	{
		QJsonObject request
		{
			{ "username", "aviv" },
			{ "password", "1234"},
		};

		socket.write(Serializer::JsonObjectToByteArray(request));
	}
}

void ClientModel::onDoubleClickLocalBrowser(const QModelIndex& index)
{	
	bool result = localBrowserModel->isDir(index);
	if (result)
	{
		QString fileName = localBrowserModel->fileName(index);
		QString filePath = localBrowserModel->filePath(index);

		if (checkIfSensitiveDirectory(filePath))
			return;

		else if (fileName == ".")
			filePath = FileManager::getPreviousFolderPath(filePath);
		
		localBrowserModel->setRootPath(filePath);
		currentLocalDirectory = filePath;
		settingsManager.setDefaultBrowserDirectory(currentLocalDirectory);
		emit setFileBrowserSignal(*localBrowserModel);
	}
}


void ClientModel::onDoubleClickServerBrowser(const QModelIndex& index)
{
	int rowSelected = index.row();
	if (!serverFileList[rowSelected].isDir)
		return;

	QString pathToRequest = (serverFileList[rowSelected].fileName == ".") ? FileManager::getPreviousFolderPath(serverFileList[rowSelected].filePath) : serverFileList[rowSelected].filePath;
	
	QMap<QString, QString> requestVariables{
		{"requestPath", pathToRequest}
	};
	
	QJsonObject request = RequestManager::createServerRequest(RequestManager::RequestType::Browse, requestVariables);
	QByteArray data = Serializer::JsonObjectToByteArray(request);

	socket.write(data);
}


void ClientModel::deleteAction(const QModelIndexList& indices, bool deleteInServer)
{
	if (!deleteInServer)
	{
		for (int i = 0; i < indices.count(); ++i)
		{
			QString fileName = localBrowserModel->fileName(indices[i]);
			QString filePath = localBrowserModel->filePath(indices[i]);

			if (fileName == "." || checkIfSensitiveDirectory(filePath))
				continue;

			else {
				QDir dir(filePath);
				dir.remove(filePath);
				dir.removeRecursively();

				emit deletedFilesSignal();
			}
		}
	}
	else if(serverFileList[indices[0].row()].fileName != "." && !serverFileList[indices[0].row()].fileName.endsWith(":/Windows"))
	{
		QStringList deletePaths;
		//QString pathToRequest = FileManager::getPreviousFolderPath(fileList[indices[0].row()].filePath);
		for (int i = 0; i < indices.count(); ++i)
		{
			deletePaths.append(serverFileList[indices[i].row()].filePath);
		}

		QMap<QString, QString> requestVariables{
			{"requestPath", currentServerDirectory}
		};

		QJsonObject request = RequestManager::createServerRequest(RequestManager::RequestType::Remove, requestVariables, deletePaths);
		QByteArray data = Serializer::JsonObjectToByteArray(request);

		socket.write(data);
	}
}


void ClientModel::renameFile(const QModelIndex& index, const QString& newFileName)
{
	int rowSelected = index.row();
	QString fileName = serverFileList[rowSelected].fileName;

	if (fileName == "." || checkIfSensitiveDirectory(fileName))
		return;

	QMap<QString, QString> requestVariables{
		{"requestPath", currentServerDirectory},
		{"renameFile", fileName},
		{"changedFileName",  newFileName},
	};

	QJsonObject response = RequestManager::createServerRequest(RequestManager::RequestType::Rename, requestVariables);
	QByteArray data = Serializer::JsonObjectToByteArray(response);

	socket.write(data);
}

void ClientModel::createFolderAction(const QString& newFolderPath, bool createInServer)
{
	if (createInServer)
	{
		QMap<QString, QString> requestVariables{
			{"requestPath", currentServerDirectory},
			{"createFolderPath" , newFolderPath}
		};

		QJsonObject response = RequestManager::createServerRequest(RequestManager::RequestType::CreateFolder, requestVariables);
		QByteArray data = Serializer::JsonObjectToByteArray(response);

		socket.write(data);
	}
	else 
	{
		QDir dir;
		dir.mkpath(newFolderPath);
		emit setFileBrowserSignal(*localBrowserModel);
	}


}

void ClientModel::searchFolder(const QString& directory, bool searchInServer)
{
	if (searchInServer)
	{
		QMap<QString, QString> requestVariables{
			{"requestPath", directory}
		};

		QJsonObject response = RequestManager::createServerRequest(RequestManager::RequestType::Browse, requestVariables);
		QByteArray data = Serializer::JsonObjectToByteArray(response);

		socket.write(data);
	}
	else {
		QDir pathDir(directory);
		if (pathDir.exists())
		{
			if (checkIfSensitiveDirectory(directory))
				return;

			currentLocalDirectory = directory;
			settingsManager.setDefaultBrowserDirectory(currentLocalDirectory);
			localBrowserModel->setRootPath(currentLocalDirectory);
			emit setFileBrowserSignal(*localBrowserModel);
		}
	}

	//emit writeTextSignal("searching: " + directory + " in server: " + QString::number(searchInServer));
}

bool ClientModel::checkIfSensitiveDirectory(const QString& directory)
{
	QDir pathDir(directory);
	if (pathDir.path().contains(":/Windows"))
	{
		emit beepSignal();
		writeTextSignal("Please stay away from sensitive directories :)", Qt::darkRed);
		return true;
	}
	else
		return false;

}



void ClientModel::localBrowseHome()
{
	currentLocalDirectory = "";
	settingsManager.setDefaultBrowserDirectory(currentLocalDirectory);
	localBrowserModel->setRootPath(currentLocalDirectory);
	emit setFileBrowserSignal(*localBrowserModel);
}


void ClientModel::copyFilesToDirectory(const QStringList& files, bool lastFunction, const QString& directoryTocopy)
{
	for (const QString& file : files)
	{ //*** Check if directory
		if (QFileInfo(file).isDir())
		{
			QDir directory(file);
			QString newDirectoryTocopy = (directoryTocopy.isEmpty()) ? currentLocalDirectory + "/" + directory.dirName() : directoryTocopy + "/" + directory.dirName();
			
			directory.mkdir(newDirectoryTocopy);
			QStringList extraFiles;
			QFileInfoList subdirectoryFiles = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files);
			if(!subdirectoryFiles.isEmpty())
				for (int i = 0; i < subdirectoryFiles.count(); ++i)
				{
					extraFiles.append(subdirectoryFiles[i].absoluteFilePath());
				}
				copyFilesToDirectory(extraFiles, false , newDirectoryTocopy);
		}
		else {

			QString directory = (directoryTocopy.isEmpty()) ? currentLocalDirectory : directoryTocopy;
			QFile::copy(file, directory + "/" + QFileInfo(file).fileName());
		}
	}

	if(lastFunction)
		emit uploadCompleteSignal();
}

void ClientModel::browseHome()
{
	QMap<QString, QString> requestVariables{ 
		{"requestPath", "./"} 
	};

	QJsonObject response = RequestManager::createServerRequest(RequestManager::RequestType::Browse, requestVariables);
	QByteArray data = Serializer::JsonObjectToByteArray(response);

	socket.write(data);
}

void ClientModel::localReturnToLastFolder()
{
	currentLocalDirectory = FileManager::getPreviousFolderPath(currentLocalDirectory);
	settingsManager.setDefaultBrowserDirectory(currentLocalDirectory);
	localBrowserModel->setRootPath(currentLocalDirectory);
	emit setFileBrowserSignal(*localBrowserModel);
}


QList<File> ClientModel::getFilesListFromJson(const QJsonArray& jsonArray)
{
	QList<File> files;
	for (int i = 1; i < jsonArray.count(); ++i)
	{
		QJsonObject json = jsonArray[i].toObject();
		files.append(
			File{
				json.value("fileName").toString(),
				json.value("filePath").toString(),
				json.value("fileSize").toInt(),
				json.value("isDir").toBool(),
				json.value("lastModified").toString(),
				Serializer::decodePixmapFromString(json.value("icon").toString())
			}
		);
	}

	return files;
}


void ClientModel::returnToLastFolder()
{
	if (!serverFileList.isEmpty())
	{

		QMap<QString, QString> requestVariables{
			{"requestPath", FileManager::getPreviousFolderPath(serverFileList[0].filePath) }
		};

		QJsonObject response = RequestManager::createServerRequest(RequestManager::RequestType::Browse, requestVariables);
		QByteArray data = Serializer::JsonObjectToByteArray(response);

		socket.write(data);
	}
}

