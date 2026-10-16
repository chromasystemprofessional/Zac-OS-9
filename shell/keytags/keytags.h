#pragma once

#include <QString>

struct xkb_context;
struct xkb_keymap;

/*
 * What each key of one keyboard layout types, read from xkb as zacos9-wm
 * builds its keymap (compositor/src/kblayout.c). Keys are Linux key codes
 * (KEY_A is 30); levels are 0 plain, 1 Shift, 2 Option (xkb's third level,
 * Alt Gr) and 3 Shift-Option.
 */
class KeyTags {
public:
	KeyTags();
	~KeyTags();
	KeyTags(const KeyTags &) = delete;
	KeyTags &operator=(const KeyTags &) = delete;

	/* The layout by its Keyboard-panel code ("us", "de(nodeadkeys)"). */
	bool load(const QString &code);
	QString code() const { return m_code; }

	struct Tag {
		QString text;      /* what the key types; empty: nothing */
		bool dead = false; /* an accent waiting for the next key */
	};
	Tag tag(int key, int level) const;
	/* Whether the key, held, is a Shift or an Option (third level) key. */
	bool isShift(int key) const;
	bool isOption(int key) const;

private:
	uint32_t keysym(int key, int level) const;

	xkb_context *m_ctx = nullptr;
	xkb_keymap *m_keymap = nullptr;
	QString m_code;
};
