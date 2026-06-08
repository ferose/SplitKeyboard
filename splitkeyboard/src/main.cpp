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

#include "splitkeyboard.h"
#include "trayicon.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDebug>

const QString appID = QStringLiteral(APP_ID);

/* Single-instance over a local socket in XDG_RUNTIME_DIR (shared across launches of the
 * same flatpak app). A second launch is a toggle gesture: it pokes the running instance
 * and exits. Replaces libcprime's CApplication -- this app has no upstream dependency. */
static const QString kInstanceKey = QStringLiteral(APP_ID ".instance");

/* True if another instance was already running (and was poked to toggle visibility).
 * Connecting is the whole signal -- the running instance toggles on newConnection. */
static bool pokeRunningInstance()
{
	QLocalSocket sock;
	sock.connectToServer(kInstanceKey);
	if (!sock.waitForConnected(300))
		return false;
	sock.disconnectFromServer();
	// Let the running instance's event loop process the toggle before we tear down.
	sock.waitForDisconnected(100);
	return true;
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);

	app.setOrganizationName("ferose.online");
	app.setApplicationName("SplitKeyboard");
	app.setApplicationVersion(QStringLiteral(VERSION_TEXT));
	app.setDesktopFileName(appID);
	app.setQuitOnLastWindowClosed(false);

	/* Single instance. Claim the socket atomically: listen()'s underlying bind wins
	   exactly once, so even simultaneous launches can't both become primary. If it fails,
	   either a live instance holds it -- poke it to toggle and exit (a relaunch is the
	   toggle gesture, and we bail *before* building a keyboard that would grab the hotkey
	   etc.) -- or the socket is stale from a hard-killed run, so clear it and claim it. */
	QLocalServer server;
	if (!server.listen(kInstanceKey))
	{
		if (pokeRunningInstance())
			return 0;
		QLocalServer::removeServer(kInstanceKey);   // stale socket; no live instance answered
		if (!server.listen(kInstanceKey))
			qWarning() << "SplitKeyboard: could not claim the single-instance socket;"
			           << "relaunch-to-toggle may not work.";
	}

	SplitKeyboard k;

	/* Each incoming connection from a relaunch toggles visibility. */
	QObject::connect(&server, &QLocalServer::newConnection, &k, [&server, &k]() {
		QLocalSocket *c = server.nextPendingConnection();
		if (c) { c->close(); c->deleteLater(); }
		k.toggleShowHide();
	});

	k.show();

	/* Start the tray icon */
	trayicon tray(&k);
	QObject::connect(&tray, &trayicon::toggleShowHide, &k, &SplitKeyboard::toggleShowHide);
	tray.show();

	return app.exec();
}
