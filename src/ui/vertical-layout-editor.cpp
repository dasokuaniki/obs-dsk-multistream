#include "ui/vertical-layout-editor.hpp"

#include "core/layout-manager.hpp"
#include "core/vertical-layout-geometry.hpp"
#include "core/vertical-scene-builder.hpp"
#include "core/stable-id-order.hpp"
#include "ui/layout-widget-utils.hpp"
#include "ui/vertical-source-icon-loader.hpp"
#include "ui/vertical-layout-metrics.hpp"
#include "ui/vertical-toolbar-layout.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QAbstractItemModel>
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

#ifdef DSK_INCLUDE_E2E_HOOKS
std::atomic<std::uint64_t> previewCallbackCount{0};
std::atomic<std::uint64_t> previewRenderedSnapshotCount{0};
std::atomic<std::uint64_t> previewRenderedGenerationCount{0};
std::atomic<std::uint64_t> previewLastRenderedGeneration{0};
std::atomic<std::uint32_t> previewSceneWidth{0};
std::atomic<std::uint32_t> previewSceneHeight{0};
#endif

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

class VerticalToolbarWidget final : public QWidget {
public:
	VerticalToolbarWidget(QLabel *status, QCheckBox *snapping, QToolButton *setup,
			      QToolButton *transform, QToolButton *obsLinks, QWidget *parent)
		: QWidget(parent),
		  status_(status),
		  snapping_(snapping),
		  setup_(setup),
		  transform_(transform),
		  obsLinks_(obsLinks),
		  layout_(new QGridLayout(this))
	{
		layout_->setContentsMargins(0, 0, 0, 0);
		layout_->setHorizontalSpacing(VerticalToolbarMaximumSpacing);
		layout_->setVerticalSpacing(2);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		applyMode(verticalToolbarUsesCompactMode(width()));
	}

	QSize minimumSizeHint() const override
	{
		return QSize(VerticalPreviewMinimumWidth, layout_->minimumSize().height());
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		applyMode(verticalToolbarUsesCompactMode(event->size().width()));
	}

private:
	void applyMode(bool compact)
	{
		if (initialized_ && compact_ == compact)
			return;
		initialized_ = true;
		compact_ = compact;
		for (QWidget *widget : {static_cast<QWidget *>(status_), static_cast<QWidget *>(snapping_),
					static_cast<QWidget *>(setup_), static_cast<QWidget *>(transform_),
					static_cast<QWidget *>(obsLinks_)}) {
			layout_->removeWidget(widget);
		}
		if (compact_) {
			snapping_->setText(QString());
			for (QToolButton *button : {setup_, transform_, obsLinks_})
				button->setToolButtonStyle(Qt::ToolButtonIconOnly);
			layout_->addWidget(status_, 0, 0, 1, 2);
			layout_->addWidget(snapping_, 1, 0);
			layout_->addWidget(setup_, 1, 1);
			layout_->addWidget(transform_, 2, 0);
			layout_->addWidget(obsLinks_, 2, 1);
			layout_->setColumnStretch(0, 1);
			layout_->setColumnStretch(1, 1);
		} else {
			snapping_->setText(QStringLiteral("Snap"));
			for (QToolButton *button : {setup_, transform_, obsLinks_})
				button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			layout_->addWidget(status_, 0, 0);
			layout_->addWidget(snapping_, 0, 1);
			layout_->addWidget(setup_, 0, 2);
			layout_->addWidget(transform_, 0, 3);
			layout_->addWidget(obsLinks_, 0, 4);
			layout_->setColumnStretch(0, 1);
			for (int column = 1; column <= 4; ++column)
				layout_->setColumnStretch(column, 0);
		}
		layout_->invalidate();
		updateGeometry();
	}

	QLabel *status_ = nullptr;
	QCheckBox *snapping_ = nullptr;
	QToolButton *setup_ = nullptr;
	QToolButton *transform_ = nullptr;
	QToolButton *obsLinks_ = nullptr;
	QGridLayout *layout_ = nullptr;
	bool initialized_ = false;
	bool compact_ = false;
};

constexpr int SceneLinkNameRole = Qt::UserRole;
constexpr int SceneLinkUuidRole = Qt::UserRole + 1;
constexpr int SceneLinkVerticalSceneIdRole = Qt::UserRole + 2;
constexpr int SceneLinkResolvedNameRole = Qt::UserRole + 3;

constexpr const char *VerticalUiConfigSection = "DSKVerticalLayout";
constexpr const char *SetupVisibleConfigKey = "SetupVisible";

QVector<QString> listStableIds(const QListWidget *list)
{
	QVector<QString> ids;
	if (!list)
		return ids;
	ids.reserve(list->count());
	for (int row = 0; row < list->count(); ++row)
		ids.push_back(list->item(row)->data(Qt::UserRole).toString());
	return ids;
}

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

#ifdef DSK_INCLUDE_E2E_HOOKS
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
#endif

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
		suspendPreview();
		releasePreviewCanvas();
	}

	void handleSceneCollectionChanged()
	{
		VerticalLayout currentLayout;
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			sceneDirty_ = true;
			currentLayout = layout_;
		}
		createDisplay();
		setLayoutData(currentLayout);
		update();
	}

#ifdef DSK_INCLUDE_E2E_HOOKS
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
#endif

	void setLayoutData(const VerticalLayout &layout)
	{
		VerticalLayoutChange change = VerticalLayoutChange::Rebuild;
		bool canUpdateTransforms = false;
		QVector<bool> itemHasVideo;
		QVector<QRectF> itemDisplayRects;
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			change = verticalLayoutChange(layout_, layout);
			if (change == VerticalLayoutChange::None && !sceneDirty_)
				return;
			canUpdateTransforms = change == VerticalLayoutChange::TransformOnly && !sceneDirty_ && display_ &&
					      isVisible();
		}

		itemHasVideo.reserve(layout.items.size());
		itemDisplayRects.reserve(layout.items.size());
		for (const auto &item : layout.items) {
			const QSizeF sourceSize = sourceVideoSize(item.sourceName);
			itemHasVideo.push_back(sourceSize.width() > 0.0 && sourceSize.height() > 0.0);
			itemDisplayRects.push_back(displayedContentRect(item, sourceSize));
		}

		const bool transformsUpdated = canUpdateTransforms && sceneBuilder_.updateItemTransforms(layout);
		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		layout_ = layout;
		sceneDirty_ = change == VerticalLayoutChange::Rebuild || !transformsUpdated;
		itemHasVideo_ = std::move(itemHasVideo);
		itemDisplayRects_ = std::move(itemDisplayRects);
#ifdef DSK_INCLUDE_E2E_HOOKS
		++layoutGeneration_;
#endif
		locker.unlock();
		if (transformsUpdated)
			return;
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
		// Dock visibility changes are transient. Keep the private video canvas
		// alive so minimizing or rearranging OBS cannot churn GPU video mixes.
		suspendPreview();
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
#ifdef DSK_INCLUDE_E2E_HOOKS
		std::uint64_t layoutGeneration = 0;
#endif
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
#ifdef DSK_INCLUDE_E2E_HOOKS
			snapshot.layoutGeneration = layoutGeneration_;
#endif
			snapshot.sceneSource = obs_source_get_ref(sceneSource_);
		}
		renderStateMutex_.unlock();
		return valid && snapshot.sceneSource;
	}

	static void drawPreview(void *param, uint32_t cx, uint32_t cy)
	{
#ifdef DSK_INCLUDE_E2E_HOOKS
		previewCallbackCount.fetch_add(1, std::memory_order_relaxed);
#endif
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
#ifdef DSK_INCLUDE_E2E_HOOKS
		previewRenderedSnapshotCount.fetch_add(1, std::memory_order_relaxed);
		const std::uint64_t previousGeneration =
			previewLastRenderedGeneration.exchange(snapshot.layoutGeneration, std::memory_order_relaxed);
		if (previousGeneration != snapshot.layoutGeneration)
			previewRenderedGenerationCount.fetch_add(1, std::memory_order_relaxed);
#endif
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
			drawObsHandle(float(r.right()), float(r.bottom()));
		}
	}

	void createDisplay()
	{
		if (display_ || !isVisible())
			return;

		QWindow *window = windowHandle();
		if (!window) {
			winId();
			window = windowHandle();
		}
		if (!window)
			return;

		const QSize size = pixelSize(this);
		gs_init_data info = {};
		info.cx = uint32_t(size.width());
		info.cy = uint32_t(size.height());
		info.format = GS_BGRA;
		info.zsformat = GS_ZS_NONE;

#ifdef _WIN32
		info.window.hwnd = reinterpret_cast<HWND>(window->winId());
#else
		return;
#endif

		display_ = obs_display_create(&info, 0xFF18181C);
		if (!display_)
			return;

		obs_display_add_draw_callback(display_, VerticalRenderWidget::drawPreview, this);
		drawCallbackAdded_ = true;
		rebuildSceneIfVisible();
		syncSceneShowing();
	}

	void destroyDisplay()
	{
		if (!display_)
			return;
		setSceneSource(nullptr);
		if (drawCallbackAdded_)
			obs_display_remove_draw_callback(display_, VerticalRenderWidget::drawPreview, this);
		drawCallbackAdded_ = false;
		obs_display_destroy(display_);
		display_ = nullptr;
	}

	void suspendPreview()
	{
		setSceneSource(nullptr);
		destroyDisplay();
		sceneBuilder_.release();
		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		sceneDirty_ = true;
	}

	void releasePreviewCanvas()
	{
#ifdef DSK_ENABLE_OBS_CANVAS_API
		if (!previewCanvas_)
			return;
		if (!obs_canvas_removed(previewCanvas_))
			obs_canvas_remove(previewCanvas_);
		obs_canvas_release(previewCanvas_);
		previewCanvas_ = nullptr;
#endif
	}

	void setSceneSource(obs_source_t *source)
	{
		obs_source_t *replacement = source ? obs_source_get_ref(source) : nullptr;
		obs_source_t *previous = nullptr;
		bool previousShowing = false;
		bool replacementShowing = false;
		bool sameSource = false;
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			sameSource = source == sceneSource_;
			if (!sameSource) {
				previous = sceneSource_;
				previousShowing = sceneSourceShowing_;
				sceneSource_ = replacement;
				replacement = nullptr;
				replacementShowing = display_ && isVisible() && sceneSource_;
				sceneSourceShowing_ = replacementShowing;
			}
		}

		if (replacement)
			obs_source_release(replacement);
		if (sameSource) {
			syncSceneShowing();
			return;
		}
		if (previousShowing && previous)
			obs_source_dec_showing(previous);
		if (replacementShowing && source)
			obs_source_inc_showing(source);
		if (previous)
			obs_source_release(previous);
	}

	void rebuildSceneIfVisible()
	{
		VerticalLayout layout;
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			if (!sceneDirty_ || !display_ || !isVisible())
				return;
			layout = layout_;
		}

		QString errorMessage;
		obs_canvas_t *previewCanvas = nullptr;
