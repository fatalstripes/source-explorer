#include "ui/scene-switch.hpp"

#include <obs-frontend-api.h>

#include <QMessageBox>
#include <QString>
#include <QWidget>

namespace se {

bool requestSceneSwitch(QWidget *parent, obs_source_t *scene)
{
	if (!scene)
		return false;

	if (obs_frontend_preview_program_mode_active()) {
		obs_frontend_set_current_preview_scene(scene);
		return true;
	}

	if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
		const auto answer = QMessageBox::question(
			parent, QObject::tr("Switch scene"),
			QObject::tr("Switch the live scene to \"%1\"? This changes what viewers see right now.")
				.arg(QString::fromUtf8(obs_source_get_name(scene))));
		if (answer != QMessageBox::Yes)
			return false;
	}
	obs_frontend_set_current_scene(scene);
	return true;
}

} // namespace se
