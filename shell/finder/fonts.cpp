#include "fonts.h"
#include "pixels.h"

#include <QCoreApplication>
#include <QCache>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>
#include <QTextBoundaryFinder>
#include <QSet>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {
std::vector<FontFace> catalog;
bool catalogValid = false;
QCache<QString, Text> previews(512);

plat_text *cloneText(const plat_text *source) {
	auto *copy = static_cast<plat_text *>(std::malloc(sizeof(plat_text)));
	*copy = *source;
	const size_t size = source->stride * source->height;
	copy->mask = static_cast<uint8_t *>(std::malloc(size));
	std::memcpy(copy->mask, source->mask, size);
	return copy;
}

QString patternString(FcPattern *pattern, const char *key) {
	FcChar8 *value = nullptr;
	return FcPatternGetString(pattern, key, 0, &value) == FcResultMatch ?
		QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString();
}

QStringList userRoots() {
	return { userFontsPath(), QDir::homePath() + "/.fonts" };
}

bool within(const QString &path, const QString &root) {
	return !root.isEmpty() && (path == root || path.startsWith(root + '/'));
}
}

QString FontFace::id() const {
	return QString::fromLatin1(QCryptographicHash::hash(
		file.toUtf8() + ':' + QByteArray::number(index), QCryptographicHash::Sha256).toHex());
}

QString userFontsPath() {
	return QStandardPaths::writableLocation(QStandardPaths::FontsLocation);
}

bool fontUserDirectory(const QString &directory) {
	const QString path = QDir(directory).absolutePath();
	const QString canonical = QFileInfo(path).canonicalFilePath();
	for (const QString &root : userRoots()) {
		if (QFileInfo(root).isSymLink()) {
			continue;
		}
		const QString realRoot = QFileInfo(root).canonicalFilePath();
		if ((within(path, QDir(root).absolutePath()) || within(path, realRoot)) &&
				(canonical.isEmpty() || within(canonical, realRoot))) {
			return true;
		}
	}
	return false;
}

bool fontCanRemove(const QString &file) {
	const QFileInfo info(file);
	return info.isFile() && !info.isSymLink() && info.ownerId() == getuid() &&
		fontUserDirectory(info.absolutePath()) && QFileInfo(info.absolutePath()).isWritable();
}

std::vector<FontFace> fontInspect(const QString &file, QString *error) {
	std::vector<FontFace> result;
	const QFileInfo info(file);
	if (!info.isFile() || !info.isReadable() || info.isSymLink()) {
		if (error) {
			*error = "The font is unavailable or is a symbolic link: " + file;
		}
		return result;
	}
	int count = 0;
	for (int index = 0; index == 0 || index < count; ++index) {
		FcPattern *pattern = FcFreeTypeQuery(
			reinterpret_cast<const FcChar8 *>(QFile::encodeName(file).constData()),
			index, nullptr, &count);
		if (!pattern) {
			if (error) {
				*error = "The font could not be read: " + file;
			}
			return {};
		}
		FontFace face;
		face.file = info.canonicalFilePath();
		face.index = index;
		face.family = patternString(pattern, FC_FAMILY);
		face.style = patternString(pattern, FC_STYLE);
		face.name = patternString(pattern, FC_FULLNAME);
		if (face.name.isEmpty()) {
			face.name = face.family + (face.style.isEmpty() ? QString() : " " + face.style);
		}
		FcPatternDestroy(pattern);
		if (face.family.isEmpty() || face.name.size() > 512 || count > 4096) {
			if (error) {
				*error = "The font has invalid name or collection information: " + file;
			}
			return {};
		}
		result.push_back(std::move(face));
	}
	return result;
}

bool fontCanInstall(const QString &file, QString *error) {
	if (!QStringList{ "ttf", "otf", "ttc", "otc" }.contains(QFileInfo(file).suffix().toLower())) {
		if (error) {
			*error = "Only TrueType/OpenType font files and collections (.ttf, .otf, .ttc, .otc) "
				"can be installed: " + file;
		}
		return false;
	}
	QFile source(file);
	const QByteArray signature = source.open(QIODevice::ReadOnly) ? source.read(4) : QByteArray();
	if (signature != QByteArray::fromHex("00010000") && signature != "OTTO" &&
			signature != "ttcf" && signature != "true") {
		if (error) {
			*error = "The file does not contain a TrueType/OpenType font: " + file;
		}
		return false;
	}
	return !fontInspect(file, error).empty();
}