#ifdef DSK_ENABLE_OBS_CANVAS_API
		obs_video_info previewInfo = {};
		if (layout.width <= 0 || layout.height <= 0 || !obs_get_video_info(&previewInfo)) {
#ifdef DSK_INCLUDE_E2E_HOOKS
			previewSceneWidth.store(0, std::memory_order_relaxed);
			previewSceneHeight.store(0, std::memory_order_relaxed);
#endif
			return;
		}
		previewInfo.base_width = uint32_t(layout.width);
		previewInfo.base_height = uint32_t(layout.height);
		previewInfo.output_width = uint32_t(layout.width);
		previewInfo.output_height = uint32_t(layout.height);

		obs_video_info currentInfo = {};
		const bool previewCanvasMatches =
			previewCanvas_ && !obs_canvas_removed(previewCanvas_) && obs_canvas_has_video(previewCanvas_) &&
			obs_canvas_get_video_info(previewCanvas_, &currentInfo) &&
			currentInfo.base_width == previewInfo.base_width &&
			currentInfo.base_height == previewInfo.base_height &&
			currentInfo.output_width == previewInfo.output_width &&
			currentInfo.output_height == previewInfo.output_height;
		if (previewCanvas_ && !previewCanvasMatches) {
			setSceneSource(nullptr);
			sceneBuilder_.release();
			releasePreviewCanvas();
		}
		if (!previewCanvas_) {
			constexpr uint32_t previewFlags = ACTIVATE | SCENE_REF | EPHEMERAL;
			previewCanvas_ =
				obs_canvas_create_private("DSK Vertical Preview Canvas", &previewInfo, previewFlags);
		}
		previewCanvas = previewCanvas_;
		if (!previewCanvas) {
#ifdef DSK_INCLUDE_E2E_HOOKS
			previewSceneWidth.store(0, std::memory_order_relaxed);
			previewSceneHeight.store(0, std::memory_order_relaxed);
#endif
			return;
		}
#endif
		obs_source_t *rebuiltSource = sceneBuilder_.rebuild(layout, previewCanvas, &errorMessage);
		setSceneSource(rebuiltSource);
#ifdef DSK_INCLUDE_E2E_HOOKS
		previewSceneWidth.store(rebuiltSource ? obs_source_get_width(rebuiltSource) : 0, std::memory_order_relaxed);
		previewSceneHeight.store(rebuiltSource ? obs_source_get_height(rebuiltSource) : 0, std::memory_order_relaxed);
#endif
		QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
		sceneDirty_ = rebuiltSource == nullptr;
	}

	void syncSceneShowing()
	{
		obs_source_t *source = nullptr;
		bool increment = false;
		bool decrement = false;
		{
			QMutexLocker<QRecursiveMutex> locker(&renderStateMutex_);
			const bool shouldShow = display_ && isVisible() && sceneSource_;
			if (shouldShow == sceneSourceShowing_)
				return;
			source = sceneSource_;
			increment = shouldShow;
			decrement = !shouldShow;
			sceneSourceShowing_ = shouldShow;
		}
		if (increment && source)
			obs_source_inc_showing(source);
		else if (decrement && source)
			obs_source_dec_showing(source);
	}

	VerticalLayout layout_;
	QVector<bool> itemHasVideo_;
	QVector<QRectF> itemDisplayRects_;
	QVector<double> snapXGuides_;
	QVector<double> snapYGuides_;
	VerticalSceneBuilder sceneBuilder_;
	// This widget owns one reference; render snapshots take temporary references.
	obs_source_t *sceneSource_ = nullptr;
	obs_display_t *display_ = nullptr;
#ifdef DSK_ENABLE_OBS_CANVAS_API
	obs_canvas_t *previewCanvas_ = nullptr;
#endif
	int selectedIndex_ = -1;
	bool drawCallbackAdded_ = false;
	bool sceneSourceShowing_ = false;
	bool sceneDirty_ = true;
#ifdef DSK_INCLUDE_E2E_HOOKS
	std::uint64_t layoutGeneration_ = 0;
#endif
	QRecursiveMutex renderStateMutex_;
};

class VerticalPreviewWidget : public QWidget {
public:
	explicit VerticalPreviewWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumSize(VerticalPreviewMinimumWidth, VerticalPreviewMinimumHeight);
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		render_ = new VerticalRenderWidget(this);
	}

	void prepareForUnload()
	{
		if (render_)
			render_->prepareForUnload();
	}

	void handleSceneCollectionChanged()
	{
		rebuildDisplayRects();
		if (render_)
			render_->handleSceneCollectionChanged();
	}

#ifdef DSK_INCLUDE_E2E_HOOKS
	bool exerciseCanvasReplacementForTest()
	{
		return render_ && render_->exerciseCanvasReplacementForTest();
	}
#endif

	void setLayoutData(const VerticalLayout &layout)
	{
		const VerticalLayoutChange change = verticalLayoutChange(layout_, layout);
		if (change == VerticalLayoutChange::None)
			return;
		layout_ = layout;
		rebuildDisplayRects();
		selectedIndex_ = validIndex(selectedIndex_) ? selectedIndex_ : -1;
		render_->setLayoutData(layout);
		render_->setSelectedIndex(selectedIndex_);
	}

	void setSelectedIndex(int index)
	{
		selectedIndex_ = validIndex(index) ? index : -1;
		render_->setSelectedIndex(selectedIndex_);
	}
	void setSnappingEnabled(bool enabled)
	{
		snappingEnabled_ = enabled;
		if (!snappingEnabled_)
			clearActiveSnapGuides();
	}
	void setSelectionChanged(std::function<void(int)> callback) { selectionChanged_ = std::move(callback); }
	void setRectChanged(std::function<void(int, const QRectF &, bool)> callback) { rectChanged_ = std::move(callback); }

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		render_->setGeometry(rect());
	}

