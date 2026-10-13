// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/panels/properties-panel.hpp"

#include "core/color-util.hpp"
#include "ui/widgets/command-menu.hpp"

#include <obs.h>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <memory>

namespace se {

namespace {

constexpr int kDebounceMs = 400; // text edits are applied after the user pauses

std::string str(const char *s)
{
	return s ? s : "";
}

// Restartable single-shot timer owned by `owner`; call ->start() on each edit.
QTimer *makeDebounce(QObject *owner, std::function<void()> fn)
{
	auto *t = new QTimer(owner);
	t->setSingleShot(true);
	t->setInterval(kDebounceMs);
	QObject::connect(t, &QTimer::timeout, owner, std::move(fn));
	return t;
}

int decimalsForStep(double step)
{
	return step < 0.001 ? 5 : (step < 0.01 ? 4 : (step < 0.1 ? 3 : 2));
}

QString colorCss(Rgba c)
{
	return QString("background-color: rgba(%1,%2,%3,%4);").arg(c.r).arg(c.g).arg(c.b).arg(c.a / 255.0, 0, 'f', 3);
}

} // namespace

// ---------------------------------------------------------------------------
PropertiesPanel::PropertiesPanel(Mode mode, QWidget *parent) : QWidget(parent), mode_(mode)
{
	auto *root = new QVBoxLayout(this);
	status_ = new QLabel(this);
	status_->setWordWrap(true);
	if (mode_ == Mode::Full) {
		targets_ = new QComboBox(this);
		scroll_ = new QScrollArea(this);
		scroll_->setWidgetResizable(true);
		scroll_->setFrameShape(QFrame::NoFrame);
		root->addWidget(targets_);
		root->addWidget(status_);
		root->addWidget(scroll_, 1);
		connect(targets_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::onTargetChanged);
	} else {
		root->setContentsMargins(0, 0, 0, 0);
		embeddedLayout_ = root;
		root->addWidget(status_);
	}

	poll_ = new QTimer(this);
	poll_->setInterval(500);
	connect(poll_, &QTimer::timeout, this, &PropertiesPanel::poll);
	poll_->start();

	setSource(nullptr);
}

PropertiesPanel::~PropertiesPanel() = default;

// ---------------------------------------------------------------------------
// Targets: the source itself plus each of its filters
// ---------------------------------------------------------------------------
std::vector<std::string> PropertiesPanel::filterNames() const
{
	std::vector<std::string> out;
	if (source_) {
		obs_source_enum_filters(
			source_.get(),
			[](obs_source_t *, obs_source_t *filter, void *p) {
				static_cast<std::vector<std::string> *>(p)->push_back(str(obs_source_get_name(filter)));
			},
			&out);
	}
	return out;
}

bool PropertiesPanel::rebuildTargets(bool keepSelection)
{
	if (mode_ == Mode::Embedded)
		return false;
	const auto names = filterNames();
	std::string sig = source_ ? str(obs_source_get_name(source_.get())) : "";
	for (const auto &n : names)
		sig += '\n' + n;
	if (keepSelection && sig == targetsSignature_)
		return false;
	targetsSignature_ = std::move(sig);

	const QString keep = keepSelection ? targets_->currentData().toString() : QString();
	QSignalBlocker block(targets_);
	targets_->clear();
	if (!source_)
		return true;
	targets_->addItem(tr("Source: %1").arg(QString::fromStdString(str(obs_source_get_name(source_.get())))),
			  QString());
	for (const auto &n : names)
		targets_->addItem(tr("Filter: %1").arg(QString::fromStdString(n)), QString::fromStdString(n));
	if (!keep.isEmpty()) {
		const int i = targets_->findData(keep);
		targets_->setCurrentIndex(i >= 0 ? i : 0);
	}
	return true;
}

void PropertiesPanel::setSource(obs_source_t *source)
{
	if (source && source == source_.get())
		return;
	source_ = obs::retain(source);
	rebuildTargets(false);
	rebuildForm();
}

void PropertiesPanel::setFilter(obs_source_t *source, const std::string &filterName)
{
	source_ = obs::retain(source);
	embeddedFilter_ = filterName;
	rebuildForm();
}

void PropertiesPanel::onTargetChanged(int)
{
	rebuildForm();
}

// ---------------------------------------------------------------------------
// Form lifecycle
// ---------------------------------------------------------------------------
std::string PropertiesPanel::settingsJson() const
{
	const char *js = settings_ ? obs_data_get_json(settings_.get()) : nullptr;
	return str(js);
}

void PropertiesPanel::scheduleRebuild()
{
	QMetaObject::invokeMethod(this, "rebuildForm", Qt::QueuedConnection);
}

bool PropertiesPanel::userIsEditing() const
{
	QWidget *fw = QApplication::focusWidget();
	return fw && content_ && content_->isAncestorOf(fw);
}

void PropertiesPanel::poll()
{
	if (!isVisible() || !source_)
		return;
	if (rebuildTargets(true)) {
		rebuildForm();
		return;
	}
	if (!settings_ || userIsEditing())
		return;
	if (settingsJson() != lastJson_)
		rebuildForm(); // changed elsewhere (OBS's own dialog, a script, websocket)
}

void PropertiesPanel::rebuildForm()
{
	++gen_; // invalidates callbacks of every widget built before this point
	building_ = true;

	const int scrollPos = scroll_ ? scroll_->verticalScrollBar()->value() : 0;
	props_.reset();
	settings_.reset();
	target_.reset();

	// Detach (and thereby delete) the old form before building the new one.
	if (scroll_) {
		content_ = new QWidget;
		scroll_->setWidget(content_);
	} else {
		if (content_) {
			content_->hide();
			content_->deleteLater();
		}
		content_ = new QWidget(this);
		embeddedLayout_->addWidget(content_);
	}

	auto finish = [this](const QString &msg) {
		status_->setText(msg);
		status_->setVisible(!msg.isEmpty());
		building_ = false;
	};

	if (!source_)
		return finish(tr("Select a source."));

	QString filterName;
	if (mode_ == Mode::Embedded) {
		filterName = QString::fromStdString(embeddedFilter_);
	} else {
		const int idx = targets_->currentIndex();
		filterName = idx > 0 ? targets_->currentData().toString() : QString();
	}
	if (filterName.isEmpty() && mode_ == Mode::Embedded)
		return finish(tr("That filter no longer exists."));
	if (filterName.isEmpty())
		target_ = obs::retain(source_.get());
	else
		target_ = obs::adopt(obs_source_get_filter_by_name(source_.get(), filterName.toUtf8().constData()));
	if (!target_)
		return finish(tr("That filter no longer exists."));

	props_ = obs::adopt(obs_source_properties(target_.get()));
	settings_ = obs::settingsOf(target_.get());
	if (!props_ || !settings_)
		return finish(tr("This source has no editable properties."));

	auto *form = new QFormLayout(content_);
	buildProperties(props_.get(), content_, form);
	lastJson_ = settingsJson();
	finish(QString());

	if (scroll_)
		QTimer::singleShot(0, this, [this, scrollPos] { scroll_->verticalScrollBar()->setValue(scrollPos); });
}

// ---------------------------------------------------------------------------
// Property -> widget
// ---------------------------------------------------------------------------
void PropertiesPanel::buildProperties(obs_properties_t *props, QWidget *parent, QFormLayout *form)
{
	for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p))
		addRow(p, parent, form);
}

