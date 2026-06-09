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

/*
  * Modified for "SplitKeyboard" (online.ferose.SplitKeyboard), 2026 — a split-layout
  * fork of CoreKeyboard. Changes in this file: lowercase boot init (caps-off); merge of
  * adjacent identical keys into one wide key; hybrid modifiers (quick tap = armed
  * one-shot, hold-while-typing = momentary) plus a classic Caps Lock toggle; multitouch
  * input (two thumbs at once / key rollover, via QTouchEvent); auto-shrink of oversized
  * key labels; and a masked see-through/click-through middle gap. The layout is a single
  * 7-row page (no page toggle); the physical Shift does all shifting.
  *
  * Input flows through three shared helpers -- keyAt() (hit-test) and pressKey() /
  * releaseKey() (the actual XTest + modifier state machine) -- driven by both the mouse
  * handlers (single pointer) and handleTouch() (multitouch).
  *
  * (This implements upstream's old "modifier keys are not supported" TODO: tapping a
  * modifier arms it for one key, tapping again disarms it, or physically hold it with one
  * thumb while the other types. Caps is a plain lock toggled on each tap.)
  */

#include "splitkeyboard.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSvgRenderer>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>

/* XGrabKey reports a conflicting grab asynchronously as a BadAccess error; swallow just
 * that one so a hotkey already taken by another app degrades to a warning instead of
 * aborting Qt's default X error handler. */
static int (*sPrevXErrorHandler)(Display *, XErrorEvent *) = nullptr;
static bool sHotkeyGrabFailed = false;
static int hotkeyGrabErrorHandler(Display *dpy, XErrorEvent *ev)
{
	if (ev->error_code == BadAccess) { sHotkeyGrabFailed = true; return 0; }
	return sPrevXErrorHandler ? sPrevXErrorHandler(dpy, ev) : 0;
}

/* The lock-modifier combos to also grab under, so Caps/Num Lock don't swallow the hotkey. */
static const unsigned int kHotkeyLockMasks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };

/* --- Visual tuning (SplitKeyboard) ---------------------------------------------------
 * Gaps stay uniform as long as the layout/mask pad equals kKeyInset: the gap between keys
 * is two insets (2*kKeyInset) and the margin around the panel is the layout inset plus the
 * mask pad (also 2*kKeyInset). Glyphs are sized as a fraction of the key, then auto-shrunk
 * to fit. The whole panel is drawn semi-transparent. */
static constexpr int   kKeyInset      = 2;      // per-key inset (half the inter-key gap)
static constexpr int   kKeyInnerInset = 4;      // inset for the corner alt-label legend
static constexpr int   kLabelInsetX   = 12;     // text padding inside the key, horizontal
static constexpr int   kLabelInsetY   = 10;     // text padding inside the key, vertical
static constexpr qreal kFontScale     = 0.34;   // label point size / min(keyW, keyH)
static constexpr qreal kAltLabelScale = 0.55;   // shifted-alt legend size / main label
static constexpr qreal kAltLabelAlpha = 0.45;   // shifted-alt legend opacity (faint)
static constexpr qreal kIconScale     = 0.42;   // icon size / min(cellW, cellH)
static constexpr qreal kPanelOpacity   = 0.75;  // whole-panel ghosting (1.0 = opaque)

