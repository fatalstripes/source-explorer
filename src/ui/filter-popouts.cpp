// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/filter-popouts.hpp"

#include "core/obs-event-bridge.hpp"
#include "core/obs-raii.hpp"
#include "ui/panels/properties-panel.hpp"

#include <obs-frontend-api.h>

#include <QApplication>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <vector>

namespace se {

namespace {

std::string str(const char *s)
{
	return s ? s : "";
}

// One floating editor. Keeps its source alive while open; reports when the user closes it.
class PopoutWindow final : public QWidget {
public:
	PopoutWindow(obs::SourceRef source, std::string filter, QWidget *parent)
		: QWidget(parent, Qt::Window | Qt::Tool),
		  source_(std::move(source)),
		  filter_(std::move(filter))
	{
		setAttribute(Qt::WA_DeleteOnClose);
		setWindowTitle(QString::fromStdString(filter_ + " - " + str(obs_source_get_name(source_.get()))));
		auto *layout = new QVBoxLayout(this);
		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		auto *panel = new PropertiesPanel(PropertiesPanel::Mode::Embedded, scroll);
		panel->setFilter(source_.get(), filter_);
		scroll->setWidget(panel);
		layout->addWidget(scroll);
		resize(380, 460);
	}

	obs_source_t *source() const { return source_.get(); }
	const std::string &filter() const { return filter_; }

private:
	obs::SourceRef source_;
	std::string filter_;
};

} // namespace

struct FilterPopouts::Impl {
	std::vector<QPointer<QWidget>> windows;
	QTimer timer;
	ObsEventBridge *bridge = nullptr;
};

FilterPopouts &FilterPopouts::instance()
{
	// Owned by OBS's main window, which is destroyed before OBS unloads plugin DLLs; parenting to
	// qApp instead would run this class's destructor after the DLL is gone.
	static QPointer<FilterPopouts> self;
	if (!self)
		self = new FilterPopouts(static_cast<QObject *>(obs_frontend_get_main_window()));
	return *self;
}

FilterPopouts::~FilterPopouts()
{
	closeAll();
	delete impl_;
}

FilterPopouts::FilterPopouts(QObject *parent) : QObject(parent), impl_(new Impl)
{
	impl_->timer.setInterval(1000);
	connect(&impl_->timer, &QTimer::timeout, this, [this] { check(); });
	impl_->bridge = new ObsEventBridge(this);
	connect(impl_->bridge, &ObsEventBridge::frontendEvent, this, [this](int e) {
		// The loaded collection's sources are about to go away.
		if (e == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING || e == OBS_FRONTEND_EVENT_EXIT)
			closeAll();
	});
}

int FilterPopouts::count() const
{
	int n = 0;
	for (const auto &w : impl_->windows)
		if (w)
			++n;
	return n;
}

bool FilterPopouts::isOpen(obs_source_t *source, const std::string &filterName) const
{
	for (const auto &w : impl_->windows) {
		auto *p = static_cast<PopoutWindow *>(w.data());
		if (p && p->source() == source && p->filter() == filterName)
			return true;
	}
	return false;
}

void FilterPopouts::open(obs_source_t *source, const std::string &filterName)
{
	if (!source)
		return;
	for (const auto &w : impl_->windows) {
		auto *p = static_cast<PopoutWindow *>(w.data());
		if (p && p->source() == source && p->filter() == filterName) {
			p->show();
			p->raise();
			p->activateWindow();
			return;
		}
	}
	QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *win = new PopoutWindow(obs::retain(source), filterName, mainWindow);
	connect(win, &QObject::destroyed, this, [this](QObject *o) { onWindowClosed(o); });
	impl_->windows.emplace_back(win);
	impl_->timer.start();
	win->show();
	emit countChanged(count());
}

void FilterPopouts::onWindowClosed(QObject *)
{
	auto &v = impl_->windows;
	v.erase(std::remove_if(v.begin(), v.end(), [](const QPointer<QWidget> &w) { return w.isNull(); }), v.end());
	if (v.empty())
		impl_->timer.stop();
	emit countChanged(count());
}

void FilterPopouts::closeAll()
{
	const auto copy = impl_->windows; // closing mutates the list
	for (const auto &w : copy)
		if (w)
			w->close();
}

void FilterPopouts::closeFor(obs_source_t *source)
{
	const auto copy = impl_->windows;
	for (const auto &w : copy) {
		auto *p = static_cast<PopoutWindow *>(w.data());
		if (p && p->source() == source)
			p->close();
	}
}

void FilterPopouts::check()
{
	const auto copy = impl_->windows;
	for (const auto &w : copy) {
		auto *p = static_cast<PopoutWindow *>(w.data());
		if (!p)
			continue;
		obs::SourceRef f = obs::adopt(obs_source_get_filter_by_name(p->source(), p->filter().c_str()));
		if (!f || obs_source_removed(p->source()))
			p->close();
	}
}

} // namespace se
