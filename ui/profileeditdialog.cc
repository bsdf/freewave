#include "profileeditdialog.hh"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

#include "controller/credentialstore.hh"
#include "segmentedcontrol.hh"
#include "theme.hh"

namespace {

void
warn_secret_not_saved(QWidget *parent, const QString &reason)
{
  QMessageBox::warning(parent, "Couldn't Save Credential",
      QStringLiteral("The API key/password couldn't be stored in the OS keyring, "
                     "so the profile was not saved.\n\n%1")
          .arg(reason));
}

}

auto
profileedit::make_profile(const Fields &f) -> BackendProfile
{
  BackendProfile profile{
      .id = f.id,
      .type = f.is_mpd ? "mpd" : "subsonic",
      .name = f.name.trimmed().isEmpty() ? f.fallback_name : f.name.trimmed()};

  if (f.is_mpd)
    {
      QString h = f.host.trimmed();
      profile.params["host"] = h.isEmpty() ? QString("localhost") : h;
      QString ps = f.port.trimmed();
      profile.params["port"] = ps.isEmpty() ? 6600u : ps.toUInt();
    }
  else
    {
      profile.params["url"] = f.url.trimmed();
      profile.params["auth_mode"] = f.userpass_auth ? "userpass" : "apikey";
      if (f.userpass_auth)
        profile.params["username"] = f.username.trimmed();
    }
  return profile;
}

ProfileEditDialog::ProfileEditDialog(Mode mode, const BackendProfile &existing,
    QWidget *parent)
  : QDialog(parent)
{
  setWindowTitle(mode == Mode::Add ? "Add Server" : "Edit Server");
  setAttribute(Qt::WA_DeleteOnClose);

  layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 20, 24, 20);
  layout->setSpacing(12);

  auto *title_lbl = new QLabel(mode == Mode::Add ? "Add Server" : "Edit Server", this);
  auto tf = theme::ui_sans(18);
  tf.setWeight(QFont::DemiBold);
  title_lbl->setFont(tf);
  layout->addWidget(title_lbl);

  if (mode == Mode::Add)
    build_add_ui();
  else
    build_edit_ui(existing);

  setMinimumWidth(380);
}

