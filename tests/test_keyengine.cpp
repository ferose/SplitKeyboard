/*
  * This file is part of SplitKeyboard (a fork of CoreKeyboard).
  * Copyright 2026 Ferose
  *
  * Unit tests for KeyEngine -- the modifier state machine. Framework-free so it compiles
  * with a bare C++17 g++ (no Qt, no X11). Run via tests/run.sh.
  *
  * GPL-3.0-or-later (see keyengine.h for the full notice).
  */

#include "keyengine.h"

#include <vector>
#include <utility>
#include <initializer_list>
#include <cstdio>

/* --- tiny test harness ------------------------------------------------------------- */
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                     \
	do {                                                                                \
		++g_checks;                                                                     \
		if (!(cond)) {                                                                  \
			++g_failures;                                                               \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
		}                                                                               \
	} while (0)

using Event = std::pair<int, bool>;          // (keycode, down)
using Events = std::vector<Event>;

static void dump(const char *label, const Events &ev)
{
	std::printf("    %s: ", label);
	for (const auto &e : ev)
		std::printf("%d%s ", e.first, e.second ? "v" : "^");
	std::printf("\n");
}

#define CHECK_SEQ(actual, ...)                                                          \
	do {                                                                                \
		++g_checks;                                                                     \
		Events expected = { __VA_ARGS__ };                                              \
		if ((actual) != expected) {                                                     \
			++g_failures;                                                               \
			std::printf("  FAIL %s:%d: event sequence mismatch\n", __FILE__, __LINE__); \
			dump("expected", expected);                                                 \
			dump("actual  ", (actual));                                                 \
		}                                                                               \
	} while (0)

/* X11 keycodes used in the tests */
enum { CTRL = 37, SHIFT = 50, ALT = 64, SUPER = 133, CAPS = 66, A = 38, B = 39, DEL = 119 };

static Event down(int k) { return { k, true }; }
static Event up(int k)   { return { k, false }; }

/* A fresh engine wired to an event recorder. */
struct Fixture {
	Events ev;
	KeyEngine eng;
	Fixture()
	{
		eng.modifiers = { CTRL, SHIFT, ALT, SUPER };
		eng.ctrl = CTRL; eng.shift = SHIFT; eng.caps = CAPS;
		eng.setSink([this](int c, bool d) { ev.push_back({ c, d }); });
	}
	void tap(int k)    { eng.press(k); eng.release(k); }
	void clear()       { ev.clear(); }
};

/* --- tests ------------------------------------------------------------------------- */

static void test_realCode()
{
	CHECK(KeyEngine::realCode(A) == A);
	CHECK(KeyEngine::realCode(1000 + 56) == 56);   // 1xxx fat key decodes
	CHECK(KeyEngine::realCode(0) == 0);
}

static void test_oneShot_armsThenClears()
{
	Fixture f;
	f.eng.press(SHIFT);            // physically down
	f.eng.release(SHIFT);          // quick tap -> armed (no extra event)
	CHECK(f.eng.modDown(SHIFT));
	CHECK(f.eng.modPress.count(SHIFT) == 1);

	f.eng.press(A);                // a normal key uses the armed Shift...
	f.eng.release(A);              // ...then it auto-clears
	CHECK_SEQ(f.ev, down(SHIFT), down(A), up(A), up(SHIFT));
	CHECK(f.eng.modPress.empty());
	CHECK(!f.eng.modDown(SHIFT));
	CHECK(!f.eng.capsOn);

	// the NEXT key must be unmodified (one-shot really cleared)
	f.clear();
	f.tap(B);
	CHECK_SEQ(f.ev, down(B), up(B));
}

static void test_doubleTap_disarms()
{
	Fixture f;
	f.tap(SHIFT);                  // arm
	f.tap(SHIFT);                  // disarm
	CHECK_SEQ(f.ev, down(SHIFT), up(SHIFT));
	CHECK(f.eng.modPress.empty());
	CHECK(!f.eng.modDown(SHIFT));
}

static void test_chord_accumulates()
{
	Fixture f;
	f.tap(CTRL);                   // arm Ctrl
	f.tap(ALT);                    // arm Alt (accumulates)
	CHECK(f.eng.modPress.count(CTRL) == 1);
	CHECK(f.eng.modPress.count(ALT) == 1);

	f.eng.press(DEL);
	f.eng.release(DEL);            // fires Ctrl+Alt+Del, then clears both
	// set-ordered release: 37 (Ctrl) before 64 (Alt)
	CHECK_SEQ(f.ev, down(CTRL), down(ALT), down(DEL), up(DEL), up(CTRL), up(ALT));
	CHECK(f.eng.modPress.empty());
}

static void test_momentaryHold()
{
	Fixture f;
	f.eng.press(SHIFT);            // hold it down...
	f.eng.press(A);                // ...type while held (marks it "used")
	f.eng.release(A);
	f.eng.release(SHIFT);          // lift -> momentary, not latched
	CHECK_SEQ(f.ev, down(SHIFT), down(A), up(A), up(SHIFT));
	CHECK(f.eng.modPress.empty());
	CHECK(f.eng.heldMods.empty());
	CHECK(f.eng.usedMods.empty());
}

static void test_caps_toggles()
{
	Fixture f;
	CHECK(!f.eng.capsOn);
	f.tap(CAPS);
	CHECK_SEQ(f.ev, down(CAPS), up(CAPS));
	CHECK(f.eng.capsOn);
	f.clear();
	f.tap(CAPS);
	CHECK_SEQ(f.ev, down(CAPS), up(CAPS));
	CHECK(!f.eng.capsOn);
}

static void test_fatKey_press()
{
	Fixture f;
	f.eng.press(1000 + A);         // a "fat" encoding of A
	f.eng.release(1000 + A);
	CHECK_SEQ(f.ev, down(A), up(A));
}

/* Regression: tap-arm a modifier, then physically hold that *same* key while typing.
 * The armed latch must not lift the modifier mid-hold -- it stays down across every key
 * and lifts exactly once on the real release (no dropped modifier, no double key-up). */
static void test_armThenHold_staysDown()
{
	Fixture f;
	f.tap(SHIFT);                  // tap -> armed (still physically down)
	f.eng.press(SHIFT);            // now press-and-hold the same modifier
	f.eng.press(A); f.eng.release(A);
	f.eng.press(B); f.eng.release(B);
	f.eng.release(SHIFT);          // finally lift
	CHECK_SEQ(f.ev, down(SHIFT), down(A), up(A), down(B), up(B), up(SHIFT));
	CHECK(f.eng.modPress.empty());
	CHECK(f.eng.heldMods.empty());
	CHECK(f.eng.usedMods.empty());
}

int main()
{
	test_realCode();
	test_oneShot_armsThenClears();
	test_doubleTap_disarms();
	test_chord_accumulates();
	test_momentaryHold();
	test_caps_toggles();
	test_fatKey_press();
	test_armThenHold_staysDown();

	std::printf("\nKeyEngine tests: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