SplitKeyboard::SplitKeyboard() : QWidget()
	, smi(new settings)
{
    loadSettings();
	loadKeymap();
	relayKeyboard();

	/* Route the engine's synthesized key events to X11. Guard the platform interface:
	   it is null when the app wasn't started on xcb (e.g. a Wayland session launched
	   without -platform xcb), and dereferencing it would crash on startup. */
	if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>())
	{
		Display *display = x11->display();
		engine.setSink([display](int keycode, bool down) {
			XTestFakeKeyEvent(display, keycode, down, 0);
		});
	}
	else
	{
		qWarning() << "SplitKeyboard: not running on X11 (xcb); key injection disabled. Launch with -platform xcb.";
	}

	/* Global show/hide hotkey (Super+Ctrl+K), grabbed on the X11 root window. */
	registerGlobalHotkey();
	qApp->installNativeEventFilter(this);

    mFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

    if (not mFont.family().length())
    {
        mFont = QFont("Cantarell", 9);
    }

    setFont(mFont);

    setAttribute(Qt::WA_ShowWithoutActivating);

    /* Multitouch: let the widget receive QTouchEvents so two thumbs register at once
     * (e.g. hold a modifier while typing, two-key rollover). Delivery to our
     * override-redirect dock window is validated on-device (touches do arrive); the
     * WM-managed WindowMode is the fallback if a future setup doesn't deliver them. */
    setAttribute(Qt::WA_AcceptTouchEvents, true);

    /* SplitKeyboard is always ghosted (semi-transparent) so the desktop / the app being
       typed into stays visible through the panel. Upstream gated this on the OpaqueMode
       setting (shared with the stock app); here it's unconditional for this fork. */
    setWindowOpacity(kPanelOpacity);

    if(not mWindowMode){
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::BypassWindowManagerHint);
        setAttribute(Qt::WA_X11NetWmWindowTypeDock, true);
        setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
        setMouseTracking(true);
    } else{
        setWindowFlags(Qt::WindowStaysOnTopHint);
    }

    if (mMode){
        modeFixed();
    } else{
        modeCompact();
    }

	// For screen rotation
    QScreen *scrn = qApp->primaryScreen();

    connect(scrn, &QScreen::availableGeometryChanged, [this]() {
        // Re-anchor on rotation/resolution change through the same path as initial layout
        // (which also excludes the panel struts and lifts clear of a floating panel).
        if (mMode)
            modeFixed();
        else
            modeCompact();
    });
}


SplitKeyboard::~SplitKeyboard()
{
	if (mHotkeyKeycode)
	{
		auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
		if (Display *display = x11 ? x11->display() : nullptr)
		{
			Window root = DefaultRootWindow(display);
			for (unsigned int lock : kHotkeyLockMasks)
				XUngrabKey(display, mHotkeyKeycode, mHotkeyMods | lock, root);
		}
	}
	qApp->removeNativeEventFilter(this);
	delete smi;
}

void SplitKeyboard::registerGlobalHotkey()
{
	// Guard the platform interface: it's null when not running on xcb (e.g. a Wayland
	// session launched without -platform xcb), and dereferencing it would crash on startup.
	auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
	if (!x11)
		return;
	Display *display = x11->display();
	if (!display)
		return;

	mHotkeyKeycode = XKeysymToKeycode(display, XK_k);
	/* Super+Ctrl (avoid Super+Alt+K -- that's KDE's default "Switch Keyboard Layout").
	 * KNOWN ISSUE: toggling the keyboard while the KDE start menu (Kickoff) is open
	 * closes the menu. We isolated it to the XGrabKey *grab firing* (show(), the dock
	 * map, and the panel-float toggle were each ruled out -- the menu survives those;
	 * and it survives the keypress when the grab is absent). The exact mechanism is
	 * NOT confirmed -- candidates are the active grab's FocusOut vs. KWin reacting to
	 * the Super key -- and the combo choice doesn't fix it. Likely fix: detect the
	 * hotkey without grabbing (e.g. XInput2 monitoring); left as follow-up. */
	mHotkeyMods    = Mod4Mask | ControlMask;      /* Super+Ctrl */
	if (mHotkeyKeycode == 0)
		return;

	Window root = DefaultRootWindow(display);
	sHotkeyGrabFailed  = false;
	sPrevXErrorHandler = XSetErrorHandler(hotkeyGrabErrorHandler);
	for (unsigned int lock : kHotkeyLockMasks)
		XGrabKey(display, mHotkeyKeycode, mHotkeyMods | lock, root, True,
		         GrabModeAsync, GrabModeAsync);
	XSync(display, False);
	XSetErrorHandler(sPrevXErrorHandler);

	if (sHotkeyGrabFailed)
		qWarning() << "SplitKeyboard: Super+Ctrl+K is already grabbed by another app; toggle hotkey disabled.";
}

