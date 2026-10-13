// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Floating windows that each hold one filter's property editor, so a filter can be tweaked while the
// dock is busy showing something else. GUI thread only.
#include <obs.h>

#include <QObject>

#include <string>

namespace se {

class FilterPopouts final : public QObject {
	Q_OBJECT
public:
	static FilterPopouts &instance();

	// Opens (or raises) the window for `filterName` on `source`.
	void open(obs_source_t *source, const std::string &filterName);
	int count() const;
	bool isOpen(obs_source_t *source, const std::string &filterName) const;
	void closeAll();
	// Closes the windows of one source (e.g. when it is removed).
	void closeFor(obs_source_t *source);

signals:
	void countChanged(int count);

private:
	explicit FilterPopouts(QObject *parent);
	~FilterPopouts() override;
	void check(); // closes windows whose filter or source has gone
	void onWindowClosed(QObject *window);

	struct Impl;
	Impl *impl_;
};

} // namespace se
