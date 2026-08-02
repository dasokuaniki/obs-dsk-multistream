#pragma once

#include "ui/neutral-platform-badge.hpp"
#include "ui/stream-control-assets.hpp"

#include <QColor>
#include <QDesktopServices>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>
#include <QSizeF>
#include <QString>
#include <QSizePolicy>
#include <QToolButton>
#include <QUrl>
#include <QWidget>

namespace dsk {

class PlatformBadge final : public QToolButton {
public:
	explicit PlatformBadge(QString platformId, QString fallbackText, QWidget *parent = nullptr)
		: QToolButton(parent)
	{
		setFixedSize(30, 30);
		setAutoRaise(true);
		setStyleSheet(QStringLiteral("QToolButton { border: 0; background: transparent; padding: 0; }"));
		connect(this, &QToolButton::clicked, this, [this]() { openPlatformDestination(); });
		setIdentity(platformId, fallbackText);
	}

	void setIdentity(const QString &platformId, const QString &fallbackText)
	{
		const QString nextPlatformId = normalizedPlatformId(platformId, fallbackText);
		const QString nextFallback = fallbackText.trimmed().isEmpty()
					     ? QStringLiteral("?")
					     : fallbackText.trimmed().left(3).toUpper();
		if (platformId_ == nextPlatformId && fallbackText_ == nextFallback)
			return;
		platformId_ = nextPlatformId;
		fallbackText_ = nextFallback;
		const bool opensService = platformId_ == QStringLiteral("youtube") ||
					  platformId_ == QStringLiteral("twitch");
		setCursor(opensService ? Qt::PointingHandCursor : Qt::ArrowCursor);
		setAttribute(Qt::WA_TransparentForMouseEvents, !opensService);
		setFocusPolicy(opensService ? Qt::StrongFocus : Qt::NoFocus);
		setAccessibleName(accessiblePlatformName());
		setToolTip(platformToolTip());
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);

		if (platformId_ == QStringLiteral("youtube")) {
			const QPixmap &iconSheet = youtubeIconSheet();
			if (!iconSheet.isNull()) {
				// Keep the official glyph and surrounding clear space unmodified.
				const QRectF sourceRect(147, 45, 117, 88);
				painter.drawPixmap(fittedRect(QRectF(rect()), sourceRect.size()), iconSheet,
						   sourceRect);
				return;
			}
		} else if (platformId_ == QStringLiteral("twitch")) {
			const QPixmap &icon = twitchGlitchIcon();
			if (!icon.isNull()) {
				const QRectF bounds = QRectF(rect()).adjusted(3, 2, -3, -2);
				painter.drawPixmap(fittedRect(bounds, icon.size()), icon,
						   QRectF(icon.rect()));
				return;
			}
		} else if (platformId_ == QStringLiteral("kick")) {
			const QPixmap &icon = kickOfficialIcon();
			if (!icon.isNull()) {
				const QRectF bounds = QRectF(rect()).adjusted(3, 2, -3, -2);
				painter.drawPixmap(fittedRect(bounds, icon.size()), icon,
						   QRectF(icon.rect()));
				return;
			}
			drawNeutralKickGlyph(painter);
			return;
		} else if (platformId_ == QStringLiteral("tiktok")) {
			const QPixmap &icon = tiktokPersonalIcon();
			if (!icon.isNull()) {
				const QRectF bounds = QRectF(rect()).adjusted(3, 3, -3, -3);
				painter.drawPixmap(fittedRect(bounds, icon.size()), icon,
						   QRectF(icon.rect()));
				return;
			}
			drawNeutralTikTokGlyph(painter);
			return;
		}

		drawNeutralBadge(painter);
	}

private:
	static QString normalizedPlatformId(const QString &platformId, const QString &fallbackText)
	{
		const QString normalized = platformId.trimmed().toLower();
		if ((normalized.isEmpty() || normalized == QStringLiteral("custom")) &&
		    fallbackText.trimmed().compare(QStringLiteral("TikTok"), Qt::CaseInsensitive) == 0) {
			return QStringLiteral("tiktok");
		}
		return normalized;
	}

	static QRectF fittedRect(const QRectF &bounds, const QSizeF &sourceSize)
	{
		QSizeF fitted = sourceSize;
		fitted.scale(bounds.size(), Qt::KeepAspectRatio);
		return QRectF(bounds.center().x() - fitted.width() / 2.0,
			      bounds.center().y() - fitted.height() / 2.0, fitted.width(),
			      fitted.height());
	}

	static const QPixmap &youtubeIconSheet()
	{
		static const QPixmap icon(streamControlAssetPath("ui/youtube-icons-2x.png"));
		return icon;
	}

	static const QPixmap &twitchGlitchIcon()
	{
		static const QPixmap icon(streamControlAssetPath("ui/twitch-glitch-purple.png"));
		return icon;
	}

	static const QPixmap &kickOfficialIcon()
	{
		// Official unmodified Green Icon from KICK's public Brand Hub.
		// This trademark asset is not licensed under the project's GPL license.
		static const QPixmap icon = []() {
			QImageReader reader(streamControlAssetPath("ui/kick-icon-green.png"));
			QSize decodedSize = reader.size();
			if (decodedSize.isValid()) {
				decodedSize.scale(QSize(48, 48), Qt::KeepAspectRatio);
				reader.setScaledSize(decodedSize);
			}
			return QPixmap::fromImage(reader.read());
		}();
		return icon;
	}

