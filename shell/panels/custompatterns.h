#pragma once

#include <QStringList>
#include <functional>

#include "draw.h"

class QObject;
QString desktopPatternsFolder();
int desktopPatternCount();
QString desktopPatternId(int index);
QString desktopPatternName(int index);
int desktopPatternFind(const QString &id);
void desktopPatternFill(pl_canvas *canvas, int index, int x0, int y0, int x1, int y1);
QStringList desktopPatternErrors();
void watchDesktopPatterns(QObject *owner, std::function<void()> changed);
QString desktopWallpaperFolder();
int desktopWallpaperCount();
QString desktopWallpaperId(int index);
QString desktopWallpaperName(int index);
int desktopWallpaperFind(const QString &id);
QStringList desktopWallpaperErrors();
void watchDesktopWallpaper(QObject *owner, std::function<void()> changed);
void desktopWallpaperFill(pl_canvas *canvas, int index, const QString &mode,
	int x0, int y0, int x1, int y1);
void desktopBackgroundFill(pl_canvas *canvas, int width, int height);