bool SplitKeyboard::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
	if (mHotkeyKeycode == 0 || eventType != "xcb_generic_event_t")
		return false;

	auto *generic = static_cast<xcb_generic_event_t *>(message);
	if ((generic->response_type & ~0x80) != XCB_KEY_PRESS)
		return false;

	auto *key = reinterpret_cast<xcb_key_press_event_t *>(generic);
	/* Match the keycode and exactly Super, ignoring lock modifiers (Caps/Num/Scroll). */
	const unsigned int relevant = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;
	if (key->detail == mHotkeyKeycode && (key->state & relevant) == mHotkeyMods)
	{
		toggleShowHide();
		return true;
	}
	return false;
}


void SplitKeyboard::loadSettings()
{
    mMode = smi->getValue("SplitKeyboard", "Mode");
    mWindowMode = smi->getValue("SplitKeyboard", "WindowMode");
    windowSize = smi->getValue("SplitKeyboard", "WindowSize");
}


void SplitKeyboard::loadKeymap()
{
	/* SplitKeyboard ships a single layout: the en_US split keymap. (Upstream's other
	 * locales and the mobile variants were removed.) */
	QString keymapName = ":/resources/en_US.keymap";

	QSettings keymapSett(keymapName, QSettings::IniFormat);

	/* All the KeySym to char data ins under the section Data */
	keymapSett.beginGroup("Data");

	/* Load the data to memory */
    Q_FOREACH (QString key, keymapSett.allKeys())
	{
        keyChart[key.toInt()] = keymapSett.value(key).toStringList();
	}

	/* Close the group */
	keymapSett.endGroup();

	Q_FOREACH (QString page, keymapSett.value("Pages").toStringList())
	{
		Page pg;
		Q_FOREACH (QString row, keymapSett.value("Rows").toStringList())
		{
			pg[row] = keymapSett.value(page + "/" + row).toStringList();
		}

		mKeyMap[page.toInt()] = pg;
	}

	QStringList modKeys = keymapSett.value("Modifiers").toStringList();

	Q_FOREACH (QString key, modKeys)
	{
		engine.modifiers.insert(key.toInt());
	}

	QStringList letterKeys = keymapSett.value("Letters").toStringList();

	Q_FOREACH (QString key, letterKeys)
	{
		letters << key.toInt();
	}

	engine.caps = keymapSett.value("Caps").toInt();
	engine.ctrl = keymapSett.value("Ctrl").toInt();
	engine.shift = keymapSett.value("Shift").toInt();
}


