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


#include <QDir>
#include <QStandardPaths>

#include "settings.h"

settings::settings()
{
    /* SplitKeyboard's own config, under XDG_CONFIG_HOME (GenericConfigLocation) rather than
     * a hard-coded ~/.config: outside the sandbox that's still ~/.config/SplitKeyboard, but
     * inside the flatpak it resolves to the app's own redirected config dir
     * (~/.var/app/online.ferose.SplitKeyboard/config/SplitKeyboard) -- so no host-filesystem
     * grant is needed. Deliberately NOT the shared CuboCore coreapps.conf: this app is
     * independent of that suite. */
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/SplitKeyboard");
    QDir().mkpath(dir);
    const QString settingsFile = dir + "/settings.conf";
    cSetting = new QSettings(settingsFile, QSettings::NativeFormat);

    setAppDefaultSettings();
}

settings::~settings()
{
    delete cSetting;
}

// Seed this app's defaults (the SplitKeyboard/* keys) if unset.
void settings::setAppDefaultSettings()
{
    if (not cSetting->contains("SplitKeyboard/Mode")) {
        cSetting->setValue("SplitKeyboard/Mode", true);
    }

    if (not cSetting->contains("SplitKeyboard/WindowMode")) {
        cSetting->setValue("SplitKeyboard/WindowMode", false);
    }

    if (!cSetting->contains("SplitKeyboard/WindowSize")) {
        cSetting->setValue("SplitKeyboard/WindowSize", QSize(400, 200));
    }

    cSetting->sync();
}

settings::cProxy settings::getValue(const QString &appName, const QString &key,
                                    const QVariant &defaultValue)
{
    return cProxy{ cSetting, appName + "/" + key, defaultValue };
}

void settings::setValue(const QString &appName, const QString &key, QVariant value)
{
    cSetting->setValue(appName + "/" + key, value);
}

QString settings::defaultSettingsFilePath() const
{
    return cSetting->fileName();
}