private:
	enum class DragMode { None, Move, Resize };

	bool validIndex(int index) const { return index >= 0 && index < layout_.items.size(); }

	void rebuildDisplayRects()
	{
		itemDisplayRects_.clear();
		itemDisplayRects_.reserve(layout_.items.size());
		for (const auto &item : layout_.items)
			itemDisplayRects_.push_back(displayedContentRect(item, sourceVideoSize(item.sourceName)));
	}

	QRectF displayRectForItem(int index) const
	{
		if (index >= 0 && index < itemDisplayRects_.size())
			return itemDisplayRects_[index];
		return validIndex(index) ? layout_.items[index].rect : QRectF();
	}

	QRectF canvasRect() const { return scaledCanvasRect(size(), layout_); }

	double scale() const
	{
		const QRectF canvas = canvasRect();
		return canvas.isValid() && layout_.width > 0 ? canvas.width() / double(layout_.width) : 1.0;
	}

	QRectF itemToWidget(const QRectF &rect) const
	{
		const QRectF canvas = canvasRect();
		const double s = scale();
		return QRectF(canvas.x() + rect.x() * s, canvas.y() + rect.y() * s, rect.width() * s, rect.height() * s);
	}

	QPointF widgetToLayout(const QPointF &point) const
	{
		const QRectF canvas = canvasRect();
		const double s = scale();
		return QPointF((point.x() - canvas.x()) / s, (point.y() - canvas.y()) / s);
	}

	QVector<QPair<int, QRectF>> handleRects(const QRectF &itemRect) const
	{
		constexpr double size = 11.0;
		constexpr double half = size / 2.0;
		const QPointF tl = itemRect.topLeft();
		const QPointF tr = itemRect.topRight();
		const QPointF bl = itemRect.bottomLeft();
		const QPointF br = itemRect.bottomRight();
		const QPointF tc(itemRect.center().x(), itemRect.top());
		const QPointF bc(itemRect.center().x(), itemRect.bottom());
		const QPointF lc(itemRect.left(), itemRect.center().y());
		const QPointF rc(itemRect.right(), itemRect.center().y());

		auto box = [](const QPointF &point) { return QRectF(point.x() - half, point.y() - half, size, size); };
		return {
			{ResizeLeft | ResizeTop, box(tl)},	  {ResizeTop, box(tc)},
			{ResizeRight | ResizeTop, box(tr)},	  {ResizeLeft, box(lc)},
			{ResizeRight, box(rc)},			  {ResizeLeft | ResizeBottom, box(bl)},
			{ResizeBottom, box(bc)},		  {ResizeRight | ResizeBottom, box(br)},
		};
	}

	int handleAt(const QPointF &point) const
	{
		if (!validIndex(selectedIndex_))
			return 0;
		const QRectF canvas = canvasRect();
		if (!canvas.contains(point))
			return 0;
		const QRectF selectedRect = itemToWidget(displayRectForItem(selectedIndex_));
		for (const auto &handle : handleRects(selectedRect)) {
			if (handle.second.adjusted(-4, -4, 4, 4).contains(point))
				return handle.first;
		}
		constexpr double edgeGrab = 8.0;
		if (selectedRect.adjusted(-edgeGrab, -edgeGrab, edgeGrab, edgeGrab).contains(point)) {
			int edges = 0;
			if (std::abs(point.x() - selectedRect.left()) <= edgeGrab)
				edges |= ResizeLeft;
			if (std::abs(point.x() - selectedRect.right()) <= edgeGrab)
				edges |= ResizeRight;
			if (std::abs(point.y() - selectedRect.top()) <= edgeGrab)
				edges |= ResizeTop;
			if (std::abs(point.y() - selectedRect.bottom()) <= edgeGrab)
				edges |= ResizeBottom;
			if (edges)
				return edges;
		}
		const QRectF visibleRect = selectedRect.intersected(canvas);
		if (visibleRect.isValid() && visibleRect != selectedRect &&
		    visibleRect.adjusted(-edgeGrab, -edgeGrab, edgeGrab, edgeGrab).contains(point)) {
			int edges = 0;
			if (selectedRect.left() < canvas.left() && std::abs(point.x() - visibleRect.left()) <= edgeGrab)
				edges |= ResizeLeft;
			if (selectedRect.right() > canvas.right() && std::abs(point.x() - visibleRect.right()) <= edgeGrab)
				edges |= ResizeRight;
			if (selectedRect.top() < canvas.top() && std::abs(point.y() - visibleRect.top()) <= edgeGrab)
				edges |= ResizeTop;
			if (selectedRect.bottom() > canvas.bottom() && std::abs(point.y() - visibleRect.bottom()) <= edgeGrab)
				edges |= ResizeBottom;
			if (edges)
				return edges;
		}
		return 0;
	}

	int itemAt(const QPointF &point) const
	{
		return verticalPreviewHitItemAtWidgetPoint(layout_, itemDisplayRects_, canvasRect(), point, selectedIndex_);
	}

	QRectF clampedRect(QRectF rect, DragMode mode, int edges) const
	{
		constexpr double minSize = 32.0;
		constexpr double minVisible = 24.0;
		if (mode == DragMode::Move) {
			const double minX = -rect.width() + minVisible;
			const double maxX = double(layout_.width) - minVisible;
			const double minY = -rect.height() + minVisible;
			const double maxY = double(layout_.height) - minVisible;
			rect.moveLeft(std::clamp(rect.x(), minX, maxX));
			rect.moveTop(std::clamp(rect.y(), minY, maxY));
			return rect;
		}

		double left = rect.left();
		double right = rect.right();
		double top = rect.top();
		double bottom = rect.bottom();

		const double minLeft = -double(layout_.width);
		const double maxRight = double(layout_.width) * 2.0;
		const double minTop = -double(layout_.height);
		const double maxBottom = double(layout_.height) * 2.0;
		left = std::clamp(left, minLeft, maxRight - minSize);
		right = std::clamp(right, minLeft + minSize, maxRight);
		top = std::clamp(top, minTop, maxBottom - minSize);
		bottom = std::clamp(bottom, minTop + minSize, maxBottom);

		if (edges & ResizeLeft)
			left = std::min(left, right - minSize);
		if (edges & ResizeRight)
			right = std::max(right, left + minSize);
		if (edges & ResizeTop)
			top = std::min(top, bottom - minSize);
		if (edges & ResizeBottom)
			bottom = std::max(bottom, top + minSize);

		return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
	}

	double snapValue(double value, double target) const
	{
		constexpr double threshold = 12.0;
		return std::abs(value - target) <= threshold ? target : value;
	}

	double snapValue(double value, const QVector<double> &targets) const
	{
		constexpr double threshold = 12.0;
		double bestValue = value;
		double bestDistance = threshold + 1.0;
		for (const double target : targets) {
			const double distance = std::abs(value - target);
			if (distance <= threshold && distance < bestDistance) {
				bestDistance = distance;
				bestValue = target;
			}
		}
		return bestValue;
	}

	void clearActiveSnapGuides()
	{
		render_->setSnapGuides({}, {});
	}

	void updateActiveSnapGuides(const QRectF &rect)
	{
		constexpr double threshold = 0.5;
		QVector<double> xActive;
		QVector<double> yActive;
		const auto collect = [threshold](QVector<double> &active, double value, const QVector<double> &guides) {
			for (const double guide : guides) {
				if (std::abs(value - guide) <= threshold && !active.contains(guide))
					active.push_back(guide);
			}
		};
		const QVector<double> xGuides = horizontalSnapGuides();
		const QVector<double> yGuides = verticalSnapGuides();
		collect(xActive, rect.left(), xGuides);
		collect(xActive, rect.center().x(), xGuides);
		collect(xActive, rect.right(), xGuides);
		collect(yActive, rect.top(), yGuides);
		collect(yActive, rect.center().y(), yGuides);
		collect(yActive, rect.bottom(), yGuides);
		render_->setSnapGuides(xActive, yActive);
	}

	QVector<double> horizontalSnapGuides() const
	{
		QVector<double> guides{0.0, double(layout_.width) / 2.0, double(layout_.width)};
		for (int i = 0; i < layout_.items.size(); ++i) {
			if (i == selectedIndex_ || !layout_.items[i].visible)
				continue;
			const QRectF rect = displayRectForItem(i);
			guides.push_back(rect.left());
			guides.push_back(rect.center().x());
			guides.push_back(rect.right());
		}
		return guides;
	}

	QVector<double> verticalSnapGuides() const
	{
		QVector<double> guides{0.0, double(layout_.height) / 2.0, double(layout_.height)};
		for (int i = 0; i < layout_.items.size(); ++i) {
			if (i == selectedIndex_ || !layout_.items[i].visible)
				continue;
			const QRectF rect = displayRectForItem(i);
			guides.push_back(rect.top());
			guides.push_back(rect.center().y());
			guides.push_back(rect.bottom());
		}
		return guides;
	}

	double snappedMovePosition(double start, double length, const QVector<double> &guides) const
	{
		constexpr double threshold = 12.0;
		double bestStart = start;
		double bestDistance = threshold + 1.0;
		const auto consider = [&](double value, double target, double candidateStart) {
			const double distance = std::abs(value - target);
			if (distance <= threshold && distance < bestDistance) {
				bestDistance = distance;
				bestStart = candidateStart;
			}
		};

		for (const double guide : guides) {
			consider(start, guide, guide);
			consider(start + length / 2.0, guide, guide - length / 2.0);
			consider(start + length, guide, guide - length);
		}
		return bestStart;
	}

	QRectF snappedRect(QRectF rect, DragMode mode, int edges) const
	{
		if (!snappingEnabled_ || layout_.width <= 0 || layout_.height <= 0)
			return rect;

		const QVector<double> xGuides = horizontalSnapGuides();
		const QVector<double> yGuides = verticalSnapGuides();
		if (mode == DragMode::Move) {
			const double width = rect.width();
			const double height = rect.height();
			double left = snappedMovePosition(rect.left(), width, xGuides);
			double top = snappedMovePosition(rect.top(), height, yGuides);
			rect.moveLeft(left);
			rect.moveTop(top);
			return rect;
		}

		double left = rect.left();
		double right = rect.right();
		double top = rect.top();
		double bottom = rect.bottom();
		if (edges & ResizeLeft)
			left = snapValue(left, xGuides);
		if (edges & ResizeRight)
			right = snapValue(right, xGuides);
		if (edges & ResizeTop)
			top = snapValue(top, yGuides);
		if (edges & ResizeBottom)
			bottom = snapValue(bottom, yGuides);
		return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
	}

	void commitRect(const QRectF &rect, bool save)
	{
		if (!validIndex(selectedIndex_))
			return;
		layout_.items[selectedIndex_].rect = rect;
		if (selectedIndex_ < itemDisplayRects_.size())
			itemDisplayRects_[selectedIndex_] =
				displayedContentRect(layout_.items[selectedIndex_], sourceVideoSize(layout_.items[selectedIndex_].sourceName));
		render_->setLayoutData(layout_);
		render_->setSelectedIndex(selectedIndex_);
		if (rectChanged_)
			rectChanged_(selectedIndex_, rect, save);
	}

	void updateCursorForPoint(const QPointF &point)
	{
		const int handle = handleAt(point);
		if (handle == (ResizeLeft | ResizeTop) || handle == (ResizeRight | ResizeBottom)) {
			setCursor(Qt::SizeFDiagCursor);
		} else if (handle == (ResizeRight | ResizeTop) || handle == (ResizeLeft | ResizeBottom)) {
			setCursor(Qt::SizeBDiagCursor);
		} else if (handle & (ResizeLeft | ResizeRight)) {
			setCursor(Qt::SizeHorCursor);
		} else if (handle & (ResizeTop | ResizeBottom)) {
			setCursor(Qt::SizeVerCursor);
		} else if (itemAt(point) >= 0) {
			setCursor(Qt::SizeAllCursor);
		} else {
			unsetCursor();
		}
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton) {
			QWidget::mousePressEvent(event);
			return;
		}

		const QPointF point = event->position();
		const int handle = handleAt(point);
		const int clickedItem = handle ? selectedIndex_ : itemAt(point);

		if (clickedItem < 0) {
			dragMode_ = DragMode::None;
			dragEdges_ = 0;
			clearActiveSnapGuides();
			selectedIndex_ = -1;
			render_->setSelectedIndex(-1);
			if (selectionChanged_)
				selectionChanged_(-1);
			unsetCursor();
			event->accept();
			return;
		}

		selectedIndex_ = clickedItem;
		render_->setSelectedIndex(clickedItem);
		setFocus(Qt::MouseFocusReason);
		if (selectionChanged_)
			selectionChanged_(clickedItem);

		dragMode_ = handle ? DragMode::Resize : DragMode::Move;
		dragEdges_ = handle;
		dragStart_ = widgetToLayout(point);
		originalRect_ = displayRectForItem(clickedItem);
		grabMouse();
		setCursor(handle ? cursor() : Qt::SizeAllCursor);
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		const QPointF point = event->position();
		if (dragMode_ == DragMode::None || !validIndex(selectedIndex_)) {
			updateCursorForPoint(point);
			return;
		}

		const QPointF current = widgetToLayout(point);
		const QPointF delta = current - dragStart_;
		QRectF next = originalRect_;
		if (dragMode_ == DragMode::Move) {
			next.translate(delta);
		} else {
			if (dragEdges_ & ResizeLeft)
				next.setLeft(originalRect_.left() + delta.x());
			if (dragEdges_ & ResizeRight)
				next.setRight(originalRect_.right() + delta.x());
			if (dragEdges_ & ResizeTop)
				next.setTop(originalRect_.top() + delta.y());
			if (dragEdges_ & ResizeBottom)
				next.setBottom(originalRect_.bottom() + delta.y());
		}

		next = clampedRect(next, dragMode_, dragEdges_);
		const bool preserveFitAspect = dragMode_ == DragMode::Resize &&
					       layout_.items[selectedIndex_].fitMode == FitMode::Fit;
		if (preserveFitAspect)
			next = aspectConstrainedResize(next, originalRect_, dragEdges_);
		const bool useSnap = snappingEnabled_ && !(event->modifiers() & Qt::AltModifier);
		if (useSnap)
			next = snappedRect(next, dragMode_, dragEdges_);
		if (preserveFitAspect)
			next = aspectConstrainedResize(next, originalRect_, dragEdges_);
		if (useSnap) {
			updateActiveSnapGuides(next);
		} else {
			clearActiveSnapGuides();
		}
		commitRect(next, false);
		event->accept();
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (dragMode_ != DragMode::None && validIndex(selectedIndex_) && rectChanged_)
			rectChanged_(selectedIndex_, layout_.items[selectedIndex_].rect, true);
		if (mouseGrabber() == this)
			releaseMouse();
		dragMode_ = DragMode::None;
		dragEdges_ = 0;
		clearActiveSnapGuides();
		updateCursorForPoint(event->position());
		event->accept();
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		if (!validIndex(selectedIndex_)) {
			QWidget::keyPressEvent(event);
			return;
		}

		QPointF delta;
		switch (event->key()) {
		case Qt::Key_Left:
			delta.setX(-1.0);
			break;
		case Qt::Key_Right:
			delta.setX(1.0);
			break;
		case Qt::Key_Up:
			delta.setY(-1.0);
			break;
		case Qt::Key_Down:
			delta.setY(1.0);
			break;
		default:
			QWidget::keyPressEvent(event);
			return;
		}

		double step = 1.0;
		if (event->modifiers() & Qt::ShiftModifier)
			step = 10.0;
		if (event->modifiers() & Qt::ControlModifier)
			step = 50.0;
		QRectF next = displayRectForItem(selectedIndex_).translated(delta * step);
		commitRect(clampedRect(next, DragMode::Move, 0), true);
		event->accept();
	}

	VerticalLayout layout_;
	VerticalRenderWidget *render_ = nullptr;
	QVector<QRectF> itemDisplayRects_;
	int selectedIndex_ = -1;
	DragMode dragMode_ = DragMode::None;
	int dragEdges_ = 0;
	QPointF dragStart_;
	QRectF originalRect_;
	bool snappingEnabled_ = true;
	std::function<void(int)> selectionChanged_;
	std::function<void(int, const QRectF &, bool)> rectChanged_;
};

