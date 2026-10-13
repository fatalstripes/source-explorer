// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/tree-model.hpp"

#include <algorithm>
#include <cctype>

namespace se {

namespace {

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

bool filterInto(const std::vector<TreeNode> &in, const std::string &needle, std::vector<TreeNode> &out)
{
	bool any = false;
	for (const TreeNode &n : in) {
		if (lower(n.label).find(needle) != std::string::npos) {
			out.push_back(n); // label match keeps the whole subtree
			any = true;
			continue;
		}
		TreeNode copy = n;
		copy.children.clear();
		if (filterInto(n.children, needle, copy.children)) {
			out.push_back(std::move(copy));
			any = true;
		}
	}
	return any;
}

void sign(const std::vector<TreeNode> &nodes, std::string &out)
{
	for (const TreeNode &n : nodes) {
		out += n.key;
		out += '\x1f';
		out += n.label;
		out += '\x1f';
		out += char('0' + int(n.kind));
		out += n.visible ? 'v' : '-';
		out += n.locked ? 'l' : '-';
		out += '(';
		sign(n.children, out);
		out += ')';
	}
}

bool leafOk(const TreeNode &n, const TreeFilter &f)
{
	if (!f.typeId.empty() && n.typeId != f.typeId)
		return false;
	if (!f.filterId.empty() && std::find(n.filterIds.begin(), n.filterIds.end(), f.filterId) == n.filterIds.end())
		return false;
	if (f.visibleOnly && !n.visible)
		return false;
	if (f.activeOnly && !n.active)
		return false;
	return true;
}

void narrow(const std::vector<TreeNode> &in, const TreeFilter &f, std::vector<TreeNode> &out)
{
	for (const TreeNode &n : in) {
		switch (n.kind) {
		case NodeKind::Section: {
			TreeNode copy = n;
			copy.children.clear();
			narrow(n.children, f, copy.children);
			if (!copy.children.empty())
				out.push_back(std::move(copy));
			break;
		}
		case NodeKind::GlobalAudio:
		case NodeKind::Item:
			if (f.showSources && leafOk(n, f))
				out.push_back(n);
			break;
		case NodeKind::Group:
		case NodeKind::Scene: {
			if (f.visibleOnly && n.kind == NodeKind::Group && !n.visible)
				break; // a hidden group hides everything in it
			TreeNode copy = n;
			copy.children.clear();
			narrow(n.children, f, copy.children);
			const bool selfMatches = f.restrictsItems() ? leafOk(n, f) : n.kind == NodeKind::Scene;
			// Without a restriction a scene always stays; a group stays when it has matches or itself matches.
			if (!copy.children.empty() || selfMatches)
				out.push_back(std::move(copy));
			break;
		}
		}
	}
}

} // namespace

std::vector<TreeNode> applyTreeFilter(const std::vector<TreeNode> &nodes, const TreeFilter &filter)
{
	std::vector<TreeNode> narrowed;
	narrow(nodes, filter, narrowed);
	return filterTree(narrowed, filter.query);
}

std::vector<TreeNode> filterTree(const std::vector<TreeNode> &nodes, const std::string &query)
{
	const std::string needle = lower(trim(query));
	if (needle.empty())
		return nodes;
	std::vector<TreeNode> out;
	filterInto(nodes, needle, out);
	return out;
}

std::string treeSignature(const std::vector<TreeNode> &nodes)
{
	std::string s;
	sign(nodes, s);
	return s;
}

} // namespace se
