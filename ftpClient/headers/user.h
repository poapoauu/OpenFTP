#pragma once
#include "stdafx.h"

class User
{

public:
	User();
	User(QString _username, QString _password);

	QString username;
	QString password;

};