void SplitKeyboard::relayKeyboard()
{
	layout.clear();
	keypress.clear();

	int cols = 0;

	Q_FOREACH (QString row, mKeyMap[mPage].keys())
	{
		cols = (mKeyMap[mPage][row].count() > cols ? mKeyMap[mPage][row].count() : cols);
	}

	/* Bail if the keymap didn't load (no rows / no columns). Otherwise the width/height
	   math below divides by `cols` (and the row count) == 0 and produces inf geometry. */
	if (cols == 0)
		return;

	/* Uniform gap: inset the whole key block by `pad` from the window edges so the mask
	   can pad outward by the same amount (see the mask section below). Without it the keys
	   tile flush to the window, so the edge margin would be one per-key inset while the gap
	   between keys is two; the inset + mask-pad makes them equal. `pad` tracks kKeyInset so
	   the gaps stay uniform. */
	const qreal pad = kKeyInset;

	/* cols is the maximum number of columns */
	qreal keywidth = (width() - 2 * pad) / cols;

	/* mKeyMap[ mPage ].keys() list the rows */
	qreal keyheight = (height() - 2 * pad) / mKeyMap[mPage].keys().count();

	/* List of the rows of this page */
	QStringList rows = mKeyMap[mPage].keys();

	for (int row = 0; row < rows.count(); row++)
	{
		/* List of all the keys of this row */
		QStringList keys = mKeyMap[mPage][rows.at(row)];

		QList<int> fatKeys;
		Q_FOREACH (QString key, keys)
		{
			if ((key.toInt() > 1000) and (key.toInt() < 2000))
			{
				fatKeys << key.toInt();
			}
		}

		qreal extraSpace = 0, fatSpace = 0;
		if (fatKeys.count())
		{
			extraSpace = 0;
			fatSpace   = (width() - keys.count() * keywidth) / fatKeys.count();
		}

		else
		{
			/* Some rows have lesser number of keys, give a space at the beginning and end */
			extraSpace = (width() - keys.count() * keywidth) / 2;
			fatSpace   = 0;
		}

		int prevKey = -1;
		QRectF prevRect;
		for (int x = 0; x < keys.count(); x++)
		{
			qreal xPos   = extraSpace + x * keywidth;
			qreal yPos   = pad + row * keyheight;
			qreal kwidth = keywidth;
			if (fatKeys.contains(keys[x].toInt()))
			{
				kwidth     += fatSpace;
				extraSpace += fatSpace;
			}
			int curKey = keys[x].toInt();
			/* Merge horizontally-adjacent identical keys into one wide key (e.g. space bar) */
			if (curKey != 0 && curKey == prevKey)
			{
				layout.remove(prevKey, prevRect);
				prevRect.setWidth(prevRect.width() + kwidth);
				layout.insert(prevKey, prevRect);
			}
			else
			{
				prevRect = QRectF(QPointF(xPos, yPos), QSizeF(kwidth, keyheight));
				layout.insert(curKey, prevRect);
				prevKey = curKey;
			}
		}
	}

	/* Scale the font size */
	qreal maxPtSize = qMin(keywidth, keyheight) * kFontScale;

    mFont.setPointSizeF(maxPtSize);
    setFont(mFont);

	/* SplitKeyboard: carve the empty middle into a hole so the desktop shows
	   through and stays clickable. Mask the window to the bounding box of each key
	   cluster, derived from the actual key rects (left-half vs right-half), so the
	   spacer gap between them becomes a hole regardless of the column count. */
	QRectF leftBox, rightBox;
	for (auto it = layout.constBegin(); it != layout.constEnd(); ++it)
	{
		if (it.key() == 0)                       // skip the invisible spacers
			continue;
		QRectF &box = (it.value().center().x() < width() / 2.0) ? leftBox : rightBox;
		box = box.isNull() ? it.value() : box.united(it.value());
	}
	/* Pad each cluster outline outward by `pad` (the same inset the layout left at the
	   window edges). This fills that room with keyboard background instead of a hole, so
	   the margin around the edge keys (pad + the per-key inset = 2*kKeyInset) matches the
	   gap between keys (also 2*kKeyInset). Without the layout inset above this would clip at
	   the window border, which is why a full-width docked panel needs both halves. */
	QRegion mask;
	if (!leftBox.isNull())  mask += leftBox.adjusted(-pad, -pad, pad, pad).toAlignedRect();
	if (!rightBox.isNull()) mask += rightBox.adjusted(-pad, -pad, pad, pad).toAlignedRect();
	if (!mask.isEmpty())
		setMask(mask);

	repaint();
}


void SplitKeyboard::resizeEvent(QResizeEvent *rEvent)
{
	rEvent->accept();
	relayKeyboard();
}


/* Encoded key whose rect contains p, or -1. Skips the invisible spacers (key 0) so a
 * touch/click in the masked middle gap finds nothing (and passes through). */
int SplitKeyboard::keyAt(const QPointF &p) const
{
	Q_FOREACH (int key, layout.uniqueKeys())
	{
		if (key == 0)
			continue;
		Q_FOREACH (const QRectF &rect, layout.values(key))
		{
			if (rect.contains(p))
				return key;
		}
	}
	return -1;
}


/* Press / release wrappers. Both input paths (mouse + touch) funnel through these: they
 * track the UI highlight (keypress[]) and the Hide pseudo-key, and hand the rest to the
 * engine, whose sink drives X11. */
void SplitKeyboard::pressKey(int key)
{
	keypress[key] = true;
	if (key == 777)            // Hide pseudo-key: acted on at release
		return;
	engine.press(key);
}


void SplitKeyboard::releaseKey(int key)
{
	keypress[key] = false;
	if (key == 777)            // Hide pseudo-key
	{
		hide();
		return;
	}
	engine.release(key);
}