const std::vector<FontFace> &fontCatalog() {
	if (catalogValid) {
		return catalog;
	}
	catalog.clear();
	if (!FcInit() || !FcConfigBuildFonts(FcConfigGetCurrent())) {
		qWarning() << "The font configuration could not be refreshed.";
	}
	FcPattern *pattern = FcPatternCreate();
	FcObjectSet *objects = FcObjectSetBuild(FC_FILE, FC_INDEX, FC_FAMILY, FC_STYLE, FC_FULLNAME, nullptr);
	FcFontSet *fonts = FcFontList(nullptr, pattern, objects);
	QSet<QString> seen;
	if (fonts) {
		for (int i = 0; i < fonts->nfont; ++i) {
			FcPattern *p = fonts->fonts[i];
			FontFace face;
			face.file = QFileInfo(patternString(p, FC_FILE)).canonicalFilePath();
			FcPatternGetInteger(p, FC_INDEX, 0, &face.index);
			// Fontconfig encodes named variable instances in the high bits.
			face.family = patternString(p, FC_FAMILY);
			face.style = patternString(p, FC_STYLE);
			face.name = patternString(p, FC_FULLNAME);
			if (face.name.isEmpty()) {
				face.name = face.family + (face.style.isEmpty() ? QString() : " " + face.style);
			}
			if (!face.file.isEmpty() && !face.name.isEmpty() && face.name.size() <= 512 &&
					!seen.contains(face.id())) {
				seen.insert(face.id());
				catalog.push_back(std::move(face));
			}
		}
		FcFontSetDestroy(fonts);
	} else {
		qWarning() << "The installed font catalog could not be read.";
	}
	FcObjectSetDestroy(objects);
	FcPatternDestroy(pattern);
	std::sort(catalog.begin(), catalog.end(), [](const FontFace &a, const FontFace &b) {
		return QString::localeAwareCompare(a.name, b.name) < 0;
	});
	catalogValid = true;
	return catalog;
}

void fontInvalidate() {
	catalogValid = false;
	previews.clear();
}

void fontWatchChanges(const std::function<void()> &changed) {
	auto *watcher = new QFileSystemWatcher(QCoreApplication::instance());
	auto *timer = new QTimer(watcher);
	timer->setSingleShot(true);
	timer->setInterval(500);
	const auto arm = [watcher] {
		QStringList roots = userRoots();
		FcStrList *dirs = FcConfigGetFontDirs(nullptr);
		while (FcChar8 *dir = FcStrListNext(dirs)) {
			roots << QString::fromUtf8(reinterpret_cast<const char *>(dir));
		}
		FcStrListDone(dirs);
		QSet<QString> paths;
		for (QString root : roots) {
			const QString requested = root;
			while (!QDir(root).exists() && root != "/") {
				root = QFileInfo(root).absolutePath();
			}
			paths.insert(root);
			if (root != requested) {
				continue;
			}
			QDirIterator it(root, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot |
				QDir::NoSymLinks, QDirIterator::Subdirectories);
			while (it.hasNext() && paths.size() < 8192) {
				paths.insert(it.next());
			}
			if (paths.size() >= 8192) {
				qWarning() << "Font source watch limit reached; reopen Fonts to rescan unwatched sources.";
			}
		}
		const QStringList directories = watcher->directories(), files = watcher->files();
		QSet<QString> watched(directories.begin(), directories.end());
		watched.unite(QSet<QString>(files.begin(), files.end()));
		for (const QString &path : paths) {
			if (!watched.contains(path) && !watcher->addPath(path)) {
				qWarning() << "Cannot watch font source:" << path;
			}
		}
	};
	QObject::connect(timer, &QTimer::timeout, watcher, [arm, changed] {
		fontInvalidate();
		arm();
		changed();
	});
	QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, timer,
		[timer] { timer->start(); });
	QObject::connect(watcher, &QFileSystemWatcher::fileChanged, timer,
		[timer] { timer->start(); });
	arm();
}

