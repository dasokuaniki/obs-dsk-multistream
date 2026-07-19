#include "ui/vertical-layout-editor.hpp"

#include "core/layout-manager.hpp"
#include "core/vertical-layout-geometry.hpp"
#include "core/vertical-scene-builder.hpp"
#include "ui/layout-widget-utils.hpp"
#include "ui/vertical-layout-metrics.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMutex>
#include <QListWidget>
#include <QPaintEngine>
#include <QPainter>
#include <QHideEvent>
#include <QResizeEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

#include <graphics/graphics.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace dsk {

namespace {

std::atomic<std::uint64_t> previewCallbackCount{0};
std::atomic<std::uint64_t> previewRenderedSnapshotCount{0};
std::atomic<std::uint64_t> previewRenderedGenerationCount{0};
std::atomic<std::uint64_t> previewLastRenderedGeneration{0};
std::atomic<std::uint32_t> previewSceneWidth{0};
std::atomic<std::uint32_t> previewSceneHeight{0};

QString itemLabel(const VerticalLayoutItem &item, int index)
{
	return item.sourceName.isEmpty() ? QString("Source %1").arg(index + 1) : item.sourceName;
}

QString currentFrontendSceneName()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene)
		return {};
	const char *name = obs_source_get_name(scene);
	const QString result = name ? QString::fromUtf8(name) : QString();
	obs_source_release(scene);
	return result;
}

bool sceneLinkMatches(const SceneLayoutLink &link, const QString &sceneName, const QString &sceneUuid)
{
	if (!link.sceneUuid.trimmed().isEmpty())
		return !sceneUuid.isEmpty() && link.sceneUuid.trimmed() == sceneUuid;
	return link.sceneName.trimmed() == sceneName.trimmed();
}

constexpr int SceneLinkNameRole = Qt::UserRole;
constexpr int SceneLinkUuidRole = Qt::UserRole + 1;
constexpr int SceneLinkVerticalSceneIdRole = Qt::UserRole + 2;
constexpr int SceneLinkResolvedNameRole = Qt::UserRole + 3;

constexpr const char *VerticalUiConfigSection = "DSKVerticalLayout";
constexpr const char *SetupVisibleConfigKey = "SetupVisible";

bool verticalSetupVisiblePreference()
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return true;
	config_set_default_bool(config, VerticalUiConfigSection, SetupVisibleConfigKey, true);
	return config_get_bool(config, VerticalUiConfigSection, SetupVisibleConfigKey);
}

void saveVerticalSetupVisiblePreference(bool visible)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return;
	config_set_bool(config, VerticalUiConfigSection, SetupVisibleConfigKey, visible);
	config_save_safe(config, "tmp", "bak");
}

QSize pixelSize(QWidget *widget)
{
	const double ratio = widget->devicePixelRatioF();
	return QSize(std::max(1, int(std::lround(widget->width() * ratio))),
		     std::max(1, int(std::lround(widget->height() * ratio))));
}

QRectF scaledCanvasRect(const QSize &size, const VerticalLayout &layout)
{
	if (layout.width <= 0 || layout.height <= 0 || size.width() <= 0 || size.height() <= 0)
		return QRectF();

	const double scale = std::min(size.width() / double(layout.width), size.height() / double(layout.height));
	const QSizeF canvasSize(layout.width * scale, layout.height * scale);
	return QRectF((size.width() - canvasSize.width()) / 2.0, (size.height() - canvasSize.height()) / 2.0,
		      canvasSize.width(), canvasSize.height());
}

QColor colorForSource(const QString &sourceName)
{
	const size_t hash = qHash(sourceName);
	return QColor::fromHsv(int(hash % 360), 120, 210);
}

QSizeF sourceVideoSize(const QString &sourceName)
{
	if (sourceName.isEmpty())
		return {};

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return {};

	const QSizeF size(obs_source_get_width(source), obs_source_get_height(source));
	obs_source_release(source);
	return size;
}

void drawSolidRect(float x, float y, float width, float height, const QColor &color)
{
	if (width <= 0.0f || height <= 0.0f)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
	if (!colorParam)
		return;
	vec4 colorValue;
	vec4_set(&colorValue, color.redF(), color.greenF(), color.blueF(), color.alphaF());
	gs_effect_set_vec4(colorParam, &colorValue);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, uint32_t(std::lround(width)), uint32_t(std::lround(height)));
	gs_matrix_pop();
}

void drawOutlineRect(const QRectF &rect, float thickness, const QColor &color)
{
	drawSolidRect(float(rect.left()), float(rect.top()), float(rect.width()), thickness, color);
	drawSolidRect(float(rect.left()), float(rect.bottom() - thickness), float(rect.width()), thickness, color);
	drawSolidRect(float(rect.left()), float(rect.top()), thickness, float(rect.height()), color);
	drawSolidRect(float(rect.right() - thickness), float(rect.top()), thickness, float(rect.height()), color);
}

void drawHandle(float x, float y, float size, const QColor &color)
{
	drawSolidRect(x - size / 2.0f, y - size / 2.0f, size, size, color);
}

} // namespace

VerticalPreviewDiagnostics verticalPreviewDiagnostics()
{
	return {
		previewCallbackCount.load(std::memory_order_relaxed),
		previewRenderedSnapshotCount.load(std::memory_order_relaxed),
		previewRenderedGenerationCount.load(std::memory_order_relaxed),
		previewSceneWidth.load(std::memory_order_relaxed),
		previewSceneHeight.load(std::memory_order_relaxed),
	};
}

void resetVerticalPreviewDiagnostics()
{
	previewCallbackCount.store(0, std::memory_order_relaxed);
	previewRenderedSnapshotCount.store(0, std::memory_order_relaxed);
	previewRenderedGenerationCount.store(0, std::memory_order_relaxed);
	previewLastRenderedGeneration.store(0, std::memory_order_relaxed);
	previewSceneWidth.store(0, std::memory_order_relaxed);
	previewSceneHeight.store(0, std::memory_order_relaxed);
}