void PropertiesPanel::addRow(obs_property_t *prop, QWidget *parent, QFormLayout *form)
{
	if (!obs_property_visible(prop))
		return;

	const std::string name = str(obs_property_name(prop));
	const QString desc = QString::fromUtf8(obs_property_description(prop));
	const QString tip = QString::fromUtf8(str(obs_property_long_description(prop)).c_str());
	const unsigned gen = gen_;
	QPointer<PropertiesPanel> self(this);
	auto live = [self, gen] { return self && gen == self->gen_ && !self->building_; };

	const obs_property_type type = obs_property_get_type(prop);

	// Nested group: recurse; checkable groups are a bool in the settings.
	if (type == OBS_PROPERTY_GROUP) {
		auto *box = new QGroupBox(desc, parent);
		auto *inner = new QFormLayout(box);
		if (obs_properties_t *content = obs_property_group_content(prop))
			buildProperties(content, box, inner);
		if (obs_property_group_type(prop) == OBS_GROUP_CHECKABLE) {
			box->setCheckable(true);
			box->setChecked(obs_data_get_bool(settings_.get(), name.c_str()));
			connect(box, &QGroupBox::toggled, this, [self, live, name, prop](bool on) {
				if (!live())
					return;
				obs_data_set_bool(self->settings_.get(), name.c_str(), on);
				self->commit(prop);
			});
			attachCommandMenu(box, [self, live, name, box]() -> std::optional<CommandSet> {
				return live() ? self->commandsFor(name, Json(box->isChecked())) : std::nullopt;
			});
		}
		box->setEnabled(obs_property_enabled(prop));
		form->addRow(box);
		return;
	}

	if (type == OBS_PROPERTY_BUTTON) {
		auto *b = new QPushButton(desc, parent);
		b->setEnabled(obs_property_enabled(prop));
		b->setToolTip(tip);
		connect(b, &QPushButton::clicked, this, [self, live, prop] {
			if (!live())
				return;
			if (obs_property_button_type(prop) == OBS_BUTTON_URL) {
				const QUrl url(QString::fromUtf8(str(obs_property_button_url(prop)).c_str()));
				if (url.scheme() == "http" || url.scheme() == "https")
					QDesktopServices::openUrl(url);
				return;
			}
			if (obs_property_button_clicked(prop, self->target_.get()))
				self->scheduleRebuild();
		});
		form->addRow(b);
		return;
	}

	if (type == OBS_PROPERTY_TEXT && obs_property_text_type(prop) == OBS_TEXT_INFO) {
		auto *l = new QLabel(desc, parent);
		l->setWordWrap(true);
		l->setToolTip(tip);
		form->addRow(l);
		return;
	}

	ValueFn valueFn;
	QWidget *editor = makeEditor(prop, parent, valueFn);
	if (!editor) {
		auto *l = new QLabel(tr("%1: unsupported property type (edit it in OBS's own Properties window)").arg(desc),
				     parent);
		l->setWordWrap(true);
		form->addRow(l);
		return;
	}
	editor->setEnabled(obs_property_enabled(prop));
	editor->setToolTip(tip);
	form->addRow(desc, editor);

	// Command copier on the editor and on any free-standing line edit inside it.
	auto provider = [self, live, name, valueFn]() -> std::optional<CommandSet> {
		return live() ? self->commandsFor(name, valueFn()) : std::nullopt;
	};
	attachCommandMenu(editor, provider);

	// Right-click the LABEL: this property, or every property as one full payload.
	if (QWidget *labelW = form->labelForField(editor)) {
		attachCommandMenu(labelW, [self, live, name, title = desc.toStdString(), valueFn]() -> std::optional<CommandSet> {
			return live() ? self->labelCommandsFor(name, title, valueFn()) : std::nullopt;
		});
	}
	// Composite editors (path, font): hook the inner controls too. Spin boxes hook
	// their own line edit inside attachCommandMenu, so skip those line edits here.
	for (auto *w : editor->findChildren<QAbstractSpinBox *>())
		attachCommandMenu(w, provider);
	for (auto *w : editor->findChildren<QComboBox *>())
		attachCommandMenu(w, provider);
	for (auto *le : editor->findChildren<QLineEdit *>()) {
		if (!qobject_cast<QAbstractSpinBox *>(le->parentWidget()))
			attachCommandMenu(le, provider);
	}
}

