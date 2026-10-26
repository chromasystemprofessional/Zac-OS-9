#pragma once

/*
 * Bridge between our pixel painters (lib/) and Qt: draw at 1x into a
 * QImage, then let Qt scale it to the output with nearest-neighbour so the
 * 1990s pixel art stays exact at 2x/3x; at 1.25x, 1.5x and 1.75x it is
 * scaled here instead (blitPixels).
 */

#include <QImage>
#include <QPainter>
#include <QWidget>
#include <utility>

#include "draw.h"
#include "icons.h"
#include "text.h"

/* A 1x image drawn with its top left at `at`. At a whole-number scale Qt
 * enlarges it exactly. At 1.25, 1.5 and 1.75 it is made here at the
 * window's own pixels, from where it is in the window (lib/draw.c pl_snap;
 * zacos9-wm starts a window where the screen's pixel pattern does), and
 * drawn one for one: each 1x pixel one or two whole pixels, the same
 * pattern across every image in the window and its frame, as zacos9-wm
 * and the menu bar draw theirs. (Qt's own enlarging rounds from each
 * image's place, so neighbours' patterns would differ.) */
inline void blitPixels(QPainter &p, const QImage &img, QPoint at, const pl_canvas *canvas = nullptr) {
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1;
	const int quarters = qRound(dpr * 4);
	const QTransform &t = p.transform();
	QPointF where = t.map(QPointF(at));
	if (auto *widget = dynamic_cast<QWidget *>(p.device())) {
		where += widget->mapTo(widget->window(), QPoint());
	}
	const int ux = qRound(where.x()), uy = qRound(where.y());
	if (quarters % 4 == 0 || qAbs(dpr * 4 - quarters) > 0.01 || t.type() > QTransform::TxTranslate ||
			qAbs(where.x() - ux) > 0.001 || qAbs(where.y() - uy) > 0.001 ||
			img.format() != QImage::Format_ARGB32_Premultiplied) {
		p.drawImage(QRect(at, img.size()), img);
		return;
	}
	const int dx = pl_snap(ux, quarters), dy = pl_snap(uy, quarters);
	QImage big(pl_snap(ux + img.width(), quarters) - dx, pl_snap(uy + img.height(), quarters) - dy,
		QImage::Format_ARGB32_Premultiplied);
	if (big.isNull() || img.bytesPerLine() != img.width() * 4 || big.bytesPerLine() != big.width() * 4) {
		p.drawImage(QRect(at, img.size()), img);
		return;
	}
	if (canvas && canvas->px == reinterpret_cast<const uint32_t *>(img.constBits()) &&
			canvas->width == img.width() && canvas->height == img.height()) {
		/* Its text in the font for this scale (lib/text.c). */
		pl_snap_scale_canvas(canvas, ux, uy, quarters, reinterpret_cast<uint32_t *>(big.bits()));
	} else {
		pl_snap_scale(reinterpret_cast<const uint32_t *>(img.constBits()), img.width(), img.height(), ux,
			uy, quarters, reinterpret_cast<uint32_t *>(big.bits()));
	}
	big.setDevicePixelRatio(dpr);
	/* At the window's pixel (dx, dy): that, logical, less what `at` adds. */
	p.drawImage(QPointF(at) + QPointF(dx / dpr - ux, dy / dpr - uy), big);
}

/* A 1x image to draw on with lib/, its text logged so that at 1.25x, 1.5x
 * and 1.75x blit draws it again in the font for that scale. */
struct Pixels {
	QImage img;
	pl_canvas c;

	Pixels(int w, int h)
		: img(w > 0 ? w : 1, h > 0 ? h : 1, QImage::Format_ARGB32_Premultiplied) {
		img.fill(0);
		uint32_t *px = reinterpret_cast<uint32_t *>(img.bits());
		const int stride = static_cast<int>(img.bytesPerLine() / 4);
		c = { px, stride, 0, 0, img.width(), img.height(), pl_text_log_new(px, stride) };
	}
	~Pixels() { pl_text_log_free(c.log); }
	Pixels(const Pixels &) = delete;
	Pixels &operator=(const Pixels &) = delete;
	Pixels(Pixels &&o) noexcept : img(std::move(o.img)), c(o.c) { o.c.log = nullptr; }

	void blit(QPainter &p, int x = 0, int y = 0) const {
		blitPixels(p, img, QPoint(x, y), &c);
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
