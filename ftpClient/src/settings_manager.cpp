#include "./headers/stdafx.h"
#include "./headers/settings_manager.h"

SettingsManager::SettingsManager(QObject* parent) : QObject(parent)
{
	settings = new QSettings(settingsDirectory, jsonFormat);
}

QString SettingsManager::getDefaultBrowserDirectory()
{
	return settings->value("defaultLocalDirectory").toString();
}


void SettingsManager::setDefaultBrowserDirectory(QString directory)
{
	 settings->setValue("defaultLocalDirectory", directory);
}

bool SettingsManager::readJsonFile(QIODevice& device, QSettings::SettingsMap& map)
{
	QJsonDocument json = QJsonDocument::fromJson(device.readAll());
	map = json.object().toVariantMap();
	return true;
}

bool SettingsManager::writeJsonFile(QIODevice& device, const QSettings::SettingsMap& map)
{
	device.write(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson());
	return true;
}