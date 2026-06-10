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

#include <QWidget>
#include <QMenu>
#include <QCoreApplication>
#include <QSystemTrayIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QPixmap>

#include "splitkeyboard.h"
#include "autostart.h"

class trayicon : public QSystemTrayIcon {
	Q_OBJECT

public:
	/* Build a crisp multi-resolution icon from an SVG: render it antialiased at each
	 * standard tray size so the panel/HiDPI always gets a native-size bitmap instead of
	 * scaling one rendered pixmap (which is what made the edges look soft). */
	static QIcon crispSvgIcon(const QString &path)
	{
		QSvgRenderer renderer(path);
		QIcon icon;
		for (int s : {16, 22, 24, 32, 44, 48, 64})
		{
			QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
			img.fill(Qt::transparent);
			QPainter p(&img);
			p.setRenderHint(QPainter::Antialiasing, true);
			p.setRenderHint(QPainter::SmoothPixmapTransform, true);
			renderer.render(&p);
			p.end();
			icon.addPixmap(QPixmap::fromImage(img));
		}
		return icon;
	}

	trayicon(QWidget *parent) : QSystemTrayIcon(parent)
	{
        setIcon(crispSvgIcon(":/resources/tray.svg"));
		show();

		connect(this, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(activationHandler(QSystemTrayIcon::ActivationReason)));

		// Parent to the widget: QSystemTrayIcon::setContextMenu does not take ownership.
		QMenu *menu = new QMenu("TrayMenu", parent);

        /* "\tSuper+K" renders as a right-aligned shortcut hint; it's informational only --
         * the real toggle is a global KGlobalAccel/XGrabKey binding, not a Qt action shortcut. */
        menu->addAction(QIcon(), "&Toggle Visible\tSuper+K", this, &trayicon::toggleShowHide);

        /* "Start on login": writes/removes a host XDG autostart .desktop (see autostart.h).
         * The .desktop's presence is the source of truth, so the check state is read from it
         * here and re-synced after each toggle in case the write failed. */
        QAction *autostartAction = menu->addAction("&Start on login");
        autostartAction->setCheckable(true);
        autostartAction->setChecked(autostart::isEnabled());
        connect(autostartAction, &QAction::triggered, this, [autostartAction](bool on) {
            autostart::setEnabled(on);
            autostartAction->setChecked(autostart::isEnabled());
        });

		menu->addSeparator();
		menu->addAction(QIcon::fromTheme("application-quit"), "&Quit SplitKeyboard", QCoreApplication::instance(), SLOT(quit()));

		setContextMenu(menu);
	}

private Q_SLOTS:
	void activationHandler(QSystemTrayIcon::ActivationReason reason)
	{
		if (reason == QSystemTrayIcon::Trigger)
			emit toggleShowHide();
	}

Q_SIGNALS:
	void toggleShowHide();
};
