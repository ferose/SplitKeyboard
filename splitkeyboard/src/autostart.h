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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTextStream>

/* "Start on login" autostart, driven by a freedesktop XDG autostart .desktop file.
 *
 * The file MUST land in the host's real ~/.config/autostart. Inside the flatpak
 * XDG_CONFIG_HOME is redirected to the app's private dir, so we deliberately build the
 * path from QDir::homePath() (the physical $HOME/.config), NOT QStandardPaths -- the
 * manifest's --filesystem=xdg-config/autostart:create grant maps the host autostart dir
 * onto exactly that path. The file's presence is the single source of truth (no
 * settings.conf mirror, so the two can't drift). */
namespace autostart {

inline QString autostartDir()
{
	return QDir::homePath() + QStringLiteral("/.config/autostart");
}

inline QString desktopFilePath()
{
	return autostartDir() + QStringLiteral("/" APP_ID ".desktop");
}

inline bool isEnabled()
{
	return QFile::exists(desktopFilePath());
}

/* The command the desktop session runs at login. --hidden starts SplitKeyboard
 * minimized to the tray so login isn't interrupted by the keyboard popping up.
 * /.flatpak-info exists only inside the sandbox, so it distinguishes a flatpak install
 * (launch via `flatpak run`) from a bare host build (launch the binary directly). */
inline QString launchCommand()
{
	if (QFile::exists(QStringLiteral("/.flatpak-info")))
		return QStringLiteral("flatpak run " APP_ID " -platform xcb --hidden");
	return QCoreApplication::applicationFilePath() + QStringLiteral(" -platform xcb --hidden");
}

inline bool setEnabled(bool on)
{
	const QString path = desktopFilePath();

	if (!on)
		return !QFile::exists(path) || QFile::remove(path);

	if (!QDir().mkpath(autostartDir()))
		return false;

	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return false;

	QTextStream out(&f);
	out << "[Desktop Entry]\n"
	       "Type=Application\n"
	       "Name=SplitKeyboard\n"
	       "Comment=Split-layout on-screen keyboard\n"
	       "Exec=" << launchCommand() << "\n"
	       "Icon=" APP_ID "\n"
	       "Terminal=false\n"
	       "X-Flatpak=" APP_ID "\n"
	       "X-GNOME-Autostart-enabled=true\n";
	f.close();
	return f.error() == QFileDevice::NoError;
}

}  // namespace autostart