class VerticalRenderWidget : public QWidget {
public:
	explicit VerticalRenderWidget(QWidget *parent = nullptr)
		: QWidget(parent), sceneBuilder_(QStringLiteral("DSK Vertical Preview"))
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_StaticContents);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAttribute(Qt::WA_DontCreateNativeAncestors);
		setAttribute(Qt::WA_NativeWindow);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	~VerticalRenderWidget() override { prepareForUnload(); }

	QPaintEngine *paintEngine() const override { return nullptr; }

	void prepareForUnload()
	{
		setSceneSource(nullptr);
		destroyDisplay();
		sceneBuilder_.release();
#ifdef DSK_ENABLE_OBS_CANVAS_API
		if (previewCanvas_) {
			obs_canvas_release(previewCanvas_);
			previewCanvas_ = nullptr;
		}
#endif
		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		sceneDirty_ = true;
	}

	void handleSceneCollectionChanged()
	{
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			sceneDirty_ = true;
		}
		createDisplay();
		rebuildSceneIfVisible();
		update();
	}

	bool exerciseCanvasReplacementForTest()
	{
#ifdef DSK_ENABLE_OBS_CANVAS_API
		createDisplay();
		rebuildSceneIfVisible();
		if (!previewCanvas_)
			return false;

		obs_canvas_remove(previewCanvas_);
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			sceneDirty_ = true;
		}
		rebuildSceneIfVisible();
		return previewCanvas_ && !obs_canvas_removed(previewCanvas_);
#else
		return true;
#endif
	}

	void setLayoutData(const VerticalLayout &layout)
	{
		QVector<bool> itemHasVideo;
		QVector<QRectF> itemDisplayRects;
		itemHasVideo.reserve(layout.items.size());
		itemDisplayRects.reserve(layout.items.size());
		for (const auto &item : layout.items) {
			const QSizeF sourceSize = sourceVideoSize(item.sourceName);
			itemHasVideo.push_back(sourceSize.width() > 0.0 && sourceSize.height() > 0.0);
			itemDisplayRects.push_back(displayedContentRect(item, sourceSize));
		}

		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		layout_ = layout;
		sceneDirty_ = true;
		itemHasVideo_ = std::move(itemHasVideo);
		itemDisplayRects_ = std::move(itemDisplayRects);
		++layoutGeneration_;
		locker.unlock();
		rebuildSceneIfVisible();
	}

	void setSelectedIndex(int index)
	{
		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		selectedIndex_ = index >= 0 && index < layout_.items.size() ? index : -1;
	}

	void setSnapGuides(const QVector<double> &xGuides, const QVector<double> &yGuides)
	{
		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		snapXGuides_ = xGuides;
		snapYGuides_ = yGuides;
	}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		createDisplay();
		QWidget::paintEvent(event);
	}

	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		createDisplay();
		if (display_) {
			const QSize size = pixelSize(this);
			obs_display_resize(display_, uint32_t(size.width()), uint32_t(size.height()));
		}
	}

	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		createDisplay();
		rebuildSceneIfVisible();
	}

	void hideEvent(QHideEvent *event) override
	{
		prepareForUnload();
		QWidget::hideEvent(event);
	}

