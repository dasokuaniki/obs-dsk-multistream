#pragma once

#include "ui/vertical-layout-metrics.hpp"
#include "ui/vertical-toolbar-layout.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace dsk {

class VerticalToolbarWidget final : public QWidget {
public:
	VerticalToolbarWidget(QLabel *status, QCheckBox *snapping, QToolButton *setup,
			      QToolButton *transform, QToolButton *obsLinks, QWidget *parent)
		: QWidget(parent), status_(status), snapping_(snapping), setup_(setup),
		  transform_(transform), obsLinks_(obsLinks), layout_(new QGridLayout(this))
	{
		layout_->setContentsMargins(0, 0, 0, 0);
		layout_->setHorizontalSpacing(VerticalToolbarMaximumSpacing);
		layout_->setVerticalSpacing(2);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		refreshMode();
	}

	QSize minimumSizeHint() const override
	{
		return QSize(VerticalPreviewMinimumWidth, layout_->minimumSize().height());
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		applyMode(event->size().width() < wideMinimumWidth_);
	}

	void changeEvent(QEvent *event) override
	{
		QWidget::changeEvent(event);
		if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
			QTimer::singleShot(0, this, [this]() { refreshMode(); });
	}

private:
	void refreshMode()
	{
		// Measure the current theme/font instead of assuming that all translated
		// text buttons fit at a fixed width. This also handles OBS UI scale changes.
		applyMode(false);
		wideMinimumWidth_ = qMax(VerticalToolbarCompactWidth, layout_->minimumSize().width());
		applyMode(width() < wideMinimumWidth_);
	}

	void applyMode(bool compact)
	{
		if (initialized_ && compact_ == compact)
			return;
		initialized_ = true;
		compact_ = compact;
		for (QWidget *widget : {static_cast<QWidget *>(status_), static_cast<QWidget *>(snapping_),
					static_cast<QWidget *>(setup_), static_cast<QWidget *>(transform_),
					static_cast<QWidget *>(obsLinks_)})
			layout_->removeWidget(widget);
		for (int column = 0; column <= 4; ++column)
			layout_->setColumnStretch(column, 0);
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
		layout_->activate();
		updateGeometry();
		update();
	}

	QLabel *status_ = nullptr;
	QCheckBox *snapping_ = nullptr;
	QToolButton *setup_ = nullptr;
	QToolButton *transform_ = nullptr;
	QToolButton *obsLinks_ = nullptr;
	QGridLayout *layout_ = nullptr;
	bool initialized_ = false;
	bool compact_ = false;
	int wideMinimumWidth_ = VerticalToolbarCompactWidth;
};

} // namespace dsk
