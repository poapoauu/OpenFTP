#pragma once
#include "stdafx.h"
#include "request_manager.h"

class SettingsManager : QObject
{
	Q_OBJECT
public:
	SettingsManager(QObject* parent);

	QString getDefaultBrowserDirectory();
	void setDefaultBrowserDirectory(QString directory);
	RequestManager::FileOverwrite getOverwriteExistingFileBehavior();
	void setOverwriteExistingFileBehavior(const int& selection);

private:
	QSettings* settings;
	const QString settingsDirectory = "./settings/client_settings.json";
	QSettings::Format jsonFormat = QSettings::registerFormat("json", SettingsManager::readJsonFile, SettingsManager::writeJsonFile);

	static bool readJsonFile(QIODevice& device, QSettings::SettingsMap& map);
	static bool writeJsonFile(QIODevice& device, const QSettings::SettingsMap& map);
};

