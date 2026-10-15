#include "helpwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBrowser>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "helpbook.h"
#include "panelkit.h"
#include "pixels.h"

namespace {

constexpr uint32_t FACE = GRAY(0xD);
constexpr int BAR_H = 40, M = 10, GAP = 6;
const QString SCHEME = "zacos9-help";

QUrl centerUrl() {
	return QUrl(SCHEME + ":center");
}

QUrl searchUrl(const QString &words) {
	QUrl url(SCHEME + ":search");
	QUrlQuery q;
	q.addQueryItem("q", words);
	url.setQuery(q);
	return url;
}

} // namespace

/* Pages from files, and the made ones. */
class HelpWindow::Browser : public QTextBrowser {
public:
	QVariant loadResource(int type, const QUrl &url) override {
		if (url.scheme() == SCHEME && type == QTextDocument::HtmlResource) {
			const QString root = helpRoot();
			if (url.path() == "search") {
				const QString words = QUrlQuery(url).queryItemValue("q", QUrl::FullyDecoded);
				const QList<HelpPage> pages = helpPages(root);
				return searchResultsHtml(words, searchHelp(pages, words));
			}
			return helpCenterHtml(helpBooks(root));
		}
		return QTextBrowser::loadResource(type, url);
	}
};

/* Back, Forward, Help Center, and the search field with its button. */
class HelpWindow::Toolbar : public QWidget {
public:
	explicit Toolbar(HelpWindow *w) : QWidget(w), m_w(w), m_host(this) {
		setFixedHeight(BAR_H);
		setFocusPolicy(Qt::StrongFocus);
		const int y = (BAR_H - PL_BUTTON_H) / 2;
		int x = M;
		m_back = PanelButton("Back", QRect(x, y, 56, PL_BUTTON_H));
		x += 56 + GAP;
		m_forward = PanelButton("Forward", QRect(x, y, 68, PL_BUTTON_H));
		x += 68 + GAP;
		m_center = PanelButton("Help Center", QRect(x, y, 92, PL_BUTTON_H));
		x += 92 + 3 * GAP;
		m_field.rect = QRect(x, (BAR_H - PL_EDIT_H) / 2, 170, PL_EDIT_H);
		x += 170 + GAP;
		m_search = PanelButton("Search", QRect(x, y, 64, PL_BUTTON_H));
		m_back.clicked = [w] { w->back(); };
		m_forward.clicked = [w] { w->forward(); };
		m_center.clicked = [w] { w->showCenter(); };
		m_search.clicked = [this] { m_w->search(m_field.text); };
		m_field.edited = [this] { m_search.enabled = !m_field.text.trimmed().isEmpty(); update(); };
		m_search.enabled = false;
		m_host.buttons = { &m_back, &m_forward, &m_center, &m_search };
		m_host.edits = { &m_field };
		m_host.defaultButton = &m_search;
		m_host.setFocus(&m_field);
	}

	void sync(bool canBack, bool canForward) {
		m_back.enabled = canBack;
		m_forward.enabled = canForward;
		update();
	}

	PanelEdit m_field;
	PanelButton m_back, m_forward, m_center, m_search;

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, width() - 1, height() - 1, FACE);
		pl_hline(c, 0, width() - 1, height() - 1, GRAY(0x8));
		m_host.paintControls(c, FACE);
		QPainter p(this);
		px.blit(p);
	}
	void mousePressEvent(QMouseEvent *e) override {
		setFocus();
		m_host.hostPress(e);
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		m_host.hostMove(e);
		update();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		m_host.hostRelease(e);
		update();
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (!m_host.hostKey(e)) {
			QWidget::keyPressEvent(e);
		}
		update();
	}

private:
	HelpWindow *m_w;
	PanelHost m_host;
};

