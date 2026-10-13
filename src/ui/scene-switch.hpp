#pragma once
#include <obs.h>

class QWidget;

namespace se {

// Make `scene` the scene being edited. In Studio Mode that is the preview scene, which is safe.
// Otherwise it changes what viewers see, so while streaming or recording the user is asked first.
// Returns true if the switch happened.
bool requestSceneSwitch(QWidget *parent, obs_source_t *scene);

} // namespace se
