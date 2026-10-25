/*
 * zacos9-jigsaw: Jigsaw Puzzle (Mac OS 9's, gap plan 8.6). A picture cut
 * into pieces with tabs and blanks, scattered; drag each to its place,
 * where it snaps and stays. Puzzle ▸ Open Picture… for another picture,
 * and three piece sizes.
 */

#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>

#include "alert.h"
#include "platinumshell.h"
#include "settings.h"

namespace {

constexpr int BOARD_W = 600, BOARD_H = 440, PICTURE_W = 360, PICTURE_H = 270;

struct Piece {
	int col = 0, row = 0;
	QPointF home, pos; /* top-left of its cell: where it belongs, where it is */
	QPainterPath shape; /* in cell coordinates, tabs and all */
	bool placed = false;
};

class Board : public QWidget {
public:
	Board() {
		setFixedSize(BOARD_W, BOARD_H);
		loadPicture(QString::fromUtf8(pl_data_dir()) + "/boot/happy-zac-4x.png");
	}

	void loadPicture(const QString &path) {
		QImage image(path);
		if (image.isNull()) {
			/* No picture: a sunset of stripes, so there's still a puzzle. */
			image = QImage(PICTURE_W, PICTURE_H, QImage::Format_RGB32);
			QPainter p(&image);
			for (int y = 0; y < PICTURE_H; y++) {
				p.setPen(QColor::fromHsv((y * 300 / PICTURE_H) % 360, 160, 230));
				p.drawLine(0, y, PICTURE_W, y);
			}
		}
		/* Fitted to the picture area, on a white mat. */
		m_picture = QImage(PICTURE_W, PICTURE_H, QImage::Format_ARGB32);
		m_picture.fill(QColor(0xEE, 0xEE, 0xEE));
		QPainter p(&m_picture);
		const QImage fitted = image.scaled(PICTURE_W, PICTURE_H, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		p.drawImage((PICTURE_W - fitted.width()) / 2, (PICTURE_H - fitted.height()) / 2, fitted);
		newPuzzle();
	}

	void setPieces(int cols) {
		m_cols = cols;
		newPuzzle();
	}

	void newPuzzle() {
		m_rows = std::max(2, m_cols * 3 / 4);
		const double cw = double(PICTURE_W) / m_cols, ch = double(PICTURE_H) / m_rows;
		/* Each inner edge bulges one way: +1 a tab to the right/down. */
		auto *rng = QRandomGenerator::global();
		QVector<QVector<int>> across(m_rows, QVector<int>(m_cols + 1)), down(m_rows + 1, QVector<int>(m_cols));
		for (auto &r : across) for (int &v : r) v = rng->bounded(2) ? 1 : -1;
		for (auto &r : down) for (int &v : r) v = rng->bounded(2) ? 1 : -1;
		m_pieces.clear();
		const QPointF origin((BOARD_W - PICTURE_W) / 2.0, 16);
		for (int r = 0; r < m_rows; r++) {
			for (int c = 0; c < m_cols; c++) {
				Piece piece;
				piece.col = c;
				piece.row = r;
				piece.home = origin + QPointF(c * cw, r * ch);
				const int top = r == 0 ? 0 : down[r][c], bottom = r == m_rows - 1 ? 0 : down[r + 1][c];
				const int left = c == 0 ? 0 : across[r][c], right = c == m_cols - 1 ? 0 : across[r][c + 1];
				piece.shape = shapeFor(cw, ch, top, right, bottom, left);
				/* Scattered over the board, loosely. */
				piece.pos = QPointF(rng->bounded(BOARD_W - cw), rng->bounded(BOARD_H - ch));
				m_pieces << piece;
			}
		}
		m_done = false;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(rect(), QColor(0x66, 0x66, 0x99));
		/* Where the picture goes, faintly. */
		p.fillRect(QRectF((BOARD_W - PICTURE_W) / 2.0, 16, PICTURE_W, PICTURE_H), QColor(0x77, 0x77, 0xAA));
		/* Placed pieces first, then loose ones, the held one on top. */
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < m_pieces.size(); i++) {
				if (m_pieces[i].placed != (pass == 0) || i == m_held) {
					continue;
				}
				paintPiece(p, m_pieces[i]);
			}
		}
		if (m_held >= 0) {
			paintPiece(p, m_pieces[m_held]);
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPointF at = e->position();
		for (int i = m_pieces.size() - 1; i >= 0; i--) {
			const Piece &piece = m_pieces[i];
			if (!piece.placed && piece.shape.contains(at - piece.pos)) {
				/* To the top of the pile. */
				m_pieces.move(i, m_pieces.size() - 1);
				m_held = m_pieces.size() - 1;
				m_grab = at - m_pieces[m_held].pos;
				update();
				return;
			}
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (m_held >= 0) {
			m_pieces[m_held].pos = e->position() - m_grab;
			update();
		}
	}
	void mouseReleaseEvent(QMouseEvent *) override {
		if (m_held < 0) {
			return;
		}
		Piece &piece = m_pieces[m_held];
		if (QLineF(piece.pos, piece.home).length() < 10) {
			piece.pos = piece.home;
			piece.placed = true;
			pl_sound_event("menu-command");
		}
		m_held = -1;
		update();
		if (!m_done && std::all_of(m_pieces.cbegin(), m_pieces.cend(), [](const Piece &x) { return x.placed; })) {
			m_done = true;
			Alert::ask("You did it! Choose New Puzzle to play again.", "OK", QString());
		}
	}

private:
	QImage m_picture;
	QList<Piece> m_pieces;
	int m_cols = 6, m_rows = 4, m_held = -1;
	QPointF m_grab;
	bool m_done = false;

	/* A cell's outline with a tab (+1), blank (-1) or flat edge (0) on each side. */
	static QPainterPath shapeFor(double w, double h, int top, int right, int bottom, int left) {
		QPainterPath path(QPointF(0, 0));
		auto edge = [&](QPointF a, QPointF b, int bulge, QPointF outward) {
			if (bulge == 0) {
				path.lineTo(b);
				return;
			}
			const QPointF d = b - a, n = outward * bulge * (QLineF(a, b).length() * 0.22);
			const QPointF p1 = a + d * 0.38, p2 = a + d * 0.62;
			path.lineTo(p1);
			path.cubicTo(p1 + n * 0.2 - d * 0.08, p1 + n * 1.2 - d * 0.12, a + d * 0.5 + n * 1.2);
			path.cubicTo(p2 + n * 1.2 + d * 0.12, p2 + n * 0.2 + d * 0.08, p2);
			path.lineTo(b);
		};
		edge({ 0, 0 }, { w, 0 }, top, { 0, -1 });
		edge({ w, 0 }, { w, h }, right, { 1, 0 });
		edge({ w, h }, { 0, h }, bottom, { 0, 1 });
		edge({ 0, h }, { 0, 0 }, left, { -1, 0 });
		path.closeSubpath();
		return path;
	}

	void paintPiece(QPainter &p, const Piece &piece) {
		p.save();
		p.translate(piece.pos);
		p.setClipPath(piece.shape);
		/* The picture as it lies under this piece's place. */
		p.drawImage(QPointF(piece.home.x() - (BOARD_W - PICTURE_W) / 2.0, piece.home.y() - 16) * -1, m_picture);
		p.setClipping(false);
		p.setPen(QPen(piece.placed ? QColor(0, 0, 0, 40) : QColor(0, 0, 0, 160), 1));
		p.drawPath(piece.shape);
		p.restore();
	}
};

class Window : public QMainWindow {
public:
	Window() {
		setWindowTitle("Jigsaw Puzzle");
		auto *board = new Board;
		setCentralWidget(board);
		QMenu *puzzle = menuBar()->addMenu("Puzzle");
		puzzle->addAction("New Puzzle", QKeySequence::New, board, [board] { board->newPuzzle(); });
		puzzle->addAction("Open Picture…", QKeySequence::Open, this, [this, board] {
			const QString path = QFileDialog::getOpenFileName(this, "Open Picture", QString(),
				"Pictures (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
			if (!path.isEmpty()) {
				board->loadPicture(path);
			}
		});
		puzzle->addSeparator();
		puzzle->addAction("Large Pieces", this, [board] { board->setPieces(4); });
		puzzle->addAction("Medium Pieces", this, [board] { board->setPieces(6); });
		puzzle->addAction("Small Pieces", this, [board] { board->setPieces(9); });
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Jigsaw Puzzle");
	QGuiApplication::setDesktopFileName("zacos9-jigsaw"); /* Wayland app_id */
	platinumShellInit();
	Window w;
	w.show();
	return app.exec();
}
