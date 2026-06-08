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

#include "keyengine.h"

int KeyEngine::realCode(int key)
{
	if (key > 1000) return key - 1000;
	return key;
}

bool KeyEngine::modDown(int realKey) const
{
	return modPress.count(realKey) != 0 || heldMods.count(realKey) != 0;
}

/* Press an encoded key. */
void KeyEngine::press(int key)
{
	int realKey = realCode(key);

	/* Modifier: physically engage it and track it as held. The sticky-vs-momentary
	 * decision is deferred to release(), the only place that can tell them apart. */
	if (isModifier(realKey))
	{
		if (!modDown(realKey))
			fire(realKey, true);
		heldMods.insert(realKey);
		return;
	}

	/* Caps (classic lock) is acted on at release; nothing to do on press. */
	if (realKey == caps)
		return;

	/* A normal key: press it. Any modifier physically held right now is "used" -- part of
	 * a real chord -- so its release won't be mistaken for a sticky tap. */
	usedMods.insert(heldMods.begin(), heldMods.end());
	fire(realKey, true);
}

/* Release an encoded key. */
void KeyEngine::release(int key)
{
	int realKey = realCode(key);

	/* Modifier: a finger that held it while other keys were pressed acts momentary (lift
	 * now); a quick tap toggles the armed one-shot state. */
	if (isModifier(realKey))
	{
		bool used = usedMods.count(realKey) != 0;
		heldMods.erase(realKey);
		usedMods.erase(realKey);

		if (used || modPress.count(realKey) != 0)
		{
			/* Momentary lift, or armed -> off: drop the latch and lift the key. */
			modPress.erase(realKey);
			fire(realKey, false);
		}
		else
		{
			/* Off -> armed: held for the next key, then auto-clears. Already physically
			 * down from press(). Modifiers accumulate so chords (Ctrl+Alt+Del) work. */
			modPress.insert(realKey);
		}
		return;
	}

	/* Caps: classic lock, toggled on each tap. The Caps keycode latches X's lock on the
	 * press edge, so send a full press+release pair and mirror the state for the labels. */
	if (realKey == caps)
	{
		fire(caps, true);
		fire(caps, false);
		capsOn = !capsOn;
		return;
	}

	/* Normal key release: lift it, then clear the one-shot (armed) modifiers it fired with.
	 * Iterate a snapshot since we erase from modPress as we go. A modifier that is *also*
	 * physically held right now (tap-armed and then pressed-and-held) is left down and
	 * armed: releasing it here would drop it mid-hold and double-fire its key-up when the
	 * finger finally lifts -- its own release() handles that lift. */
	fire(realKey, false);

	std::set<int> armed = modPress;
	for (int mod : armed)
	{
		if (heldMods.count(mod) != 0)
			continue;
		fire(mod, false);
		modPress.erase(mod);
	}
}
