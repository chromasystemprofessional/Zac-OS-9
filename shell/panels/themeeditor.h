#pragma once

#include <QImage>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <vector>

#include "customthemes.h"
#include "panelkit.h"

class ThemeArtworkWindow;

/*
 * The Theme Editor: one window for a new theme, an imported Mac OS 9
 * theme (saved as a linked copy, so the import stays untouched), or a
 * theme the editor wrote. Colours, desktop art, sounds and the program
 * font are edited together; Save writes the bundle and its preset and
 * applies them at once.
 */
class ThemeEditorWindow : public QWidget {
public:
	/* -1 makes a new theme from the current appearance settings. */
	explicit ThemeEditorWindow(int presetIndex, QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	struct ColourWell {
		QRect rect;
		QString key;
		QString label;
		QString hint;
	};
	void buildDraft(int presetIndex);
	void buildPaletteControls();
	void buildAppearanceControls();
	void loadValues();
	void fillPalette(bool dark);
	void deriveSecondaryColours();
	void paintWell(pl_canvas *c, const ColourWell &well, int baseline) const;
	void paintPreview(pl_canvas *c, const QRect &rect) const;
	void chooseColour(const ColourWell &well);
	void markDirty();
	void save();
	void cancel();
	bool confirmDiscard();
	void openArtwork();
	struct Snapshot {
		ThemeDraft draft;
		QString name;
	};
	void pushUndo(const QString &name = QString());
	void undo();
	void redo();
	void restoreSnapshot(const Snapshot &snapshot);

	QString value(const QString &key) const { return m_draft.chrome.value(key); }
	uint32_t colour(const QString &key) const;
	void setValue(const QString &key, const QString &v);

	ThemeDraft m_draft;
	int m_source = -1;
	QString m_sourceName;
	QString m_lastName;
	bool m_dirty = false;
	bool m_saved = false;
	QString m_error;
	std::vector<Snapshot> m_undo, m_redo;
	QPointer<ThemeArtworkWindow> m_artworkWindow;

	PanelHost m_host{ this };
	PanelEdit m_name;
	PanelButton m_light, m_dark, m_delete, m_duplicate, m_artwork, m_cancel, m_save;
	PanelPopup m_accent, m_highlight, m_pattern, m_wallpaper, m_placement;
	PanelPopup m_soundSet, m_alert, m_font;
	std::vector<ColourWell> m_wells;

	QStringList m_accentIds, m_highlightValues, m_patternIds, m_wallpaperIds;
	QStringList m_soundSetIds, m_alertIds, m_fontIds;

	QRect m_previewRect;
};

/*
 * The per-slot window artwork window. It edits the parent's draft in place
 * and asks the parent to record undo steps and repaint the preview.
 */
class ThemeArtworkWindow : public QWidget {
public:
	ThemeArtworkWindow(ThemeDraft *draft, std::function<void()> begin,
		std::function<void()> changed, std::function<void()> undo,
		std::function<void()> redo, QWidget *parent = nullptr);
	void reload();
	void selectSlot(const QString &key);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	void refreshList();
	void loadPreview();
	const ThemeArtSlot &currentSlot() const { return m_slots.at(m_selected); }
	void useImported();
	void usePlatinum();
	void chooseImage();
	void paintPreviewArt(pl_canvas *c, const QRect &rect) const;

	ThemeDraft *m_draft = nullptr;
	std::function<void()> m_begin, m_changed, m_undo, m_redo;
	QList<ThemeArtSlot> m_slots;

	PanelHost m_host{ this };
	PanelList m_list;
	PanelButton m_imported, m_platinum, m_choose, m_done;
	PanelPopup m_all;
	QRect m_previewRect;
	QImage m_preview;
	QString m_info;
	int m_selected = 0;
};
