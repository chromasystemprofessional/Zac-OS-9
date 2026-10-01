#pragma once

/*
 * Bridge between our pixel painters (lib/) and Qt: draw at 1x into a
 * QImage, then let Qt scale it to the output with nearest-neighbour so the
 * 1990s pixel art stays exact at 2x/3x.
 */

#include <QImage>
#include <QPainter>

#include "draw.h"
#include "icons.h"
#include "text.h"

struct Pixels {
	QImage img;
	pl_canvas c;

	Pixels(int w, int h)
		: img(w > 0 ? w : 1, h > 0 ? h : 1, QImage::Format_ARGB32_Premultiplied) {
		img.fill(0);
		c = { reinterpret_cast<uint32_t *>(img.bits()),
			static_cast<int>(img.bytesPerLine() / 4), 0, 0, img.width(), img.height() };
	}

	void blit(QPainter &p, int x = 0, int y = 0) const {
		p.setRenderHint(QPainter::SmoothPixmapTransform, false);
		p.drawImage(QRect(x, y, img.width(), img.height()), img);
	}
};

/* Owning wrapper for a rendered text mask. */
struct Text {
	plat_text *t = nullptr;
	Text() = default;
	Text(const QString &s, int max_width, pl_font font) {
		t = text_render_font(s.toUtf8().constData(), max_width, font);
	}
	Text(const Text &) = delete;
	Text &operator=(const Text &) = delete;
	Text(Text &&o) noexcept : t(o.t) { o.t = nullptr; }
	Text &operator=(Text &&o) noexcept {
		std::swap(t, o.t);
		return *this;
	}
	~Text() { text_destroy(t); }
	int inkWidth() const { return t && t->ink_l >= 0 ? t->ink_r - t->ink_l + 1 : 0; }
};