VerticalLayoutEditor::VerticalLayoutEditor(OutputManager *manager, QWidget *parent)
	: QWidget(parent),
	  manager_(manager)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);
	saveTimer_ = new QTimer(this);
	saveTimer_->setSingleShot(true);
	saveTimer_->setInterval(250);
	connect(saveTimer_, &QTimer::timeout, this, [this]() { saveNow(); });
	connect(manager_, &OutputManager::verticalLayoutChanged, this, &VerticalLayoutEditor::refreshItems);

	const auto makeTextToolButton = [this](const QString &text, const QString &toolTip) {
		auto *button = new QToolButton(this);
		button->setText(text);
		button->setToolTip(toolTip);
		button->setAccessibleName(toolTip);
		button->setAutoRaise(true);
		button->setFixedSize(28, 26);
		button->setStyleSheet(QStringLiteral("QToolButton { font-size: 17px; font-weight: 600; }"));
		return button;
	};
	const auto makeIconToolButton = [this](QStyle::StandardPixmap icon, const QString &toolTip) {
		auto *button = new QToolButton(this);
		button->setIcon(style()->standardIcon(icon));
		button->setToolTip(toolTip);
		button->setAccessibleName(toolTip);
		button->setAutoRaise(true);
		button->setFixedSize(28, 26);
		return button;
	};

	activeSceneStatus_ = new QLabel(this);
	activeSceneStatus_->setObjectName(QStringLiteral("dskVerticalProgramLabel"));
	activeSceneStatus_->setStyleSheet(QStringLiteral("QLabel { font-weight: 700; padding: 2px 4px; }"));
	// The program label must not define the dock's minimum width. At compact
	// widths Qt clips the text while leaving Snap and Setup usable.
	activeSceneStatus_->setMinimumWidth(0);
	activeSceneStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	snapping_ = new QCheckBox("Snap", this);
	snapping_->setChecked(true);
	snapping_->setToolTip("Snap source edges and centers. Hold Alt while dragging to bypass.");

	setupToggle_ = new QToolButton(this);
	setupToggle_->setObjectName(QStringLiteral("dskVerticalSetupToggle"));
	setupToggle_->setText(QStringLiteral("Setup"));
	setupToggle_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
	setupToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	setupToggle_->setCheckable(true);
	setupToggle_->setChecked(verticalSetupVisiblePreference());
	setupToggle_->setAutoRaise(true);
	setupToggle_->setToolTip(QStringLiteral("Show vertical scene and source setup."));
	transformToggle_ = new QToolButton(this);
	transformToggle_->setText(QStringLiteral("Transform"));
	transformToggle_->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
	transformToggle_->setCheckable(true);
	transformToggle_->setAutoRaise(true);
	transformToggle_->setToolTip(QStringLiteral("Show transform controls for the selected vertical source."));
	obsLinksToggle_ = new QToolButton(this);
	obsLinksToggle_->setText(QStringLiteral("OBS Link"));
	obsLinksToggle_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
	obsLinksToggle_->setCheckable(true);
	obsLinksToggle_->setAutoRaise(true);
	obsLinksToggle_->setToolTip(QStringLiteral("Show OBS scene link controls."));

	auto *topBar = new VerticalToolbarWidget(activeSceneStatus_, snapping_, setupToggle_, transformToggle_,
					       obsLinksToggle_, this);
	topBar->setObjectName(QStringLiteral("dskVerticalResponsiveToolbar"));
	root->addWidget(topBar);

	preview_ = new VerticalPreviewWidget(this);
	preview_->setObjectName(QStringLiteral("dskVerticalPreview"));
	connect(snapping_, &QCheckBox::toggled, preview_, &VerticalPreviewWidget::setSnappingEnabled);
	preview_->setSelectionChanged([this](int row) {
		if (row < 0) {
			clearSourceSelection();
			return;
		}
		if (items_->currentRow() != row)
			items_->setCurrentRow(row);
		else
			selectItem(row);
	});
	preview_->setRectChanged(
		[this](int row, const QRectF &rect, bool save) { updateItemRectFromPreview(row, rect, save); });
	root->addWidget(preview_, 1);

	auto *transformPanel = new QWidget(this);
	transformPanel->setObjectName(QStringLiteral("dskVerticalTransformPanel"));
	auto *transformLayout = new QGridLayout(transformPanel);
	transformLayout->setContentsMargins(4, 3, 4, 3);
	transformLayout->setHorizontalSpacing(5);
	transformLayout->setVerticalSpacing(3);
	x_ = new QSpinBox(this);
	y_ = new QSpinBox(this);
	w_ = new QSpinBox(this);
	h_ = new QSpinBox(this);
	x_->setRange(-4000, 4000);
	y_->setRange(-4000, 4000);
	w_->setRange(1, 4000);
	h_->setRange(1, 4000);
	for (auto *spin : {x_, y_, w_, h_}) {
		spin->setMinimumWidth(74);
		connect(spin, &QSpinBox::valueChanged, this, &VerticalLayoutEditor::updateSelectedItem);
	}
	fit_ = new QComboBox(this);
	fit_->addItem("Fit", fitModeToString(FitMode::Fit));
	fit_->addItem("Fill", fitModeToString(FitMode::Fill));
	fit_->addItem("Stretch", fitModeToString(FitMode::Stretch));
	connect(fit_, &QComboBox::currentIndexChanged, this, &VerticalLayoutEditor::updateSelectedItem);
	visible_ = new QCheckBox("Visible", this);
	connect(visible_, &QCheckBox::checkStateChanged, this, &VerticalLayoutEditor::updateSelectedItem);

	auto *center = new QPushButton("Center", this);
	auto *fitCanvas = new QPushButton("Fit Canvas", this);
	auto *fillCanvas = new QPushButton("Fill Canvas", this);
	connect(center, &QPushButton::clicked, this, &VerticalLayoutEditor::centerSelectedItem);
	connect(fitCanvas, &QPushButton::clicked, this, &VerticalLayoutEditor::fitSelectedItemToCanvas);
	connect(fillCanvas, &QPushButton::clicked, this, &VerticalLayoutEditor::fillSelectedItemToCanvas);

	transformLayout->addWidget(new QLabel("X", this), 0, 0);
	transformLayout->addWidget(x_, 0, 1);
	transformLayout->addWidget(new QLabel("Y", this), 0, 2);
	transformLayout->addWidget(y_, 0, 3);
	transformLayout->addWidget(new QLabel("W", this), 0, 4);
	transformLayout->addWidget(w_, 0, 5);
	transformLayout->addWidget(new QLabel("H", this), 0, 6);
	transformLayout->addWidget(h_, 0, 7);
	transformLayout->addWidget(fit_, 1, 0, 1, 2);
	transformLayout->addWidget(visible_, 1, 2);
	transformLayout->addWidget(center, 1, 3, 1, 2);
	transformLayout->addWidget(fitCanvas, 1, 5);
	transformLayout->addWidget(fillCanvas, 1, 6, 1, 2);
	transformPanel->setVisible(false);
	connect(transformToggle_, &QToolButton::toggled, transformPanel, &QWidget::setVisible);
	root->addWidget(transformPanel);

	auto *workspace = new QSplitter(Qt::Horizontal, this);
	workspace->setObjectName(QStringLiteral("dskVerticalLists"));
	setupPanel_ = workspace;
	workspace->setChildrenCollapsible(false);
	workspace->setMinimumHeight(170);

	auto *scenesPanel = new QWidget(workspace);
	auto *scenesLayout = new QVBoxLayout(scenesPanel);
	scenesLayout->setContentsMargins(0, 0, 0, 0);
	scenesLayout->setSpacing(3);
	auto *scenesLabel = new QLabel(QStringLiteral("Vertical Scenes"), scenesPanel);
	scenesLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: 700; padding-left: 2px; }"));
	verticalScenes_ = new QListWidget(scenesPanel);
	verticalScenes_->setObjectName(QStringLiteral("dskVerticalScenes"));
	verticalScenes_->setSelectionMode(QAbstractItemView::SingleSelection);
	verticalScenes_->setIconSize(QSize(16, 16));
	verticalScenes_->setDragDropMode(QAbstractItemView::InternalMove);
	verticalScenes_->setDefaultDropAction(Qt::MoveAction);
	verticalScenes_->setDropIndicatorShown(true);
	verticalScenes_->setAccessibleName(QStringLiteral("Vertical scenes. Drag to reorder."));
	verticalScenes_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(verticalScenes_, &QListWidget::currentRowChanged, this, &VerticalLayoutEditor::selectVerticalScene);
	connect(verticalScenes_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { renameScene(); });
	connect(verticalScenes_, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
		auto *sceneItem = verticalScenes_->itemAt(position);
		if (!sceneItem)
			return;
		verticalScenes_->setCurrentItem(sceneItem);
		QMenu contextMenu(verticalScenes_);
		auto *renameAction = contextMenu.addAction(QStringLiteral("Rename"));
		if (contextMenu.exec(verticalScenes_->viewport()->mapToGlobal(position)) == renameAction)
			renameScene();
	});
	connect(verticalScenes_->model(), &QAbstractItemModel::rowsMoved, this,
		[this](const QModelIndex &, int, int, const QModelIndex &, int) {
			if (loading_)
				return;
			const QVector<QString> orderedIds = listStableIds(verticalScenes_);
			QTimer::singleShot(0, this, [this, orderedIds]() {
				if (!manager_->reorderVerticalScenes(orderedIds))
					refreshItems();
			});
		});

	auto *sceneTools = new QHBoxLayout();
	sceneTools->setContentsMargins(0, 0, 0, 0);
	sceneTools->setSpacing(1);
	auto *newScene = makeTextToolButton("+", QStringLiteral("Create vertical scene"));
	auto *removeSceneButton = makeTextToolButton("-", QStringLiteral("Remove vertical scene"));
	auto *sceneUp = makeIconToolButton(QStyle::SP_ArrowUp, QStringLiteral("Move vertical scene up"));
	auto *sceneDown = makeIconToolButton(QStyle::SP_ArrowDown, QStringLiteral("Move vertical scene down"));
	connect(newScene, &QToolButton::clicked, this, &VerticalLayoutEditor::createScene);
	connect(removeSceneButton, &QToolButton::clicked, this, &VerticalLayoutEditor::removeScene);
	connect(sceneUp, &QToolButton::clicked, this, &VerticalLayoutEditor::moveSceneUp);
	connect(sceneDown, &QToolButton::clicked, this, &VerticalLayoutEditor::moveSceneDown);
	sceneTools->addWidget(newScene);
	sceneTools->addWidget(removeSceneButton);
	sceneTools->addStretch(1);
	sceneTools->addWidget(sceneUp);
	sceneTools->addWidget(sceneDown);
	scenesLayout->addWidget(scenesLabel);
	scenesLayout->addWidget(verticalScenes_, 1);
	scenesLayout->addLayout(sceneTools);

	auto *sourcesPanel = new QWidget(workspace);
	auto *sourcesLayout = new QVBoxLayout(sourcesPanel);
	sourcesLayout->setContentsMargins(0, 0, 0, 0);
	sourcesLayout->setSpacing(3);
	auto *sourcesTitleRow = new QHBoxLayout();
	auto *sourcesLabel = new QLabel(QStringLiteral("Vertical Sources"), sourcesPanel);
	sourcesLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: 700; padding-left: 2px; }"));
	sourcesHeader_ = new QLabel(sourcesPanel);
	sourcesHeader_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	sourcesHeader_->setStyleSheet(QStringLiteral("QLabel { color: #aeb4be; padding-right: 2px; }"));
	sourcesTitleRow->addWidget(sourcesLabel);
	sourcesTitleRow->addWidget(sourcesHeader_, 1);
	items_ = new QListWidget(sourcesPanel);
	items_->setObjectName(QStringLiteral("dskVerticalSources"));
	items_->setSelectionMode(QAbstractItemView::SingleSelection);
	items_->setIconSize(QSize(16, 16));
	items_->setDragDropMode(QAbstractItemView::InternalMove);
	items_->setDefaultDropAction(Qt::MoveAction);
	items_->setDropIndicatorShown(true);
	items_->setAccessibleName(QStringLiteral("Vertical sources. Drag to reorder layers."));
	connect(items_, &QListWidget::currentRowChanged, this, &VerticalLayoutEditor::selectItem);
	connect(items_, &QListWidget::itemChanged, this, &VerticalLayoutEditor::updateItemVisibility);
	connect(items_, &QListWidget::itemDoubleClicked, this,
		[this](QListWidgetItem *) { transformToggle_->setChecked(true); });
	connect(items_->model(), &QAbstractItemModel::rowsMoved, this,
		[this](const QModelIndex &, int, int, const QModelIndex &, int) {
			if (loading_)
				return;
			const QVector<QString> orderedIds = listStableIds(items_);
			QTimer::singleShot(0, this, [this, orderedIds]() {
				VerticalLayout layout = manager_->layouts().verticalLayout();
				QVector<VerticalLayoutItem> reordered;
				if (!reorderValuesByStableIds(layout.items, orderedIds, &reordered)) {
					refreshItems();
					return;
				}
				bool changed = false;
				for (int index = 0; index < reordered.size(); ++index) {
					if (reordered[index].id != layout.items[index].id) {
						changed = true;
						break;
					}
				}
				if (!changed) {
					refreshItems();
					return;
				}
				layout.items = std::move(reordered);
				manager_->layouts().setVerticalLayout(layout);
				manager_->saveVerticalLayout();
			});
		});

	source_ = new QComboBox(this);
	source_->hide();
	auto *sourceTools = new QHBoxLayout();
	sourceTools->setContentsMargins(0, 0, 0, 0);
	sourceTools->setSpacing(1);
	auto *addSourceButton = makeTextToolButton("+", QStringLiteral("Add OBS source to vertical scene"));
	auto *sourceMenu = new QMenu(addSourceButton);
	connect(sourceMenu, &QMenu::aboutToShow, this, [this, sourceMenu]() {
		refreshSourceList();
		sourceMenu->clear();
		for (int i = 0; i < source_->count(); ++i) {
			const QString sourceName = source_->itemText(i);
			auto *action = sourceMenu->addAction(source_->itemIcon(i), sourceName);
			connect(action, &QAction::triggered, this, [this, sourceName]() {
				const int sourceIndex = source_->findText(sourceName);
				if (sourceIndex >= 0)
					source_->setCurrentIndex(sourceIndex);
				addItem();
			});
		}
		if (sourceMenu->isEmpty()) {
			auto *empty = sourceMenu->addAction(QStringLiteral("No OBS sources available"));
			empty->setEnabled(false);
		}
	});
	connect(addSourceButton, &QToolButton::clicked, this, [addSourceButton, sourceMenu]() {
		sourceMenu->popup(addSourceButton->mapToGlobal(QPoint(0, addSourceButton->height())));
	});
	auto *removeSourceButton = makeTextToolButton("-", QStringLiteral("Remove vertical source"));
	auto *sourceUp = makeIconToolButton(QStyle::SP_ArrowUp, QStringLiteral("Move source up"));
	auto *sourceDown = makeIconToolButton(QStyle::SP_ArrowDown, QStringLiteral("Move source down"));
	connect(removeSourceButton, &QToolButton::clicked, this, &VerticalLayoutEditor::removeItem);
	connect(sourceUp, &QToolButton::clicked, this, &VerticalLayoutEditor::moveItemUp);
	connect(sourceDown, &QToolButton::clicked, this, &VerticalLayoutEditor::moveItemDown);
	sourceTools->addWidget(addSourceButton);
	sourceTools->addWidget(removeSourceButton);
	sourceTools->addStretch(1);
	sourceTools->addWidget(sourceUp);
	sourceTools->addWidget(sourceDown);
	sourcesLayout->addLayout(sourcesTitleRow);
	sourcesLayout->addWidget(items_, 1);
	sourcesLayout->addLayout(sourceTools);

	workspace->addWidget(scenesPanel);
	workspace->addWidget(sourcesPanel);
	workspace->setStretchFactor(0, 1);
	workspace->setStretchFactor(1, 1);
	workspace->setSizes({260, 340});
	root->addWidget(workspace);

	followScene_ = new QCheckBox("Follow OBS scene", this);
	followScene_->setToolTip("Activate the linked DSK vertical scene when the main OBS scene changes.");
	followScene_->setChecked(manager_->followObsScene());
	connect(followScene_, &QCheckBox::checkStateChanged, this,
		[this](Qt::CheckState state) { updateFollowScene(int(state)); });

	auto *sceneLinkRow = new QHBoxLayout();
	sceneLinkScene_ = new QComboBox(this);
	sceneLinkScene_->setObjectName(QStringLiteral("dskObsLinkScene"));
	sceneLinkTemplate_ = new QComboBox(this);
	sceneLinkTemplate_->setObjectName(QStringLiteral("dskObsLinkVerticalScene"));
	sceneLinkTemplate_->setToolTip("DSK vertical scene to activate when the selected OBS scene becomes active.");
	auto *linkScene = new QPushButton("Link", this);
	auto *unlinkScene = new QPushButton("Unlink", this);
	unlinkScene->setObjectName(QStringLiteral("dskObsUnlinkScene"));
	auto *refreshScenes = new QPushButton("Refresh", this);
	connect(linkScene, &QPushButton::clicked, this, &VerticalLayoutEditor::addSceneLink);
	connect(unlinkScene, &QPushButton::clicked, this, &VerticalLayoutEditor::removeSceneLink);
	connect(refreshScenes, &QPushButton::clicked, this, &VerticalLayoutEditor::refreshSceneList);
	connect(sceneLinkScene_, &QComboBox::currentTextChanged, this, &VerticalLayoutEditor::selectSceneLinkForObsScene);
	sceneLinkRow->addWidget(sceneLinkScene_, 1);
	sceneLinkRow->addWidget(sceneLinkTemplate_);
	sceneLinkRow->addWidget(linkScene);
	sceneLinkRow->addWidget(unlinkScene);
	sceneLinkRow->addWidget(refreshScenes);

	sceneLinks_ = new QListWidget(this);
	sceneLinks_->setObjectName(QStringLiteral("dskObsSceneLinks"));
	sceneLinks_->setMaximumHeight(68);
	connect(sceneLinks_, &QListWidget::currentRowChanged, this, &VerticalLayoutEditor::selectSceneLink);

	auto *obsLinksPage = new QWidget(this);
	auto *obsLinksControls = new QVBoxLayout(obsLinksPage);
	obsLinksControls->setContentsMargins(4, 3, 4, 3);
	obsLinksControls->setSpacing(4);
	obsLinkStatus_ = new QLabel(this);
	obsLinkStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	obsLinkStatus_->setStyleSheet(QStringLiteral("QLabel { color: #cfd4dc; padding: 2px; }"));
	auto *followRow = new QHBoxLayout();
	followRow->addWidget(followScene_);
	followRow->addWidget(obsLinkStatus_, 1);
	obsLinksControls->addLayout(followRow);
	obsLinksControls->addLayout(sceneLinkRow);
	obsLinksControls->addWidget(sceneLinks_, 1);
	obsLinksPage->setVisible(false);
	connect(obsLinksToggle_, &QToolButton::toggled, obsLinksPage, &QWidget::setVisible);
	root->addWidget(obsLinksPage);
	connect(setupToggle_, &QToolButton::toggled, this,
		[this](bool visible) { setSetupVisible(visible, true); });

	loading_ = true;
	loading_ = false;
	refreshSceneList();
	refreshSourceList();
	refreshItems();
	setSetupVisible(setupToggle_->isChecked(), false);
	clearSourceSelection();
}

