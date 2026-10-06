#include "nowplayingbadge.hh"
#include "theme.hh"
#include "model/song.hh"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

constexpr auto COVER_SIZE = 38;
constexpr auto BADGE_WIDTH = 220;
constexpr auto INFO_SPACING = 10;
// Space the title/artist labels actually get (badge is fixed-width, margins 0).
constexpr auto TEXT_WIDTH = BADGE_WIDTH - COVER_SIZE - INFO_SPACING;

NowPlayingBadge::NowPlayingBadge(QWidget *parent)
  : QWidget{parent}
{
  cover_label = new QLabel(this);
  cover_label->setFixedSize(COVER_SIZE, COVER_SIZE);

  title_label = new QLabel("Not Playing", this);
  title_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

  artist_label = new QLabel(this);
  artist_label->setForegroundRole(QPalette::Dark);
  artist_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

  update_fonts();

  auto *info_layout = new QVBoxLayout;
  info_layout->setContentsMargins(0, 0, 0, 0);
  info_layout->setSpacing(1);
  info_layout->addWidget(title_label);
  info_layout->addWidget(artist_label);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(INFO_SPACING);
  layout->addWidget(cover_label, 0, Qt::AlignVCenter);
  layout->addLayout(info_layout);
  layout->setAlignment(info_layout, Qt::AlignVCenter);

  setFixedWidth(BADGE_WIDTH);
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_Hover, true);
  // A QWidget subclass only paints its stylesheet background when told to; without
  // this the #npBadge:hover fill below silently does nothing.
  setAttribute(Qt::WA_StyledBackground, true);
  setObjectName("npBadge");
  setStyleSheet("#npBadge { border-radius: 5px; } #npBadge:hover { background: palette(link); }");
}

auto
NowPlayingBadge::update_fonts() -> void
{
  auto tf = theme::ui_sans(12);
  tf.setWeight(QFont::Medium);
  title_label->setFont(tf);
  artist_label->setFont(theme::ui_sans(11));
  apply_elision();
}

auto
NowPlayingBadge::apply_elision() -> void
{
  title_label->setText(
      QFontMetrics(title_label->font()).elidedText(full_title, Qt::ElideRight, TEXT_WIDTH));
  artist_label->setText(
      QFontMetrics(artist_label->font()).elidedText(full_artist, Qt::ElideRight, TEXT_WIDTH));
}

auto
NowPlayingBadge::set_album_art(const QPixmap &pixmap) -> void
{
  if (pixmap.isNull())
    {
      cover_label->clear();
      return;
    }

  // Preserve the source devicePixelRatio (text covers are rendered dpr-aware);
  // clip in logical coordinates so the round happens at the right scale.
  QPixmap rounded(pixmap.size());
  rounded.setDevicePixelRatio(pixmap.devicePixelRatio());
  rounded.fill(Qt::transparent);
  QPainter p(&rounded);
  p.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(
      QRectF(QPointF(0, 0), QSizeF(pixmap.size()) / pixmap.devicePixelRatio()),
      theme::tok::rSm, theme::tok::rSm);
  p.setClipPath(path);
  p.drawPixmap(0, 0, pixmap);
  p.end();
  cover_label->setPixmap(rounded);
}

auto
NowPlayingBadge::set_song(const song &song) -> void
{
  full_title = song.title;
  full_artist = song.artist;
  apply_elision();
  setToolTip(song.artist.isEmpty() ? song.title
                                   : song.title + " ～ " + song.artist);
}

auto
NowPlayingBadge::mousePressEvent(QMouseEvent *event) -> void
{
  // Accept the press so the badge becomes the mouse grabber and reliably
  // receives the matching release (children are plain QLabels that ignore it).
  event->accept();
}

auto
NowPlayingBadge::mouseReleaseEvent(QMouseEvent *event) -> void
{
  if (rect().contains(event->pos()))
    emit clicked();
}