/* Route touch events here (and accept them, which suppresses synthesized mouse events). */
bool SplitKeyboard::event(QEvent *e)
{
	switch (e->type())
	{
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd:
		handleTouch(static_cast<QTouchEvent *>(e));
		return true;

	default:
		return QWidget::event(e);
	}
}


/* Multitouch dispatch: one encoded key per touch-point id. Keys commit on touch-down
 * and release on lift; moves/stationary are ignored in v1 (no slide-to-correct). */
void SplitKeyboard::handleTouch(QTouchEvent *tEvent)
{
	Q_FOREACH (const QEventPoint &tp, tEvent->points())
	{
		switch (tp.state())
		{
		case QEventPoint::Pressed:
		{
			int k = keyAt(tp.position());
			if (k >= 0)
			{
				touchKey.insert(tp.id(), k);
				pressKey(k);
			}
			break;
		}

		case QEventPoint::Released:
			if (touchKey.contains(tp.id()))
				releaseKey(touchKey.take(tp.id()));
			break;

		default:
			break;   /* Moved / Stationary: ignored in v1 */
		}
	}

	repaint();
	tEvent->accept();
}


void SplitKeyboard::mousePressEvent(QMouseEvent *mEvent)
{
	/* Only act on a real mouse/trackpad; ignore mouse events the system synthesizes
	 * from a touch (we handle those directly in handleTouch). */
	if (mEvent->source() != Qt::MouseEventNotSynthesized)
	{
		mEvent->accept();
		return;
	}

	mousePressedKey = keyAt(mEvent->pos());
	if (mousePressedKey >= 0)
		pressKey(mousePressedKey);

	repaint();
	mEvent->accept();
}


void SplitKeyboard::mouseReleaseEvent(QMouseEvent *mEvent)
{
	if (mEvent->source() != Qt::MouseEventNotSynthesized)
	{
		mEvent->accept();
		return;
	}

	if (mousePressedKey >= 0)
		releaseKey(mousePressedKey);
	mousePressedKey = -1;

	repaint();
	mEvent->accept();
}


