#pragma once

#include "core/output-manager.hpp"

#include <QRectF>
#include <QWidget>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QTimer;
class QToolButton;

namespace dsk {

class VerticalPreviewWidget;

#ifdef DSK_INCLUDE_E2E_HOOKS
struct VerticalPreviewDiagnostics {
	std::uint64_t callbackCount = 0;
	std::uint64_t renderedSnapshotCount = 0;
	std::uint64_t renderedGenerationCount = 0;
	std::uint32_t sceneWidth = 0;
	std::uint32_t sceneHeight = 0;
};

VerticalPreviewDiagnostics verticalPreviewDiagnostics();
void resetVerticalPreviewDiagnostics();
#endif

class VerticalLayoutEditor : public QWidget {
	Q_OBJECT

public:
	explicit VerticalLayoutEditor(OutputManager *manager, QWidget *parent = nullptr);
	void prepareForUnload();
	void handleSceneCollectionChanged();
#ifdef DSK_INCLUDE_E2E_HOOKS
	bool exercisePreviewCanvasReplacementForTest();
	bool exerciseSourceVisibilityToggleForTest();
	bool exerciseSetupVisibilityToggleForTest();
#endif

private slots:
	void refreshSourceList();
	void refreshSceneList();
	void addItem();
	void removeItem();
	void moveItemUp();
	void moveItemDown();
	void createScene();
	void removeScene();
	void renameScene();
	void moveSceneUp();
	void moveSceneDown();
	void selectVerticalScene(int row);
	void addSceneLink();
	void removeSceneLink();
	void selectSceneLink(int row);
	void selectSceneLinkForObsScene(const QString &sceneName);
	void updateFollowScene(int state);
	void centerSelectedItem();
	void fitSelectedItemToCanvas();
	void fillSelectedItemToCanvas();
	void selectItem(int row);
	void updateItemVisibility(QListWidgetItem *item);
	void updateSelectedItem();
	void refreshItems();

private:
	int selectedIndex() const;
	void setSelectedItemRect(const QRectF &rect);
	void scheduleSave();
	void saveNow();
	void updateItemRectFromPreview(int row, const QRectF &rect, bool save);
	void setLayerControlsEnabled(bool enabled);
	void updateSceneStatus();
	void updateObsLinkStatus();
	void setSetupVisible(bool visible, bool persist);

	OutputManager *manager_ = nullptr;
	QCheckBox *followScene_ = nullptr;
	QComboBox *sceneLinkScene_ = nullptr;
	QComboBox *sceneLinkTemplate_ = nullptr;
	QLabel *obsLinkStatus_ = nullptr;
	QListWidget *verticalScenes_ = nullptr;
	QListWidget *sceneLinks_ = nullptr;
	QLabel *activeSceneStatus_ = nullptr;
	QLabel *sourcesHeader_ = nullptr;
	QComboBox *source_ = nullptr;
	QListWidget *items_ = nullptr;
	QSpinBox *x_ = nullptr;
	QSpinBox *y_ = nullptr;
	QSpinBox *w_ = nullptr;
	QSpinBox *h_ = nullptr;
	QComboBox *fit_ = nullptr;
	QCheckBox *visible_ = nullptr;
	QCheckBox *snapping_ = nullptr;
	QToolButton *setupToggle_ = nullptr;
	QToolButton *transformToggle_ = nullptr;
	QToolButton *obsLinksToggle_ = nullptr;
	QWidget *setupPanel_ = nullptr;
	VerticalPreviewWidget *preview_ = nullptr;
	QTimer *saveTimer_ = nullptr;
	int setupSelectedRow_ = -1;
	bool loading_ = false;
};

} // namespace dsk
