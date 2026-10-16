#pragma once

#include <QMainWindow>
#include <QProcess>

#include "textfile.h"

class QPlainTextEdit;

/*
 * A Scribble document window (Mac OS 9's SimpleText): one text file, its
 * menus in ZacOS 9's menu bar. Closing it with changes asks to save them.
 */
class ScribbleWindow : public QMainWindow {
public:
	ScribbleWindow();
	/* Opens `path` in this (new) window; false, with an alert, if it can't. */
	bool open(const QString &path);
	/* New window: untitled, or for a file chosen. */
	static ScribbleWindow *newWindow();
	static void chooseAndOpen();
	bool save();
	bool saveAs();

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	void buildMenus();
	void updateTitle();
	void find(bool again);
	void applyFont();
	void speakSelection();
	void stopSpeaking();

	QPlainTextEdit *m_edit = nullptr;
	QString m_path;
	TextFile m_file;
	QString m_findText;
	QProcess m_speech;
	bool m_speechStopped = false;
};
