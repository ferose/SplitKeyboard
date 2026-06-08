/*
 * This file is part of SplitKeyboard (a fork of CoreKeyboard).
 * An on-screen keyboard for X11.
 * Copyright 2019 CuboCore Group
 * Fork modifications Copyright 2026 Ferose
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see {http://www.gnu.org/licenses/}.
 */

#pragma once

#include <QFont>
#include <QRect>
#include <QSize>
#include <QSettings>


class settings {
public:
	explicit settings();
	~settings();

	struct cProxy
	{
		QSettings *cSetting;
		QString   data;
		QVariant  defaultValue;

		operator int() const {
			return cSetting->value(data, defaultValue).toInt();
		}

		operator bool() const {
			return cSetting->value(data, defaultValue).toBool();
		}

		operator QString() const {
			return cSetting->value(data, defaultValue).toString();
		}

		operator QStringList() const {
			return cSetting->value(data, defaultValue).value<QStringList>();
		}

		operator QSize() const {
			return cSetting->value(data, defaultValue).toSize();
		}

		operator QFont() const {
			return cSetting->value(data, defaultValue).value<QFont>();
		}

		operator QRect() const {
			return cSetting->value(data, defaultValue).toRect();
		}
	};

	settings::cProxy getValue(const QString& appName, const QString& key, const QVariant& defaultValue = QVariant());
	QString defaultSettingsFilePath() const;
    void setValue(const QString &appName, const QString &key, QVariant value);

private:
	QSettings *cSetting;
	QString defaultSett;

    void setAppDefaultSettings();
};
