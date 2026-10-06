#include "credentialstore.hh"
#include <qt6keychain/keychain.h>
#include <QEventLoop>
#include <spdlog/spdlog.h>

namespace {

auto
entry_key(const QString &profile_id, const QString &field) -> QString
{
  return profile_id + "/" + field;
}

}

namespace {
static const QString SERVICE = QStringLiteral("freewave");
}

void
CredentialStore::remove(const QString &profile_id, const QString &field)
{
  auto *job = new QKeychain::DeletePasswordJob(SERVICE);
  auto key = entry_key(profile_id, field);
  job->setKey(key);
  job->setAutoDelete(false);

  QObject::connect(job, &QKeychain::Job::finished, [job, key](QKeychain::Job * /*unused*/) {
    if (job->error() != QKeychain::NoError)
      spdlog::warn("CredentialStore remove failed for {}: {}", key, job->errorString());
    job->deleteLater();
  });

  job->start();
}

auto
CredentialStore::write_blocking(const QString &profile_id, const QString &field,
    const QString &secret) -> Result
{
  QKeychain::WritePasswordJob job(SERVICE);
  job.setAutoDelete(false);
  job.setKey(entry_key(profile_id, field));
  job.setTextData(secret);

  QEventLoop loop;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() != QKeychain::NoError)
    {
      spdlog::warn("CredentialStore write_blocking failed for {}: {}", entry_key(profile_id, field),
          job.errorString());
      return std::unexpected(job.errorString());
    }
  return {};
}

auto
CredentialStore::read_blocking(const QString &profile_id, const QString &field) -> SecretResult
{
  QKeychain::ReadPasswordJob job(SERVICE);
  job.setAutoDelete(false);
  job.setKey(entry_key(profile_id, field));

  QEventLoop loop;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() != QKeychain::NoError)
    {
      spdlog::warn("CredentialStore read_blocking failed for {}: {}", entry_key(profile_id, field),
          job.errorString());
      return std::unexpected(job.errorString());
    }
  return job.textData();
}

auto
CredentialStore::remove_blocking(const QString &profile_id, const QString &field) -> Result
{
  QKeychain::DeletePasswordJob job(SERVICE);
  job.setAutoDelete(false);
  job.setKey(entry_key(profile_id, field));

  QEventLoop loop;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() != QKeychain::NoError)
    {
      spdlog::warn("CredentialStore remove_blocking failed for {}: {}", entry_key(profile_id, field),
          job.errorString());
      return std::unexpected(job.errorString());
    }
  return {};
}