private:
	struct RenderSnapshot {
		VerticalLayout layout;
		QVector<bool> itemHasVideo;
		QVector<QRectF> itemDisplayRects;
		QVector<double> snapXGuides;
		QVector<double> snapYGuides;
		obs_source_t *sceneSource = nullptr;
		int selectedIndex = -1;
		std::uint64_t layoutGeneration = 0;
	};

	bool tryTakeRenderSnapshot(RenderSnapshot &snapshot)
	{
		// The OBS graphics thread already owns the graphics mutex while this
		// callback runs. Never wait for the UI mutex here: the UI may be
		// rebuilding scene items while waiting for that graphics mutex.
		if (!renderStateMutex_.tryLock())
			return false;

		const bool valid = sceneSource_ && layout_.width > 0 && layout_.height > 0;
		if (valid) {
			snapshot.layout = layout_;
			snapshot.itemHasVideo = itemHasVideo_;
			snapshot.itemDisplayRects = itemDisplayRects_;
			snapshot.snapXGuides = snapXGuides_;
			snapshot.snapYGuides = snapYGuides_;
			snapshot.selectedIndex = selectedIndex_;
			snapshot.layoutGeneration = layoutGeneration_;
			snapshot.sceneSource = obs_source_get_ref(sceneSource_);
		}
		renderStateMutex_.unlock();
		return valid && snapshot.sceneSource;
	}

	static void drawPreview(void *param, uint32_t cx, uint32_t cy)
	{
		previewCallbackCount.fetch_add(1, std::memory_order_relaxed);
		auto *self = static_cast<VerticalRenderWidget *>(param);
		if (!self || cx == 0 || cy == 0)
			return;

		RenderSnapshot snapshot;
		if (!self->tryTakeRenderSnapshot(snapshot))
			return;

		const double scale = std::min(cx / double(snapshot.layout.width), cy / double(snapshot.layout.height));
		if (scale <= 0.0) {
			obs_source_release(snapshot.sceneSource);
			return;
		}
		const int viewportWidth = int(std::lround(snapshot.layout.width * scale));
		const int viewportHeight = int(std::lround(snapshot.layout.height * scale));
		const int viewportX = int(cx - viewportWidth) / 2;
		const int viewportY = int(cy - viewportHeight) / 2;

		gs_viewport_push();
		gs_projection_push();
		const bool previous = gs_set_linear_srgb(true);
		gs_ortho(0.0f, float(snapshot.layout.width), 0.0f, float(snapshot.layout.height), -100.0f, 100.0f);
		gs_set_viewport(viewportX, viewportY, viewportWidth, viewportHeight);
		obs_source_video_render(snapshot.sceneSource);
		previewRenderedSnapshotCount.fetch_add(1, std::memory_order_relaxed);
		const std::uint64_t previousGeneration =
			previewLastRenderedGeneration.exchange(snapshot.layoutGeneration, std::memory_order_relaxed);
		if (previousGeneration != snapshot.layoutGeneration)
			previewRenderedGenerationCount.fetch_add(1, std::memory_order_relaxed);
		self->drawOverlay(snapshot, float(scale));
		gs_set_linear_srgb(previous);
		gs_projection_pop();
		gs_viewport_pop();
		obs_source_release(snapshot.sceneSource);
	}

	void drawOverlay(const RenderSnapshot &snapshot, float displayScale)
	{
		const auto &layout = snapshot.layout;
		const float line = std::max(1.0f, 2.0f / displayScale);
		const float handleSize = std::max(18.0f, 10.0f / displayScale);
		const QColor selected(238, 242, 248, 235);
		const QColor inactiveOutline(190, 196, 205, 55);
		const QColor handleFill(248, 250, 252, 245);
		const QColor handleBorder(20, 24, 32, 220);
		const QColor snapGuide(90, 180, 255, 155);

		// Match the clean OBS main preview when Setup is closed. A cleared
		// selection means the user is not editing, so no source or canvas
		// outlines should remain visible around the program image.
		if (snapshot.selectedIndex < 0)
			return;

		for (const double x : snapshot.snapXGuides)
			drawSolidRect(float(x) - line / 2.0f, 0.0f, line, float(layout.height), snapGuide);
		for (const double y : snapshot.snapYGuides)
			drawSolidRect(0.0f, float(y) - line / 2.0f, float(layout.width), line, snapGuide);

		for (int i = 0; i < layout.items.size(); ++i) {
			const auto &item = layout.items[i];
			if (!item.visible)
				continue;

			QColor sourceColor = i == snapshot.selectedIndex ? selected : colorForSource(item.sourceName);
			if (i != snapshot.selectedIndex)
				sourceColor = inactiveOutline;
			const bool hasVideo = i < snapshot.itemHasVideo.size() && snapshot.itemHasVideo[i];
			if (!hasVideo) {
				QColor fill = colorForSource(item.sourceName);
				fill.setAlpha(55);
				drawSolidRect(float(item.rect.x()), float(item.rect.y()), float(item.rect.width()),
					      float(item.rect.height()), fill);
			}
			const QRectF visibleRect =
				i < snapshot.itemDisplayRects.size() ? snapshot.itemDisplayRects[i] : item.rect;
			drawOutlineRect(visibleRect, i == snapshot.selectedIndex ? line * 1.5f : line, sourceColor);

			if (i != snapshot.selectedIndex)
				continue;

			const QRectF r = visibleRect;
			const auto drawObsHandle = [&](float x, float y) {
				drawHandle(x, y, handleSize + line * 2.0f, handleBorder);
				drawHandle(x, y, handleSize, handleFill);
			};
			drawObsHandle(float(r.left()), float(r.top()));
			drawObsHandle(float(r.center().x()), float(r.top()));
			drawObsHandle(float(r.right()), float(r.top()));
			drawObsHandle(float(r.left()), float(r.center().y()));
			drawObsHandle(float(r.right()), float(r.center().y()));
			drawObsHandle(float(r.left()), float(r.bottom()));
			drawObsHandle(float(r.center().x()), float(r.bottom()));
			drawObsHandle(float(r.right())×ŽùâÚ$z{-®éÜj×æTÆ–æµfW'F–6Å66VæT–E&öÆR’“° ––b‡fW'F–6Å66VæT–æFW‚ãÒ —66VæTÆ–æµFV×ÆFUòÓç6WD7W'&VçD–æFW‚‡fW'F–6Å66VæT–æFW‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§6VÆV7E66VæTÆ–æ´f÷$ö'566VæR†6öç7B7G&–ærg66VæTæÖR§° ––b†ÆöF–æuòÇÂ66VæTÆ–æ·5ò —&WGW&ã°  –f÷"†–çB&÷rÒ²&÷rÂ66VæTÆ–æ·5òÓæ6÷VçB‚“²²·&÷r’° –6öç7BÆ—7Ev–FvWD—FVÒ¦—FVÒÒ66VæTÆ–æ·5òÓæ—FVÒ‡&÷r“° ––b†—FVÒbb—FVÒÓæFF…66VæTÆ–æµ&W6öÇfVDæÖU&öÆR’çFõ7G&–ær‚’ÓÒ66VæTæÖR’° —66VæTÆ–æ·5òÓç6WD7W'&VçE&÷r‡&÷r“° —6VÆV7E66VæTÆ–æ²‡&÷r“° —&WGW&ã° —Ð —Ð  —66VæTÆ–æ·5òÓç6WD7W'&VçE&÷r‚Ó“° —66VæTÆ–æ·5òÓæ6ÆV%6VÆV7F–öâ‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§WFFTföÆÆ÷u66VæR†–çB7FFR§° –ÖævW%òÓç6WDföÆÆ÷tö'566VæR‡7FFRÓÒC£¤6†V6¶VB“° —WFFTö'4Æ–æµ7FGW2‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£¦6VçFW%6VÆV7FVD—FVÒ‚§° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° –6öç7BWFòfÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b†–æFW‚ÂÇÂ–æFW‚ãÒÆ–÷WBæ—FV×2ç6—¦R‚’ —&WGW&ã°  •&V7Db&V7BÒF—7Æ–VD6öçFVçE&V7B†Æ–÷WBæ—FV×5¶–æFW…ÒÂ6÷W&6Uf–FVõ6—¦R†Æ–÷WBæ—FV×5¶–æFW…Òç6÷W&6TæÖR’“° —&V7BæÖ÷fT6VçFW"…ö–çDb†Æ–÷WBçv–GF‚ò"ãÂÆ–÷WBæ†V–v‡Bò"ã’“° —6WE6VÆV7FVD—FVÕ&V7B‡&V7B“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£¦f—E6VÆV7FVD—FVÕFô6çf2‚§° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° •fW'F–6ÄÆ–÷WBÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b†–æFW‚ÂÇÂ–æFW‚ãÒÆ–÷WBæ—FV×2ç6—¦R‚’ —&WGW&ã°  •6—¦Tb6÷W&6U6—¦RÒ6÷W&6Uf–FVõ6—¦R†Æ–÷WBæ—FV×5¶–æFW…Òç6÷W&6TæÖR“° —6÷W&6U6—¦RÒ7&÷VE6÷W&6U6—¦R†Æ–÷WBæ—FV×5¶–æFW…ÒÂ6÷W&6U6—¦R“° –Æ–÷WBæ—FV×5¶–æFW…Òæf—DÖöFRÒf—DÖöFS£¤f—C° –Æ–÷WBæ—FV×5¶–æFW…Òç&V7BÒ6VçFW&VD7V7Df—E&V7B‡6÷W&6U6—¦RÂ6—¦Tb†Æ–÷WBçv–GF‚ÂÆ–÷WBæ†V–v‡B’“° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° —6fTæ÷r‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r†–æFW‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£¦f–ÆÅ6VÆV7FVD—FVÕFô6çf2‚§° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° •fW'F–6ÄÆ–÷WBÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b†–æFW‚ÂÇÂ–æFW‚ãÒÆ–÷WBæ—FV×2ç6—¦R‚’ —&WGW&ã°  •6—¦Tb6÷W&6U6—¦RÒ6÷W&6Uf–FVõ6—¦R†Æ–÷WBæ—FV×5¶–æFW…Òç6÷W&6TæÖR“° —6÷W&6U6—¦RÒ7&÷VE6÷W&6U6—¦R†Æ–÷WBæ—FV×5¶–æFW…ÒÂ6÷W&6U6—¦R“° •&V7Db&V7BƒÂÂÆ–÷WBçv–GF‚ÂÆ–÷WBæ†V–v‡B“° ––b‡6÷W&6U6—¦Rçv–GF‚‚’âãbb6÷W&6U6—¦Ræ†V–v‡B‚’âã’° –6öç7BF÷V&ÆR66ÆRÒ7FC£¦Ö‚†Æ–÷WBçv–GF‚ò6÷W&6U6—¦Rçv–GF‚‚’ÂÆ–÷WBæ†V–v‡Bò6÷W&6U6—¦Ræ†V–v‡B‚’“° –6öç7B6—¦Tb6—¦R‡6÷W&6U6—¦Rçv–GF‚‚’¢66ÆRÂ6÷W&6U6—¦Ræ†V–v‡B‚’¢66ÆR“° —&V7BÒ&V7Db‚†Æ–÷WBçv–GF‚Ò6—¦Rçv–GF‚‚’’ò"ãÀ ’†Æ–÷WBæ†V–v‡BÒ6—¦Ræ†V–v‡B‚’’ò"ãÀ ’6—¦Rçv–GF‚‚’À ’6—¦Ræ†V–v‡B‚’“° —Ð –Æ–÷WBæ—FV×5¶–æFW…Òæf—DÖöFRÒf—DÖöFS£¤f–ÆÃ° –Æ–÷WBæ—FV×5¶–æFW…Òç&V7BÒ&V7C° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° —6fTæ÷r‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r†–æFW‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£¦FD—FVÒ‚§° –6öç7B7G&–ær6÷W&6TæÖRÒ6÷W&6UòÓæ7W'&VçEFW‡B‚’çG&–ÖÖVB‚“° ––b‡6÷W&6TæÖRæ—4V×G’‚’ —&WGW&ã°  •fW'F–6ÄÆ–÷WBÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° •fW'F–6ÄÆ–÷WD—FVÒ—FVÓ° –—FVÒæ–BÒæWuF&vWD–B‚“° –—FVÒç6÷W&6TæÖRÒ6÷W&6TæÖS° –—FVÒæf—DÖöFRÒf—DÖöFS£¤f—C° –—FVÒç&V7BÒ6VçFW&VD7V7Df—E&V7B‡6÷W&6Uf–FVõ6—¦R‡6÷W&6TæÖR’Â6—¦Tb†Æ–÷WBçv–GF‚ÂÆ–÷WBæ†V–v‡B’“° –Æ–÷WBæ—FV×2çW6…ö&6²†—FVÒ“° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° –ÖævW%òÓç6fUfW'F–6ÄÆ–÷WB‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r†Æ–÷WBæ—FV×2ç6—¦R‚’Ò“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§&VÖ÷fT—FVÒ‚§° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° ––b†–æFW‚Â —&WGW&ã° •fW'F–6ÄÆ–÷WBÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° –Æ–÷WBæ—FV×2ç&VÖ÷fTB†–æFW‚“° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° –ÖævW%òÓç6fUfW'F–6ÄÆ–÷WB‚“° —&Vg&W6„—FV×2‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£¦Ö÷fT—FVÕW‚§° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° ––b†–æFW‚ÃÒ —&WGW&ã° •fW'F–6ÄÆ–÷WBÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° –Æ–÷WBæ—FV×2æÖ÷fR†–æFW‚Â–æFW‚Ò“° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° —6fTæ÷r‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r†–æFW‚Ò“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£¦Ö÷fT—FVÔF÷vâ‚§° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° –6öç7BWFòf7W'&VçBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b†–æFW‚ÂÇÂ–æFW‚ãÒ7W'&VçBæ—FV×2ç6—¦R‚’Ò —&WGW&ã° •fW'F–6ÄÆ–÷WBÆ–÷WBÒ7W'&VçC° –Æ–÷WBæ—FV×2æÖ÷fR†–æFW‚Â–æFW‚²“° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° —6fTæ÷r‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r†–æFW‚²“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§6VÆV7D—FVÒ†–çB&÷r§° –6öç7BWFòfÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b‡&÷rÂÇÂ&÷rãÒÆ–÷WBæ—FV×2ç6—¦R‚’’° —&Wf–WuòÓç6WE6VÆV7FVD–æFW‚‚Ó“° —6WDÆ–W$6öçG&öÇ4Væ&ÆVB†fÇ6R“° —&WGW&ã° —Ð  –ÆöF–æuòÒG'VS° —6WDÆ–W$6öçG&öÇ4Væ&ÆVB‡G'VR“° –6öç7BWFòf—FVÒÒÆ–÷WBæ—FV×5·&÷uÓ° —…òÓç6WEfÇVR†–çB†—FVÒç&V7Bç‚‚’’“° —•òÓç6WEfÇVR†–çB†—FVÒç&V7Bç’‚’’“° —uòÓç6WEfÇVR†–çB†—FVÒç&V7Bçv–GF‚‚’’“° –…òÓç6WEfÇVR†–çB†—FVÒç&V7Bæ†V–v‡B‚’’“° –6öç7B–çBf—D–æFW‚Òf—EòÓæf–æDFF†f—DÖöFUFõ7G&–ær†—FVÒæf—DÖöFR’“° –f—EòÓç6WD7W'&VçD–æFW‚†f—D–æFW‚ãÒòf—D–æFW‚¢“° —f—6–&ÆUòÓç6WD6†V6¶VB†—FVÒçf—6–&ÆR“° –ÆöF–æuòÒfÇ6S° —&Wf–WuòÓç6WE6VÆV7FVD–æFW‚‡&÷r“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§WFFT—FVÕf—6–&–Æ—G’…Æ—7Ev–FvWD—FVÒ¦—FVÒ§° ––b†ÆöF–æuòÇÂ—FVÒÇÂ—FV×5ò —&WGW&ã° –6öç7B–çB&÷rÒ—FV×5òÓç&÷r†—FVÒ“° –6öç7BWFòf7W'&VçBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b‡&÷rÂÇÂ&÷rãÒ7W'&VçBæ—FV×2ç6—¦R‚’ —&WGW&ã° –6öç7B&ööÂf—6–&ÆRÒ—FVÒÓæ6†V6µ7FFR‚’ÓÒC£¤6†V6¶VC° ––b†7W'&VçBæ—FV×5·&÷uÒçf—6–&ÆRÓÒf—6–&ÆR —&WGW&ã° •fW'F–6ÄÆ–÷WBÆ–÷WBÒ7W'&VçC° –Æ–÷WBæ—FV×5·&÷uÒçf—6–&ÆRÒf—6–&ÆS° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° —&Wf–WuòÓç6WDÆ–÷WDFF†Æ–÷WB“° •F–ÖW#£§6–ævÆU6†÷BƒÂF†—2Â·F†—5Ò‚’²6fTæ÷r‚“²Ò“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§WFFU6VÆV7FVD—FVÒ‚§° ––b†ÆöF–æuò —&WGW&ã° –6öç7B–çB–æFW‚Ò6VÆV7FVD–æFW‚‚“° ––b†–æFW‚Â —&WGW&ã°  •fW'F–6ÄÆ–÷WBÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° –WFòf—FVÒÒÆ–÷WBæ—FV×5¶–æFW…Ó° –—FVÒç&V7BÒ&V7Db‡…òÓçfÇVR‚’Â•òÓçfÇVR‚’ÂuòÓçfÇVR‚’Â…òÓçfÇVR‚’“° –—FVÒæf—DÖöFRÒf—DÖöFTg&öÕ7G&–ær†f—EòÓæ7W'&VçDFF‚’çFõ7G&–ær‚’“° –—FVÒçf—6–&ÆRÒf—6–&ÆUòÓæ—46†V6¶VB‚“° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° –ÖævW%òÓç6fUfW'F–6ÄÆ–÷WB‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r†–æFW‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§&Vg&W6„—FV×2‚§° ––b‚—FV×5òÇÂfW'F–6Å66VæW5òÇÂ66VæTÆ–æ·5òÇÂ66VæTÆ–æµFV×ÆFUòÇÂ&Wf–Wuò —&WGW&ã°  –6öç7B–çB6VÆV7FVBÒ—FV×5òÓæ7W'&VçE&÷r‚“° •7G&–ær6VÆV7FVE66VæTÆ–æ´æÖS° •7G&–ær6VÆV7FVE66VæTÆ–æµWV–C° ––b†6öç7BÆ—7Ev–FvWD—FVÒ§6VÆV7FVDÆ–æ²Ò66VæTÆ–æ·5òÓæ7W'&VçD—FVÒ‚’’° —6VÆV7FVE66VæTÆ–æ´æÖRÒ6VÆV7FVDÆ–æ²ÓæFF…66VæTÆ–æ´æÖU&öÆR’çFõ7G&–ær‚“° —6VÆV7FVE66VæTÆ–æµWV–BÒ6VÆV7FVDÆ–æ²ÓæFF…66VæTÆ–æµWV–E&öÆR’çFõ7G&–ær‚“° —Ð –ÆöF–æuòÒG'VS° –6öç7B6–væÄ&Æö6¶W"—FV×4&Æö6¶W"†—FV×5ò“° –6öç7B6–væÄ&Æö6¶W"fW'F–6Å66VæW4&Æö6¶W"‡fW'F–6Å66VæW5ò“° –6öç7B6–væÄ&Æö6¶W"Æ–æµ66VæT&Æö6¶W"‡66VæTÆ–æµFV×ÆFUò“° –6öç7B6–væÄ&Æö6¶W"66VæTÆ–æ·4&Æö6¶W"‡66VæTÆ–æ·5ò“° –—FV×5òÓæ6ÆV"‚“° —fW'F–6Å66VæW5òÓæ6ÆV"‚“° —66VæTÆ–æ·5òÓæ6ÆV"‚“° –6öç7B7G&–ær6VÆV7FVDÆ–æ¶VE66VæT–BÒ66VæTÆ–æµFV×ÆFUòÓæ7W'&VçDFF‚’çFõ7G&–ær‚“° —66VæTÆ–æµFV×ÆFUòÓæ6ÆV"‚“° ––çB7F—fUfW'F–6Å66VæU&÷rÒÓ° –f÷"†6öç7BWFòg66VæR¢ÖævW%òÓæÆ–÷WG2‚’çfW'F–6Å66VæW2‚’’° –6öç7B&ööÂ7F—fU66VæRÒ66VæRæ–BÓÒÖævW%òÓæÆ–÷WG2‚’æ7F—fUfW'F–6Å66VæT–B‚“° –WFò§66VæT—FVÒÒæWrÆ—7Ev–FvWD—FVÒ†7F—fU66VæRò7G–ÆR‚’Óç7FæF&D–6öâ…7G–ÆS£¥5ôÖVF–Æ’’¢–6öâ‚’À ’66VæRææÖR“° —66VæT—FVÒÓç6WDFF…C£¥W6W%&öÆRÂ66VæRæ–B“° —66VæT—FVÒÓç6WEFööÅF—†7F—fU66VæRò7G&–ætÆ—FW&Â‚$7F—fRE4²fW'F–6Â÷WGWB66VæR" ’¢7G&–ætÆ—FW&Â‚$E4²fW'F–6Â66VæR"’“° —fW'F–6Å66VæW5òÓæFD—FVÒ‡66VæT—FVÒ“° ––b†7F—fU66VæR –7F—fUfW'F–6Å66VæU&÷rÒfW'F–6Å66VæW5òÓæ6÷VçB‚’Ò° —66VæTÆ–æµFV×ÆFUòÓæFD—FVÒ‡66VæRææÖRÂ66VæRæ–B“° —Ð ––b†7F—fUfW'F–6Å66VæU&÷rãÒ —fW'F–6Å66VæW5òÓç6WD7W'&VçE&÷r†7F—fUfW'F–6Å66VæU&÷r“° –6öç7B–çBÆ–æ¶VE66VæT–æFW‚Ò66VæTÆ–æµFV×ÆFUòÓæf–æDFF‡6VÆV7FVDÆ–æ¶VE66VæT–B“° ––b†Æ–æ¶VE66VæT–æFW‚ãÒ —66VæTÆ–æµFV×ÆFUòÓç6WD7W'&VçD–æFW‚†Æ–æ¶VE66VæT–æFW‚“° ––çB6VÆV7FVE66VæTÆ–æµ&÷rÒÓ° –f÷"†6öç7BWFòfÆ–æ²¢ÖævW%òÓç66VæTÆ–æ·2‚’’° –6öç7B7G&–ær66VæTæÖRÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6Å66VæTæÖR†Æ–æ²çfW'F–6Å66VæT–B“° –6öç7B7G&–ærÆ–æ¶VDö'566VæRÒÖævW%òÓç&W6öÇfVDö'566VæTæÖR†Æ–æ²ç66VæUWV–BÂÆ–æ²ç66VæTæÖR“° –6öç7B7G&–ærÆ–æ¶VDö'566VæTF—7Æ’ÒÆ–æ¶VDö'566VæRæ—4V×G’‚ ’ò7G&–ætÆ—FW&Â‚$Ö—76–æs¢S"’æ&r†Æ–æ²ç66VæTæÖR ’¢Æ–æ¶VDö'566VæS° –6öç7B7G&–ær&Vf—‚Ò66VæTÆ–æ´ÖF6†W2†Æ–æ²ÂÖævW%òÓæ7W'&VçDö'566VæTæÖR‚’ÂÖævW%òÓæ7W'&VçDö'566VæUWV–B‚’ ’ò7G&–ætÆ—FW&Â‚$7W'&VçC¢" ’¢7G&–ær‚“° –WFò¦Æ–æ´—FVÒÒæWrÆ—7Ev–FvWD—FVÒ…7G&–ær‚"SS"ÓâS2"’æ&r€ —&Vf—‚À –Æ–æ¶VDö'566VæTF—7Æ’À —66VæTæÖRæ—4V×G’‚’ò7G&–ætÆ—FW&Â‚"†Ö—76–ærE4²66VæR’"’¢66VæTæÖR’“° –Æ–æ´—FVÒÓç6WDFF…66VæTÆ–æ´æÖU&öÆRÂÆ–æ²ç66VæTæÖR“° –Æ–æ´—FVÒÓç6WDFF…66VæTÆ–æµWV–E&öÆRÂÆ–æ²ç66VæUWV–B“° –Æ–æ´—FVÒÓç6WDFF…66VæTÆ–æµfW'F–6Å66VæT–E&öÆRÂÆ–æ²çfW'F–6Å66VæT–B“° –Æ–æ´—FVÒÓç6WDFF…66VæTÆ–æµ&W6öÇfVDæÖU&öÆRÂÆ–æ¶VDö'566VæR“° –Æ–æ´—FVÒÓç6WEFööÅF—…7G&–ætÆ—FW&Â‚%6VÆV7BF†—2Æ–æ²FòVF—B÷"VæÆ–æ²—Bâ"’“° —66VæTÆ–æ·5òÓæFD—FVÒ†Æ–æ´—FVÒ“° –6öç7B&ööÂ6VÆV7FVDÆ–æ´ÖF6†W2Ò6VÆV7FVE66VæTÆ–æµWV–Bæ—4V×G’‚ ’òÆ–æ²ç66VæUWV–BÓÒ6VÆV7FVE66VæTÆ–æµWV–@ ’¢Æ–æ²ç66VæTæÖRÓÒ6VÆV7FVE66VæTÆ–æ´æÖS° ––b‡6VÆV7FVDÆ–æ´ÖF6†W2 —6VÆV7FVE66VæTÆ–æµ&÷rÒ66VæTÆ–æ·5òÓæ6÷VçB‚’Ò° —Ð —WFFTö'4Æ–æµ7FGW2‚“° –6öç7BWFòfÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° –f÷"†–çB’Ò²’ÂÆ–÷WBæ—FV×2ç6—¦R‚“²²¶’’° –6öç7BWFòfÆ–÷WD—FVÒÒÆ–÷WBæ—FV×5¶•Ó° –WFò§6÷W&6T—FVÒÒæWrÆ—7Ev–FvWD—FVÒ‡7G–ÆR‚’Óç7FæF&D–6öâ…7G–ÆS£¥5ôf–ÆT–6öâ’À –—FVÔÆ&VÂ†Æ–÷WD—FVÒÂ’’“° —6÷W&6T—FVÒÓç6WDfÆw2‡6÷W&6T—FVÒÓæfÆw2‚’ÂC£¤—FVÔ—5W6W$6†V6¶&ÆR“° —6÷W&6T—FVÒÓç6WD6†V6µ7FFR†Æ–÷WD—FVÒçf—6–&ÆRòC£¤6†V6¶VB¢C£¥Væ6†V6¶VB“° —6÷W&6T—FVÒÓç6WEFööÅF—…7G&–ætÆ—FW&Â‚"S‚S"BS2ÂSB" ’æ&r‡7FC£¦Ç&÷VæB†Æ–÷WD—FVÒç&V7Bçv–GF‚‚’’ ’æ&r‡7FC£¦Ç&÷VæB†Æ–÷WD—FVÒç&V7Bæ†V–v‡B‚’’ ’æ&r‡7FC£¦Ç&÷VæB†Æ–÷WD—FVÒç&V7Bç‚‚’’ ’æ&r‡7FC£¦Ç&÷VæB†Æ–÷WD—FVÒç&V7Bç’‚’’’“° –—FV×5òÓæFD—FVÒ‡6÷W&6T—FVÒ“° —Ð —&Wf–WuòÓç6WDÆ–÷WDFF†Æ–÷WB“° —WFFU66VæU7FGW2‚“° –ÆöF–æuòÒfÇ6S° ––b‡6VÆV7FVE66VæTÆ–æµ&÷rãÒ’° —66VæTÆ–æ·5òÓç6WD7W'&VçE&÷r‡6VÆV7FVE66VæTÆ–æµ&÷r“° —6VÆV7E66VæTÆ–æ²‡6VÆV7FVE66VæTÆ–æµ&÷r“° —ÒVÇ6R° —6VÆV7E66VæTÆ–æ´f÷$ö'566VæR‡66VæTÆ–æµ66VæUòÓæ7W'&VçEFW‡B‚’“° —Ð ––b‚6WGWFövvÆUòÇÂ6WGWFövvÆUòÓæ—46†V6¶VB‚’’° –—FV×5òÓç6WD7W'&VçE&÷r‚Ó“° –—FV×5òÓæ6ÆV%6VÆV7F–öâ‚“° —&Wf–WuòÓç6WE6VÆV7FVD–æFW‚‚Ó“° —6WDÆ–W$6öçG&öÇ4Væ&ÆVB†fÇ6R“° —ÒVÇ6R–b‡6VÆV7FVBãÒbb6VÆV7FVBÂÆ–÷WBæ—FV×2ç6—¦R‚’’° –—FV×5òÓç6WD7W'&VçE&÷r‡6VÆV7FVB“° —6VÆV7D—FVÒ‡6VÆV7FVB“° —ÒVÇ6R–b‚Æ–÷WBæ—FV×2æ—4V×G’‚’’° –—FV×5òÓç6WD7W'&VçE&÷rƒ“° —6VÆV7D—FVÒƒ“° —ÒVÇ6R° —&Wf–WuòÓç6WE6VÆV7FVD–æFW‚‚Ó“° —6WDÆ–W$6öçG&öÇ4Væ&ÆVB†fÇ6R“° —Ð§Ð ¦–çBfW'F–6ÄÆ–÷WDVF—F÷#£§6VÆV7FVD–æFW‚‚’6öç7@§° –6öç7B–çB&÷rÒ—FV×5òÓæ7W'&VçE&÷r‚“° –6öç7BWFòfÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b‡&÷rÂÇÂ&÷rãÒÆ–÷WBæ—FV×2ç6—¦R‚’ —&WGW&âÓ° —&WGW&â&÷s°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§6WE6VÆV7FVD—FVÕ&V7B†6öç7B&V7Dbg&V7B§° –6öç7B–çB&÷rÒ6VÆV7FVD–æFW‚‚“° –6öç7BWFòf7W'&VçDÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b‡&÷rÂÇÂ&÷rãÒ7W'&VçDÆ–÷WBæ—FV×2ç6—¦R‚’ —&WGW&ã°  •fW'F–6ÄÆ–÷WBÆ–÷WBÒ7W'&VçDÆ–÷WC° –Æ–÷WBæ—FV×5·&÷uÒç&V7BÒ&V7C° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° —6fTæ÷r‚“° —&Vg&W6„—FV×2‚“° –—FV×5òÓç6WD7W'&VçE&÷r‡&÷r“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§66†VGVÆU6fR‚§° ––b‡6fUF–ÖW%ò —6fUF–ÖW%òÓç7F'B‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§6fTæ÷r‚§° ––b‡6fUF–ÖW%ò —6fUF–ÖW%òÓç7F÷‚“° ––b†ÖævW%ò –ÖævW%òÓç6fUfW'F–6ÄÆ–÷WB‚“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§6WE6WGWf—6–&ÆR†&ööÂf—6–&ÆRÂ&ööÂW'6—7B§° ––b‚6WGWæVÅòÇÂG&ç6f÷&ÕFövvÆUòÇÂö'4Æ–æ·5FövvÆUò —&WGW&ã°  —6WGWæVÅòÓç6WEf—6–&ÆR‡f—6–&ÆR“° —G&ç6f÷&ÕFövvÆUòÓç6WEf—6–&ÆR‡f—6–&ÆR“° –ö'4Æ–æ·5FövvÆUòÓç6WEf—6–&ÆR‡f—6–&ÆR“° ––b‚f—6–&ÆR’° ––b†—FV×5ò —6WGW6VÆV7FVE&÷uòÒ—FV×5òÓæ7W'&VçE&÷r‚“° —G&ç6f÷&ÕFövvÆUòÓç6WD6†V6¶VB†fÇ6R“° –ö'4Æ–æ·5FövvÆUòÓç6WD6†V6¶VB†fÇ6R“° ––b†—FV×5ò’° –6öç7B6–væÄ&Æö6¶W"&Æö6¶W"†—FV×5ò“° –—FV×5òÓç6WD7W'&VçE&÷r‚Ó“° –—FV×5òÓæ6ÆV%6VÆV7F–öâ‚“° —Ð ––b‡&Wf–Wuò —&Wf–WuòÓç6WE6VÆV7FVD–æFW‚‚Ó“° —6WDÆ–W$6öçG&öÇ4Væ&ÆVB†fÇ6R“° —ÒVÇ6R–b†—FV×5òbb6WGW6VÆV7FVE&÷uòãÒbb6WGW6VÆV7FVE&÷uòÂ—FV×5òÓæ6÷VçB‚’’° –—FV×5òÓç6WD7W'&VçE&÷r‡6WGW6VÆV7FVE&÷uò“° —6VÆV7D—FVÒ‡6WGW6VÆV7FVE&÷uò“° —Ð  ––b‡W'6—7B —6fUfW'F–6Å6WGWf—6–&ÆU&VfW&Væ6R‡f—6–&ÆR“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§WFFT—FVÕ&V7Dg&öÕ&Wf–Wr†–çB&÷rÂ6öç7B&V7Dbg&V7BÂ&ööÂ6fR§° –6öç7BWFòf7W'&VçDÆ–÷WBÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚“° ––b‡&÷rÂÇÂ&÷rãÒ7W'&VçDÆ–÷WBæ—FV×2ç6—¦R‚’ —&WGW&ã°  •fW'F–6ÄÆ–÷WBÆ–÷WBÒ7W'&VçDÆ–÷WC° –Æ–÷WBæ—FV×5·&÷uÒç&V7BÒ&V7C° –ÖævW%òÓæÆ–÷WG2‚’ç6WEfW'F–6ÄÆ–÷WB†Æ–÷WB“° ––b‡6fR —6fTæ÷r‚“° –VÇ6P —66†VGVÆU6fR‚“°  –ÆöF–æuòÒG'VS° —…òÓç6WEfÇVR†–çB‡7FC£¦Ç&÷VæB‡&V7Bç‚‚’’’“° —•òÓç6WEfÇVR†–çB‡7FC£¦Ç&÷VæB‡&V7Bç’‚’’’“° —uòÓç6WEfÇVR†–çB‡7FC£¦Ç&÷VæB‡&V7Bçv–GF‚‚’’’“° –…òÓç6WEfÇVR†–çB‡7FC£¦Ç&÷VæB‡&V7Bæ†V–v‡B‚’’’“° ––b†WFò¦Æ—7D—FVÒÒ—FV×5òÓæ—FVÒ‡&÷r’ –Æ—7D—FVÒÓç6WEFW‡B†—FVÔÆ&VÂ†Æ–÷WBæ—FV×5·&÷uÒÂ&÷r’“° –ÆöF–æuòÒfÇ6S°  —&Wf–WuòÓç6WDÆ–÷WDFF†Æ–÷WB“° —&Wf–WuòÓç6WE6VÆV7FVD–æFW‚‡&÷r“° ––b†—FV×5òÓæ7W'&VçE&÷r‚’Ò&÷r –—FV×5òÓç6WD7W'&VçE&÷r‡&÷r“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§6WDÆ–W$6öçG&öÇ4Væ&ÆVB†&ööÂVæ&ÆVB§° –f÷"†WFò§7–â¢·…òÂ•òÂuòÂ…÷Ò’° ––b‡7–â —7–âÓç6WDVæ&ÆVB†Væ&ÆVB“° —Ð ––b†f—Eò –f—EòÓç6WDVæ&ÆVB†Væ&ÆVB“° ––b‡f—6–&ÆUò —f—6–&ÆUòÓç6WDVæ&ÆVB†Væ&ÆVB“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§WFFU66VæU7FGW2‚§° ––b‚ÖævW%ò —&WGW&ã°  –6öç7B7G&–ær7F—fRÒÖævW%òÓæÆ–÷WG2‚’æ7F—fUfW'F–6Å66VæTæÖR‚’æ—4V×G’‚’ð ’7G&–ætÆ—FW&Â‚"†æöæR’"’  ’ÖævW%òÓæÆ–÷WG2‚’æ7F—fUfW'F–6Å66VæTæÖR‚“°  ––b†7F—fU66VæU7FGW5ò –7F—fU66VæU7FGW5òÓç6WEFW‡B…7G&–ætÆ—FW&Â‚%fW'F–6Â&öw&Ó¢S"’æ&r†7F—fR’“°  ––b‡6÷W&6W4†VFW%ò —6÷W&6W4†VFW%òÓç6WEFW‡B…7G&–æs£¦çVÖ&W"†ÖævW%òÓæÆ–÷WG2‚’çfW'F–6ÄÆ–÷WB‚’æ—FV×2ç6—¦R‚’’“°§Ð §fö–BfW'F–6ÄÆ–÷WDVF—F÷#£§WFFTö'4Æ–æµ7FGW2‚§° ––b‚ö'4Æ–æµ7FGW5òÇÂÖævW%ò —&WGW&ã°  –6öç7B7G&–ærö'566VæRÒ7W'&VçDg&öçFVæE66VæTæÖR‚“° –6öç7B7G&–ærö'566VæUWV–BÒÖævW%òÓæ7W'&VçDö'566VæUWV–B‚“° •7G&–ærÆ–æ¶VE66VæS° –f÷"†6öç7BWFòfÆ–æ²¢ÖævW%òÓç66VæTÆ–æ·2‚’’° ––b‡66VæTÆ–æ´ÖF6†W2†Æ–æ²Âö'566VæRÂö'566VæUWV–B’’° –Æ–æ¶VE66VæRÒÖævW%òÓæÆ–÷WG2‚’çfW'F–6Å66VæTæÖR†Æ–æ²çfW'F–6Å66VæT–B“° ––b†Æ–æ¶VE66VæRæ—4V×G’‚’ –Æ–æ¶VE66VæRÒ7G&–ætÆ—FW&Â‚"†Ö—76–ærE4²66VæR’"“° –'&V³° —Ð —Ð  –6öç7B7G&–ærö'4æÖRÒö'566VæRæ—4V×G’‚’ò7G&–ætÆ—FW&Â‚"†æòô%266VæR’"’¢ö'566VæS° –6öç7B7G&–ærÆ–æµFW‡BÒÆ–æ¶VE66VæRæ—4V×G’‚’ò7G&–ætÆ—FW&Â‚$æ÷BÆ–æ¶VB"’¢Æ–æ¶VE66VæS° –ö'4Æ–æµ7FGW5òÓç6WEFW‡B…7G&–ætÆ—FW&Â‚"SÓâS""’æ&r†ö'4æÖRÂÆ–æµFW‡B’“° —WFFU66VæU7FGW2‚“°§Ð §ÒòòæÖW76RG6°