HelpWindow::HelpWindow() {
	setWindowTitle("Help Center");
	setMinimumSize(520, 360);
	resize(620, 520);
	auto *central = new QWidget(this);
	auto *layout = new QVBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	m_toolbar = new Toolbar(this);
	m_browser = new Browser;
	m_browser->setFrameShape(QFrame::NoFrame);
	m_browser->setOpenLinks(false);
	m_browser->document()->setDocumentMargin(14);
	layout->addWidget(m_toolbar);
	layout->addWidget(m_browser);
	setCentralWidget(central);
	connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &link) { follow(link); });
	connect(m_browser, &QTextBrowser::historyChanged, this, [this] {
		m_toolbar->sync(m_browser->isBackwardAvailable(), m_browser->isForwardAvailable());
		updateTitle();
	});
	buildMenus();
	m_toolbar->sync(false, false);
}

void HelpWindow::buildMenus() {
	/* Exported to ZacOS 9's menu bar, as other programs' are. */
	QMenu *file = menuBar()->addMenu("File");
	file->addAction("Close", QKeySequence::Close, this, [this] { close(); });
	file->addSeparator();
	file->addAction("Quit", QKeySequence::Quit, this, [this] { close(); })->setMenuRole(QAction::NoRole);
	QMenu *edit = menuBar()->addMenu("Edit");
	edit->addAction("Copy", QKeySequence::Copy, m_browser, &QTextBrowser::copy);
	edit->addAction("Select All", QKeySequence::SelectAll, m_browser, &QTextBrowser::selectAll);
	edit->addSeparator();
	edit->addAction("Find in Help…", QKeySequence::Find, this, [this] {
		m_toolbar->setFocus();
		m_toolbar->m_field.selectAll();
		m_toolbar->update();
	})->setMenuRole(QAction::NoRole);
	QMenu *go = menuBar()->addMenu("Go");
	go->addAction("Back", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), this, [this] { back(); });
	go->addAction("Forward", QKeySequence(Qt::CTRL | Qt::Key_BracketRight), this, [this] { forward(); });
	go->addSeparator();
	go->addAction("Help Center", this, [this] { showCenter(); });
	go->addAction("ZacOS Help", this, [this] { showPage("zacos9"); });
}

void HelpWindow::showCenter() {
	m_browser->setSource(centerUrl(), QTextDocument::HtmlResource);
}

bool HelpWindow::showPage(const QString &pathOrBook) {
	QString path = pathOrBook;
	if (!path.contains('/') && !path.endsWith(".html")) {
		path = helpRoot() + "/" + path + "/index.html"; /* a book */
	} else if (QFileInfo(path).isRelative() && !QFileInfo::exists(path)) {
		path = helpRoot() + "/" + path; /* "zacos9/files.html" */
	}
	if (!QFileInfo(path).isFile()) {
		return false;
	}
	m_browser->setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()), QTextDocument::HtmlResource);
	return true;
}

void HelpWindow::search(const QString &words) {
	if (words.trimmed().isEmpty()) {
		return;
	}
	setSearchText(words);
	m_browser->setSource(searchUrl(words.trimmed()), QTextDocument::HtmlResource);
}

QString HelpWindow::searchText() const {
	return m_toolbar->m_field.text;
}

void HelpWindow::setSearchText(const QString &text) {
	m_toolbar->m_field.setText(text);
	m_toolbar->m_search.enabled = !text.trimmed().isEmpty();
	m_toolbar->update();
}

void HelpWindow::back() {
	m_browser->backward();
}

void HelpWindow::forward() {
	m_browser->forward();
}

void HelpWindow::follow(const QUrl &link) {
	const QUrl url = m_browser->source().resolved(link);
	if (url.scheme() == "file" || url.scheme() == SCHEME) {
		// A link to a place on this page, or another page of help.
		if (url.adjusted(QUrl::RemoveFragment) == m_browser->source().adjusted(QUrl::RemoveFragment) &&
				url.hasFragment()) {
			m_browser->scrollToAnchor(url.fragment());
		} else {
			m_browser->setSource(url, QTextDocument::HtmlResource);
		}
		return;
	}
	QDesktopServices::openUrl(url); /* the web, mail */
}

void HelpWindow::updateTitle() {
	const QString title = m_browser->documentTitle();
	setWindowTitle(title.isEmpty() ? QStringLiteral("Help Center") : title);
}