void VerticalLayoutEditor::prepareForUnload()
{
	if (saveTimer_)
		saveTimer_->stop();
	if (preview_)
		preview_->prepareForUnload();
}

void VerticalLayoutEditor::handleSceneCollectionChanged()
{
	refreshSceneList();
	refreshSourceList();
	refreshItems();
	if (preview_)
		preview_->handleSceneCollectionChanged();
}

#ifdef DSK_INCLUDE_E2E_HOOKS
bool VerticalLayoutEditor::exercisePreviewCanvasReplacementForTest()
{
	return preview_ && preview_->exerciseCanvasReplacementForTest();
}

bool VerticalLayoutEditor::exerciseSourceVisibilityToggleForTest()
{
	if (!items_ || items_->count() == 0 || !manager_)
		return false;
	const int row = items_->currentRow() >= 0 ? items_->currentRow() : 0;
	auto *item = items_->item(row);
	const auto &layout = manager_->layouts().verticalLayout();
	if (!item || row < 0 || row >= layout.items.size())
		return false;
	const bool initiallyVisible = layout.items[row].visible;
	item->setCheckState(initiallyVisible ? Qt::Unchecked : Qt::Checked);
	item->setCheckState(initiallyVisible ? Qt::Checked : Qt::Unchecked);
	return manager_->layouts().verticalLayout().items[row].visible == initiallyVisible;
}

