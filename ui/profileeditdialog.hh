#ifndef PROFILEEDITDIALOG_HH
#define PROFILEEDITDIALOG_HH

#include <QDialog>

#include "controller/profilestore.hh" // BackendProfile

class QLineEdit;
class QVBoxLayout;

// Modal editor for a server connection profile, extracted from MainWindow.
// Add mode shows an MPD/Subsonic type selector + the matching fields; Edit mode
// edits an existing profile's fields for its fixed type. On accept it persists via
// ProfileStore (+ CredentialStore for the Subsonic api_key under ENABLE_KEYCHAIN)
// and emits profiles_changed(). Usage: construct, connect profiles_changed(), then
// call open() — the dialog deletes itself on close (WA_DeleteOnClose).
class ProfileEditDialog : public QDialog {
  Q_OBJECT

public:
  enum class Mode { Add,
    Edit };

  // For Add, `existing` is ignored. For Edit, pass the profile to edit.
  ProfileEditDialog(Mode mode, const BackendProfile &existing,
      QWidget *parent = nullptr);

signals:
  void profiles_changed();

private:
  void build_add_ui();
  void build_edit_ui(const BackendProfile &existing);

  QVBoxLayout *layout = nullptr;
};

// Pure mapping from the Add form's raw field strings to a new BackendProfile (no
// UUID generation, no secret-store side effects), factored out of the dialog so the
// MPD/Subsonic + name/port/host fallback logic is unit-testable. `id` is supplied by
// the caller (a fresh UUID). Empty MPD host -> "localhost", empty port -> 6600, empty
// name -> `fallback_name`. The Edit path is "copy existing + override" (it preserves
// the profile's other fields/params) and stays inline in the dialog.
namespace profileedit {
struct Fields {
  bool is_mpd = true;
  QString id;
  QString name;               // raw (untrimmed) name field
  QString fallback_name;      // used when the trimmed name is empty
  QString host;               // raw
  QString port;               // raw
  QString url;                // raw
  bool userpass_auth = false; // subsonic only: username/password vs api key
  QString username;           // raw, subsonic + userpass_auth only
};

auto make_profile(const Fields &f) -> BackendProfile;
} // namespace profileedit

#endif // PROFILEEDITDIALOG_HH
