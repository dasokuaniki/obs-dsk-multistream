#pragma once

#include <obs-module.h>
#include <util/bmem.h>

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QString>
#include <QToolButton>

namespace dsk {

inline QString streamControlAssetPath(const char *relativePath)
{
	char *rawPath = obs_module_file(relativePath);
	if (!rawPath)
		return {};
	const QString path = QString::fromUtf8(rawPath);
	bfree(rawPath);
	return path;
}

inline void paintReferenceButtonFrame(QPainter &painter, const QRect &rect, const QColor &top,
				      const QColor &bottom, const QColor &border, bool hovered, bool pressed)
{
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QRectF frame = QRectF(rect).adjusted(0.75, 0.75, -0.75, -0.75);
	QLinearGradient fill(0, frame.top(), 0, frame.bottom());
	fill.setColorAt(0.0, hovered ? top.lighter(112) : top);
	fill.setColorAt(1.0, pressed ? bottom.darker(125) : bottom);
	painter.setPen(QPen(hovered ? border.lighter(125) : border, 1));
	painter.setBrush(fill);
	painter.drawRoundedRect(frame, 7, 7);
	painter.setPen(QPen(QColor(255, 255, 255, hovered ? 32 : 18), 0.8));
	painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5), 5.5, 5.5);
}

inline void paintControlFocus(QPainter &painter, const QRect &rect, bool focused)
{
	if (!focused)
		return;
	painter.setPen(QPen(QColor(QStringLiteral("#8aa4ff")), 1, Qt::DashLine));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(QRectF(rect).adjusted(2.5, 2.5, -2.5, -2.5), 5, 5);
}

class ReferenceAllButton final : public QPushButton {
public:
	explicit ReferenceAllButton(QWidget *parent = nullptr) : QPushButton(parent)
	{
		setAttribute(Qt::WA_Hover, true);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		const bool stopMode = text().contains(QStringLiteral("STOP"), Qt::CaseInsensitive);
		const QColor top = stopMode ? QColor(QStringLiteral("#8d4159")) : QColor(QStringLiteral("#6960e8"));
		const QColor bottom = stopMode ? QColor(QStringLiteral("#5e2738")) : QColor(QStringLiteral("#286ee7"));
		paintReferenceButtonFrame(painter, rect(), top, bottom,
					  stopMode ? QColor(QStringLiteral("#d86a80")) : QColor(QStringLiteral("#789aff")),
					  underMouse(), isDown());
		painter.setOpacity(isEnabled() ? 1.0 : 0.45);
		painter.setFont(font());
		painter.setPen(QColor(QStringLiteral("#f5f6ff")));
		painter.drawText(rect(), Qt::AlignCenter, text());
		painter.setOpacity(1.0);
		paintControlFocus(painter, rect(), hasFocus());
	}
};

class ReferenceActionButton final : public QPushButton {
public:
	explicit ReferenceActionButton(QWidget *parent = nullptr) : QPushButton(parent)
	{
		setAttribute(Qt::WA_Hover, true);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		paintReferenceButtonFrame(painter, rect(), QColor(QStringLiteral("#252c38")),
					  QColor(QStringLiteral("#171d27")), QColor(QStringLiteral("#353e4b")),
					  underMouse(), isDown());
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setOpacity(isEnabled() ? 1.0 : 0.45);
		const QString state = text().toUpper();
		if (state.contains(QStringLiteral("STOP")) && !state.contains(QStringLiteral("STOPPING"))) {
			const qreal side = qMin(width(), height()) * 0.28;
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(QStringLiteral("#ff7181")));
			painter.drawRoundedRect(QRectF(width() / 2.0 - side / 2.0, height() / 2.0 - side / 2.0,
						       side, side), 1.5, 1.5);
		} else if (state.contains(QStringLiteral("ING"))) {
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(QStringLiteral("#dce1ea")));
			for (int offset : {-7, 0, 7})
				painter.drawEllipse(QPointF(width() / 2.0 + offset, height() / 2.0), 1.7, 1.7);
		} else {
			const qreal iconHeight = qMin(width(), height()) * 0.46;
			const qreal iconWidth = iconHeight * 0.78;
			QPainterPath play;
			play.moveTo(width() / 2.0 - iconWidth * 0.42, height() / 2.0 - iconHeight / 2.0);
			play.lineTo(width() / 2.0 + iconWidth * 0.58, height() / 2.0);
			play.lineTo(width() / 2.0 - iconWidth * 0.42, height() / 2.0 + iconHeight / 2.0);
			play.closeSubpath();
			painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 0.8));
			painter.setBrush(QColor(QStringLiteral("#eef1f6")));
			painter.drawPath(play);
		}
		painter.setOpacity(1.0);
		paintControlFocus(painter, rect(), hasFocus());
	}
};

class ReferenceMenuButton final : public QToolButton {
public:
	explicit ReferenceMenuButton(QWidget *parent = nullptr) : QToolButton(parent)
	{
		setAttribute(Qt::WA_Hover, true);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		paintReferenceButtonFrame(painter, rect(), QColor(QStringLiteral("#252c38")),
					  QColor(QStringLiteral("#171d27")), QColor(QStringLiteral("#353e4b")),
					  underMouse(), isDown());
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setOpacity(isEnabled() ? 1.0 : 0.45);
		QPainterPath chevron;
		chevron.moveTo(width() / 2.0 - 5.0, height() / 2.0 - 2.0);
		chevron.lineTo(width() / 2.0, height() / 2.0 + 3.0);
		chevron.lineTo(width() / 2.0 + 5.0, height() / 2.0 - 2.0);
		painter.setPen(QPen(QColor(QStringLiteral("#eef1f6")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawPath(chevron);
		painter.setOpacity(1.0);
		paintControlFocus(painter, rect(), hasFocus());
	}
};

} // namespace dsk