bool VerticalLayoutEditor::exerciseSetupVisibilityToggleForTest()
{
	if (!setupToggle_ || !setupPanel_ || !transformToggle_ || !obsLinksToggle_ || !preview_)
		return false;

	const bool originalVisible = setupToggle_->isChecked();
	{
		const QSignalBlocker blocker(setupToggle_);
		setupToggle_->setChecked(false);
	}
	setSetupVisible(false, false);
	refreshItems();
	const bool hidden = !setupPanel_->isVisible() && !transformToggle_->isVisible() &&
			    !obsLinksToggle_->isVisible() && preview_->isVisible() && items_->currentRow() < 0;

	{
		const QSignalBlocker blocker(setupToggle_);
		setupToggle_->setChecked(true);
	}
	setSetupVisible(true, false);
	const bool shown = setupPanel_->isVisible() && transformToggle_->isVisible() &&
			   obsLinksToggle_->isVisible() && preview_->isVisible();

	{
		const QSignalBlocker blocker(setupToggle_);
		setupToggle_->setChecked(originalVisible);
	}
	setSetupVisible(originalVisible, false);
	return hidden && shown;
}
#endif

void VerticalLayoutEditor::refreshSourceList()
{
	source_->clear();

	obs_enum_sources(
		[](void *context, obs_source_t *source) {
			auto *combo = static_cast<QComboBox *>(context);
			const char *name = obs_source_get_name(source);
			const QString sourceName = name ? QString::fromUtf8(name) : QString();
			if (!sourceName.isEmpty() && sourceName != QStringLiteral("DSK Vertical Layout") &&
			    sourceName != QStringLiteral("DSK Vertical Program") &&
			    sourceName != QStringLiteral("DSK Vertical Preview"))
				combo->addItem(obsSourceTypeIcon(source), sourceName);
			return true;
		},
		source_);
}

void VerticalLayoutEditor::refreshSceneList()
{
	const QString selected = sceneLinkScene_->currentText();
	const QSignalBlocker blocker(sceneLinkScene_);
	sceneLinkScene_->clear();

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *scene = scenes.sources.array[i];
		const char *name = obs_source_get_name(scene);
		if (name && *name)
			sceneLinkScene_->addItem(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&scenes);

	const int index = sceneLinkScene_->findText(selected);
	if (index >= 0)
		sceneLinkScene_->setCurrentIndex(index);
	selectSceneLinkForObsScene(sceneLinkScene_->currentText());
}

void VerticalLayoutEditor::createScene()
{
	bool accepted = false;
	const QString requestedName =
		QInputDialog::getText(this, QStringLiteral("New DSK Vertical Scene"), QStringLiteral("Scene name"),
				      QLineEdit::Normal, QStringLiteral("Vertical Scene"), &accepted)
			.trimmed();
	if (!accepted || requestedName.isEmpty())
		return;

	const QString id = manager_->createVerticalScene(requestedName);
	if (id.isEmpty())
		return;
	refreshItems();
	for (int row = 0; row < verticalScenes_->count(); ++row) {
		if (verticalScenes_->item(row)->data(Qt::UserRole).toString() == id) {
			verticalScenes_->setCurrentRow(row);
			break;
		}
	}
}

void VerticalLayoutEditor::removeScene()
{
	const int row = verticalScenes_ ? verticalScenes_->currentRow() : -1;
	if (row < 0 || manager_->layouts().verticalScenes().size() <= 1)
		return;
	const auto *item = verticalScenes_->item(row);
	if (!item)
		return;
	const QString sceneId = item->data(Qt::UserRole).toString();
	const QString sceneName = item->text();
	if (QMessageBox::question(this,
				  QStringLiteral("Remove Vertical Scene"),
				  QStringLiteral("Remove '%1'?").arg(sceneName),
				  QMessageBox::Yes | QMessageBox::No,
				  QMessageBox::No) != QMessageBox::Yes)
		return;
	manager_->removeVerticalScene(sceneId);
}

void VerticalLayoutEditor::renameScene()
{
	const int row = verticalScenes_ ? verticalScenes_->currentRow() : -1;
	if (row < 0)
		return;
	const auto *item = verticalScenes_->item(row);
	if (!item)
		return;
	const QString sceneId = item->data(Qt::UserRole).toString();
	const QString currentName = item->text();
	bool accepted = false;
	const QString name = QInputDialog::getText(this,
						     QStringLiteral("Rename Vertical Scene"),
						     QStringLiteral("Scene name"),
						     QLineEdit::Normal,
						     currentName,
						     &accepted)
				     .trimmed();
	if (accepted && !name.isEmpty())
		manager_->renameVerticalScene(sceneId, name);
}

void VerticalLayoutEditor::moveSceneUp()
{
	const int row = verticalScenes_ ? verticalScenes_->currentRow() : -1;
	if (row <= 0)
		return;
	const auto *item = verticalScenes_->item(row);
	if (item && manager_->moveVerticalScene(item->data(Qt::UserRole).toString(), -1))
		verticalScenes_->setCurrentRow(row - 1);
}

void VerticalLayoutEditor::moveSceneDown()
{
	const int row = verticalScenes_ ? verticalScenes_->currentRow() : -1;
	if (row < 0 || row >= verticalScenes_->count() - 1)
		return;
	const auto *item = verticalScenes_->item(row);
	if (item && manager_->moveVerticalScene(item->data(Qt::UserRole).toString(), 1))
		verticalScenes_->setCurrentRow(row + 1);
}