	static const QPixmap &tiktokPersonalIcon()
	{
		// TikTok requires prior written permission for logo use. This optional
		// runtime-only file is deliberately absent from public source/packages.
		static const QPixmap icon(streamControlAssetPath("ui/tiktok-personal.png"));
		return icon;
	}

	QString neutralBadgeText() const
	{
		return fallbackText_;
	}

	void drawNeutralTikTokGlyph(QPainter &painter) const
	{
		drawNeutralBadgeFrame(painter);

		QPen notePen(QColor(QStringLiteral("#dce2e8")), 2.4, Qt::SolidLine, Qt::RoundCap,
			     Qt::RoundJoin);
		painter.setPen(notePen);
		painter.setBrush(Qt::NoBrush);
		painter.drawLine(QPointF(17.5, 8), QPointF(17.5, 19));
		painter.drawLine(QPointF(17.5, 8), QPointF(22, 9.5));
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(QStringLiteral("#dce2e8")));
		painter.drawEllipse(QRectF(10, 17, 8, 6));
	}

	void drawNeutralKickGlyph(QPainter &painter) const
	{
		drawNeutralBadgeFrame(painter);
		drawNeutralMonogram(painter, QStringLiteral("K"));
	}

	void drawNeutralBadge(QPainter &painter) const
	{
		drawNeutralBadgeFrame(painter);
		painter.setPen(QColor(QStringLiteral("#dce2e8")));
		QFont font(QStringLiteral("Bahnschrift SemiCondensed"));
		font.setBold(true);
		font.setPixelSize(neutralBadgeText().size() > 2 ? 10 : 15);
		painter.setFont(font);
		painter.drawText(rect(), Qt::AlignCenter, neutralBadgeText());
	}

	QString accessiblePlatformName() const
	{
		if (platformId_ == QStringLiteral("youtube"))
			return QStringLiteral("YouTube - open YouTube Studio");
		if (platformId_ == QStringLiteral("twitch"))
			return QStringLiteral("Twitch - open Creator Dashboard");
		if (platformId_ == QStringLiteral("kick"))
			return QStringLiteral("Kick platform");
		if (platformId_ == QStringLiteral("tiktok"))
			return QStringLiteral("TikTok platform");
		return QStringLiteral("%1 platform").arg(fallbackText_);
	}

	QString platformToolTip() const
	{
		if (platformId_ == QStringLiteral("youtube"))
			return QStringLiteral("Open YouTube Studio");
		if (platformId_ == QStringLiteral("twitch"))
			return QStringLiteral("Open Twitch Creator Dashboard");
		return {};
	}

	void openPlatformDestination() const
	{
		if (platformId_ == QStringLiteral("youtube")) {
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://studio.youtube.com/")));
			return;
		}
		if (platformId_ == QStringLiteral("twitch")) {
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://dashboard.twitch.tv/")));
		}
	}

	QString platformId_;
	QString fallbackText_;
};

class PerforatedRail final : public QWidget {
public:
	explicit PerforatedRail(QWidget *parent = nullptr) : QWidget(parent)
	{
		setFixedWidth(7);
		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
		setAccessibleName(QStringLiteral("Decorative perforated rail"));
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor(QStringLiteral("#090d10")));
		QLinearGradient edge(0, 0, width(), 0);
		edge.setColorAt(0.0, QColor(QStringLiteral("#26333a")));
		edge.setColorAt(0.18, QColor(QStringLiteral("#0b1013")));
		edge.setColorAt(0.82, QColor(QStringLiteral("#080b0d")));
		edge.setColorAt(1.0, QColor(QStringLiteral("#28343b")));
		painter.fillRect(rect(), edge);
		painter.setRenderHint(QPainter::Antialiasing, true);
		for (int y = 7; y < height(); y += 9) {
			for (int x = 5; x < width() - 3; x += 7) {
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(0, 0, 0, 230));
				painter.drawEllipse(QRectF(x, y, 4.5, 4.5));
				painter.setPen(QPen(QColor(70, 86, 94, 135), 0.8));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(QRectF(x + 0.5, y + 0.5, 3.5, 3.5));
			}
		}
	}
};

class StatusLight final : public QWidget {
public:
	explicit StatusLight(QWidget *parent = nullptr) : QWidget(parent)
	{
		setFixedSize(8, 8);
		setAccessibleName(QStringLiteral("Stream status indicator"));
	}

	void setColor(const QColor &color)
	{
		if (color_ == color)
			return;
		color_ = color;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.scale(width() / 26.0, height() / 26.0);
		QRadialGradient glow(QPointF(13, 13), 13);
		QColor edge = color_;
		edge.setAlpha(0);
		glow.setColorAt(0.0, color_.lighter(135));
		glow.setColorAt(0.38, color_);
		glow.setColorAt(1.0, edge);
		painter.setPen(Qt::NoPen);
		painter.setBrush(glow);
		painter.drawEllipse(QRectF(0, 0, 26, 26));
		painter.setBrush(color_.lighter(115));
		painter.drawEllipse(QRectF(7.5, 7.5, 11, 11));
	}

private:
	QColor color_ = QColor(QStringLiteral("#35e6f2"));
};

} // namespace dsk