QWidget *PropertiesPanel::makeEditor(obs_property_t *prop, QWidget *parent, ValueFn &valueFn)
{
	const std::string name = str(obs_property_name(prop));
	const unsigned gen = gen_;
	QPointer<PropertiesPanel> self(this);
	auto live = [self, gen] { return self && gen == self->gen_ && !self->building_; };
	obs_data_t *settings = settings_.get();
	const char *key = name.c_str(); // only used synchronously below; lambdas copy `name`

	switch (obs_property_get_type(prop)) {
	case OBS_PROPERTY_BOOL: {
		auto *w = new QCheckBox(parent);
		w->setChecked(obs_data_get_bool(settings, key));
		connect(w, &QCheckBox::toggled, this, [self, live, name, prop](bool on) {
			if (!live())
				return;
			obs_data_set_bool(self->settings_.get(), name.c_str(), on);
			self->commit(prop);
		});
		valueFn = [w] { return Json(w->isChecked()); };
		return w;
	}

	case OBS_PROPERTY_INT: {
		auto *w = new QSpinBox(parent);
		const int lo = int(std::max<long long>(obs_property_int_min(prop), INT_MIN));
		const int hi = int(std::min<long long>(obs_property_int_max(prop), INT_MAX));
		w->setRange(lo, hi);
		w->setSingleStep(std::max(1, int(obs_property_int_step(prop))));
		w->setSuffix(QString::fromUtf8(str(obs_property_int_suffix(prop)).c_str()));
		w->setValue(int(obs_data_get_int(settings, key)));
		connect(w, &QSpinBox::valueChanged, this, [self, live, name, prop](int v) {
			if (!live())
				return;
			obs_data_set_int(self->settings_.get(), name.c_str(), v);
			self->commit(prop);
		});
		valueFn = [w] { return Json(static_cast<std::int64_t>(w->value())); };
		return w;
	}

	case OBS_PROPERTY_FLOAT: {
		auto *w = new QDoubleSpinBox(parent);
		const double step = obs_property_float_step(prop);
		w->setDecimals(decimalsForStep(step));
		w->setRange(obs_property_float_min(prop), obs_property_float_max(prop));
		w->setSingleStep(step > 0 ? step : 0.1);
		w->setSuffix(QString::fromUtf8(str(obs_property_float_suffix(prop)).c_str()));
		w->setValue(obs_data_get_double(settings, key));
		connect(w, &QDoubleSpinBox::valueChanged, this, [self, live, name, prop](double v) {
			if (!live())
				return;
			obs_data_set_double(self->settings_.get(), name.c_str(), v);
			self->commit(prop);
		});
		valueFn = [w] { return Json(w->value()); };
		return w;
	}

	case OBS_PROPERTY_TEXT: {
		const QString current = QString::fromUtf8(obs_data_get_string(settings, key));
		auto apply = [self, live, name, prop](const QString &text) {
			if (!live())
				return;
			obs_data_set_string(self->settings_.get(), name.c_str(), text.toUtf8().constData());
			self->commit(prop);
		};
		if (obs_property_text_type(prop) == OBS_TEXT_MULTILINE) {
			auto *w = new QPlainTextEdit(parent);
			w->setPlainText(current);
			w->setMinimumHeight(80);
			auto *t = makeDebounce(w, [w, apply] { apply(w->toPlainText()); });
			connect(w, &QPlainTextEdit::textChanged, t, qOverload<>(&QTimer::start));
			valueFn = [w] { return Json(w->toPlainText().toStdString()); };
			return w;
		}
		auto *w = new QLineEdit(current, parent);
		if (obs_property_text_type(prop) == OBS_TEXT_PASSWORD)
			w->setEchoMode(QLineEdit::Password);
		auto *t = makeDebounce(w, [w, apply] { apply(w->text()); });
		connect(w, &QLineEdit::textEdited, t, qOverload<>(&QTimer::start));
		connect(w, &QLineEdit::editingFinished, this, [t, w, apply] {
			if (t->isActive()) { // commit immediately on Enter / focus-out
				t->stop();
				apply(w->text());
			}
		});
		valueFn = [w] { return Json(w->text().toStdString()); };
		return w;
	}

	case OBS_PROPERTY_PATH: {
		auto *box = new QWidget(parent);
		auto *h = new QHBoxLayout(box);
		h->setContentsMargins(0, 0, 0, 0);
		auto *edit = new QLineEdit(QString::fromUtf8(obs_data_get_string(settings, key)), box);
		auto *browse = new QToolButton(box);
		browse->setText("...");
		h->addWidget(edit, 1);
		h->addWidget(browse);

		auto apply = [self, live, name, prop](const QString &text) {
			if (!live())
				return;
			obs_data_set_string(self->settings_.get(), name.c_str(), text.toUtf8().constData());
			self->commit(prop);
		};
		auto *t = makeDebounce(edit, [edit, apply] { apply(edit->text()); });
		connect(edit, &QLineEdit::textEdited, t, qOverload<>(&QTimer::start));

		const obs_path_type ptype = obs_property_path_type(prop);
		const QString filter = QString::fromUtf8(str(obs_property_path_filter(prop)).c_str());
		const QString defPath = QString::fromUtf8(str(obs_property_path_default_path(prop)).c_str());
		const QString title = QString::fromUtf8(obs_property_description(prop));
		connect(browse, &QToolButton::clicked, box, [=] {
			const QString start = edit->text().isEmpty() ? defPath : edit->text();
			QString chosen;
			if (ptype == OBS_PATH_DIRECTORY)
				chosen = QFileDialog::getExistingDirectory(box, title, start);
			else if (ptype == OBS_PATH_FILE_SAVE)
				chosen = QFileDialog::getSaveFileName(box, title, start, filter);
			else
				chosen = QFileDialog::getOpenFileName(box, title, start, filter);
			if (!chosen.isEmpty()) {
				edit->setText(chosen);
				apply(chosen);
			}
		});
		valueFn = [edit] { return Json(edit->text().toStdString()); };
		return box;
	}

	case OBS_PROPERTY_LIST: {
		auto *w = new QComboBox(parent);
		const obs_combo_format fmt = obs_property_list_format(prop);
		const bool editable = obs_property_list_type(prop) == OBS_COMBO_TYPE_EDITABLE && fmt == OBS_COMBO_FORMAT_STRING;
		w->setEditable(editable);

		auto itemValue = [prop, fmt](size_t i) -> QVariant {
			switch (fmt) {
			case OBS_COMBO_FORMAT_INT: return QVariant::fromValue<qlonglong>(obs_property_list_item_int(prop, i));
			case OBS_COMBO_FORMAT_FLOAT: return obs_property_list_item_float(prop, i);
			case OBS_COMBO_FORMAT_BOOL: return obs_property_list_item_bool(prop, i);
			default: return QString::fromUtf8(str(obs_property_list_item_string(prop, i)).c_str());
			}
		};
		const size_t count = obs_property_list_item_count(prop);
		for (size_t i = 0; i < count; ++i)
			w->addItem(QString::fromUtf8(obs_property_list_item_name(prop, i)), itemValue(i));
		if (auto *model = qobject_cast<QStandardItemModel *>(w->model())) {
			for (size_t i = 0; i < count; ++i)
				if (obs_property_list_item_disabled(prop, i))
					model->item(int(i))->setEnabled(false);
		}

		QVariant current;
		switch (fmt) {
		case OBS_COMBO_FORMAT_INT: current = QVariant::fromValue<qlonglong>(obs_data_get_int(settings, key)); break;
		case OBS_COMBO_FORMAT_FLOAT: current = obs_data_get_double(settings, key); break;
		case OBS_COMBO_FORMAT_BOOL: current = obs_data_get_bool(settings, key); break;
		default: current = QString::fromUtf8(obs_data_get_string(settings, key)); break;
		}
		int at = w->findData(current);
		if (at < 0 && fmt == OBS_COMBO_FORMAT_STRING && !current.toString().isEmpty()) {
			// Value not offered (e.g. a device that is unplugged): show it rather than hide it.
			w->addItem(current.toString(), current);
			at = w->count() - 1;
		}
		w->setCurrentIndex(at);
		if (editable)
			w->setEditText(current.toString());

		auto writeValue = [self, live, name, prop, fmt](const QVariant &v) {
			if (!live())
				return;
			obs_data_t *d = self->settings_.get();
			switch (fmt) {
			case OBS_COMBO_FORMAT_INT: obs_data_set_int(d, name.c_str(), v.toLongLong()); break;
			case OBS_COMBO_FORMAT_FLOAT: obs_data_set_double(d, name.c_str(), v.toDouble()); break;
			case OBS_COMBO_FORMAT_BOOL: obs_data_set_bool(d, name.c_str(), v.toBool()); break;
			default: obs_data_set_string(d, name.c_str(), v.toString().toUtf8().constData()); break;
			}
			self->commit(prop);
		};
		connect(w, &QComboBox::activated, this, [w, writeValue](int i) {
			if (i >= 0)
				writeValue(w->itemData(i));
		});
		if (editable)
			connect(w->lineEdit(), &QLineEdit::editingFinished, this, [w, writeValue] { writeValue(w->currentText()); });

		valueFn = [w, fmt, editable] {
			const QVariant v = (editable || w->currentIndex() < 0) ? QVariant(w->currentText())
									       : w->itemData(w->currentIndex());
			switch (fmt) {
			case OBS_COMBO_FORMAT_INT: return Json(static_cast<std::int64_t>(v.toLongLong()));
			case OBS_COMBO_FORMAT_FLOAT: return Json(v.toDouble());
			case OBS_COMBO_FORMAT_BOOL: return Json(v.toBool());
			default: return Json(v.toString().toStdString());
			}
		};
		return w;
	}

	case OBS_PROPERTY_COLOR:
	case OBS_PROPERTY_COLOR_ALPHA: {
		const bool alpha = obs_property_get_type(prop) == OBS_PROPERTY_COLOR_ALPHA;
		auto *w = new QPushButton(parent);
		auto color = std::make_shared<Rgba>(fromObsColor(obs_data_get_int(settings, key)));
		if (!alpha)
			color->a = 255;
		auto paint = [w, color] {
			w->setStyleSheet(colorCss(*color));
			w->setText(QString("#%1%2%3")
					   .arg(color->r, 2, 16, QChar('0'))
					   .arg(color->g, 2, 16, QChar('0'))
					   .arg(color->b, 2, 16, QChar('0'))
					   .toUpper());
		};
		paint();
		connect(w, &QPushButton::clicked, this, [=] {
			const QColor picked = QColorDialog::getColor(
				QColor(color->r, color->g, color->b, color->a), w, QString::fromUtf8(obs_property_description(prop)),
				alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
			if (!picked.isValid() || !live())
				return;
			*color = {picked.red(), picked.green(), picked.blue(), alpha ? picked.alpha() : 255};
			paint();
			obs_data_set_int(self->settings_.get(), name.c_str(), toObsColor(*color));
			self->commit(prop);
		});
		valueFn = [color] { return Json(static_cast<std::int64_t>(toObsColor(*color))); };
		return w;
	}

	case OBS_PROPERTY_FONT: {
		auto *box = new QWidget(parent);
		auto *h = new QHBoxLayout(box);
		h->setContentsMargins(0, 0, 0, 0);
		auto *face = new QFontComboBox(box);
		auto *size = new QSpinBox(box);
		size->setRange(1, 999);
		h->addWidget(face, 1);
		h->addWidget(size);

		obs::DataRef font = obs::adopt(obs_data_get_obj(settings, key)); // null if never set
		if (font) {
			face->setCurrentFont(QFont(QString::fromUtf8(obs_data_get_string(font.get(), "face"))));
			size->setValue(int(obs_data_get_int(font.get(), "size")));
		}

		// Only face and size are edited; style/flags stay as OBS had them.
		auto write = [self, live, name, prop, face, size] {
			if (!live())
				return;
			obs::DataRef obj = obs::adopt(obs_data_get_obj(self->settings_.get(), name.c_str()));
			if (!obj) {
				obj = obs::adopt(obs_data_create());
				obs_data_set_obj(self->settings_.get(), name.c_str(), obj.get());
			}
			obs_data_set_string(obj.get(), "face", face->currentFont().family().toUtf8().constData());
			obs_data_set_int(obj.get(), "size", size->value());
			self->commit(prop);
		};
		connect(face, &QFontComboBox::activated, this, [write](int) { write(); });
		connect(size, &QSpinBox::valueChanged, this, [write](int) { write(); });

		valueFn = [self, name, face, size] {
			obs::DataRef obj = self ? obs::adopt(obs_data_get_obj(self->settings_.get(), name.c_str())) : obs::DataRef{};
			if (obj)
				return Json::raw(str(obs_data_get_json(obj.get())));
			Json o = Json::object();
			o.set("face", face->currentFont().family().toStdString());
			o.set("size", size->value());
			return o;
		};
		return box;
	}

	default: // EDITABLE_LIST, FRAME_RATE, ...: not rendered yet
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// Apply + command copier
// ---------------------------------------------------------------------------
void PropertiesPanel::commit(obs_property_t *prop)
{
	if (!settings_ || !target_)
		return;
	// Lets the source rewrite other properties/visibility (e.g. a mode switch).
	const bool refresh = obs_property_modified(prop, settings_.get());
	obs_source_update(target_.get(), settings_.get());
	lastJson_ = settingsJson(); // our own edit: don't treat it as an external change
	if (refresh)
		scheduleRebuild();
}

void PropertiesPanel::resetProperty(const std::string &name)
{
	if (!settings_ || !target_)
		return;
	obs_data_unset_user_value(settings_.get(), name.c_str()); // falls back to the source's default
	obs_source_update(target_.get(), settings_.get());
	scheduleRebuild();
}

std::pair<ObsRequest, ObsRequest> PropertiesPanel::requestsFor(Json settings) const
{
	if (target_.get() != source_.get()) { // a filter
		const std::string src = str(obs_source_get_name(source_.get()));
		const std::string filter = str(obs_source_get_name(target_.get()));
		return {reqSetSourceFilterSettings(src, filter, std::move(settings), true), reqGetSourceFilter(src, filter)};
	}
	const std::string input = str(obs_source_get_name(source_.get()));
	return {reqSetInputSettings(input, std::move(settings), true), reqGetInputSettings(input)};
}

std::optional<CommandSet> PropertiesPanel::commandsFor(const std::string &name, Json value)
{
	if (!source_ || !target_)
		return std::nullopt;

	Json settings = Json::object();
	settings.set(name, std::move(value));
	auto [set, get] = requestsFor(std::move(settings));

	QPointer<PropertiesPanel> self(this);
	CommandSet c = makeSet(set, get);
	c.reset = [self, name] {
		if (self)
			self->resetProperty(name);
	};
	return c;
}

std::optional<CommandSet> PropertiesPanel::labelCommandsFor(const std::string &name, const std::string &title,
							    Json value)
{
	if (!source_ || !target_)
		return std::nullopt;

	Json one = Json::object();
	one.set(name, std::move(value));
	auto [oneSet, oneGet] = requestsFor(std::move(one));
	// obs_data_get_json holds only user-set values, which is what a Set should carry.
	auto [allSet, allGet] = requestsFor(Json::raw(settingsJson()));

	QPointer<PropertiesPanel> self(this);
	CommandSet c;
	c.choices.push_back(makeChoice(title, oneSet, oneGet));
	c.choices.push_back(makeChoice("All properties (full payload)", allSet, allGet));
	c.reset = [self, name] {
		if (self)
			self->resetProperty(name);
	};
	return c;
}

} // namespace se