void VerticalLayoutEditor::selectVerticalScene(int row)
{
	if (loading_ || row < 0 || !verticalScenes_)
		return;
	const auto *item = verticalScenes_->item(row);
	if (!item)
		return;
	if (!manager_->layouts().selectVerticalScene(item->data(Qt::UserRole).toString()))
		return;
	QTimer::singleShot(0, this, [this]() { manager_->saveVerticalLayout(); });
}

void VerticalLayoutEditor::addSceneLink()
{
	if (sceneLinkScene_->currentText().trimmed().isEmpty() || sceneLinkTemplate_->currentData().toString().isEmpty())
		return;
	SceneLayoutLink link;
	link.sceneName = sceneLinkScene_->currentText();
	link.verticalSceneId = sceneLinkTemplate_->currentData().toString();
	manager_->upsertSceneLink(link);
	refreshItems();
	selectSceneLinkForObsScene(link.sceneName);
}

void VerticalLayoutEditor::removeSceneLink()
{
	QString sceneName = sceneLinkScene_->currentText();
	QString sceneUuid;
	if (const QListWidgetItem *selectedLink = sceneLinks_->currentItem()) {
		sceneName = selectedLink->data(SceneLinkNameRole).toString();
		sceneUuid = selectedLink->data(SceneLinkUuidRole).toString();
	}
	if (sceneName.trimmed().isEmpty())
		return;
	manager_->removeSceneLink(sceneName, sceneUuid);
	refreshItems();
}

void VerticalLayoutEditor::selectSceneLink(int row)
{
	if (loading_ || !sceneLinks_ || row < 0 || row >= sceneLinks_->count())
		return;

	const QListWidgetItem *item = sceneLinks_->item(row);
	if (!item)
		return;

	const QString sceneName = item->data(SceneLinkResolvedNameRole).toString();
	const int sceneIndex = sceneLinkScene_->findText(sceneName);
	if (sceneIndex >= 0)
		sceneLinkScene_->setCurrentIndex(sceneIndex);

	const int verticalSceneIndex = sceneLinkTemplate_->findData(item->data(SceneLinkVerticalSceneIdRole));
	if (verticalSceneIndex >= 0)
		sceneLinkTemplate_->setCurrentIndex(verticalSceneIndex);
}

void VerticalLayoutEditor::selectSceneLinkForObsScene(const QString &sceneName)
{
	if (loading_ || !sceneLinks_)
		return;

	for (int row = 0; row < sceneLinks_->count(); ++row) {
		const QListWidgetItem *item = sceneLinks_->item(row);
		if (item && item->data(SceneLinkResolvedNameRole).toString() == sceneName) {
			sceneLinks_->setCurrentRow(row);
			selectSceneLink(row);
			return;
		}
	}

	sceneLinks_->setCurrentRow(-1);
	sceneLinks_->clearSelection();
}

void VerticalLayoutEditor::updateFollowScene(int state)
{
	manager_->setFollowObsScene(state == Qt::Checked);
	updateObsLinkStatus();
}

void VerticalLayoutEditor::centerSelectedItem()
{
	const int index = selectedIndex();
	const auto &layout = manager_->layouts().verticalLayout();
	if (index < 0 || index >= layout.items.size())
		return;

	QRectF rect = displayedContentRect(layout.items[index], sourceVideoSize(layout.items[index].sourceName));
	rect.moveCenter(QPointF(layout.width / 2.0, layout.height / 2.0));
	setSelectedItemRect(rect);
}

void VerticalLayoutEditor::fitSelectedItemToCanvas()
{
	const int index = selectedIndex();
	VerticalLayout layout = manager_->layouts().verticalLayout();
	if (index < 0 || index >= layout.items.size())
		return;

	QSizeF sourceSize = sourceVideoSize(layout.items[index].sourceName);
	sourceSize = croppedSourceSize(layout.items[index], sourceSize);
	layout.items[index].fitMode = FitMode::Fit;
	layout.items[index].rect = centeredAspectFitRect(sourceSize, QSizeF(layout.width, layout.height));
	manager_->layouts().setVerticalLayout(layout);
	saveNow();
	refreshItems();
	items_->setCurrentRow(index);
}

void VerticalLayoutEditor::fillSelectedItemToCanvas()
{
	const int index = selectedIndex();
	VerticalLayout layout = manager_->layouts().verticalLayout();
	if (index < 0 || index >= layout.items.size())
		return;

	QSizeF sourceSize = sourceVideoSize(layout.items[index].sourceName);
	sourceSize = croppedSourceSize(layout.items[index], sourceSize);
	QRectF rect(0, 0, layout.width, layout.height);
	if (sourceSize.width() > 0.0 && sourceSize.height() > 0.0) {
		const double scale = std::max(layout.width / sourceSize.width(), layout.height / sourceSize.height());
		const QSizeF size(sourceSize.width() * scale, sourceSize.height() * scale);
		rect = QRectF((layout.width - size.width()) / 2.0,
			      (layout.height - size.height()) / 2.0,
			      size.width(),
			      size.height());
	}
	layout.items[index].fitMode = FitMode::Fill;
	layout.items[index].rect = rect;
	manager_->layouts().setVerticalLayout(layout);
	saveNow();
	refreshItems();
	items_->setCurrentRow(index);
}

void VerticalLayoutEditor::addItem()
{
	const QString sourceName = source_->currentText().trimmed();
	if (sourceName.isEmpty())
		return;

	VerticalLayout layout = manager_->layouts().verticalLayout();
	VerticalLayoutItem item;
	item.id = newTargetId();
	item.sourceName = sourceName;
	item.fitMode = FitMode::Fit;
	item.rect = centeredAspectFitRect(sourceVideoSize(sourceName), QSizeF(layout.width, layout.height));
	layout.items.push_back(item);
	manager_->layouts().setVerticalLayout(layout);
	manager_->saveVerticalLayout();
	refreshItems();
	items_->setCurrentRow(layout.items.size() - 1);
}

void VerticalLayoutEditor::removeItem()
{
	const int index = selectedIndex();
	if (index < 0)
		return;
	VerticalLayout layout = manager_->layouts().verticalLayout();
	layout.items.removeAt(index);
	manager_->layouts().setVerticalLayout(layout);
	manager_->saveVerticalLayout();
	refreshItems();
}

void VerticalLayoutEditor::moveItemUp()
{
	const int index = selectedIndex();
	if (index <= 0)
		return;
	VerticalLayout layout = manager_->layouts().verticalLayout();
	layout.items.move(index, index - 1);
	manager_->layouts().setVerticalLayout(layout);
	saveNow();
	refreshItems();
	items_->setCurrentRow(index - 1);
}

void VerticalLayoutEditor::moveItemDown()
{
	const int index = selectedIndex();
	const auto &current = manager_->layouts().verticalLayout();
	if (index < 0 || index >= current.items.size() - 1)
		return;
	VerticalLayout layout = current;
	layout.items.move(index, index + 1);
	manager_->layouts().setVerticalLayout(layout);
	saveNow();
	refreshItems();
	items_->setCurrentRow(index + 1);
}

void VerticalLayoutEditor::selectItem(int row)
{
	const auto &layout = manager_->layouts().verticalLayout();
	if (row < 0 || row >= layout.items.size()) {
		preview_->setSelectedIndex(-1);
		setLayerControlsEnabled(false);
		return;
	}

	loading_ = true;
	setLayerControlsEnabled(true);
	const auto &item = layout.items[row];
	x_->setValue(int(item.rect.x()));
	y_->setValue(int(item.rect.y()));
	w_->setValue(int(item.rect.width()));
	h_->setValue(int(item.rect.height()));
	const int fitIndex = fit_->findData(fitModeToString(item.fitMode));
	fit_->setCurrentIndex(fitIndex >= 0 ? fitIndex : 1);
	visible_->setChecked(item.visible);
	loading_ = false;
	preview_->setSelectedIndex(row);
}

void VerticalLayoutEditor::updateItemVisibility(QListWidgetItem *item)
{
	if (loading_ || !item || !items_)
		return;
	const int row = items_->row(item);
	const auto &current = manager_->layouts().verticalLayout();
	if (row < 0 || row >= current.items.size())
		return;
	const bool visible = item->checkState() == Qt::Checked;
	if (current.items[row].visible == visible)
		return;
	VerticalLayout layout = current;
	layout.items[row].visible = visible;
	manager_->layouts().setVerticalLayout(layout);
	preview_->setLayoutData(layout);
	QTimer::singleShot(0, this, [this]() { saveNow(); });
}

void VerticalLayoutEditor::updateSelectedItem()
{
	if (loading_)
		return;
	const int index = selectedIndex();
	if (index < 0)
		return;

	VerticalLayout layout = manager_->layouts().verticalLayout();
	auto &item = layout.items[index];
	item.rect = QRectF(x_->value(), y_->value(), w_->value(), h_->value());
	item.fitMode = fitModeFromString(fit_->currentData().toString());
	item.visible = visible_->isChecked();
	manager_->layouts().setVerticalLayout(layout);
	manager_->saveVerticalLayout();
	refreshItems();
	items_->setCurrentRow(index);
}