plat_text *fontRenderName(const QString &name, int maxWidth, const QString &file, int index) {
	const QString cacheKey = file + '\n' + QString::number(index) + '\n' +
		QString::number(maxWidth) + '\n' + name;
	if (const Text *cached = previews.object(cacheKey)) {
		return cloneText(cached->t);
	}
	FT_Library library = nullptr;
	FT_Face face = nullptr;
	if (FT_Init_FreeType(&library) || FT_New_Face(library, QFile::encodeName(file).constData(), index, &face)) {
		qWarning() << "Cannot load font preview:" << file << index;
		if (library) {
			FT_Done_FreeType(library);
		}
		return text_render_font(name.toUtf8().constData(), maxWidth, PL_FONT_VIEWS);
	}
	if (FT_Set_Pixel_Sizes(face, 0, 12)) {
		qWarning() << "Cannot set font preview size:" << file << index;
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return text_render_font(name.toUtf8().constData(), maxWidth, PL_FONT_VIEWS);
	}
	hb_font_t *font = hb_ft_font_create_referenced(face);
	struct Glyph {
		int x, y, width, height;
		QByteArray mask;
	};
	const auto render = [&](const QString &text) {
		hb_buffer_t *buffer = hb_buffer_create();
		const QByteArray utf8 = text.toUtf8();
		hb_buffer_add_utf8(buffer, utf8.constData(), utf8.size(), 0, utf8.size());
		hb_buffer_guess_segment_properties(buffer);
		hb_shape(font, buffer, nullptr, 0);
		unsigned count = 0;
		const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
		const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, nullptr);
		std::vector<Glyph> glyphs;
		int penX = 0, penY = 0, left = 0, right = 0, top = -10, bottom = 3;
		const auto addGlyph = [&](Glyph g) {
			left = std::min(left, g.x);
			right = std::max(right, g.x + g.width);
			top = std::min(top, g.y);
			bottom = std::max(bottom, g.y + g.height);
			glyphs.push_back(std::move(g));
		};
		for (unsigned i = 0; i < count; ++i) {
			int advanceX = positions[i].x_advance;
			if (!infos[i].codepoint) {
				const auto codepoints = QString::fromUtf8(utf8.mid(infos[i].cluster)).toUcs4();
				const char32_t codepoint = codepoints.isEmpty() ? U'?' :
					static_cast<char32_t>(codepoints.front());
				const QString character = QString::fromUcs4(&codepoint, 1);
				Text fallback(character, 1000, PL_FONT_VIEWS);
				if (fallback.t) {
					const auto *t = fallback.t;
					addGlyph({ penX / 64, -t->baseline, t->width, t->height,
						QByteArray(reinterpret_cast<const char *>(t->mask), t->stride * t->height) });
					advanceX = t->advance * 64;
				}
			} else if (!FT_Load_Glyph(face, infos[i].codepoint, FT_LOAD_DEFAULT) &&
					!FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO)) {
				const FT_GlyphSlot slot = face->glyph;
				const FT_Bitmap &bitmap = slot->bitmap;
				Glyph g{ static_cast<int>(std::lround((penX + positions[i].x_offset) / 64.0)) + slot->bitmap_left,
					-static_cast<int>(std::lround((penY + positions[i].y_offset) / 64.0)) - slot->bitmap_top,
					static_cast<int>(bitmap.width), static_cast<int>(bitmap.rows), {} };
				g.mask.resize(g.width * g.height);
				for (int y = 0; y < g.height; ++y) {
					const unsigned char *row = bitmap.buffer +
						(bitmap.pitch >= 0 ? y : g.height - 1 - y) * std::abs(bitmap.pitch);
					for (int x = 0; x < g.width; ++x) {
						g.mask[y * g.width + x] = (row[x / 8] & (0x80 >> (x % 8))) ? 255 : 0;
					}
				}
				addGlyph(std::move(g));
			} else {
				qWarning() << "Cannot render font preview glyph:" << file << infos[i].codepoint;
			}
			penX += advanceX;
			penY += positions[i].y_advance;
		}
		hb_buffer_destroy(buffer);
		auto *result = static_cast<plat_text *>(std::calloc(1, sizeof(plat_text)));
		result->width = result->stride = std::max(1, right - left);
		result->height = bottom - top;
		result->baseline = -top;
		result->advance = penX / 64;
		result->ink_l = -1;
		result->ink_r = -1;
		result->mask = static_cast<uint8_t *>(std::calloc(result->width * result->height, 1));
		for (const Glyph &g : glyphs) {
			for (int y = 0; y < g.height; ++y) {
				for (int x = 0; x < g.width; ++x) {
					if (static_cast<unsigned char>(g.mask[y * g.width + x])) {
						const int dx = g.x - left + x;
						result->mask[(g.y - top + y) * result->stride + dx] = 255;
						result->ink_l = result->ink_l < 0 ? dx : std::min(result->ink_l, dx);
						result->ink_r = std::max(result->ink_r, dx);
					}
				}
			}
		}
		return result;
	};
	plat_text *result = render(name);
	if (result->width > maxWidth) {
		QStringList clusters;
		QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, name);
		int start = 0, end;
		while ((end = boundaries.toNextBoundary()) >= 0) {
			clusters << name.mid(start, end - start);
			start = end;
		}
		const auto shortened = [&](int keep) {
			return clusters.mid(0, (keep + 1) / 2).join(QString()) +
				QStringLiteral("…") + clusters.mid(clusters.size() - keep / 2).join(QString());
		};
		int low = 0, high = std::max(0, static_cast<int>(clusters.size()) - 1);
		while (low < high) {
			const int keep = (low + high + 1) / 2;
			plat_text *probe = render(shortened(keep));
			const bool fits = probe->width <= maxWidth;
			text_destroy(probe);
			if (fits) {
				low = keep;
			} else {
				high = keep - 1;
			}
		}
		text_destroy(result);
		result = render(shortened(low));
		if (result->width > maxWidth) {
			const int width = std::max(1, maxWidth);
			result->ink_l = result->ink_r = -1;
			for (int y = 0; y < result->height; ++y) {
				for (int x = 0; x < result->width; ++x) {
					uint8_t &pixel = result->mask[y * result->stride + x];
					if (x >= maxWidth) {
						pixel = 0;
					} else if (pixel) {
						result->ink_l = result->ink_l < 0 ? x : std::min(result->ink_l, x);
						result->ink_r = std::max(result->ink_r, x);
					}
				}
			}
			result->width = width;
		}
	}
	hb_font_destroy(font);
	FT_Done_Face(face);
	FT_Done_FreeType(library);
	auto *cached = new Text;
	cached->t = cloneText(result);
	previews.insert(cacheKey, cached);
	return result;
}