void SplitKeyboard::paintEvent(QPaintEvent *pEvent)
{
	QPainter painter(this);
	bool shiftPressed = engine.modDown(engine.shift);   /* armed tap or momentary hold */

	/* Under HiDPI scaling the painter works in logical pixels, so the fixed label-inset
	 * constants (px) would consume twice the physical space at 200% -- crushing the text
	 * box and shrinking the glyph. Divide them by the device pixel ratio to keep the
	 * padding physically constant at any scale (a no-op at 100%, where dpr == 1). */
	const qreal dpr = devicePixelRatioF();
	const qreal labelInsetX = kLabelInsetX / dpr;
	const qreal labelInsetY = kLabelInsetY / dpr;

	Q_FOREACH (int key, layout.uniqueKeys())
	{
		/* A key having value 0, will be our spacer */
		if (key == 0)
		{
			continue;
		}

		int realKey = KeyEngine::realCode(key);
		const QStringList labels = keyChart.value(realKey);   // copy; avoids operator[] default-inserting during paint

		Q_FOREACH (QRectF layoutRect, layout.values(key))
		{
			/* Draw the key background: pressed (incl. armed modifier / locked Caps) */
			if (keypress.value(key) or engine.modPress.count(realKey) or (realKey == engine.caps && engine.capsOn))
			{
				painter.save();
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(Qt::darkGray));
				painter.drawRect(layoutRect.adjusted(kKeyInset, kKeyInset, -kKeyInset, -kKeyInset));
				painter.restore();
			}

			/* Draw the key background: normal */
			else
			{
				painter.save();
				painter.setPen(Qt::NoPen);
				painter.setBrush(palette().color(QPalette::Window).darker());
				painter.drawRect(layoutRect.adjusted(kKeyInset, kKeyInset, -kKeyInset, -kKeyInset));
				painter.restore();
			}

			/* Draw the key text: pressed */
			painter.save();
			painter.setRenderHints(QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
			QString keyTxt;

			bool useShifted =
				/* If shift is down (no caps, not the caps key) and the key has a
				   shifted variant, */
				(!engine.capsOn && shiftPressed
					&& realKey != engine.shift && realKey != engine.caps
					&& labels.size() > 1)
				/* or if caps is on (no shift) and it is a letter, */
				|| (engine.capsOn && !shiftPressed
					&& letters.contains(realKey))
				// or if caps is on (w/ shift) and it is a non-letter
				// (but non-shift key) with a shifted variant,
				|| (engine.capsOn && shiftPressed
					&& realKey != engine.shift && !letters.contains(realKey)
					&& labels.size() > 1)
				/* or if caps is on and it is the caps key, */
				|| (engine.capsOn && realKey == engine.caps);

			/* Then draw the shifted label, else the primary */
			keyTxt = useShifted ? labels.value(1)
			                    : labels.value(0);

			if (not keyTxt.contains("/") or (keyTxt == "/"))
			{
				/* Pad the label inside the key, then auto-shrink to fit -- by width
				   AND height, so a multi-line label (e.g. "Prt\nScr") stays inside. */
				QRectF txtRect = layoutRect.adjusted(labelInsetX, labelInsetY, -labelInsetX, -labelInsetY);
				QFont f = painter.font();
				QFontMetricsF fm(f);
				QRectF need = fm.boundingRect(txtRect, Qt::AlignCenter, keyTxt);
				qreal sx = need.width()  > 0 ? txtRect.width()  / need.width()  : 1.0;
				qreal sy = need.height() > 0 ? txtRect.height() / need.height() : 1.0;
				qreal scale = qMin(qreal(1.0), qMin(sx, sy));
				if (scale < 1.0)
				{
					f.setPointSizeF(f.pointSizeF() * scale);
					painter.setFont(f);
				}
				painter.drawText(txtRect, Qt::AlignCenter, keyTxt);

				/* The *other* variant, shown small + faint in the top-right corner
				   (like the printed legend on a physical key): the shifted symbol
				   while the base is centered, and the base char while shift swaps the
				   main label to the symbol. Non-letter keys only (no a/A clutter). */
				QString alt = useShifted ? labels.value(0)
				                         : labels.value(1);
				if (!letters.contains(realKey)
					&& realKey != engine.shift && realKey != engine.caps
					&& labels.size() > 1
					&& alt != keyTxt
					&& !alt.contains("/"))
				{
					painter.save();
					QFont af = painter.font();
					af.setPointSizeF(qMax(qreal(1.0), f.pointSizeF() * kAltLabelScale));
					painter.setFont(af);
					QColor ac = painter.pen().color();
					ac.setAlphaF(kAltLabelAlpha);
					painter.setPen(ac);
					QRectF altRect = layoutRect.adjusted(0, (kKeyInnerInset + 2) / dpr, -(kKeyInnerInset + 4) / dpr, 0);
					painter.drawText(altRect, Qt::AlignRight | Qt::AlignTop, alt);
					painter.restore();
				}
			}

			else
			{
				/* Render the key glyph as vector (QSvgRenderer) straight onto the painter
				   instead of rasterising the SVG into a QImage and scaling it up: the raster
				   path isn't rendered at the device pixel ratio, so it blurs under HiDPI /
				   fractional scaling, while vector rendering stays crisp at any scale. */
				const qreal size = qMin(layoutRect.width(), layoutRect.height()) * kIconScale;
				QSvgRenderer svg(keyTxt);
				QSizeF isz = svg.defaultSize();
				isz.scale(size, size, Qt::KeepAspectRatio);   // keep the icon's aspect ratio
				QRectF iconRect(QPointF(0, 0), isz);
				iconRect.moveCenter(layoutRect.center());
				svg.render(&painter, iconRect);
			}

			painter.restore();
		}
	}

	painter.end();

	pEvent->accept();
}


void SplitKeyboard::toggleShowHide()
{
	if (isVisible()){
        hide();
    } else{
		show();
	}
}