void VerticalLayoutEditor::refreshItems()
{
	if (!items_ || !verticalScenes_ || !sceneLinks_ || !sceneLinkTemplate_ || !preview_)
		return;

	const int selected = items_->currentRow();
	QString selectedSceneLinkName;
	QString selectedSceneLinkUuid;
	if (const QListWidgetItem *selectedLink = sceneLinks_->currentItem()) {
		selectedSceneLinkName = selectedLink->data(SceneLinkNameRole).toString();
		selectedSceneLinkUuid = selectedLink->data(SceneLinkUuidRole).toString();
	}
	loading_ = true;
	const QSignalBlocker itemsBlocker(items_);
	const QSignalBlocker verticalScenesBlocker(verticalScenes_);
	const QSignalBlocker linkSceneBlocker(sceneLinkTemplate_);
	const QSignalBlocker sceneLinksBlocker(sceneLinks_);
	items_->clear();
	verticalScenes_->clear();
	sceneLinks_->clear();
	const QString selectedLinkedSceneId = sceneLinkTemplate_->currentData().toString();
	sceneLinkTemplate_->clear();
	int activeVerticalSceneRow = -1;
	for (const auto &scene : manager_->layouts().verticalScenes()) {
		const bool activeScene = scene.id == manager_->layouts().activeVerticalSceneId();
		auto *sceneItem = new QListWidgetItem(activeScene ? style()->standardIcon(QStyle::SP_MediaPlay) : QIcon(),
						      scene.name);
		sceneItem->setData(Qt::UserRole, scene.id);
		sceneItem->setToolTip(activeScene ? QStringLiteral("Active DSK vertical output scene")
						  : QStringLiteral("DSK vertical scene"));
		verticalScenes_->addItem(sceneItem);
		if (activeScene)
			activeVerticalSceneRow = verticalScenes_->count() - 1;
		sceneLinkTemplate_->addItem(scene.name, scene.id);
	}
	if (activeVerticalSceneRow >= 0)
		verticalScenes_->setCurrentRow(activeVerticalSceneRow);
	const int linkedSceneIndex = sceneLinkTemplate_->findData(selectedLinkedSceneId);
	if (linkedSceneIndex >= 0)
		sceneLinkTemplate_->setCurrentIndex(linkedSceneIndex);
	int selectedSceneLinkRow = -1;
	for (const auto &link : manager_->sceneLinks()) {
		const QString sceneName = manager_->layouts().verticalSceneName(link.verticalSceneId);
		const QString linkedObsScene = manager_->resolvedObsSceneName(link.sceneUuid, link.sceneName);
		const QString linkedObsSceneDisplay = linkedObsScene.isEmpty()
						      ? QStringLiteral("Missing: %1").arg(link.sceneName)
						      : linkedObsScene;
		const QString prefix = sceneLinkMatches(link, manager_->currentObsSceneName(), manager_->currentObsSceneUuid())
					       ? QStringLiteral("Current: ")
					       : QString();
		auto *linkItem = new QListWidgetItem(QString("%1%2 -> %3").arg(
			prefix,
			linkedObsSceneDisplay,
			sceneName.isEmpty() ? QStringLiteral("(missing DSK scene)") : sceneName));
		linkItem->setData(SceneLinkNameRole, link.sceneName);
		linkItem->setData(SceneLinkUuidRole, link.sceneUuid);
		linkItem->setData(SceneLinkVerticalSceneIdRole, link.verticalSceneId);
		linkItem->setData(SceneLinkResolvedNameRole, linkedObsScene);
		linkItem->setToolTip(QStringLiteral("Select this link to edit or unlink it."));
		sceneLinks_->addItem(linkItem);
		const bool selectedLinkMatches = !selectedSceneLinkUuid.isEmpty()
						 ? link.sceneUuid == selectedSceneLinkUuid
						 : link.sceneName == selectedSceneLinkName;
		if (selectedLinkMatches)
			selectedSceneLinkRow = sceneLinks_->count() - 1;
	}
	updateObsLinkStatus();
	const auto &layout = manager_->layouts().verticalLayout();
	for (int i = 0; i < layout.items.size(); ++i) {
		const auto &layoutItem = layout.items[i];
		auto *sourceItem = new QListWidgetItem(obsSourceTypeIcon(layoutItem.sourceName),
							itemLabel(layoutItem, i));
		sourceItem->setData(Qt::UserRole, layoutItem.id);
		sourceItem->setFlags(sourceItem->flags() | Qt::ItemIsUserCheckable);
		sourceItem->setCheckState(layoutItem.visible ? Qt::Checked : Qt::Unchecked);
		sourceItem->setToolTip(QStringLiteral("%1x%2 at %3,%4")
					       .arg(std::lround(layoutItem.rect.width()))
					       .arg(std::lround(layoutItem.rect.height()))
					       .arg(std::lround(layoutItem.rect.x()))
					       .arg(std::lround(layoutItem.rect.y())));
		items_->addItem(sourceItem);
	}
	preview_->setLayoutData(layout);
	updateSceneStatus();
	loading_ = false;
	if (selectedSceneLinkRow >= 0) {
		sceneLinks_->setCurrentRow(selectedSceneLinkRow);
		selectSceneLink(selectedSceneLinkRow);
	} else {
		selectSceneLinkForObsScene(sceneLinkScene_->currentText());
	}
	if (!setupToggle_ || !setupToggle_->isChecked()) {
		clearSourceSelection();
	} else if (selected >= 0 && selected < layout.items.size()) {
		items_->setCurrentRow(selected);
		selectItem(selected);
	} else if (!layout.items.isEmpty()) {
		items_->setCurrentRow(0);
		selectItem(0);
	} else {
		preview_->setSelectedIndex(-1);
		setLayerControlsEnabled(false);
	}
}

int VerticalLayoutEditor::selectedIndex() const
{
	const int row = items_->currentRow();
	const auto &layout = manager_->layouts().verticalLayout();
	if (row < 0 || row >= layout.items.size())
		return -1;
	return row;
}

void VerticalLayoutEditor::setSelectedItemRect(const QRectF &rect)
{
	const int row = selectedIndex();
	const auto &currentLayout = manager_->layouts().verticalLayout();
	if (row < 0 || row >= currentLayout.items.size())
		return;

	VerticalLayout layout = currentLayout;
	layout.items[row].rect = rect;
	manager_->layouts().setVerticalLayout(layout);
	saveNow();
	refreshItems();
	items_->setCurrentRow(row);
}

void VerticalLayoutEditor::scheduleSave()
{
	if (saveTimer_)
		saveTimer_->start();
}

void VerticalLayoutEditor::saveNow()
{
	if (saveTimer_)
		saveTimer_->stop();
	if (manager_)
		manager_->saveVerticalLayout();
}

void VerticalLayoutEditor::setSetupVisible(bool visible, bool persist)
{
	if (!setupPanel_ || !transformToggle_ || !obsLinksToggle_)
		return;

	setupPanel_->setVisible(visible);
	transformToggle_->setVisible(visible);
	obsLinksToggle_->setVisible(visible);
	if (!visible) {
		if (items_)
			setupSelectedRow_ = items_->currentRow();
		transformToggle_->setChecked(false);
		obsLinksToggle_->setChecked(false);
		clearSourceSelection();
	} else if (items_ && setupSelectedRow_ >= 0 && setupSelectedRow_ < items_->count()) {
		items_->setCurrentRow(setupSelectedRow_);
		selectItem(setupSelectedRow_);
	}

	if (persist)
		saveVerticalSetupVisiblePreference(visible);
}

void VerticalLayoutEditor::clearSourceSelection()
{
	if (items_) {
		const QSignalBlocker blocker(items_);
		items_->setCurrentRow(-1);
		items_->clearSelection();
	}
	if (preview_)
		preview_->setSelectedIndex(-1);
	setLayerControlsEnabled(false);
}

void VerticalLayoutEditor::updateItemRectFromPreview(int row, const QRectF &rect, bool save)
{
	const auto &currentLayout = manager_->layouts().verticalLayout();
	if (row < 0 || row >= currentLayout.items.size())
		return;

	VerticalLayout layout = currentLayout;
	layout.items[row].rect = rect;
	manager_->layouts().setVerticalLayout(layout);
	if (save)
		saveNow();
	else
		scheduleSave();

	loading_ = true;
	x_->setValue(int(std::lround(rect.x())));
	y_->setValue(int(std::lround(rect.y())));
	w_->setValue(int(std::lround(rect.width())));
	h_->setValue(int(std::lround(rect.height())));
	if (auto *listItem = items_->item(row))
		listItem->setText(itemLabel(layout.items[row], row));
	loading_ = false;

	preview_->setLayoutData(layout);
	preview_->setSelectedIndex(row);
	if (items_->currentRow() != row)
		items_->setCurrentRow(row);
}

void VerticalLayoutEditor::setLayerControlsEnabled(bool enabled)
{
	for (auto *spin : {x_, y_, w_, h_}) {
		if (spin)
			spin->setEnabled(enabled);
	}
	if (fit_)
		fit_->setEnabled(enabled);
	if (visible_)
		visible_->setEnabled(enabled);
}

void VerticalLayoutEditor::updateSceneStatus()
{
	if (!manager_)
		return;

	const QString active = manager_->layouts().activeVerticalSceneName().isEmpty() ?
				       QStringLiteral("(none)") :
				       manager_->layouts().activeVerticalSceneName();

	if (activeSceneStatus_)
		activeSceneStatus_->setText(QStringLiteral("Vertical Program: %1").arg(active));

	if (sourcesHeader_)
		sourcesHeader_->setText(QString::number(manager_->layouts().verticalLayout().items.size()));
}

void VerticalLayoutEditor::updateObsLinkStatus()
{
	if (!obsLinkStatus_ || !manager_)
		return;

	const QString obsScene = currentFrontendSceneName();
	const QString obsSceneUuid = manager_->currentObsSceneUuid();
	QString linkedScene;
	for (const auto &link : manager_->sceneLinks()) {
		if (sceneLinkMatches(link, obsScene, obsSceneUuid)) {
			linkedScene = manager_->layouts().verticalSceneName(link.verticalSceneId);
			if (linkedScene.isEmpty())
				linkedScene = QStringLiteral("(missing DSK scene)");
			break;
		}
	}

	const QString obsName = obsScene.isEmpty() ? QStringLiteral("(no OBS scene)") : obsScene;
	const QString linkText = linkedScene.isEmpty() ? QStringLiteral("Not linked") : linkedScene;
	obsLinkStatus_->setText(QStringLiteral("%1 -> %2").arg(obsName, linkText));
	updateSceneStatus();
}

} // namespace dsk
