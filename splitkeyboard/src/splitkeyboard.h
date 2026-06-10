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

#include <QtWidgets>
#include <QAbstractNativeEventFilter>

#include <src/settings.h>
#include "keyengine.h"

typedef QMap<QString, QStringList>   Page;
typedef QMap<int, Page>              Keymap;

extern const QString appID;

class SplitKeyboard : public QWidget, public QAbstractNativeEventFilter {
	Q_OBJECT

public:
	SplitKeyboard();
	~SplitKeyboard();

private:
	/* Load the persisted settings (Mode / WindowMode / WindowSize). */
    void loadSettings();

	/* Load KeyMap */
	void loadKeymap();

	/* Prepare the layout of the keyboard */
	void relayKeyboard();

	/* Apply `region` as the X11 input shape (ShapeInput) so the masked-out middle gap is
	 * click-/touch-through under rootless Xwayland, not just visually transparent. */
	void setInputShape(const QRegion &region);

	/* Compact or fixed mode */
    bool mMode;

    /* Show CK with or without window */
    bool mWindowMode;

	/* Font to be used for the keys */
	QFont mFont;

	/* Current page; start with 1 */
    int mPage = 1;

    /* Window size (floating/compact mode) */
    QSize windowSize;

	Keymap mKeyMap;

	/* The keysym to character data */
	QMap<int, QStringList> keyChart;

	/* Letter keycodes (used by the renderer to apply Caps to letters only) */
	QSet<int> letters;

	/* The modifier state machine (armed/momentary/Caps) + X11 event emission, decoupled
	 * from the widget so it can be unit-tested. Its sink is wired to XTestFakeKeyEvent in
	 * the constructor; the renderer reads engine.modPress/capsOn for highlighting. */
	KeyEngine engine;

	/* A map of the pressed keys (UI highlight; keyed by encoded key) */
	QMap<int, bool> keypress;

	QHash<int, int> touchKey;     /* touch-point id -> encoded key currently held */
	int mousePressedKey = -1;     /* encoded key held by the mouse, or -1 */

	/* Encoded key under a point, or -1 (skips invisible spacers) */
	int keyAt(const QPointF &p) const;

	/* Shared press / release logic for both the mouse and touch paths (thin wrappers over
	 * the engine that also track keypress[] and the Hide pseudo-key) */
	void pressKey(int key);
	void releaseKey(int key);

	/* Dispatch a multitouch event to pressKey/releaseKey, one point at a time */
	void handleTouch(QTouchEvent *tEvent);

	/* The rectangle for each key */
	QMultiMap<int, QRectF> layout;

	settings *smi;

	/* Global hotkey (Super+K) to toggle visibility. On KDE we register it with the
	 * KGlobalAccel daemon over D-Bus (registerKGlobalAccelHotkey) so it fires regardless
	 * of which window has focus -- crucially including native Wayland windows, where a raw
	 * XGrabKey is never delivered (under Xwayland the grab only reaches us while an X11
	 * window is focused, so otherwise the bare 'k' gets typed). Off KDE we fall back to an
	 * XGrabKey on the X11 root window (the path that also drives nativeEventFilter). */
	void registerGlobalHotkey();
	bool registerKGlobalAccelHotkey();   /* true if KDE's KGlobalAccel claimed the hotkey */
	int  mHotkeyKeycode = 0;     /* X11 keycode of the toggle key (0 = not grabbed / using KGlobalAccel) */
	uint mHotkeyMods    = 0;     /* required modifier mask (Super) */
	bool mHotkeyDown    = false; /* hotkey physically held? edge-detect to swallow auto-repeat */

    /* Toggle Comapct UI keyboard */
    void modeCompact();

    /* Toggle Fixed UI keyboard */
    void modeFixed();

	/* Un-float the bottom Plasma panel while the keyboard is shown so its top edge
	 * meets the keyboard's bottom (a floating panel overlaps the bottom key row);
	 * restore the floating default when hidden. Done via Plasma's scripting D-Bus;
	 * a harmless no-op on non-Plasma desktops. */
	void setPanelFloating(bool floating);

protected:
	/* Route touch events to handleTouch (and suppress synthesized mouse events) */
	bool event(QEvent *e) override;

	/* Catch the grabbed global hotkey from the raw X11/xcb event stream */
	bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

	/* Override the default mouse button press events */
    void mousePressEvent(QMouseEvent *mEvent) override;

	/* Override the default mouse button release events */
    void mouseReleaseEvent(QMouseEvent *mEvent) override;

	/* Relay the layout on keyboard resize */
    void resizeEvent(QResizeEvent *rEvent) override;

	/* Paint the keyboard */
    void paintEvent(QPaintEvent *pEvent) override;

    void changeEvent(QEvent *event) override;

    void closeEvent(QCloseEvent *cEvent) override;

	/* Un-float / re-float the bottom Plasma panel as the keyboard shows / hides */
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

public Q_SLOTS:
	/* Toggle between minimized to tray and visible states */
	void toggleShowHide();
};