auto
ProfileEditDialog::build_add_ui() -> void
{
  // Type selector
  auto *type_control = new SegmentedControl(this);
  auto *mpd_btn = type_control->add_segment(0, "MPD");
  auto *sub_btn = type_control->add_segment(1, "Subsonic");

  bool have_mpd = false, have_sub = false;
#ifdef ENABLE_MPD
  have_mpd = true;
#endif
#ifdef ENABLE_SUBSONIC
  have_sub = true;
#endif

  mpd_btn->setVisible(have_mpd);
  sub_btn->setVisible(have_sub);

  auto *type_row = new QWidget(this);
  auto *tr_layout = new QHBoxLayout(type_row);
  tr_layout->setContentsMargins(0, 0, 0, 0);
  auto *type_lbl = new QLabel("Type");
  type_lbl->setFont(theme::ui_sans(13));
  type_lbl->setFixedWidth(80);
  tr_layout->addWidget(type_lbl);
  tr_layout->addWidget(type_control, 1);
  layout->addWidget(type_row);

  // Subsonic auth-mode selector
  auto *auth_control = new SegmentedControl(this);
  auto *apikey_btn = auth_control->add_segment(0, "API Key");
  auth_control->add_segment(1, "Username/Password");
  apikey_btn->setChecked(true);

  auto *auth_row = new QWidget(this);
  auto *ar_layout = new QHBoxLayout(auth_row);
  ar_layout->setContentsMargins(0, 0, 0, 0);
  auto *auth_lbl = new QLabel("Auth");
  auth_lbl->setFont(theme::ui_sans(13));
  auth_lbl->setFixedWidth(80);
  ar_layout->addWidget(auth_lbl);
  ar_layout->addWidget(auth_control, 1);
  layout->addWidget(auth_row);

  // Form fields
  auto *form = new QWidget(this);
  auto *form_layout = new QFormLayout(form);
  form_layout->setContentsMargins(0, 0, 0, 0);
  form_layout->setSpacing(8);
  form_layout->setLabelAlignment(Qt::AlignRight);

  auto make_field = [&](const QString &label) -> QLineEdit * {
    auto *edit = new QLineEdit(form);
    edit->setFont(theme::ui_sans(13));
    auto *lbl = new QLabel(label);
    lbl->setFont(theme::ui_sans(13));
    lbl->setBuddy(edit);
    // Fixed width (matches the Type/Auth row labels above) so the field
    // column doesn't shift when swapping label text between auth modes.
    lbl->setFixedWidth(80);
    form_layout->addRow(lbl, edit);
    return edit;
  };

  auto *name_edit = make_field("Name");
  auto *host_edit = make_field("Host");
  auto *port_edit = make_field("Port");
  port_edit->setPlaceholderText("6600");
  auto *url_edit = make_field("URL");
  auto *key_edit = make_field("API Key");
  key_edit->setEchoMode(QLineEdit::Password);
  auto *username_edit = make_field("Username");
  auto *password_edit = make_field("Password");
  password_edit->setEchoMode(QLineEdit::Password);

  layout->addWidget(form);

  auto *btn_box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  btn_box->setFont(theme::ui_sans(12));
  layout->addWidget(btn_box);

  // Show/hide fields based on type + (for Subsonic) auth mode
  auto update_fields = [=, this] {
    bool mpd = (type_control->current() == 0);
    bool userpass = (auth_control->current() == 1);
    // setRowVisible (not plain setVisible) is required to actually collapse a
    // QFormLayout row's reserved space — hiding just the field/label widgets
    // leaves the row's height reserved.
    form_layout->setRowVisible(host_edit, mpd);
    form_layout->setRowVisible(port_edit, mpd);
    form_layout->setRowVisible(url_edit, !mpd);
    auth_row->setVisible(!mpd);
    form_layout->setRowVisible(key_edit, !mpd && !userpass);
    form_layout->setRowVisible(username_edit, !mpd && userpass);
    form_layout->setRowVisible(password_edit, !mpd && userpass);

    // setRowVisible() alone doesn't synchronously recompute row geometry —
    // form_layout (not the outer vbox `layout`) must be reactivated so its
    // sizeHint() reflects the new visibility before we resize. And
    // QLayout::activate() bakes the layout's minimum size hint into the
    // top-level widget's minimumSize as a floor, so clear that first or a
    // taller field set shown earlier permanently blocks shrinking back down.
    setMinimumHeight(0);
    form_layout->activate();
    layout->activate();
    resize(width(), sizeHint().height());
  };

  connect(type_control, &SegmentedControl::selected, this, [=](int) { update_fields(); });
  connect(auth_control, &SegmentedControl::selected, this, [=](int) { update_fields(); });

  // Select default type
  if (have_mpd)
    mpd_btn->setChecked(true);
  else if (have_sub)
    sub_btn->setChecked(true);
  update_fields();

  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(btn_box, &QDialogButtonBox::accepted, this, [=, this] {
    bool is_mpd = (type_control->current() == 0);
    bool userpass = (auth_control->current() == 1);

    auto profile = profileedit::make_profile({
        .is_mpd = is_mpd,
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
        .name = name_edit->text(),
        .fallback_name = is_mpd ? "MPD" : "Subsonic",
        .host = host_edit->text(),
        .port = port_edit->text(),
        .url = url_edit->text(),
        .userpass_auth = userpass,
        .username = username_edit->text(),
    });

#ifdef ENABLE_KEYCHAIN
    if (!is_mpd)
      {
        CredentialStore cs;
        // Trim: a stray leading/trailing space (paste artifact) changes the
        // salted-token hash and gets silently rejected by the server.
        auto stored = cs.write_blocking(profile.id,
            userpass ? "password" : "api_key",
            userpass ? password_edit->text().trimmed() : key_edit->text().trimmed());
        if (!stored)
          {
            warn_secret_not_saved(this, stored.error());
            return;
          }
      }
#endif

    ProfileStore().add(profile);
    accept();
    emit profiles_changed();
  });
}

