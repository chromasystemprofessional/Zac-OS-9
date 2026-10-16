#include "keytags.h"

#include <xkbcommon/xkbcommon.h>

extern "C" {
#include "kblayout.h"
}

namespace {

/* Linux key codes are xkb's less 8. */
constexpr int XKB_OFFSET = 8;

/* Dead keys show the accent they add. */
struct DeadAccent {
	uint32_t sym;
	char32_t accent;
};
const DeadAccent DEAD_ACCENTS[] = {
	{ XKB_KEY_dead_grave, U'`' },      { XKB_KEY_dead_acute, U'´' },
	{ XKB_KEY_dead_circumflex, U'^' }, { XKB_KEY_dead_tilde, U'~' },
	{ XKB_KEY_dead_macron, U'¯' }, { XKB_KEY_dead_breve, U'˘' },
	{ XKB_KEY_dead_abovedot, U'˙' }, { XKB_KEY_dead_diaeresis, U'¨' },
	{ XKB_KEY_dead_abovering, U'˚' }, { XKB_KEY_dead_doubleacute, U'˝' },
	{ XKB_KEY_dead_caron, U'ˇ' },  { XKB_KEY_dead_cedilla, U'¸' },
	{ XKB_KEY_dead_ogonek, U'˛' },
};

} // namespace

KeyTags::KeyTags() : m_ctx(xkb_context_new(XKB_CONTEXT_NO_FLAGS)) {}

KeyTags::~KeyTags() {
	xkb_keymap_unref(m_keymap);
	xkb_context_unref(m_ctx);
}

bool KeyTags::load(const QString &code) {
	if (!m_ctx) {
		return false;
	}
	/* The model and options zacos9-wm uses, with this one layout. */
	struct kb_layouts layouts;
	kb_layouts_load(&layouts);
	kb_layouts_parse(&layouts, code.toUtf8().constData());
	if (layouts.n != 1) {
		return false;
	}
	/* Not kb_layouts_keymap(): it falls back to xkb's default layout,
	 * which would show the wrong characters under this one's name. */
	const QString layout = code.section('(', 0, 0);
	const QString variant = code.contains('(') ? code.section('(', 1).chopped(1) : QString();
	const QByteArray layoutName = layout.toUtf8(), variantName = variant.toUtf8();
	const xkb_rule_names names = {
		nullptr,
		*layouts.model ? layouts.model : nullptr,
		layoutName.constData(),
		variantName.constData(),
		*layouts.options ? layouts.options : nullptr,
	};
	xkb_keymap *keymap = xkb_keymap_new_from_names(m_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap) {
		return false;
	}
	xkb_keymap_unref(m_keymap);
	m_keymap = keymap;
	m_code = code;
	return true;
}

uint32_t KeyTags::keysym(int key, int level) const {
	if (!m_keymap) {
		return XKB_KEY_NoSymbol;
	}
	const xkb_keycode_t keycode = key + XKB_OFFSET;
	const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(m_keymap, keycode, 0);
	if (level < 0 || xkb_level_index_t(level) >= levels) {
		return XKB_KEY_NoSymbol;
	}
	const xkb_keysym_t *syms = nullptr;
	return xkb_keymap_key_get_syms_by_level(m_keymap, keycode, 0, level, &syms) == 1
		? syms[0] : XKB_KEY_NoSymbol;
}

KeyTags::Tag KeyTags::tag(int key, int level) const {
	const uint32_t sym = keysym(key, level);
	Tag tag;
	for (const DeadAccent &dead : DEAD_ACCENTS) {
		if (dead.sym == sym) {
			tag.text = QString::fromUcs4(&dead.accent, 1);
			tag.dead = true;
			return tag;
		}
	}
	const char32_t c = xkb_keysym_to_utf32(sym);
	/* Characters only: not Tab, Return, Delete or the like. */
	if (c >= 0x20 && c != 0x7f) {
		tag.text = QString::fromUcs4(&c, 1);
	}
	return tag;
}

bool KeyTags::isShift(int key) const {
	const uint32_t sym = keysym(key, 0);
	return sym == XKB_KEY_Shift_L || sym == XKB_KEY_Shift_R;
}

bool KeyTags::isOption(int key) const {
	const uint32_t sym = keysym(key, 0);
	return sym == XKB_KEY_ISO_Level3_Shift || sym == XKB_KEY_Mode_switch;
}
