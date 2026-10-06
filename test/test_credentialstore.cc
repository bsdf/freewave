#include "controller/credentialstore.hh"
#include <gtest/gtest.h>
#include <QString>

TEST(CredentialStoreTest, WriteReadRemoveRoundTrip)
{
  CredentialStore cs;
  const QString pid = "test-profile-xyz";
  const QString field = "api_key";
  const QString secret = "s3cr3t-value-123";
  // Clean any leftover from a prior run; absent is the normal case, so a
  // failure here says nothing.
  (void)cs.remove_blocking(pid, field);
  // Skip (not fail) where no Secret Service is reachable — sandboxed/headless/CI
  // runs have no D-Bus keyring socket. This is the legitimate "no keyring" path.
  if (!cs.write_blocking(pid, field, secret))
    GTEST_SKIP() << "No OS keyring/Secret Service reachable; skipping round-trip";
  auto got = cs.read_blocking(pid, field);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got.value().toStdString(), secret.toStdString());
  EXPECT_TRUE(cs.remove_blocking(pid, field).has_value());
  auto after = cs.read_blocking(pid, field);
  ASSERT_FALSE(after.has_value());
  // The failure carries the backend's own words — callers put this in front of
  // a user, so an empty string would be worse than no message at all.
  EXPECT_FALSE(after.error().isEmpty());
}

// Runs everywhere, unlike the round-trip above: reading a key that was never
// written fails whether or not a keyring exists, and both dialogs paste this
// text straight into the "Couldn't Save Credential" box.
TEST(CredentialStoreTest, FailureCarriesAMessage)
{
  auto got = CredentialStore().read_blocking(
      "profile-that-was-never-written", "api_key");
  ASSERT_FALSE(got.has_value());
  EXPECT_FALSE(got.error().isEmpty());
}