void SplitKeyboard::modeCompact()
{
    // Anchor to the available area (taskbar excluded) so the floating keyboard's
    // bottom-right corner sits above the panel, not on top of it (showEvent un-floats a
    // KDE panel so it sits flush against this bottom edge).
    QScreen *screen = qApp->primaryScreen();
    QRect avail = screen->availableGeometry();

    // Effective size = the configured window size, clamped up to our minimum. Compute it
    // here rather than reading width()/height() after the async resize(), so the move()
    // below places the window correctly on the very first show.
    const int minW = avail.width()  * 0.2;
    const int minH = avail.height() * 0.15;
    const int w = qMax(windowSize.width(),  minW);
    const int h = qMax(windowSize.height(), minH);
    setMinimumSize(minW, minH);
    resize(w, h);
    move(avail.x() + avail.width() - w, avail.y() + avail.height() - h);
    qApp->processEvents();
}

void SplitKeyboard::modeFixed()
{
    // Use the available area (panel excluded) so the docked bar's bottom edge sits at the
    // top of the taskbar rather than under it (showEvent un-floats a KDE panel so it sits
    // flush against that edge rather than overlapping the bottom key row).
    QScreen *screen = qApp->primaryScreen();
    QRect avail = screen->availableGeometry();

    resize(avail.width(), avail.height() * .3);
    setMinimumSize(avail.width(), avail.height() * .3);
    setGeometry(avail.x(), avail.y() + avail.height() * .7, avail.width(), avail.height() * .3);
    qApp->processEvents();
}

void SplitKeyboard::setPanelFloating(bool floating)
{
	// Plasma exposes a panel's float state only through its scripting bridge, so we drive
	// it via the org.kde.PlasmaShell.evaluateScript D-Bus call (needs the
	// org.kde.plasmashell talk-name, granted in the manifest). Fire-and-forget: if Plasma
	// isn't on the bus (another DE, or it's restarting) this is a harmless no-op.
	const QString flag = floating ? QStringLiteral("true") : QStringLiteral("false");
	const QString script = QStringLiteral(
		"var ps = panels();"
		"for (var i = 0; i < ps.length; i++) {"
		"    if (ps[i].location == 'bottom') ps[i].floating = %1;"
		"}").arg(flag);

	QDBusMessage msg = QDBusMessage::createMethodCall(
		QStringLiteral("org.kde.plasmashell"),
		QStringLiteral("/PlasmaShell"),
		QStringLiteral("org.kde.PlasmaShell"),
		QStringLiteral("evaluateScript"));
	msg << script;
	// send() queues the call without waiting for a reply (fire-and-forget).
	QDBusConnection::sessionBus().send(msg);
}

void SplitKeyboard::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// Un-float the taskbar so its top edge aligns with our bottom edge (no overlap).
	setPanelFloating(false);
	// Re-anchor against the current availableGeometry on every show. This is what makes a
	// hidden start (the --hidden autostart) safe: geometry is otherwise computed once in
	// the constructor, and if the app launched before the panel reserved its strut that
	// stale (full-screen) size would stick. Recomputing here -- by which point the strut is
	// established -- replaces the removed DaemonMode's broken one-shot detection.
	if (mMode)
		modeFixed();
	else
		modeCompact();
}

void SplitKeyboard::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	// Restore the floating default now that the keyboard is out of the way.
	setPanelFloating(true);
}

void SplitKeyboard::changeEvent(QEvent *event)
{
	/* Windowed (WM-managed) mode only: when the keyboard loses activation, re-assert
	   stay-on-top + no-focus and re-show so it stays above the app being typed into.
	   The dock mode is an override-redirect window that never takes focus, so it must
	   NOT be rewritten here -- doing so drops BypassWindowManagerHint and breaks the dock. */
	if (mWindowMode and (event->type() == QEvent::ActivationChange) and (!isActiveWindow()))
	{
		setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
		show();
		event->accept();
	}

	else
	{
		QWidget::changeEvent(event);
		event->accept();
	}
}


void SplitKeyboard::closeEvent(QCloseEvent *cEvent)
{
    smi->setValue("SplitKeyboard", "WindowSize", this->size());
    QWidget::closeEvent(cEvent);
}
