#include "core/json-value.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace se {

std::string jsonEscape(const std::string &s)
{
	std::string out;
	out.reserve(s.size() + 2);
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof buf, "\\u%04x", c);
				out += buf;
			} else {
				out += static_cast<char>(c);
			}
		}
	}
	return out;
}

std::string formatDouble(double d)
{
	if (!std::isfinite(d))
		return "0.0";
	if (d == 0.0)
		return "0.0"; // also normalizes -0
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof buf, d);
	std::string s(buf, res.ptr);
	// Keep doubles typed as doubles ("1.0", not "1") so obs_data stores a float.
	if (s.find_first_of(".eE") == std::string::npos)
		s += ".0";
	return s;
}

Json Json::raw(std::string text)
{
	if (text.empty())
		return Json(Object{}); // empty settings blob -> {}
	return Json(Raw{std::move(text)});
}

Json &Json::set(std::string key, Json value)
{
	if (!std::holds_alternative<Object>(v_))
		v_ = Object{};
	auto &obj = std::get<Object>(v_);
	for (auto &kv : obj) {
		if (kv.first == key) {
			kv.second = std::move(value);
			return *this;
		}
	}
	obj.emplace_back(std::move(key), std::move(value));
	return *this;
}

Json &Json::push(Json value)
{
	if (!std::holds_alternative<Array>(v_))
		v_ = Array{};
	std::get<Array>(v_).push_back(std::move(value));
	return *this;
}

std::string Json::dump(int indent) const
{
	std::string out;
	write(out, indent, 0);
	return out;
}

void Json::write(std::string &out, int indent, int depth) const
{
	const bool pretty = indent >= 0;
	auto nl = [&](int d) {
		if (pretty) {
			out += '\n';
			out.append(static_cast<size_t>(indent * d), ' ');
		}
	};

	if (std::holds_alternative<std::nullptr_t>(v_)) {
		out += "null";
	} else if (auto *b = std::get_if<bool>(&v_)) {
		out += *b ? "true" : "false";
	} else if (auto *i = std::get_if<std::int64_t>(&v_)) {
		out += std::to_string(*i);
	} else if (auto *d = std::get_if<double>(&v_)) {
		out += formatDouble(*d);
	} else if (auto *s = std::get_if<std::string>(&v_)) {
		out += '"';
		out += jsonEscape(*s);
		out += '"';
	} else if (auto *a = std::get_if<Array>(&v_)) {
		if (a->empty()) {
			out += "[]";
			return;
		}
		out += '[';
		for (size_t k = 0; k < a->size(); ++k) {
			if (k)
				out += ',';
			nl(depth + 1);
			(*a)[k].write(out, indent, depth + 1);
		}
		nl(depth);
		out += ']';
	} else if (auto *r = std::get_if<Raw>(&v_)) {
		out += r->text;
	} else if (auto *o = std::get_if<Object>(&v_)) {
		if (o->empty()) {
			out += "{}";
			return;
		}
		out += '{';
		for (size_t k = 0; k < o->size(); ++k) {
			if (k)
				out += ',';
			nl(depth + 1);
			out += '"';
			out += jsonEscape((*o)[k].first);
			out += pretty ? "\": " : "\":";
			(*o)[k].second.write(out, indent, depth + 1);
		}
		nl(depth);
		out += '}';
	}
}

} // namespace se
