// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Minimal ordered JSON value + serializer. Locale-independent number output.
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace se {

class Json {
public:
	using Array = std::vector<Json>;
	using Object = std::vector<std::pair<std::string, Json>>; // insertion-ordered

	Json() = default; // null
	Json(std::nullptr_t) {}
	Json(bool b) : v_(b) {}
	Json(int i) : v_(static_cast<std::int64_t>(i)) {}
	Json(std::int64_t i) : v_(i) {}
	Json(double d) : v_(d) {}
	Json(const char *s) : v_(std::string(s)) {}
	Json(std::string s) : v_(std::move(s)) {}
	Json(Array a) : v_(std::move(a)) {}
	Json(Object o) : v_(std::move(o)) {}

	static Json object() { return Json(Object{}); }
	static Json array() { return Json(Array{}); }

	// Pre-serialized JSON emitted verbatim (e.g. obs_data_get_json output).
	// The caller guarantees `text` is valid JSON; it is never re-parsed or escaped.
	static Json raw(std::string text);

	// Append/replace a key (objects only). Returns *this for chaining.
	Json &set(std::string key, Json value);
	Json &push(Json value);

	// indent < 0 -> compact; otherwise pretty-printed with that many spaces.
	std::string dump(int indent = -1) const;

private:
	void write(std::string &out, int indent, int depth) const;
	struct Raw {
		std::string text;
	};
	explicit Json(Raw r) : v_(std::move(r)) {}
	std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object, Raw> v_;
};

std::string jsonEscape(const std::string &s);
std::string formatDouble(double d); // shortest round-trip, '.' decimal, never "nan"/"inf"

} // namespace se