auto
ProfileEditDialog::build_edit_ui(const BackendProfile &existing) -> void
{
  auto *form = new QWidget(this);
  auto *form_layout = new QFormLayout(form);
  form_layout->setContentsMargins(0, 0, 0, 0);
  form_layout->setSpacing(8);
  form_layout->setLabelAlignment(Qt::AlignRight);

  auto make_field = [&](const QString &label, const QString &value) -> QLineEdit * {
    auto *edit = new QLineEdit(form);
    edit->setFont(theme::ui_sans(13));
    edit->setText(value);
    auto *lbl = new QLabel(label);
    lbl->setFont(theme::ui_sans(13));
    lbl->setBuddy(edit);
    // Fixed width so the field column doesn't shift when swapping label text
    // between auth modes (matches the Auth row label below).
    lbl->setFixedWidth(80);
    form_layout->addRow(lbl, edit);
    return edit;
  };

  auto *name_edit = make_field("Name", existing.name);
  QLineEdit *host_edit = nullptr, *port_edit = nullptr;
  QLineEdit *url_edit = nullptr, *key_edit = nullptr;
  QLineEdit *username_edit = nullptr, *password_edit = nullptr;
  SegmentedControl *auth_control = nullptr;

  const bool is_mpd = (existing.type == "mpd");
  const bool userpass = (existing.params.value("auth_mode", "apikey").toString() == "userpass");
  if (is_mpd)
    {
      host_edit = make_field("Host", existing.params.value("host").toString());
      port_edit = make_field("Port", QString::number(existing.params.value("port", 6600u).toUInt()));
    }
  else
    {
      url_edit = make_field("URL", existing.params.value("url").toString());

      auth_control = new SegmentedControl(this);
      auto *apikey_btn = auth_control->add_segment(0, "API Key");
      auto *userpass_btn = auth_control->add_segment(1, "Username/Password");
      (userpass ? userpass_btn : apikey_btn)->setChecked(true);
      auto *auth_row = new QWidget(form);
      auto *ar_layout = new QHBoxLayout(auth_row);
      ar_layout->setContentsMargins(0, 0, 0, 0);
      auto *auth_lbl = new QLabel("Auth");
      auth_lbl->setFixedWidth(80);
      form_layout->addRow(auth_lbl, auth_row);
      ar_layout->addWidget(auth_control);

      key_edit = make_field("API Key", "");
      key_edit->setEchoMode(QLineEdit::Password);
      key_edit->setPlaceholderText("Leave blank to keep existing");

      username_edit = make_field("Username", existing.params.value("username").toString());
      password_edit = make_field("Password", "");
      password_edit->setEchoMode(QLineEdit::Password);
      password_edit->setPlaceholderText("Leave blank to keep existing");

      auto update_auth_fields = [=, this] {
        bool up = (auth_control->current() == 1);
        form_layout->setRowVisible(key_edit, !up);
        form_layout->setRowVisible(username_edit, up);
        form_layout->setRowVisible(password_edit, up);

        // See the matching comment in build_add_ui().
        setMinimumHeight(0);
        form_layout->activate();
        layout->activate();
        resize(width(), sizeHint().height());
      };
      connect(auth_control, &SegmentedControl::selected, this, [=](int) { update_auth_fields(); });
      update_auth_fields();
    }

  layout->addWidget(form);

  auto *btn_box = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  btn_box->setFont(theme::ui_sans(12));
  layout->addWidget(btn_box);

  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(btn_box, &QDialogButtonBox::accepted, this, [=, this] {
    // Edit semantics: copy the existing profile (preserving id, type, and any
    // params keys the form doesn't edit) and override only the edited fields.
    BackendProfile updated = existing;
    updated.name = name_edit->text().trimmed().isEmpty()
                       ? existing.name
                       : name_edit->text().trimmed();

    if (is_mpd)
      {
        updated.params["host"] = host_edit->text().trimmed();
        QString ps = port_edit->text().trimmed();
        updated.params["port"] = ps.isEmpty() ? 6600u : ps.toUInt();
      }
    else
      {
        updated.params["url"] = url_edit->text().trimmed();
        bool new_userpass = (auth_control->current() == 1);
        updated.params["auth_mode"] = new_userpass ? "userpass" : "apikey";
        if (new_userpass)
          updated.params["username"] = username_edit->text().trimmed();
        else
          updated.params.remove("username");

        // Trimmed: a whitespace-only entry becomes empty and so is treated as
        // "keep existing", and a real secret can't carry a stray paste space.
        QString new_secret = (new_userpass ? password_edit->text() : key_edit->text()).trimmed();
#ifdef ENABLE_KEYCHAIN
        if (!new_secret.isEmpty())
          {
            CredentialStore cs;
            auto stored = cs.write_blocking(
                existing.id, new_userpass ? "password" : "api_key", new_secret);
            if (!stored)
              {
                warn_secret_not_saved(this, stored.error());
                return;
              }
          }
        // Mode switch: drop whichever secret is no longer relevant so it
        // doesn't linger in the keychain if the user switches back and forth.
        CredentialStore().remove(existing.id, new_userpass ? "api_key" : "password");
#endif
      }

    ProfileStore().update(updated);
    accept();
    emit profiles_changed();
  });
}
