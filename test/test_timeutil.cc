#include <gtest/gtest.h>

#include "timeutil.hh"

TEST(TimeUtil, RelativeLabel_Invalid_ReturnsEmpty)
{
  EXPECT_TRUE(timeutil::relative_label(QDateTime{}).isEmpty());
}

TEST(TimeUtil, RelativeLabel_JustNow)
{
  auto when = QDateTime::currentDateTime().addSecs(-5);
  EXPECT_EQ(timeutil::relative_label(when), "just now");
}

TEST(TimeUtil, RelativeLabel_Minutes_Singular)
{
  auto when = QDateTime::currentDateTime().addSecs(-90); // 1.5 min -> 1 minute
  EXPECT_EQ(timeutil::relative_label(when), "1 minute ago");
}

TEST(TimeUtil, RelativeLabel_Minutes_Plural)
{
  auto when = QDateTime::currentDateTime().addSecs(-5 * 60);
  EXPECT_EQ(timeutil::relative_label(when), "5 minutes ago");
}

TEST(TimeUtil, RelativeLabel_Hours)
{
  auto when = QDateTime::currentDateTime().addSecs(-3 * 60 * 60);
  EXPECT_EQ(timeutil::relative_label(when), "3 hours ago");
}

TEST(TimeUtil, RelativeLabel_Days)
{
  auto when = QDateTime::currentDateTime().addDays(-3);
  EXPECT_EQ(timeutil::relative_label(when), "3 days ago");
}

TEST(TimeUtil, RelativeLabel_Months)
{
  auto when = QDateTime::currentDateTime().addDays(-60);
  EXPECT_EQ(timeutil::relative_label(when), "2 months ago");
}

TEST(TimeUtil, RelativeLabel_Years)
{
  auto when = QDateTime::currentDateTime().addDays(-400);
  EXPECT_EQ(timeutil::relative_label(when), "1 year ago");
}

TEST(TimeUtil, RelativeLabel_FutureClampedToJustNow)
{
  auto when = QDateTime::currentDateTime().addSecs(60); // clock skew / future timestamp
  EXPECT_EQ(timeutil::relative_label(when), "just now");
}

TEST(TimeUtil, MsToText_UnderMinute)
{
  EXPECT_EQ(timeutil::ms_to_text(5000), "0:05");
  EXPECT_EQ(timeutil::ms_to_text(45000), "0:45");
}

TEST(TimeUtil, MsToText_MinutesSeconds_NoLeadingMinutePad)
{
  EXPECT_EQ(timeutil::ms_to_text(3 * 60 * 1000 + 45 * 1000), "3:45");
  EXPECT_EQ(timeutil::ms_to_text(12 * 60 * 1000 + 5 * 1000), "12:05");
}

TEST(TimeUtil, MsToText_Hours_PadsMinutes)
{
  // Regression: an hour-plus duration must zero-pad the minutes ("1:07:36"),
  // not collapse to a single digit ("1:7:36").
  EXPECT_EQ(timeutil::ms_to_text(60 * 60 * 1000 + 7 * 60 * 1000 + 36 * 1000), "1:07:36");
  EXPECT_EQ(timeutil::ms_to_text(2 * 60 * 60 * 1000 + 0 * 60 * 1000 + 9 * 1000), "2:00:09");
}
