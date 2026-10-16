#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <array>

namespace dsk {

class CanvasSettingsScrollArea final : public QScrollArea {
public:
	explicit CanvasSettingsScrollArea(QWidget *content, QWidget *parent) : QScrollArea(parent)
	{
		setFrameShape(QFrame::NoFrame);
		setWidgetResizable(true);
		setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		setMaximumHeight(220);
		setWidget(content);
	}

	QSize minimumSizeHint() const override { return QSize(0, 60); }
	QSize sizeHint() const override
	{
		const QSize hint = QScrollArea::sizeHint();
		return QSize(qMin(240, hint.width()), qBound(60, hint.height(), 220));
	}
};

inline QWidget *createCanvasTransformPanel(QWidget *parent, const std::array<QSpinBox *, 4> &axes,
					   QComboBox *fit, QCheckBox *visible,
					   const std::array<QPushButton *, 3> &actions)
{
	auto *panel = new QWidget();
	auto *layout = new QFormLayout(panel);
	layout->setContentsMargins(4, 3, 4, 3);
	layout->setHorizontalSpacing(5);
	layout->setVerticalSpacing(3);
	layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const std::array<const char *, 4> names{"X", "Y", "W", "H"};
	for (int i = 0; i < 4; ++i)
		layout->addRow(QString::fromLatin1(names[i]), axes[i]);
	layout->addRow(QStringLiteral("Mode"), fit);
	layout->addRow(visible);
	for (auto *action : actions)
		layout->addRow(action);
	// Advanced controls may scroll, but must not set the surrounding OBS
	// dock's width or require their entire form height beside the preview.
	return new CanvasSettingsScrollArea(panel, parent);
}

inline QWidget *createCanvasObsLinksPanel(QWidget *parent, QCheckBox *follow, QLabel *status,
					  QComboBox *obsScene, QComboBox *canvasScene,
					  const std::array<QPushButton *, 3> &actions, QListWidget *links)
{
	auto *panel = new QWidget();
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(4, 3, 4, 3);
	layout->setSpacing(4);
	status->setTextFormat(Qt::PlainText);
	status->setMinimumWidth(0);
	status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(follow);
	layout->addWidget(status);
	auto *scenes = new QFormLayout();
	scenes->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scenes->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	for (auto *combo : {obsScene, canvasScene}) {
		combo->setMinimumContentsLength(4);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	}
	scenes->addRow(QStringLiteral("OBS"), obsScene);
	scenes->addRow(QStringLiteral("DSK"), canvasScene);
	layout->addLayout(scenes);
	for (auto *action : actions)
		layout->addWidget(action);
	layout->addWidget(links, 1);
	return new CanvasSettingsScrollArea(panel, parent);
}

} // namespace dsk
