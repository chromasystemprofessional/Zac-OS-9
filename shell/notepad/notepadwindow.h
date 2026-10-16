#pragma once

#include <QMainWindow>

class QPlainTextEdit;
class QTimer;
class QCloseEvent;

class NotepadWindow : public QMainWindow {
public:
	explicit NotepadWindow(int page = 0);

	int currentPage() const { return m_page; }
	QString pageText(int page) const;
	void setPageText(int page, const QString &text);

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	void showPage(int page);
	void savePage();
	void updateTitle();

	QPlainTextEdit *m_edit = nullptr;
	QTimer *m_saveTimer = nullptr;
	int m_page = 0;
	bool m_loading = false;
};
