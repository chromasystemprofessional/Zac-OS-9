#pragma once

#include <QList>
#include <QString>

class QFileDialog;

struct FileChooserLocation {
	QString name;
	QString path;
};

QList<FileChooserLocation> fileChooserLocations();
void prepareFileChooserDialog(QFileDialog &dialog);
